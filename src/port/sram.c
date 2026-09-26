// Zmienione dla K-OS 2026-09-24 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): zapisy gry na karte SD (/sd/doom/zapisy.sav) zamiast sektora flasha.
// Save games + settings.
// GBADoom writes < 2KB total (8 save slots + settings), so everything lives
// in a small RAM shadow.
// K-OS: cien trafia do pliku /sd/doom/zapisy.sav (kos.cpp, wzorzec safewrite z Office), a nie
// do sektora flasha jak w upstream. Sektor we flashu musialby lezec w slocie ota_0, a slot
// zamazuje kazdy inny uruchomiony program - zapisy ginelyby przy pierwszym uruchomieniu czegos
// innego. Karta przezywa wszystko, co robi K-OS.
#include <string.h>
#include <stdlib.h>
#include "doomport.h"
#include "kos.h"

#define SRAM_SHADOW_SIZE KOS_SAVE_SIZE

static unsigned char s_shadow[SRAM_SHADOW_SIZE];

// 1 = cien odpowiada plikowi na karcie (wczytany albo pliku nie bylo). 0 = przy starcie karta
// nie odpowiadala, wiec cien to zera ZAMIAST prawdziwych zapisow. Zapisanie takiego cienia
// skasowaloby graczowi wszystkie pozostale sloty (GBADoom zapisuje 8 slotow naraz).
static int s_known = 0;

extern int g_kosSaveFailed;

static void storeRange(const unsigned char* src, unsigned int size, unsigned int offset)
{
    if (!s_known) {
        // Do bufora tymczasowego: nieudany odczyt nie moze nadpisac cienia smieciami.
        unsigned char* tmp = (unsigned char*)malloc(SRAM_SHADOW_SIZE);
        if (!tmp) { g_kosSaveFailed = 1; return; }
        int r = KosSaveLoad(tmp, SRAM_SHADOW_SIZE);
        if (r == 1) memcpy(s_shadow, tmp, SRAM_SHADOW_SIZE);
        free(tmp);
        if (r == 1) {
            // Na karcie SA zapisy, ktorych gra nie widziala. Nie nadpisujemy - ten zapis
            // przepada, gracz widzi "save failed", a nastepny juz pojdzie na prawdziwym cieniu.
            s_known = 1;
            g_kosSaveFailed = 1;
            return;
        }
        if (r == 0) s_known = 1;
    }
    memcpy(s_shadow + offset, src, size);
    if (s_known)
        KosSaveStore(s_shadow, SRAM_SHADOW_SIZE);
    else
        g_kosSaveFailed = 1;   // karta dalej nie odpowiada: zapis zyje tylko do wylaczenia
}

disp_cfg_t g_dispCfg;

extern void I_Error(const char* error, ...);

void SramInit(void)
{
    // Brak pliku (pierwsze uruchomienie, brak karty) = dziewicza pamiec, jak w upstream:
    // GBADoom sprawdza wlasne ciasteczka, wiec zera sa bezpieczne.
    int r = KosSaveLoad(s_shadow, SRAM_SHADOW_SIZE);
    if (r != 1)
        memset(s_shadow, 0, SRAM_SHADOW_SIZE);
    s_known = (r >= 0);

    memcpy(&g_dispCfg, s_shadow + DISPCFG_SRAM_OFFSET, sizeof(g_dispCfg));
    if (g_dispCfg.magic != DISPCFG_MAGIC)
        memset(&g_dispCfg, 0, sizeof(g_dispCfg));
}

void CfgSave(void)
{
    storeRange((const unsigned char*)&g_dispCfg, sizeof(g_dispCfg), DISPCFG_SRAM_OFFSET);
}

void SramRead(unsigned char* dst, unsigned int size, unsigned int offset)
{
    if (offset + size > SRAM_SHADOW_SIZE)
        I_Error("SramRead: out of range (%u+%u)", offset, size);

    memcpy(dst, s_shadow + offset, size);
}

void SramWrite(const unsigned char* src, unsigned int size, unsigned int offset)
{
    if (offset + size > SRAM_SHADOW_SIZE)
        I_Error("SramWrite: out of range (%u+%u)", offset, size);

    // Ten sam zapis co w cieniu = nic do roboty. Menu ustawien wola SaveSRAM przy kazdej
    // zmianie; bez tego kazde klikniecie to montowanie karty (~0,3 s zawieszenia gry).
    // Po nieudanym zapisie cien juz ma nowe dane - wtedy porownanie nie moze zablokowac ponowienia.
    if (!g_kosSaveFailed && !memcmp(s_shadow + offset, src, size))
        return;

    storeRange(src, size, offset);
}
