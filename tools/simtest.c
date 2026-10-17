/* Host-side checks: the beta solver climbs every built-in route (and the
 * moves it found replay to the top), plus a save / load round trip.
 * Build and run: tools/simtest.sh [-v] */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "beta.h"
#include "gen.h"

static const char *LIMB_NAME[LIMBS] = { "LH", "RH", "LF", "RF" };

/* Solve a route, print what was found; returns true if it was climbed and
 * the replay agrees. */
static bool solve(Route *r, bool verbose)
{
	static Beta b;
	clock_t t0 = clock();
	beta_start(&b, r);
	while (!beta_work(&b, 1000))
		;
	double ms = (clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
	char g[8];
	grade_text(g, sizeof(g), b.grade);
	printf("  %-4s %2d moves  spent %.2f crux %.3f lowest %.2f holds %.2f  (%d tries, %.0f ms)\n",
	       g, b.nmoves, b.c.spent, b.crux, b.lowest, b.holdAvg, b.tries, ms);
	if (verbose)
		for (int i = 0; i < b.nmoves; i++)
			printf("    %2d %s -> %s (y %d)\n", i + 1, LIMB_NAME[b.moves[i].limb],
			       HOLD_INFO[r->holds[b.moves[i].hold].type].name, r->holds[b.moves[i].hold].y);
	if (b.status != BETA_DONE)
		return false;
	beta_store(&b, r);

	/* Replaying the stored moves must reach the top the same way. */
	Climber c;
	climb_start(&c, r);
	for (int i = 0; i < BETA_START_FRAMES; i++)
		climb_update(&c);
	for (int i = 0; i < r->nbeta; i++)
		beta_move(&c, r->beta[i].limb, r->beta[i].hold, NULL);
	if (c.state != CL_TOPPED) {
		printf("  replay did not top out\n");
		return false;
	}
	return true;
}

int main(int argc, char **argv)
{
	bool verbose = argc > 1;
	static RouteBook b;
	book_samples(&b);
	int ok = 0, total = 0;
	for (int i = 0; i < b.count; i++) {
		printf("%s (%s):\n", b.routes[i].name, ANGLE_NAMES[b.routes[i].angle]);
		ok += solve(&b.routes[i], verbose);
		total++;
	}

	/* Generated routes: every one must be climbable. */
	int genOk = 0, genTotal = 0, off = 0;
	for (int target = 0; target <= 8; target += 2)
		for (int angle = 0; angle < ANGLE_COUNT; angle++) {
			static Route r;
			static Generator gen;
			gen_start(&gen, target, angle, 1234u + target * 7 + angle);
			while (!gen_work(&gen, 1000))
				;
			genTotal++;
			if (gen.status != GEN_DONE) {
				printf("gen V%d %s: FAILED\n", target, ANGLE_NAMES[angle]);
				continue;
			}
			r = gen.result;
			genOk++;
			off += abs((int)r.grade - target);
			printf("gen V%d %-8s -> %-16s V%d, %d holds, %d moves, %d attempts\n", target, ANGLE_NAMES[angle],
			       r.name, r.grade, r.nholds, r.nbeta, gen.attempts);
		}
	printf("generated: %d/%d climbable, average grade error %.2f\n", genOk, genTotal, genOk ? (double)off / genOk : 0.0);

	/* Save / load round trip. */
	const char *path = "/tmp/bouldering3ds_test.dat";
	static RouteBook back;
	bool saved = book_save(&b, path) && book_load(&back, path) && back.count == b.count &&
	             memcmp(back.routes, b.routes, sizeof(Route) * b.count) == 0;
	printf("save/load: %s\n", saved ? "ok" : "FAILED");

	/* A save from version 1 (no grades or beta) still loads, ungraded. */
	struct { uint32_t magic; uint16_t version, routeSize; uint32_t count; } h1 = { 0x52444C42u, 1, 0, 1 };
	size_t v1Size = offsetof(Route, grade);
	h1.routeSize = (uint16_t)v1Size;
	FILE *f = fopen(path, "wb");
	fwrite(&h1, sizeof(h1), 1, f);
	fwrite(&b.routes[1], v1Size, 1, f);
	fclose(f);
	bool v1 = book_load(&back, path) && back.count == 1 && strcmp(back.routes[0].name, b.routes[1].name) == 0 &&
	          back.routes[0].nholds == b.routes[1].nholds && back.routes[0].grade == GRADE_UNKNOWN &&
	          back.routes[0].nbeta == 0;
	printf("version 1 save: %s\n", v1 ? "ok" : "FAILED");
	saved = saved && v1;
	return ok == total && genOk == genTotal && saved ? 0 : 1;
}
