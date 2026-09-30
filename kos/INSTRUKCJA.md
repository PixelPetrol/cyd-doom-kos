# DOOM na K-OS - jak przygotowac dane gry

Program DOOM ze sklepu K-OS to sam silnik gry. Plansze, potwory i grafike (tzw. plik WAD)
trzeba dac mu samemu - sklep ich nie dostarcza. Wystarczy to zrobic raz.

**To wersja BETA: gra nie byla jeszcze uruchomiona do konca na plytce** (0.1.1 poprawia pierwsze
uruchomienie na 2.4" - odbity obraz i brak startu).

## Najprosciej: gotowy plik ze sklepu

Przy DOOM w sklepie K-OS jest link do gotowego pliku `doom.kwad` z darmowego Freedoomu
(jedna plansza, E1M1). Pobierz go na komputer, zaloz na karcie SD folder `doom`, skopiuj plik
tak, zeby lezal jako `/doom/doom.kwad`, i przejdz od razu do kroku 5. Bez Pythona.

## 5 krokow (wlasny plik: DOOM albo inne plansze)

1. **Wez plik z gra.** Najprosciej darmowy **Freedoom**: wejdz na https://freedoom.github.io,
   pobierz *Freedoom: Phase 1* i wyjmij z paczki plik `freedoom1.wad`.
   Zamiast tego mozesz uzyc wlasnego DOOM-a: `doom1.wad` (darmowa wersja shareware)
   albo `doom.wad` (pelna wersja, ktora masz kupiona).
2. **Pobierz narzedzie `wad2kos.py`** (link przy DOOM w sklepie K-OS) i zainstaluj Pythona 3
   ze strony https://www.python.org, jesli jeszcze go nie masz.
3. **Przerob plik na komputerze.** Otworz Terminal (Mac, Linux) albo Wiersz polecen (Windows)
   w folderze, w ktorym leza oba pliki, i wpisz:

   ```
   python3 wad2kos.py freedoom1.wad doom.kwad
   ```

   (na Windows czasem `py` zamiast `python3`; przy wlasnym DOOM-ie wpisz `doom1.wad` albo `doom.wad`).
   Narzedzie samo wybierze tyle plansz, ile zmiesci sie na plytce, i wypisze, ktore to sa.
4. **Skopiuj `doom.kwad` na karte SD** plytki, do folderu `doom` - tak, zeby lezal jako
   `/doom/doom.kwad`. Folder `doom` zaloz, jesli go nie ma.
5. **Wloz karte do plytki i uruchom DOOM z K-OS.** Za pierwszym razem program instaluje dane
   (napis "Instaluje dane gry", pasek: kasowanie, kopiowanie, sprawdzanie) - do okolo pol minuty.
   Kolejne starty sa od razu.

## Dobrze wiedziec

* Na plytce miesci sie tylko czesc pierwszego epizodu - dokladnie pokaze
  `python3 wad2kos.py --budzet freedoom1.wad`. Z Freedoomu 0.13 wchodzi jedna plansza, i to ze
  scianami i postaciami w polowie rozdzielczosci poziomej (narzedzie wlaczy to samo). Inne plansze
  wybierzesz tak:
  `python3 wad2kos.py freedoom1.wad doom.kwad --maps E1M1,E1M2,E1M3`.
* Zapisy gry trafiaja na karte, do pliku `/doom/zapisy.sav`.
* Plik `doom.kwad` zrobiony z DOOM-a (`doom1.wad`, `doom.wad`) jest tylko dla Ciebie -
  nie udostepniaj go dalej. Zrobiony z Freedoomu mozna dawac innym.
* Gdy plytka pokaze komunikat o danych (za duze, zly plik, stara wersja) - zrob `doom.kwad`
  jeszcze raz najnowszym `wad2kos.py` i podmien plik na karcie.
* Powrot do K-OS: przycisk RST na plytce. Na ekranach z komunikatem wystarczy dotknac ekranu
  albo nacisnac BOOT; po 3 minutach bez dotyku program sam wraca do K-OS.
* **Obraz odbity albo do gory nogami?** Utworz na karcie plik `/doom/ekran.txt` z jedna linia
  (dotyk odwroci sie razem z obrazem):
  `madctl=0xA8` - odbicie lewo-prawo, `madctl=0x68` - gora-dol, `madctl=0xE8` - obrot o 180
  stopni (2.4" i 2.8" ILI9341; domyslnie `0x28`). Na 2.8" ST7789 domyslnie `0x68`, wtedy:
  `0xE8` lewo-prawo, `0x28` gora-dol, `0xA8` obrot. Usun plik, zeby wrocic do domyslnego.

---

# DOOM on K-OS - preparing the game data

The DOOM program from the K-OS store is only the game engine. The levels, monsters and
graphics (a so-called WAD file) have to come from you - the store does not provide them.
You only need to do this once.

**This is a BETA version: the game has not been played through on a board yet** (0.1.1 fixes the
first start on 2.4" - mirrored picture and no start).

## Easiest: the ready-made file from the store

At DOOM in the K-OS store there is a link to a ready-made `doom.kwad` made from the free Freedoom
(one level, E1M1). Download it to a computer, create a `doom` folder on the SD card, copy the file
so that it is `/doom/doom.kwad`, and go straight to step 5. No Python needed.

## 5 steps (your own file: DOOM or other levels)

1. **Get a game file.** The easiest is the free **Freedoom**: go to https://freedoom.github.io,
   download *Freedoom: Phase 1* and take `freedoom1.wad` out of the archive.
   You can use your own DOOM instead: `doom1.wad` (the free shareware version)
   or `doom.wad` (the full version you own).
2. **Download the `wad2kos.py` tool** (link at DOOM in the K-OS store) and install Python 3
   from https://www.python.org if you do not have it yet.
3. **Convert the file on a computer.** Open Terminal (Mac, Linux) or Command Prompt (Windows)
   in the folder with both files and type:

   ```
   python3 wad2kos.py freedoom1.wad doom.kwad
   ```

   (on Windows sometimes `py` instead of `python3`; with your own DOOM type `doom1.wad` or `doom.wad`).
   The tool picks as many levels as fit on the board and prints which ones.
4. **Copy `doom.kwad` to the board's SD card**, into a folder named `doom`, so that it is
   `/doom/doom.kwad`. Create the `doom` folder if it is not there.
5. **Put the card into the board and start DOOM from K-OS.** The first time, the program installs
   the data ("Installing game data", bar: erasing, copying, verifying) - up to about half a minute.
   Later starts are immediate.

## Good to know

* Only part of the first episode fits on the board - the exact plan is shown by
  `python3 wad2kos.py --budzet freedoom1.wad`. From Freedoom 0.13 one level fits, and only with
  walls and characters at half horizontal resolution (the tool switches it on by itself).
  To choose other levels:
  `python3 wad2kos.py freedoom1.wad doom.kwad --maps E1M1,E1M2,E1M3`.
* Saved games go to the card, into `/doom/zapisy.sav`.
* A `doom.kwad` made from DOOM (`doom1.wad`, `doom.wad`) is for you only - do not share it.
  One made from Freedoom may be shared.
* If the board shows a message about the data (too big, wrong file, old version) - make
  `doom.kwad` again with the newest `wad2kos.py` and replace the file on the card.
* Back to K-OS: the RST button on the board. On message screens touching the screen or pressing
  BOOT is enough; after 3 minutes without a touch the program returns to K-OS by itself.
* **Picture mirrored or upside down?** Create the file `/doom/ekran.txt` on the card with one line
  (touch flips together with the picture):
  `madctl=0xA8` - left-right mirror, `madctl=0x68` - top-bottom, `madctl=0xE8` - rotated 180
  degrees (2.4" and 2.8" ILI9341; default `0x28`). On 2.8" ST7789 the default is `0x68`, then:
  `0xE8` left-right, `0x28` top-bottom, `0xA8` rotated. Delete the file to go back to the default.
