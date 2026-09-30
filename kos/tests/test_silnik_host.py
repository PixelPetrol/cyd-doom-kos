#!/usr/bin/env python3
# DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (patrz LICENSE).
"""Prawdziwy plik .kwad przez prawdziwy silnik - na komputerze.

1. Sprawdzenia programu na plytce (kos.cpp srcCheck/dataCheck + CRC, st_stuff.c KosCheckPatch) -
   to samo lustro co w test_wad2kos.py, tylko na podanym pliku.
2. Silnik z src/doom skompilowany na komputer (kos/tests/silnik_host.c zamiast warstwy ESP32):
   W_Init, R_Init, menu, Nowa gra, E1M1, chodzenie, strzal, automapa - 1500 klatek bez I_Error.
   Klatki co 50 zapisuje jako .ppm (240x160) w katalogu tymczasowym - do obejrzenia.

Plik: pierwszy argument albo zmienna DOOM_KWAD. Bez pliku albo bez kompilatora C - POMINIETE
(kod 0), bo danych gry nie ma w repozytorium. Uruchom:
    DOOM_KWAD=/sciezka/doom.kwad python3 kos/tests/test_silnik_host.py
Zuzycie pamieci NIE odpowiada plytce (wskazniki 64-bitowe, strefa 256 kB) - to test formatu.
"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('DOOM_KWAD', '')
    if not path or not os.path.isfile(path):
        print('silnik_host: POMINIETE - podaj plik .kwad (argument albo DOOM_KWAD)')
        return 0
    cc = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
    blob = open(path, 'rb').read()

    import test_wad2kos as t   # lustro sprawdzen z plytki
    errs = t.engine_accepts(blob)
    bad, _ = t.stbar_engine_ok(blob)
    print(f'plik: {path} ({len(blob)} B)')
    print('  srcCheck/dataCheck/CRC: ' + ('OK' if not errs else f'BLAD {errs}'))
    print('  KosCheckPatch (pasek):  ' + ('OK' if not bad else f'BLAD {bad[:5]}'))
    fails = bool(errs or bad)

    if not cc:
        print('silnik_host: brak kompilatora C - silnik POMINIETY')
        return 1 if fails else 0
    tmp = tempfile.mkdtemp(prefix='doom-host-')
    exe = os.path.join(tmp, 'silnik')
    srcs = sorted(glob.glob(os.path.join(ROOT, 'src', 'doom', '*.c')))
    cmd = [cc, '-O1', '-w', '-std=gnu99', '-include', 'stddef.h', '-include', 'string.h',
           '-Wno-error=implicit-function-declaration', '-Wno-error=int-conversion',
           '-Wno-error=incompatible-pointer-types', '-Wno-error=implicit-int',
           '-I', os.path.join(ROOT, 'include'), '-o', exe, os.path.join(HERE, 'silnik_host.c')] + srcs
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print('  kompilacja silnika: BLAD\n' + r.stderr[-2000:])
        return 1
    out = os.path.join(tmp, 'klatki')
    os.makedirs(out)
    try:
        r = subprocess.run([exe, path, out, '1500'], capture_output=True, text=True, timeout=300)
        log = r.stdout + r.stderr
        rc = r.returncode
    except subprocess.TimeoutExpired:
        log, rc = 'TIMEOUT 300 s', -1
    frames = sorted(glob.glob(os.path.join(out, '*.ppm')))
    distinct = len({open(f, 'rb').read() for f in frames})
    ok = rc == 0 and 'koniec OK' in log and 'I_Error' not in log and distinct >= 10
    print(f'  silnik: {"OK" if ok else "BLAD"} (kod {rc}, {len(frames)} klatek zapisanych, {distinct} roznych)')
    for line in log.splitlines():
        if line.startswith(('I_Error', 'HOST', 'Z_Init', 'Playing')):
            print('    ' + line)
    print(f'  klatki: {out}')
    fails = fails or not ok
    print('silnik_host: ' + ('OK' if not fails else 'BLEDY'))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
