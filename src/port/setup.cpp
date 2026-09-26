// Zmienione dla K-OS 2026-09-24 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): kreator tylko awaryjnie (BOOT przy starcie).
// First-boot setup wizard: panel orientation + colour order + touch
// calibration, persisted with the save games (K-OS: /sd/doom/zapisy.sav).
//
// Runs when no valid config exists, or when the BOOT button is held during
// power-on. Controls:
//   Step 1: a large letter "F" and three bars are shown. Short-press BOOT to
//           cycle the 8 orientation/colour combinations until the F reads
//           correctly (not mirrored, upright) and the bars are RED, GREEN,
//           BLUE left to right. Long-press BOOT (>0.8s) to accept.
//   Step 2: tap the four crosshairs as they appear (hold briefly).
#include <Arduino.h>
#include "doomport.h"

#define COL_RED   0xF800
#define COL_GREEN 0x07E0
#define COL_BLUE  0x001F
#define COL_WHITE 0xFFFF
#define COL_BLACK 0x0000

// 4 landscape orientations (MV set), each with RGB and BGR variants.
static const unsigned char kMadctl[8] = {
    0x60, 0x68,   // MX|MV       RGB / BGR
    0xA0, 0xA8,   // MY|MV       RGB / BGR
    0x20, 0x28,   // MV          RGB / BGR
    0xE0, 0xE8,   // MY|MX|MV    RGB / BGR
};

static void drawTestScreen(void)
{
    DisplayFillRect(0, 0, 320, 240, COL_BLACK);

    // Big "F" (chiral: mirroring is instantly visible)
    DisplayFillRect(40, 30, 16, 100, COL_WHITE);   // vertical bar
    DisplayFillRect(40, 30, 70, 14, COL_WHITE);    // top arm
    DisplayFillRect(40, 74, 50, 12, COL_WHITE);    // middle arm

    // Colour bars: must read RED, GREEN, BLUE left to right
    DisplayFillRect(40, 170, 70, 50, COL_RED);
    DisplayFillRect(125, 170, 70, 50, COL_GREEN);
    DisplayFillRect(210, 170, 70, 50, COL_BLUE);

    // Small marker in the top-right corner (orientation aid)
    DisplayFillRect(300, 10, 10, 10, COL_WHITE);
}

// Wait for a BOOT press; returns 1 for long press (accept), 0 for short.
static int waitBootPress(void)
{
    while (!InputBootPressed())
        delay(20);
    unsigned long t0 = millis();
    while (InputBootPressed())
        delay(20);
    return (millis() - t0) > 800 ? 1 : 0;
}

static void drawCross(int cx, int cy, unsigned short color)
{
    DisplayFillRect(cx - 12, cy - 2, 24, 4, color);
    DisplayFillRect(cx - 2, cy - 12, 4, 24, color);
}

// Wait for a firm touch, sample it, wait for release. Returns raw values.
static void captureTap(int* rx, int* ry)
{
    for (;;) {
        int x, y;
        if (InputRawTouch(&x, &y) > 300) {
            delay(60);                      // let the press settle
            long sx = 0, sy = 0;
            int n = 0;
            for (int i = 0; i < 8; i++) {
                if (InputRawTouch(&x, &y) > 200) { sx += x; sy += y; n++; }
                delay(15);
            }
            if (n >= 5) {
                *rx = (int)(sx / n);
                *ry = (int)(sy / n);
                // wait for release
                int idle = 0;
                while (idle < 8) {
                    idle = InputRawTouch(&x, &y) < 100 ? idle + 1 : 0;
                    delay(15);
                }
                return;
            }
        }
        delay(20);
    }
}

void SetupRunIfNeeded(void)
{
    // K-OS: bez obowiazkowego kreatora. Orientacje i kolory daje profil plytki z build.sh
    // (te same co w K-OS), dotyk - kalibracja z /sd/korona/<plytka>/dotyk.txt. Kreator zostaje
    // jako wyjscie awaryjne: BOOT trzymany przy starcie DOOM. Wynik idzie do zapisow na karcie.
    if (!InputBootPressed())
        return;

    printf("[setup] entering display setup wizard\n");
    printf("[setup] step 1: short-press BOOT until the F is correct and bars\n");
    printf("[setup]         read RED GREEN BLUE, then hold BOOT to accept\n");

    // Step 1: orientation + colour order
    int idx = 0;
    // start from the stored value if there is one
    if (g_dispCfg.magic == DISPCFG_MAGIC) {
        for (int i = 0; i < 8; i++)
            if (kMadctl[i] == g_dispCfg.madctl) { idx = i; break; }
    }

    for (;;) {
        DisplaySetMadctl(kMadctl[idx]);
        drawTestScreen();
        printf("[setup] showing MADCTL=0x%02x\n", kMadctl[idx]);
        if (waitBootPress())
            break;
        idx = (idx + 1) % 8;
    }
    g_dispCfg.madctl = kMadctl[idx];
    printf("[setup] orientation accepted: MADCTL=0x%02x\n", g_dispCfg.madctl);

    // Step 2: touch calibration - tap 4 crosshairs
    printf("[setup] step 2: tap the crosshairs\n");
    static const int px[4] = {20, 300, 300, 20};
    static const int py[4] = {20, 20, 220, 220};
    int rrx[4], rry[4];

    for (int i = 0; i < 4; i++) {
        DisplayFillRect(0, 0, 320, 240, COL_BLACK);
        drawCross(px[i], py[i], COL_WHITE);
        captureTap(&rrx[i], &rry[i]);
        drawCross(px[i], py[i], COL_GREEN);
        printf("[setup] tap %d: raw=(%d,%d)\n", i, rrx[i], rry[i]);
        delay(250);
    }

    // Which raw axis follows screen X? Compare movement across the top edge.
    int dxAsX = abs(rrx[1] - rrx[0]);
    int dyAsX = abs(rry[1] - rry[0]);
    g_dispCfg.touchSwapXY = dyAsX > dxAsX;

    int xA = g_dispCfg.touchSwapXY ? rry[0] : rrx[0]; // at screen x=20 (top-left)
    int xB = g_dispCfg.touchSwapXY ? rry[1] : rrx[1]; // at screen x=300
    int xA2 = g_dispCfg.touchSwapXY ? rry[3] : rrx[3]; // bottom-left
    int xB2 = g_dispCfg.touchSwapXY ? rry[2] : rrx[2]; // bottom-right
    int yA = g_dispCfg.touchSwapXY ? rrx[0] : rry[0]; // at screen y=20
    int yB = g_dispCfg.touchSwapXY ? rrx[3] : rry[3]; // at screen y=220
    int yA2 = g_dispCfg.touchSwapXY ? rrx[1] : rry[1];
    int yB2 = g_dispCfg.touchSwapXY ? rrx[2] : rry[2];

    long x20 = (xA + xA2) / 2, x300 = (xB + xB2) / 2;
    long y20 = (yA + yA2) / 2, y220 = (yB + yB2) / 2;

    // Extrapolate the 20..300 / 20..220 spans out to the full screen.
    g_dispCfg.xRaw0 = (short)(x20 - (x300 - x20) * 20 / 280);
    g_dispCfg.xRaw319 = (short)(x300 + (x300 - x20) * 19 / 280);
    g_dispCfg.yRaw0 = (short)(y20 - (y220 - y20) * 20 / 200);
    g_dispCfg.yRaw239 = (short)(y220 + (y220 - y20) * 19 / 200);

    g_dispCfg.magic = DISPCFG_MAGIC;
    CfgSave();
    printf("[setup] saved: madctl=0x%02x swap=%d x[%d..%d] y[%d..%d]\n",
           g_dispCfg.madctl, g_dispCfg.touchSwapXY,
           g_dispCfg.xRaw0, g_dispCfg.xRaw319,
           g_dispCfg.yRaw0, g_dispCfg.yRaw239);

    DisplayFillRect(0, 0, 320, 240, COL_GREEN);
    delay(400);
    DisplayFillRect(0, 0, 320, 240, COL_BLACK);
}
