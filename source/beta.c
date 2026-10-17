#include "beta.h"
#include <math.h>
#include <string.h>

#define MAX_REPOSITIONS 12

bool beta_move(Climber *c, int limb, int hold, float *lowest)
{
	if (!climb_pick(c, limb))
		return false;
	const Hold *h = &c->route->holds[hold];
	for (int i = 0; i < BETA_REACH_FRAMES && c->state == CL_CLIMBING; i++) {
		climb_aim(c, h->x, h->y);
		climb_update(c);
		if (lowest && c->stamina < *lowest)
			*lowest = c->stamina;
	}
	climb_release(c);
	for (int i = 0; i < BETA_SETTLE_FRAMES && c->state == CL_CLIMBING; i++) {
		climb_update(c);
		if (lowest && c->stamina < *lowest)
			*lowest = c->stamina;
	}
	return c->state == CL_TOPPED || (c->state == CL_CLIMBING && c->on[limb] == hold);
}

/* Lower is better: how high the limbs are, with a bonus for grip left. */
static float score(const Climber *c)
{
	float s = 0;
	for (int l = 0; l < LIMBS; l++)
		s += c->end[l].y * (limb_is_hand(l) ? 1.0f : 0.7f);
	return s - c->stamina * 20.0f;
}

static uint32_t position_key(const Climber *c)
{
	uint32_t k = 0;
	for (int l = 0; l < LIMBS; l++)
		k = k * 64 + (uint32_t)(c->on[l] + 4);
	return k;
}

static bool seen(const Beta *b, uint32_t key)
{
	for (int i = 0; i < b->nseen; i++)
		if (b->seen[i] == key)
			return true;
	return false;
}

static void reset_search(Beta *b)
{
	b->candLimb = 0;
	b->candHold = 0;
	b->bestLimb = b->bestHold = -1;
	b->bestScore = b->stuck ? 1e30f : score(&b->c) - 4.0f;
}

void beta_start(Beta *b, const Route *r)
{
	memset(b, 0, sizeof(*b));
	b->route = r;
	b->lowest = 1.0f;
	b->grade = GRADE_NONE;
	if (!climb_start(&b->c, r)) {
		b->status = BETA_FAILED;
		return;
	}
	for (int i = 0; i < BETA_START_FRAMES; i++)
		climb_update(&b->c);
	b->seen[b->nseen++] = position_key(&b->c);
	b->status = BETA_WORKING;
	reset_search(b);
}

/* Grade from what the solver's climb took. */
static int compute_grade(Beta *b)
{
	const Route *r = b->route;
	float holds = 0;
	int hands = 0;
	for (int i = 0; i < b->nmoves; i++) {
		if (!limb_is_hand(b->moves[i].limb))
			continue;
		const Hold *h = &r->holds[b->moves[i].hold];
		holds += h->type == HOLD_UNDERCLING ? 1.0f : hold_hand_difficulty(h->type, r->angle);
		hands++;
	}
	if (hands == 0)
		return 0;
	holds /= hands;
	b->holdAvg = holds;
	/* Mostly: how much the climb pumps you out, and how bad the holds are. */
	float v = b->c.spent * 3.0f + b->crux * 8.0f + (1.0f - b->lowest) * 2.5f + (holds - 0.4f) * 2.5f;
	int g = (int)lroundf(v);
	return g < 0 ? 0 : g > 12 ? 12 : g;
}

static void finish(Beta *b, int status)
{
	b->status = status;
	b->grade = status == BETA_DONE ? compute_grade(b) : GRADE_NONE;
}

static void commit(Beta *b)
{
	float before = b->c.spent;
	b->c = b->best;
	b->moves[b->nmoves].limb = (uint8_t)b->bestLimb;
	b->moves[b->nmoves].hold = (uint8_t)b->bestHold;
	b->nmoves++;
	float cost = b->c.spent - before;
	if (cost > b->crux)
		b->crux = cost;
	if (b->bestLow < b->lowest)
		b->lowest = b->bestLow;
	if (b->nseen < MAX_BETA + 1)
		b->seen[b->nseen++] = position_key(&b->c);
	if (b->c.state == CL_TOPPED) {
		finish(b, BETA_DONE);
		return;
	}
	if (b->nmoves >= MAX_BETA) {
		finish(b, BETA_FAILED);
		return;
	}
	b->stuck = false;
	reset_search(b);
}

bool beta_work(Beta *b, int tries)
{
	const Route *r = b->route;
	int top = route_finish(r);
	while (b->status == BETA_WORKING && tries > 0) {
		if (b->candLimb >= LIMBS) {
			/* Every candidate tried. */
			if (b->bestLimb >= 0) {
				if (b->stuck)
					b->repositions++;
				commit(b);
			} else if (!b->stuck && b->repositions < MAX_REPOSITIONS) {
				b->stuck = true;
				reset_search(b);
			} else {
				finish(b, BETA_FAILED);
			}
			continue;
		}
		int l = b->candLimb, i = b->candHold;
		if (++b->candHold >= r->nholds) {
			b->candHold = 0;
			b->candLimb++;
		}
		if (i >= r->nholds)
			continue;

		const Climber *c = &b->c;
		const Hold *h = &r->holds[i];
		bool hand = limb_is_hand(l);
		if (i == c->on[l] || (hand && h->type == HOLD_FOOT) || (!hand && h->role == ROLE_FINISH))
			continue;
		Vec j = climb_joint(c, l);
		if (hypotf(h->x - j.x, h->y - j.y) > limb_reach(l) + 30)
			continue;

		tries--;
		b->tries++;
		Climber t = *c;
		float low = t.stamina;
		if (!beta_move(&t, l, i, &low))
			continue;
		float sc = score(&t);
		int other = l == LIMB_LH ? LIMB_RH : LIMB_LH;
		if (t.state == CL_TOPPED || (hand && i == top && c->on[other] == top))
			sc -= 10000;
		if (b->stuck && seen(b, position_key(&t)))
			continue;
		if (sc < b->bestScore) {
			b->bestScore = sc;
			b->best = t;
			b->bestLimb = l;
			b->bestHold = i;
			b->bestLow = low;
		}
	}
	return b->status != BETA_WORKING;
}

float beta_progress(const Beta *b)
{
	if (b->status != BETA_WORKING)
		return 1.0f;
	/* How far up the wall the hands are. */
	float start = FLOOR_Y - 110, y = fminf(b->c.end[LIMB_LH].y, b->c.end[LIMB_RH].y);
	int top = route_finish(b->route);
	float goal = top >= 0 ? b->route->holds[top].y : 0;
	float p = (start - y) / (start - goal);
	return p < 0 ? 0 : p > 0.98f ? 0.98f : p;
}

void beta_store(const Beta *b, Route *r)
{
	r->grade = (uint8_t)b->grade;
	r->nbeta = b->status == BETA_DONE ? (uint8_t)b->nmoves : 0;
	memcpy(r->beta, b->moves, sizeof(BetaMove) * r->nbeta);
}
