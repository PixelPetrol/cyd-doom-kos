# DOOM dla K-OS (BETA)

Silnik DOOM (GBADoom / PrBoom) jako program **K-OS** na plytki ESP32 "Cheap Yellow Display":
**cyd24** (2.4" ILI9341), **cyd28** (2.8" ILI9341) i **cyd28s** (2.8" ST7789, "2 USB").
Port portu: [HenrysCat/cyd-doom](https://github.com/HenrysCat/cyd-doom) (commit `1c58bf4`),
ktory opiera sie na [doomhack/GBADoom](https://github.com/doomhack/GBADoom), a ten na PrBoom.

**Stan: BETA - obraz nie byl jeszcze uruchomiony na plytce.** Wszystko, co tu jest, zostalo
zbudowane i sprawdzone testami na komputerze; pierwsze uruchomienie na sprzecie jest przed nami.

**Dane gry nie sa dolaczone.** Program potrzebuje pliku `/doom/doom.kwad` na karcie SD, ktory
uzytkownik robi sam z `freedoom1.wad` (Freedoom, darmowy) albo z wlasnego `doom1.wad` / `doom.wad`
narzedziem `kos/wad2kos.py` - instrukcja krok po kroku: [kos/INSTRUKCJA.md](kos/INSTRUKCJA.md).

DOOM jest znakiem towarowym id Software. To nieoficjalny port silnika, niezwiazany z id Software.

## Jak to dziala pod K-OS

* Program jest **jedna binarka** w slocie `ota_0` (K-OS "Model B": RST = powrot do menu K-OS).
  Upstream wgrywal sie od `0x0` z wlasna tablica partycji - pod K-OS tak nie wolno.
* Dane gry leza w **ogonie wlasnego slotu**, za obrazem silnika (od +0xA0000, 1852 kB).
  Ladowarka K-OS kasuje przed kopiowaniem programu tylko dlugosc obrazu, wiec ogon przezywa.
  Program kopiuje `/doom/doom.kwad` z karty do ogona przy pierwszym starcie (~10 s) i za kazdym
  razem, gdy ktos ogon zamazal albo plik na karcie sie zmienil.
* Zapisy gry ida na karte (`/doom/zapisy.sav`), dotyk, jasnosc, kolory i jezyk - z ustawien K-OS.

## Budowanie

```
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
./kos/build.sh all          # kos/bin/cyd24|cyd28|cyd28s/doom.bin + rozmiary i SHA-256
python3 kos/tests/test_wad2kos.py
```

Port nie uzywa bibliotek Arduino (ekran, dotyk, karta - przez ESP-IDF z rdzenia). `build.sh`
odmawia obrazu, po ktorym zostaloby mniej niz 1852 kB na dane. Obraz jest powtarzalny bit w bit
(stale nazwy zamiast sciezek, `SOURCE_DATE_EPOCH`) - na macOS z tym samym arduino-cli i rdzeniem
wychodzi ten sam SHA-256 co w sklepie K-OS. Wgrywanie jest celowo poza skryptem: program
instaluje sie przez sklep K-OS.

## Licencja

* Calosc: **GNU GPL 2.0 lub pozniejsza** ([LICENSE](LICENSE)) - tak jak PrBoom i GBADoom.
  Autorzy silnika: id Software (zrodla DOOM, 1997), zespol PrBoom (Chi Hoang, Lee Killough,
  Jess Haas, Colin Phipps, Florian Schulze i inni - naglowki plikow w `src/doom/`), doomhack
  (GBADoom), Kippykip (poprawki GBADoom), HenrysCat (warstwa ESP32 `src/port/`).
* Zmiany dla K-OS i nowe pliki: (c) 2026 Piotr Korona, na tej samej licencji.
* `src/port/kos_font.h` - dane fontu 5x7 z Adafruit_GFX (`glcdfont.c`, licencja BSD, zgodna z GPL).
* Dane gry (WAD) nie sa czescia tego repozytorium ani obrazow. `.kwad` zrobiony z `doom1.wad` /
  `doom.wad` jest przerobionym plikiem id Software - wolno go miec tylko dla siebie. `.kwad`
  z Freedoomu wolno rozdawac na licencji BSD-3-Clause Freedoomu (z jej tekstem i lista autorow).
* Zrodlo odpowiadajace konkretnemu obrazowi w sklepie: tag `kos-<wersja>` w tym repozytorium.

## Co zmieniono wzgledem HenrysCat/cyd-doom 1c58bf4

Kazdy zmieniony plik upstreamu ma w pierwszej linii naglowek "Zmienione dla K-OS" z data i opisem
(GPL-2.0 par. 2a).

| plik | zmiana |
|---|---|
| `src/doom/st_stuff.c` | tlo paska stanu i polozenia widzetow z lumpu `KOSSTBAR` (grafika z WAD-u uzytkownika) zamiast wkompilowanego paska GBA Doom II; cyfry `STTNUM` zamiast `STGANUM`; kazda latka paska sprawdzana przy starcie (w granicach ekranu i lumpu); bez tabeli amunicji (nie miesci sie w 240 px) |
| `src/doom/g_game.c` | nastepna mapa = pierwsza, ktora jest w danych (upstream: na sztywno E1M5 -> E1M8); koniec ostatniej zachowanej mapy = final epizodu; komunikat o nieudanym zapisie na karte |
| `src/port/main.cpp` | Model B na starcie `setup()`, przygotowanie K-OS (karta, dane w ogonie slotu) przed `Z_Init` |
| `src/port/wad_mmap.c` | mapowanie ogona biegnacej partycji zamiast partycji `wad` |
| `src/port/sram.c` | zapisy gry i ustawienia kreatora w `/sd/doom/zapisy.sav` zamiast sektora flasha |
| `src/port/display.cpp` | MADCTL, INVON/INVOFF i pin podswietlenia z profilu plytki K-OS; ekrany K-OS (napisy 5x7), ekran bledu zamiast pustego `DisplayDrawText`, zwolnienie ekranu przed silnikiem |
| `src/port/input.cpp` | dotyk XPT2046 jak w K-OS (2.8": bit-bang, 2.4": `spi_master` na magistrali ekranu), kalibracja K-OS przeliczona na poziom |
| `src/port/setup.cpp` | kreator ekranu tylko awaryjnie (BOOT przy starcie) |
| `src/port/i_system_esp32.cpp` | `I_Error` na ekranie, dotyk/BOOT wraca do K-OS (upstream: wieczna petla) |
| `src/port/doomport.h` | deklaracje nowych funkcji ekranu i dotyku |

Nowe pliki: `src/port/kos.cpp`, `kos.h` (warstwa K-OS: karta, ustawienia, kalibracja, kopia danych
do slotu z CRC, sprawdzenie katalogu lumpow, ekrany "brak danych", zapisy), `kos_font.h`,
`safewrite.cpp/.h` (bezpieczny zapis pliku `.part` -> `.bak` -> podmiana), `kos/build.sh`,
`kos/wad2kos.py` (zamiennik `GbaWadUtil.exe`: przerobka GBA + przycinanie + pasek stanu),
`kos/tests/test_wad2kos.py`, `kos/INSTRUKCJA.md`, `kos/PUBLIKACJA.md`, `kos/sprawdz_publikacje.py`.

Usuniete z upstreamu:

* `src/doom/gfx/stbar.h`, `src/doom/st_gfx.c`, `include/st_gfx.h` - tlo paska stanu z **GBA Doom II
  (Torus Games)**, grafika komercyjna, nie GPL. Zastapione paskiem z WAD-u uzytkownika.
* `tools/GbaWadUtil/` - `GbaWadUtil.exe` i `Qt5Core.dll` (binarki Windows bez zrodel) oraz
  `gbadoom.wad` (palety pochodne od id i cyfry `STGANUM` z GBA Doom II). Zastapione `kos/wad2kos.py`.
* W publicznym repozytorium nie ma tez plikow tylko dla sposobu upstreamu (wgrywanie od `0x0`):
  `platformio.ini`, `partitions.csv`, `tools/*.py`, `CLAUDE.md`, `docs/cyd-doom.jpg` (patrz
  `kos/PUBLIKACJA.md`).

## Znane ograniczenia

* Tylko epizod 1 i tylko tyle plansz, ile zmiesci sie w 1852 kB (zwykle 2-4; `wad2kos.py --budzet`).
* Bez dzwieku (upstream tez go nie ma). Tlo przerywnika i ekrany konca epizodu sa czarne.
* Pasek stanu bez tabeli amunicji BULL/SHEL/RCKT/CELL; biezaca amunicja - duzy licznik AMMO.
* Freedoom: jego `DEHACKED` (teksty, nazwy map w automapie) nie jest stosowany - silnik go nie czyta.
* `wad2kos.py` rozpoznaje IWAD po mapach i lumpach, nie po sumie kontrolnej: inny IWAD zgodny
  z DOOM (np. REKKR) zostanie potraktowany jak DOOM.
* Silnik, tak jak oryginalny DOOM, wierzy danym map i grafiki. `wad2kos.py` sprawdza ich budowe,
  a program na plytce - katalog, wyrownanie, CRC i pasek stanu; plik `.kwad` zrobiony recznie
  z pominieciem `wad2kos.py` moze wywrocic silnik (restart do menu K-OS, bez szkody dla plytki).
* Silnik ma stale limity (visplanes, drawsegs) i ~110 kB strefy - bardzo duze mapy Freedoomu moga
  sie nie zmiescic w pamieci; pierwszy start na plytce to pokaze (komunikat na ekranie).
