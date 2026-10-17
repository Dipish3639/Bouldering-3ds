#ifndef BETA_H
#define BETA_H

#include <stdbool.h>
#include "climb.h"
#include "route.h"

/* The beta solver: finds a sequence of moves up a route by trying every
 * limb on every nearby hold through the real climbing code, keeping the move
 * that gains the most height for the least grip. The moves it finds also
 * give the route its grade.
 *
 * Every move is made the same way, so replaying the beta with the same
 * timing gives exactly the same climb. */

#define BETA_START_FRAMES  30 /* standing on the mat before the first move */
#define BETA_REACH_FRAMES  25 /* limb picked up and moving to the hold */
#define BETA_SETTLE_FRAMES 35 /* after grabbing, before the next move */

enum { BETA_WORKING, BETA_DONE, BETA_FAILED };

typedef struct {
	const Route *route;
	int status;
	Climber c;                  /* where the moves found so far lead */
	BetaMove moves[MAX_BETA];
	int nmoves;

	/* Search for the next move, a few candidates per beta_work call. */
	int candLimb, candHold;
	bool stuck;                 /* no move gains height: reposition instead */
	int repositions;
	Climber best;
	float bestScore, bestLow;
	int bestLimb, bestHold;
	uint32_t seen[MAX_BETA + 1];/* limb positions already visited */
	int nseen;
	int tries;

	/* What the climb took, for the grade. */
	float crux;                 /* most grip spent on any one move */
	float lowest;               /* lowest grip reached */
	float holdAvg;              /* average difficulty of the hand holds used */
	int grade;                  /* 0..12, or GRADE_NONE */
} Beta;

void beta_start(Beta *b, const Route *r);
/* Try up to `tries` candidate moves. Returns true once finished. */
bool beta_work(Beta *b, int tries);
/* Rough progress 0..1 for a progress bar. */
float beta_progress(const Beta *b);
/* Store the result in the route (grade and moves). */
void beta_store(const Beta *b, Route *r);

/* Make one beta move: pick the limb, reach for the hold, let go, settle.
 * Returns false if the limb didn't end up there (or the climber fell). */
bool beta_move(Climber *c, int limb, int hold, float *lowest);

#endif
