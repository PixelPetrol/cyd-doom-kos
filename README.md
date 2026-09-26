<!-- Zmienione dla K-OS 2026-09-25 (galaz kos, na bazie HenrysCat/cyd-doom 1c58bf4, GPL-2.0): dopisek o wersji K-OS na gorze, zdjecie plytki jako link do upstreamu (samego pliku nie ma w tej kopii). -->
> **K-OS:** to repozytorium to port dla systemu K-OS na plytkach CYD (BETA) - opis, zmiany
> i licencja w [README-KOS.md](README-KOS.md), dane gry w [kos/INSTRUKCJA.md](kos/INSTRUKCJA.md).
> Instrukcje wgrywania ponizej sa z oryginalnego projektu i **nie dotycza wersji K-OS**
> (wgranie od `0x0` usuneloby K-OS z plytki); tez pliki `tools/GbaWadUtil/` itp. nie sa tu dolaczone.
>
> **K-OS:** this repository is the K-OS port for CYD boards (BETA) - see [README-KOS.md](README-KOS.md)
> and [kos/INSTRUKCJA.md](kos/INSTRUKCJA.md). The flashing instructions below are from the original
> project and **do not apply to the K-OS build** (flashing at `0x0` would remove K-OS).

# DOOM for the ESP32-2432S028R "Cheap Yellow Display"

Real DOOM (shareware Episode 1) running on the $12 CYD board: ESP32-WROOM
with **no PSRAM**, 2.8" 320x240 SPI display, resistive touch. Based on
[GBADoom](https://github.com/doomhack/GBADoom) (doomhack's Game Boy Advance
port of PrBoom), with an ESP32 platform layer written for this board.

Runs at ~28 fps. The WAD lives memory-mapped in flash, saves persist across
power-off, and a first-boot wizard calibrates the display and touch panel.

![DOOM running on the Cheap Yellow Display](https://raw.githubusercontent.com/HenrysCat/cyd-doom/1c58bf4/docs/cyd-doom.jpg)

---

## Requirements

- ESP32-2432S028R "Cheap Yellow Display" (the common dual-USB 2.8" CYD)
- USB cable, board shows up as a CH340 serial port (here: **COM5**)
- Python 3 with PlatformIO (`pip install platformio`)

## Flashing

### Option A: prebuilt firmware (no toolchain needed)

Grab `cyd-doom-firmware.bin` from a release, then with just
`pip install esptool`:

```
python -m esptool --chip esp32 --port COM5 --baud 921600 write_flash 0x0 cyd-doom-firmware.bin
```

Then build and flash the game data (also pure Python - see
"Building the WAD" below):

```
tools\GbaWadUtil\GbaWadUtil.exe -in doom1.wad -out doom1_gba.wad
python tools/wadtrim.py trim doom1_gba.wad doom1_cyd.wad --limit 0x2E8000 --drop-maps E1M6,E1M7,E1M9
python tools/flashwad.py
```

### Option B: build from source

```
python -m platformio run -t upload    # build + flash firmware
python tools/flashwad.py              # flash the game data (doom1_cyd.wad)
```

Both options default to COM5; pass `--port COMx` (or edit `platformio.ini`)
if your board enumerates differently. The firmware and the WAD are
independent - you only need to re-flash the WAD if it changes.

Maintainers: `python tools/mkrelease.py` packages `dist/` for a release.

---

## First-boot setup (calibration)

The first time the board boots (or whenever you **hold the BOOT button while
powering on**), the setup wizard runs:

**Step 1 - orientation and colour.** The screen shows a large letter **F**,
three coloured bars, and a small corner dot. These clone boards vary, so:

- **Short-press BOOT** to cycle through the 8 possible orientation/colour
  combinations.
- Stop when the **F reads correctly** (upright, not mirrored) and the bars
  are **RED, GREEN, BLUE from left to right**.
- **Hold BOOT for ~1 second** to accept.

**Step 2 - touch calibration.** A white crosshair appears in each corner in
turn. Press and hold your fingertip (or a stylus) on each crosshair until it
turns green. After the fourth one the screen flashes green and the game
starts.

The result is saved to flash - every later boot goes straight into the game.

---

## Controls

### Touch (during play)

```
+---------------------------+----------------+
| MENU                      |            MAP |   top corners
+------------+--------------+----------------+
|            |                               |
|  virtual   |          USE (open doors,     |
|  d-pad     |           flip switches)      |
|  forward/  +-------------------------------+
|  back/     |                               |
|  turn      |          FIRE                 |
+------------+-------------------------------+
     left half            right half
```

- Left half: tap towards the edge you want - up = forward, down = backpedal,
  left/right = turn. (Resistive touch is single-point: you cannot move and
  fire at the same instant - alternate taps, or use serial keys.)
- In menus: FIRE/USE zone = select, MENU zone = back.

### BOOT button

Opens/closes the menu (and confirms "hold to accept" in the wizard).

### Serial keys (115200 baud, e.g. `python -m platformio device monitor`)

| Key       | Action            |
|-----------|-------------------|
| `w` / `s` | forward / back    |
| `a` / `d` | turn left / right |
| `q` / `e` | strafe            |
| `f`       | fire              |
| `space`   | use / confirm     |
| `m` or Enter | menu (Esc)     |
| `t` or Tab   | automap        |

### Saves

8 save slots via the in-game menu, persisted to a flash sector - they
survive power-off and re-flashing the firmware.

---

## What's included / cut

Included: **E1M1-E1M5 and the E1M8 finale** with all enemies, weapons and
secrets. E1M5's exit leads straight to E1M8.

Cut to fit 4MB of flash: E1M6, E1M7, secret map E1M9 (secret exits act as
normal exits), demos, help/credit screens, and all audio (no sound hardware
driver yet - the speaker on GPIO26 is a future project).

## Building the WAD

The WAD files are **not** included in this repository (id's shareware
license doesn't allow distributing modified game data). Get `doom1.wad`
(the DOOM shareware 1.9 IWAD, 4,196,020 bytes - widely mirrored and easy
to find), drop it in the repo root, then:

```
tools\GbaWadUtil\GbaWadUtil.exe -in doom1.wad -out doom1_gba.wad
python tools/wadtrim.py trim doom1_gba.wad doom1_cyd.wad --limit 0x2E8000 --drop-maps E1M6,E1M7,E1M9
python tools/flashwad.py
```

`tools/wadtrim.py analyze <wad>` shows a size breakdown per category.

## Troubleshooting

- **Serial log** (115200) shows boot diagnostics: `[lcd]` lines report the
  display self-test (DC pin probe, SPI speed negotiation), `[stat]` lines
  report fps and free heap every 5 seconds, `[touchdbg]` lines show raw
  touch data while pressed.
- **Wrong orientation / colours / touch drift**: hold BOOT while powering
  on to re-run the setup wizard.
- **`I_Error: ...` on serial**: the game halts with a reason (e.g. missing
  WAD partition - run `tools/flashwad.py`).

## Credits & license

- DOOM engine: id Software; PrBoom contributors (GPL v2)
- GBA port: [doomhack/GBADoom](https://github.com/doomhack/GBADoom)
- `tools/GbaWadUtil` binary from GBADoom
  ([source](https://github.com/doomhack/GbaWadUtil)); ships with Qt5Core.dll
  (LGPL, The Qt Company)
- ESP32/CYD port: this repository. Engine and port code GPL v2 - see
  [LICENSE](LICENSE) and `src/doom/` headers. Game data not included.
