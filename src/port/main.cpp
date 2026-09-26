// Zmienione dla K-OS 2026-09-24 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): Model B na starcie setup(), przygotowanie K-OS (karta, dane w ogonie slotu) przed Z_Init.
// DOOM on the ESP32-2432S028R "Cheap Yellow Display".
// Port of GBADoom (doomhack), which is based on PrBoom.
#include <Arduino.h>
#include "doomport.h"
#include "kos.h"

extern "C" {
#include "doomdef.h"
#include "d_main.h"
#include "z_zone.h"
#include "global_data.h"
void I_PreInitGraphics(void);
void InitGlobals(void);
}

static void doomTask(void*)
{
    // K-OS: karta, ustawienia, dotyk i dane gry w ogonie slotu. Tu moga pojawic sie ekrany
    // (brak danych / instalacja) - wszystko PRZED Z_Init, a KosPrepareDone zwalnia ekran i FAT,
    // zeby strefa dostala ten sam najwiekszy blok co w upstream.
    KosPrepare();          // w srodku takze SramInit (zapisy z /sd/doom/zapisy.sav)
    KosPrepareDone();
    WadInit();             // mapuje ogon slotu, ktory przygotowal KosPrepare

    printf("heap before Z_Init: free=%u largest=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // Z_Init first: the zone wants the largest contiguous heap block, so it
    // must run before the display/framebuffer allocations fragment the heap.
    Z_Init();

    printf("heap after Z_Init: free=%u largest=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    I_PreInitGraphics();   // display + input + framebuffers

    SetupRunIfNeeded();    // first-boot wizard (or hold BOOT at power-on)

    InitGlobals();

    D_DoomMain();

    for (;;)
        vTaskDelay(portMAX_DELAY);
}

// Static stack keeps 24KB out of the heap regions the zone/framebuffers need.
static StaticTask_t s_doomTcb;
static StackType_t s_doomStack[24576 / sizeof(StackType_t)];

void setup()
{
    // K-OS "Model B": PIERWSZE, zanim cokolwiek moze sie wywrocic - inaczej plytka zostaje
    // zapetlona w DOOM i odzyskuje sie ja tylko kablem.
    KosModelB();

    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("DOOM dla K-OS " KOS_DOOM_WERSJA " (" KOS_BOARD_ID ") - port cyd-doom (GBADoom/PrBoom), GPL-2.0");
    Serial.println("keys: w/a/s/d move, q/e strafe, f fire, space use, m menu, t map");

    xTaskCreateStaticPinnedToCore(doomTask, "doom", 24576 / sizeof(StackType_t),
                                  nullptr, 2, s_doomStack, &s_doomTcb, 1);

    vTaskDelete(nullptr); // setup runs in loopTask; we don't need it
}

void loop() {}
