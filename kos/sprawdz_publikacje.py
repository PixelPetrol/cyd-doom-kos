#!/usr/bin/env python3
# DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (patrz LICENSE).
"""Lista plikow publicznego repozytorium DOOM dla K-OS i sprawdzenie, ze nie ma w nich niczego
spoza GPL (grafiki GBA Doom II, WAD-ow, binarek bez zrodel, obrazkow o nieznanej licencji).

  python3 kos/sprawdz_publikacje.py              sprawdz pliki z listy w tym drzewie
  python3 kos/sprawdz_publikacje.py --lista      wypisz liste plikow do publicznego repo
  python3 kos/sprawdz_publikacje.py --kopiuj DIR skopiuj pliki z listy do DIR (nowe, czyste repo)
  python3 kos/sprawdz_publikacje.py --repo DIR   sprawdz gotowe repo: `git ls-files` = lista
  python3 kos/sprawdz_publikacje.py --upstream DIR  plus: kazdy plik rozny od upstreamu
                                                 (HenrysCat/cyd-doom 1c58bf4) ma naglowek zmian

Kod wyjscia 0 = mozna publikowac, 1 = sa problemy (wypisane). Skrypt tylko czyta, nic nie wysyla.
DLACZEGO LISTA, A NIE "WSZYSTKO Z GALEZI": publiczne repo dostaje nowa historie bez niewolnych
plikow upstreamu - a w historii prywatnej galezi one zostaja (stbar.h, gbadoom.wad, .exe).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Pliki spoza src/ i include/ podane z nazwy; src/ i include/ - cale katalogi (patrz files()).
EXTRA = [
    'LICENSE', 'README.md', 'README-KOS.md', '.gitignore',
    'kos/build.sh', 'kos/wad2kos.py', 'kos/tests/test_wad2kos.py', 'kos/tests/test_silnik_host.py',
    'kos/tests/silnik_host.c', 'kos/INSTRUKCJA.md',
    'kos/SKLEP.md', 'kos/PUBLIKACJA.md', 'kos/sprawdz_publikacje.py',
]
DIRS = [('include', ('.h',)), ('src/doom', ('.c', '.h')), ('src/port', ('.c', '.cpp', '.h'))]
# Czego NIE bierzemy z galezi i dlaczego (--lista to wypisze).
SKIPPED = {
    'CLAUDE.md': 'notatki upstreamu o jego sposobie wgrywania (COM5, GbaWadUtil.exe) - myla',
    'platformio.ini': 'budowanie upstreamu (PlatformIO, wgrywanie od 0x0 usuwa K-OS)',
    'partitions.csv': 'tablica partycji upstreamu - pod K-OS nie wolno jej wgrywac',
    'tools/': 'skrypty wgrywania upstreamu (flashwad, wadtrim, mkrelease) - nie dla K-OS',
    'docs/cyd-doom.jpg': 'zdjecie autora upstreamu, licencja nieopisana; README linkuje do oryginalu',
    '.vscode/': 'ustawienia edytora',
    'kos/STAN-PRAC.md': 'dziennik pracy (wewnetrzny)',
    'kos/bin/': 'zbudowane obrazy - ida do sklepu K-OS, nie do zrodel',
}
FORBIDDEN_NAME = re.compile(r'(\.(wad|kwad|exe|dll|lmp|pyc|bin|elf|zip|jpg|jpeg|png|gif|bmp|ico)$)|'
                            r'(^|/)(stbar\.h|st_gfx\.[ch]|gbadoom[^/]*|GbaWadUtil[^/]*)$', re.I)
# Kod, ktory siegalby po usuniete zasoby (sam napis w komentarzu o tym, co usunieto, jest w porzadku).
FORBIDDEN_CODE = [
    (re.compile(r'gfx_stbar'), 'tablica paska GBA Doom II (gfx_stbar)'),
    (re.compile(r'#\s*include\s*"gfx/'), 'dolaczenie grafiki z src/doom/gfx'),
    (re.compile(r'"STGANUM'), 'cyfry GBA Doom II z gbadoom.wad (STGANUM)'),
    (re.compile(r'gbadoom\.wad"'), 'odwolanie do gbadoom.wad jako pliku'),
]
# Pliki z duzymi tablicami liczb, ktore SA czescia zrodel GPL (id Software 1997 / PrBoom / GBADoom).
# Kazdy inny plik z taka tablica to podejrzenie wkompilowanej grafiki - jak byl stbar.h.
BIG_TABLES_OK = {
    'src/doom/tables.c': 'finesine/finetangent/tantoangle ze zrodel DOOM (GPL)',
    'src/doom/info.c': 'states/mobjinfo ze zrodel DOOM (GPL)',
}
BIG_TABLE = 2000              # tyle liczb w jednym pliku = "tablica danych"


def files(root=ROOT):
    out = list(EXTRA)
    for d, exts in DIRS:
        p = os.path.join(root, d)
        if os.path.isdir(p):
            out += sorted(f'{d}/{f}' for f in os.listdir(p) if f.endswith(exts))
    return out


def check(root, names, upstream=None):
    probs = []
    for n in names:
        p = os.path.join(root, n)
        if not os.path.isfile(p):
            probs.append(f'{n}: brak pliku')
            continue
        if FORBIDDEN_NAME.search(n):
            probs.append(f'{n}: zakazany rodzaj pliku (WAD, binarka, obrazek albo zasob GBA)')
        data = open(p, 'rb').read()
        if b'\0' in data:
            probs.append(f'{n}: plik binarny (bajt 0) - w publicznych zrodlach tylko tekst')
            continue
        text = data.decode('utf-8', 'replace')
        # Tylko kod: dokumenty (.md) moga nazywac usuniete zasoby, zeby wyjasnic, czemu ich nie ma.
        if n.endswith(('.c', '.h', '.cpp', '.py', '.sh', '.ino')) and n != 'kos/sprawdz_publikacje.py':
            for rx, why in FORBIDDEN_CODE:
                if rx.search(text):
                    probs.append(f'{n}: {why}')
        nums = len(re.findall(r'(?<![\w.])(?:0x[0-9a-fA-F]+|\d+)(?![\w.])', text))
        if nums > BIG_TABLE and n not in BIG_TABLES_OK:
            probs.append(f'{n}: {nums} liczb - wyglada na wkompilowane dane (jak stbar.h); '
                         'sprawdz i dopisz do BIG_TABLES_OK tylko, jesli to zrodla GPL')
    lic = os.path.join(root, 'LICENSE')
    if os.path.isfile(lic):
        head = open(lic, encoding='utf-8', errors='replace').read(400)
        if 'GNU GENERAL PUBLIC LICENSE' not in head or 'Version 2, June 1991' not in head:
            probs.append('LICENSE: to nie jest tekst GNU GPL 2.0')
    for n in names:
        if n.startswith('src/port/kos') or n.startswith('src/port/safewrite') or n.startswith('kos/') and n.endswith(('.py', '.sh')):
            p = os.path.join(root, n)
            if os.path.isfile(p) and 'Licencja: GPL' not in open(p, encoding='utf-8', errors='replace').read(600) \
                    and 'GPL-2.0 lub pozniejsza' not in open(p, encoding='utf-8', errors='replace').read(600):
                probs.append(f'{n}: brak linii z licencja na poczatku pliku')
    if upstream:
        for n in names:
            if not n.startswith(('src/', 'include/')) and n != 'README.md':
                continue
            up = os.path.join(upstream, n)
            mine = os.path.join(root, n)
            if os.path.isfile(up) and os.path.isfile(mine) and open(up, 'rb').read() != open(mine, 'rb').read():
                if 'Zmienione dla K-OS' not in open(mine, encoding='utf-8', errors='replace').readline():
                    probs.append(f'{n}: rozni sie od upstreamu, a nie ma naglowka "Zmienione dla K-OS" (GPL-2.0 par. 2a)')
    return probs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--lista', action='store_true')
    ap.add_argument('--kopiuj', metavar='DIR')
    ap.add_argument('--repo', metavar='DIR')
    ap.add_argument('--upstream', metavar='DIR')
    a = ap.parse_args()
    names = files()
    if a.lista:
        print('\n'.join(names))
        print('\npominiete celowo:')
        for k, v in SKIPPED.items():
            print(f'  {k:20s} {v}')
        return 0
    if a.kopiuj:
        if os.path.exists(a.kopiuj) and os.listdir(a.kopiuj):
            print(f'{a.kopiuj}: katalog nie jest pusty - nie kopiuje')
            return 1
        for n in names:
            dst = os.path.join(a.kopiuj, n)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(ROOT, n), dst)
        print(f'skopiowano {len(names)} plikow do {a.kopiuj}')
        root = a.kopiuj
    else:
        root = a.repo or ROOT
    probs, checked = [], names
    if a.repo:
        try:
            tracked = subprocess.run(['git', '-C', a.repo, 'ls-files', '-z'], capture_output=True, text=True,
                                     check=True).stdout.split('\0')
            tracked = [t for t in tracked if t]
        except (OSError, subprocess.CalledProcessError) as e:
            probs.append(f'{a.repo}: git ls-files nie dziala ({e})')
            tracked = []
        probs += [f'{n}: w repo, a nie ma go na liscie' for n in sorted(set(tracked) - set(names))]
        probs += [f'{n}: na liscie, a nie ma go w repo (git add?)' for n in sorted(set(names) - set(tracked))]
        # Sprawdzamy tez pliki spoza listy - to wlasnie one moga byc zasobem, ktory sie przemknal.
        checked = sorted(set(names) | set(tracked))
    probs = check(root, checked, a.upstream) + probs
    print(f'sprawdzone pliki: {len(checked)} w {root}')
    if probs:
        print('PROBLEMY:')
        for p in probs:
            print('  ' + p)
        return 1
    print('OK - brak zasobow spoza GPL, binarek i WAD-ow; licencja GPL-2.0 na miejscu')
    return 0


if __name__ == '__main__':
    sys.exit(main())
