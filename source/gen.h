#ifndef GEN_H
#define GEN_H

#include <stdbool.h>
#include <stdint.h>
#include "beta.h"
#include "route.h"

/* Route generator: lays out a line of holds for a target grade, then runs
 * the beta solver on it. Routes that can't be climbed are thrown away, and
 * the difficulty is nudged until the solver's grade matches the target. */

enum { GEN_WORKING, GEN_DONE, GEN_FAILED };

#define GEN_ANY_ANGLE (-1)

typedef struct {
	int target, angle;
	uint32_t rng;
	int status;
	int attempts;
	float bias;          /* added to the target when laying out holds */
	bool solving;
	Route cand;
	Beta beta;
	Route result;        /* best climbable route so far */
	int resultErr;       /* how far its grade is from the target, -1 = none yet */
} Generator;

void gen_start(Generator *g, int target, int angle, uint32_t seed);
/* Spend up to `tries` solver tries. Returns true once finished. */
bool gen_work(Generator *g, int tries);
float gen_progress(const Generator *g);

#endif
