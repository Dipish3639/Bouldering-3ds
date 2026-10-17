/* Runs the real game loop (source/main.c) on a PC against a stubbed libctru,
 * with a scripted player tapping, dragging and pressing buttons through the
 * title, route list, climbing and the route editor.
 * Build and run: tools/smoketest.sh (uses AddressSanitizer). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "3ds.h"
#include "audio.h"

/* No sound on the PC. */
bool audio_init(void) { return true; }
void audio_exit(void) {}
void audio_music(int t) { (void)t; }
void audio_music_volume(float v) { (void)v; }
void audio_sfx(int s) { (void)s; }
void audio_sfx_pitch(int s, float p) { (void)s; (void)p; }
void audio_enable(bool m, bool s) { (void)m; (void)s; }
void audio_update(void) {}

#define main game_main
#include "../source/main.c"
#undef main

/* ------------------------------------------------------------------ */
/* Stubbed platform. */

static u8 fbTopL[400 * 240 * 3], fbTopR[400 * 240 * 3], fbBot[320 * 240 * 3];
static u64 ticks;
static int frame;
static u32 keysNow, keysPrev;
static touchPosition touchNow;
static circlePosition circleNow;
static float slider;
static int failures;

void gfxInitDefault(void) {}
void gfxExit(void) {}
void gfxSetDoubleBuffering(gfxScreen_t s, bool on) { (void)s; (void)on; }
void gfxSet3D(bool on) { (void)on; }
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t side, u16 *w, u16 *h)
{
	(void)w; (void)h;
	return s == GFX_BOTTOM ? fbBot : side == GFX_RIGHT ? fbTopR : fbTopL;
}
void gfxFlushBuffers(void) {}
void gfxSwapBuffers(void) {}
void gspWaitForVBlank(void) {}
bool osSetSpeedupEnable(bool on) { (void)on; return true; }
float osGet3DSliderState(void) { return slider; }
u64 svcGetSystemTick(void) { return ticks += SYSCLOCK_ARM11 / 60 / 2; } /* called twice a frame */
void hidTouchRead(touchPosition *t) { *t = touchNow; }
void hidCircleRead(circlePosition *c) { *c = circleNow; }
u32 hidKeysDown(void) { return keysNow & ~keysPrev; }
u32 hidKeysHeld(void) { return keysNow; }
u32 hidKeysUp(void) { return keysPrev & ~keysNow; }
void swkbdInit(SwkbdState *s, SwkbdType t, int b, int m) { (void)s; (void)t; (void)b; (void)m; }
void swkbdSetHintText(SwkbdState *s, const char *t) { (void)s; (void)t; }
void swkbdSetInitialText(SwkbdState *s, const char *t) { (void)s; (void)t; }
void swkbdSetValidation(SwkbdState *s, int v, u32 f, u32 d) { (void)s; (void)v; (void)f; (void)d; }
SwkbdButton swkbdInputText(SwkbdState *s, char *buf, size_t size)
{
	(void)s;
	snprintf(buf, size, "My \xc3\xa9 Problem");
	return SWKBD_BUTTON_CONFIRM;
}

static void check(bool ok, const char *what)
{
	if (!ok) {
		printf("FAIL (frame %d): %s\n", frame, what);
		failures++;
	}
}

/* ------------------------------------------------------------------ */
/* The scripted player. Each step is a little state machine fed one frame
 * at a time; it sets keysNow / touchNow for the coming frame. */

static int phase, phaseFrame;
static int dragLimb, dragHold, dragFrames;
static int climbMoves;

static void next_phase(void) { phase++; phaseFrame = 0; }

static void tap(int x, int y)
{
	keysNow |= KEY_TOUCH;
	touchNow.px = (u16)x;
	touchNow.py = (u16)y;
}

/* Pick a limb and the highest hold it could plausibly reach. */
static bool plan_move(void)
{
	const Route *r = climbRoute;
	int bestL = -1, bestH = -1;
	float bestY = 1e9f;
	for (int l = 0; l < LIMBS; l++) {
		Vec j = climb_joint(&climber, l);
		for (int i = 0; i < r->nholds; i++) {
			const Hold *h = &r->holds[i];
			if (i == climber.on[l] || (limb_is_hand(l) && h->type == HOLD_FOOT))
				continue;
			if (hypotf(h->x - j.x, h->y - j.y) > limb_reach(l) - 2 || h->y >= climber.end[l].y - 8)
				continue;
			if (!limb_is_hand(l) && h->y < fminf(climber.end[0].y, climber.end[1].y) + 40)
				continue;
			float sy = climber.end[l].y - ui.camY, hy = h->y - ui.camY;
			if (sy < 2 || sy > 238 || hy < 2 || hy > 238)
				continue;
			if (h->y < bestY) {
				bestY = h->y;
				bestL = l;
				bestH = i;
			}
		}
	}
	dragLimb = bestL;
	dragHold = bestH;
	dragFrames = 0;
	return bestL >= 0;
}

static bool script(void)
{
	keysNow = 0;
	circleNow.dx = circleNow.dy = 0;
	phaseFrame++;
	switch (phase) {
	case 0: /* title: wait, toggle 3D on, start */
		slider = 0.6f;
		if (phaseFrame == 20) {
			check(mode == MODE_TITLE, "starts on the title screen");
			keysNow = KEY_A;
			next_phase();
		}
		break;
	case 1: /* list: select the crimp route and climb it */
		if (phaseFrame == 5) check(mode == MODE_LIST, "A opens the route list");
		if (phaseFrame == 10) keysNow = KEY_DDOWN;
		if (phaseFrame == 15) {
			check(ui.listSel == 2, "down moves the selection");
			keysNow = KEY_A;
		}
		if (phaseFrame == 20) {
			check(mode == MODE_CLIMB && climbIndex == 1, "A starts climbing the selected route");
			next_phase();
		}
		break;
	case 2: /* climb with the stylus: drag limbs onto holds */
		if (climber.state != CL_CLIMBING || phaseFrame > 3000) {
			next_phase();
			break;
		}
		if (dragLimb < 0 || dragFrames == 0) {
			if (phaseFrame % 30 != 0 || !plan_move())
				break;
		}
		dragFrames++;
		if (dragFrames < 25) {
			const Hold *h = &climbRoute->holds[dragHold];
			float t = dragFrames / 20.0f;
			if (t > 1) t = 1;
			Vec e = dragFrames == 1 ? climber.end[dragLimb] : (Vec){ 0, 0 };
			static float sx, sy;
			if (dragFrames == 1) { sx = e.x; sy = e.y - ui.camY; }
			float hx = h->x, hy = h->y - ui.camY;
			tap((int)(sx + (hx - sx) * t), (int)(sy + (hy - sy) * t));
		} else {
			climbMoves++;
			dragFrames = 0;
			dragLimb = -1;
		}
		break;
	case 3:
		printf("stylus climb: %d drags, %d moves, state %d, stamina %.2f, body y %.0f\n",
		       climbMoves, climber.moves, climber.state, climber.stamina, climber.body.y);
		check(climber.moves >= 6, "stylus drags move limbs onto holds");
		if (climber.state == CL_CLIMBING) {
			/* Steer with buttons: hold L and push the circle pad up. */
			next_phase();
		} else {
			phase = 5;
			phaseFrame = 0;
		}
		break;
	case 4:
		if (phaseFrame < 40) {
			keysNow = KEY_L;
			circleNow.dy = 150;
		} else if (phaseFrame == 40) {
			check(climber.moving == -1 || climber.moving == LIMB_LH, "L picks the left hand");
		} else {
			next_phase();
		}
		break;
	case 5: /* pause and quit to the list */
		if (climber.state == CL_TOPPED || climber.state == CL_FELL) {
			if (phaseFrame == 5) keysNow = KEY_B;
		} else {
			if (phaseFrame == 5) keysNow = KEY_START;
			if (phaseFrame == 10) {
				check(ui.menuOpen, "START opens the pause menu");
				tap(160, MENU_ROW_Y + 2 * MENU_ROW_H + 5);
			}
		}
		if (phaseFrame == 20) {
			check(mode == MODE_LIST, "quit returns to the route list");
			check(book.routes[1].attempts == 1, "the attempt was counted");
			next_phase();
		}
		break;
	case 6: /* create a new route */
		if (phaseFrame == 2) tap(160, LIST_Y + 5);         /* select the "new" row */
		if (phaseFrame == 6) tap(160, LIST_Y + 5);         /* tap again to open it */
		if (phaseFrame == 12) {
			check(mode == MODE_EDIT && editIndex == -1, "new route opens the editor");
			next_phase();
		}
		break;
	case 7: { /* build: start holds near the mat, a ladder of jugs, a top */
		static const int PTS[][2] = {
			{150, 585}, {180, 588}, {140, 650}, {190, 655}, {130, 545}, {195, 535}, {160, 500},
			{120, 460}, {185, 455}, {150, 410}, {200, 380}, {135, 360}, {175, 320}, {130, 280},
			{195, 260}, {160, 220}, {120, 185}, {190, 170}, {150, 130}, {185, 95}, {140, 70}, {165, 30},
		};
		int n = sizeof(PTS) / sizeof(PTS[0]);
		int k = phaseFrame / 6, sub = phaseFrame % 6;
		if (k < n) {
			/* Scroll so the point is on screen, then tap it. */
			if (sub == 0)
				ui.camY = PTS[k][1] - 100;
			clamp_edit_cam();
			if (sub == 1)
				tap(PTS[k][0], (int)(PTS[k][1] - ui.camY));
		} else {
			check(editRoute.nholds == n, "taps place holds");
			next_phase();
		}
		break;
	}
	case 8: /* mark starts (S tool) and the top (TOP tool) */
		if (phaseFrame == 1) { ui.camY = 450; tap(TOOL_START * TOOL_W + 20, 220); }
		if (phaseFrame == 3) tap(150, (int)(585 - ui.camY));
		if (phaseFrame == 5) tap(180, (int)(588 - ui.camY));
		if (phaseFrame == 7) { ui.camY = 0; tap(TOOL_TOP * TOOL_W + 20, 220); }
		if (phaseFrame == 9) tap(165, 30);
		if (phaseFrame == 12) {
			check(route_count_role(&editRoute, ROLE_START) == 2, "start tool marks start holds");
			check(route_finish(&editRoute) >= 0, "top tool marks the top hold");
			check(route_problem(&editRoute) == NULL, "the new route is climbable");
		}
		/* Drag a hold, rotate one with a tap, erase one. */
		if (phaseFrame == 14) { keysNow = KEY_L; } /* tool back to TOP -> START */
		if (phaseFrame == 16) { tap(0 * TOOL_W + 20, 220); }        /* JUG tool */
		if (phaseFrame >= 18 && phaseFrame < 30) tap(140 + (phaseFrame - 18) * 2, 70);
		if (phaseFrame == 32) {
			int h = route_hold_at(&editRoute, 162, 70, 2);
			check(h >= 0, "dragging moves a hold");
		}
		if (phaseFrame == 34) tap(185, 95);
		if (phaseFrame == 38) tap(TOOL_ERASE * TOOL_W + 20, 220);
		if (phaseFrame == 40) tap(190, 170);
		if (phaseFrame == 44) {
			check(editRoute.nholds == 21, "erase removes a hold");
			next_phase();
		}
		break;
	case 9: /* test climb straight from the editor, then back */
		if (phaseFrame == 2) keysNow = KEY_X;
		if (phaseFrame == 5) check(mode == MODE_CLIMB && ui.testing, "X test-climbs the route");
		if (phaseFrame == 8) keysNow = KEY_START;
		if (phaseFrame == 12) tap(160, MENU_ROW_Y + 2 * MENU_ROW_H + 5);
		if (phaseFrame == 16) {
			check(mode == MODE_EDIT, "back to the editor after testing");
			next_phase();
		}
		break;
	case 10: /* editor menu: rename, change wall angle, save */
		if (phaseFrame == 2) keysNow = KEY_START;
		if (phaseFrame == 6) tap(160, MENU_ROW_Y + 2 * MENU_ROW_H + 5); /* rename */
		if (phaseFrame == 10) tap(160, MENU_ROW_Y + 3 * MENU_ROW_H + 5); /* wall angle */
		if (phaseFrame == 14) {
			check(strcmp(editRoute.name, "MY ? PROBLEM") == 0, "rename uppercases and strips non-ASCII");
			check(editRoute.angle == ANGLE_OVERHANG, "wall angle cycles");
			tap(160, MENU_ROW_Y + 0 * MENU_ROW_H + 5); /* save & exit */
		}
		if (phaseFrame == 20) {
			check(mode == MODE_LIST && book.count == 4, "save adds the route to the book");
			next_phase();
		}
		break;
	case 11: /* climb the new route with buttons: just make sure it runs */
		if (phaseFrame == 2) keysNow = KEY_A;
		if (phaseFrame == 5) check(mode == MODE_CLIMB && climbIndex == 3, "climb the new route");
		if (phaseFrame > 5 && phaseFrame < 200) {
			static const u32 K[4] = { KEY_Y, KEY_A, KEY_L, KEY_R };
			int cycle = (phaseFrame - 6) / 40, sub = (phaseFrame - 6) % 40;
			if (sub < 30) {
				keysNow = K[cycle % 4];
				circleNow.dy = 120;
			}
		}
		if (phaseFrame == 210) keysNow = KEY_START;
		if (phaseFrame == 214) tap(160, MENU_ROW_Y + 2 * MENU_ROW_H + 5);
		if (phaseFrame == 220) {
			check(mode == MODE_LIST, "back to the list");
			next_phase();
		}
		break;
	case 12: /* delete it (Y twice), then back to the title */
		if (phaseFrame == 2) keysNow = KEY_Y;
		if (phaseFrame == 6) keysNow = KEY_Y;
		if (phaseFrame == 10) {
			check(book.count == 3, "Y twice deletes the route");
			keysNow = KEY_B;
		}
		if (phaseFrame == 14) {
			check(mode == MODE_TITLE, "B goes back to the title");
			keysNow = KEY_START;
		}
		if (phaseFrame == 18)
			return false;
		break;
	}
	return true;
}

static bool running = true;

bool aptMainLoop(void)
{
	return running && frame < 20000;
}

void hidScanInput(void)
{
	keysPrev = keysNow;
	frame++;
	running = script();
}

int main(void)
{
	game_main();
	check(phase == 12, "script reached the end");
	printf("%s after %d frames\n", failures ? "FAILED" : "passed", frame);
	return failures ? 1 : 0;
}
