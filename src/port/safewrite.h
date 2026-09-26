// DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (jak silnik, patrz LICENSE).
// K-OS Office - JEDEN wzorzec bezpiecznego zapisu pliku na karte SD.
//
// PO CO TO JEST. Na FAT `fopen(path,"w")` UCINA plik natychmiast, jeszcze zanim
// cokolwiek zostanie zapisane. Gdy karta jest pelna albo wyjeta, uzytkownik traci
// stara tresc i nie dostaje nowej. Do tego FatFs zglasza blad ostatniego sektora
// dopiero z `fclose`, a `rename` NIE nadpisuje istniejacego celu - wiec kazdy zapis
// "na piechote" ma trzy rozne sposoby na ciche skasowanie danych.
//
// Wzorzec (ten sam co `zapiszAtomowo` w K-OS, `progSetLabel` w sdprog.cpp i `copyFile`
// w mod_files.cpp - trzy kopie, ktore juz sie rozjechaly, stad ta jedna):
//   1. cala tresc idzie do "<sciezka>.part",
//   2. KAZDY zapis jest sprawdzany, `fclose` tez,
//   3. stary plik wedruje na "<sciezka>.bak",
//   4. ".part" dostaje wlasciwa nazwe,
//   5. dopiero teraz ".bak" znika.
// Gdy (4) sie nie uda, stary plik WRACA z ".bak", a nowa tresc ZOSTAJE w ".part".
// Zasada nadrzedna: **nigdy nie ma chwili, w ktorej nie ma ani starej, ani nowej
// tresci**, i nigdy nie kasujemy jedynej ocalalej kopii.
//
// Ten plik jest CELOWO CZYSTY - zadnego Arduino, TFT_eSPI ani VFS (tak samo jak
// fmpath.cpp). Dzieki temu ten sam kod, ktory leci na plytke, kompiluje sie na Macu
// i jest sprawdzany testami (office/tests/test_safewrite.cpp), razem z symulacja
// pelnej karty i nieudanego `rename`. Komunikaty na Serial i diode zostawiamy
// warstwie wyzej (sdWrite* w sdcard.cpp) - tutaj tylko kod bledu.
#pragma once
#include <stddef.h>
#include <stdio.h>

// Bufor sciezki: FM_PATH (384) z menedzera plikow plus zapas na ".part"/".bak".
#define SW_PATH 400
// Najdluzszy pojedynczy wpis swPrintf. Wiersz notatki to 63 znaki, wiersz kursow ~16.
#define SW_LINE 320

enum SwRes {
  SW_OK = 0,
  SW_ERR_PATH,        // sciezka + ".part" nie miesci sie w buforze
  SW_ERR_BUSY,        // "<plik>.part" albo "<plik>.bak" JUZ ISTNIEJE - nie ruszamy go
  SW_ERR_OPEN,        // nie da sie zalozyc pliku roboczego (brak katalogu, karta tylko do odczytu)
  SW_ERR_WRITE,       // fwrite zapisal mniej, niz mial - zwykle karta pelna
  SW_ERR_FMT,         // wpis nie miesci sie w SW_LINE (blad programisty, nie karty)
  SW_ERR_CLOSE,       // fclose zglosil blad - dane siedzialy w buforze i nie doszly
  SW_ERR_REPLACE,     // podmiana nieudana; STARA tresc jest na miejscu, nowa czeka w ".part"
  SW_ERR_ORPHAN,      // podmiana nieudana I stary plik nie wrocil - lezy jako "<plik>.bak"
  SW_ERR_BODY         // funkcja piszaca przerwala sama (anulowane, blad odczytu zrodla)
};

// DLACZEGO SW_ERR_BUSY, A NIE "skasuj i pisz dalej": ".part" i ".bak" to legalne nazwy
// plikow na FAT, wiec pod taka nazwa moze lezec plik UZYTKOWNIKA (kopia notatki zrobiona
// na komputerze) albo NASZA tresc z przerwanego zapisu, o ktorej program wlasnie
// powiedzial "nowa tresc czeka w .part". Kasowanie takiego pliku byloby dokladnie tym
// bledem, ktory ten plik ma usuwac - wiec odmawiamy i mowimy, co usunac.
// Slady po WLASNYM przerwanym zapisie sprzata raz, przy montowaniu karty,
// sdRecoverWork() z sdcard.h - dzieki temu SW_ERR_BUSY zostaje dla cudzych plikow.

// Uchwyt zapisu. Pierwszy blad gasi `ok` na stale, wiec funkcja piszaca moze wolac
// swPuts/swPrintf w petli i NIE sprawdzac kazdego z osobna - blad i tak nie zginie.
struct SwOut {
  FILE* f;
  bool  ok;
  SwRes res;
};

bool swWrite(SwOut* o, const void* data, size_t n);
bool swPuts(SwOut* o, const char* s);
bool swPrintf(SwOut* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Funkcja piszaca tresc pliku. Zwraca false, gdy sama chce przerwac (anulowanie,
// blad odczytu zrodla) - wtedy plik roboczy jest kasowany i cel zostaje nietkniety.
typedef bool (*SwBody)(SwOut* out, void* user);

// "<path>.part" - jedna definicja nazwy pliku roboczego dla calego programu.
bool swTmpPath(const char* path, char* out, size_t n);
// "<path>.bak" - plik odstawiony na czas podmiany.
bool swBakPath(const char* path, char* out, size_t n);
// Czy plik istnieje (stat, nie fopen - nie zjada uchwytu z puli max_files karty).
bool swExists(const char* path);

// Zapis calego pliku wzorcem opisanym wyzej.
SwRes safeWriteFile(const char* path, SwBody body, void* user);
// Wygodne opakowanie, gdy tresc jest juz w buforze (n == 0 tworzy pusty plik).
SwRes safeWriteBytes(const char* path, const void* data, size_t n);

// Sama podmiana: gotowy, ZAMKNIETY plik `tmp` wchodzi na miejsce `dst` przez ".bak".
// Uzywa jej takze kopiowanie w menedzerze plikow, ktore pisze plik roboczy samo
// (pasek postepu, przerwanie dotykiem, oddawanie czasu planiscie).
// Przy niepowodzeniu `tmp` ZOSTAJE - to jedyna kopia nowej tresci; kto go nie chce,
// kasuje go sam (tak robi menedzer plikow z niedokonczona kopia 2,4 MB).
// Zwraca SW_OK, SW_ERR_PATH, SW_ERR_BUSY, SW_ERR_REPLACE albo SW_ERR_ORPHAN - to
// ROZNE sytuacje dla uzytkownika i tylko przy ORPHAN stara tresc lezy pod inna nazwa.
SwRes safeReplace(const char* tmp, const char* dst);

// Krotki opis bledu do logu na Serial (po polsku, jak reszta logow).
const char* swErrText(SwRes r);

// ---- podmiana operacji plikowych: TYLKO DLA TESTOW NA HOSCIE ----
// Testy podstawiaja tu atrapy, zeby udac pelna karte, nieudany fclose i nieudany
// rename. Na plytce zawsze siedzi tu SW_OPS_REAL i kod jest ten sam, ktory jest badany.
struct SwOps {
  FILE*  (*xopen)(const char* path, const char* mode);
  size_t (*xwrite)(const void* p, size_t sz, size_t n, FILE* f);
  int    (*xclose)(FILE* f);
  int    (*xrename)(const char* from, const char* to);
  int    (*xremove)(const char* path);
};
extern const SwOps SW_OPS_REAL;
void swSetOps(const SwOps* ops);      // nullptr = wroc do prawdziwych funkcji
