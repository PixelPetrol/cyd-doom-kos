// Zmienione dla K-OS 2026-09-24 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): I_Error na ekranie i powrot do K-OS.
// ESP32 implementation of GBADoom's i_system_e32 platform API.
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include "esp_timer.h"
#include "esp_system.h"
#include "doomport.h"
#include "kos.h"

extern "C" {
#include "doomdef.h"
#include "doomtype.h"
#include "i_system_e32.h"
}

#define FB_SHORTS (120 * 160)

// Single framebuffer: internal RAM is too fragmented for two 38KB buffers
// alongside a useful zone heap, so the frame is pushed synchronously
// (the display driver still overlaps palette-convert with SPI DMA).
static unsigned short* s_buffer;

extern "C" {

void I_InitScreen_e32(void)
{
    DisplayInit();
    InputInit();

    s_buffer = (unsigned short*)malloc(FB_SHORTS * 2);

    if (!s_buffer)
        I_Error("I_InitScreen_e32: no memory for framebuffer");

    memset(s_buffer, 0, FB_SHORTS * 2);
}

void I_BlitScreenBmp_e32(void) {}
void I_StartWServEvents_e32(void) {}
void I_PollWServEvents_e32(void) {}
void I_ClearWindow_e32(void) {}

unsigned short* I_GetBackBuffer(void)
{
    return s_buffer;
}

unsigned short* I_GetFrontBuffer(void)
{
    return s_buffer;
}

void I_CreateWindow_e32(void) {}

void I_CreateBackBuffer_e32(void)
{
    I_CreateWindow_e32();
}

void I_FinishUpdate_e32(const byte* srcBuffer, const byte* pallete,
                        const unsigned int width, const unsigned int height)
{
    (void)pallete; (void)width; (void)height;

    if (srcBuffer)
        DisplayPushFrame(srcBuffer);

    // Periodic status over serial: frame rate + memory health.
    static uint32_t frames = 0;
    static uint32_t lastReport = 0;
    frames++;
    uint32_t now = millis();
    if (now - lastReport >= 5000) {
        if (lastReport)
            printf("[stat] fps=%.1f heap_free=%u\n",
                   frames * 1000.0f / (now - lastReport),
                   (unsigned)esp_get_free_heap_size());
        frames = 0;
        lastReport = now;
    }
}

void I_SetPallete_e32(const byte* pallete)
{
    DisplaySetPalette(pallete);
}

int I_GetVideoWidth_e32(void)
{
    return 120;
}

int I_GetVideoHeight_e32(void)
{
    return 160;
}

void I_ProcessKeyEvents(void)
{
    InputPoll();
}

int I_GetTime_e32(void)
{
    return (int)((esp_timer_get_time() * 35) / 1000000);
}

void I_Error(const char* error, ...)
{
    char msg[512];

    va_list v;
    va_start(v, error);
    vsnprintf(msg, sizeof(msg), error, v);
    va_end(v);

    printf("\nI_Error: %s\n", msg);
    fflush(stdout);
    DisplayDrawText(msg);

    // K-OS: zamiast wiecznej petli - dotyk albo BOOT wraca do menu K-OS (Model B).
    InputInit();
    int x, y;
    vTaskDelay(pdMS_TO_TICKS(800));      // palec, ktory wywolal blad, nie moze od razu wyjsc
    for (;;) {
        if (InputRawTouch(&x, &y) > 350 || InputBootPressed())
            KosExitToMenu();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void I_Quit_e32(void)
{
    esp_restart();
}

} // extern "C"
