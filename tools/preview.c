/* Host-side preview: renders sample screens to PPM files so the look can be
 * checked without a 3DS. Build: see tools/preview.sh */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "beta.h"
#include "gen.h"
#include "render.h"

static uint8_t top[TOP_W * TOP_H * 3], bot[BOT_W * BOT_H * 3];

static void save(const char *name, const uint8_t *buf, int w, int h)
{
	FILE *f = fopen(name, "wb");
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) {
		uint8_t px[3] = { buf[i * 3 + 2], buf[i * 3 + 1], buf[i * 3] };
		fwrite(px, 1, 3, f);
	}
	fclose(f);
}

static void shot(const char *dir, const char *prefix, const UiState *ui)
{
	Canvas ct, cb;
	cv_init_linear(&ct, top, TOP_W, TOP_H);
	cv_init_linear(&cb, bot, BOT_W, BOT_H);
	render_top_backdrop(&ct);
	memset(bot, 0, sizeof(bot));
	render_top(&ct, ui, 0);
	render_bottom(&cb, ui);
	char name[256];
	snprintf(name, sizeof(name), "%s/%s_top.ppm", dir, prefix);
	save(name, top, TOP_W, TOP_H);
	snprintf(name, sizeof(name), "%s/%s_bot.ppm", dir, prefix);
	save(name, bot, BOT_W, BOT_H);
}

static void step(Climber *c, int frames)
{
	for (int i = 0; i < frames; i++)
		climb_update(c);
}

/* Move a limb onto a hold the way a player would. */
static void move(Climber *c, int limb, int hold)
{
	climb_pick(c, limb);
	for (int i = 0; i < 25; i++) {
		climb_aim(c, c->route->holds[hold].x, c->route->holds[hold].y);
		climb_update(c);
	}
	climb_release(c);
	step(c, 20);
}

/* Move a limb to the highest hold it can actually get to. */
static void move_up(Climber *c, int limb)
{
	Climber best = *c;
	float bestY = c->end[limb].y - 4;
	for (int i = 0; i < c->route->nholds; i++) {
		const Hold *h = &c->route->holds[i];
		if (h->y >= bestY || (limb_is_hand(limb) && h->type == HOLD_FOOT))
			continue;
		if (!limb_is_hand(limb) && h->y < fminf(c->end[0].y, c->end[1].y) + 40)
			continue;
		Climber t = *c;
		move(&t, limb, i);
		if (t.state == CL_CLIMBING && t.on[limb] == i) {
			best = t;
			bestY = h->y;
		}
	}
	*c = best;
}

static float cam_for(const Climber *c)
{
	float y = c->body.y - 130;
	return y < 0 ? 0 : y > WALL_H - BOT_H ? WALL_H - BOT_H : y;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	static RouteBook book;
	book_samples(&book);
	book.routes[1].bestFrames = 60 * 83 + 20;
	book.routes[1].sends = 2;
	book.routes[1].attempts = 7;
	UiState ui;
	Climber c;

	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_TITLE; ui.frame = 30; ui.audioOk = ui.musicOn = ui.sfxOn = true;
	climb_start(&c, &book.routes[0]);
	step(&c, 30);
	ui.route = &book.routes[0]; ui.climber = &c;
	render_wall_texture(book.routes[0].angle);
	shot(dir, "title", &ui);

	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_LIST; ui.book = &book; ui.listSel = LIST_FIRST + 1;
	shot(dir, "list", &ui);
	ui.listSel = LIST_NEW;
	shot(dir, "list_new", &ui);
	ui.listSel = LIST_GENERATE;
	ui.menuOpen = true; ui.menuTitle = "GENERATE A ROUTE"; ui.menuCount = 4; ui.menuSel = 2;
	const char *gitems[] = { "<  GRADE: V4  >", "<  WALL: OVERHANG  >", "SET IT!", "BACK" };
	for (int i = 0; i < 4; i++) ui.menuItems[i] = gitems[i];
	shot(dir, "generate_menu", &ui);

	/* Editor partway up the crimp route. */
	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_EDIT; ui.route = &book.routes[1]; ui.camY = 430; ui.tool = TOOL_CRIMP; ui.activeHold = 9;
	ui.toast = "PLACED CRIMP"; ui.toastTimer = 40;
	render_wall_texture(book.routes[1].angle);
	shot(dir, "edit", &ui);
	ui.menuOpen = true; ui.menuTitle = "EDITOR"; ui.menuSel = 2;
	const char *items[] = { "SAVE & EXIT", "TEST CLIMB", "WATCH THE BETA", "RENAME", "WALL: VERTICAL", "COLOR: BLUE", "EXIT" };
	ui.menuCount = 7;
	for (int i = 0; i < 7; i++) ui.menuItems[i] = items[i];
	shot(dir, "edit_menu", &ui);

	/* Climbing the cave, mid-move. */
	const Route *cave = &book.routes[2];
	render_wall_texture(cave->angle);
	climb_start(&c, cave);
	step(&c, 30);
	static const int ORDER[] = { LIMB_LF, LIMB_RF, LIMB_RH, LIMB_LH, LIMB_LF, LIMB_RF, LIMB_RH };
	for (int i = 0; i < (int)(sizeof(ORDER) / sizeof(ORDER[0])); i++)
		move_up(&c, ORDER[i]);
	c.note = NULL;
	climb_pick(&c, LIMB_LH);
	for (int i = 0; i < 15; i++) { climb_aim(&c, c.end[LIMB_LH].x - 30, c.end[LIMB_LH].y - 60); climb_update(&c); }
	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_CLIMB; ui.route = cave; ui.climber = &c; ui.camY = cam_for(&c); ui.frame = 12;
	shot(dir, "climb", &ui);

	climb_release(&c);
	c.stamina = 0.18f; c.note = "MATCH THE TOP HOLD!"; c.noteTimer = 60;
	ui.camY = cam_for(&c);
	shot(dir, "climb_pumped", &ui);

	c.state = CL_TOPPED; c.frames = 60 * 41 + 30; ui.newBest = true;
	shot(dir, "topped", &ui);
	c.state = CL_FELL; c.note = "PUMPED OUT!";
	shot(dir, "fell", &ui);

	/* Generate a route with every hold type, grade it, and show it in the
	 * editor and as a beta replay. */
	static Generator gen;
	gen_start(&gen, 6, ANGLE_OVERHANG, 77);
	while (!gen_work(&gen, 1000))
		;
	static Route gr;
	gr = gen.result;
	/* A few of the new hold types where they're easy to see. */
	int k;
	k = route_add_hold(&gr, 40, 470, HOLD_VOLUME); if (k >= 0) gr.holds[k].rot = 0;
	k = route_add_hold(&gr, 280, 500, HOLD_VOLUME); if (k >= 0) gr.holds[k].rot = 1;
	route_add_hold(&gr, 45, 535, HOLD_POCKET);
	route_add_hold(&gr, 275, 560, HOLD_UNDERCLING);
	route_add_hold(&gr, 40, 600, HOLD_EDGE);
	render_wall_texture(gr.angle);
	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_EDIT; ui.route = &gr; ui.camY = 440; ui.tool = TOOL_UNDER; ui.activeHold = -1;
	ui.toast = "UNDERCLING"; ui.toastTimer = 40;
	shot(dir, "edit_new_holds", &ui);

	/* Watching the beta partway through. */
	gr = gen.result;
	climb_start(&c, &gr);
	for (int i = 0; i < BETA_START_FRAMES; i++) climb_update(&c);
	int upto = gr.nbeta / 3;
	for (int i = 0; i < upto; i++) beta_move(&c, gr.beta[i].limb, gr.beta[i].hold, NULL);
	climb_pick(&c, gr.beta[upto].limb);
	for (int i = 0; i < 12; i++) { climb_aim(&c, gr.holds[gr.beta[upto].hold].x, gr.holds[gr.beta[upto].hold].y); climb_update(&c); }
	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_CLIMB; ui.route = &gr; ui.climber = &c; ui.camY = cam_for(&c); ui.frame = 5;
	ui.watching = true; ui.betaStep = upto;
	static char bt[48];
	snprintf(bt, sizeof(bt), "%d. %s TO %s", upto + 1, gr.beta[upto].limb == 0 ? "LEFT HAND" : gr.beta[upto].limb == 1 ? "RIGHT HAND" : gr.beta[upto].limb == 2 ? "LEFT FOOT" : "RIGHT FOOT",
	         HOLD_INFO[gr.holds[gr.beta[upto].hold].type].name);
	ui.betaText = bt;
	shot(dir, "watch_beta", &ui);

	/* The working screen while the solver is on the wall. */
	static Beta b;
	beta_start(&b, &gr);
	beta_work(&b, 150);
	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_WORKING; ui.route = &gr; ui.climber = &b.c; ui.frame = 9;
	ui.camY = fminf(fmaxf(b.c.body.y + 100, TOP_H), WALL_H);
	ui.workTitle = "SETTING A ROUTE"; ui.workSub = "V6  -  ATTEMPT 2"; ui.workProgress = 0.4f;
	shot(dir, "working", &ui);
	return 0;
}
