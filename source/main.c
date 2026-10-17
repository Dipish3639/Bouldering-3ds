#include <3ds.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "audio.h"
#include "beta.h"
#include "climb.h"
#include "gen.h"
#include "render.h"
#include "route.h"

#define SAVE_DIR  "sdmc:/3ds/bouldering3ds"
#define SAVE_PATH SAVE_DIR "/routes.dat"

typedef enum { MODE_TITLE, MODE_LIST, MODE_EDIT, MODE_CLIMB, MODE_WORK } Mode;
typedef enum { MENU_NONE, MENU_EDITOR, MENU_PAUSE, MENU_GENERATE } MenuKind;

/* Long jobs run a slice per frame on the working screen. */
typedef enum {
	JOB_SAVE,      /* grade the edited route, then save it */
	JOB_WATCH,     /* find the beta, then show it */
	JOB_GENERATE,  /* set a new route */
	JOB_GRADE_ALL, /* grade every route that hasn't been (old saves, samples) */
} Job;

static Mode mode;
static UiState ui;
static RouteBook book;

/* Editor: we work on a copy and only write it back on save. */
static Route editRoute;
static int editIndex;       /* slot in the book, or -1 for a new route */
static bool editDirty;
static int dragHold = -1;   /* hold under the stylus */
static bool dragMoved, dragNew;
static int dragStartX, dragStartY;

/* Climbing. */
static Climber climber;
static const Route *climbRoute;
static int climbIndex;      /* slot in the book, or -1 when testing from the editor */
static int touchLimb = -1;  /* limb being dragged with the stylus */
static int buttonLimb = -1; /* limb being steered with L/R/Y/A */
static float aimX, aimY;
static int pumpTimer;
/* Last seen value of each climber event counter, for sounds. */
static struct { int grab, foot, miss, slip, fall, land, top; } heard;

/* Watching the beta: which move is playing and how far into it. */
static int watchWait, watchFrame;
static char betaText[48];

static Job job;
static Mode jobFrom;        /* where B (cancel) goes back to */
static Route *jobRoute;     /* route being graded */
static int jobIndex;        /* its slot in the book, or -1 for the editor's route */
static Beta solver;
static Generator gen;
static int genGrade = 3, genAngle = GEN_ANY_ANGLE;
static char workSub[48];

static MenuKind menuKind;
static char menuText[MENU_MAX][32];

static bool audioOk, musicOn = true, sfxOn = true;

/* Title screen climber, standing at the bottom of the first route. */
static Climber titleClimber;

static void toast(const char *msg)
{
	ui.toast = msg;
	ui.toastTimer = 100;
}

static void save_book(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir(SAVE_DIR, 0777);
	if (!book_save(&book, SAVE_PATH))
		toast("COULD NOT SAVE TO SD CARD");
}

/* ------------------------------------------------------------------ */
/* Pop-up menus. */

static void open_menu(MenuKind kind)
{
	menuKind = kind;
	ui.menuOpen = true;
	ui.menuSel = 0;
	int n = 0;
	if (kind == MENU_EDITOR) {
		ui.menuTitle = "EDITOR";
		snprintf(menuText[n++], 32, "SAVE & EXIT");
		snprintf(menuText[n++], 32, "TEST CLIMB");
		snprintf(menuText[n++], 32, "WATCH THE BETA");
		snprintf(menuText[n++], 32, "RENAME");
		snprintf(menuText[n++], 32, "WALL: %s", ANGLE_NAMES[editRoute.angle]);
		snprintf(menuText[n++], 32, "COLOR: %s", COLOR_NAMES[editRoute.color]);
		snprintf(menuText[n++], 32, editDirty ? "EXIT WITHOUT SAVING" : "EXIT");
	} else if (kind == MENU_GENERATE) {
		ui.menuTitle = "GENERATE A ROUTE";
		snprintf(menuText[n++], 32, "<  GRADE: V%d  >", genGrade);
		snprintf(menuText[n++], 32, "<  WALL: %s  >", genAngle == GEN_ANY_ANGLE ? "ANY" : ANGLE_NAMES[genAngle]);
		snprintf(menuText[n++], 32, "SET IT!");
		snprintf(menuText[n++], 32, "BACK");
	} else {
		ui.menuTitle = "PAUSED";
		snprintf(menuText[n++], 32, "RESUME");
		snprintf(menuText[n++], 32, "RESTART");
		snprintf(menuText[n++], 32, "WATCH THE BETA");
		snprintf(menuText[n++], 32, climbIndex < 0 ? "BACK TO EDITOR" : "QUIT TO ROUTES");
	}
	ui.menuCount = n;
	for (int i = 0; i < n; i++)
		ui.menuItems[i] = menuText[i];
	audio_sfx(SFX_MENU_SELECT);
}

static void close_menu(void)
{
	ui.menuOpen = false;
	menuKind = MENU_NONE;
}

/* ------------------------------------------------------------------ */
/* Mode changes. */

static void go_title(void)
{
	mode = MODE_TITLE;
	/* Pose the climber at the start of the first route for the backdrop. */
	if (book.count > 0) {
		climb_start(&titleClimber, &book.routes[0]);
		for (int i = 0; i < 60; i++)
			climb_update(&titleClimber);
	}
}

static void go_list(void)
{
	mode = MODE_LIST;
	ui.confirmDelete = false;
	close_menu();
	if (ui.listSel >= LIST_FIRST + book.count)
		ui.listSel = LIST_FIRST + book.count - 1;
}

/* Keep the selected row of the route list on screen. */
static void list_scroll_to_sel(void)
{
	if (ui.listSel < ui.listScroll)
		ui.listScroll = ui.listSel;
	if (ui.listSel >= ui.listScroll + LIST_ROWS)
		ui.listScroll = ui.listSel - LIST_ROWS + 1;
	int maxScroll = LIST_FIRST + book.count - LIST_ROWS;
	if (ui.listScroll > maxScroll)
		ui.listScroll = maxScroll < 0 ? 0 : maxScroll;
}

static void start_climb_mode(const Route *r, int index, bool watch)
{
	if (!climb_start(&climber, r)) {
		const char *p = route_problem(r);
		toast(p ? p : "CAN'T REACH THE START HOLDS");
		return;
	}
	climbRoute = r;
	climbIndex = index;
	mode = MODE_CLIMB;
	touchLimb = buttonLimb = -1;
	pumpTimer = 0;
	memset(&heard, 0, sizeof(heard)); /* climb_start zeroes the counters too */
	ui.newBest = false;
	ui.testing = index < 0;
	ui.watching = watch;
	ui.betaStep = 0;
	ui.betaText = NULL;
	watchWait = watchFrame = 0;
	ui.camY = WALL_H - BOT_H;
	close_menu();
	render_wall_texture(r->angle);
	if (index >= 0 && !watch)
		book.routes[index].attempts++;
	audio_sfx(SFX_START);
}

static void start_climb(const Route *r, int index)
{
	start_climb_mode(r, index, false);
}

/* Open the editor on a route from the book, a new one, or `from` (generated). */
static void start_edit_from(int index, const Route *from)
{
	editIndex = index;
	if (index >= 0) {
		editRoute = book.routes[index];
	} else if (from) {
		editRoute = *from;
	} else {
		char name[32];
		snprintf(name, sizeof(name), "ROUTE %d", book.count + 1);
		route_init(&editRoute, name);
		editRoute.color = (uint8_t)(book.count % ROUTE_COLORS);
	}
	editDirty = index < 0;
	mode = MODE_EDIT;
	ui.camY = WALL_H - EDIT_VIEW_H;
	ui.tool = TOOL_JUG;
	dragHold = -1;
	close_menu();
	render_wall_texture(editRoute.angle);
}

static void start_edit(int index)
{
	start_edit_from(index, NULL);
}

static void back_to_edit(void)
{
	mode = MODE_EDIT;
	close_menu();
	ui.camY = WALL_H - EDIT_VIEW_H;
	render_wall_texture(editRoute.angle);
}

static void leave_climb(void)
{
	if (climbIndex >= 0) {
		save_book();
		go_list();
	} else {
		back_to_edit();
	}
}

static bool save_edit(void)
{
	if (editIndex < 0) {
		if (book.count >= MAX_ROUTES) {
			toast("ROUTE BOOK IS FULL");
			return false;
		}
		editIndex = book.count++;
		ui.listSel = LIST_FIRST + editIndex;
	} else {
		/* Edited routes keep their stats, but a changed route is a new problem. */
		Route *old = &book.routes[editIndex];
		if (old->nholds != editRoute.nholds || old->angle != editRoute.angle ||
		    memcmp(old->holds, editRoute.holds, editRoute.nholds * sizeof(Hold)) != 0) {
			editRoute.bestFrames = 0;
			editRoute.sends = editRoute.attempts = 0;
		}
	}
	book.routes[editIndex] = editRoute;
	list_scroll_to_sel();
	editDirty = false;
	save_book();
	return true;
}

/* ------------------------------------------------------------------ */
/* Jobs: grading, beta finding and route generation. */

static void start_job(Job j, Route *r, int index)
{
	job = j;
	jobFrom = mode;
	jobRoute = r;
	jobIndex = index;
	close_menu();
	mode = MODE_WORK;
	ui.workProgress = 0;
	ui.workSub = workSub;
	workSub[0] = 0;
	switch (j) {
	case JOB_SAVE:
		ui.workTitle = "GRADING YOUR ROUTE";
		snprintf(workSub, sizeof(workSub), "THE BETA FINDER IS ON THE WALL");
		beta_start(&solver, r);
		break;
	case JOB_WATCH:
		ui.workTitle = "FINDING THE BETA";
		snprintf(workSub, sizeof(workSub), "TRYING EVERY HAND AND FOOT");
		beta_start(&solver, r);
		break;
	case JOB_GENERATE:
		ui.workTitle = "SETTING A ROUTE";
		snprintf(workSub, sizeof(workSub), "V%d  -  %s WALL", genGrade,
		         genAngle == GEN_ANY_ANGLE ? "ANY" : ANGLE_NAMES[genAngle]);
		gen_start(&gen, genGrade, genAngle, (uint32_t)svcGetSystemTick());
		break;
	case JOB_GRADE_ALL:
		ui.workTitle = "GRADING ROUTES";
		jobIndex = -1;
		break;
	}
	render_wall_texture(j == JOB_GENERATE ? ANGLE_VERT : r ? r->angle : ANGLE_VERT);
	ui.camY = WALL_H;
}

/* Start watching a route's beta, finding it first if needed. */
static void watch_beta(Route *r, int index)
{
	if (r->grade == GRADE_UNKNOWN) {
		if (route_problem(r)) {
			toast(route_problem(r));
			return;
		}
		start_job(JOB_WATCH, r, index);
		return;
	}
	if (r->nbeta == 0) {
		toast("NO WAY UP FOUND ON THIS ONE");
		return;
	}
	start_climb_mode(r, index, true);
}

static void save_and_exit(void)
{
	if (editRoute.grade == GRADE_UNKNOWN && !route_problem(&editRoute)) {
		start_job(JOB_SAVE, &editRoute, -1);
		return;
	}
	if (save_edit())
		go_list();
}

/* The next route the grade-all job should look at, from jobIndex on. */
static int next_ungraded(int from)
{
	for (int i = from; i < book.count; i++)
		if (book.routes[i].grade == GRADE_UNKNOWN && !route_problem(&book.routes[i]))
			return i;
	return -1;
}

static void job_cancel(void)
{
	audio_sfx(SFX_MENU_MOVE);
	if (job == JOB_GRADE_ALL) {
		save_book();
		go_title();
	} else if (jobFrom == MODE_EDIT) {
		back_to_edit();
	} else {
		go_list();
	}
}

/* Called when a solver run on jobRoute is finished. */
static void job_solved(void)
{
	static char msg[48];
	beta_store(&solver, jobRoute);
	char g[8];
	grade_text(g, sizeof(g), jobRoute->grade);
	if (job == JOB_SAVE) {
		if (jobRoute->grade == GRADE_NONE)
			snprintf(msg, sizeof(msg), "SAVED - NO WAY UP FOUND (YET)");
		else
			snprintf(msg, sizeof(msg), "SAVED AS %s  -  %d MOVES", g, jobRoute->nbeta);
		mode = MODE_EDIT;
		if (save_edit()) {
			go_list();
			toast(msg);
			audio_sfx(SFX_TOP);
		}
	} else if (job == JOB_WATCH) {
		if (jobIndex >= 0)
			save_book();
		mode = jobFrom;
		if (jobRoute->nbeta == 0) {
			if (jobFrom == MODE_EDIT) back_to_edit(); else go_list();
			toast("NO WAY UP FOUND ON THIS ONE");
		} else {
			start_climb_mode(jobRoute, jobIndex, true);
		}
	}
}

/* Spend about one frame's worth of time on the current job. */
static void job_work(void)
{
	u64 start = svcGetSystemTick();
	const u64 budget = SYSCLOCK_ARM11 / 45;
	do {
		switch (job) {
		case JOB_SAVE:
		case JOB_WATCH:
			if (beta_work(&solver, 2)) {
				job_solved();
				return;
			}
			ui.workProgress = beta_progress(&solver);
			break;
		case JOB_GENERATE:
			if (gen_work(&gen, 2)) {
				if (gen.status == GEN_DONE) {
					static char msg[48];
					snprintf(msg, sizeof(msg), "SET: %s  V%d", gen.result.name, gen.result.grade);
					start_edit_from(-1, &gen.result);
					toast(msg);
					audio_sfx(SFX_TOP);
				} else {
					go_list();
					toast("COULDN'T SET ONE - TRY AGAIN");
				}
				return;
			}
			ui.workProgress = gen_progress(&gen);
			snprintf(workSub, sizeof(workSub), "V%d  -  ATTEMPT %d", genGrade, gen.attempts);
			break;
		case JOB_GRADE_ALL:
			if (jobIndex < 0 || solver.status != BETA_WORKING) {
				if (jobIndex >= 0)
					beta_store(&solver, &book.routes[jobIndex]);
				jobIndex = next_ungraded(jobIndex + 1);
				if (jobIndex < 0) {
					save_book();
					go_title();
					return;
				}
				jobRoute = &book.routes[jobIndex];
				render_wall_texture(jobRoute->angle);
				beta_start(&solver, jobRoute);
				snprintf(workSub, sizeof(workSub), "%s", jobRoute->name);
			}
			beta_work(&solver, 2);
			ui.workProgress = beta_progress(&solver);
			break;
		}
	} while (svcGetSystemTick() - start < budget);
}

static void rename_route(void)
{
	SwkbdState kb;
	char buf[NAME_LEN + 1];
	swkbdInit(&kb, SWKBD_TYPE_WESTERN, 2, NAME_LEN);
	swkbdSetHintText(&kb, "Route name");
	swkbdSetInitialText(&kb, editRoute.name);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	if (swkbdInputText(&kb, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM)
		return;
	/* The font only has ASCII; the game shows names in capitals. */
	int n = 0;
	for (const unsigned char *s = (const unsigned char *)buf; *s && n < NAME_LEN; s++) {
		if (*s >= 0x80) {
			if ((*s & 0xC0) == 0x80)
				continue; /* rest of a UTF-8 character */
			editRoute.name[n++] = '?';
		} else if (*s >= 32) {
			editRoute.name[n++] = (char)toupper(*s);
		}
	}
	editRoute.name[n] = 0;
	editDirty = true;
}

static void menu_choose(int item)
{
	audio_sfx(SFX_MENU_SELECT);
	if (menuKind == MENU_EDITOR) {
		switch (item) {
		case 0:
			save_and_exit();
			break;
		case 1:
			close_menu();
			start_climb(&editRoute, -1);
			break;
		case 2:
			watch_beta(&editRoute, -1);
			break;
		case 3:
			rename_route();
			open_menu(MENU_EDITOR);
			ui.menuSel = 3;
			break;
		case 4:
			editRoute.angle = (uint8_t)((editRoute.angle + 1) % ANGLE_COUNT);
			editDirty = true;
			route_clear_beta(&editRoute);
			render_wall_texture(editRoute.angle);
			open_menu(MENU_EDITOR);
			ui.menuSel = 4;
			break;
		case 5:
			editRoute.color = (uint8_t)((editRoute.color + 1) % ROUTE_COLORS);
			editDirty = true;
			open_menu(MENU_EDITOR);
			ui.menuSel = 5;
			break;
		case 6:
			go_list();
			break;
		}
	} else if (menuKind == MENU_PAUSE) {
		switch (item) {
		case 0: close_menu(); break;
		case 1: start_climb(climbRoute, climbIndex); break;
		case 2:
			close_menu();
			watch_beta(climbIndex >= 0 ? &book.routes[climbIndex] : &editRoute, climbIndex);
			break;
		case 3: leave_climb(); break;
		}
	} else if (menuKind == MENU_GENERATE) {
		switch (item) {
		case 0:
			genGrade = (genGrade + 1) % 11;
			open_menu(MENU_GENERATE);
			ui.menuSel = 0;
			break;
		case 1:
			genAngle = genAngle + 1 >= ANGLE_COUNT ? GEN_ANY_ANGLE : genAngle + 1;
			open_menu(MENU_GENERATE);
			ui.menuSel = 1;
			break;
		case 2:
			start_job(JOB_GENERATE, NULL, -1);
			break;
		case 3:
			close_menu();
			break;
		}
	}
}

/* Shared menu navigation; returns true while a menu is open. */
static bool menu_input(u32 down, const touchPosition *t)
{
	if (!ui.menuOpen)
		return false;
	if (down & (KEY_DUP | KEY_CPAD_UP)) {
		ui.menuSel = (ui.menuSel + ui.menuCount - 1) % ui.menuCount;
		audio_sfx(SFX_MENU_MOVE);
	}
	if (down & (KEY_DDOWN | KEY_CPAD_DOWN)) {
		ui.menuSel = (ui.menuSel + 1) % ui.menuCount;
		audio_sfx(SFX_MENU_MOVE);
	}
	if (menuKind == MENU_GENERATE && ui.menuSel <= 1 && (down & (KEY_DLEFT | KEY_DRIGHT | KEY_CPAD_LEFT | KEY_CPAD_RIGHT))) {
		int d = (down & (KEY_DLEFT | KEY_CPAD_LEFT)) ? -1 : 1;
		if (ui.menuSel == 0)
			genGrade = (genGrade + d + 11) % 11;
		else
			genAngle = (genAngle + 1 + d + ANGLE_COUNT + 1) % (ANGLE_COUNT + 1) - 1;
		int sel = ui.menuSel;
		open_menu(MENU_GENERATE);
		ui.menuSel = sel;
		return true;
	}
	if (down & (KEY_B | KEY_START)) {
		close_menu();
		audio_sfx(SFX_MENU_MOVE);
		return true;
	}
	if (down & KEY_TOUCH) {
		int hit = hit_menu(&ui, t->px, t->py);
		if (hit >= 0) {
			ui.menuSel = hit;
			menu_choose(hit);
		}
		return true;
	}
	if (down & KEY_A)
		menu_choose(ui.menuSel);
	return true;
}

/* ------------------------------------------------------------------ */
/* Route list. */

static void list_select(int sel)
{
	if (sel < 0 || sel >= LIST_FIRST + book.count)
		return;
	if (sel != ui.listSel)
		audio_sfx(SFX_MENU_MOVE);
	ui.listSel = sel;
	ui.confirmDelete = false;
	list_scroll_to_sel();
}

static void list_action(int action)
{
	int i = ui.listSel - LIST_FIRST;
	switch (action) {
	case LBTN_BACK:
		go_title();
		audio_sfx(SFX_MENU_MOVE);
		break;
	case LBTN_DELETE:
		if (i < 0)
			break;
		if (!ui.confirmDelete) {
			ui.confirmDelete = true;
			audio_sfx(SFX_MENU_MOVE);
		} else {
			book_delete(&book, i);
			save_book();
			audio_sfx(SFX_ERASE);
			ui.confirmDelete = false;
			if (ui.listSel >= LIST_FIRST + book.count)
				ui.listSel = LIST_FIRST + book.count - 1;
			list_scroll_to_sel();
		}
		break;
	case LBTN_EDIT:
		if (ui.listSel == LIST_GENERATE) {
			open_menu(MENU_GENERATE);
			break;
		}
		if (i < 0 && book.count >= MAX_ROUTES) {
			toast("DELETE A ROUTE FIRST");
			break;
		}
		audio_sfx(SFX_MENU_SELECT);
		start_edit(i < 0 ? -1 : i);
		break;
	case LBTN_BETA:
		if (i >= 0)
			watch_beta(&book.routes[i], i);
		break;
	case LBTN_CLIMB:
		if (i >= 0)
			start_climb(&book.routes[i], i);
		break;
	}
}

static void list_input(u32 down, const touchPosition *t)
{
	bool route = ui.listSel >= LIST_FIRST;
	if (down & (KEY_DUP | KEY_CPAD_UP))
		list_select(ui.listSel - 1);
	if (down & (KEY_DDOWN | KEY_CPAD_DOWN))
		list_select(ui.listSel + 1);
	if (down & KEY_B)           list_action(LBTN_BACK);
	else if (down & KEY_Y)      list_action(LBTN_DELETE);
	else if (down & KEY_X)      list_action(LBTN_EDIT);
	else if (down & KEY_SELECT) list_action(LBTN_BETA);
	else if (down & KEY_A)      list_action(route ? LBTN_CLIMB : LBTN_EDIT);
	if (down & KEY_TOUCH) {
		int row = hit_list_row(&ui, t->px, t->py);
		int btn = hit_list_button(t->px, t->py);
		if (row >= 0) {
			/* Tapping the selected row again opens it. */
			if (row == ui.listSel)
				list_action(row >= LIST_FIRST ? LBTN_CLIMB : LBTN_EDIT);
			else
				list_select(row);
		} else if (btn >= 0) {
			list_action(btn);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Editor. */

static void clamp_edit_cam(void)
{
	if (ui.camY < 0) ui.camY = 0;
	if (ui.camY > WALL_H - EDIT_VIEW_H) ui.camY = WALL_H - EDIT_VIEW_H;
}

/* Any change to the holds makes the old grade and beta wrong. */
static void holds_changed(void)
{
	editDirty = true;
	route_clear_beta(&editRoute);
}

static void edit_tap_hold(int h)
{
	Hold *hd = &editRoute.holds[h];
	switch (ui.tool) {
	case TOOL_START:
		if (hd->role == ROLE_START) {
			hd->role = ROLE_NONE;
		} else if (route_count_role(&editRoute, ROLE_START) >= MAX_START) {
			toast("ONLY 2 START HOLDS");
			return;
		} else {
			hd->role = ROLE_START;
			if (hd->y < FLOOR_Y - 128)
				toast("TOO HIGH TO START FROM");
		}
		break;
	case TOOL_TOP:
		if (hd->role == ROLE_FINISH) {
			hd->role = ROLE_NONE;
		} else {
			for (int i = 0; i < editRoute.nholds; i++)
				if (editRoute.holds[i].role == ROLE_FINISH)
					editRoute.holds[i].role = ROLE_NONE;
			hd->role = ROLE_FINISH;
		}
		break;
	case TOOL_ERASE:
		route_remove_hold(&editRoute, h);
		audio_sfx(SFX_ERASE);
		holds_changed();
		return;
	default:
		/* Same type: turn it. Different type: swap it. */
		if (hd->type == TOOL_HOLD[ui.tool])
			hd->rot = (uint8_t)((hd->rot + 1) & 3);
		else
			hd->type = (uint8_t)TOOL_HOLD[ui.tool];
		break;
	}
	audio_sfx(SFX_PLACE);
	holds_changed();
}

static void edit_input(u32 down, u32 held, u32 up, const touchPosition *t, const circlePosition *cp)
{
	/* Scrolling. */
	float scroll = 0;
	if (held & KEY_DUP) scroll -= 4;
	if (held & KEY_DDOWN) scroll += 4;
	if (abs(cp->dy) > 20)
		scroll -= cp->dy / 156.0f * 6.0f;
	ui.camY += scroll;
	clamp_edit_cam();

	if (down & (KEY_L | KEY_R)) {
		ui.tool = (ui.tool + ((down & KEY_L) ? TOOL_COUNT - 1 : 1)) % TOOL_COUNT;
		toast(TOOL_NAMES[ui.tool]);
		audio_sfx(SFX_MENU_MOVE);
	}
	if (down & KEY_X) {
		start_climb(&editRoute, -1);
		return;
	}
	if (down & KEY_START) {
		open_menu(MENU_EDITOR);
		return;
	}

	float wx = t->px, wy = t->py + ui.camY;
	if (down & KEY_TOUCH) {
		int tool = hit_tool(t->px, t->py);
		if (tool >= 0) {
			ui.tool = tool;
			toast(TOOL_NAMES[tool]);
			audio_sfx(SFX_MENU_MOVE);
			return;
		}
		int h = route_hold_at(&editRoute, wx, wy, 2);
		dragHold = -1;
		dragMoved = dragNew = false;
		dragStartX = t->px;
		dragStartY = t->py;
		if (h >= 0) {
			dragHold = h;
		} else if (TOOL_HOLD[ui.tool] >= 0) {
			if (wy > HOLD_MAX_Y + 6) {
				toast("THAT'S THE CRASH PAD");
				return;
			}
			int k = route_add_hold(&editRoute, (int)wx, (int)wy, TOOL_HOLD[ui.tool]);
			if (k < 0) {
				toast("ROUTE IS FULL (48 HOLDS)");
				return;
			}
			editRoute.holds[k].rot = (uint8_t)(editRoute.nholds & 3);
			dragHold = k;
			dragNew = true;
			holds_changed();
			audio_sfx(SFX_PLACE);
		} else if (ui.tool != TOOL_ERASE) {
			toast("TAP A HOLD TO MARK IT");
		}
	} else if ((held & KEY_TOUCH) && dragHold >= 0) {
		if (abs(t->px - dragStartX) + abs(t->py - dragStartY) > 5)
			dragMoved = true;
		if (dragMoved && ui.tool != TOOL_ERASE) {
			/* Drag near the edges to scroll while carrying a hold. */
			if (t->py < 14) ui.camY -= 3;
			if (t->py > EDIT_VIEW_H - 14 && t->py < EDIT_VIEW_H) ui.camY += 3;
			clamp_edit_cam();
			int x = t->px, y = (int)(fminf(t->py, EDIT_VIEW_H - 1) + ui.camY);
			Hold *hd = &editRoute.holds[dragHold];
			hd->x = (int16_t)(x < 8 ? 8 : x > WALL_W - 8 ? WALL_W - 8 : x);
			hd->y = (int16_t)(y < 8 ? 8 : y > HOLD_MAX_Y ? HOLD_MAX_Y : y);
			holds_changed();
		}
	}
	if ((up & KEY_TOUCH) && dragHold >= 0) {
		if (!dragMoved && !dragNew)
			edit_tap_hold(dragHold);
		else if (dragMoved)
			audio_sfx(SFX_PLACE);
		dragHold = -1;
	}
	ui.activeHold = dragHold;
}

/* ------------------------------------------------------------------ */
/* Climbing. */

static int limb_for_button(u32 keys)
{
	if (keys & KEY_L) return LIMB_LH;
	if (keys & KEY_R) return LIMB_RH;
	if (keys & KEY_Y) return LIMB_LF;
	if (keys & KEY_A) return LIMB_RF;
	return -1;
}

static const u32 LIMB_KEYS[LIMBS] = { KEY_L, KEY_R, KEY_Y, KEY_A };

static void climb_input(u32 down, u32 held, u32 up, const touchPosition *t, const circlePosition *cp)
{
	if (ui.watching) {
		/* The beta plays by itself; A replays it at the end, B leaves. */
		if (down & (KEY_B | KEY_START))
			leave_climb();
		else if ((down & KEY_A) && climber.state != CL_CLIMBING)
			start_climb_mode(climbRoute, climbIndex, true);
		return;
	}
	if (down & KEY_SELECT) {
		if (climbRoute->nbeta) {
			ui.showBeta = !ui.showBeta;
			toast(ui.showBeta ? "BETA SHOWN: FOLLOW THE NUMBERS" : "BETA HIDDEN");
		} else {
			toast("NO BETA YET: TRY WATCH THE BETA");
		}
	}
	if (climber.state == CL_TOPPED || climber.state == CL_FELL) {
		if (down & KEY_A)
			start_climb(climbRoute, climbIndex);
		else if (down & (KEY_B | KEY_START))
			leave_climb();
		return;
	}
	if (down & KEY_START) {
		if (climber.moving >= 0)
			climb_release(&climber);
		touchLimb = buttonLimb = -1;
		open_menu(MENU_PAUSE);
		return;
	}
	if (climber.state != CL_CLIMBING)
		return;

	/* Stylus: grab a hand or foot and drag it onto a hold. */
	if ((down & KEY_TOUCH) && buttonLimb < 0) {
		float wx = t->px, wy = t->py + ui.camY;
		int best = -1;
		float bestD = 18;
		for (int l = 0; l < LIMBS; l++) {
			float d = hypotf(climber.end[l].x - wx, climber.end[l].y - wy);
			if (d < bestD) {
				bestD = d;
				best = l;
			}
		}
		if (best >= 0 && climb_pick(&climber, best))
			touchLimb = best;
	}
	if (touchLimb >= 0) {
		if (held & KEY_TOUCH) {
			climb_aim(&climber, t->px, t->py + ui.camY);
		} else {
			climb_release(&climber);
			touchLimb = -1;
		}
		return;
	}

	/* Buttons: hold L / R / Y / A and steer with the Circle Pad or D-Pad. */
	if (buttonLimb < 0) {
		int l = limb_for_button(down);
		if (l >= 0 && climb_pick(&climber, l)) {
			buttonLimb = l;
			aimX = climber.end[l].x;
			aimY = climber.end[l].y;
		}
	}
	if (buttonLimb >= 0) {
		if (up & LIMB_KEYS[buttonLimb]) {
			climb_release(&climber);
			buttonLimb = -1;
			return;
		}
		float dx = 0, dy = 0;
		if (abs(cp->dx) > 15) dx += cp->dx / 156.0f * 3.5f;
		if (abs(cp->dy) > 15) dy -= cp->dy / 156.0f * 3.5f;
		if (held & KEY_DLEFT) dx -= 2.5f;
		if (held & KEY_DRIGHT) dx += 2.5f;
		if (held & KEY_DUP) dy -= 2.5f;
		if (held & KEY_DDOWN) dy += 2.5f;
		aimX += dx;
		aimY += dy;
		/* Don't let the aim run off far beyond what the arm can reach. */
		Vec j = climb_joint(&climber, buttonLimb);
		float ex = aimX - j.x, ey = aimY - j.y, l = sqrtf(ex * ex + ey * ey), r = limb_reach(buttonLimb) + 12;
		if (l > r) {
			aimX = j.x + ex / l * r;
			aimY = j.y + ey / l * r;
		}
		climb_aim(&climber, aimX, aimY);
	}
}

/* Sounds for whatever the climber did since last frame. */
static void climb_sounds(void)
{
	if (climber.grabEvent != heard.grab) {
		/* Small holds sound a little higher. */
		float pitch = climber.lastGrabType == HOLD_JUG ? 0.9f : climber.lastGrabType == HOLD_SLOPER ? 0.8f : 1.15f;
		audio_sfx_pitch(SFX_GRAB, pitch);
	}
	if (climber.footEvent != heard.foot) audio_sfx(SFX_FOOT);
	if (climber.missEvent != heard.miss) audio_sfx(SFX_MISS);
	if (climber.slipEvent != heard.slip) audio_sfx(SFX_SLIP);
	if (climber.fallEvent != heard.fall) audio_sfx(SFX_FALL);
	if (climber.landEvent != heard.land) audio_sfx(SFX_LAND);
	if (climber.topEvent != heard.top)   audio_sfx(SFX_TOP);
	heard.grab = climber.grabEvent;
	heard.foot = climber.footEvent;
	heard.miss = climber.missEvent;
	heard.slip = climber.slipEvent;
	heard.fall = climber.fallEvent;
	heard.land = climber.landEvent;
	heard.top = climber.topEvent;

	if (climber.state == CL_CLIMBING && climber.stamina < 0.3f) {
		if (--pumpTimer <= 0) {
			audio_sfx(SFX_PUMP);
			pumpTimer = climber.stamina < 0.15f ? 30 : 50;
		}
	} else {
		pumpTimer = 0;
	}
}

static const char *const LIMB_WORDS[LIMBS] = { "LEFT HAND", "RIGHT HAND", "LEFT FOOT", "RIGHT FOOT" };

/* One frame of the beta playing itself, timed exactly like beta_move so it
 * repeats the solver's climb. */
static void watch_tick(void)
{
	const Route *r = climbRoute;
	if (watchWait < BETA_START_FRAMES) {
		watchWait++;
		climb_update(&climber);
		return;
	}
	if (ui.betaStep >= r->nbeta || climber.state != CL_CLIMBING) {
		climb_update(&climber);
		return;
	}
	const BetaMove *m = &r->beta[ui.betaStep];
	const Hold *h = &r->holds[m->hold];
	if (watchFrame == 0) {
		climb_pick(&climber, m->limb);
		snprintf(betaText, sizeof(betaText), "%d. %s TO %s", ui.betaStep + 1, LIMB_WORDS[m->limb],
		         h->role == ROLE_FINISH ? "THE TOP" : HOLD_INFO[h->type].name);
		ui.betaText = betaText;
	}
	if (watchFrame < BETA_REACH_FRAMES)
		climb_aim(&climber, h->x, h->y);
	else if (watchFrame == BETA_REACH_FRAMES)
		climb_release(&climber);
	climb_update(&climber);
	if (++watchFrame >= BETA_REACH_FRAMES + BETA_SETTLE_FRAMES) {
		watchFrame = 0;
		ui.betaStep++;
	}
}

static void climb_step(void)
{
	int wasState = climber.state;
	if (ui.watching)
		watch_tick();
	else
		climb_update(&climber);
	if (climber.state == CL_TOPPED && wasState != CL_TOPPED && climbIndex >= 0 && !ui.watching) {
		Route *r = &book.routes[climbIndex];
		r->sends++;
		if (!r->bestFrames || (uint32_t)climber.frames < r->bestFrames) {
			ui.newBest = r->bestFrames != 0;
			r->bestFrames = (uint32_t)climber.frames;
		}
		save_book();
	}
	/* Camera keeps the climber's body in the upper half of the bottom screen. */
	float target = climber.body.y - 130;
	if (target < 0) target = 0;
	if (target > WALL_H - BOT_H) target = WALL_H - BOT_H;
	ui.camY += (target - ui.camY) * (climber.state == CL_FALLING ? 0.25f : 0.08f);
}

/* ------------------------------------------------------------------ */

static void update_music(void)
{
	bool climbing = mode == MODE_CLIMB;
	audio_music(climbing ? TRACK_CLIMB : TRACK_CALM);
	audio_music_volume(climbing && (ui.menuOpen || climber.state != CL_CLIMBING) ? 0.4f : 0.8f);
	audio_update();
}

static void fill_ui(void)
{
	ui.book = &book;
	ui.audioOk = audioOk;
	ui.musicOn = musicOn;
	ui.sfxOn = sfxOn;
	switch (mode) {
	case MODE_TITLE:
		ui.screen = SCR_TITLE;
		ui.route = &book.routes[0];
		ui.climber = &titleClimber;
		if (book.count == 0)
			ui.route = NULL;
		break;
	case MODE_LIST:
		ui.screen = SCR_LIST;
		break;
	case MODE_EDIT:
		ui.screen = SCR_EDIT;
		ui.route = &editRoute;
		break;
	case MODE_CLIMB:
		ui.screen = SCR_CLIMB;
		ui.route = climbRoute;
		ui.climber = &climber;
		break;
	case MODE_WORK: {
		ui.screen = SCR_WORKING;
		const Beta *b = job == JOB_GENERATE ? &gen.beta : &solver;
		bool live = job == JOB_GENERATE ? gen.solving : b->status == BETA_WORKING && b->route;
		ui.route = live ? b->route : NULL;
		ui.climber = live ? &b->c : NULL;
		if (live) {
			render_wall_texture(b->route->angle);
			/* Only the top screen shows the wall here: keep the climber on it. */
			float target = b->c.body.y + 100;
			if (target < TOP_H) target = TOP_H;
			if (target > WALL_H) target = WALL_H;
			ui.camY += (target - ui.camY) * 0.15f;
		}
		break;
	}
	}
}

static u8 topBg[TOP_W * TOP_H * 3];

static void draw_top(gfx3dSide_t side, float depth)
{
	Canvas cv;
	u8 *fb = gfxGetFramebuffer(GFX_TOP, side, NULL, NULL);
	memcpy(fb, topBg, sizeof(topBg));
	cv_init_rotated(&cv, fb, TOP_W, TOP_H);
	render_top(&cv, &ui, depth);
}

static void draw_frame(void)
{
	/* With the 3D slider up, draw each eye with nearer things shifted apart. */
	static bool stereo;
	float slider = osGet3DSliderState();
	bool want = slider > 0.01f && mode != MODE_LIST;
	if (want != stereo) {
		stereo = want;
		gfxSet3D(stereo);
	}
	draw_top(GFX_LEFT, stereo ? slider * 3.0f : 0);
	if (stereo)
		draw_top(GFX_RIGHT, -slider * 3.0f);

	Canvas cv;
	u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	cv_init_rotated(&cv, fb, BOT_W, BOT_H);
	render_bottom(&cv, &ui);

	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

int main(void)
{
	gfxInitDefault();
	osSetSpeedupEnable(true); /* 804 MHz on New 3DS */
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, true);
	Canvas bg;
	cv_init_rotated(&bg, topBg, TOP_W, TOP_H);
	render_top_backdrop(&bg);

	/* Show a loading screen while the music is synthesised. */
	ui.screen = SCR_LOADING;
	for (int i = 0; i < 2; i++)
		draw_frame();
	audioOk = audio_init();

	if (!book_load(&book, SAVE_PATH) || book.count == 0) {
		book_samples(&book);
		save_book();
	}
	ui.listSel = book.count > 0 ? LIST_FIRST : LIST_NEW;
	go_title();
	/* Routes from older saves (and the samples) get their grade and beta now. */
	if (next_ungraded(0) >= 0)
		start_job(JOB_GRADE_ALL, NULL, -1);

	/* Logic runs on a fixed 60 Hz clock, however long a frame takes to draw. */
	u64 lastTick = svcGetSystemTick();
	u64 tickAcc = 0;
	const u64 TICKS_PER_STEP = SYSCLOCK_ARM11 / 60;

	while (aptMainLoop()) {
		u64 now = svcGetSystemTick();
		tickAcc += now - lastTick;
		lastTick = now;
		int steps = (int)(tickAcc / TICKS_PER_STEP);
		tickAcc -= (u64)steps * TICKS_PER_STEP;
		if (steps > 4) /* after a long stall (e.g. HOME menu), don't fast-forward */
			steps = 4;

		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp();
		touchPosition touch;
		circlePosition cp;
		hidTouchRead(&touch);
		hidCircleRead(&cp);

		if (menu_input(down, &touch)) {
			/* A menu swallows input for the screen behind it. */
		} else switch (mode) {
		case MODE_TITLE:
			if (down & KEY_START)
				goto quit;
			if (down & KEY_X) {
				musicOn = !musicOn;
				audio_enable(musicOn, sfxOn);
			}
			if (down & KEY_Y) {
				sfxOn = !sfxOn;
				audio_enable(musicOn, sfxOn);
				audio_sfx(SFX_MENU_MOVE);
			}
			if ((down & KEY_A) || ((down & KEY_TOUCH) && hit_title_button(touch.px, touch.py))) {
				audio_sfx(SFX_MENU_SELECT);
				go_list();
			}
			break;
		case MODE_LIST:
			list_input(down, &touch);
			break;
		case MODE_EDIT:
			edit_input(down, held, up, &touch, &cp);
			break;
		case MODE_CLIMB:
			climb_input(down, held, up, &touch, &cp);
			break;
		case MODE_WORK:
			if (down & KEY_B)
				job_cancel();
			else
				job_work();
			break;
		}

		if (mode == MODE_CLIMB && !ui.menuOpen) {
			for (int i = 0; i < steps; i++)
				climb_step();
			climb_sounds();
		}
		if (mode == MODE_TITLE)
			render_wall_texture(book.count ? book.routes[0].angle : ANGLE_VERT);
		if (ui.toastTimer > 0)
			ui.toastTimer -= steps;

		update_music();
		fill_ui();
		draw_frame();
		ui.frame++;
	}

quit:
	audio_exit();
	gfxExit();
	return 0;
}
