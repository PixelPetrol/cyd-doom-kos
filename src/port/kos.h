// DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (jak silnik, patrz LICENSE).
// Warstwa K-OS portu DOOM: to, czego upstream (cyd-doom) nie ma, bo wgrywal sie od 0x0
// z wlasna tablica partycji. Pod K-OS program jest JEDNA binarka w slocie ota_0, wiec:
//   * dane gry leza w OGONIE WLASNEGO SLOTU, za obrazem aplikacji (kopiowane z karty),
//   * zapisy gry leza na karcie SD (/sd/doom/zapisy.sav), nie w sektorze flasha,
//   * dotyk, jasnosc, odwrocenie kolorow i jezyk biora sie z plikow K-OS na karcie.
// Szczegoly i liczby: README-KOS.md.
#ifndef KOS_H
#define KOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Identyfikator plytki - klucz katalogu na karcie (/sd/korona/<plytka>/dotyk.txt).
#ifndef KOS_BOARD_ID
#if defined(CYD_BOARD_28) && defined(CYD_PANEL_ST7789)
#define KOS_BOARD_ID "cyd28s"
#elif defined(CYD_BOARD_28)
#define KOS_BOARD_ID "cyd28"
#else
#define KOS_BOARD_ID "cyd24"
#endif
#endif

// Wersja programu w sklepie K-OS i tag zrodel w publicznym repozytorium (kos-<wersja>).
#define KOS_DOOM_WERSJA "0.1.1-beta"

// Plik z danymi gry przygotowany na komputerze skryptem kos/wad2kos.py.
#define KOS_DATA_PATH  "/sd/doom/doom.kwad"
#define KOS_SAVE_PATH  "/sd/doom/zapisy.sav"
#define KOS_SAVE_SIZE  4096

typedef struct {
    // wyglad z /sd/korona/ustawienia.txt (wartosci domyslne = motyw "zielony" K-OS)
    uint16_t bg, txt, txt2, acc, err;
    uint8_t  bright;        // 5..100 %
    uint8_t  invert;        // ustawienie uzytkownika z K-OS (NIE argument dla INVON - patrz display.cpp)
    uint8_t  en;            // 1 = angielski
    // kalibracja dotyku z /sd/korona/<plytka>/dotyk.txt, format TFT_eSPI setTouch, rotacja 0
    uint16_t cal[5];
    uint8_t  calFromCard;   // 1 = z karty, 0 = domyslna z kos_board.h
    uint8_t  sdOk;          // karta byla przy starcie
    uint8_t  madctl;        // 0 = z profilu plytki; inaczej MADCTL z /sd/doom/ekran.txt (tylko z bitem MV)
} kos_cfg_t;

extern kos_cfg_t g_kos;

// "Model B": skasuj otadata, zeby kazdy RST wracal do menu K-OS. Wolac PIERWSZE w setup().
void KosModelB(void);

// Karta, ustawienia, dotyk, dane gry (w razie potrzeby kopiowanie z karty do ogona slotu
// z paskiem postepu) i wczytanie zapisow. Gdy danych nie ma, pokazuje ekran z instrukcja
// i po dotyku wraca do K-OS - wtedy NIE wraca do wolajacego.
void KosPrepare(void);

// Odmontowuje karte i zwalnia ekran po SramInit - PRZED Z_Init (strefa bierze najwiekszy blok).
void KosPrepareDone(void);

// Polozenie danych gry wewnatrz biegnacej partycji (ustawione przez KosPrepare).
void KosDataWhere(uint32_t* offset, uint32_t* length);

// Zapisy gry na karcie. Karta jest montowana na czas operacji i zaraz odmontowana,
// bo trzymanie FAT-u zamontowanego przez cala gre zabieraloby strefie silnika ~12 kB.
// KosSaveLoad: 1 = wczytane, 0 = karta jest, pliku nie ma, -1 = karta nie odpowiada / plik zly.
// KosSaveStore: 1 = zapisane.
int KosSaveLoad(unsigned char* buf, unsigned int size);
int KosSaveStore(const unsigned char* buf, unsigned int size);
extern int g_kosSaveFailed;   // ostatni zapis nieudany - g_game.c pokazuje to graczowi

// Etap startu do logu: co 3 s na Serial wiersz "[doom] etap: ..." (z postepem, gdy total > 0),
// zeby zadne czekanie przed pierwsza klatka nie bylo cisza. KosStage(NULL) wylacza (pierwsza klatka).
void KosStage(const char* name);
void KosStageProgress(uint32_t done, uint32_t total);

// Powrot do K-OS (Model B: otadata skasowana, wiec restart = menu K-OS).
void KosExitToMenu(void);

#ifdef __cplusplus
}
#endif

#endif
