// DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (patrz LICENSE).
// Silnik (src/doom) na komputerze, bez ESP32: sprawdza, ze konkretny plik .kwad przechodzi przez
// W_Init/R_Init/P_SetupLevel i daje sie grac (menu -> Nowa gra -> chodzenie, strzal, mapa).
// Buduje i uruchamia to kos/tests/test_silnik_host.py. Wskazniki 64-bitowe, wiec zuzycie strefy
// NIE odpowiada plytce (tam ~110 kB) - to sprawdzenie formatu danych, nie pamieci.
// Zastepniki funkcji spoza C99, ktorych silnik uzywa (itoa, strupr) - tylko dla tego testu.
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
char* itoa(int v, char* s, int r);
char* strupr(char* s);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "doomdef.h"
#include "doomtype.h"
#include "d_event.h"
void D_PostEvent(event_t* ev);
void Z_Init(void);
void InitGlobals(void);
void D_DoomMain(void);

const unsigned char* doom_iwad = 0;
unsigned int doom_iwad_len = 0;
int g_kosSaveFailed = 0;

static unsigned char sram[4096];
void SramRead(unsigned char* d, unsigned int s, unsigned int o) { memcpy(d, sram + o, s); }
void SramWrite(const unsigned char* s, unsigned int n, unsigned int o) { memcpy(sram + o, s, n); }

static unsigned short fb[120 * 160];
static unsigned char pal[768];
static int frame = 0, tics = 0;
static const char* outdir;
static int maxFrames = 1500;

void I_InitScreen_e32(void) {}
void I_BlitScreenBmp_e32(void) {}
void I_StartWServEvents_e32(void) {}
void I_PollWServEvents_e32(void) {}
void I_ClearWindow_e32(void) {}
unsigned short* I_GetBackBuffer(void) { return fb; }
unsigned short* I_GetFrontBuffer(void) { return fb; }
void I_CreateWindow_e32(void) {}
void I_CreateBackBuffer_e32(void) {}
int I_GetVideoWidth_e32(void) { return 120; }
int I_GetVideoHeight_e32(void) { return 160; }
void I_SetPallete_e32(const byte* p) { memcpy(pal, p, 768); }

static void dump(const char* name, const unsigned char* src)
{
    char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    FILE* f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6 240 160 255\n");
    for (int i = 0; i < 240 * 160; i++) fwrite(pal + 3 * src[i], 1, 3, f);
    fclose(f);
}

void I_FinishUpdate_e32(const byte* src, const byte* p, const unsigned int w, const unsigned int h)
{
    (void)p; (void)w; (void)h;
    frame++;
    if (src && (frame % 50 == 0)) { char n[32]; snprintf(n, sizeof n, "f%05d", frame); dump(n, src); }
    if (frame >= maxFrames) { printf("HOST: %d klatek, %d tikow - koniec OK\n", frame, tics); exit(0); }
}

static void key(int type, int k) { event_t ev = {0}; ev.type = type; ev.data1 = k; D_PostEvent(&ev); }

// Skrypt: menu -> Nowa gra -> epizod -> trudnosc, potem chodzenie i strzelanie.
void I_ProcessKeyEvents(void)
{
    static int last = -1;
    int t = frame;
    if (t == last) return;
    last = t;
    struct { int at, type, k; } s[] = {
        {100, ev_keydown, KEYD_START}, {103, ev_keyup, KEYD_START},
        {110, ev_keydown, KEYD_A}, {113, ev_keyup, KEYD_A},
        {120, ev_keydown, KEYD_A}, {123, ev_keyup, KEYD_A},
        {130, ev_keydown, KEYD_A}, {133, ev_keyup, KEYD_A},
        {140, ev_keydown, KEYD_A}, {143, ev_keyup, KEYD_A},
        {300, ev_keydown, KEYD_UP}, {600, ev_keyup, KEYD_UP},
        {610, ev_keydown, KEYD_LEFT}, {660, ev_keyup, KEYD_LEFT},
        {670, ev_keydown, KEYD_B}, {700, ev_keyup, KEYD_B},
        {710, ev_keydown, KEYD_UP}, {1000, ev_keyup, KEYD_UP},
        {1010, ev_keydown, KEYD_SELECT}, {1013, ev_keyup, KEYD_SELECT},
    };
    for (unsigned i = 0; i < sizeof s / sizeof s[0]; i++) if (s[i].at == t) key(s[i].type, s[i].k);
}

int I_GetTime_e32(void) { return ++tics / 3; }

void I_Error(const char* e, ...)
{
    va_list v; va_start(v, e); printf("I_Error: "); vprintf(e, v); printf("\n"); va_end(v);
    exit(3);
}
void I_Quit_e32(void) { exit(0); }

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "host plik.kwad katalog [klatki]\n"); return 2; }
    outdir = argv[2];
    if (argc > 3) maxFrames = atoi(argv[3]);
    FILE* f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* b = malloc(n); fread(b, 1, n, f); fclose(f);
    doom_iwad = b; doom_iwad_len = (unsigned)n;
    Z_Init();
    InitGlobals();
    D_DoomMain();
    return 0;
}

char* itoa(int v, char* s, int r) { (void)r; sprintf(s, "%d", v); return s; }
char* strupr(char* s) { for (char* p = s; *p; p++) *p = (char)toupper((unsigned char)*p); return s; }
