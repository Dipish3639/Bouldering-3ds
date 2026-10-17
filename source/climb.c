#include "climb.h"
#include <math.h>
#include <string.h>

#define GRAVITY     0.6f
#define SOLVE_ITERS 6
#define LEG_REST    42.0f
#define ARM_MIN     10.0f
#define LEG_MIN     16.0f
#define DRAIN_K     0.0036f
#define RECOVER     0.0015f
#define NOTE_FRAMES 100

/* Share of body weight left on the hands with both feet on good holds. */
static const float ANGLE_HAND_BASE[ANGLE_COUNT] = { 0.08f, 0.22f, 0.42f, 0.62f };
/* How well a smear works, per angle (0 = not allowed). */
static const float SMEAR_QUALITY[ANGLE_COUNT] = { 0.7f, 0.35f, 0, 0 };

static Vec v(float x, float y) { Vec r = { x, y }; return r; }
static float len(Vec a) { return sqrtf(a.x * a.x + a.y * a.y); }

bool limb_is_hand(int limb) { return limb == LIMB_LH || limb == LIMB_RH; }
float limb_reach(int limb) { return limb_is_hand(limb) ? ARM_REACH : LEG_REACH; }

static Vec joint_at(Vec body, int limb)
{
	switch (limb) {
	case LIMB_LH: return v(body.x - SHOULDER_DX, body.y + SHOULDER_DY);
	case LIMB_RH: return v(body.x + SHOULDER_DX, body.y + SHOULDER_DY);
	case LIMB_LF: return v(body.x - HIP_DX, body.y + HIP_DY);
	default:      return v(body.x + HIP_DX, body.y + HIP_DY);
	}
}

Vec climb_joint(const Climber *c, int limb)
{
	return joint_at(c->body, limb);
}

Vec climb_bend(const Climber *c, int limb)
{
	Vec j = climb_joint(c, limb), p = c->end[limb];
	float seg = limb_is_hand(limb) ? ARM_SEG : LEG_SEG;
	Vec d = v(p.x - j.x, p.y - j.y);
	float full = len(d);
	if (full < 0.01f)
		return v(j.x, j.y + seg);
	Vec dir = v(d.x / full, d.y / full);
	float l = full > seg * 2 - 0.01f ? seg * 2 - 0.01f : full;
	/* Both segments are the same length, so the bend sits halfway along. */
	float along = l * 0.5f;
	float out = sqrtf(seg * seg - along * along);
	/* Elbows point out and down; knees point out to the sides. */
	bool left = limb == LIMB_LH || limb == LIMB_LF;
	float s = limb_is_hand(limb) ? (left ? -1.0f : 1.0f) : (left ? 1.0f : -1.0f);
	Vec perp = v(-dir.y, dir.x);
	return v(j.x + dir.x * along + perp.x * out * s, j.y + dir.y * along + perp.y * out * s);
}

static bool attached(const Climber *c, int limb)
{
	return c->on[limb] != ON_NOTHING;
}

static void note(Climber *c, const char *s)
{
	c->note = s;
	c->noteTimer = NOTE_FRAMES;
}

/* Keep `limb`'s joint between min and max distance of its end. Moves the body. */
static void constrain(Climber *c, int limb, float minD, float maxD)
{
	Vec j = joint_at(c->body, limb), p = c->end[limb];
	Vec d = v(j.x - p.x, j.y - p.y);
	float l = len(d);
	if (l < 0.001f)
		return;
	float want = l > maxD ? maxD : l < minD ? minD : l;
	if (want == l)
		return;
	float k = (want - l) / l;
	c->body.x += d.x * k;
	c->body.y += d.y * k;
}

static void solve(Climber *c, int iters)
{
	for (int it = 0; it < iters; it++) {
		c->body.y += GRAVITY;

		/* Feet on the floor stay under the hips. */
		for (int f = LIMB_LF; f <= LIMB_RF; f++)
			if (c->on[f] == ON_FLOOR)
				c->end[f] = v(c->body.x + (f == LIMB_LF ? -9.0f : 9.0f), FLOOR_Y);

		/* Legs stand the body up over its footholds. */
		for (int f = LIMB_LF; f <= LIMB_RF; f++) {
			if (!attached(c, f))
				continue;
			Vec j = joint_at(c->body, f), p = c->end[f];
			c->body.x += (p.x - j.x) * 0.04f;
			c->body.y += (p.y - LEG_REST - j.y) * 0.12f;
		}

		/* Hang plumb under whatever we're holding. */
		float cx = 0;
		int n = 0;
		for (int l = 0; l < LIMBS; l++)
			if (attached(c, l)) {
				cx += c->end[l].x;
				n++;
			}
		if (n)
			c->body.x += (cx / n - c->body.x) * 0.05f;

		/* Lean towards the limb that's reaching. */
		if (c->moving >= 0) {
			Vec j = joint_at(c->body, c->moving);
			Vec d = v(c->target.x - j.x, c->target.y - j.y);
			float l = len(d), r = limb_reach(c->moving) * 0.92f;
			if (l > r) {
				float step = l - r > 3.0f ? 3.0f : l - r;
				c->body.x += d.x / l * step;
				c->body.y += d.y / l * step;
			}
		}

		/* Hard limits last; hands after feet so the grip always wins. */
		static const int ORDER[LIMBS] = { LIMB_LF, LIMB_RF, LIMB_LH, LIMB_RH };
		for (int i = 0; i < LIMBS; i++) {
			int l = ORDER[i];
			if (!attached(c, l))
				continue;
			if (limb_is_hand(l))
				constrain(c, l, ARM_MIN, ARM_REACH);
			else
				constrain(c, l, LEG_MIN, LEG_REACH);
		}
	}
}

static Vec hold_pos(const Route *r, int h)
{
	return v(r->holds[h].x, r->holds[h].y);
}

bool climb_start(Climber *c, const Route *r)
{
	memset(c, 0, sizeof(*c));
	c->route = r;
	c->moving = -1;
	c->targetHold = -1;
	c->stamina = 1.0f;
	if (route_problem(r))
		return false;
	int s[MAX_START];
	int n = route_starts(r, s);
	c->on[LIMB_LH] = s[0];
	c->on[LIMB_RH] = s[n - 1];
	c->end[LIMB_LH] = hold_pos(r, s[0]);
	c->end[LIMB_RH] = hold_pos(r, s[n - 1]);
	c->on[LIMB_LF] = c->on[LIMB_RF] = ON_FLOOR;
	c->body = v((c->end[LIMB_LH].x + c->end[LIMB_RH].x) * 0.5f, FLOOR_Y - 64);
	solve(c, 80);
	for (int h = LIMB_LH; h <= LIMB_RH; h++) {
		Vec j = climb_joint(c, h);
		if (len(v(c->end[h].x - j.x, c->end[h].y - j.y)) > ARM_REACH + 1.5f)
			return false;
	}
	return true;
}

static int hands_on(const Climber *c)
{
	return attached(c, LIMB_LH) + attached(c, LIMB_RH);
}

bool climb_pick(Climber *c, int limb)
{
	if (c->state != CL_CLIMBING)
		return false;
	if (c->moving == limb)
		return true;
	if (c->moving >= 0)
		climb_release(c);
	if (limb_is_hand(limb)) {
		int other = limb == LIMB_LH ? LIMB_RH : LIMB_LH;
		if (!attached(c, other)) {
			note(c, "HOLD ON WITH THE OTHER HAND");
			return false;
		}
	}
	c->moving = limb;
	c->prevOn = c->on[limb];
	c->prevEnd = c->end[limb];
	c->on[limb] = ON_NOTHING;
	c->target = c->end[limb];
	c->targetHold = -1;
	return true;
}

void climb_aim(Climber *c, float x, float y)
{
	c->target = v(x, y);
}

/* The hold a limb at `p` would grab, or -1. */
static int grab_candidate(const Climber *c, int limb, Vec p)
{
	const Route *r = c->route;
	Vec j = climb_joint(c, limb);
	float reach = limb_reach(limb) + 2.0f;
	int best = -1;
	float bestD = 1e9f;
	for (int i = 0; i < r->nholds; i++) {
		Vec h = hold_pos(r, i);
		float d = len(v(h.x - p.x, h.y - p.y));
		if (d > HOLD_INFO[r->holds[i].type].grab + 4.0f || d >= bestD)
			continue;
		if (len(v(h.x - j.x, h.y - j.y)) > reach)
			continue;
		best = i;
		bestD = d;
	}
	return best;
}

void climb_release(Climber *c)
{
	int limb = c->moving;
	if (limb < 0)
		return;
	c->moving = -1;
	c->targetHold = -1;
	const Route *r = c->route;
	bool hand = limb_is_hand(limb);
	int h = grab_candidate(c, limb, c->end[limb]);
	if (h >= 0) {
		c->on[limb] = h;
		c->end[limb] = hold_pos(r, h);
		if (h != c->prevOn)
			c->moves++;
		c->lastGrabType = r->holds[h].type;
		if (hand) {
			c->grabEvent++;
			if (r->holds[h].type == HOLD_FOOT)
				note(c, "THAT'S A FOOTHOLD!");
		} else {
			c->footEvent++;
		}
	} else if (!hand && SMEAR_QUALITY[r->angle] > 0 && c->end[limb].y < FLOOR_Y - 4) {
		c->on[limb] = ON_SMEAR;
		c->footEvent++;
		c->lastGrabType = -1;
		if (c->prevOn != ON_SMEAR)
			c->moves++;
	} else {
		/* Nothing there: go back. A hand always finds its old hold again (the
		 * body just swings back); a foot only if it's still in reach. */
		Vec j = climb_joint(c, limb);
		float back = len(v(c->prevEnd.x - j.x, c->prevEnd.y - j.y));
		if (c->prevOn != ON_NOTHING && (hand || back <= limb_reach(limb) + 2.0f)) {
			c->on[limb] = c->prevOn;
			c->end[limb] = c->prevEnd;
		}
		if (hand) {
			c->missEvent++;
			note(c, "MISSED");
		}
	}
	if (c->on[LIMB_LF] != ON_FLOOR && c->on[LIMB_RF] != ON_FLOOR)
		c->leftGround = true;
}

static void start_fall(Climber *c, const char *why)
{
	if (c->moving >= 0) {
		c->on[c->moving] = ON_NOTHING;
		c->moving = -1;
	}
	for (int l = 0; l < LIMBS; l++)
		c->on[l] = ON_NOTHING;
	c->state = CL_FALLING;
	c->vel = v(0, 0);
	c->fallEvent++;
	c->fallTimer = 0;
	note(c, why);
}

/* Unattached limbs hang down and swing towards rest. */
static void dangle(Climber *c)
{
	for (int l = 0; l < LIMBS; l++) {
		if (attached(c, l) || l == c->moving)
			continue;
		Vec j = climb_joint(c, l);
		bool left = l == LIMB_LH || l == LIMB_LF;
		Vec rest = limb_is_hand(l)
			? v(j.x + (left ? -6.0f : 6.0f), j.y + ARM_REACH * 0.85f)
			: v(j.x + (left ? -5.0f : 5.0f), j.y + LEG_REACH * 0.9f);
		if (c->state == CL_FALLING) /* limbs flail upwards a little */
			rest = limb_is_hand(l) ? v(j.x + (left ? -20.0f : 20.0f), j.y - 30.0f)
			                       : v(j.x + (left ? -12.0f : 12.0f), j.y + 40.0f);
		c->end[l].x += (rest.x - c->end[l].x) * 0.25f;
		c->end[l].y += (rest.y - c->end[l].y) * 0.25f;
		if (c->end[l].y > FLOOR_Y)
			c->end[l].y = FLOOR_Y;
	}
}

float climb_hand_difficulty(const Climber *c, int hand)
{
	const Hold *h = &c->route->holds[c->on[hand]];
	if (h->type != HOLD_UNDERCLING)
		return hold_hand_difficulty(h->type, c->route->angle);
	/* An undercling is pulled upwards: great once the shoulder is well above
	 * it, useless when reaching up to it. */
	float above = h->y - climb_joint(c, hand).y; /* > 0: hold below the shoulder */
	float d = 1.6f - above * 0.035f;
	return d < 0.45f ? 0.45f : d > 2.8f ? 2.8f : d;
}

static void update_stamina(Climber *c)
{
	const Route *r = c->route;
	float feet = 0;
	for (int f = LIMB_LF; f <= LIMB_RF; f++) {
		int on = c->on[f];
		if (on == ON_FLOOR)
			feet += 1.0f;
		else if (on == ON_SMEAR)
			feet += SMEAR_QUALITY[r->angle];
		else if (on >= 0)
			feet += HOLD_INFO[r->holds[on].type].foot * (r->angle == ANGLE_ROOF ? 0.8f : 1.0f);
	}
	if (feet > 2)
		feet = 2;
	float base = ANGLE_HAND_BASE[r->angle];
	float load = base + (1.0f - base) * (2.0f - feet) * 0.5f;
	int nh = hands_on(c);
	float sum = 0;
	for (int h = LIMB_LH; h <= LIMB_RH; h++) {
		c->handLoad[h] = 0;
		if (!attached(c, h))
			continue;
		c->handLoad[h] = load / nh;
		sum += c->handLoad[h] * climb_hand_difficulty(c, h);
	}
	c->drain = DRAIN_K * sum - RECOVER;
	if (!c->leftGround && c->on[LIMB_LF] == ON_FLOOR && c->on[LIMB_RF] == ON_FLOOR)
		c->drain = -RECOVER * 2; /* still standing on the mat */
	c->stamina -= c->drain;
	if (c->drain > 0)
		c->spent += c->drain;
	if (c->stamina > 1)
		c->stamina = 1;
	if (c->stamina <= 0) {
		c->stamina = 0;
		start_fall(c, "PUMPED OUT!");
	}
}

void climb_update(Climber *c)
{
	if (c->noteTimer > 0 && --c->noteTimer == 0)
		c->note = NULL;

	if (c->state == CL_FALLING) {
		c->vel.y += 0.45f;
		c->body.x += c->vel.x;
		c->body.y += c->vel.y;
		c->fallTimer++;
		dangle(c);
		float feetY = c->body.y + HIP_DY + LEG_REACH * 0.85f;
		if (feetY >= FLOOR_Y) {
			c->body.y = FLOOR_Y - HIP_DY - LEG_REACH * 0.85f;
			c->end[LIMB_LF] = v(c->body.x - 12, FLOOR_Y);
			c->end[LIMB_RF] = v(c->body.x + 12, FLOOR_Y);
			c->state = CL_FELL;
			c->landEvent++;
		}
		return;
	}
	if (c->state != CL_CLIMBING)
		return;

	c->frames++;

	/* While reaching, the limb follows the aim point but can't overstretch. */
	solve(c, SOLVE_ITERS);
	if (c->moving >= 0) {
		Vec j = climb_joint(c, c->moving);
		Vec d = v(c->target.x - j.x, c->target.y - j.y);
		float l = len(d), r = limb_reach(c->moving);
		Vec p = c->target;
		if (l > r)
			p = v(j.x + d.x / l * r, j.y + d.y / l * r);
		if (p.y > FLOOR_Y)
			p.y = FLOOR_Y;
		c->end[c->moving].x += (p.x - c->end[c->moving].x) * 0.5f;
		c->end[c->moving].y += (p.y - c->end[c->moving].y) * 0.5f;
		c->targetHold = grab_candidate(c, c->moving, c->end[c->moving]);
	}

	/* A foot stretched past its reach pops off; standing feet just lift. */
	for (int f = LIMB_LF; f <= LIMB_RF; f++) {
		if (!attached(c, f))
			continue;
		Vec j = climb_joint(c, f);
		if (len(v(c->end[f].x - j.x, c->end[f].y - j.y)) > LEG_REACH + 3.0f) {
			bool floor = c->on[f] == ON_FLOOR;
			c->on[f] = ON_NOTHING;
			if (!floor) {
				c->slipEvent++;
				note(c, "FOOT SLIPPED");
			}
		}
	}
	if (c->on[LIMB_LF] != ON_FLOOR && c->on[LIMB_RF] != ON_FLOOR)
		c->leftGround = true;
	dangle(c);

	if (hands_on(c) == 0) {
		start_fall(c, "LET GO!");
		return;
	}

	int top = route_finish(c->route);
	if (top >= 0 && c->on[LIMB_LH] == top && c->on[LIMB_RH] == top) {
		c->state = CL_TOPPED;
		c->topEvent++;
		c->note = NULL;
		return;
	}
	if (top >= 0 && (c->on[LIMB_LH] == top || c->on[LIMB_RH] == top) && !c->note)
		note(c, "MATCH THE TOP HOLD!");

	update_stamina(c);
}
