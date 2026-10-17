#include "gen.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ATTEMPTS  12
#define GOOD_ENOUGH   0 /* grade error at which we stop early */

static uint32_t next(Generator *g)
{
	uint32_t x = g->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return g->rng = x ? x : 0x9E3779B9u;
}

static int rnd(Generator *g, int lo, int hi) /* inclusive */
{
	return lo + (int)(next(g) % (uint32_t)(hi - lo + 1));
}

static float frnd(Generator *g)
{
	return (next(g) & 0xFFFFFF) / 16777216.0f;
}

static const char *const ADJ[] = {
	"CRIMSON", "SILENT", "ANGRY", "LAZY", "COSMIC", "FUNKY", "SNEAKY", "GOLDEN",
	"WOBBLY", "ELECTRIC", "SPICY", "FROSTY", "MIGHTY", "SLEEPY", "WILD", "TINY",
};
static const char *const NOUN[] = {
	"CRUX", "DYNO", "GECKO", "LADDER", "ARETE", "MANTLE", "SPIDER", "MONKEY",
	"CANYON", "PEBBLE", "TOWER", "GHOST", "COMET", "CRAB", "RIDGE", "LIZARD",
};

/* Hand hold mix per difficulty band (weights for each HOLD_* type). */
static const uint8_t HAND_MIX[4][HOLD_TYPES] = {
	/* JUG CRIMP SLOPER PINCH FOOT EDGE POCKET UNDER VOLUME */
	{ 55,  0,    0,     10,   0,   35,  0,     0,    0 },  /* V0-1 */
	{ 25,  15,   10,    15,   0,   28,  2,     5,    0 },  /* V2-3 */
	{ 8,   26,   16,    15,   0,   18,  10,    7,    0 },  /* V4-5 */
	{ 2,   32,   20,    14,   0,   10,  17,    5,    0 },  /* V6+  */
};

static int pick_type(Generator *g, float diff)
{
	int band = diff < 2 ? 0 : diff < 4 ? 1 : diff < 6 ? 2 : 3;
	int total = 0;
	for (int t = 0; t < HOLD_TYPES; t++)
		total += HAND_MIX[band][t];
	int k = rnd(g, 0, total - 1);
	for (int t = 0; t < HOLD_TYPES; t++) {
		if (k < HAND_MIX[band][t])
			return t;
		k -= HAND_MIX[band][t];
	}
	return HOLD_JUG;
}

/* Add a hold, nudging it sideways if it would sit on top of another. */
static int place(Route *r, Generator *g, int x, int y, int type, int role)
{
	for (int attempt = 0; attempt < 6; attempt++) {
		bool clear = true;
		for (int i = 0; i < r->nholds; i++) {
			float need = (HOLD_INFO[type].grab + HOLD_INFO[r->holds[i].type].grab) * 0.7f;
			if (hypotf(r->holds[i].x - x, r->holds[i].y - y) < need) {
				clear = false;
				break;
			}
		}
		if (clear) {
			int k = route_add_hold(r, x, y, type);
			if (k >= 0) {
				r->holds[k].role = (uint8_t)role;
				r->holds[k].rot = (uint8_t)rnd(g, 0, 3);
			}
			return k;
		}
		x += (attempt & 1 ? -1 : 1) * (12 + attempt * 6);
		if (x < 20) x = 20 + attempt * 4;
		if (x > WALL_W - 20) x = WALL_W - 20 - attempt * 4;
	}
	return -1;
}

static void layout(Generator *g, Route *r)
{
	float d = g->target + g->bias;
	if (d < 0) d = 0;
	if (d > 11) d = 11;
	int angle = g->angle == GEN_ANY_ANGLE ? rnd(g, 0, ANGLE_COUNT - 1) : g->angle;
	/* Steep walls are harder by themselves, so their holds are spaced closer. */
	d -= angle * 0.8f;
	if (d < 0) d = 0;

	char name[40];
	snprintf(name, sizeof(name), "%s %s", ADJ[rnd(g, 0, 15)], NOUN[rnd(g, 0, 15)]);
	route_init(r, name);
	r->angle = (uint8_t)angle;
	r->color = (uint8_t)rnd(g, 0, ROUTE_COLORS - 2); /* not black */

	float gap = 30.0f + d * 3.4f;
	float lateral = 10.0f + d * 2.6f;
	float footChance = 1.0f - d * 0.06f;
	if (footChance < 0.35f) footChance = 0.35f;
	int footType = d < 2 ? HOLD_EDGE : HOLD_FOOT;

	/* Start: two holds within reach of the mat, two footholds below. */
	int cx = rnd(g, 90, 230);
	int startType = d < 3 ? HOLD_JUG : d < 6 ? HOLD_EDGE : HOLD_CRIMP;
	place(r, g, cx - 14, rnd(g, 584, 592), startType, ROLE_START);
	place(r, g, cx + 14, rnd(g, 582, 590), startType, ROLE_START);
	place(r, g, cx - 22, rnd(g, 648, 658), footType, ROLE_NONE);
	place(r, g, cx + 20, rnd(g, 650, 660), footType, ROLE_NONE);

	float x = cx, y = 586;
	int side = rnd(g, 0, 1) ? 1 : -1;
	while (r->nholds < MAX_HOLDS - 4) {
		float step = gap + (frnd(g) - 0.5f) * 10.0f;
		if (y - step < 75)
			break;
		y -= step;
		x += side * lateral * (0.6f + frnd(g) * 0.6f) + (frnd(g) - 0.5f) * 8.0f;
		if (x < 30) { x = 30; side = 1; }
		if (x > WALL_W - 30) { x = WALL_W - 30; side = -1; }
		if (frnd(g) < 0.75f)
			side = -side;
		place(r, g, (int)x, (int)y, pick_type(g, d), ROLE_NONE);

		/* A foothold below most hand holds. */
		if (frnd(g) < footChance && y + 88 < FLOOR_Y - 30)
			place(r, g, (int)x + rnd(g, -16, 16), (int)y + rnd(g, 78, 96), footType, ROLE_NONE);
		/* Now and then a volume off to one side. */
		if (frnd(g) < 0.07f && y + 40 < FLOOR_Y - 40)
			place(r, g, (int)x + (rnd(g, 0, 1) ? 40 : -40), (int)y + 40, HOLD_VOLUME, ROLE_NONE);
	}
	int topType = d < 5 ? HOLD_JUG : rnd(g, 0, 1) ? HOLD_EDGE : HOLD_SLOPER;
	int tx = (int)(x + (frnd(g) - 0.5f) * lateral);
	place(r, g, tx < 30 ? 30 : tx > WALL_W - 30 ? WALL_W - 30 : tx, rnd(g, 28, 46), topType, ROLE_FINISH);
	if (route_finish(r) < 0) /* nudged away entirely: force one */
		r->holds[r->nholds - 1].role = ROLE_FINISH;
}

void gen_start(Generator *g, int target, int angle, uint32_t seed)
{
	memset(g, 0, sizeof(*g));
	g->target = target;
	g->angle = angle;
	g->rng = seed ? seed : 1;
	g->resultErr = -1;
	g->status = GEN_WORKING;
}

bool gen_work(Generator *g, int tries)
{
	while (g->status == GEN_WORKING && tries > 0) {
		if (!g->solving) {
			if (g->attempts >= MAX_ATTEMPTS) {
				g->status = g->resultErr >= 0 ? GEN_DONE : GEN_FAILED;
				break;
			}
			g->attempts++;
			layout(g, &g->cand);
			beta_start(&g->beta, &g->cand);
			g->solving = true;
		}
		int before = g->beta.tries;
		bool done = beta_work(&g->beta, tries);
		tries -= g->beta.tries - before;
		if (!done) {
			if (g->beta.tries == before)
				tries--; /* always make progress */
			continue;
		}
		g->solving = false;
		if (g->beta.status != BETA_DONE) {
			g->bias -= 0.7f; /* too hard to climb at all: ease off */
			continue;
		}
		int err = abs(g->beta.grade - g->target);
		if (g->resultErr < 0 || err < g->resultErr) {
			g->result = g->cand;
			beta_store(&g->beta, &g->result);
			g->resultErr = err;
		}
		if (err <= GOOD_ENOUGH) {
			g->status = GEN_DONE;
			break;
		}
		g->bias += (g->target - g->beta.grade) * 0.6f;
	}
	return g->status != GEN_WORKING;
}

float gen_progress(const Generator *g)
{
	if (g->status != GEN_WORKING)
		return 1.0f;
	float p = (g->attempts - 1 + (g->solving ? beta_progress(&g->beta) : 0)) / (float)MAX_ATTEMPTS;
	return p < 0 ? 0 : p > 0.99f ? 0.99f : p;
}
