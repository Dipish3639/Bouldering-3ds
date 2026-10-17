#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include "climb.h"
#include "gfx.h"
#include "route.h"

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

/* The two screens show one tall strip of wall: the bottom screen shows world
 * rows camY..camY+240 and the top screen the 240 rows directly above, with
 * the wall centred between two HUD margins. */
#define TOP_WALL_X 40

/* Editor: wall view above a toolbar on the bottom screen. */
#define EDIT_VIEW_H 200
#define TOOL_W      26
#define TOOL_X0     4

enum {
	TOOL_JUG, TOOL_EDGE, TOOL_CRIMP, TOOL_POCKET, TOOL_SLOPER, TOOL_PINCH, TOOL_UNDER, TOOL_VOLUME, TOOL_FOOT,
	TOOL_START, TOOL_TOP, TOOL_ERASE, TOOL_COUNT
};
extern const char *const TOOL_NAMES[TOOL_COUNT];
extern const char *const TOOL_SHORT[TOOL_COUNT];
/* Hold type a tool places, or -1 for START / TOP / ERASE. */
extern const int8_t TOOL_HOLD[TOOL_COUNT];

/* Route list layout: "new route" and "generate" rows, then one row per route. */
#define LIST_NEW       0
#define LIST_GENERATE  1
#define LIST_FIRST     2 /* row of book->routes[0] */
#define LIST_Y       32
#define LIST_ROW_H   28
#define LIST_ROWS    5
#define LIST_BTN_Y   184
#define LIST_BTN_H   48
#define LIST_BTN_W   60
#define LIST_BTN_STEP 64
enum { LBTN_BACK, LBTN_DELETE, LBTN_EDIT, LBTN_BETA, LBTN_CLIMB, LBTN_COUNT };

/* Pop-up menu on the bottom screen. */
#define MENU_X     40
#define MENU_W     240
#define MENU_ROW_Y 54
#define MENU_ROW_H 30
#define MENU_MAX   7

/* Title screen start button. */
#define TITLE_BTN_X 60
#define TITLE_BTN_Y 70
#define TITLE_BTN_W 200
#define TITLE_BTN_H 64

typedef enum { SCR_LOADING, SCR_TITLE, SCR_LIST, SCR_EDIT, SCR_CLIMB, SCR_WORKING } Screen;

typedef struct {
	Screen screen;
	int frame;
	bool audioOk, musicOn, sfxOn;

	const char *toast;  /* short message on the bottom screen */
	int toastTimer;

	/* Route list. */
	const RouteBook *book;
	int listSel;        /* LIST_NEW, LIST_GENERATE, or LIST_FIRST + route index */
	int listScroll;
	bool confirmDelete;

	/* Editor and climbing. */
	const Route *route;
	float camY;         /* world row at the top of the bottom screen */
	int tool;
	int activeHold;     /* hold being dragged in the editor, or -1 */
	const Climber *climber;
	bool testing;       /* climbing straight from the editor */
	bool newBest;

	/* Beta: numbered moves on the holds, and the step being shown when
	 * watching the solver's climb. */
	bool showBeta, watching;
	int betaStep;
	const char *betaText;

	/* Working screen (solver / generator running). */
	const char *workTitle, *workSub;
	float workProgress;

	/* Pop-up menu. */
	bool menuOpen;
	const char *menuTitle;
	const char *menuItems[MENU_MAX];
	int menuCount, menuSel;
} UiState;

/* Rebuild the wall texture for a wall angle (cheap to call every frame:
 * it only does work when the angle changes). */
void render_wall_texture(int angle);

/* The grade to show for a route: the solver's, or "~V3" from the holds. */
void route_grade_label(char *out, int size, const Route *r);

/* Fill a buffer with the static top-screen backdrop (HUD margins). */
void render_top_backdrop(Canvas *cv);

/* `depth` is this eye's stereo amount: the 3D slider (0..1) for the left eye,
 * minus it for the right, 0 when 3D is off;
 * nearer things are shifted by more of it. */
void render_top(Canvas *cv, const UiState *ui, float depth);
void render_bottom(Canvas *cv, const UiState *ui);

/* Touch hit tests for the layouts above; -1 = nothing. */
int hit_list_row(const UiState *ui, int px, int py);
int hit_list_button(int px, int py);
int hit_menu(const UiState *ui, int px, int py);
int hit_tool(int px, int py);
bool hit_title_button(int px, int py);

/* "1:05.3" */
void format_time(char *out, int size, uint32_t frames);

#endif
