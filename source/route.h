#ifndef ROUTE_H
#define ROUTE_H

#include <stdbool.h>
#include <stdint.h>

/* The wall, in world pixels. It is exactly as wide as the bottom screen. */
#define WALL_W   320
#define WALL_H   720
#define FLOOR_Y  696 /* top of the crash pad */
#define HOLD_MAX_Y (FLOOR_Y - 8)

#define MAX_HOLDS  48
#define MAX_ROUTES 12
#define NAME_LEN   16
#define MAX_START  2

/* New types go at the end: saved routes store these numbers. */
enum {
	HOLD_JUG, HOLD_CRIMP, HOLD_SLOPER, HOLD_PINCH, HOLD_FOOT,
	HOLD_EDGE, HOLD_POCKET, HOLD_UNDERCLING, HOLD_VOLUME,
	HOLD_TYPES
};
enum { ROLE_NONE, ROLE_START, ROLE_FINISH };
enum { ANGLE_SLAB, ANGLE_VERT, ANGLE_OVERHANG, ANGLE_ROOF, ANGLE_COUNT };
#define ROUTE_COLORS 8

typedef struct {
	int16_t x, y;
	uint8_t type, role, rot, pad;
} Hold;

#define MAX_BETA 96

/* One move of a route's beta: put a limb (LIMB_*) on a hold. */
typedef struct {
	uint8_t limb, hold;
} BetaMove;

#define GRADE_UNKNOWN 0xFF /* not worked out yet */
#define GRADE_NONE    0xFE /* the solver found no way up */

typedef struct {
	char name[NAME_LEN + 1];
	uint8_t color, angle, nholds;
	Hold holds[MAX_HOLDS];
	uint32_t bestFrames; /* 0 = never sent */
	uint16_t sends, attempts;
	/* Added in save version 2: the solver's grade and the moves it found. */
	uint8_t grade, nbeta, pad[2];
	BetaMove beta[MAX_BETA];
} Route;

typedef struct {
	int count;
	Route routes[MAX_ROUTES];
} RouteBook;

typedef struct {
	const char *name;
	float hand;   /* how hard it is to hang on with a hand (higher = harder) */
	float foot;   /* how much weight a foot on it takes off the hands (0..1) */
	float grab;   /* snap radius when grabbing it */
} HoldInfo;

extern const HoldInfo HOLD_INFO[HOLD_TYPES];
extern const char *const ANGLE_NAMES[ANGLE_COUNT];
extern const uint32_t ROUTE_COLOR[ROUTE_COLORS];
extern const char *const COLOR_NAMES[ROUTE_COLORS];

/* Hand difficulty of a hold on a given wall angle (slopers get worse when steep). */
float hold_hand_difficulty(int type, int angle);

void route_init(Route *r, const char *name);
/* Returns the index of the new hold, or -1 when the route is full. */
int  route_add_hold(Route *r, int x, int y, int type);
void route_remove_hold(Route *r, int i);
/* Hold at a point (closest within its radius plus `slack`), or -1. */
int  route_hold_at(const Route *r, float x, float y, float slack);
int  route_count_role(const Route *r, int role);
/* Start holds sorted left to right; returns how many there are. */
int  route_starts(const Route *r, int out[MAX_START]);
int  route_finish(const Route *r);
/* NULL when the route can be climbed, else what is missing. */
const char *route_problem(const Route *r);
/* Quick V grade guess (0..12) from the holds alone, for the editor. */
int  route_grade(const Route *r);
/* Forget the grade and beta (after the holds change). */
void route_clear_beta(Route *r);
/* "V4", "V?" (not graded yet) or "V??" (no way up found). */
void grade_text(char *out, int size, int grade);

void book_samples(RouteBook *b);
bool book_load(RouteBook *b, const char *path);
bool book_save(const RouteBook *b, const char *path);
void book_delete(RouteBook *b, int i);

#endif
