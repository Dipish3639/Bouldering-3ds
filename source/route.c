#include "route.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const HoldInfo HOLD_INFO[HOLD_TYPES] = {
	[HOLD_JUG]    = { "JUG",    0.35f, 1.00f, 12 },
	[HOLD_CRIMP]  = { "CRIMP",  1.25f, 0.85f, 9 },
	[HOLD_SLOPER] = { "SLOPER", 1.00f, 0.60f, 13 },
	[HOLD_PINCH]  = { "PINCH",  0.85f, 0.70f, 10 },
	[HOLD_FOOT]   = { "FOOT",   3.00f, 0.95f, 7 },
	[HOLD_EDGE]   = { "EDGE",   0.65f, 0.95f, 11 },
	[HOLD_POCKET] = { "POCKET", 1.50f, 0.50f, 8 },
	/* Underclings depend on where the body is: see climb.c. */
	[HOLD_UNDERCLING] = { "UNDERCLING", 1.00f, 0.60f, 11 },
	[HOLD_VOLUME] = { "VOLUME", 1.40f, 1.00f, 20 },
};

const char *const ANGLE_NAMES[ANGLE_COUNT] = { "SLAB", "VERTICAL", "OVERHANG", "ROOF" };

const uint32_t ROUTE_COLOR[ROUTE_COLORS] = {
	0xF2C230, 0xE84A5F, 0x3DBE8B, 0x4C8DF0, 0xB45FE0, 0xF08A3C, 0xF5F5F0, 0x2B2B33,
};
const char *const COLOR_NAMES[ROUTE_COLORS] = {
	"YELLOW", "RED", "GREEN", "BLUE", "PURPLE", "ORANGE", "WHITE", "BLACK",
};

float hold_hand_difficulty(int type, int angle)
{
	float d = HOLD_INFO[type].hand;
	if (type == HOLD_SLOPER || type == HOLD_VOLUME)
		d *= 1.0f + 0.3f * angle;
	return d;
}

void route_init(Route *r, const char *name)
{
	memset(r, 0, sizeof(*r));
	snprintf(r->name, sizeof(r->name), "%s", name);
	r->angle = ANGLE_VERT;
	r->grade = GRADE_UNKNOWN;
}

void route_clear_beta(Route *r)
{
	r->grade = GRADE_UNKNOWN;
	r->nbeta = 0;
}

void grade_text(char *out, int size, int grade)
{
	if (grade == GRADE_UNKNOWN)
		snprintf(out, size, "V?");
	else if (grade == GRADE_NONE)
		snprintf(out, size, "V??");
	else
		snprintf(out, size, "V%d", grade);
}

int route_add_hold(Route *r, int x, int y, int type)
{
	if (r->nholds >= MAX_HOLDS)
		return -1;
	if (x < 8) x = 8;
	if (x > WALL_W - 8) x = WALL_W - 8;
	if (y < 8) y = 8;
	if (y > HOLD_MAX_Y) y = HOLD_MAX_Y;
	Hold *h = &r->holds[r->nholds];
	memset(h, 0, sizeof(*h));
	h->x = (int16_t)x;
	h->y = (int16_t)y;
	h->type = (uint8_t)type;
	return r->nholds++;
}

void route_remove_hold(Route *r, int i)
{
	if (i < 0 || i >= r->nholds)
		return;
	memmove(&r->holds[i], &r->holds[i + 1], (r->nholds - i - 1) * sizeof(Hold));
	r->nholds--;
}

int route_hold_at(const Route *r, float x, float y, float slack)
{
	int best = -1;
	float bestD = 1e9f;
	for (int i = 0; i < r->nholds; i++) {
		const Hold *h = &r->holds[i];
		float dx = h->x - x, dy = h->y - y;
		float d = sqrtf(dx * dx + dy * dy);
		if (d <= HOLD_INFO[h->type].grab + slack && d < bestD) {
			bestD = d;
			best = i;
		}
	}
	return best;
}

int route_count_role(const Route *r, int role)
{
	int n = 0;
	for (int i = 0; i < r->nholds; i++)
		n += r->holds[i].role == role;
	return n;
}

int route_starts(const Route *r, int out[MAX_START])
{
	int n = 0;
	for (int i = 0; i < r->nholds && n < MAX_START; i++)
		if (r->holds[i].role == ROLE_START)
			out[n++] = i;
	if (n == 2 && r->holds[out[0]].x > r->holds[out[1]].x) {
		int t = out[0];
		out[0] = out[1];
		out[1] = t;
	}
	return n;
}

int route_finish(const Route *r)
{
	for (int i = 0; i < r->nholds; i++)
		if (r->holds[i].role == ROLE_FINISH)
			return i;
	return -1;
}

const char *route_problem(const Route *r)
{
	int s[MAX_START];
	int n = route_starts(r, s);
	if (n == 0)
		return "MARK A START HOLD";
	if (route_finish(r) < 0)
		return "MARK A TOP HOLD";
	for (int i = 0; i < n; i++)
		if (r->holds[s[i]].y < FLOOR_Y - 128)
			return "START HOLDS TOO HIGH";
	if (n == 2 && abs(r->holds[s[0]].x - r->holds[s[1]].x) > 70)
		return "START HOLDS TOO FAR APART";
	return NULL;
}

int route_grade(const Route *r)
{
	float diff = 0, gap = 0;
	int hands = 0, feet = 0;
	for (int i = 0; i < r->nholds; i++) {
		const Hold *h = &r->holds[i];
		if (h->type == HOLD_FOOT) {
			feet++;
			continue;
		}
		hands++;
		diff += hold_hand_difficulty(h->type, r->angle);
		/* Distance to the closest hand hold above: how big the moves are. */
		if (h->role == ROLE_FINISH)
			continue;
		float best = 200;
		for (int j = 0; j < r->nholds; j++) {
			const Hold *o = &r->holds[j];
			if (j == i || o->type == HOLD_FOOT || o->y >= h->y)
				continue;
			float d = hypotf(o->x - h->x, o->y - h->y);
			if (d < best)
				best = d;
		}
		gap += best;
	}
	if (hands == 0)
		return 0;
	diff /= hands;
	gap /= hands;
	float score = (diff - 0.4f) * 2.5f + r->angle * 1.2f + (gap - 42.0f) / 10.0f;
	if (feet < 3)
		score += (3 - feet) * 0.4f;
	int g = (int)lroundf(score);
	return g < 0 ? 0 : g > 12 ? 12 : g;
}

/* ------------------------------------------------------------------ */
/* Built-in routes so there is something to climb straight away.      */

typedef struct { int16_t x, y; uint8_t type, role; } SampleHold;

static void add_sample(RouteBook *b, const char *name, int color, int angle,
                       const SampleHold *holds, int n)
{
	if (b->count >= MAX_ROUTES)
		return;
	Route *r = &b->routes[b->count++];
	route_init(r, name);
	r->color = (uint8_t)color;
	r->angle = (uint8_t)angle;
	for (int i = 0; i < n; i++) {
		int k = route_add_hold(r, holds[i].x, holds[i].y, holds[i].type);
		if (k >= 0) {
			r->holds[k].role = holds[i].role;
			r->holds[k].rot = (uint8_t)((holds[i].x + holds[i].y) & 3);
		}
	}
}

#define J HOLD_JUG
#define C HOLD_CRIMP
#define S HOLD_SLOPER
#define P HOLD_PINCH
#define F HOLD_FOOT
#define E HOLD_EDGE
#define ST ROLE_START
#define TP ROLE_FINISH

static const SampleHold WARMUP[] = {
	{150, 585, J, ST}, {180, 590, J, ST},
	{140, 650, F, 0}, {190, 655, F, 0}, {170, 615, J, 0},
	{130, 545, J, 0}, {195, 535, J, 0}, {160, 500, J, 0},
	{120, 460, J, 0}, {185, 455, J, 0}, {150, 410, J, 0},
	{200, 380, J, 0}, {135, 360, J, 0}, {175, 320, J, 0},
	{130, 280, J, 0}, {195, 260, J, 0}, {160, 220, J, 0},
	{120, 185, J, 0}, {190, 170, J, 0}, {150, 130, J, 0},
	{185, 95, J, 0}, {140, 70, J, 0}, {165, 30, J, TP},
};

static const SampleHold CRIMPS[] = {
	{95, 590, C, ST}, {125, 588, C, ST},
	{80, 655, F, 0}, {140, 660, F, 0}, {105, 625, F, 0},
	{75, 548, C, 0}, {130, 535, P, 0}, {100, 590, F, 0}, {150, 600, F, 0},
	{160, 495, C, 0}, {110, 470, C, 0}, {170, 545, F, 0}, {85, 520, F, 0},
	{185, 430, P, 0}, {140, 405, C, 0}, {120, 495, F, 0}, {180, 480, F, 0},
	{200, 360, C, 0}, {160, 330, J, 0}, {150, 440, F, 0}, {205, 410, F, 0},
	{220, 290, C, 0}, {180, 255, C, 0}, {175, 370, F, 0}, {230, 340, F, 0},
	{215, 210, P, 0}, {170, 180, C, 0}, {200, 300, F, 0}, {160, 280, F, 0},
	{200, 135, C, 0}, {165, 100, C, 0}, {220, 230, F, 0}, {175, 215, F, 0},
	{190, 60, S, 0}, {150, 25, J, TP}, {195, 160, F, 0}, {150, 140, F, 0},
};

static const SampleHold CAVE[] = {
	{220, 580, J, ST}, {250, 575, P, ST},
	{210, 655, F, 0}, {265, 650, J, 0},
	{200, 530, S, 0}, {245, 515, J, 0}, {230, 610, F, 0}, {185, 585, F, 0},
	{190, 470, P, 0}, {150, 460, S, 0}, {215, 555, F, 0}, {170, 520, F, 0},
	{120, 420, J, 0}, {155, 390, E, 0}, {150, 500, F, 0}, {110, 470, F, 0},
	{95, 345, J, 0}, {130, 320, P, 0}, {125, 430, F, 0}, {85, 400, F, 0},
	{100, 270, J, 0}, {140, 240, E, 0}, {120, 360, F, 0}, {80, 330, F, 0},
	{110, 190, J, 0}, {150, 165, P, 0}, {105, 290, F, 0}, {145, 270, F, 0},
	{125, 115, E, 0}, {165, 90, S, 0}, {130, 215, F, 0}, {165, 200, F, 0},
	{140, 45, J, TP}, {140, 135, F, 0}, {180, 125, F, 0},
};

void book_samples(RouteBook *b)
{
	memset(b, 0, sizeof(*b));
	add_sample(b, "WARM UP", 2, ANGLE_SLAB, WARMUP, sizeof(WARMUP) / sizeof(WARMUP[0]));
	add_sample(b, "CRIMP LADDER", 3, ANGLE_VERT, CRIMPS, sizeof(CRIMPS) / sizeof(CRIMPS[0]));
	add_sample(b, "THE CAVE", 1, ANGLE_OVERHANG, CAVE, sizeof(CAVE) / sizeof(CAVE[0]));
}

/* ------------------------------------------------------------------ */
/* Save file: a small header, then the routes as stored in memory.     */

#define SAVE_MAGIC   0x52444C42u /* "BLDR" */
#define SAVE_VERSION 2

typedef struct {
	uint32_t magic;
	uint16_t version, routeSize;
	uint32_t count;
} SaveHeader;

/* Version 1 routes: the same fields, without the grade and beta. */
typedef struct {
	char name[NAME_LEN + 1];
	uint8_t color, angle, nholds;
	Hold holds[MAX_HOLDS];
	uint32_t bestFrames;
	uint16_t sends, attempts;
} RouteV1;

bool book_load(RouteBook *b, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	SaveHeader h;
	bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == SAVE_MAGIC && h.count <= MAX_ROUTES &&
	          ((h.version == SAVE_VERSION && h.routeSize == sizeof(Route)) ||
	           (h.version == 1 && h.routeSize == sizeof(RouteV1)));
	if (ok) {
		memset(b, 0, sizeof(*b));
		for (uint32_t i = 0; i < h.count && ok; i++) {
			Route *r = &b->routes[i];
			if (h.version == 1) {
				RouteV1 old;
				ok = fread(&old, sizeof(old), 1, f) == 1;
				memcpy(r, &old, sizeof(old)); /* same leading fields */
				route_clear_beta(r);
			} else {
				ok = fread(r, sizeof(Route), 1, f) == 1;
			}
		}
		b->count = ok ? (int)h.count : 0;
		for (int i = 0; i < b->count; i++) {
			Route *r = &b->routes[i];
			r->name[NAME_LEN] = 0;
			if (r->nholds > MAX_HOLDS) r->nholds = MAX_HOLDS;
			if (r->angle >= ANGLE_COUNT) r->angle = ANGLE_VERT;
			if (r->color >= ROUTE_COLORS) r->color = 0;
			for (int k = 0; k < r->nholds; k++)
				if (r->holds[k].type >= HOLD_TYPES)
					r->holds[k].type = HOLD_JUG;
			if (r->nbeta > MAX_BETA)
				route_clear_beta(r);
			for (int k = 0; k < r->nbeta; k++)
				if (r->beta[k].hold >= r->nholds || r->beta[k].limb >= 4)
					route_clear_beta(r);
		}
	}
	fclose(f);
	return ok;
}

bool book_save(const RouteBook *b, const char *path)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	SaveHeader h = { SAVE_MAGIC, SAVE_VERSION, sizeof(Route), (uint32_t)b->count };
	bool ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
	          fwrite(b->routes, sizeof(Route), b->count, f) == (size_t)b->count;
	ok = fclose(f) == 0 && ok;
	return ok;
}

void book_delete(RouteBook *b, int i)
{
	if (i < 0 || i >= b->count)
		return;
	memmove(&b->routes[i], &b->routes[i + 1], (b->count - i - 1) * sizeof(Route));
	b->count--;
}
