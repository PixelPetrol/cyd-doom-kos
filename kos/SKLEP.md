# DOOM - wpis do sklepu K-OS (tekst do przeniesienia do korona-programy)

Ten plik to gotowy tekst i dane wpisu. Do repozytorium sklepu (`korona-programy/`) przenosi go
sesja glowna - stad nic tam nie jest zapisywane.

* **Kategoria:** `zewnetrzne` (port cudzego programu, jak Bruce / Marauder / ESP32-DIV).
* **Status:** BETA 0.1.1 - poprawka pierwszego uruchomienia 0.1.0 na cyd24 (30.09: odbity obraz,
  brak startu bez sladu w logu). Gra do konca na plytce jeszcze nie przeszla.
* **Dane gry:** sklep instaluje sam silnik; gotowy `/doom/doom.kwad` z Freedoomu 0.13.0 lezy na
  stronie sklepu (`korona-programy/dane/doom/`, od 28.09) - **bez zmian dla 0.1.1**: ten sam plik
  (sha256 `5e580851...88e3`) przechodzi sprawdzenia programu i silnik na komputerze
  (`kos/tests/test_silnik_host.py`). Wlasny plik: `kos/INSTRUKCJA.md`.
* **Licencja:** GPL-2.0 (lub pozniejsza). Zrodlo: https://github.com/PixelPetrol/cyd-doom-kos
  (wydanie: tag `kos-0.1.1-beta`, https://github.com/PixelPetrol/cyd-doom-kos/tree/kos-0.1.1-beta -
  tag powstaje dopiero przy publikacji; do tego czasu w publicznym repo lezy tylko lokalny commit).
* **Ikona:** proponuje `art/icons/sports_esports.svg` (jak K-OS GAME VOL1) - do przerobienia
  narzedziem sklepu na `bin/<plytka>/doom.ico`. Bez logo DOOM (znak towarowy id Software).
* **Nazwa:** "DOOM (BETA)". DOOM jest znakiem towarowym id Software - w opisie jest zdanie,
  ze to nieoficjalny port silnika (jak robia Chocolate Doom, PrBoom).

## Wpisy do `katalog-<plytka>.json` (0.1.1-beta - NIE OPUBLIKOWANE)

Rozmiary i SHA-256 z `./kos/build.sh all` (30.09.2026, galaz `kos`, `SOURCE_DATE_EPOCH`
2026-09-30). Obrazy: `porty/cyd-doom/kos/bin/<plytka>/doom.bin`. Budowanie jest powtarzalne bit
w bit (macOS, arduino-cli 1.5.1, rdzen 3.3.11), wiec te same skroty wyjda z publicznego repo.
Zmieniaja sie tylko `rozmiar`, `sha256`, `wersja` (reszta jak w obecnym wpisie 0.1.0 w sklepie):

```json
{
 "plik": "bin/cyd24/doom.bin",
 "rozmiar": 640896,
 "sha256": "5ef5d9579e6dd0172ea6022b9597e8af1bb25d3f9088e13c6e97d334f70617fe",
 "nazwa": "DOOM (BETA)",
 "wersja": "0.1.1-beta",
 "kategoria": "zewnetrzne",
 "autor": "id Software, doomhack (GBADoom), HenrysCat (cyd-doom) / port K-OS: Piotr Korona",
 "opis": {
  "pl": "silnik DOOM; BETA - poprawka pierwszego startu, gra jeszcze niesprawdzona do konca; darmowe dane Freedoom do pobrania ze strony sklepu",
  "en": "DOOM engine; BETA - first-start fix, game not yet fully tested; free Freedoom game data to download from the store website"
 },
 "ikona": "bin/cyd24/doom.ico",
 "info": { "pl": "info/cyd24/doom.bin.pl.txt", "en": "info/cyd24/doom.bin.en.txt" }
}
```

| plytka | rozmiar | sha256 |
|---|---|---|
| cyd24 | 640 896 | `5ef5d9579e6dd0172ea6022b9597e8af1bb25d3f9088e13c6e97d334f70617fe` |
| cyd28 | 640 816 | `1a25d026991694fde311234c5a2c0b7b6d4367f119c2ae218f429e7aadd9931e` |
| cyd28s | 640 784 | `2e73587b3127d960039ce4e32c5b3b5aa1880fa28cbed83c19b029563834ea6d` |

Dane w ogonie slotu dalej od +0xA0000 (1852 kB) - dane zainstalowane przez 0.1.0 (o ile kopia
doszla do konca) zostaja i nie sa kopiowane drugi raz.

Poprzednio w sklepie (0.1.0-beta, 28.09): cyd24 637 168 `320f5c89...`, cyd28 637 088
`a1353f4b...`, cyd28s 637 104 `eaee66eb...`.

## `info/<plytka>/doom.bin.pl.txt` (0.1.1)

Obecny tekst w sklepie z trzema zmianami: pierwsze zdanie, "~10 s" -> "do pol minuty", tag
`kos-0.1.1-beta`, plus zdanie o `/doom/ekran.txt`. Zdanie o plytce w nawiasie klamrowym - bez zmian:

```
BETA - poprawka pierwszego uruchomienia (0.1.1); gra nie byla jeszcze sprawdzona do konca na plytce. Silnik DOOM (GBADoom/PrBoom) przeniesiony z projektu HenrysCat/cyd-doom na program K-OS: jedna binarka, RST wraca do K-OS, dotyk, jasnosc, kolory i jezyk z ustawien K-OS, zapisy gry na karcie (/doom/zapisy.sav). {cyd24: Wersja 2.4" - dotyk na magistrali ekranu, podswietlenie na GPIO27. | cyd28: Wersja 2.8" ILI9341 - kolory z odwroceniem jak w K-OS. | cyd28s: Wersja 2.8" ST7789 ("2 USB") - najblizsza plytce, na ktorej sprawdzal autor oryginalu.} DANE GRY: sklep instaluje sam silnik, a gotowe darmowe dane z Freedoom 0.13.0 (plansza E1M1, licencja BSD-3-Clause, bez danych id Software) pobierzesz na komputer ze strony sklepu: pixelpetrol.github.io/korona-programy/dane/doom/doom.kwad (opis i licencja obok: README.txt, LICENCJA-FREEDOOM.txt). Na karcie zaloz folder doom i skopiuj plik jako /doom/doom.kwad - bez Pythona. Pierwszy start instaluje dane do pol minuty. Freedoom 0.13 ma duza grafike, wiec w pliku jest jedna plansza, a sciany i postacie maja polowe rozdzielczosci poziomej. Wlasny doom1.wad / doom.wad albo inne plansze Freedoomu przerobisz sam skryptem wad2kos.py (github.com/PixelPetrol/cyd-doom-kos, instrukcja kos/INSTRUKCJA.md); z DOOM-a miesci sie czesc epizodu 1. Sterowanie: lewa polowa ekranu - chodzenie, prawa - uzyj (gora) i strzal (dol), lewy gorny rog - menu, prawy gorny - mapa, BOOT - menu. Obraz odbity? Plik /doom/ekran.txt z linia madctl=0xA8 (lewo-prawo), 0x68 (gora-dol) lub 0xE8 (obrot) - opis w kos/INSTRUKCJA.md. Bez dzwieku. Licencja silnika GPL-2.0, zrodlo tej wersji: github.com/PixelPetrol/cyd-doom-kos (tag kos-0.1.1-beta). DOOM jest znakiem towarowym id Software; to nieoficjalny port silnika.
```

(Na cyd28s wartosci `ekran.txt` sa inne - tam zdanie: "madctl=0xE8 (lewo-prawo), 0x28 (gora-dol)
lub 0xA8 (obrot)".)

## `info/<plytka>/doom.bin.en.txt` (0.1.1)

```
BETA - first-start fix (0.1.1); the game has not been fully tested on a board yet. The DOOM engine (GBADoom/PrBoom) ported from the HenrysCat/cyd-doom project to a K-OS program: one binary, RST returns to K-OS, touch, brightness, colours and language from K-OS settings, saved games on the card (/doom/zapisy.sav). {cyd24: 2.4" version - touch on the display bus, backlight on GPIO27. | cyd28: 2.8" ILI9341 version - colours inverted as in K-OS. | cyd28s: 2.8" ST7789 ("2 USB") version - closest to the board the original author tested on.} GAME DATA: the store installs only the engine; ready-made free data from Freedoom 0.13.0 (level E1M1, BSD-3-Clause licence, no id Software data) can be downloaded to a computer from the store website: pixelpetrol.github.io/korona-programy/dane/doom/doom.kwad (description and licence next to it: README.txt, LICENCJA-FREEDOOM.txt). Create a doom folder on the card and copy the file as /doom/doom.kwad - no Python needed. The first start installs the data in up to half a minute. Freedoom 0.13 has large graphics, so the file holds one level, and walls and characters are at half horizontal resolution. Your own doom1.wad / doom.wad or other Freedoom levels you can convert yourself with the wad2kos.py script (github.com/PixelPetrol/cyd-doom-kos, instructions kos/INSTRUKCJA.md); from DOOM part of episode 1 fits. Controls: left half of the screen - move, right half - use (top) and fire (bottom), top-left corner - menu, top-right - map, BOOT - menu. Picture mirrored? File /doom/ekran.txt with the line madctl=0xA8 (left-right), 0x68 (top-bottom) or 0xE8 (rotated) - see kos/INSTRUKCJA.md. No sound. Engine licence GPL-2.0, source of this version: github.com/PixelPetrol/cyd-doom-kos (tag kos-0.1.1-beta). DOOM is a trademark of id Software; this is an unofficial engine port.
```

(cyd28s: "madctl=0xE8 (left-right), 0x28 (top-bottom) or 0xA8 (rotated)".)

## Uwagi dla sesji glownej

* Link do zrodla w sklepie ma wskazywac tag wydania (`kos-0.1.1-beta`) - GPL wymaga zrodla tej
  wersji, z ktorej zbudowano obraz. Kolejne wydanie = nowa wersja w `KOS_DOOM_WERSJA`
  (`src/port/kos.h`) i nowy tag.
* Plik `.kwad` z Freedoomu: 28.09 Piotr sie zgodzil - gotowy plik (Freedoom 0.13.0, E1M1,
  `--polowa wszystko`) z licencja, README i nowymi opisami wpisu lezy w
  `korona-programy/_przygotowane/doom-freedoom/` (tam `WYDANIE.md`). Z `doom1.wad` - nigdy.
* "zwykle 2-4 plansze" to szacunek bez prawdziwego pliku - dokladnie pokaze
  `python3 kos/wad2kos.py --budzet freedoom1.wad` (albo `doom1.wad`).
* 0.1.1 do sklepu DOPIERO po tescie Piotra na cyd24 i po pushu + tagu `kos-0.1.1-beta`
  w publicznym repo (GPL: obraz w sklepie = zrodlo pod tagiem). Do tego czasu w sklepie zostaje 0.1.0.
