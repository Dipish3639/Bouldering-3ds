#include "render.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *const TOOL_NAMES[TOOL_COUNT] = {
	"JUG", "CRIMP", "SLOPER", "PINCH", "FOOT", "START", "TOP", "ERASE",
};

/* Palette. */
#define C_BG_TOP    0x1A1D26
#define C_BG_BOT    0x0D0F14
#define C_PANEL     0x232733
#define C_PANEL_HI  0x2F3443
#define C_TEXT      0xF2F2F5
#define C_DIM       0x9097A8
#define C_ACCENT    0x3DD6C4
#define C_WARN      0xFF6B5E
#define C_GOLD      0xFFD54A
#define C_CEILING   0x14161C
#define C_PAD       0x2C5A9C

#define SKIN   0xEDBF96
#define SHIRT  0x2EC4B6
#define PANTS  0x3B3F58
#define SHOE   0xD9483B
#define RUBBER 0x1E1E22
#define HAIR   0x3A2A1E
#define CHALK  0xEDE6D6

/* ------------------------------------------------------------------ */
/* Wall texture: one full-height image, stored in framebuffer layout.  */

static uint8_t wallPixels[WALL_W * WALL_H * 3];
static Canvas wallCv;
static int wallAngle = -1;

static const uint32_t WALL_TINT[ANGLE_COUNT] = { 0x8E929B, 0x737985, 0x5E6471, 0x4D525E };

static uint32_t hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

void render_wall_texture(int angle)
{
	if (angle == wallAngle)
		return;
	wallAngle = angle;
	cv_init_rotated(&wallCv, wallPixels, WALL_W, WALL_H);
	uint32_t base = WALL_TINT[angle];
	/* Brighter near the top, where the gym lights are. */
	for (int y = 0; y < FLOOR_Y; y++) {
		uint32_t row = rgb_mix(rgb_mix(base, 0xFFFFFF, 36), rgb_mix(base, 0x000000, 40), y * 255 / FLOOR_Y);
		int r0 = (row >> 16) & 0xFF, g0 = (row >> 8) & 0xFF, b0 = row & 0xFF;
		for (int x = 0; x < WALL_W; x++) {
			/* Sanded texture: a little per-pixel noise. */
			int n = (int)(hash((uint32_t)(y * WALL_W + x)) & 15) - 8;
			int r = r0 + n, g = g0 + n, b = b0 + n;
			uint8_t *p = wallCv.base + x * wallCv.xs + y * wallCv.ys;
			p[0] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
			p[1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
			p[2] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
		}
	}
	/* Panel seams and the grid of bolt holes. */
	for (int x = 0; x < WALL_W; x += 80) {
		cv_blend(&wallCv, x, 0, 1, FLOOR_Y, 0x000000, 70);
		cv_blend(&wallCv, x + 1, 0, 1, FLOOR_Y, 0xFFFFFF, 18);
	}
	for (int y = 120; y < FLOOR_Y; y += 120) {
		cv_blend(&wallCv, 0, y, WALL_W, 1, 0x000000, 70);
		cv_blend(&wallCv, 0, y + 1, WALL_W, 1, 0xFFFFFF, 18);
	}
	for (int y = 10; y < FLOOR_Y - 6; y += 20)
		for (int x = 10 + ((y / 20) & 1) * 10; x < WALL_W; x += 20) {
			cv_blend(&wallCv, x, y, 2, 2, 0x000000, 110);
			cv_blend(&wallCv, x, y + 2, 2, 1, 0xFFFFFF, 25);
		}
	/* The top lip of the wall. */
	cv_fill(&wallCv, 0, 0, WALL_W, 3, rgb_mix(base, 0xFFFFFF, 90));
	cv_blend(&wallCv, 0, 3, WALL_W, 2, 0x000000, 60);
	/* Shadow where the wall meets the mat. */
	for (int i = 0; i < 10; i++)
		cv_blend(&wallCv, 0, FLOOR_Y - 10 + i, WALL_W, 1, 0x000000, i * 9);

	/* Crash pads. */
	cv_vgradient(&wallCv, 0, FLOOR_Y, WALL_W, WALL_H - FLOOR_Y, rgb_mix(C_PAD, 0xFFFFFF, 50), rgb_mix(C_PAD, 0, 60));
	cv_fill(&wallCv, 0, FLOOR_Y, WALL_W, 2, rgb_mix(C_PAD, 0xFFFFFF, 110));
	for (int x = 0; x < WALL_W; x += 107) {
		cv_blend(&wallCv, x, FLOOR_Y, 2, WALL_H - FLOOR_Y, 0x000000, 90);
		for (int y = FLOOR_Y + 5; y < WALL_H; y += 4)
			cv_blend(&wallCv, x + 5, y, 1, 2, 0xFFFFFF, 50);
	}
}

/* ------------------------------------------------------------------ */
/* Small helpers. */

static void text_shadow(Canvas *cv, const Font *f, int x, int y, const char *s,
                        uint32_t rgb, int alpha, int spacing, int align)
{
	cv_text(cv, f, x + 1, y + 2, s, 0x000000, alpha * 3 / 5, spacing, align);
	cv_text(cv, f, x, y, s, rgb, alpha, spacing, align);
}

static void panel(Canvas *cv, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
	/* Rounded corners: trim the four corner pixels. */
	cv_blend(cv, x + 1, y, w - 2, 1, rgb, alpha);
	cv_blend(cv, x, y + 1, w, h - 2, rgb, alpha);
	cv_blend(cv, x + 1, y + h - 1, w - 2, 1, rgb, alpha);
}

static void button(Canvas *cv, int x, int y, int w, int h, const char *label, bool on, bool enabled)
{
	uint32_t bg = on ? C_ACCENT : C_PANEL_HI;
	panel(cv, x, y, w, h, bg, enabled ? 255 : 110);
	if (!on)
		cv_frame(cv, x, y, w, h, 0xFFFFFF, 30);
	uint32_t fg = on ? 0x0D1A1A : enabled ? C_TEXT : C_DIM;
	cv_text(cv, &FONT_MED, x + w / 2, y + (h - FONT_MED.capH) / 2, label, fg, enabled ? 255 : 140, 1, ALIGN_CENTER);
}

void format_time(char *out, int size, uint32_t frames)
{
	uint32_t tenths = frames / 6;
	snprintf(out, size, "%u:%02u.%u", (unsigned)(tenths / 600), (unsigned)(tenths / 10 % 60), (unsigned)(tenths % 10));
}

static uint32_t route_color(const Route *r)
{
	return ROUTE_COLOR[r->color % ROUTE_COLORS];
}

static int grade_of(const Route *r)
{
	return route_grade(r);
}

/* ------------------------------------------------------------------ */
/* Holds. */

static const float ROT_COS[4] = { 1.0f, 0.7071f, 0.0f, -0.7071f };
static const float ROT_SIN[4] = { 0.0f, 0.7071f, 1.0f, 0.7071f };

/* Draw one hold centred at (x, y); `lift` is the extra stereo shift of the
 * hold itself over its shadow. */
static void draw_hold_shape(Canvas *cv, float x, float y, int type, int rot, uint32_t col, float lift)
{
	float c = ROT_COS[rot & 3], s = ROT_SIN[rot & 3];
	uint32_t dark = rgb_mix(col, 0x000000, 120);
	uint32_t light = rgb_mix(col, 0xFFFFFF, 120);
	float sx = x + 2, sy = y + 3; /* shadow on the wall */
	x += lift;
	switch (type) {
	case HOLD_JUG:
		cv_disc(cv, sx, sy, 8.5f, 0x000000, 70);
		cv_disc(cv, x + c * 2.5f, y + s * 1.5f, 6.5f, col, 255);
		cv_disc(cv, x - c * 2.0f, y - s * 1.0f, 7.5f, col, 255);
		cv_capsule(cv, x - 4.5f, y - 2.5f, x + 4.5f, y - 2.5f, 2.2f, dark, 220); /* the incut lip */
		cv_disc(cv, x - 3, y + 3, 2.2f, light, 110);
		break;
	case HOLD_CRIMP:
		cv_capsule(cv, sx - c * 6, sy - s * 6, sx + c * 6, sy + s * 6, 3.4f, 0x000000, 70);
		cv_capsule(cv, x - c * 6, y - s * 6, x + c * 6, y + s * 6, 3.0f, col, 255);
		cv_capsule(cv, x - c * 5 + s, y - s * 5 - c, x + c * 5 + s, y + s * 5 - c, 1.0f, light, 150);
		break;
	case HOLD_SLOPER:
		cv_disc(cv, sx, sy, 11.5f, 0x000000, 70);
		cv_disc(cv, x, y, 11.0f, rgb_mix(col, 0x000000, 40), 255);
		cv_disc(cv, x - 1.5f, y - 1.5f, 8.0f, col, 255);
		cv_disc(cv, x - 3.5f, y - 3.5f, 3.5f, light, 120);
		break;
	case HOLD_PINCH: {
		/* Pinches stand across the crimp direction. */
		float pc = -s, ps = c;
		cv_capsule(cv, sx - pc * 7, sy - ps * 7, sx + pc * 7, sy + ps * 7, 4.4f, 0x000000, 70);
		cv_capsule(cv, x - pc * 7, y - ps * 7, x + pc * 7, y + ps * 7, 4.0f, col, 255);
		cv_capsule(cv, x - pc * 6 - 1.2f, y - ps * 6 - 1.2f, x + pc * 4 - 1.2f, y + ps * 4 - 1.2f, 1.3f, light, 130);
		break;
	}
	default: /* HOLD_FOOT */
		cv_disc(cv, sx - 1, sy - 1, 4.3f, 0x000000, 70);
		cv_disc(cv, x, y, 4.0f, col, 255);
		cv_disc(cv, x - 1.3f, y - 1.3f, 1.5f, light, 130);
		break;
	}
	(void)dark;
}

static void draw_tag(Canvas *cv, float x, float y, const char *label)
{
	int w = text_width(&FONT_SMALL, label, 0) + 6;
	int tx = (int)x - w / 2, ty = (int)y + 12;
	panel(cv, tx, ty, w, 11, 0xFFFFFF, 235);
	cv_text(cv, &FONT_SMALL, (int)x, ty + 2, label, 0x15161A, 255, 0, ALIGN_CENTER);
}

/* Draw every hold of a route that falls inside a view. World (wx, wy) lands
 * at (wx + ox, wy + oy); `clipH` limits drawing to the top rows of cv. */
static void draw_holds(Canvas *cv, const Route *r, float ox, float oy, float lift, int clipH, int highlight)
{
	uint32_t col = route_color(r);
	int saveH = cv->h;
	cv->h = clipH;
	for (int i = 0; i < r->nholds; i++) {
		const Hold *h = &r->holds[i];
		float x = h->x + ox, y = h->y + oy;
		if (y < -20 || y > clipH + 20 || x < -20 || x > cv->w + 20)
			continue;
		draw_hold_shape(cv, x, y, h->type, h->rot, col, lift);
		if (h->role == ROLE_START)
			draw_tag(cv, x + lift, y, "S");
		else if (h->role == ROLE_FINISH)
			draw_tag(cv, x + lift, y, "TOP");
		if (i == highlight)
			cv_ring(cv, x + lift, y, HOLD_INFO[h->type].grab + 3, 1.6f, 0xFFFFFF, 200);
	}
	cv->h = saveH;
}

/* ------------------------------------------------------------------ */
/* Climber. */

static void draw_climber(Canvas *cv, const Climber *c, float ox, float oy, bool labels)
{
	float bx = c->body.x + ox, by = c->body.y + oy;
	if (by < -80 || by > cv->h + 90)
		return;
	float pump = 1.0f - c->stamina;
	uint32_t skin = rgb_mix(SKIN, 0xE0503C, (int)(pump * pump * 170));

	/* Legs first: they're behind the torso. */
	for (int f = LIMB_LF; f <= LIMB_RF; f++) {
		Vec j = climb_joint(c, f), k = climb_bend(c, f), e = c->end[f];
		cv_capsule(cv, j.x + ox, j.y + oy, k.x + ox, k.y + oy, 4.6f, PANTS, 255);
		cv_capsule(cv, k.x + ox, k.y + oy, e.x + ox, e.y + oy, 3.8f, PANTS, 255);
		/* Shoe pointing into the wall: a short capsule along the shin's end. */
		float dx = e.x - k.x, dy = e.y - k.y, l = sqrtf(dx * dx + dy * dy);
		if (l < 0.01f) l = 1;
		float toe = f == LIMB_LF ? -1.0f : 1.0f;
		cv_capsule(cv, e.x + ox - dx / l * 3, e.y + oy - dy / l * 3, e.x + ox + toe * 3, e.y + oy + 1, 3.6f, SHOE, 255);
		cv_capsule(cv, e.x + ox - 1, e.y + oy + 2.5f, e.x + ox + toe * 3.5f, e.y + oy + 2.5f, 1.4f, RUBBER, 255);
	}

	/* Torso, hips and chalk bag. */
	cv_capsule(cv, bx, by + SHOULDER_DY + 3, bx, by + HIP_DY - 4, 10.5f, SHIRT, 255);
	cv_capsule(cv, bx - 6, by + SHOULDER_DY + 6, bx - 6, by + 4, 2.0f, rgb_mix(SHIRT, 0xFFFFFF, 60), 90);
	cv_capsule(cv, bx - HIP_DX, by + HIP_DY - 2, bx + HIP_DX, by + HIP_DY - 2, 6.0f, PANTS, 255);
	cv_disc(cv, bx + 2, by + HIP_DY - 1, 4.6f, CHALK, 255);
	cv_capsule(cv, bx - 2, by + HIP_DY - 4, bx + 6, by + HIP_DY - 4, 1.2f, 0x6B6355, 255);

	/* Neck and head (seen from behind: mostly hair). */
	cv_capsule(cv, bx, by + SHOULDER_DY, bx, by + HEAD_DY + 4, 3.2f, skin, 255);
	cv_disc(cv, bx, by + HEAD_DY, 7.8f, HAIR, 255);
	cv_disc(cv, bx - 2.5f, by + HEAD_DY - 3, 3.0f, rgb_mix(HAIR, 0xFFFFFF, 40), 120);

	/* Arms. */
	for (int h = LIMB_LH; h <= LIMB_RH; h++) {
		Vec j = climb_joint(c, h), k = climb_bend(c, h), e = c->end[h];
		cv_capsule(cv, j.x + ox, j.y + oy, k.x + ox, k.y + oy, 3.9f, SHIRT, 255);
		cv_capsule(cv, k.x + ox, k.y + oy, e.x + ox, e.y + oy, 3.3f, skin, 255);
		cv_disc(cv, e.x + ox, e.y + oy, 3.7f, rgb_mix(skin, CHALK, 140), 255);
	}

	if (labels && c->state == CL_CLIMBING && c->moving < 0) {
		static const char *KEY[LIMBS] = { "L", "R", "Y", "A" };
		for (int l = 0; l < LIMBS; l++) {
			Vec e = c->end[l];
			bool left = l == LIMB_LH || l == LIMB_LF;
			float lx = e.x + ox + (left ? -11 : 11), ly = e.y + oy + (limb_is_hand(l) ? -7 : 5);
			cv_disc(cv, lx, ly, 5.5f, 0x000000, 150);
			cv_text(cv, &FONT_SMALL, (int)lx, (int)ly - 3, KEY[l], C_TEXT, 230, 0, ALIGN_CENTER);
		}
	}
}

/* Reach circle and grab target for the limb being placed. */
static void draw_reach(Canvas *cv, const Climber *c, float ox, float oy, int frame)
{
	if (c->moving < 0 || c->state != CL_CLIMBING)
		return;
	Vec j = climb_joint(c, c->moving);
	cv_ring(cv, j.x + ox, j.y + oy, limb_reach(c->moving), 1.2f, 0xFFFFFF, 60);
	Vec e = c->end[c->moving];
	if (c->targetHold >= 0) {
		const Hold *h = &c->route->holds[c->targetHold];
		float pulse = 2.0f * sinf(frame * 0.25f);
		cv_ring(cv, h->x + ox, h->y + oy, HOLD_INFO[h->type].grab + 3 + pulse, 2.0f, 0x6CF59A, 230);
	} else {
		cv_ring(cv, e.x + ox, e.y + oy, 7, 1.4f, 0xFFFFFF, 150);
	}
}

/* ------------------------------------------------------------------ */
/* Wall views. */

static void draw_ceiling(Canvas *cv, int x, int y, int w, int h, float worldTop)
{
	/* Gym ceiling above the wall: dark, with a strip light. */
	cv_fill(cv, x, y, w, h, C_CEILING);
	int lightY = (int)(-150 - worldTop) + y;
	if (lightY > y - 10 && lightY < y + h) {
		cv_fill(cv, x + 40, lightY, w - 80, 6, 0xDDE6F0);
		for (int i = 1; i < 14; i++)
			cv_blend(cv, x + 40 - i, lightY + 6 + i, w - 80 + i * 2, 1, 0xDDE6F0, 70 - i * 5);
	}
}

/* Blit the wall for world rows [worldTop, worldTop + h) to (x, y). */
static void draw_wall(Canvas *cv, int x, int y, int h, float worldTop)
{
	int wt = (int)floorf(worldTop);
	if (wt < 0) {
		int ch = -wt < h ? -wt : h;
		draw_ceiling(cv, x, y, WALL_W, ch, worldTop);
	}
	cv_blit(cv, x, y + (wt < 0 ? -wt : 0), &wallCv, 0, wt < 0 ? 0 : wt, WALL_W, h - (wt < 0 ? -wt : 0));
}

/* A thin overview of the whole route: holds, the visible window and the climber. */
static void draw_route_strip(Canvas *cv, int x, int y, int w, int h, const Route *r, float camY, const Climber *c)
{
	panel(cv, x, y, w, h, 0x000000, 120);
	float sx = (float)(w - 4) / WALL_W, sy = (float)(h - 4) / WALL_H;
	uint32_t col = route_color(r);
	for (int i = 0; i < r->nholds; i++) {
		const Hold *hd = &r->holds[i];
		float px = x + 2 + hd->x * sx, py = y + 2 + hd->y * sy;
		cv_disc(cv, px, py, hd->type == HOLD_FOOT ? 0.8f : 1.3f, hd->role == ROLE_FINISH ? 0xFFFFFF : col, 255);
	}
	int vy0 = y + 2 + (int)((camY - 240) * sy), vy1 = y + 2 + (int)((camY + 240) * sy);
	if (vy0 < y) vy0 = y;
	if (vy1 > y + h) vy1 = y + h;
	cv_frame(cv, x, vy0, w, vy1 - vy0, 0xFFFFFF, 110);
	cv_blend(cv, x + 1, y + 2 + (int)(FLOOR_Y * sy), w - 2, 1, C_PAD, 255);
	if (c)
		cv_disc(cv, x + 2 + c->body.x * sx, y + 2 + c->body.y * sy, 2.2f, SHIRT, 255);
}

/* ------------------------------------------------------------------ */
/* Top screen. */

void render_top_backdrop(Canvas *cv)
{
	cv_vgradient(cv, 0, 0, TOP_W, TOP_H, C_BG_TOP, C_BG_BOT);
}

static void top_header(Canvas *cv, const char *left, const char *right, uint32_t accent, int dx)
{
	cv_blend(cv, TOP_WALL_X, 0, WALL_W, 20, 0x000000, 150);
	cv_fill(cv, TOP_WALL_X, 19, WALL_W, 1, accent);
	cv_text(cv, &FONT_MED, TOP_WALL_X + 8 + dx, 4, left, C_TEXT, 255, 1, ALIGN_LEFT);
	cv_text(cv, &FONT_MED, TOP_WALL_X + WALL_W - 8 + dx, 4, right, C_TEXT, 230, 1, ALIGN_RIGHT);
}

static void stamina_bar(Canvas *cv, const Climber *c, int dx, int frame)
{
	int x = 14 + dx, y = 40, w = 12, h = 170;
	cv_text(cv, &FONT_SMALL, 20 + dx, 26, "GRIP", C_DIM, 255, 1, ALIGN_CENTER);
	panel(cv, x - 2, y - 2, w + 4, h + 4, 0x000000, 160);
	int fill = (int)(c->stamina * h + 0.5f);
	uint32_t col = c->stamina > 0.5f ? rgb_mix(C_GOLD, 0x6CF59A, (int)((c->stamina - 0.5f) * 510))
	                                 : rgb_mix(C_WARN, C_GOLD, (int)(c->stamina * 510));
	bool flash = c->stamina < 0.25f && (frame / 8) % 2 == 0;
	cv_fill(cv, x, y + h - fill, w, fill, flash ? 0xFFFFFF : col);
	char pct[8];
	snprintf(pct, sizeof(pct), "%d", (int)(c->stamina * 100 + 0.5f));
	cv_text(cv, &FONT_SMALL, 20 + dx, y + h + 8, pct, C_TEXT, 255, 0, ALIGN_CENTER);
	/* Arrow showing whether we're tiring or resting. */
	const char *trend = c->drain > 0.0004f ? "v" : c->drain < -0.0004f ? "^" : "-";
	cv_text(cv, &FONT_SMALL, 20 + dx, y + h + 20, trend, c->drain > 0.0004f ? C_WARN : 0x6CF59A, 255, 0, ALIGN_CENTER);
}

static void big_banner(Canvas *cv, const char *main, const char *sub, const char *hint, uint32_t col, int dx)
{
	cv_blend(cv, TOP_WALL_X, 72, WALL_W, 98, 0x000000, 170);
	cv_fill(cv, TOP_WALL_X, 72, WALL_W, 2, col);
	cv_fill(cv, TOP_WALL_X, 168, WALL_W, 2, col);
	text_shadow(cv, &FONT_HUGE, TOP_W / 2 + dx, 86, main, col, 255, 3, ALIGN_CENTER);
	if (sub)
		cv_text(cv, &FONT_MED, TOP_W / 2 + dx, 130, sub, C_TEXT, 255, 1, ALIGN_CENTER);
	if (hint)
		cv_text(cv, &FONT_SMALL, TOP_W / 2 + dx, 152, hint, C_DIM, 255, 1, ALIGN_CENTER);
}

static void top_climb(Canvas *cv, const UiState *ui, float depth)
{
	const Route *r = ui->route;
	const Climber *c = ui->climber;
	float worldTop = ui->camY - TOP_H;
	int hud = (int)lroundf(depth * 1.6f);
	draw_wall(cv, TOP_WALL_X, 0, TOP_H, worldTop);
	draw_holds(cv, r, TOP_WALL_X, -worldTop, depth * 0.5f, TOP_H, -1);
	draw_reach(cv, c, TOP_WALL_X + depth, -worldTop, ui->frame);
	draw_climber(cv, c, TOP_WALL_X + depth, -worldTop, false);

	char right[32], t[16];
	format_time(t, sizeof(t), c->frames);
	snprintf(right, sizeof(right), "%s   %d MOVES", t, c->moves);
	top_header(cv, r->name, right, route_color(r), hud);
	stamina_bar(cv, c, hud, ui->frame);
	draw_route_strip(cv, 368 + hud, 26, 24, 200, r, ui->camY, c);

	if (c->note && c->state == CL_CLIMBING) {
		int a = c->noteTimer > 20 ? 255 : c->noteTimer * 12;
		const Font *f = text_width(&FONT_BIG, c->note, 1) < WALL_W - 16 ? &FONT_BIG : &FONT_MED;
		text_shadow(cv, f, TOP_W / 2 + hud, 40, c->note, C_TEXT, a, 1, ALIGN_CENTER);
	}
	if (c->state == CL_TOPPED) {
		char sub[48];
		format_time(t, sizeof(t), c->frames);
		snprintf(sub, sizeof(sub), "%s  -  %d MOVES%s", t, c->moves, ui->newBest ? "  -  NEW BEST!" : "");
		big_banner(cv, "SENT IT!", sub, ui->testing ? "A: CLIMB AGAIN   B: BACK TO EDITOR" : "A: CLIMB AGAIN   B: ROUTE LIST",
		           C_GOLD, hud);
	} else if (c->state == CL_FELL) {
		big_banner(cv, "FELL", c->note ? c->note : "", ui->testing ? "A: TRY AGAIN   B: BACK TO EDITOR" : "A: TRY AGAIN   B: ROUTE LIST",
		           C_WARN, hud);
	}
}

static void top_edit(Canvas *cv, const UiState *ui, float depth)
{
	const Route *r = ui->route;
	float worldTop = ui->camY - TOP_H;
	int hud = (int)lroundf(depth * 1.6f);
	draw_wall(cv, TOP_WALL_X, 0, TOP_H, worldTop);
	draw_holds(cv, r, TOP_WALL_X, -worldTop, depth * 0.5f, TOP_H, ui->activeHold);

	char right[40];
	snprintf(right, sizeof(right), "V%d  %s", grade_of(r), ANGLE_NAMES[r->angle]);
	top_header(cv, r->name, right, route_color(r), hud);
	draw_route_strip(cv, 368 + hud, 26, 24, 200, r, ui->camY, NULL);

	/* Left margin: the current tool and hold count. */
	char n[16];
	snprintf(n, sizeof(n), "%d", r->nholds);
	cv_text(cv, &FONT_SMALL, 20 + hud, 28, "HOLDS", C_DIM, 255, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_MED, 20 + hud, 40, n, C_TEXT, 255, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, 20 + hud, 70, "TOOL", C_DIM, 255, 0, ALIGN_CENTER);
	if (ui->tool < HOLD_TYPES)
		draw_hold_shape(cv, 20 + hud, 94, ui->tool, 0, route_color(r), 0);
	else
		cv_text(cv, &FONT_MED, 20 + hud, 88, ui->tool == TOOL_START ? "S" : ui->tool == TOOL_TOP ? "T" : "X",
		        C_TEXT, 255, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, 20 + hud, 114, TOOL_NAMES[ui->tool], C_TEXT, 255, 0, ALIGN_CENTER);

	/* Controls, bottom of the wall area. */
	cv_blend(cv, TOP_WALL_X, TOP_H - 30, WALL_W, 30, 0x000000, 150);
	cv_text(cv, &FONT_SMALL, TOP_W / 2 + hud, TOP_H - 25, "TAP: PLACE / TAP AGAIN: TURN / DRAG: MOVE", C_TEXT, 230, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, TOP_W / 2 + hud, TOP_H - 13, "UP/DOWN: SCROLL  L/R: TOOL  X: TEST  START: MENU", C_DIM, 255, 0, ALIGN_CENTER);
}

/* A whole route at 1/3 scale (720 rows fit the 240-row screen). */
static void draw_minimap(Canvas *cv, int x, int y, const Route *r)
{
	const float k = 1.0f / 3.0f;
	int w = (int)(WALL_W * k), h = (int)(WALL_H * k);
	uint32_t base = WALL_TINT[r->angle];
	cv_vgradient(cv, x, y, w, (int)(FLOOR_Y * k), rgb_mix(base, 0xFFFFFF, 30), rgb_mix(base, 0, 40));
	cv_fill(cv, x, y + (int)(FLOOR_Y * k), w, h - (int)(FLOOR_Y * k), C_PAD);
	uint32_t col = route_color(r);
	for (int i = 0; i < r->nholds; i++) {
		const Hold *hd = &r->holds[i];
		float rad = hd->type == HOLD_FOOT ? 1.4f : hd->type == HOLD_SLOPER ? 3.2f : hd->type == HOLD_CRIMP ? 2.0f : 2.6f;
		float px = x + hd->x * k, py = y + hd->y * k;
		cv_disc(cv, px + 0.7f, py + 0.9f, rad, 0x000000, 80);
		cv_disc(cv, px, py, rad, col, 255);
		if (hd->role != ROLE_NONE)
			cv_ring(cv, px, py, rad + 2.0f, 1.0f, 0xFFFFFF, 230);
	}
	cv_frame(cv, x - 1, y, w + 2, h, 0x000000, 160);
}

static void top_list(Canvas *cv, const UiState *ui)
{
	const RouteBook *b = ui->book;
	if (ui->listSel == 0 || ui->listSel > b->count) {
		text_shadow(cv, &FONT_BIG, TOP_W / 2, 50, "SET A NEW ROUTE", C_ACCENT, 255, 1, ALIGN_CENTER);
		static const char *TIPS[] = {
			"PICK A HOLD TYPE ON THE TOOLBAR, THEN TAP THE WALL.",
			"MARK ONE OR TWO START HOLDS (S) NEAR THE MAT",
			"AND ONE TOP HOLD (T). MATCH BOTH HANDS ON IT TO SEND.",
			"",
			"JUGS ARE EASY. CRIMPS AND SLOPERS PUMP YOU OUT.",
			"FOOT CHIPS ARE ONLY GOOD FOR FEET.",
			"STEEPER WALLS PUT MORE WEIGHT ON YOUR HANDS.",
		};
		for (int i = 0; i < 7; i++)
			cv_text(cv, &FONT_SMALL, TOP_W / 2, 92 + i * 16, TIPS[i], i < 3 ? C_TEXT : C_DIM, 255, 0, ALIGN_CENTER);
		char n[32];
		snprintf(n, sizeof(n), "%d / %d ROUTES SAVED", b->count, MAX_ROUTES);
		cv_text(cv, &FONT_SMALL, TOP_W / 2, 214, n, C_DIM, 255, 1, ALIGN_CENTER);
		return;
	}
	const Route *r = &b->routes[ui->listSel - 1];
	draw_minimap(cv, 20, 0, r);
	int x = 150;
	uint32_t col = route_color(r);
	text_shadow(cv, &FONT_BIG, x, 18, r->name, C_TEXT, 255, 1, ALIGN_LEFT);
	cv_fill(cv, x, 46, 230, 2, col);
	char g[8];
	snprintf(g, sizeof(g), "V%d", grade_of(r));
	text_shadow(cv, &FONT_HUGE, x, 62, g, col == 0x2B2B33 ? C_TEXT : col, 255, 2, ALIGN_LEFT);
	cv_text(cv, &FONT_SMALL, x + text_width(&FONT_HUGE, g, 2) + 10, 66, "ESTIMATED", C_DIM, 255, 1, ALIGN_LEFT);
	cv_text(cv, &FONT_SMALL, x + text_width(&FONT_HUGE, g, 2) + 10, 82, "GRADE", C_DIM, 255, 1, ALIGN_LEFT);

	char line[48], t[16];
	int y = 116;
	snprintf(line, sizeof(line), "%s WALL", ANGLE_NAMES[r->angle]);
	cv_text(cv, &FONT_MED, x, y, line, C_TEXT, 255, 1, ALIGN_LEFT);
	snprintf(line, sizeof(line), "%s HOLDS  -  %d IN TOTAL", COLOR_NAMES[r->color % ROUTE_COLORS], r->nholds);
	cv_text(cv, &FONT_SMALL, x, y + 22, line, C_DIM, 255, 1, ALIGN_LEFT);
	if (r->bestFrames) {
		format_time(t, sizeof(t), r->bestFrames);
		snprintf(line, sizeof(line), "BEST %s", t);
		cv_text(cv, &FONT_MED, x, y + 50, line, C_GOLD, 255, 1, ALIGN_LEFT);
	} else {
		cv_text(cv, &FONT_MED, x, y + 50, "NOT SENT YET", C_DIM, 255, 1, ALIGN_LEFT);
	}
	snprintf(line, sizeof(line), "%d SENDS  /  %d ATTEMPTS", r->sends, r->attempts);
	cv_text(cv, &FONT_SMALL, x, y + 72, line, C_DIM, 255, 1, ALIGN_LEFT);
	const char *p = route_problem(r);
	if (p)
		cv_text(cv, &FONT_SMALL, x, y + 94, p, C_WARN, 255, 1, ALIGN_LEFT);
}

static void top_title(Canvas *cv, const UiState *ui, float depth)
{
	/* The first route with the climber waiting at the bottom, under the title. */
	if (ui->route && ui->climber) {
		float worldTop = WALL_H - TOP_H;
		draw_wall(cv, TOP_WALL_X, 0, TOP_H, worldTop);
		draw_holds(cv, ui->route, TOP_WALL_X, -worldTop, depth * 0.5f, TOP_H, -1);
		draw_climber(cv, ui->climber, TOP_WALL_X + depth, -worldTop, false);
		cv_blend(cv, 0, 0, TOP_W, TOP_H, 0x000000, 60);
	}
	int dx = (int)lroundf(depth * 2.2f);
	cv_blend(cv, 0, 10, TOP_W, 84, 0x000000, 150);
	text_shadow(cv, &FONT_HUGE, TOP_W / 2 + dx, 24, "BOULDER", C_TEXT, 255, 6, ALIGN_CENTER);
	cv_text(cv, &FONT_MED, TOP_W / 2 + dx, 70, "S E T   -   C L I M B   -   S E N D", C_ACCENT, 255, 0, ALIGN_CENTER);
	(void)ui;
}

void render_top(Canvas *cv, const UiState *ui, float depth)
{
	switch (ui->screen) {
	case SCR_LOADING:
		text_shadow(cv, &FONT_BIG, TOP_W / 2, 100, "BOULDER 3DS", C_TEXT, 255, 2, ALIGN_CENTER);
		cv_text(cv, &FONT_SMALL, TOP_W / 2, 136, "TUNING THE SPEAKERS...", C_DIM, 255, 1, ALIGN_CENTER);
		break;
	case SCR_TITLE: top_title(cv, ui, depth); break;
	case SCR_LIST:  top_list(cv, ui); break;
	case SCR_EDIT:  top_edit(cv, ui, depth); break;
	case SCR_CLIMB: top_climb(cv, ui, depth); break;
	}
}

/* ------------------------------------------------------------------ */
/* Bottom screen. */

static void toast(Canvas *cv, const UiState *ui)
{
	if (!ui->toast || ui->toastTimer <= 0)
		return;
	int a = ui->toastTimer > 15 ? 255 : ui->toastTimer * 17;
	int w = text_width(&FONT_MED, ui->toast, 1) + 24;
	panel(cv, (BOT_W - w) / 2, 8, w, 24, 0x000000, a * 3 / 4);
	cv_text(cv, &FONT_MED, BOT_W / 2, 14, ui->toast, C_TEXT, a, 1, ALIGN_CENTER);
}

static void popup_menu(Canvas *cv, const UiState *ui)
{
	if (!ui->menuOpen)
		return;
	cv_blend(cv, 0, 0, BOT_W, BOT_H, 0x000000, 150);
	int h = MENU_ROW_Y - 14 + ui->menuCount * MENU_ROW_H + 10;
	panel(cv, MENU_X - 10, 14, MENU_W + 20, h, C_PANEL, 250);
	cv_frame(cv, MENU_X - 10, 14, MENU_W + 20, h, 0xFFFFFF, 30);
	cv_text(cv, &FONT_MED, BOT_W / 2, 26, ui->menuTitle ? ui->menuTitle : "", C_DIM, 255, 2, ALIGN_CENTER);
	for (int i = 0; i < ui->menuCount; i++) {
		int y = MENU_ROW_Y + i * MENU_ROW_H;
		bool sel = i == ui->menuSel;
		if (sel)
			panel(cv, MENU_X, y, MENU_W, MENU_ROW_H - 4, C_ACCENT, 255);
		cv_text(cv, &FONT_MED, BOT_W / 2, y + 7, ui->menuItems[i], sel ? 0x0D1A1A : C_TEXT, 255, 1, ALIGN_CENTER);
	}
}

static void bottom_title(Canvas *cv, const UiState *ui)
{
	cv_vgradient(cv, 0, 0, BOT_W, BOT_H, C_BG_TOP, C_BG_BOT);
	int pulse = 200 + (int)(55 * sinf(ui->frame * 0.08f));
	panel(cv, TITLE_BTN_X, TITLE_BTN_Y, TITLE_BTN_W, TITLE_BTN_H, C_ACCENT, pulse);
	cv_text(cv, &FONT_BIG, BOT_W / 2, TITLE_BTN_Y + 22, "START", 0x0D1A1A, 255, 3, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, BOT_W / 2, 30, "TAP START OR PRESS A", C_DIM, 255, 1, ALIGN_CENTER);

	char line[64];
	if (ui->audioOk) {
		snprintf(line, sizeof(line), "X: MUSIC %s     Y: SOUND %s", ui->musicOn ? "ON" : "OFF", ui->sfxOn ? "ON" : "OFF");
		cv_text(cv, &FONT_SMALL, BOT_W / 2, 168, line, C_TEXT, 230, 1, ALIGN_CENTER);
	} else {
		cv_text(cv, &FONT_SMALL, BOT_W / 2, 168, "NO SOUND: /3DS/DSPFIRM.CDC IS MISSING", C_WARN, 255, 1, ALIGN_CENTER);
	}
	cv_text(cv, &FONT_SMALL, BOT_W / 2, 196, "SLIDE THE 3D SLIDER UP FOR DEPTH", C_DIM, 255, 1, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, BOT_W / 2, 220, "START: QUIT", C_DIM, 200, 1, ALIGN_CENTER);
}

static void bottom_list(Canvas *cv, const UiState *ui)
{
	const RouteBook *b = ui->book;
	cv_vgradient(cv, 0, 0, BOT_W, BOT_H, C_BG_TOP, C_BG_BOT);
	cv_text(cv, &FONT_MED, 12, 10, "ROUTES", C_TEXT, 255, 2, ALIGN_LEFT);
	char n[16];
	snprintf(n, sizeof(n), "%d/%d", b->count, MAX_ROUTES);
	cv_text(cv, &FONT_SMALL, BOT_W - 12, 12, n, C_DIM, 255, 1, ALIGN_RIGHT);

	for (int row = 0; row < LIST_ROWS; row++) {
		int i = ui->listScroll + row;
		if (i > b->count)
			break;
		int y = LIST_Y + row * LIST_ROW_H;
		bool sel = i == ui->listSel;
		panel(cv, 8, y, BOT_W - 16, LIST_ROW_H - 4, sel ? C_PANEL_HI : C_PANEL, 255);
		if (sel)
			cv_fill(cv, 8, y, 3, LIST_ROW_H - 4, C_ACCENT);
		if (i == 0) {
			bool full = b->count >= MAX_ROUTES;
			cv_text(cv, &FONT_MED, 22, y + 6, full ? "ROUTE BOOK FULL" : "+  NEW ROUTE", full ? C_DIM : C_ACCENT, 255, 1, ALIGN_LEFT);
			continue;
		}
		const Route *r = &b->routes[i - 1];
		cv_disc(cv, 24, y + 12, 5, route_color(r), 255);
		cv_ring(cv, 24, y + 12, 5.5f, 1, 0x000000, 120);
		cv_text(cv, &FONT_MED, 36, y + 6, r->name, C_TEXT, 255, 1, ALIGN_LEFT);
		char info[24];
		snprintf(info, sizeof(info), "V%d", grade_of(r));
		cv_text(cv, &FONT_MED, BOT_W - 52, y + 6, info, C_TEXT, 255, 1, ALIGN_RIGHT);
		if (r->sends)
			cv_text(cv, &FONT_SMALL, BOT_W - 18, y + 8, "SENT", C_GOLD, 255, 0, ALIGN_RIGHT);
	}
	/* Scroll hints. */
	if (ui->listScroll > 0)
		cv_text(cv, &FONT_SMALL, BOT_W / 2, LIST_Y - 9, "^", C_DIM, 255, 0, ALIGN_CENTER);
	if (ui->listScroll + LIST_ROWS <= b->count)
		cv_text(cv, &FONT_SMALL, BOT_W / 2, LIST_Y + LIST_ROWS * LIST_ROW_H - 3, "v", C_DIM, 255, 0, ALIGN_CENTER);

	bool isNew = ui->listSel == 0;
	bool canNew = b->count < MAX_ROUTES;
	static const char *LABELS[LBTN_COUNT] = { "BACK", "DELETE", "EDIT", "CLIMB" };
	for (int k = 0; k < LBTN_COUNT; k++) {
		int x = 6 + k * 78;
		const char *label = LABELS[k];
		bool enabled = true, on = false;
		if (k == LBTN_DELETE) {
			enabled = !isNew;
			if (ui->confirmDelete) {
				label = "SURE?";
				on = true;
			}
		}
		if (k == LBTN_EDIT && isNew) {
			label = "CREATE";
			enabled = canNew;
		}
		if (k == LBTN_CLIMB)
			enabled = !isNew;
		button(cv, x, LIST_BTN_Y, 74, LIST_BTN_H, label, on, enabled);
	}
	cv_text(cv, &FONT_SMALL, 6 + 0 * 78 + 37, LIST_BTN_Y + 36, "B", C_DIM, 255, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, 6 + 1 * 78 + 37, LIST_BTN_Y + 36, "Y", C_DIM, 255, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, 6 + 2 * 78 + 37, LIST_BTN_Y + 36, "X", C_DIM, 255, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, 6 + 3 * 78 + 37, LIST_BTN_Y + 36, "A", C_DIM, 255, 0, ALIGN_CENTER);
}

static void tool_icon(Canvas *cv, int t, int cx, int cy, uint32_t col)
{
	if (t < HOLD_TYPES) {
		draw_hold_shape(cv, cx, cy, t, 0, col, 0);
		return;
	}
	if (t == TOOL_START || t == TOOL_TOP) {
		const char *l = t == TOOL_START ? "S" : "TOP";
		int w = text_width(&FONT_SMALL, l, 0) + 8;
		panel(cv, cx - w / 2, cy - 7, w, 14, 0xFFFFFF, 240);
		cv_text(cv, &FONT_SMALL, cx, cy - 3, l, 0x15161A, 255, 0, ALIGN_CENTER);
		return;
	}
	cv_capsule(cv, cx - 6, cy - 6, cx + 6, cy + 6, 1.8f, C_WARN, 255);
	cv_capsule(cv, cx - 6, cy + 6, cx + 6, cy - 6, 1.8f, C_WARN, 255);
}

static void bottom_edit(Canvas *cv, const UiState *ui)
{
	const Route *r = ui->route;
	draw_wall(cv, 0, 0, EDIT_VIEW_H, ui->camY);
	draw_holds(cv, r, 0, -ui->camY, 0, EDIT_VIEW_H, ui->activeHold);
	/* Height guide for start holds. */
	float startLine = FLOOR_Y - 128 - ui->camY;
	if (startLine > 0 && startLine < EDIT_VIEW_H)
		for (int x = 0; x < BOT_W; x += 8)
			cv_blend(cv, x, (int)startLine, 4, 1, 0xFFFFFF, 90);

	/* Toolbar. */
	cv_fill(cv, 0, EDIT_VIEW_H, BOT_W, BOT_H - EDIT_VIEW_H, C_PANEL);
	cv_fill(cv, 0, EDIT_VIEW_H, BOT_W, 1, 0x000000);
	for (int t = 0; t < TOOL_COUNT; t++) {
		int x = t * TOOL_W;
		if (t == ui->tool) {
			panel(cv, x + 2, EDIT_VIEW_H + 3, TOOL_W - 4, BOT_H - EDIT_VIEW_H - 5, C_PANEL_HI, 255);
			cv_fill(cv, x + 6, BOT_H - 4, TOOL_W - 12, 2, C_ACCENT);
		}
		tool_icon(cv, t, x + TOOL_W / 2, EDIT_VIEW_H + 16, route_color(r));
		cv_text(cv, &FONT_SMALL, x + TOOL_W / 2, BOT_H - 13, TOOL_NAMES[t], t == ui->tool ? C_TEXT : C_DIM, 255, 0, ALIGN_CENTER);
	}
}

static void bottom_climb(Canvas *cv, const UiState *ui)
{
	const Climber *c = ui->climber;
	draw_wall(cv, 0, 0, BOT_H, ui->camY);
	draw_holds(cv, ui->route, 0, -ui->camY, 0, BOT_H, -1);
	draw_reach(cv, c, 0, -ui->camY, ui->frame);
	draw_climber(cv, c, 0, -ui->camY, true);
	if (c->state == CL_CLIMBING && c->moves == 0 && c->moving < 0) {
		cv_blend(cv, 0, BOT_H - 22, BOT_W, 22, 0x000000, 150);
		cv_text(cv, &FONT_SMALL, BOT_W / 2, BOT_H - 15, "DRAG A HAND OR FOOT - OR HOLD L/R/Y/A + CIRCLE PAD", C_TEXT, 255, 0, ALIGN_CENTER);
	}
}

void render_bottom(Canvas *cv, const UiState *ui)
{
	switch (ui->screen) {
	case SCR_LOADING:
		cv_vgradient(cv, 0, 0, BOT_W, BOT_H, C_BG_TOP, C_BG_BOT);
		break;
	case SCR_TITLE: bottom_title(cv, ui); break;
	case SCR_LIST:  bottom_list(cv, ui); break;
	case SCR_EDIT:  bottom_edit(cv, ui); break;
	case SCR_CLIMB: bottom_climb(cv, ui); break;
	}
	toast(cv, ui);
	popup_menu(cv, ui);
}

/* ------------------------------------------------------------------ */
/* Hit tests. */

int hit_list_row(const UiState *ui, int px, int py)
{
	if (px < 8 || px >= BOT_W - 8 || py < LIST_Y || py >= LIST_Y + LIST_ROWS * LIST_ROW_H)
		return -1;
	int i = ui->listScroll + (py - LIST_Y) / LIST_ROW_H;
	return i <= ui->book->count ? i : -1;
}

int hit_list_button(int px, int py)
{
	if (py < LIST_BTN_Y || py >= LIST_BTN_Y + LIST_BTN_H)
		return -1;
	for (int k = 0; k < LBTN_COUNT; k++) {
		int x = 6 + k * 78;
		if (px >= x && px < x + 74)
			return k;
	}
	return -1;
}

int hit_menu(const UiState *ui, int px, int py)
{
	if (!ui->menuOpen || px < MENU_X || px >= MENU_X + MENU_W || py < MENU_ROW_Y)
		return -1;
	int i = (py - MENU_ROW_Y) / MENU_ROW_H;
	return i < ui->menuCount ? i : -1;
}

int hit_tool(int px, int py)
{
	if (py < EDIT_VIEW_H || px < 0 || px >= BOT_W)
		return -1;
	return px / TOOL_W;
}

bool hit_title_button(int px, int py)
{
	return px >= TITLE_BTN_X && px < TITLE_BTN_X + TITLE_BTN_W && py >= TITLE_BTN_Y && py < TITLE_BTN_Y + TITLE_BTN_H;
}
