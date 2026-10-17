#include "gfx.h"
#include <math.h>
#include <string.h>

void cv_init_rotated(Canvas *cv, uint8_t *fb, int w, int h)
{
	/* 3DS framebuffers are stored as columns of `h` pixels, bottom to top. */
	cv->base = fb + (h - 1) * 3;
	cv->xs = h * 3;
	cv->ys = -3;
	cv->w = w;
	cv->h = h;
}

void cv_init_linear(Canvas *cv, uint8_t *buf, int w, int h)
{
	cv->base = buf;
	cv->xs = 3;
	cv->ys = w * 3;
	cv->w = w;
	cv->h = h;
}

static int clip(const Canvas *cv, int *x, int *y, int *w, int *h)
{
	if (*x < 0) { *w += *x; *x = 0; }
	if (*y < 0) { *h += *y; *y = 0; }
	if (*x + *w > cv->w) *w = cv->w - *x;
	if (*y + *h > cv->h) *h = cv->h - *y;
	return *w > 0 && *h > 0;
}

void cv_fill(Canvas *cv, int x, int y, int w, int h, uint32_t rgb)
{
	if (!clip(cv, &x, &y, &w, &h))
		return;
	uint8_t r = rgb >> 16, g = rgb >> 8, b = rgb;
	for (int i = 0; i < w; i++) {
		uint8_t *p = cv->base + (x + i) * cv->xs + y * cv->ys;
		for (int j = 0; j < h; j++, p += cv->ys) {
			p[0] = b; p[1] = g; p[2] = r;
		}
	}
}

static inline void blend_px(uint8_t *p, int r, int g, int b, int a)
{
	p[0] = (uint8_t)(p[0] + (((b - p[0]) * a) >> 8));
	p[1] = (uint8_t)(p[1] + (((g - p[1]) * a) >> 8));
	p[2] = (uint8_t)(p[2] + (((r - p[2]) * a) >> 8));
}

void cv_blend(Canvas *cv, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
	if (alpha <= 0 || !clip(cv, &x, &y, &w, &h))
		return;
	if (alpha >= 255) {
		cv_fill(cv, x, y, w, h, rgb);
		return;
	}
	int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
	for (int i = 0; i < w; i++) {
		uint8_t *p = cv->base + (x + i) * cv->xs + y * cv->ys;
		for (int j = 0; j < h; j++, p += cv->ys)
			blend_px(p, r, g, b, alpha);
	}
}

void cv_frame(Canvas *cv, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
	cv_blend(cv, x, y, w, 1, rgb, alpha);
	cv_blend(cv, x, y + h - 1, w, 1, rgb, alpha);
	cv_blend(cv, x, y + 1, 1, h - 2, rgb, alpha);
	cv_blend(cv, x + w - 1, y + 1, 1, h - 2, rgb, alpha);
}

uint32_t rgb_mix(uint32_t a, uint32_t b, int t)
{
	uint32_t out = 0;
	for (int s = 0; s <= 16; s += 8) {
		int ca = (a >> s) & 0xFF, cb = (b >> s) & 0xFF;
		out |= (uint32_t)(ca + ((cb - ca) * t) / 255) << s;
	}
	return out;
}

void cv_vgradient(Canvas *cv, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
	for (int j = 0; j < h; j++)
		cv_fill(cv, x, y + j, w, 1, rgb_mix(top, bottom, h > 1 ? j * 255 / (h - 1) : 0));
}

static const Glyph *glyph(const Font *f, char c)
{
	if (c < 32 || c > 126)
		c = '?';
	return &f->glyphs[c - 32];
}

int text_width(const Font *f, const char *s, int spacing)
{
	int w = 0;
	for (; *s; s++)
		w += glyph(f, *s)->adv + (s[1] ? spacing : 0);
	return w;
}

void cv_text(Canvas *cv, const Font *f, int x, int y, const char *s,
             uint32_t rgb, int alpha, int spacing, int align)
{
	if (align == ALIGN_CENTER)
		x -= text_width(f, s, spacing) / 2;
	else if (align == ALIGN_RIGHT)
		x -= text_width(f, s, spacing);
	int top = y - f->capTop;
	int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;

	for (; *s; s++) {
		const Glyph *gl = glyph(f, *s);
		const uint8_t *src = f->bits + gl->off;
		for (int j = 0; j < gl->h; j++) {
			int py = top + gl->yo + j;
			if (py < 0 || py >= cv->h)
				continue;
			for (int i = 0; i < gl->w; i++) {
				int px = x + gl->xo + i;
				int a = src[j * gl->w + i];
				if (!a || px < 0 || px >= cv->w)
					continue;
				blend_px(cv->base + px * cv->xs + py * cv->ys, r, g, b, (a * alpha) >> 8);
			}
		}
		x += gl->adv + spacing;
	}
}

/* Coverage of a pixel whose centre is `d` from an edge (negative = inside). */
static inline int coverage(float d)
{
	if (d <= -0.5f)
		return 256;
	if (d >= 0.5f)
		return 0;
	return (int)((0.5f - d) * 256.0f);
}

static inline void shade(Canvas *cv, int px, int py, int r, int g, int b, int a)
{
	uint8_t *p = cv->base + px * cv->xs + py * cv->ys;
	if (a >= 255) {
		p[0] = (uint8_t)b; p[1] = (uint8_t)g; p[2] = (uint8_t)r;
	} else {
		blend_px(p, r, g, b, a);
	}
}

void cv_disc(Canvas *cv, float cx, float cy, float r, uint32_t rgb, int alpha)
{
	cv_ring(cv, cx, cy, r, r * 2 + 2, rgb, alpha);
}

void cv_ring(Canvas *cv, float cx, float cy, float r, float thick, uint32_t rgb, int alpha)
{
	if (alpha <= 0 || r <= 0)
		return;
	int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
	int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > cv->w - 1) x1 = cv->w - 1;
	if (y1 > cv->h - 1) y1 = cv->h - 1;
	int cr = (rgb >> 16) & 0xFF, cg = (rgb >> 8) & 0xFF, cb = rgb & 0xFF;
	float inner = r - thick;
	for (int y = y0; y <= y1; y++) {
		float dy = y + 0.5f - cy;
		for (int x = x0; x <= x1; x++) {
			float dx = x + 0.5f - cx;
			float d = sqrtf(dx * dx + dy * dy);
			int c = coverage(d - r);
			if (inner > 0) {
				int ci = coverage(inner - d);
				if (ci < c)
					c = ci;
			}
			if (c > 0)
				shade(cv, x, y, cr, cg, cb, (c * alpha) >> 8);
		}
	}
}

void cv_capsule(Canvas *cv, float x0, float y0, float x1, float y1, float r, uint32_t rgb, int alpha)
{
	if (alpha <= 0)
		return;
	int bx0 = (int)floorf(fminf(x0, x1) - r - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + r + 1);
	int by0 = (int)floorf(fminf(y0, y1) - r - 1), by1 = (int)ceilf(fmaxf(y0, y1) + r + 1);
	if (bx0 < 0) bx0 = 0;
	if (by0 < 0) by0 = 0;
	if (bx1 > cv->w - 1) bx1 = cv->w - 1;
	if (by1 > cv->h - 1) by1 = cv->h - 1;
	int cr = (rgb >> 16) & 0xFF, cg = (rgb >> 8) & 0xFF, cb = rgb & 0xFF;
	float vx = x1 - x0, vy = y1 - y0;
	float len2 = vx * vx + vy * vy;
	float inv = len2 > 1e-6f ? 1.0f / len2 : 0.0f;
	for (int y = by0; y <= by1; y++) {
		float py = y + 0.5f - y0;
		for (int x = bx0; x <= bx1; x++) {
			float px = x + 0.5f - x0;
			float t = (px * vx + py * vy) * inv;
			if (t < 0) t = 0;
			if (t > 1) t = 1;
			float ex = px - vx * t, ey = py - vy * t;
			int c = coverage(sqrtf(ex * ex + ey * ey) - r);
			if (c > 0)
				shade(cv, x, y, cr, cg, cb, (c * alpha) >> 8);
		}
	}
}

void cv_blit(Canvas *dst, int dx, int dy, const Canvas *src, int sx, int sy, int w, int h)
{
	/* Clip against the source, then the destination. */
	if (sx < 0) { dx -= sx; w += sx; sx = 0; }
	if (sy < 0) { dy -= sy; h += sy; sy = 0; }
	if (sx + w > src->w) w = src->w - sx;
	if (sy + h > src->h) h = src->h - sy;
	if (dx < 0) { sx -= dx; w += dx; dx = 0; }
	if (dy < 0) { sy -= dy; h += dy; dy = 0; }
	if (dx + w > dst->w) w = dst->w - dx;
	if (dy + h > dst->h) h = dst->h - dy;
	if (w <= 0 || h <= 0)
		return;
	if (src->ys == dst->ys && (src->ys == 3 || src->ys == -3)) {
		/* Columns are contiguous in both: one memcpy per column. */
		for (int i = 0; i < w; i++) {
			const uint8_t *s = src->base + (sx + i) * src->xs + sy * src->ys;
			uint8_t *d = dst->base + (dx + i) * dst->xs + dy * dst->ys;
			if (src->ys < 0)
				memcpy(d + (h - 1) * dst->ys, s + (h - 1) * src->ys, h * 3);
			else
				memcpy(d, s, h * 3);
		}
		return;
	}
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++) {
			const uint8_t *s = src->base + (sx + i) * src->xs + (sy + j) * src->ys;
			uint8_t *d = dst->base + (dx + i) * dst->xs + (dy + j) * dst->ys;
			d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
		}
}

void cv_tri(Canvas *cv, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb, int alpha)
{
	if (alpha <= 0)
		return;
	/* Wind the triangle one way so "inside" is always positive. */
	if ((x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0) < 0) {
		float tx = x1, ty = y1;
		x1 = x2; y1 = y2;
		x2 = tx; y2 = ty;
	}
	float ex[3] = { x0, x1, x2 }, ey[3] = { y0, y1, y2 };
	float nx[3], ny[3], c[3];
	for (int i = 0; i < 3; i++) {
		int k = (i + 1) % 3;
		float dx = ex[k] - ex[i], dy = ey[k] - ey[i], l = sqrtf(dx * dx + dy * dy);
		if (l < 1e-4f)
			return;
		nx[i] = -dy / l;
		ny[i] = dx / l;
		c[i] = -(nx[i] * ex[i] + ny[i] * ey[i]);
	}
	int bx0 = (int)floorf(fminf(x0, fminf(x1, x2))) - 1, bx1 = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))) + 1;
	int by0 = (int)floorf(fminf(y0, fminf(y1, y2))) - 1, by1 = (int)ceilf(fmaxf(y0, fmaxf(y1, y2))) + 1;
	if (bx0 < 0) bx0 = 0;
	if (by0 < 0) by0 = 0;
	if (bx1 > cv->w - 1) bx1 = cv->w - 1;
	if (by1 > cv->h - 1) by1 = cv->h - 1;
	int cr = (rgb >> 16) & 0xFF, cg = (rgb >> 8) & 0xFF, cb = rgb & 0xFF;
	for (int y = by0; y <= by1; y++)
		for (int x = bx0; x <= bx1; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float d = 1e9f;
			for (int i = 0; i < 3; i++) {
				float e = nx[i] * px + ny[i] * py + c[i];
				if (e < d)
					d = e;
			}
			int cov = coverage(-d);
			if (cov > 0)
				shade(cv, x, y, cr, cg, cb, (cov * alpha) >> 8);
		}
}
