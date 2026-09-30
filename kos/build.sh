#!/bin/bash
# DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (patrz LICENSE).
# DOOM jako program K-OS - kompilacja naszym lancuchem (arduino-cli, rdzen esp32:esp32 3.3.11).
#   ./kos/build.sh all              -> wszystkie trzy plytki + rozmiary i sumy SHA-256
#   ./kos/build.sh                  -> cyd28 (2.8" ILI9341, plytka z oryginalu cyd-doom)
#   BOARD=24 ./kos/build.sh         -> cyd24 (2.4", dotyk na magistrali LCD)
#   BOARD=28 PANEL=st7789 ./kos/build.sh -> cyd28s
#   ./kos/build.sh size             -> jedna plytka plus rozmiar i zapas do ota_0
#
# OD ZERA (GPL-2.0 par. 3 - skrypt budowania nalezy do zrodla):
#   arduino-cli core update-index
#   arduino-cli core install esp32:esp32@3.3.11
#   ./kos/build.sh all
# Nic wiecej: port nie uzywa bibliotek Arduino (ekran, dotyk i karta ida przez ESP-IDF z rdzenia).
# Obrazy trafiaja do kos/bin/<plytka>/doom.bin.
#
# DLACZEGO KATALOG POSREDNI, A NIE SZKIC W MIEJSCU: upstream trzyma naglowki w include/,
# a zrodla w src/doom i src/port (uklad PlatformIO). arduino-cli nie przyjmie -I ze sciezka
# ze spacja ("CYD wifi radar") przez build-property, wiec skladamy plaski szkic w .kos-build/.
# Zrodlem prawdy zostaja pliki repo - katalog posredni jest kasowany przy kazdym budowaniu.
#
# WGRYWANIE JEST TU CELOWO NIEDOSTEPNE. Plytka i port naleza do Piotra.
set -e
export PATH="/opt/homebrew/bin:$PATH"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CORE_WANT="3.3.11"
LIMIT=2555904          # ota_0 w loader/partitions_loader.csv (0x270000)
DATA_MIN=1896448       # 1852 kB na dane - tyle obiecuje sklep i INSTRUKCJA.md

if [ "${1:-}" = "all" ]; then
  for b in "BOARD=24" "BOARD=28" "BOARD=28 PANEL=st7789"; do
    env $b "$0" size || exit 1
  done
  echo "===="
  if command -v shasum >/dev/null; then SHA="shasum -a 256"; else SHA="sha256sum"; fi
  for id in cyd24 cyd28 cyd28s; do
    f="$ROOT/kos/bin/$id/doom.bin"
    printf '%-7s %8s B  sha256 %s\n' "$id" "$(wc -c < "$f" | tr -d ' ')" "$($SHA "$f" | cut -d' ' -f1)"
  done
  exit 0
fi

command -v arduino-cli >/dev/null || { echo "brak arduino-cli (https://arduino.github.io/arduino-cli/)"; exit 1; }
CORE_HAVE="$(arduino-cli core list 2>/dev/null | awk '$1=="esp32:esp32"{print $2}')"
if [ "$CORE_HAVE" != "$CORE_WANT" ]; then
  echo "rdzen esp32:esp32 jest w wersji '${CORE_HAVE:-brak}', a potrzebny $CORE_WANT:"
  echo "  arduino-cli core install esp32:esp32@$CORE_WANT"
  exit 1
fi

BOARD="${BOARD:-28}"
PANEL="${PANEL:-ili9341}"
case "$BOARD" in
  24) BOARD_FLAGS="-DCYD_BOARD_24=1 -DCYD_BL_PIN=27"; BOARD_ID="cyd24" ;;
  28) BOARD_FLAGS="-DCYD_BOARD_28=1 -DCYD_BL_PIN=21"; BOARD_ID="cyd28" ;;
  *)  echo "nieznana plytka BOARD=$BOARD (24 albo 28)"; exit 1 ;;
esac
case "$PANEL" in
  ili9341) PANEL_FLAGS="-DCYD_PANEL_ILI9341=1" ;;
  st7789)
    [ "$BOARD" = "28" ] || { echo "PANEL=st7789 istnieje tylko dla BOARD=28"; exit 1; }
    PANEL_FLAGS="-DCYD_PANEL_ST7789=1"; BOARD_ID="cyd28s" ;;
  *) echo "nieznany panel PANEL=$PANEL"; exit 1 ;;
esac

STAGE="$ROOT/.kos-build/doom_kos"
BUILDDIR="$ROOT/.kos-build/out-$BOARD_ID"
rm -rf "$STAGE"; mkdir -p "$STAGE" "$BUILDDIR"
cp include/*.h "$STAGE/"
cp src/doom/*.c "$STAGE/"
cp src/port/*.c src/port/*.cpp src/port/*.h "$STAGE/"
# Szkic musi miec .ino o nazwie katalogu. setup()/loop() siedza w main.cpp.
printf '// setup() i loop() sa w main.cpp (warstwa K-OS portu DOOM).\n' > "$STAGE/doom_kos.ino"

FQBN="esp32:esp32:esp32:PartitionScheme=huge_app,PSRAM=disabled,FlashSize=4M,CPUFreq=240,FlashFreq=80"
# Obraz powtarzalny bit w bit - z publicznego repo ma wyjsc ten sam SHA-256 co w sklepie (GPL:
# kazdy moze sprawdzic, ze obraz powstal z tego zrodla). Dwie rzeczy psuly powtarzalnosc: sciezki
# plikow w __FILE__ (ESP_ERROR_CHECK, asserty rdzenia) i __DATE__/__TIME__ w raporcie chipu rdzenia.
# Stale nazwy zamiast sciezek (przy okazji w obrazie nie ma nazwy konta ani katalogow autora)
# i data z SOURCE_DATE_EPOCH (GCC bierze ja do __DATE__/__TIME__). Cudzyslowy w wartosci: sciezka
# ma spacje, a arduino-cli dzieli przepis na argumenty z poszanowaniem cudzyslowow.
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1790726400}"     # 2026-09-30 00:00 UTC (0.1.1-beta)
DATA_DIR="$(arduino-cli config get directories.data 2>/dev/null || true)"
[ -n "$DATA_DIR" ] || DATA_DIR="$HOME/Library/Arduino15"
# Caly katalog repo (szkic posredni, katalog budowania, katalog roboczy kompilatora w DWARF),
# bo skrot ELF-a trafia do naglowka obrazu (esp_app_desc.app_elf_sha256).
MAP="\"-ffile-prefix-map=$ROOT=kos-doom\" \"-ffile-prefix-map=$DATA_DIR=arduino15\""
# -O2 jak w upstream platformio.ini: silnik liczy w petli rysowania, -Os kosztuje klatki.
FLAGS="-O2 $BOARD_FLAGS $PANEL_FLAGS -Werror=return-type $MAP"
# GCC 14 z rdzenia 3.3.11 robi z tych ostrzezen bledy; upstream budowal na GCC 8 (rdzen 2.0.x),
# gdzie byly ostrzezeniami. Kod silnika to PrBoom/GBADoom - nie przepisujemy go pod nowy C.
CFLAGS_OLD="-Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=implicit-int"

arduino-cli compile --fqbn "$FQBN" \
  --build-property "compiler.c.extra_flags=$FLAGS $CFLAGS_OLD" \
  --build-property "compiler.cpp.extra_flags=$FLAGS" \
  --warnings "${WARN:-none}" --build-path "$BUILDDIR" "$STAGE"

mkdir -p "$ROOT/kos/bin/$BOARD_ID"
OUT="$ROOT/kos/bin/$BOARD_ID/doom.bin"
cp "$BUILDDIR/doom_kos.ino.bin" "$OUT"
echo "obraz: $OUT"

if [ "${1:-build}" = "size" ]; then
  SZ=$(wc -c < "$OUT" | tr -d ' ')
  # Dane gry zaczynaja sie od pierwszej strony 64 kB za obrazem, plus 4 kB naglowka (kos.cpp).
  BASE=$(( (SZ + 65535) / 65536 * 65536 ))
  DATA=$(( LIMIT - BASE - 4096 ))
  echo "----"
  echo "rozmiar: $SZ B    limit ota_0: $LIMIT B    zapas: $((LIMIT-SZ)) B"
  echo "dane od +$(printf '0x%X' $BASE), miejsce na dane: $DATA B ($((DATA/1024)) kB), do nastepnej strony 64 kB: $((BASE-SZ)) B"
  echo "bajt 0:  0x$(head -c1 "$OUT" | xxd -p) (ma byc 0xe9)"
  [ "$SZ" -le "$LIMIT" ] || { echo "OBRAZ ZA DUZY"; exit 1; }
  [ "$DATA" -ge "$DATA_MIN" ] || { echo "MNIEJ NIZ 1852 kB NA DANE - silnik urosl za strone 64 kB"; exit 1; }
fi
