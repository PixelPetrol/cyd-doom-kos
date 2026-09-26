// Zmienione dla K-OS 2026-09-24 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): deklaracje DisplayDrawString, DisplayDeinit, InputDeinit.
// ESP32-2432S028R (Cheap Yellow Display) port glue.
#ifndef DOOMPORT_H
#define DOOMPORT_H

#ifdef __cplusplus
extern "C" {
#endif

// --- WAD in memory-mapped flash partition ---
// Maps the "wad" partition and points doom_iwad at it. Dies via I_Error on failure.
void WadInit(void);

// --- Persistent "SRAM" (save games + settings) in the "sram" partition ---
void SramInit(void);
void SramRead(unsigned char* dst, unsigned int size, unsigned int offset);
void SramWrite(const unsigned char* src, unsigned int size, unsigned int offset);

// --- Display/touch configuration (set by the first-boot setup wizard) ---
// Lives at the top of the sram shadow, away from the game's save slots.
typedef struct {
    unsigned int magic;              // DISPCFG_MAGIC when valid
    unsigned char madctl;            // panel orientation / colour order
    unsigned char touchSwapXY;       // screen-x follows raw-y
    short xRaw0, xRaw319;            // raw touch value at screen x=0 / x=319
    short yRaw0, yRaw239;            // raw touch value at screen y=0 / y=239
} disp_cfg_t;

#define DISPCFG_MAGIC 0xCA1D0042u
#define DISPCFG_SRAM_OFFSET 3072

extern disp_cfg_t g_dispCfg;         // loaded by SramInit; magic==0 if unset

void CfgSave(void);

// --- First-boot setup wizard (orientation, colour order, touch cal) ---
void SetupRunIfNeeded(void);

// --- Display (ILI9341 over SPI) ---
// The game renders 240x160 8bpp into double buffers owned by display.cpp
// (exposed through I_GetBackBuffer/I_GetFrontBuffer in the e32 API).
void DisplayInit(void);
// Kick an async palette-convert + scaled push of `frame` (120*160 shorts).
// Blocks only if the previous frame is still being pushed.
void DisplayPushFrame(const unsigned char* frame);
void DisplaySetPalette(const unsigned char* rgb768);
void DisplayDrawText(const char* msg); // minimal panic-text output (I_Error)
void DisplaySetMadctl(unsigned char v);
void DisplayFillRect(int x, int y, int w, int h, unsigned short rgb565);
// K-OS: napis fontem 5x7 (skala 1..4) - ekrany "brak danych" i instalacja danych.
void DisplayDrawString(int x, int y, const char* s, int scale, unsigned short fg, unsigned short bg);
// K-OS: zwolnienie buforow DMA i magistrali przed Z_Init (ekrany K-OS ida PRZED silnikiem).
void DisplayDeinit(void);

// --- Input (XPT2046 touch, serial keys, BOOT button) ---
void InputInit(void);
void InputPoll(void); // posts key events via D_PostEvent
// Raw touch sample for the setup wizard: returns pressure (0 = untouched).
int InputRawTouch(int* rx, int* ry);
int InputBootPressed(void);
// K-OS: na 2.4" dotyk jest urzadzeniem na magistrali ekranu - trzeba je zdjac przed DisplayDeinit.
void InputDeinit(void);

#ifdef __cplusplus
}
#endif

#endif
