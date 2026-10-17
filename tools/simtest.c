/* Host-side check that every built-in route can be climbed: a greedy bot
 * makes moves through the same climb_* calls the game uses.
 * Build and run: tools/simtest.sh */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "climb.h"

static void run(Climber *c, int frames)
{
	for (int i = 0; i < frames && c->state == CL_CLIMBING; i++)
		climb_update(c);
}

/* Try to move `limb` onto hold h; returns true if it ended up there. */
static bool try_move(Climber *c, int limb, int h)
{
	Climber save = *c;
	if (!climb_pick(c, limb))
		return false;
	const Hold *hd = &c->route->holds[h];
	for (int i = 0; i < 25 && c->state == CL_CLIMBING; i++) {
		climb_aim(c, hd->x, hd->y);
		climb_update(c);
	}
	climb_release(c);
	run(c, 35); /* settle, plus a human-ish pause before the next move */
	if (c->state == CL_TOPPED || (c->state == CL_CLIMBING && c->on[limb] == h))
		return true;
	*c = save;
	return false;
}

/* Lower is better: how far up the wall the four limbs are. */
static float height_score(const Climber *c)
{
	float s = 0;
	for (int l = 0; l < LIMBS; l++)
		s += c->end[l].y * (limb_is_hand(l) ? 1.0f : 0.7f);
	return s;
}

static int climb_route(const Route *r, bool verbose)
{
	Climber c;
	if (!climb_start(&c, r)) {
		printf("  cannot start: %s\n", route_problem(r) ? route_problem(r) : "start out of reach");
		return 0;
	}
	run(&c, 30);
	int top = route_finish(r);
	for (int step = 0; step < 200 && c.state == CL_CLIMBING; step++) {
		/* Try every limb on every nearby hold and keep the move that gains most. */
		Climber best = c;
		float bestScore = height_score(&c) - 4.0f;
		int bestLimb = -1, bestHold = -1;
		for (int l = 0; l < LIMBS; l++) {
			bool hand = limb_is_hand(l);
			int other = l == LIMB_LH ? LIMB_RH : LIMB_LH;
			for (int i = 0; i < r->nholds; i++) {
				const Hold *h = &r->holds[i];
				if (i == c.on[l] || (hand && h->type == HOLD_FOOT) || (!hand && h->role == ROLE_FINISH))
					continue;
				Vec j = climb_joint(&c, l);
				if (hypotf(h->x - j.x, h->y - j.y) > limb_reach(l) + 30)
					continue;
				Climber t = c;
				if (!try_move(&t, l, i))
					continue;
				float sc = height_score(&t);
				if (t.state == CL_TOPPED || (hand && c.on[other] == top && i == top))
					sc -= 10000;
				sc -= t.stamina * 20; /* prefer restful positions */
				if (sc < bestScore) {
					bestScore = sc;
					best = t;
					bestLimb = l;
					bestHold = i;
				}
			}
		}
		if (bestLimb < 0) {
			printf("  stuck at hands y=%.0f/%.0f  stamina %.2f\n", c.end[0].y, c.end[1].y, c.stamina);
			return 0;
		}
		c = best;
		if (verbose)
			printf("  %s -> hold %d (%s, y %d)  stamina %.2f\n",
			       (const char *[]){ "LH", "RH", "LF", "RF" }[bestLimb], bestHold,
			       HOLD_INFO[r->holds[bestHold].type].name, r->holds[bestHold].y, c.stamina);
	}
	if (c.state == CL_TOPPED) {
		printf("  TOPPED in %d moves, %.1f s, stamina left %.2f\n", c.moves, c.frames / 60.0, c.stamina);
		return 1;
	}
	printf("  failed: state %d (%s)\n", c.state, c.note ? c.note : "");
	return 0;
}

int main(int argc, char **argv)
{
	bool verbose = argc > 1;
	RouteBook b;
	book_samples(&b);
	int ok = 0;
	for (int i = 0; i < b.count; i++) {
		printf("%s (%s, V%d):\n", b.routes[i].name, ANGLE_NAMES[b.routes[i].angle], route_grade(&b.routes[i]));
		ok += climb_route(&b.routes[i], verbose);
	}

	/* Save / load round trip. */
	const char *path = "/tmp/bouldering3ds_test.dat";
	RouteBook back;
	bool saved = book_save(&b, path) && book_load(&back, path) && back.count == b.count &&
	             memcmp(back.routes, b.routes, sizeof(Route) * b.count) == 0;
	printf("save/load: %s\n", saved ? "ok" : "FAILED");
	return ok == b.count && saved ? 0 : 1;
}
