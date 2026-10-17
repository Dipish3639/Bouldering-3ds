#include "render.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *const TOOL_NAMES[TOOL_COUNT] = {
	"JUG", "EDGE", "CRIMP", "POCKET", "SLOPER", "PINCH", "UNDERCLING", "VOLUME", "FOOT CHIP", "START", "TOP", "ERASE",
};
const char *const TOOL_SHORT[TOOL_COUNT] = {
	"JUG", "EDGE", "CRMP", "PCKT", "SLPR", "PNCH", "UNDR", "VOL", "FOOT", "S", "TOP", "DEL",
};
const int8_t TOOL_HOLD[TOOL_COUNT] = {
	HOLD_JUG, HOLD_EDGE, HOLD_CRIMP, HOLD_POCKET, HOLD_SLOPER, HOLD_PINCH, HOLD_UNDERCLING, HOLD_VOLUME, HOLD_FOOT,
	-1, -1, -1,
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

void route_grade_label(char *out, int size, const Route *r)
{
	if (r->grade == GRADE_UNKNOWN)
		snprintf(out, size, "~V%d", route_grade(r));
	else
		grade_text(out, size, r->grade);
}

/* ------------------------------------------------------------------ */
/* Stereo 3D (top screen only).
 *
 * Depth is measured in pixels of shift per eye at full 3D slider; positive
 * comes out of the screen. The wall itself is tilted by its angle: a slab's
 * top leans away, an overhang's and a roof's lean out towards you, and the
 * middle of the top screen sits at the screen's surface. Holds stand out
 * from the wall and the climber stands out further. */

static struct {
	float eye;   /* +slider for the left eye, -slider for the right, 0 = flat */
	float refY;  /* world row at the screen surface */
	int angle;
} st;

/* Depth gained per world row going up the wall. */
static const float WALL_LEAN[ANGLE_COUNT] = { -0.02f, 0.0f, 0.026f, 0.04f };
static const float HOLD_POP[HOLD_TYPES] = {
	[HOLD_JUG] = 2.0f, [HOLD_CRIMP] = 1.0f, [HOLD_SLOPER] = 1.8f, [HOLD_PINCH] = 1.6f, [HOLD_FOOT] = 0.7f,
	[HOLD_EDGE] = 1.2f, [HOLD_POCKET] = 1.4f, [HOLD_UNDERCLING] = 1.8f, [HOLD_VOLUME] = 2.8f,
};
#define CLIMBER_POP 3.6f

static void stereo_set(float eye, float refY, int angle)
{
	st.eye = eye;
	st.refY = refY;
	st.angle = angle >= 0 && angle < ANGLE_COUNT ? angle : ANGLE_VERT;
}

/* Horizontal shift for something `pop` out from the wall at world row wy. */
static float sdx(float wy, float pop)
{
	if (st.eye == 0)
		return 0;
	return st.eye * (WALL_LEAN[st.angle] * (st.refY - wy) + pop);
}

/* ------------------------------------------------------------------ */
/* Holds. */

static const float ROT_COS[4] = { 1.0f, 0.7071f, 0.0f, -0.7071f };
static const float ROT_SIN[4] = { 0.0f, 0.7071f, 1.0f, 0.7071f };

/* Volumes: big triangular features with three lit faces. */
static void draw_volume(Canvas *cv, float x, float y, float sx, float sy, int rot, uint32_t col, float k)
{
	float R = 17.0f * k;
	float a0 = -1.5708f + rot * 1.5708f;
	float vx[3], vy[3];
	for (int i = 0; i < 3; i++) {
		vx[i] = cosf(a0 + i * 2.0944f) * R;
		vy[i] = sinf(a0 + i * 2.0944f) * R;
	}
	cv_tri(cv, sx + vx[0] + 1, sy + vy[0] + 1, sx + vx[1] + 1, sy + vy[1] + 1, sx + vx[2] + 1, sy + vy[2] + 1, 0x000000, 80);
	uint32_t base = rgb_mix(col, 0x8A8F99, 90);
	float apx = x - 1.5f * k, apy = y - 1.5f * k; /* the peak, nudged towards the light */
	for (int i = 0; i < 3; i++) {
		int n = (i + 1) % 3;
		float mx = (vx[i] + vx[n]) * 0.5f, my = (vy[i] + vy[n]) * 0.5f, ml = sqrtf(mx * mx + my * my);
		float lit = (mx * -0.6f + my * -0.8f) / (ml > 0 ? ml : 1); /* light from the top left */
		uint32_t face = lit > 0 ? rgb_mix(base, 0xFFFFFF, (int)(lit * 80)) : rgb_mix(base, 0x000000, (int)(-lit * 90));
		cv_tri(cv, apx, apy, x + vx[i], y + vy[i], x + vx[n], y + vy[n], face, 255);
	}
}

/* Draw one hold centred at (x, y), scaled by k; `lift` is the extra stereo
 * shift of the hold itself over its shadow. */
static void draw_hold_shape(Canvas *cv, float x, float y, int type, int rot, uint32_t col, float lift, float k)
{
	float c = ROT_COS[rot & 3], s = ROT_SIN[rot & 3];
	uint32_t dark = rgb_mix(col, 0x000000, 120);
	uint32_t light = rgb_mix(col, 0xFFFFFF, 120);
	float sx = x + 2 * k, sy = y + 3 * k; /* shadow on the wall */
	x += lift;
	switch (type) {
	case HOLD_JUG:
		cv_disc(cv, sx, sy, 8.5f * k, 0x000000, 70);
		cv_disc(cv, x + c * 2.5f * k, y + s * 1.5f * k, 6.5f * k, col, 255);
		cv_disc(cv, x - c * 2.0f * k, y - s * 1.0f * k, 7.5f * k, col, 255);
		cv_capsule(cv, x - 4.5f * k, y - 2.5f * k, x + 4.5f * k, y - 2.5f * k, 2.2f * k, dark, 220); /* the incut lip */
		cv_disc(cv, x - 3 * k, y + 3 * k, 2.2f * k, light, 110);
		break;
	case HOLD_UNDERCLING:
		/* A jug flipped over: the lip is underneath. */
		cv_disc(cv, sx, sy, 8.0f * k, 0x000000, 70);
		cv_disc(cv, x + c * 2.5f * k, y - s * 1.0f * k, 6.0f * k, col, 255);
		cv_disc(cv, x - c * 2.0f * k, y + s * 0.5f * k, 7.0f * k, col, 255);
		cv_capsule(cv, x - 4.5f * k, y + 3.0f * k, x + 4.5f * k, y + 3.0f * k, 2.2f * k, dark, 230);
		cv_disc(cv, x - 3 * k, y - 3 * k, 2.2f * k, light, 120);
		break;
	case HOLD_CRIMP:
		cv_capsule(cv, sx - c * 6 * k, sy - s * 6 * k, sx + c * 6 * k, sy + s * 6 * k, 3.4f * k, 0x000000, 70);
		cv_capsule(cv, x - c * 6 * k, y - s * 6 * k, x + c * 6 * k, y + s * 6 * k, 3.0f * k, col, 255);
		cv_capsule(cv, x - c * 5 * k + s, y - s * 5 * k - c, x + c * 5 * k + s, y + s * 5 * k - c, 1.0f * k, light, 150);
		break;
	case HOLD_EDGE:
		/* A long, flat rail: deeper than a crimp. */
		cv_capsule(cv, sx - c * 8 * k, sy - s * 8 * k, sx + c * 8 * k, sy + s * 8 * k, 4.4f * k, 0x000000, 70);
		cv_capsule(cv, x - c * 8 * k, y - s * 8 * k, x + c * 8 * k, y + s * 8 * k, 4.0f * k, col, 255);
		cv_capsule(cv, x - c * 7 * k + s * 2, y - s * 7 * k - c * 2, x + c * 7 * k + s * 2, y + s * 7 * k - c * 2,
		           1.3f * k, light, 160);
		cv_capsule(cv, x - c * 7 * k - s * 2, y - s * 7 * k + c * 2, x + c * 7 * k - s * 2, y + s * 7 * k + c * 2,
		           1.0f * k, dark, 120);
		break;
	case HOLD_SLOPER:
		cv_disc(cv, sx, sy, 11.5f * k, 0x000000, 70);
		cv_disc(cv, x, y, 11.0f * k, rgb_mix(col, 0x000000, 40), 255);
		cv_disc(cv, x - 1.5f * k, y - 1.5f * k, 8.0f * k, col, 255);
		cv_disc(cv, x - 3.5f * k, y - 3.5f * k, 3.5f * k, light, 120);
		break;
	case HOLD_POCKET:
		/* A round hold with a two-finger hole in it. */
		cv_disc(cv, sx, sy, 7.5f * k, 0x000000, 70);
		cv_disc(cv, x, y, 7.0f * k, col, 255);
		cv_disc(cv, x - 2 * k, y - 2.5f * k, 2.0f * k, light, 110);
		cv_disc(cv, x + 0.5f * k, y + 0.5f * k, 3.0f * k, 0x101014, 230);
		cv_ring(cv, x + 0.5f * k, y + 0.5f * k, 3.4f * k, 1.0f, dark, 200);
		break;
	case HOLD_PINCH: {
		/* Pinches stand across the crimp direction. */
		float pc = -s, ps = c;
		cv_capsule(cv, sx - pc * 7 * k, sy - ps * 7 * k, sx + pc * 7 * k, sy + ps * 7 * k, 4.4f * k, 0x000000, 70);
		cv_capsule(cv, x - pc * 7 * k, y - ps * 7 * k, x + pc * 7 * k, y + ps * 7 * k, 4.0f * k, col, 255);
		cv_capsule(cv, x - (pc * 6 + 1.2f) * k, y - (ps * 6 + 1.2f) * k, x + (pc * 4 - 1.2f) * k, y + (ps * 4 - 1.2f) * k,
		           1.3f * k, light, 130);
		break;
	}
	case HOLD_VOLUME:
		draw_volume(cv, x, y, sx, sy, rot, col, k);
		break;
	default: /* HOLD_FOOT */
		cv_disc(cv, sx - 1, sy - 1, 4.3f * k, 0x000000, 70);
		cv_disc(cv, x, y, 4.0f * k, col, 255);
		cv_disc(cv, x - 1.3f * k, y - 1.3f * k, 1.5f * k, light, 130);
		break;
	}
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
static void draw_holds(Canvas *cv, const Route *r, float ox, float oy, int clipH, int highlight)
{
	uint32_t col = route_color(r);
	int saveH = cv->h;
	cv->h = clipH;
	/* Volumes first: other holds can be bolted on top of them. */
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < r->nholds; i++) {
			const Hold *h = &r->holds[i];
			if ((h->type == HOLD_VOLUME) != (pass == 0))
				continue;
			/* The shadow sits on the (tilted) wall; the hold stands out from it. */
			float x = h->x + ox + sdx(h->y, 0), y = h->y + oy;
			float lift = st.eye * HOLD_POP[h->type];
			if (y < -24 || y > clipH + 24 || x < -24 || x > cv->w + 24)
				continue;
			draw_hold_shape(cv, x, y, h->type, h->rot, col, lift, 1.0f);
			if (h->role == ROLE_START)
				draw_tag(cv, x + lift, y, "S");
			else if (h->role == ROLE_FINISH)
				draw_tag(cv, x + lift, y, "TOP");
			if (i == highlight)
				cv_ring(cv, x + lift, y, HOLD_INFO[h->type].grab + 3, 1.6f, 0xFFFFFF, 200);
		}
	cv->h = saveH;
}

/* Numbered beta moves next to their holds: hands white, feet grey, moves
 * already made dimmed. Offsets keep the four limbs' numbers apart. */
static void draw_beta(Canvas *cv, const Route *r, float ox, float oy, int clipH, int done)
{
	static const float DX[LIMBS] = { -10, 10, -10, 10 }, DY[LIMBS] = { -10, -10, 10, 10 };
	int saveH = cv->h;
	cv->h = clipH;
	for (int i = 0; i < r->nbeta; i++) {
		const BetaMove *m = &r->beta[i];
		const Hold *h = &r->holds[m->hold];
		float x = h->x + ox + sdx(h->y, HOLD_POP[h->type] + 0.8f) + DX[m->limb], y = h->y + oy + DY[m->limb];
		if (y < -8 || y > clipH + 8)
			continue;
		bool hand = m->limb <= LIMB_RH;
		int a = i < done ? 90 : 235;
		bool next = i == done;
		cv_disc(cv, x, y, next ? 7.5f : 6.0f, next ? C_ACCENT : hand ? 0xFFFFFF : 0xB8BCC6, a);
		char n[12];
		snprintf(n, sizeof(n), "%d", i + 1);
		cv_text(cv, &FONT_SMALL, (int)x, (int)y - 3, n, 0x15161A, a, 0, ALIGN_CENTER);
	}
	cv->h = saveH;
}

/* ------------------------------------------------------------------ */
/* Climber. */

/* Screen x and y of a world point on the climber, shifted for stereo. */
#define CP(wx, wy) ((wx) + ox + sdx((wy), CLIMBER_POP)), ((wy) + oy)

static void draw_climber(Canvas *cv, const Climber *c, float ox, float oy, bool labels)
{
	float bx = c->body.x, by = c->body.y;
	if (by + oy < -80 || by + oy > cv->h + 90)
		return;
	float pump = 1.0f - c->stamina;
	uint32_t skin = rgb_mix(SKIN, 0xE0503C, (int)(pump * pump * 170));

	/* Legs first: they're behind the torso. */
	for (int f = LIMB_LF; f <= LIMB_RF; f++) {
		Vec j = climb_joint(c, f), k = climb_bend(c, f), e = c->end[f];
		cv_capsule(cv, CP(j.x, j.y), CP(k.x, k.y), 4.6f, PANTS, 255);
		cv_capsule(cv, CP(k.x, k.y), CP(e.x, e.y), 3.8f, PANTS, 255);
		/* Shoe pointing into the wall: a short capsule along the shin's end. */
		float dx = e.x - k.x, dy = e.y - k.y, l = sqrtf(dx * dx + dy * dy);
		if (l < 0.01f) l = 1;
		float toe = f == LIMB_LF ? -1.0f : 1.0f;
		cv_capsule(cv, CP(e.x - dx / l * 3, e.y - dy / l * 3), CP(e.x + toe * 3, e.y + 1), 3.6f, SHOE, 255);
		cv_capsule(cv, CP(e.x - 1, e.y + 2.5f), CP(e.x + toe * 3.5f, e.y + 2.5f), 1.4f, RUBBER, 255);
	}

	/* Torso, hips and chalk bag. */
	cv_capsule(cv, CP(bx, by + SHOULDER_DY + 3), CP(bx, by + HIP_DY - 4), 10.5f, SHIRT, 255);
	cv_capsule(cv, CP(bx - 6, by + SHOULDER_DY + 6), CP(bx - 6, by + 4), 2.0f, rgb_mix(SHIRT, 0xFFFFFF, 60), 90);
	cv_capsule(cv, CP(bx - HIP_DX, by + HIP_DY - 2), CP(bx + HIP_DX, by + HIP_DY - 2), 6.0f, PANTS, 255);
	cv_disc(cv, CP(bx + 2, by + HIP_DY - 1), 4.6f, CHALK, 255);
	cv_capsule(cv, CP(bx - 2, by + HIP_DY - 4), CP(bx + 6, by + HIP_DY - 4), 1.2f, 0x6B6355, 255);

	/* Neck and head (seen from behind: mostly hair). */
	cv_capsule(cv, CP(bx, by + SHOULDER_DY), CP(bx, by + HEAD_DY + 4), 3.2f, skin, 255);
	cv_disc(cv, CP(bx, by + HEAD_DY), 7.8f, HAIR, 255);
	cv_disc(cv, CP(bx - 2.5f, by + HEAD_DY - 3), 3.0f, rgb_mix(HAIR, 0xFFFFFF, 40), 120);

	/* Arms. */
	for (int h = LIMB_LH; h <= LIMB_RH; h++) {
		Vec j = climb_joint(c, h), k = climb_bend(c, h), e = c->end[h];
		cv_capsule(cv, CP(j.x, j.y), CP(k.x, k.y), 3.9f, SHIRT, 255);
		cv_capsule(cv, CP(k.x, k.y), CP(e.x, e.y), 3.3f, skin, 255);
		cv_disc(cv, CP(e.x, e.y), 3.7f, rgb_mix(skin, CHALK, 140), 255);
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
	cv_ring(cv, CP(j.x, j.y), limb_reach(c->moving), 1.2f, 0xFFFFFF, 60);
	Vec e = c->end[c->moving];
	if (c->targetHold >= 0) {
		const Hold *h = &c->route->holds[c->targetHold];
		float pulse = 2.0f * sinf(frame * 0.25f);
		cv_ring(cv, h->x + ox + sdx(h->y, HOLD_POP[h->type]), h->y + oy, HOLD_INFO[h->type].grab + 3 + pulse, 2.0f,
		        0x6CF59A, 230);
	} else {
		cv_ring(cv, CP(e.x, e.y), 7, 1.4f, 0xFFFFFF, 150);
	}
}

#undef CP

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
	int top = wt < 0 ? -wt : 0;
	if (st.eye == 0) {
		cv_blit(cv, x, y + top, &wallCv, 0, wt + top, WALL_W, h - top);
		return;
	}
	/* In 3D each row shifts by the wall's depth there. Rows with the same
	 * shift are blitted together, clipped to the wall's window. */
	Canvas view = *cv;
	view.base = cv->base + x * cv->xs;
	view.w = WALL_W;
	int r0 = top;
	int s0 = (int)lroundf(sdx(wt + r0, 0));
	for (int r = top + 1; r <= h; r++) {
		int sh = r < h ? (int)lroundf(sdx(wt + r, 0)) : s0 + 1000;
		if (sh == s0)
			continue;
		cv_blit(&view, s0, y + r0, &wallCv, 0, wt + r0, WALL_W, r - r0);
		/* Fill the sliver the shift uncovers with the gym's dark backdrop. */
		if (s0 > 0)
			cv_fill(&view, 0, y + r0, s0, r - r0, C_BG_TOP);
		else if (s0 < 0)
			cv_fill(&view, WALL_W + s0, y + r0, -s0, r - r0, C_BG_TOP);
		r0 = r;
		s0 = sh;
	}
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
	int hud = (int)lroundf(depth * 2.5f);
	draw_wall(cv, TOP_WALL_X, 0, TOP_H, worldTop);
	draw_holds(cv, r, TOP_WALL_X, -worldTop, TOP_H, -1);
	if (ui->showBeta || ui->watching)
		draw_beta(cv, r, TOP_WALL_X, -worldTop, TOP_H, ui->watching ? ui->betaStep : 0);
	draw_reach(cv, c, TOP_WALL_X, -worldTop, ui->frame);
	draw_climber(cv, c, TOP_WALL_X, -worldTop, false);

	char right[32], t[16];
	format_time(t, sizeof(t), c->frames);
	if (ui->watching)
		snprintf(right, sizeof(right), "BETA %d / %d", ui->betaStep < r->nbeta ? ui->betaStep + 1 : r->nbeta, r->nbeta);
	else
		snprintf(right, sizeof(right), "%s   %d MOVES", t, c->moves);
	top_header(cv, r->name, right, route_color(r), hud);
	if (ui->watching && ui->betaText && c->state == CL_CLIMBING)
		text_shadow(cv, &FONT_MED, TOP_W / 2 + hud, 30, ui->betaText, C_ACCENT, 255, 1, ALIGN_CENTER);
	stamina_bar(cv, c, hud, ui->frame);
	draw_route_strip(cv, 368 + hud, 26, 24, 200, r, ui->camY, c);

	if (c->note && c->state == CL_CLIMBING && !ui->watching) {
		int a = c->noteTimer > 20 ? 255 : c->noteTimer * 12;
		const Font *f = text_width(&FONT_BIG, c->note, 1) < WALL_W - 16 ? &FONT_BIG : &FONT_MED;
		text_shadow(cv, f, TOP_W / 2 + hud, 40, c->note, C_TEXT, a, 1, ALIGN_CENTER);
	}
	if (c->state == CL_TOPPED && ui->watching) {
		char sub[48];
		snprintf(sub, sizeof(sub), "%d MOVES TO THE TOP", r->nbeta);
		big_banner(cv, "THAT'S THE BETA", sub, "A: WATCH AGAIN   B: BACK", C_ACCENT, hud);
	} else if (c->state == CL_TOPPED) {
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
	int hud = (int)lroundf(depth * 2.5f);
	draw_wall(cv, TOP_WALL_X, 0, TOP_H, worldTop);
	draw_holds(cv, r, TOP_WALL_X, -worldTop, TOP_H, ui->activeHold);

	char right[40], g[8];
	route_grade_label(g, sizeof(g), r);
	snprintf(right, sizeof(right), "%s  %s", g, ANGLE_NAMES[r->angle]);
	top_header(cv, r->name, right, route_color(r), hud);
	draw_route_strip(cv, 368 + hud, 26, 24, 200, r, ui->camY, NULL);

	/* Left margin: the current tool and hold count. */
	char n[16];
	snprintf(n, sizeof(n), "%d", r->nholds);
	cv_text(cv, &FONT_SMALL, 20 + hud, 28, "HOLDS", C_DIM, 255, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_MED, 20 + hud, 40, n, C_TEXT, 255, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, 20 + hud, 70, "TOOL", C_DIM, 255, 0, ALIGN_CENTER);
	int ht = TOOL_HOLD[ui->tool];
	if (ht >= 0)
		draw_hold_shape(cv, 20 + hud, 94, ht, 0, route_color(r), 0, ht == HOLD_VOLUME ? 0.55f : 0.8f);
	else
		cv_text(cv, &FONT_MED, 20 + hud, 88, ui->tool == TOOL_START ? "S" : ui->tool == TOOL_TOP ? "T" : "X",
		        C_TEXT, 255, 0, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, 20 + hud, 114, TOOL_SHORT[ui->tool], C_TEXT, 255, 0, ALIGN_CENTER);


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

static void tips(Canvas *cv, const char *title, const char *const *lines, int n, int bright)
{
	text_shadow(cv, &FONT_BIG, TOP_W / 2, 44, title, C_ACCENT, 255, 1, ALIGN_CENTER);
	for (int i = 0; i < n; i++)
		cv_text(cv, &FONT_SMALL, TOP_W / 2, 84 + i * 16, lines[i], i < bright ? C_TEXT : C_DIM, 255, 0, ALIGN_CENTER);
}

static void top_list(Canvas *cv, const UiState *ui)
{
	const RouteBook *b = ui->book;
	char line[48], t[16];
	if (ui->listSel == LIST_GENERATE) {
		static const char *const L[] = {
			"PICK A GRADE AND A WALL, AND A ROUTE IS SET FOR YOU.",
			"EVERY ONE IS TEST-CLIMBED BY THE BETA FINDER,",
			"SO IT CAN BE DONE - AND IT KNOWS HOW.",
			"",
			"OPEN IT IN THE EDITOR TO TWEAK IT, WATCH THE BETA",
			"OR TRY IT YOURSELF, THEN SAVE IT TO YOUR BOOK.",
		};
		tips(cv, "GENERATE A ROUTE", L, 6, 3);
	} else if (ui->listSel < LIST_FIRST || ui->listSel - LIST_FIRST >= b->count) {
		static const char *const L[] = {
			"PICK A HOLD TYPE ON THE TOOLBAR, THEN TAP THE WALL.",
			"MARK ONE OR TWO START HOLDS (S) NEAR THE MAT",
			"AND ONE TOP HOLD (TOP). MATCH BOTH HANDS ON IT TO SEND.",
			"SAVING WORKS OUT THE GRADE AND THE BEST WAY UP.",
			"",
			"JUGS AND EDGES ARE KIND. CRIMPS, POCKETS AND SLOPERS",
			"PUMP YOU OUT. UNDERCLINGS ARE GOOD ONCE YOU'RE ABOVE",
			"THEM. FOOT CHIPS AND VOLUMES ARE FOR YOUR FEET.",
		};
		tips(cv, "SET A NEW ROUTE", L, 8, 4);
	} else {
		const Route *r = &b->routes[ui->listSel - LIST_FIRST];
		draw_minimap(cv, 20, 0, r);
		int x = 150;
		uint32_t col = route_color(r);
		text_shadow(cv, &FONT_BIG, x, 18, r->name, C_TEXT, 255, 1, ALIGN_LEFT);
		cv_fill(cv, x, 46, 230, 2, col);
		char g[8];
		route_grade_label(g, sizeof(g), r);
		text_shadow(cv, &FONT_HUGE, x, 62, g, col == 0x2B2B33 ? C_TEXT : col, 255, 2, ALIGN_LEFT);
		int gx = x + text_width(&FONT_HUGE, g, 2) + 10;
		if (r->grade == GRADE_NONE) {
			cv_text(cv, &FONT_SMALL, gx, 66, "NO WAY UP", C_WARN, 255, 1, ALIGN_LEFT);
			cv_text(cv, &FONT_SMALL, gx, 82, "FOUND", C_WARN, 255, 1, ALIGN_LEFT);
		} else if (r->grade == GRADE_UNKNOWN) {
			cv_text(cv, &FONT_SMALL, gx, 66, "NOT GRADED", C_DIM, 255, 1, ALIGN_LEFT);
			cv_text(cv, &FONT_SMALL, gx, 82, "YET", C_DIM, 255, 1, ALIGN_LEFT);
		} else {
			snprintf(line, sizeof(line), "BETA: %d MOVES", r->nbeta);
			cv_text(cv, &FONT_SMALL, gx, 66, line, C_TEXT, 255, 1, ALIGN_LEFT);
			cv_text(cv, &FONT_SMALL, gx, 82, "SELECT: WATCH IT", C_DIM, 255, 1, ALIGN_LEFT);
		}

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
		return;
	}
	snprintf(line, sizeof(line), "%d / %d ROUTES SAVED", b->count, MAX_ROUTES);
	cv_text(cv, &FONT_SMALL, TOP_W / 2, 214, line, C_DIM, 255, 1, ALIGN_CENTER);
}

/* Solver or generator at work: its climber on the wall, with progress. */
static void top_working(Canvas *cv, const UiState *ui, float depth)
{
	if (ui->route && ui->climber) {
		float worldTop = ui->camY - TOP_H;
		draw_wall(cv, TOP_WALL_X, 0, TOP_H, worldTop);
		draw_holds(cv, ui->route, TOP_WALL_X, -worldTop, TOP_H, -1);
		draw_climber(cv, ui->climber, TOP_WALL_X, -worldTop, false);
	}
	int hud = (int)lroundf(depth * 2.5f);
	top_header(cv, ui->workTitle ? ui->workTitle : "", ui->workSub ? ui->workSub : "", C_ACCENT, hud);
	int w = 260, x = (TOP_W - w) / 2 + hud, y = TOP_H - 22;
	panel(cv, x - 3, y - 3, w + 6, 12, 0x000000, 170);
	cv_fill(cv, x, y, (int)(w * ui->workProgress), 6, C_ACCENT);
}

static void top_title(Canvas *cv, const UiState *ui, float depth)
{
	/* The first route with the climber waiting at the bottom, under the title. */
	if (ui->route && ui->climber) {
		float worldTop = WALL_H - TOP_H;
		draw_wall(cv, TOP_WALL_X, 0, TOP_H, worldTop);
		draw_holds(cv, ui->route, TOP_WALL_X, -worldTop, TOP_H, -1);
		draw_climber(cv, ui->climber, TOP_WALL_X, -worldTop, false);
		cv_blend(cv, 0, 0, TOP_W, TOP_H, 0x000000, 60);
	}
	int dx = (int)lroundf(depth * 6.0f);
	cv_blend(cv, 0, 10, TOP_W, 84, 0x000000, 150);
	text_shadow(cv, &FONT_HUGE, TOP_W / 2 + dx, 24, "BOULDER", C_TEXT, 255, 6, ALIGN_CENTER);
	cv_text(cv, &FONT_MED, TOP_W / 2 + dx, 70, "S E T   -   C L I M B   -   S E N D", C_ACCENT, 255, 0, ALIGN_CENTER);
	(void)ui;
}

void render_top(Canvas *cv, const UiState *ui, float depth)
{
	/* The title shows the bottom of the wall; other screens the rows above camY. */
	float centre = ui->screen == SCR_TITLE ? WALL_H - TOP_H / 2 : ui->camY - TOP_H / 2;
	stereo_set(depth, centre, ui->route ? ui->route->angle : ANGLE_VERT);
	switch (ui->screen) {
	case SCR_LOADING:
		text_shadow(cv, &FONT_BIG, TOP_W / 2, 100, "BOULDER 3DS", C_TEXT, 255, 2, ALIGN_CENTER);
		cv_text(cv, &FONT_SMALL, TOP_W / 2, 136, "TUNING THE SPEAKERS...", C_DIM, 255, 1, ALIGN_CENTER);
		break;
	case SCR_TITLE: top_title(cv, ui, depth); break;
	case SCR_LIST:  top_list(cv, ui); break;
	case SCR_EDIT:  top_edit(cv, ui, depth); break;
	case SCR_CLIMB: top_climb(cv, ui, depth); break;
	case SCR_WORKING: top_working(cv, ui, depth); break;
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

	bool full = b->count >= MAX_ROUTES;
	int rows = LIST_FIRST + b->count;
	for (int row = 0; row < LIST_ROWS; row++) {
		int i = ui->listScroll + row;
		if (i >= rows)
			break;
		int y = LIST_Y + row * LIST_ROW_H;
		bool sel = i == ui->listSel;
		panel(cv, 8, y, BOT_W - 16, LIST_ROW_H - 4, sel ? C_PANEL_HI : C_PANEL, 255);
		if (sel)
			cv_fill(cv, 8, y, 3, LIST_ROW_H - 4, C_ACCENT);
		if (i == LIST_NEW || i == LIST_GENERATE) {
			const char *label = full ? "ROUTE BOOK FULL" : i == LIST_NEW ? "+  NEW ROUTE" : "*  GENERATE A ROUTE";
			if (full && i == LIST_GENERATE)
				label = "*  GENERATE (BOOK FULL)";
			cv_text(cv, &FONT_MED, 22, y + 6, label, full && i == LIST_NEW ? C_DIM : C_ACCENT, 255, 1, ALIGN_LEFT);
			continue;
		}
		const Route *r = &b->routes[i - LIST_FIRST];
		cv_disc(cv, 24, y + 12, 5, route_color(r), 255);
		cv_ring(cv, 24, y + 12, 5.5f, 1, 0x000000, 120);
		cv_text(cv, &FONT_MED, 36, y + 6, r->name, C_TEXT, 255, 1, ALIGN_LEFT);
		char g[8];
		route_grade_label(g, sizeof(g), r);
		cv_text(cv, &FONT_MED, BOT_W - 52, y + 6, g, C_TEXT, 255, 1, ALIGN_RIGHT);
		if (r->sends)
			cv_text(cv, &FONT_SMALL, BOT_W - 18, y + 8, "SENT", C_GOLD, 255, 0, ALIGN_RIGHT);
	}
	/* Scroll hints. */
	if (ui->listScroll > 0)
		cv_text(cv, &FONT_SMALL, BOT_W / 2, LIST_Y - 9, "^", C_DIM, 255, 0, ALIGN_CENTER);
	if (ui->listScroll + LIST_ROWS < rows)
		cv_text(cv, &FONT_SMALL, BOT_W / 2, LIST_Y + LIST_ROWS * LIST_ROW_H - 3, "v", C_DIM, 255, 0, ALIGN_CENTER);

	bool route = ui->listSel >= LIST_FIRST;
	bool solved = route && b->routes[ui->listSel - LIST_FIRST].nbeta > 0;
	static const char *LABELS[LBTN_COUNT] = { "BACK", "DELETE", "EDIT", "BETA", "CLIMB" };
	static const char *KEYS[LBTN_COUNT] = { "B", "Y", "X", "SELECT", "A" };
	for (int k = 0; k < LBTN_COUNT; k++) {
		int x = 2 + k * LIST_BTN_STEP;
		const char *label = LABELS[k];
		const char *key = KEYS[k];
		bool enabled = true, on = false;
		switch (k) {
		case LBTN_DELETE:
			enabled = route;
			if (ui->confirmDelete) {
				label = "SURE?";
				on = true;
			}
			break;
		case LBTN_EDIT:
			if (ui->listSel == LIST_NEW) label = "CREATE";
			if (ui->listSel == LIST_GENERATE) label = "SET IT";
			if (!route) {
				enabled = !full || ui->listSel == LIST_GENERATE;
				key = "A";
			}
			break;
		case LBTN_BETA:
			enabled = route && (solved || b->routes[ui->listSel - LIST_FIRST].grade == GRADE_UNKNOWN);
			break;
		case LBTN_CLIMB:
			enabled = route;
			break;
		}
		button(cv, x, LIST_BTN_Y, LIST_BTN_W, LIST_BTN_H, label, on, enabled);
		cv_text(cv, &FONT_SMALL, x + LIST_BTN_W / 2, LIST_BTN_Y + 36, key, C_DIM, 255, 0, ALIGN_CENTER);
	}
}

static void tool_icon(Canvas *cv, int t, int cx, int cy, uint32_t col)
{
	int ht = TOOL_HOLD[t];
	if (ht >= 0) {
		draw_hold_shape(cv, cx, cy, ht, 0, col, 0, ht == HOLD_VOLUME ? 0.55f : ht == HOLD_SLOPER ? 0.8f : 0.9f);
		return;
	}
	if (t == TOOL_START || t == TOOL_TOP) {
		const char *l = t == TOOL_START ? "S" : "TOP";
		int w = text_width(&FONT_SMALL, l, 0) + 6;
		panel(cv, cx - w / 2, cy - 7, w, 14, 0xFFFFFF, 240);
		cv_text(cv, &FONT_SMALL, cx, cy - 3, l, 0x15161A, 255, 0, ALIGN_CENTER);
		return;
	}
	cv_capsule(cv, cx - 5, cy - 5, cx + 5, cy + 5, 1.8f, C_WARN, 255);
	cv_capsule(cv, cx - 5, cy + 5, cx + 5, cy - 5, 1.8f, C_WARN, 255);
}

static void bottom_edit(Canvas *cv, const UiState *ui)
{
	const Route *r = ui->route;
	draw_wall(cv, 0, 0, EDIT_VIEW_H, ui->camY);
	draw_holds(cv, r, 0, -ui->camY, EDIT_VIEW_H, ui->activeHold);
	/* Height guide for start holds. */
	float startLine = FLOOR_Y - 128 - ui->camY;
	if (startLine > 0 && startLine < EDIT_VIEW_H)
		for (int x = 0; x < BOT_W; x += 8)
			cv_blend(cv, x, (int)startLine, 4, 1, 0xFFFFFF, 90);

	/* Toolbar. */
	cv_fill(cv, 0, EDIT_VIEW_H, BOT_W, BOT_H - EDIT_VIEW_H, C_PANEL);
	cv_fill(cv, 0, EDIT_VIEW_H, BOT_W, 1, 0x000000);
	for (int t = 0; t < TOOL_COUNT; t++) {
		int x = TOOL_X0 + t * TOOL_W;
		if (t == ui->tool) {
			panel(cv, x + 1, EDIT_VIEW_H + 3, TOOL_W - 2, BOT_H - EDIT_VIEW_H - 5, C_PANEL_HI, 255);
			cv_fill(cv, x + 4, BOT_H - 4, TOOL_W - 8, 2, C_ACCENT);
		}
		tool_icon(cv, t, x + TOOL_W / 2, EDIT_VIEW_H + 19, route_color(r));
	}
}

static void bottom_climb(Canvas *cv, const UiState *ui)
{
	const Climber *c = ui->climber;
	draw_wall(cv, 0, 0, BOT_H, ui->camY);
	draw_holds(cv, ui->route, 0, -ui->camY, BOT_H, -1);
	if (ui->showBeta || ui->watching)
		draw_beta(cv, ui->route, 0, -ui->camY, BOT_H, ui->watching ? ui->betaStep : 0);
	draw_reach(cv, c, 0, -ui->camY, ui->frame);
	draw_climber(cv, c, 0, -ui->camY, !ui->watching);
	const char *hint = NULL;
	if (ui->watching)
		hint = "WATCHING THE BETA   -   B: STOP";
	else if (c->state == CL_CLIMBING && c->moves == 0 && c->moving < 0)
		hint = ui->route->nbeta ? "DRAG A LIMB OR HOLD L/R/Y/A   -   SELECT: SHOW BETA"
		                        : "DRAG A HAND OR FOOT - OR HOLD L/R/Y/A + CIRCLE PAD";
	if (hint) {
		cv_blend(cv, 0, BOT_H - 22, BOT_W, 22, 0x000000, 150);
		cv_text(cv, &FONT_SMALL, BOT_W / 2, BOT_H - 15, hint, C_TEXT, 255, 0, ALIGN_CENTER);
	}
}

static void bottom_working(Canvas *cv, const UiState *ui)
{
	cv_vgradient(cv, 0, 0, BOT_W, BOT_H, C_BG_TOP, C_BG_BOT);
	text_shadow(cv, &FONT_BIG, BOT_W / 2, 70, ui->workTitle ? ui->workTitle : "", C_TEXT, 255, 1, ALIGN_CENTER);
	if (ui->workSub)
		cv_text(cv, &FONT_SMALL, BOT_W / 2, 104, ui->workSub, C_DIM, 255, 1, ALIGN_CENTER);
	int w = 240, x = (BOT_W - w) / 2, y = 130;
	panel(cv, x - 3, y - 3, w + 6, 16, 0x000000, 170);
	cv_fill(cv, x, y, (int)(w * ui->workProgress), 10, C_ACCENT);
	/* A little bouncing chalk dust while we wait. */
	for (int i = 0; i < 5; i++) {
		float t = ui->frame * 0.12f + i * 1.2f;
		cv_disc(cv, BOT_W / 2 - 40 + i * 20, 170 - fabsf(sinf(t)) * 10, 3, CHALK, 200);
	}
	cv_text(cv, &FONT_SMALL, BOT_W / 2, 214, "B: CANCEL", C_DIM, 255, 1, ALIGN_CENTER);
}

void render_bottom(Canvas *cv, const UiState *ui)
{
	stereo_set(0, 0, ANGLE_VERT); /* the bottom screen is flat */
	switch (ui->screen) {
	case SCR_LOADING:
		cv_vgradient(cv, 0, 0, BOT_W, BOT_H, C_BG_TOP, C_BG_BOT);
		break;
	case SCR_TITLE:   bottom_title(cv, ui); break;
	case SCR_LIST:    bottom_list(cv, ui); break;
	case SCR_EDIT:    bottom_edit(cv, ui); break;
	case SCR_CLIMB:   bottom_climb(cv, ui); break;
	case SCR_WORKING: bottom_working(cv, ui); break;
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
	return i < LIST_FIRST + ui->book->count ? i : -1;
}

int hit_list_button(int px, int py)
{
	if (py < LIST_BTN_Y || py >= LIST_BTN_Y + LIST_BTN_H)
		return -1;
	for (int k = 0; k < LBTN_COUNT; k++) {
		int x = 2 + k * LIST_BTN_STEP;
		if (px >= x && px < x + LIST_BTN_W)
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
	if (py < EDIT_VIEW_H || px < TOOL_X0 || px >= TOOL_X0 + TOOL_COUNT * TOOL_W)
		return -1;
	return (px - TOOL_X0) / TOOL_W;
}

bool hit_title_button(int px, int py)
{
	return px >= TITLE_BTN_X && px < TITLE_BTN_X + TITLE_BTN_W && py >= TITLE_BTN_Y && py < TITLE_BTN_Y + TITLE_BTN_H;
}
