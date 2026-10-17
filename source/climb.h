#ifndef CLIMB_H
#define CLIMB_H

#include <stdbool.h>
#include "route.h"

/* Platform-independent climber: four limbs that grab holds, a body that
 * settles between them under gravity, and a stamina meter. */

enum { LIMB_LH, LIMB_RH, LIMB_LF, LIMB_RF, LIMBS };

/* What a limb is on, besides a hold index. */
#define ON_NOTHING (-1)
#define ON_FLOOR   (-2) /* feet, before leaving the ground */
#define ON_SMEAR   (-3) /* a foot pressed on blank wall (slab / vertical only) */

enum { CL_CLIMBING, CL_FALLING, CL_FELL, CL_TOPPED };

/* Body geometry, relative to the torso centre. */
#define SHOULDER_DX 10.0f
#define SHOULDER_DY (-19.0f)
#define HIP_DX      7.0f
#define HIP_DY      16.0f
#define HEAD_DY     (-33.0f)
#define ARM_SEG     23.5f /* upper arm and forearm */
#define LEG_SEG     27.5f /* thigh and shin */
#define ARM_REACH   (ARM_SEG * 2 - 1)
#define LEG_REACH   (LEG_SEG * 2 - 1)

typedef struct { float x, y; } Vec;

typedef struct {
	const Route *route;
	int state;
	Vec body;
	Vec vel;          /* only while falling */
	Vec end[LIMBS];   /* where each hand / foot is drawn */
	int on[LIMBS];    /* hold index or ON_* */
	int moving;       /* limb being placed, or -1 */
	int prevOn;       /* where the moving limb was, to go back on a miss */
	Vec prevEnd;
	Vec target;       /* where the moving limb is reaching */
	int targetHold;   /* hold the moving limb would grab, or -1 */
	float stamina;    /* 0..1 */
	float handLoad[2];/* share of body weight each hand holds (for the HUD) */
	float drain;      /* stamina change per frame, negative when recovering */
	float spent;      /* total stamina drained so far (recovery not counted) */
	bool leftGround;
	int frames, moves;
	int fallTimer;
	/* Event counters: the front end compares them with the last value it saw. */
	int grabEvent, footEvent, missEvent, slipEvent, fallEvent, landEvent, topEvent;
	int lastGrabType;
	const char *note; /* short hint for the HUD, may be NULL */
	int noteTimer;
} Climber;

/* Returns false when the route can't be started (see route_problem). */
bool climb_start(Climber *c, const Route *r);

/* Pick up a limb to place it. Returns false when that isn't allowed
 * (e.g. it is the only hand on the wall). */
bool climb_pick(Climber *c, int limb);
/* Where the moving limb is reaching for (world coordinates). */
void climb_aim(Climber *c, float x, float y);
/* Let go of the moving limb: it grabs whatever is under it, or goes back. */
void climb_release(Climber *c);

void climb_update(Climber *c);

/* Joint the limb hangs from (shoulder or hip) for the current body position. */
Vec climb_joint(const Climber *c, int limb);
/* Elbow / knee position for drawing. */
Vec climb_bend(const Climber *c, int limb);
float limb_reach(int limb);
/* How hard the hold under a hand is to hang on to right now. */
float climb_hand_difficulty(const Climber *c, int hand);
bool limb_is_hand(int limb);

#endif
