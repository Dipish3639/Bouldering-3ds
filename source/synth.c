#include "synth.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define PI 3.14159265f
#define TABLE_BITS 11
#define TABLE_SIZE (1 << TABLE_BITS)

static float sine_table[TABLE_SIZE];
static int table_ready;

static void init_tables(void)
{
	if (table_ready)
		return;
	for (int i = 0; i < TABLE_SIZE; i++)
		sine_table[i] = sinf(2 * PI * i / TABLE_SIZE);
	table_ready = 1;
}

/* Oscillators take a phase in cycles (0..1). */
static inline float osc_sine(float ph)
{
	return sine_table[(int)(ph * TABLE_SIZE) & (TABLE_SIZE - 1)];
}

static inline float osc_tri(float ph)
{
	ph -= (int)ph;
	return ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
}

static inline float osc_pulse(float ph, float duty)
{
	ph -= (int)ph;
	return ph < duty ? 1.0f : -1.0f;
}

static uint32_t noise_state = 0x1234567u;
static inline float noise(void)
{
	noise_state = noise_state * 1664525u + 1013904223u;
	return (float)(int32_t)noise_state / 2147483648.0f;
}

static float midi_hz(int note)
{
	return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

/* Exponential decays are done by repeated multiplication: expf per sample
 * is far too slow on the 3DS CPU. */
static float decay_step(float rate)
{
	return expf(-rate / SYNTH_RATE);
}

static int16_t to_s16(float v)
{
	/* Soft clip keeps loud chords from wrapping. */
	if (v > 1.0f) v = 1.0f;
	if (v < -1.0f) v = -1.0f;
	v = v * (1.5f - 0.5f * v * v);
	return (int16_t)(v * 32000.0f);
}

/* ------------------------------------------------------------------ */
/* Music. */

static int eighth_samples(int bpm)
{
	return SYNTH_RATE * 60 / bpm / 2;
}

/* Soft brushed kit for the climbing track: kick on 1 and the "and" of 2,
 * a rim click on 2 and 4, and a quiet shaker. */
static void add_beat_bar(float *buf, int start, int eighth, float vol)
{
	for (int e = 0; e < 8; e++) {
		int base = start + e * eighth;
		if (e == 0 || e == 3 || e == 5) {
			float ph = 0, pitch = 1, amp = 1;
			float kp = decay_step(30.0f), ka = decay_step(12.0f);
			for (int s = 0; s < eighth; s++) {
				ph += (45.0f + 70.0f * pitch) / SYNTH_RATE;
				buf[base + s] += osc_sine(ph) * amp * vol;
				pitch *= kp;
				amp *= ka;
			}
		}
		if (e == 2 || e == 6) {
			float amp = 1, k = decay_step(60.0f), lp = 0, ph = 0;
			for (int s = 0; s < eighth; s++, amp *= k) {
				lp += (noise() - lp) * 0.5f;
				ph += 1700.0f / SYNTH_RATE;
				buf[base + s] += (lp * 0.25f + osc_sine(ph) * 0.12f) * amp * vol;
			}
		}
		float prev = 0, amp = 1, k = decay_step(70.0f);
		for (int s = 0; s < eighth / 2; s++, amp *= k) {
			float n = noise();
			buf[base + s] += (n - prev) * amp * vol * (e & 1 ? 0.07f : 0.11f);
			prev = n;
		}
	}
}

/* Both tracks share a chord loop: arpeggiated sevenths with a bell tone. */
#define CALM_BPM  84
#define CLIMB_BPM 82
static const uint8_t CHORDS[4][4] = {
	{ 57, 60, 64, 67 }, /* Am7   */
	{ 53, 57, 60, 64 }, /* Fmaj7 */
	{ 48, 52, 55, 59 }, /* Cmaj7 */
	{ 55, 59, 62, 64 }, /* G6    */
};
static const int8_t ARP[8] = { 0, 1, 2, 3, 2, 1, 3, 2 };
/* A lazy pentatonic tune over the climbing beat (0 = rest), one note per eighth. */
static const uint8_t CLIMB_TUNE[64] = {
	76, 0, 79, 0, 81, 0, 79, 76,   0, 0, 74, 0, 72, 0, 0, 0,
	72, 0, 74, 0, 76, 0, 79, 0,    76, 0, 0, 74, 72, 0, 69, 0,
	76, 0, 79, 0, 81, 0, 84, 81,   79, 0, 0, 76, 74, 0, 0, 0,
	72, 0, 74, 76, 0, 74, 72, 0,   69, 0, 0, 0, 0, 0, 0, 0,
};

static void add_bell(float *buf, int pos, int maxPos, float hz, float vol, int len)
{
	float p1 = 0, p2 = 0, d1 = 1, d2 = 1;
	float k1 = decay_step(2.2f), k2 = decay_step(6.0f);
	for (int s = 0; s < len && pos + s < maxPos; s++) {
		p1 += hz / SYNTH_RATE;
		p2 += hz * 2.0f / SYNTH_RATE;
		float env = (s < 60 ? s / 60.0f : 1.0f) * d1;
		buf[pos + s] += (osc_sine(p1) + 0.3f * osc_sine(p2) * d2) * env * vol;
		d1 *= k1;
		d2 *= k2;
	}
}

/* A rounded electric-piano-ish note for the climbing tune. */
static void add_keys(float *buf, int pos, int maxPos, float hz, float vol, int len)
{
	float ph = 0, amp = 1, k = decay_step(3.5f);
	for (int s = 0; s < len && pos + s < maxPos; s++, amp *= k) {
		ph += hz / SYNTH_RATE;
		float env = (s < 120 ? s / 120.0f : 1.0f) * amp;
		buf[pos + s] += (osc_sine(ph) + 0.25f * osc_tri(ph * 2)) * env * vol;
	}
}

#define TRACK_BARS 16

static int track_bpm(int track)
{
	return track == TRACK_CLIMB ? CLIMB_BPM : CALM_BPM;
}

int synth_track_len(int track)
{
	return eighth_samples(track_bpm(track)) * 8 * TRACK_BARS;
}

/* Shared scratch buffer, big enough for the longest track. */
static float scratch[SYNTH_RATE * 50];

void synth_track(int track, int16_t *out)
{
	init_tables();
	int len = synth_track_len(track);
	if (len > (int)(sizeof(scratch) / sizeof(scratch[0])))
		len = sizeof(scratch) / sizeof(scratch[0]);
	memset(scratch, 0, sizeof(float) * len);
	int eighth = eighth_samples(track_bpm(track));
	int bar = eighth * 8;
	bool climb = track == TRACK_CLIMB;

	for (int b = 0; b < TRACK_BARS; b++) {
		const uint8_t *ch = CHORDS[(b / 2) % 4];
		int start = b * bar;
		for (int e = 0; e < 8; e++) {
			if (climb && (e & 1))
				continue; /* sparser arpeggio under the tune */
			int note = ch[ARP[e]] + 12;
			add_bell(scratch, start + e * eighth, len, midi_hz(note), climb ? 0.10f : 0.16f, eighth * 6);
		}
		/* Soft sustained bass on the chord root. */
		if (b % 2 == 0) {
			float ph = 0;
			float hz = midi_hz(ch[0] - 12);
			for (int s = 0; s < bar * 2 && start + s < len; s++) {
				float env = (s < 4000 ? s / 4000.0f : 1.0f) * (1.0f - 0.5f * s / (bar * 2));
				ph += hz / SYNTH_RATE;
				scratch[start + s] += osc_sine(ph) * env * (climb ? 0.22f : 0.18f);
			}
		}
		if (climb) {
			add_beat_bar(scratch, start, eighth, b < 2 ? 0.0f : 0.32f);
			if (b >= 4) {
				const uint8_t *tune = &CLIMB_TUNE[((b - 4) % 8) * 8];
				for (int e = 0; e < 8; e++)
					if (tune[e])
						add_keys(scratch, start + e * eighth, len, midi_hz(tune[e]), 0.13f, eighth * 4);
			}
		}
	}
	/* Echo that wraps around so the loop point is seamless. */
	int d = eighth * 3;
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < d; i++)
			scratch[i] += scratch[i - d + len] * 0.25f;
		for (int i = d; i < len; i++)
			scratch[i] += scratch[i - d] * 0.25f;
	}

	for (int i = 0; i < len; i++)
		out[i] = to_s16(scratch[i]);
}

/* ------------------------------------------------------------------ */
/* Sound effects. */

static const float SFX_SECONDS[SFX_COUNT] = {
	[SFX_GRAB] = 0.12f, [SFX_FOOT] = 0.08f, [SFX_MISS] = 0.22f, [SFX_SLIP] = 0.30f,
	[SFX_FALL] = 0.7f, [SFX_LAND] = 0.45f, [SFX_TOP] = 1.4f, [SFX_PUMP] = 0.25f,
	[SFX_PLACE] = 0.06f, [SFX_ERASE] = 0.12f, [SFX_START] = 0.4f,
	[SFX_MENU_MOVE] = 0.04f, [SFX_MENU_SELECT] = 0.25f,
};

int synth_sfx_len(int sfx)
{
	return (int)(SFX_SECONDS[sfx] * SYNTH_RATE);
}

/* A plucked tone starting at `at` seconds. */
static void tone(float *buf, int len, float at, float hz, float dur, float vol, float decay, int wave)
{
	int start = (int)(at * SYNTH_RATE);
	int n = (int)(dur * SYNTH_RATE);
	float ph = 0;
	for (int s = 0; s < n && start + s < len; s++) {
		float t = s / (float)SYNTH_RATE;
		float env = (s < 40 ? s / 40.0f : 1.0f) * expf(-t * decay) * (1.0f - s / (float)n);
		ph += hz / SYNTH_RATE;
		float v = wave == 0 ? osc_sine(ph) : wave == 1 ? osc_tri(ph) : osc_pulse(ph, 0.5f) * 0.6f;
		buf[start + s] += v * env * vol;
	}
}

static void sweep(float *buf, int len, float hz0, float hz1, float vol, int wave)
{
	float ph = 0;
	for (int s = 0; s < len; s++) {
		float k = s / (float)len;
		float hz = hz0 + (hz1 - hz0) * k;
		ph += hz / SYNTH_RATE;
		float v = wave == 0 ? osc_sine(ph) : osc_pulse(ph, 0.5f) * 0.5f;
		buf[s] += v * (1.0f - k) * vol;
	}
}

/* Filtered noise; `smooth` near 0 is dull, near 1 is bright. */
static void noise_burst(float *buf, int len, float vol, float decay, float smooth)
{
	float lp = 0;
	for (int s = 0; s < len; s++) {
		lp += (noise() - lp) * smooth;
		buf[s] += lp * expf(-s / (float)SYNTH_RATE * decay) * vol;
	}
}

void synth_sfx(int sfx, int16_t *out)
{
	init_tables();
	int len = synth_sfx_len(sfx);
	float *b = scratch;
	memset(b, 0, sizeof(float) * len);

	switch (sfx) {
	case SFX_GRAB: /* chalky slap on resin */
		noise_burst(b, len, 0.55f, 45, 0.35f);
		sweep(b, len / 2, 260, 120, 0.35f, 0);
		break;
	case SFX_FOOT: /* rubber on a hold */
		noise_burst(b, len, 0.25f, 70, 0.6f);
		sweep(b, len, 420, 220, 0.18f, 0);
		break;
	case SFX_MISS: /* hand slaps air, then a quick swish */
		for (int s = 0; s < len; s++) {
			float k = s / (float)len;
			b[s] += noise() * sinf(PI * k) * 0.22f;
		}
		tone(b, len, 0.05f, 330, 0.15f, 0.2f, 12, 1);
		break;
	case SFX_SLIP: /* scrape down the wall */
		{
			float lp = 0;
			for (int s = 0; s < len; s++) {
				float k = s / (float)len;
				lp += (noise() - lp) * (0.5f - 0.4f * k);
				b[s] += lp * (1.0f - k) * 0.5f;
			}
		}
		break;
	case SFX_FALL:
		sweep(b, len, 700, 160, 0.3f, 0);
		for (int s = 0; s < len; s++)
			b[s] += noise() * 0.08f * s / (float)len;
		break;
	case SFX_LAND: /* thump onto the crash pad */
		{
			float ph = 0, pitch = 1, amp = 1;
			float kp = decay_step(18.0f), ka = decay_step(9.0f);
			for (int s = 0; s < len; s++) {
				ph += (40.0f + 90.0f * pitch) / SYNTH_RATE;
				b[s] += osc_sine(ph) * amp * 0.8f;
				pitch *= kp;
				amp *= ka;
			}
		}
		noise_burst(b, len, 0.3f, 25, 0.15f);
		break;
	case SFX_TOP: {
		static const int N[6] = { 69, 72, 76, 79, 81, 84 };
		for (int i = 0; i < 6; i++)
			tone(b, len, i * 0.09f, midi_hz(N[i]), 0.9f, 0.22f, 3, i & 1 ? 0 : 1);
		tone(b, len, 0.55f, midi_hz(88), 0.85f, 0.18f, 2.5f, 0);
		break;
	}
	case SFX_PUMP: /* heartbeat */
		tone(b, len, 0, 70, 0.1f, 0.6f, 20, 0);
		tone(b, len, 0.12f, 60, 0.12f, 0.45f, 20, 0);
		break;
	case SFX_PLACE:
		tone(b, len, 0, 900, 0.06f, 0.3f, 40, 1);
		noise_burst(b, len, 0.12f, 90, 0.5f);
		break;
	case SFX_ERASE:
		sweep(b, len, 700, 250, 0.3f, 1);
		break;
	case SFX_START:
		tone(b, len, 0, midi_hz(76), 0.3f, 0.3f, 6, 2);
		tone(b, len, 0.1f, midi_hz(81), 0.3f, 0.3f, 6, 2);
		break;
	case SFX_MENU_MOVE:
		tone(b, len, 0, 1000, 0.04f, 0.25f, 40, 2);
		break;
	case SFX_MENU_SELECT:
		tone(b, len, 0, 784, 0.12f, 0.3f, 10, 2);
		tone(b, len, 0.07f, 1175, 0.18f, 0.3f, 10, 2);
		break;
	}

	for (int i = 0; i < len; i++)
		out[i] = to_s16(b[i]);
}
