# Publikacja zrodel DOOM dla K-OS (GPL-2.0)

Publiczne repozytorium: **https://github.com/PixelPetrol/cyd-doom-kos** - z **nowa historia**.
Prywatna galaz robocza `kos` (i jej historia z upstreamu) zawiera w starszych commitach niewolne
pliki (`src/doom/gfx/stbar.h` - pasek GBA Doom II, `tools/GbaWadUtil/*.exe|.dll|gbadoom.wad`),
dlatego do publicznego repo idzie tylko **lista plikow** ponizej, skopiowana do czystego katalogu.

Zrodlem prawdy listy jest `kos/sprawdz_publikacje.py` (EXTRA + cale `include/`, `src/doom/`,
`src/port/`) - ta tabela to jej odbitka z 25.09.2026:

| co | pliki |
|---|---|
| licencja i opis | `LICENSE` (GNU GPL 2.0, tekst z upstreamu), `README.md` (upstream + baner K-OS, naglowek zmian), `README-KOS.md`, `.gitignore` |
| narzedzia K-OS | `kos/build.sh`, `kos/wad2kos.py`, `kos/tests/test_wad2kos.py`, `kos/tests/test_silnik_host.py`, `kos/tests/silnik_host.c`, `kos/sprawdz_publikacje.py` |
| dokumenty K-OS | `kos/INSTRUKCJA.md`, `kos/SKLEP.md`, `kos/PUBLIKACJA.md` |
| silnik | `include/*.h` (71), `src/doom/*.c` (56), `src/port/*` (13) - razem z plikami wyzej 153 pliki (od 0.1.1) |

Celowo POMINIETE (nie sa potrzebne do zbudowania obrazu, GPL-2.0 par. 3 tego nie wymaga):

| plik | dlaczego |
|---|---|
| `src/doom/gfx/stbar.h`, `src/doom/st_gfx.c`, `include/st_gfx.h` | pasek stanu GBA Doom II (Torus Games) - usuniete juz z galezi `kos` |
| `tools/GbaWadUtil/` | `.exe`/`.dll` bez zrodel, `gbadoom.wad` z grafika GBA Doom II - usuniete z galezi `kos` |
| `tools/*.py`, `platformio.ini`, `partitions.csv` | sposob upstreamu: wgrywanie od `0x0`, ktore usuwa K-OS |
| `CLAUDE.md` | notatki upstreamu o tamtym sposobie (COM5, GbaWadUtil.exe) - mylace |
| `docs/cyd-doom.jpg` | zdjecie autora upstreamu, licencja nieopisana; `README.md` linkuje do oryginalu |
| `.vscode/`, `kos/STAN-PRAC.md`, `kos/bin/`, `.kos-build/` | edytor, dziennik pracy, obrazy (ida do sklepu), katalog posredni |

## Sprawdzenie (przed kazdym wydaniem)

W prywatnym drzewie `porty/cyd-doom` (galaz `kos`):

```
python3 kos/tests/test_wad2kos.py                         # wad2kos: OK
python3 kos/sprawdz_publikacje.py                         # OK - brak zasobow spoza GPL...
mkdir -p /tmp/up && git archive master src include README.md | tar -x -C /tmp/up
python3 kos/sprawdz_publikacje.py --upstream /tmp/up      # kazdy zmieniony plik ma naglowek zmian
./kos/build.sh all                                        # 3 obrazy, >= 1852 kB na dane
```

`sprawdz_publikacje.py` odrzuca: pliki z bajtem 0 (binarki), rozszerzenia `.wad .kwad .exe .dll
.lmp .pyc .bin .elf .zip .jpg .png .gif .bmp .ico`, nazwy `stbar.h`, `st_gfx.*`, `gbadoom*`,
`GbaWadUtil*`, kod siegajacy po `gfx_stbar`, `"gfx/...`, `"STGANUM`, oraz kazdy plik z ponad
2000 liczb poza `tables.c` i `info.c` (zrodla DOOM na GPL) - tak wygladal wkompilowany `stbar.h`
(test negatywny 25.09: podrzucone `stbar.h` i stary `st_stuff.c` wykryte). Sprawdza tez tekst
`LICENSE` i linie licencji w naszych plikach.

## Zalozenie czystego repo (sesja glowna)

```
cd "porty/cyd-doom"
python3 kos/sprawdz_publikacje.py --kopiuj /sciezka/cyd-doom-kos     # pusty katalog
cd /sciezka/cyd-doom-kos
git init -b main
git add -A
git commit -m "DOOM dla K-OS 0.1.0-beta: port HenrysCat/cyd-doom 1c58bf4 (GBADoom/PrBoom, GPL-2.0)"
python3 kos/sprawdz_publikacje.py --repo .      # git ls-files = lista, zadnych innych plikow
git tag kos-0.1.0-beta
# obecne prywatne PixelPetrol/cyd-doom-kos przemianowac na robocze, zalozyc publiczne o tej nazwie:
git remote add origin https://github.com/PixelPetrol/cyd-doom-kos.git
git push -u origin main --tags
```

W opisie repozytorium na GitHubie: "DOOM (GBADoom/PrBoom) as a K-OS program for ESP32 CYD boards -
BETA, engine only, game data not included. GPL-2.0. Based on HenrysCat/cyd-doom."

## Kolejne wydanie (np. 0.1.1-beta) w istniejacym repo

```
cd "porty/cyd-doom"
python3 kos/sprawdz_publikacje.py --kopiuj /tmp/nowe      # pusty katalog
rsync -a --delete --exclude .git /tmp/nowe/ /sciezka/cyd-doom-kos/
cd /sciezka/cyd-doom-kos && git add -A && python3 kos/sprawdz_publikacje.py --repo .
git commit -m "DOOM dla K-OS 0.1.1-beta: ..."
./kos/build.sh all       # SHA-256 = kos/SKLEP.md
git tag kos-0.1.1-beta && git push origin main --tags
```

## Obraz a zrodlo

* Obrazy do sklepu: `kos/bin/cyd24|cyd28|cyd28s/doom.bin` z `./kos/build.sh all` na tym samym
  drzewie, ktore idzie do repo (rozmiary i SHA-256 w `kos/SKLEP.md`).
* **Obraz jest powtarzalny bit w bit**: `build.sh` zamienia sciezki (repo i katalog Arduino) na
  stale nazwy (`-ffile-prefix-map`) i ustawia `SOURCE_DATE_EPOCH` (2026-09-26), wiec w obrazie nie
  ma ani sciezek z dysku, ani daty budowania. Sprawdzone 26.09: cyd28 zbudowany w drzewie roboczym
  i w kopii z `--kopiuj` (inny katalog, ze spacja w nazwie) - ten sam SHA-256. Warunek: macOS,
  arduino-cli 1.5.1, rdzen esp32:esp32 3.3.11 (w obrazie jest nazwa systemu budujacego,
  `ARDUINO_HOST_OS` = `macosx` - na Linuksie wyjdzie inny skrot, ale ten sam kod).
* Po zalozeniu czystego repo: `./kos/build.sh all` w nim i porownanie SHA-256 z `kos/SKLEP.md`
  to dowod, ze obraz w sklepie powstal z opublikowanego zrodla.
* Kazde kolejne wydanie w sklepie = nowy tag `kos-<wersja>` (wersja z `KOS_DOOM_WERSJA`
  w `src/port/kos.h`; przy nowej wersji warto przesunac tez `SOURCE_DATE_EPOCH` w `build.sh`),
  a link "zrodlo" w sklepie wskazuje ten tag.
