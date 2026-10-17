/* Just enough of libctru to run source/main.c on a PC (tools/smoketest.c). */
#ifndef STUB_3DS_H
#define STUB_3DS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t s16;
typedef int32_t s32;

enum {
	KEY_A = 1 << 0, KEY_B = 1 << 1, KEY_SELECT = 1 << 2, KEY_START = 1 << 3,
	KEY_DRIGHT = 1 << 4, KEY_DLEFT = 1 << 5, KEY_DUP = 1 << 6, KEY_DDOWN = 1 << 7,
	KEY_R = 1 << 8, KEY_L = 1 << 9, KEY_X = 1 << 10, KEY_Y = 1 << 11,
	KEY_TOUCH = 1 << 20,
	KEY_CPAD_RIGHT = 1 << 28, KEY_CPAD_LEFT = 1 << 29, KEY_CPAD_UP = 1 << 30, KEY_CPAD_DOWN = 1u << 31,
};

typedef struct { u16 px, py; } touchPosition;
typedef struct { s16 dx, dy; } circlePosition;
typedef enum { GFX_TOP, GFX_BOTTOM } gfxScreen_t;
typedef enum { GFX_LEFT, GFX_RIGHT } gfx3dSide_t;

#define SYSCLOCK_ARM11 268111856ULL

void gfxInitDefault(void);
void gfxExit(void);
void gfxSetDoubleBuffering(gfxScreen_t s, bool on);
void gfxSet3D(bool on);
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t side, u16 *w, u16 *h);
void gfxFlushBuffers(void);
void gfxSwapBuffers(void);
void gspWaitForVBlank(void);
bool osSetSpeedupEnable(bool on);
float osGet3DSliderState(void);
u64 svcGetSystemTick(void);
bool aptMainLoop(void);
void hidScanInput(void);
u32 hidKeysDown(void);
u32 hidKeysHeld(void);
u32 hidKeysUp(void);
void hidTouchRead(touchPosition *t);
void hidCircleRead(circlePosition *c);

typedef struct { int dummy; } SwkbdState;
typedef enum { SWKBD_TYPE_WESTERN } SwkbdType;
typedef enum { SWKBD_BUTTON_LEFT, SWKBD_BUTTON_MIDDLE, SWKBD_BUTTON_RIGHT, SWKBD_BUTTON_CONFIRM = SWKBD_BUTTON_RIGHT } SwkbdButton;
enum { SWKBD_NOTEMPTY_NOTBLANK = 3 };
void swkbdInit(SwkbdState *s, SwkbdType t, int buttons, int maxLen);
void swkbdSetHintText(SwkbdState *s, const char *t);
void swkbdSetInitialText(SwkbdState *s, const char *t);
void swkbdSetValidation(SwkbdState *s, int valid, u32 flags, u32 digits);
SwkbdButton swkbdInputText(SwkbdState *s, char *buf, size_t size);

#endif
