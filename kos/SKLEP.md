# DOOM - wpis do sklepu K-OS (tekst do przeniesienia do korona-programy)

Ten plik to gotowy tekst i dane wpisu. Do repozytorium sklepu (`korona-programy/`) przenosi go
sesja glowna - stad nic tam nie jest zapisywane.

* **Kategoria:** `zewnetrzne` (port cudzego programu, jak Bruce / Marauder / ESP32-DIV).
* **Status:** BETA - obraz nie byl jeszcze uruchomiony na zadnej plytce.
* **Dane gry:** sklep ich NIE dostarcza (tylko silnik); uzytkownik robi `/doom/doom.kwad` sam
  z `freedoom1.wad` albo z wlasnego `doom1.wad` / `doom.wad` - `kos/INSTRUKCJA.md`.
* **Licencja:** GPL-2.0 (lub pozniejsza). Zrodlo: https://github.com/PixelPetrol/cyd-doom-kos
  (wydanie: tag `kos-0.1.0-beta`, https://github.com/PixelPetrol/cyd-doom-kos/tree/kos-0.1.0-beta).
* **Ikona:** proponuje `art/icons/sports_esports.svg` (jak K-OS GAME VOL1) - do przerobienia
  narzedziem sklepu na `bin/<plytka>/doom.ico`. Bez logo DOOM (znak towarowy id Software).
* **Nazwa:** "DOOM (BETA)". DOOM jest znakiem towarowym id Software - w opisie jest zdanie,
  ze to nieoficjalny port silnika (jak robia Chocolate Doom, PrBoom).

## Wpisy do `katalog-<plytka>.json`

Rozmiary i SHA-256 z `./kos/build.sh all` (26.09.2026, galaz `kos`, stan po przegladzie -
etap 7). Obrazy: `porty/cyd-doom/kos/bin/<plytka>/doom.bin`. Budowanie jest powtarzalne bit w bit
(macOS, arduino-cli 1.5.1, rdzen 3.3.11), wiec te same skroty wyjda z publicznego repo.

```json
{
 "plik": "bin/cyd24/doom.bin",
 "rozmiar": 637168,
 "sha256": "320f5c89cebd0b4f2745ec13f381d26dc1c1fefced59431ba4304ee586d16a90",
 "nazwa": "DOOM (BETA)",
 "wersja": "0.1.0-beta",
 "kategoria": "zewnetrzne",
 "autor": "id Software, doomhack (GBADoom), HenrysCat (cyd-doom) / port K-OS: Piotr Korona",
 "opis": {
  "pl": "silnik DOOM; BETA - nie byla jeszcze uruchomiona na plytce; wymaga danych gry, ktorych sklep nie dostarcza",
  "en": "DOOM engine; BETA - not yet run on a board; needs game data that the store does not provide"
 },
 "ikona": "bin/cyd24/doom.ico",
 "info": { "pl": "info/cyd24/doom.bin.pl.txt", "en": "info/cyd24/doom.bin.en.txt" }
}
```

Dla `cyd28` i `cyd28s` tak samo, z `bin/cyd28/...` / `bin/cyd28s/...` i:

| plytka | rozmiar | sha256 |
|---|---|---|
| cyd24 | 637 168 | `320f5c89cebd0b4f2745ec13f381d26dc1c1fefced59431ba4304ee586d16a90` |
| cyd28 | 637 088 | `a1353f4bc34f2ae762af3b067297ebb3b26148ae19dabc9140ee0ef813217c9e` |
| cyd28s | 637 104 | `eaee66ebb02180473ea4ea2849f9170cc54a1ae0d82522a66b6327c54755a1a1` |

Krotsza wersja `opis`, gdyby ekran sklepu jej potrzebowal:
`"pl": "silnik DOOM; BETA, NIESPRAWDZONE; dane gry (WAD) dajesz sam"`,
`"en": "DOOM engine; BETA, UNTESTED; bring your own game data (WAD)"`.

## `info/<plytka>/doom.bin.pl.txt`

(ASCII, jak pozostale pliki info; zdanie o plytce w nawiasie klamrowym - wybrac jedno)

```
BETA - nie byla jeszcze uruchomiona na plytce; wymaga danych gry, ktorych sklep nie dostarcza. Silnik DOOM (GBADoom/PrBoom) przeniesiony z projektu HenrysCat/cyd-doom na program K-OS: jedna binarka, RST wraca do K-OS, dotyk, jasnosc, kolory i jezyk z ustawien K-OS, zapisy gry na karcie (/doom/zapisy.sav). {cyd24: Wersja 2.4" - dotyk na magistrali ekranu, podswietlenie na GPIO27. | cyd28: Wersja 2.8" ILI9341 - kolory z odwroceniem jak w K-OS. | cyd28s: Wersja 2.8" ST7789 ("2 USB") - najblizsza plytce, na ktorej sprawdzal autor oryginalu.} Dane gry robisz raz na komputerze: pobierz darmowy Freedoom (freedoom.github.io, plik freedoom1.wad) albo wez wlasny doom1.wad / doom.wad, przerob go skryptem wad2kos.py (github.com/PixelPetrol/cyd-doom-kos, plik kos/wad2kos.py, instrukcja kos/INSTRUKCJA.md) i poloz wynik na karcie jako /doom/doom.kwad. Pierwszy start instaluje dane ~10 s. Miesci sie czesc epizodu 1 (zwykle 2-4 plansze). Sterowanie: lewa polowa ekranu - chodzenie, prawa - uzyj (gora) i strzal (dol), lewy gorny rog - menu, prawy gorny - mapa, BOOT - menu. Bez dzwieku. Licencja GPL-2.0, zrodlo: github.com/PixelPetrol/cyd-doom-kos. DOOM jest znakiem towarowym id Software; to nieoficjalny port silnika.
```

## `info/<plytka>/doom.bin.en.txt`

```
BETA - not yet run on a board; needs game data that the store does not provide. The DOOM engine (GBADoom/PrBoom) ported from the HenrysCat/cyd-doom project to a K-OS program: one binary, RST returns to K-OS, touch, brightness, colours and language from K-OS settings, saved games on the card (/doom/zapisy.sav). {cyd24: 2.4" version - touch on the display bus, backlight on GPIO27. | cyd28: 2.8" ILI9341 version - colours inverted as in K-OS. | cyd28s: 2.8" ST7789 ("2 USB") version - closest to the board the original author tested on.} You make the game data once on a computer: download the free Freedoom (freedoom.github.io, file freedoom1.wad) or use your own doom1.wad / doom.wad, convert it with the wad2kos.py script (github.com/PixelPetrol/cyd-doom-kos, file kos/wad2kos.py, instructions kos/INSTRUKCJA.md) and put the result on the card as /doom/doom.kwad. The first start installs the data in ~10 s. Part of episode 1 fits (usually 2-4 levels). Controls: left half of the screen - move, right half - use (top) and fire (bottom), top-left corner - menu, top-right - map, BOOT - menu. No sound. GPL-2.0 licence, source: github.com/PixelPetrol/cyd-doom-kos. DOOM is a trademark of id Software; this is an unofficial engine port.
```

## Uwagi dla sesji glownej

* Link do zrodla w sklepie ma wskazywac tag wydania (`kos-0.1.0-beta`) - GPL wymaga zrodla tej
  wersji, z ktorej zbudowano obraz. Kolejne wydanie = nowa wersja w `KOS_DOOM_WERSJA`
  (`src/port/kos.h`) i nowy tag.
* Plik `.kwad` z Freedoomu sklep MOGLBY rozdawac osobno (BSD-3-Clause, z tekstem licencji
  i lista autorow Freedoomu) - to osobna decyzja, poza tym wpisem. Z `doom1.wad` - nigdy.
* "zwykle 2-4 plansze" to szacunek bez prawdziwego pliku - dokladnie pokaze
  `python3 kos/wad2kos.py --budzet freedoom1.wad` (albo `doom1.wad`).
