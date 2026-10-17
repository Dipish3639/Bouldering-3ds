#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

/* Everything the game plays is synthesised at start-up into 16-bit mono PCM. */
#define SYNTH_RATE 22050

enum {
	TRACK_CALM,   /* gentle arpeggios, for the menus and the editor */
	TRACK_CLIMB,  /* laid-back beat under the same chords, for climbing */
	TRACK_COUNT
};

enum {
	SFX_GRAB, SFX_FOOT, SFX_MISS, SFX_SLIP, SFX_FALL, SFX_LAND, SFX_TOP,
	SFX_PUMP, SFX_PLACE, SFX_ERASE, SFX_START,
	SFX_MENU_MOVE, SFX_MENU_SELECT,
	SFX_COUNT
};

/* Number of samples a track / effect needs. */
int synth_track_len(int track);
int synth_sfx_len(int sfx);

/* Render into `out`, which must hold the length above. */
void synth_track(int track, int16_t *out);
void synth_sfx(int sfx, int16_t *out);

#endif
