#!/usr/bin/env python3
# DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (patrz LICENSE).
"""wad2kos.py - przygotowanie danych gry dla DOOM na K-OS (plik /doom/doom.kwad na karcie SD).

Uzycie (na komputerze, Python 3.8+, bez dodatkowych bibliotek):
  python3 wad2kos.py WEJSCIE.wad doom.kwad                 najwiecej map, ile sie zmiesci
  python3 wad2kos.py WEJSCIE.wad doom.kwad --maps E1M1,E1M2  wybrane mapy
  python3 wad2kos.py --budzet WEJSCIE.wad                  tylko plan: co sie zmiesci
  python3 wad2kos.py WEJSCIE.wad doom.kwad --podglad pasek.png   plus obraz paska stanu
  python3 wad2kos.py --analiza WEJSCIE.wad                 rozbicie pliku na kategorie

Wejscie - IWAD uzytkownika (skrypt niczego nie pobiera):
  * freedoom1.wad  Freedoom: Phase 1, darmowy (BSD-3-Clause), https://freedoom.github.io
  * doom1.wad      DOOM shareware (id Software)
  * doom.wad       DOOM / The Ultimate DOOM - pelna wersja (uzyty zostaje epizod 1)
  DOOM II, Freedoom: Phase 2, FreeDM, Heretic, Chex Quest i PWAD-y sa odrzucane z komunikatem.

DLACZEGO TEN SKRYPT ISTNIEJE (a nie GbaWadUtil.exe z upstreamu):
  * GbaWadUtil to binarka Windows (Qt). Samo przetwarzanie to ~500 linii C++
    (doomhack/GbaWadUtil, wadprocessor.cpp) - tu jest ich wierne przepisanie: VERTEXES -> 16.16,
    LINEDEFS -> line_t (56 B), SEGS -> seg_t (32 B), SIDEDEFS -> sidedef_t (12 B) z numerami
    tekstur, PNAMES wielkimi literami, bez D_/DS/DP/GENMIDI.
  * Slot ota_0 ma 1852 kB na dane, a upstream mial 2,9 MB. Samo wyciecie map nie wystarcza,
    wiec skrypt wycina tez GRAFIKE, ktorej zachowane mapy nie uzywaja (latki scian, plaskie
    tekstury, sprite'y potworow), liczac to z samych map i z tabel silnika (info.c, p_spec.c).
  * Pasek stanu: upstream mial wkompilowane tlo z GBA Doom II (Torus Games) i cyfry z gbadoom.wad.
    Tu pasek jest skladany z grafiki WAD-u uzytkownika (STBAR + STARMS + STTPRCNT) do 240 px
    i zapisywany jako lump KOSSTBAR - kazdy bajt wyniku pochodzi z WAD-u uzytkownika albo jest
    policzony: PLAYPALn = alias PLAYPAL (albo --gamma), M_ARUN i M_GAMMA z fontu STCFN.
  * WAD-owi nie ufamy: katalog, mapy i latki sa sprawdzane, zanim cokolwiek trafi do silnika,
    ktory na plytce czyta dane wprost z flasha bez zadnych granic. Zly WAD = komunikat, nie plik.

Format wyniku (musi sie zgadzac z src/port/kos.cpp i src/doom/st_stuff.c): IWAD z lumpami
wyrownanymi do 4 B, lump KOSINFO zaczyna sie od "KOSDOOM2", lump KOSSTBAR (64 B naglowka
"KOSSTBR1" + 240x32 pikseli), katalog na koncu, a za nim 4 B CRC-32 (LE) calego pliku przed nimi.

Wynik jest PRZEROBIONYM WAD-em. Zrobiony z doom1.wad/doom.wad - tylko do wlasnego uzytku
(licencja id Software pozwala rozdawac wylacznie niezmieniony doom1.wad). Z Freedoomu wolno go
rozdawac na licencji BSD-3-Clause Freedoomu (z jej tekstem i lista autorow).
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE_SRC = os.path.join(os.path.dirname(HERE), 'src', 'doom')

SLOT_OTA0 = 2555904          # loader/partitions_loader.csv: ota_0 0x270000
HDR_SECTOR = 4096            # naglowek danych w ogonie slotu (src/port/kos.cpp)
# Miejsce na dane, ktore obiecuje sklep: silnik < 640 kB, wiec dane od +0xA0000 w slocie.
# kos/build.sh odmawia zbudowania obrazu, po ktorym zostaloby mniej.
DEFAULT_CAP = SLOT_OTA0 - 0xA0000 - HDR_SECTOR          # 1 896 448 B = 1852 kB
KOSINFO_MAGIC = b'KOSDOOM2'
STBAR_MAGIC = b'KOSSTBR1'
MAX_INPUT = 256 * 1024 * 1024     # IWAD-y DOOM/Freedoom maja ponizej 40 MB
MAX_LUMPS_IN = 65536
MAX_LUMPS_OUT = 8192              # KD_MAX_LUMPS w src/port/kos.cpp
CRC_RESIDUE = 0x2144DF1C          # CRC-32 pliku razem z dopisanym CRC (kos.cpp: KD_CRC_RESIDUE)

MAP_CHILD = ['THINGS', 'LINEDEFS', 'SIDEDEFS', 'VERTEXES', 'SEGS',
             'SSECTORS', 'NODES', 'SECTORS', 'REJECT', 'BLOCKMAP']
REC = {'THINGS': 10, 'LINEDEFS': 14, 'SIDEDEFS': 30, 'VERTEXES': 4, 'SEGS': 12,
       'SSECTORS': 4, 'NODES': 28, 'SECTORS': 26}
NO_INDEX = 0xFFFF
ML_TWOSIDED = 4
SECRET_EXITS = (51, 124)          # S1 i W1 "tajne wyjscie" - tylko przez nie da sie wejsc na ExM9
MTF_SKILLS, MTF_NOTSINGLE = 7, 16

# Grafika pelnoekranowa, ktora dla epizodu 1 w trybie shareware jest albo tlem, bez ktorego da sie
# zyc (przerywnik, final), albo nie pokaze sie nigdy (E2-E4, DOOM II). Zostaje jako JEDNA wspolna
# czarna latka 320x200: nazwa musi byc, bo silnik woluje ja z nazwy i bez niej stanalby z I_Error.
BLACK_NAMES = ('HELP2', 'CREDIT', 'WIMAP0', 'WIMAP1', 'WIMAP2', 'INTERPIC', 'VICTORY2', 'ENDPIC',
               'PFUB1', 'PFUB2', 'BOSSBACK', 'END0', 'END1', 'END2', 'END3', 'END4', 'END5', 'END6')
# Lumpy, po ktorych CheckIWAD2 (d_main.c) uznaje dane za TNT/Plutonie - czyli za DOOM II.
COMMERCIAL_MARKERS = ('MURAL1', 'WFALL1')


class WadError(Exception):
    """Blad danych wejsciowych: komunikat dla uzytkownika, bez sladu stosu."""


class Lump:
    __slots__ = ('name', 'data', 'alias', '_key')

    def __init__(self, name, data, alias=None):
        self.name = name        # str, wielkie litery jak w katalogu
        self.data = data        # bytes albo memoryview (bez kopii)
        self.alias = alias      # nazwa lumpu, z ktorym dzieli dane (0 B w wyniku)
        self._key = None

    def key(self):
        # Klucz do laczenia identycznych tresci w wyniku; liczony raz na lump.
        if self._key is None:
            self._key = (len(self.data), hashlib.sha1(self.data).digest())
        return self._key


def name8(b):
    # latin-1 w obie strony: nazwy lumpow to bajty, a 'replace' + UTF-8 psulby nazwy spoza ASCII
    return bytes(b).split(b'\0', 1)[0].decode('latin-1').upper()


def is_map_marker(n):
    return bool(re.fullmatch(r'E\dM\d', n) or re.fullmatch(r'MAP\d\d', n))


def s32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


# ---------------------------------------------------------------------------------------
# Odczyt WAD-u - wszystko sprawdzone, zanim zostanie uzyte
# ---------------------------------------------------------------------------------------
def read_wad(path):
    try:
        size = os.path.getsize(path)
    except OSError as e:
        raise WadError(f'{path}: nie moge odczytac pliku ({e.strerror})')
    if size > MAX_INPUT:
        raise WadError(f'{path}: {size:,} B - to za duzo jak na IWAD DOOM-a (limit {MAX_INPUT:,} B)')
    try:
        with open(path, 'rb') as f:
            d = f.read(MAX_INPUT + 1)
    except OSError as e:
        raise WadError(f'{path}: nie moge odczytac pliku ({e.strerror})')
    if len(d) > MAX_INPUT:
        raise WadError(f'{path}: to za duzo jak na IWAD DOOM-a (limit {MAX_INPUT:,} B)')
    if len(d) < 12:
        raise WadError(f'{path}: za krotki na plik WAD ({len(d)} B)')
    ident, n, ofs = struct.unpack_from('<4sII', d, 0)
    if ident not in (b'IWAD', b'PWAD'):
        raise WadError(f'{path}: to nie jest plik WAD (brak IWAD/PWAD na poczatku)')
    if n > MAX_LUMPS_IN or ofs > len(d) or ofs + 16 * n > len(d):
        raise WadError(f'{path}: uszkodzony katalog lumpow ({n} wpisow od {ofs}, plik ma {len(d):,} B)')
    mv = memoryview(d)
    lumps = []
    for i in range(n):
        fp, sz, nm = struct.unpack_from('<II8s', d, ofs + 16 * i)
        name = name8(nm)
        if sz and (fp > len(d) or sz > len(d) - fp):
            raise WadError(f'{path}: lump nr {i} ({name}) wychodzi poza plik - plik uszkodzony albo niepelny')
        # memoryview: bez kopii, wiec nawet katalog z tysiacami wpisow na ten sam obszar nie zje RAM-u
        lumps.append(Lump(name, mv[fp:fp + sz] if sz else b''))
    return ident, lumps, d


def last_index(lumps):
    # Silnik (w_wad.c) szuka nazwy OD KONCA katalogu - ostatni wpis wygrywa.
    return {l.name: i for i, l in enumerate(lumps)}


# ---------------------------------------------------------------------------------------
# Rozpoznanie IWAD-u
# ---------------------------------------------------------------------------------------
KINDS = {
    'freedoom1': 'Freedoom: Phase 1 (freedoom1.wad, BSD-3-Clause)',
    'doom-shareware': 'DOOM shareware (doom1.wad, id Software)',
    'doom-pelna': 'DOOM, pelna wersja (doom.wad, id Software)',
    'doom-ultimate': 'The Ultimate DOOM (doom.wad, id Software)',
}
ENGINE_NEEDS = ('PLAYPAL', 'COLORMAP', 'TEXTURE1', 'PNAMES', 'F_START', 'F_END', 'S_START', 'S_END')
DOOM_SIGNS = ('STBAR', 'STARMS', 'M_DOOM', 'TITLEPIC', 'STFST01', 'TROOA1')


def identify(ident, lumps):
    names = {l.name for l in lumps}
    maps = [l.name for l in lumps if is_map_marker(l.name)]
    if 'KOSINFO' in names:
        raise WadError('To juz jest plik z wad2kos.py (.kwad) - podaj oryginalny WAD.')
    if ident == b'PWAD':
        raise WadError('To jest PWAD (dodatek z mapami), a potrzebny jest pelny IWAD: '
                       'freedoom1.wad, doom1.wad albo doom.wad.')
    if 'FREEDM' in names:
        raise WadError('To FreeDM (Freedoom tylko do gry wieloosobowej) - nie nadaje sie. '
                       'Uzyj freedoom1.wad (Freedoom: Phase 1).')
    doom2 = any(m.startswith('MAP') for m in maps)
    if 'FREEDOOM' in names:
        if doom2 or 'E1M1' not in names:
            raise WadError('To Freedoom: Phase 2 (freedoom2.wad, odpowiednik DOOM II) - nieobslugiwany. '
                           'Uzyj freedoom1.wad (Freedoom: Phase 1).')
        kind = 'freedoom1'
    else:
        if doom2:
            raise WadError('To DOOM II / Final DOOM albo inna gra z mapami MAPxx - nieobslugiwana. '
                           'Uzyj freedoom1.wad, doom1.wad albo doom.wad.')
        missing = [x for x in DOOM_SIGNS if x not in names] + \
                  [f'E1M{i}' for i in range(1, 10) if f'E1M{i}' not in names]
        if missing:
            raise WadError('Nieznany IWAD - nie wyglada na DOOM ani Freedoom (brak: ' + ', '.join(missing[:8]) +
                           '). Uzyj freedoom1.wad, doom1.wad albo doom.wad.')
        eps = {m[1] for m in maps}
        kind = 'doom-ultimate' if '4' in eps else 'doom-pelna' if eps - {'1'} else 'doom-shareware'
    lacking = [x for x in ENGINE_NEEDS if x not in names]
    if lacking:
        raise WadError('W pliku brakuje lumpow, bez ktorych silnik nie ruszy: ' + ', '.join(lacking))
    return kind


# ---------------------------------------------------------------------------------------
# Latki (patch_t)
# ---------------------------------------------------------------------------------------
def patch_check(d, name, full=False):
    """Naglowek (w, h, lo, to) po sprawdzeniu, ze kolumny i posty leza w lumpie. full=True:
    posty tez w wysokosci latki - tak jak sprawdza silnik dla paska (st_stuff.c, KosCheckPatch)."""
    n = len(d)
    if n < 8:
        raise WadError(f'latka {name}: za krotka ({n} B)')
    w, h, lo, to = struct.unpack_from('<hhhh', d, 0)
    if w <= 0 or h <= 0 or w > 4096 or h > 4096 or 8 + 4 * w > n:
        raise WadError(f'latka {name}: zly naglowek ({w}x{h}, {n} B)')
    # Pozycje, od ktorych lancuch postow juz raz doszedl do konca kolumny. Bez tego zlosliwa latka
    # (4096 kolumn wskazujacych w srodek jednego dlugiego lancucha) kosztowalaby kwadrat dlugosci
    # lumpu - a tak kazda pozycja jest przechodzona najwyzej raz.
    good = set()
    for x, p in enumerate(struct.unpack_from(f'<{w}I', d, 8)):
        walk = []
        while p not in good:   # kazdy post przesuwa p o >= 4 B, wiec petla konczy sie na koncu lumpu
            if p >= n:
                raise WadError(f'latka {name}: kolumna {x} wychodzi poza lump')
            top = d[p]
            if top == 0xFF:
                break
            if p + 4 > n or p + 4 + d[p + 1] > n:
                raise WadError(f'latka {name}: post w kolumnie {x} wychodzi poza lump')
            if full and top + d[p + 1] > h:
                raise WadError(f'latka {name}: post w kolumnie {x} wychodzi poza wysokosc {h}')
            walk.append(p)
            if len(walk) > h + 64:
                raise WadError(f'latka {name}: kolumna {x} ma wiecej postow niz wierszy')
            p += d[p + 1] + 4
        good.update(walk)
        good.add(p)
    return w, h, lo, to


def patch_decode(d, name):
    w, h, lo, to = patch_check(d, name)
    cols = []
    for x in range(w):
        p = struct.unpack_from('<I', d, 8 + 4 * x)[0]
        col = {}
        while d[p] != 0xFF:          # patch_check ograniczyl liczbe postow w kolumnie
            top, cnt = d[p], d[p + 1]
            for k in range(cnt):
                col[top + k] = d[p + 3 + k]
            p += cnt + 4
        cols.append(col)
    return w, h, lo, to, cols


def patch_encode(w, h, cols):
    body = bytearray()
    offs = []
    base = 8 + 4 * w
    for x in range(w):
        offs.append(base + len(body))
        col = cols[x]
        y = 0
        while y < h:
            if y not in col:
                y += 1
                continue
            run = []
            y0 = y
            while y in col and len(run) < 128:
                run.append(col[y])
                y += 1
            body += bytes([y0, len(run), 0]) + bytes(run) + b'\0'
        body += b'\xff'
    return struct.pack('<hhhh', w, h, 0, 0) + b''.join(struct.pack('<I', o) for o in offs) + bytes(body)


def black_patch():
    """320x200 cala czarna: kazda kolumna wskazuje na jedna wspolna kolumne (jak wadtrim.py)."""
    col_ofs = 8 + 320 * 4
    blob = struct.pack('<hhhh', 320, 200, 0, 0) + struct.pack('<I', col_ofs) * 320
    return blob + b'\x00' + bytes([200]) + b'\x00' + b'\x00' * 200 + b'\x00' + b'\xff'


def text_patch(get, text):
    """Napis z fontu STCFN (maly font HUD-u). Menu GBADoom chce tu lumpu-grafiki; wlasny
    napis zamiast grafiki z gbadoom.wad, zeby nic nie pochodzilo spoza WAD-u uzytkownika."""
    glyphs = []
    for ch in text:
        g = None if ch == ' ' else get(f'STCFN{ord(ch):03d}')
        glyphs.append(patch_decode(g, f'STCFN{ord(ch):03d}') if g is not None else None)
    h = max((g[1] for g in glyphs if g), default=7)
    cols = []
    for g in glyphs:
        if not g:
            cols += [{} for _ in range(4)]
            continue
        cols += [{y: v for y, v in c.items() if y < h} for c in g[4]]
        cols.append({})
    return patch_encode(len(cols), h, cols)


def gamma_palettes(playpal, levels=5):
    """Wlasna krzywa (nie tablica z GBADoom): v' = 255 * (v/255) ** (1 / (1 + 0.15*k))."""
    out = []
    for k in range(1, levels + 1):
        g = 1.0 / (1.0 + 0.15 * k)
        out.append(bytes(min(255, int(round(255 * ((v / 255.0) ** g)))) for v in playpal))
    return out


# ---------------------------------------------------------------------------------------
# Pasek stanu KOSSTBAR (src/doom/st_stuff.c: kos_stbar_t)
# ---------------------------------------------------------------------------------------
ST_W, ST_H = 240, 32
PC_W = 320
# Uklad vanilla (Chocolate Doom st_stuff.c, y wzgledem gory paska ST_Y=168). Liczby: x = prawa
# krawedz, 3 cyfry w lewo z krokiem szerokosci "0" (STlib_drawNum).
PC_AMMO, PC_HEALTH, PC_ARMOR, PC_FACE = (44, 3), (90, 3), (221, 3), (143, 0)
PC_ARMS = [(111 + 12 * (i % 3), 4 + 10 * (i // 3)) for i in range(6)]
PC_KEYS = [(239, 3 + 10 * j) for j in range(3)]
PC_ARMSBG = (104, 0)
# Prawy segment paska PC (x >= 250: tabela BULL/SHEL/RCKT/CELL) nie miesci sie w 240 px -
# to jedyny segment, bez ktorego da sie grac (biezaca amunicje pokazuje duzy licznik AMMO).
PC_CROP = 250
SEAM_GAP = 3                      # wyciete kolumny co najmniej tyle od siebie: szew sie rozklada
FACES = ([f'STFST{i}{j}' for i in range(5) for j in range(3)] +
         [f'{p}{i}0' for i in range(5) for p in ('STFTR', 'STFTL')] +
         [f'{p}{i}' for i in range(5) for p in ('STFOUCH', 'STFEVL', 'STFKILL')] + ['STFGOD0', 'STFDEAD0'])
STBAR_LUMPS = (['STBAR', 'STARMS', 'STTPRCNT'] + [f'STTNUM{k}' for k in range(10)] +
               [f'STYSNUM{k}' for k in range(10)] + [f'STGNUM{k}' for k in range(2, 8)] +
               [f'STKEYS{k}' for k in range(6)] + FACES)


def draw(canvas, cw, ch, p, x, y):
    """Jak V_DrawPatch/V_DrawPatchNoScale: offsety odjete, piksele poza plotnem pominiete."""
    w, h, lo, to, cols = p
    x -= lo
    y -= to
    for cx in range(w):
        X = x + cx
        if 0 <= X < cw:
            for ry, v in cols[cx].items():
                Y = y + ry
                if 0 <= Y < ch:
                    canvas[Y * cw + X] = v


def box(p, x, y):
    w, h, lo, to = p[:4]
    return x - lo, x - lo + w, y - to, y - to + h


def widget_draws(pat, at):
    """Kazde (nazwa, x, y), w ktorym silnik moze narysowac latke paska (jak KosCheckPatch)."""
    w0 = pat['STTNUM0'][0]
    out = []
    for ax, ay in (at['ammo'], at['health'], at['armor']):
        for k in range(1, 4):
            out += [(f'STTNUM{d}', ax - k * w0, ay) for d in range(10)]
    for i, (ax, ay) in enumerate(at['arms']):
        out += [(f'STGNUM{i + 2}', ax, ay), (f'STYSNUM{i + 2}', ax, ay)]
    for j, (ax, ay) in enumerate(at['keys']):
        out += [(f'STKEYS{j}', ax, ay), (f'STKEYS{j + 3}', ax, ay)]
    out += [(f, at['face'][0], at['face'][1]) for f in FACES]
    return out


def make_stbar(get, playpal):
    """Zwraca (lump KOSSTBAR, uklad 240 px, wyciete kolumny, plotno PC 320x32, latki)."""
    missing = [n for n in STBAR_LUMPS if get(n) is None]
    if missing:
        raise WadError('brak lumpow paska stanu: ' + ' '.join(missing) +
                       ' - to nie jest pelny IWAD DOOM/Freedoom')
    pat = {n: patch_decode(get(n), n) for n in STBAR_LUMPS}
    canvas = bytearray(PC_W * ST_H)
    # Kolejnosc jak w vanilla: tlo, panel ARMS (gra jednoosobowa), znaki procentu (w vanilla
    # rysowane co klatke, ale sie nie zmieniaja - tu wrysowane raz, jak w pasku GBA upstreamu).
    draw(canvas, PC_W, ST_H, pat['STBAR'], 0, 0)
    draw(canvas, PC_W, ST_H, pat['STARMS'], *PC_ARMSBG)
    draw(canvas, PC_W, ST_H, pat['STTPRCNT'], *PC_HEALTH)
    draw(canvas, PC_W, ST_H, pat['STTPRCNT'], *PC_ARMOR)

    pc_at = {'ammo': PC_AMMO, 'health': PC_HEALTH, 'armor': PC_ARMOR, 'face': PC_FACE,
             'arms': PC_ARMS, 'keys': PC_KEYS}
    w0 = pat['STTNUM0'][0]
    if not 0 < w0 <= 20:
        raise WadError(f'STTNUM0 ma {w0} px szerokosci - trzy cyfry nie zmieszcza sie w polu paska')
    protect = set()
    draws = widget_draws(pat, pc_at) + [('STTPRCNT', *PC_HEALTH), ('STTPRCNT', *PC_ARMOR)]
    for name, x, y in draws:
        x0, x1, y0, y1 = box(pat[name], x, y)
        if x0 < 0 or x1 > PC_CROP or y0 < 0 or y1 > ST_H:
            raise WadError(f'latka {name} wychodzi poza pasek ({x0}..{x1} x {y0}..{y1}) - '
                           'uklad paska tego WAD-u nie pasuje do K-OS')
        protect.update(range(x0, x1))
    removed = choose_seams(canvas, playpal, protect, PC_CROP - ST_W)
    keep = [x for x in range(PC_CROP) if x not in removed]
    img = bytearray(ST_W * ST_H)
    for y in range(ST_H):
        row = y * PC_W
        img[y * ST_W:(y + 1) * ST_W] = bytes(canvas[row + x] for x in keep)

    def remap(x):
        return x - sum(1 for r in removed if r < x)
    at = {k: (remap(v[0]), v[1]) for k, v in pc_at.items() if k not in ('arms', 'keys')}
    at['arms'] = [(remap(x), y) for x, y in PC_ARMS]
    at['keys'] = [(remap(x), y) for x, y in PC_KEYS]
    # Te same warunki, co silnik sprawdzi przy starcie (st_stuff.c, KosCheckPatch).
    for name, x, y in widget_draws(pat, at):
        patch_check(get(name), name, full=True)
        x0, x1, y0, y1 = box(pat[name], x, y)
        if x0 < 0 or x1 > ST_W or y0 < 0 or y1 > ST_H:
            raise WadError(f'blad ukladu paska: {name} w ({x},{y}) wychodzi poza 240x32')
    vals = [*at['ammo'], *at['health'], *at['armor'], *at['face']]
    for x, y in at['arms'] + at['keys']:
        vals += [x, y]
    hdr = STBAR_MAGIC + struct.pack('<hh', ST_W, ST_H) + struct.pack(f'<{len(vals)}h', *vals)
    assert len(hdr) == 64
    return hdr + bytes(img), at, removed, canvas, pat


def choose_seams(canvas, playpal, protect, count):
    """Kolumny tla do wyciecia (320 -> 240 px po odcieciu tabeli amunicji). Grafika idzie 1:1,
    wiec zamiast skalowania (ktore zjada kreski cyfr i liter) wycinamy pojedyncze kolumny tam,
    gdzie sa najbardziej podobne do obu sasiadow - na plaskim tle szwu nie widac. Kolumny pod
    widzetami sa chronione, zeby cyfry, twarz i klucze mialy pod soba to samo tlo co w oryginale."""
    def rgb(x, y):
        v = canvas[y * PC_W + x] * 3
        return playpal[v], playpal[v + 1], playpal[v + 2]
    colrgb = [[rgb(x, y) for y in range(ST_H)] for x in range(PC_CROP)]

    def dist(a, b):
        return sum(abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2])
                   for p, q in zip(colrgb[a], colrgb[b]))
    seq = list(range(PC_CROP))
    removed = []
    gap = SEAM_GAP
    while len(removed) < count:
        best = None
        for i in range(1, len(seq) - 1):          # skrajnych kolumn nie ruszamy
            x = seq[i]
            if x in protect or any(abs(x - r) < gap for r in removed):
                continue
            cost = dist(seq[i - 1], x) + dist(x, seq[i + 1])
            if best is None or cost < best[0]:
                best = (cost, i)
        if best is None:
            if gap > 1:
                gap -= 1                          # ciasny pasek: dopuszczamy blizsze szwy
                continue
            raise WadError('pasek stanu tego WAD-u nie da sie zwezic do 240 px bez ciecia widzetow')
        removed.append(seq.pop(best[1]))
    return sorted(removed)


def png_bytes(w, h, rgb):
    raw = b''.join(b'\0' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def stbar_preview(path, lump, at, canvas, pat, playpal, scale=3):
    """Obraz do obejrzenia na komputerze: u gory pasek PC 320 px, pod nim pasek K-OS 240 px,
    oba z przykladowymi widzetami (AMMO 50, 100%, pancerz 45%, twarz, bronie 2-3, dwa klucze)."""
    def with_widgets(img, cw, a):
        img = bytearray(img)
        w0 = pat['STTNUM0'][0]
        for (ax, ay), val in ((a['ammo'], 50), (a['health'], 100), (a['armor'], 45)):
            x = ax
            while val:
                x -= w0
                draw(img, cw, ST_H, pat[f'STTNUM{val % 10}'], x, ay)
                val //= 10
        for i, (ax, ay) in enumerate(a['arms']):
            draw(img, cw, ST_H, pat[f'STYSNUM{i + 2}' if i < 2 else f'STGNUM{i + 2}'], ax, ay)
        draw(img, cw, ST_H, pat['STKEYS0'], *a['keys'][0])
        draw(img, cw, ST_H, pat['STKEYS5'], *a['keys'][2])
        draw(img, cw, ST_H, pat['STFST01'], *a['face'])
        return img
    pc_at = {'ammo': PC_AMMO, 'health': PC_HEALTH, 'armor': PC_ARMOR, 'face': PC_FACE,
             'arms': PC_ARMS, 'keys': PC_KEYS}
    top = with_widgets(canvas, PC_W, pc_at)
    bot = with_widgets(lump[64:], ST_W, at)
    W, H, gap = PC_W, 2 * ST_H + 8, 8
    rgb = bytearray(W * scale * H * scale * 3)
    for y in range(H):
        for x in range(W):
            if y < ST_H:
                v = top[y * PC_W + x]
            elif ST_H + gap <= y and x < ST_W:
                v = bot[(y - ST_H - gap) * ST_W + x]
            else:
                continue
            c = bytes(playpal[v * 3:v * 3 + 3])
            for sy in range(scale):
                o = ((y * scale + sy) * W * scale + x * scale) * 3
                rgb[o:o + 3 * scale] = c * scale
    try:
        with open(path, 'wb') as f:
            f.write(png_bytes(W * scale, H * scale, rgb))
    except OSError as e:
        raise WadError(f'{path}: nie moge zapisac podgladu ({e.strerror})')


# ---------------------------------------------------------------------------------------
# GbaWadUtil - wierne przepisanie wadprocessor.cpp (doomhack/GbaWadUtil, commit 4ea9f48)
# ---------------------------------------------------------------------------------------
def fixed_div(a, b):
    if (abs(a) >> 14) >= abs(b):
        return (-1 if (a ^ b) < 0 else 0) ^ 0x7FFFFFFF
    q = abs(a << 16) // abs(b)
    return s32(-q if (a < 0) != (b < 0) else q)


def texture_dir(lumps, idx):
    """Nazwy tekstur w kolejnosci numerow (TEXTURE1, potem TEXTURE2) + indeksy ich latek."""
    names, patches = [], []
    for tl in ('TEXTURE1', 'TEXTURE2'):
        if tl not in idx:
            continue
        d = lumps[idx[tl]].data
        n = len(d)
        if n < 4:
            raise WadError(f'{tl}: za krotki')
        cnt = struct.unpack_from('<i', d, 0)[0]
        if cnt < 0 or 4 + 4 * cnt > n:
            raise WadError(f'{tl}: zla liczba tekstur ({cnt})')
        for k in range(cnt):
            off = struct.unpack_from('<i', d, 4 + 4 * k)[0]
            if off < 0 or off + 22 > n:
                raise WadError(f'{tl}: tekstura nr {k} poza lumpem')
            pc = struct.unpack_from('<h', d, off + 20)[0]
            if pc < 0 or off + 22 + 10 * pc > n:
                raise WadError(f'{tl}: tekstura nr {k} ma zla liczbe latek ({pc})')
            names.append(name8(d[off:off + 8]))
            patches.append([struct.unpack_from('<h', d, off + 22 + 10 * j + 4)[0] for j in range(pc)])
    return names, patches


def read_pnames(lumps, idx):
    d = lumps[idx['PNAMES']].data
    if len(d) < 4:
        raise WadError('PNAMES: za krotki')
    n = struct.unpack_from('<I', d, 0)[0]
    if 4 + 8 * n > len(d):
        raise WadError(f'PNAMES: {n} nazw nie miesci sie w {len(d)} B')
    return [name8(d[4 + 8 * k:12 + 8 * k]) for k in range(n)]


class MapInfo:
    __slots__ = ('name', 'conv', 'used_tex', 'used_flat', 'used_ednum', 'secret', 'problem', 'ram', 'size')


def analyze_map(lumps, mi, texnum, flatset):
    """Sprawdzenie i przerobka jednej mapy. Blad = mapa odpada z planu (problem), nie caly WAD:
    silnik wierzy indeksom mapy na slowo, wiec zly indeks to odczyt obok danych na plytce."""
    m = MapInfo()
    m.name = lumps[mi].name
    m.problem = None
    m.conv, m.used_tex, m.used_flat, m.used_ednum = [], set(), set(), set()
    m.secret, m.ram, m.size = False, 0, 0

    def bad(msg):
        m.problem = msg
        return m
    kids = [l.name for l in lumps[mi + 1:mi + 11]]
    if kids != MAP_CHILD:
        return bad('niepelna mapa albo format inny niz DOOM (lumpy: ' + ' '.join(kids) + ')')
    d = {c: lumps[mi + 1 + k].data for k, c in enumerate(MAP_CHILD)}
    for c, r in REC.items():
        if len(d[c]) % r:
            return bad(f'{c}: {len(d[c])} B to nie wielokrotnosc {r} B')
    verts = list(struct.iter_unpack('<hh', d['VERTEXES']))
    lines = list(struct.iter_unpack('<HHHhhHH', d['LINEDEFS']))
    sides = list(struct.iter_unpack('<hh8s8s8sh', d['SIDEDEFS']))
    segs = list(struct.iter_unpack('<HHhHhh', d['SEGS']))
    ssec = list(struct.iter_unpack('<HH', d['SSECTORS']))
    nodes = list(struct.iter_unpack('<14h', d['NODES']))
    secs = list(struct.iter_unpack('<hh8s8shhh', d['SECTORS']))
    things = list(struct.iter_unpack('<hhhhh', d['THINGS']))
    nv, nl, ns, nsg, nss, nn, nsec = len(verts), len(lines), len(sides), len(segs), len(ssec), len(nodes), len(secs)
    if not (nv and nl and ns and nsg and nss and nsec and things):
        return bad('pusta mapa (brak linii, segmentow albo sektorow)')
    for i, (v1, v2, flags, special, tag, s0, s1) in enumerate(lines):
        if v1 >= nv or v2 >= nv:
            return bad(f'linia {i} wskazuje nieistniejacy wierzcholek')
        if s0 >= ns or (s1 != NO_INDEX and s1 >= ns):
            return bad(f'linia {i} wskazuje nieistniejacy sidedef')
        if special in SECRET_EXITS:
            m.secret = True
    for i, s in enumerate(sides):
        if not 0 <= s[5] < nsec:
            return bad(f'sidedef {i} wskazuje nieistniejacy sektor')
    for i, (v1, v2, ang, ld, side, off) in enumerate(segs):
        if v1 >= nv or v2 >= nv or ld >= nl or side not in (0, 1) or lines[ld][5 + side] == NO_INDEX:
            return bad(f'segment {i} wskazuje nieistniejacy wierzcholek, linie albo strone linii')
    for i, (cnt, first) in enumerate(ssec):
        if not cnt or first + cnt > nsg:
            return bad(f'podsektor {i} wskazuje segmenty poza lista')
    if not nn and nss != 1:
        return bad('brak wezlow BSP')
    for i, nd in enumerate(nodes):
        for ch in (nd[12] & 0xFFFF, nd[13] & 0xFFFF):
            if (ch & 0x8000 and (ch & 0x7FFF) >= nss) or (not ch & 0x8000 and ch >= nn):
                return bad(f'wezel {i} wskazuje nieistniejace dziecko')
    if nn:   # cykl w drzewie BSP = nieskonczona rekurencja silnika
        state = [0] * nn
        stack = [(nn - 1, False)]
        while stack:
            k, done = stack.pop()
            if done:
                state[k] = 2
                continue
            if state[k] == 1:
                return bad('cykl w drzewie wezlow BSP')
            if state[k] == 2:
                continue
            state[k] = 1
            stack.append((k, True))
            for ch in (nodes[k][12] & 0xFFFF, nodes[k][13] & 0xFFFF):
                if not ch & 0x8000:
                    if state[ch] == 1:
                        return bad('cykl w drzewie wezlow BSP')
                    stack.append((ch, False))
    for i, sec in enumerate(secs):
        for f in (name8(sec[2]), name8(sec[3])):
            if f not in flatset:
                return bad(f'sektor {i} uzywa plaskiej tekstury {f}, ktorej nie ma w WAD-zie')
            m.used_flat.add(f)
    bm = d['BLOCKMAP']
    nw = len(bm) // 2
    if len(bm) < 8 or len(bm) % 2:
        return bad('zly BLOCKMAP')
    cols, rows = struct.unpack_from('<hh', bm, 4)
    if cols <= 0 or rows <= 0 or 4 + cols * rows > nw:
        return bad('zly naglowek BLOCKMAP')
    words = struct.unpack_from(f'<{nw}H', bm, 0)
    # Slowa, od ktorych juz wiadomo, ze lista dochodzi do 0xFFFF: listy komorek moga zaczynac sie
    # w srodku innych list, a bez tego zly BLOCKMAP kosztowalby kwadrat swojej dlugosci.
    good = set()
    for c in range(cols * rows):
        off = words[4 + c]
        # blockmap[] to w silniku "const short" - przesuniecie >= 32768 staje sie ujemne
        if off >= 32768 or off >= nw:
            return bad('BLOCKMAP za duzy dla silnika (przesuniecie > 32767 slow)')
        j = off + 1           # P_BlockLinesIterator pomija pierwsze slowo (0)
        walk = []
        while j not in good:
            if j >= nw:
                return bad('lista w BLOCKMAP bez konca')
            if words[j] == 0xFFFF:
                break
            if words[j] >= nl:
                return bad('BLOCKMAP wskazuje nieistniejaca linie')
            walk.append(j)
            j += 1
        good.update(walk)
        good.add(j)
    if not any(t[3] == 1 for t in things):
        return bad('brak startu gracza 1')
    for t in things:
        if t[4] & MTF_SKILLS and not t[4] & MTF_NOTSINGLE:
            m.used_ednum.add(t[3])      # tylko to, co silnik postawi w grze jednoosobowej
    for s in sides:
        for t in s[2:5]:
            n = name8(t)
            if n and n != '-':
                m.used_tex.add(n)

    # --- przerobka jak GbaWadUtil ---
    vx = [(x << 16, y << 16) for x, y in verts]
    V = b''.join(struct.pack('<ii', s32(x), s32(y)) for x, y in vx)
    out = bytearray()
    for i, (v1, v2, flags, special, tag, sn0, sn1) in enumerate(lines):
        x1, y1 = vx[v1]
        x2, y2 = vx[v2]
        dx, dy = s32(x2 - x1), s32(y2 - y1)
        slope = 1 if not dx else 0 if not dy else (2 if fixed_div(dy, dx) > 0 else 3)
        bbox = (max(y1, y2), min(y1, y2), min(x1, x2), max(x1, x2))   # TOP, BOTTOM, LEFT, RIGHT
        out += struct.pack('<iiiiIiiHHiiiiHhhH', x1, y1, x2, y2, i, dx, dy, sn0, sn1,
                           bbox[0], bbox[1], bbox[2], bbox[3], flags, special, tag, slope)
    L = bytes(out)
    out = bytearray()   # segi uzywaja ORYGINALNYCH sidedefow (sektory) - przerabiane dopiero nizej
    for v1, v2, angle, linedef, side, offset in segs:
        x1, y1 = vx[v1]
        x2, y2 = vx[v2]
        flags, sn = lines[linedef][2], (lines[linedef][5], lines[linedef][6])
        sidenum = sn[side]
        front = sides[sidenum][5] & 0xFFFF
        back = NO_INDEX
        if flags & ML_TWOSIDED and sn[side ^ 1] != NO_INDEX:
            back = sides[sn[side ^ 1]][5] & 0xFFFF
        out += struct.pack('<iiiiiIHHHH', x1, y1, x2, y2, s32(offset << 16), (angle << 16) & 0xFFFFFFFF,
                           sidenum, linedef, front, back)
    G = bytes(out)
    S = b''.join(struct.pack('<hhhhhh', toff, roff, texnum.get(name8(top), 0), texnum.get(name8(bot), 0),
                             texnum.get(name8(mid), 0), sector)
                 for toff, roff, top, bot, mid, sector in sides)
    rej = d['REJECT']
    need = (nsec * nsec + 7) // 8
    if len(rej) < need:   # silnik czyta REJECT bez granic - krotszy dopelniamy "widac wszystko"
        rej = bytes(rej) + b'\0' * (need - len(rej))
    conv = {'VERTEXES': V, 'LINEDEFS': L, 'SEGS': G, 'SIDEDEFS': S, 'REJECT': rej}
    m.conv = [Lump(m.name, b'')] + [Lump(c, conv[c] if c in conv else d[c]) for c in MAP_CHILD]
    m.size = sum(len(l.data) for l in m.conv)
    # Szacunek strefy silnika na poziom (rozmiary struktur z kompilatora Xtensa: sector_t 60 B,
    # side_t 12, subsector_t 8, linedata_t 8, mobj_t 124, blocklinks 4 B na komorke).
    n2s = sum(1 for ln in lines if ln[6] != NO_INDEX)
    m.ram = 60 * nsec + 12 * ns + 8 * nss + 8 * nl + 124 * len(things) + 4 * (nl + n2s) + 4 * cols * rows
    return m


# ---------------------------------------------------------------------------------------
# Tabele silnika. Zapisane w skrypcie, zeby dzialal jako jeden plik bez zrodel silnika;
# tests/test_wad2kos.py pilnuje, ze sa takie same jak policzone z src/doom (engine_tables_from_src).
# ---------------------------------------------------------------------------------------
def engine_tables_from_src():
    info = open(os.path.join(ENGINE_SRC, 'info.c'), encoding='latin-1').read()
    states = {}
    body = info[info.index('const state_t states'):info.index('const mobjinfo_t')]
    for mt in re.finditer(r'\{\s*SPR_(\w+)\s*,[^,]*,[^,]*,[^,]*,\s*(S_\w+)\s*,[^}]*\}\s*,?\s*//\s*(S_\w+)', body):
        states[mt.group(3)] = (mt.group(1), mt.group(2))
    mobj = {}
    body = info[info.index('const mobjinfo_t'):]
    for mt in re.finditer(r'//\s*(MT_\w+)\s*\n(.*?)\n\s*\}', body, re.S):
        fields = [f.split('//')[0].strip().rstrip(',').strip() for f in mt.group(2).split('\n')]
        fields = [f for f in fields if f]
        try:
            ednum = int(fields[0])
        except ValueError:
            continue
        sprs = set()
        for st in [f for f in fields if f.startswith('S_')]:
            seen = set()
            while st and st != 'S_NULL' and st not in seen and st in states:
                seen.add(st)
                spr, st = states[st]
                sprs.add(spr)
        mobj[mt.group(1)] = (ednum, sorted(sprs))
    spec = open(os.path.join(ENGINE_SRC, 'p_spec.c'), encoding='latin-1').read()
    anims = [(t == 'true', e, s) for t, e, s in re.findall(r'\{\s*(true|false)\s*,\s*"(\w+)"\s*,\s*"(\w+)"', spec)]
    sw = open(os.path.join(ENGINE_SRC, 'p_switch.c'), encoding='latin-1').read()
    switches = re.findall(r'\{\s*"(\w+)"\s*,\s*"(\w+)"\s*,\s*[1-3]\s*\}', sw)
    lit, pats = set(), set()
    for fn in sorted(os.listdir(ENGINE_SRC)):
        if not fn.endswith('.c'):
            continue
        src = re.sub(r'//[^\n]*', '', open(os.path.join(ENGINE_SRC, fn), encoding='latin-1').read())
        for mt in re.finditer(r'"([A-Z][A-Z0-9_]{1,7}(?:%[^"]*)?)"', src):
            t = mt.group(1)
            if '%' in t:
                rx = re.sub(r'%(?:0?2d|2\.2d)', r'\\d\\d', t)
                rx = re.sub(r'%\.3d', r'\\d\\d\\d', rx)
                rx = re.sub(r'%[di]', r'\\d', rx)
                if '%' not in rx:
                    pats.add(rx)
            else:
                lit.add(t)
    return {'mobj': dict(sorted(mobj.items())), 'anims': anims, 'switches': [list(s) for s in switches],
            'lumps': sorted(lit), 'patterns': sorted(pats)}


def tables_literal(t):
    """Deterministyczny zapis tabel do wklejenia miedzy znaczniki TABELE SILNIKA."""
    lines = ['ENGINE = {', "    'mobj': {"]
    for k, (ed, sp) in t['mobj'].items():
        lines.append(f"        {k!r}: ({ed}, {sp!r}),")
    lines.append('    },')
    lines.append("    'anims': [")
    lines += [f'        {a!r},' for a in t['anims']]
    lines.append('    ],')
    lines.append("    'switches': [")
    for k in range(0, len(t['switches']), 3):
        lines.append('        ' + ' '.join(f'{s!r},' for s in t['switches'][k:k + 3]))
    lines.append('    ],')
    lines.append("    'lumps': [")
    for k in range(0, len(t['lumps']), 8):
        lines.append('        ' + ' '.join(f'{s!r},' for s in t['lumps'][k:k + 8]))
    lines.append('    ],')
    lines.append(f"    'patterns': {t['patterns']!r},")
    lines.append('}')
    return '\n'.join(lines) + '\n'


# --- TABELE SILNIKA (python3 kos/wad2kos.py --tabele; test pilnuje zgodnosci ze zrodlami) ---
ENGINE = {
    'mobj': {
        'MT_ARACHPLAZ': (-1, ['APBX', 'APLS']),
        'MT_BABY': (68, ['BSPI']),
        'MT_BARREL': (2035, ['BAR1', 'BEXP']),
        'MT_BFG': (-1, ['BFE1', 'BFS1']),
        'MT_BLOOD': (-1, ['BLUD']),
        'MT_BOSSBRAIN': (88, ['BBRN']),
        'MT_BOSSSPIT': (89, ['SSWV']),
        'MT_BOSSTARGET': (87, []),
        'MT_BRUISER': (3003, ['BOSS']),
        'MT_BRUISERSHOT': (-1, ['BAL7']),
        'MT_CHAINGUN': (2002, ['MGUN']),
        'MT_CHAINGUY': (65, ['CPOS']),
        'MT_CLIP': (2007, ['CLIP']),
        'MT_CYBORG': (16, ['CYBR']),
        'MT_EXTRABFG': (-1, ['BFE2']),
        'MT_FATSHOT': (-1, ['MANF', 'MISL']),
        'MT_FATSO': (67, ['FATT']),
        'MT_FIRE': (-1, ['FIRE']),
        'MT_HEAD': (3005, ['HEAD']),
        'MT_HEADSHOT': (-1, ['BAL2']),
        'MT_IFOG': (-1, ['IFOG']),
        'MT_INS': (2024, ['PINS']),
        'MT_INV': (2022, ['PINV']),
        'MT_KEEN': (72, ['KEEN']),
        'MT_KNIGHT': (69, ['BOS2']),
        'MT_MEGA': (83, ['MEGA']),
        'MT_MISC0': (2018, ['ARM1']),
        'MT_MISC1': (2019, ['ARM2']),
        'MT_MISC10': (2011, ['STIM']),
        'MT_MISC11': (2012, ['MEDI']),
        'MT_MISC12': (2013, ['SOUL']),
        'MT_MISC13': (2023, ['PSTR']),
        'MT_MISC14': (2025, ['SUIT']),
        'MT_MISC15': (2026, ['PMAP']),
        'MT_MISC16': (2045, ['PVIS']),
        'MT_MISC17': (2048, ['AMMO']),
        'MT_MISC18': (2010, ['ROCK']),
        'MT_MISC19': (2046, ['BROK']),
        'MT_MISC2': (2014, ['BON1']),
        'MT_MISC20': (2047, ['CELL']),
        'MT_MISC21': (17, ['CELP']),
        'MT_MISC22': (2008, ['SHEL']),
        'MT_MISC23': (2049, ['SBOX']),
        'MT_MISC24': (8, ['BPAK']),
        'MT_MISC25': (2006, ['BFUG']),
        'MT_MISC26': (2005, ['CSAW']),
        'MT_MISC27': (2003, ['LAUN']),
        'MT_MISC28': (2004, ['PLAS']),
        'MT_MISC29': (85, ['TLMP']),
        'MT_MISC3': (2015, ['BON2']),
        'MT_MISC30': (86, ['TLP2']),
        'MT_MISC31': (2028, ['COLU']),
        'MT_MISC32': (30, ['COL1']),
        'MT_MISC33': (31, ['COL2']),
        'MT_MISC34': (32, ['COL3']),
        'MT_MISC35': (33, ['COL4']),
        'MT_MISC36': (37, ['COL6']),
        'MT_MISC37': (36, ['COL5']),
        'MT_MISC38': (41, ['CEYE']),
        'MT_MISC39': (42, ['FSKU']),
        'MT_MISC4': (5, ['BKEY']),
        'MT_MISC40': (43, ['TRE1']),
        'MT_MISC41': (44, ['TBLU']),
        'MT_MISC42': (45, ['TGRN']),
        'MT_MISC43': (46, ['TRED']),
        'MT_MISC44': (55, ['SMBT']),
        'MT_MISC45': (56, ['SMGT']),
        'MT_MISC46': (57, ['SMRT']),
        'MT_MISC47': (47, ['SMIT']),
        'MT_MISC48': (48, ['ELEC']),
        'MT_MISC49': (34, ['CAND']),
        'MT_MISC5': (13, ['RKEY']),
        'MT_MISC50': (35, ['CBRA']),
        'MT_MISC51': (49, ['GOR1']),
        'MT_MISC52': (50, ['GOR2']),
        'MT_MISC53': (51, ['GOR3']),
        'MT_MISC54': (52, ['GOR4']),
        'MT_MISC55': (53, ['GOR5']),
        'MT_MISC56': (59, ['GOR2']),
        'MT_MISC57': (60, ['GOR4']),
        'MT_MISC58': (61, ['GOR3']),
        'MT_MISC59': (62, ['GOR5']),
        'MT_MISC6': (6, ['YKEY']),
        'MT_MISC60': (63, ['GOR1']),
        'MT_MISC61': (22, ['HEAD']),
        'MT_MISC62': (15, ['PLAY']),
        'MT_MISC63': (18, ['POSS']),
        'MT_MISC64': (21, ['SARG']),
        'MT_MISC65': (23, ['SKUL']),
        'MT_MISC66': (20, ['TROO']),
        'MT_MISC67': (19, ['SPOS']),
        'MT_MISC68': (10, ['PLAY']),
        'MT_MISC69': (12, ['PLAY']),
        'MT_MISC7': (39, ['YSKU']),
        'MT_MISC70': (28, ['POL2']),
        'MT_MISC71': (24, ['POL5']),
        'MT_MISC72': (27, ['POL4']),
        'MT_MISC73': (29, ['POL3']),
        'MT_MISC74': (25, ['POL1']),
        'MT_MISC75': (26, ['POL6']),
        'MT_MISC76': (54, ['TRE2']),
        'MT_MISC77': (70, ['FCAN']),
        'MT_MISC78': (73, ['HDB1']),
        'MT_MISC79': (74, ['HDB2']),
        'MT_MISC8': (38, ['RSKU']),
        'MT_MISC80': (75, ['HDB3']),
        'MT_MISC81': (76, ['HDB4']),
        'MT_MISC82': (77, ['HDB5']),
        'MT_MISC83': (78, ['HDB6']),
        'MT_MISC84': (79, ['POB1']),
        'MT_MISC85': (80, ['POB2']),
        'MT_MISC86': (81, ['BRS1']),
        'MT_MISC9': (40, ['BSKU']),
        'MT_PAIN': (71, ['PAIN']),
        'MT_PLASMA': (-1, ['PLSE', 'PLSS']),
        'MT_PLAYER': (-1, ['PLAY']),
        'MT_POSSESSED': (3004, ['POSS']),
        'MT_PUFF': (-1, ['PUFF']),
        'MT_ROCKET': (-1, ['MISL']),
        'MT_SERGEANT': (3002, ['SARG']),
        'MT_SHADOWS': (58, ['SARG']),
        'MT_SHOTGUN': (2001, ['SHOT']),
        'MT_SHOTGUY': (9, ['SPOS']),
        'MT_SKULL': (3006, ['SKUL']),
        'MT_SMOKE': (-1, ['PUFF']),
        'MT_SPAWNFIRE': (-1, ['FIRE']),
        'MT_SPAWNSHOT': (-1, ['BOSF']),
        'MT_SPIDER': (7, ['SPID']),
        'MT_SUPERSHOTGUN': (82, ['SGN2']),
        'MT_TELEPORTMAN': (14, []),
        'MT_TFOG': (-1, ['TFOG']),
        'MT_TRACER': (-1, ['FATB', 'FBXP']),
        'MT_TROOP': (3001, ['TROO']),
        'MT_TROOPSHOT': (-1, ['BAL1']),
        'MT_UNDEAD': (66, ['SKEL']),
        'MT_VILE': (64, ['VILE']),
        'MT_WOLFSS': (84, ['SSWV']),
    },
    'anims': [
        (False, 'NUKAGE3', 'NUKAGE1'),
        (False, 'FWATER4', 'FWATER1'),
        (False, 'SWATER4', 'SWATER1'),
        (False, 'LAVA4', 'LAVA1'),
        (False, 'BLOOD3', 'BLOOD1'),
        (False, 'RROCK08', 'RROCK05'),
        (False, 'SLIME04', 'SLIME01'),
        (False, 'SLIME08', 'SLIME05'),
        (False, 'SLIME12', 'SLIME09'),
        (True, 'BLODGR4', 'BLODGR1'),
        (True, 'SLADRIP3', 'SLADRIP1'),
        (True, 'BLODRIP4', 'BLODRIP1'),
        (True, 'FIREWALL', 'FIREWALA'),
        (True, 'GSTFONT3', 'GSTFONT1'),
        (True, 'FIRELAVA', 'FIRELAV3'),
        (True, 'FIREMAG3', 'FIREMAG1'),
        (True, 'FIREBLU2', 'FIREBLU1'),
        (True, 'ROCKRED3', 'ROCKRED1'),
        (True, 'BFALL4', 'BFALL1'),
        (True, 'SFALL4', 'SFALL1'),
        (True, 'WFALL4', 'WFALL1'),
        (True, 'DBRAIN4', 'DBRAIN1'),
    ],
    'switches': [
        ['SW1BRCOM', 'SW2BRCOM'], ['SW1BRN1', 'SW2BRN1'], ['SW1BRN2', 'SW2BRN2'],
        ['SW1BRNGN', 'SW2BRNGN'], ['SW1BROWN', 'SW2BROWN'], ['SW1COMM', 'SW2COMM'],
        ['SW1COMP', 'SW2COMP'], ['SW1DIRT', 'SW2DIRT'], ['SW1EXIT', 'SW2EXIT'],
        ['SW1GRAY', 'SW2GRAY'], ['SW1GRAY1', 'SW2GRAY1'], ['SW1METAL', 'SW2METAL'],
        ['SW1PIPE', 'SW2PIPE'], ['SW1SLAD', 'SW2SLAD'], ['SW1STARG', 'SW2STARG'],
        ['SW1STON1', 'SW2STON1'], ['SW1STON2', 'SW2STON2'], ['SW1STONE', 'SW2STONE'],
        ['SW1STRTN', 'SW2STRTN'], ['SW1BLUE', 'SW2BLUE'], ['SW1CMT', 'SW2CMT'],
        ['SW1GARG', 'SW2GARG'], ['SW1GSTON', 'SW2GSTON'], ['SW1HOT', 'SW2HOT'],
        ['SW1LION', 'SW2LION'], ['SW1SATYR', 'SW2SATYR'], ['SW1SKIN', 'SW2SKIN'],
        ['SW1VINE', 'SW2VINE'], ['SW1WOOD', 'SW2WOOD'], ['SW1PANEL', 'SW2PANEL'],
        ['SW1ROCK', 'SW2ROCK'], ['SW1MET2', 'SW2MET2'], ['SW1WDMET', 'SW2WDMET'],
        ['SW1BRIK', 'SW2BRIK'], ['SW1MOD1', 'SW2MOD1'], ['SW1ZIM', 'SW2ZIM'],
        ['SW1STON6', 'SW2STON6'], ['SW1TEK', 'SW2TEK'], ['SW1MARB', 'SW2MARB'],
        ['SW1SKULL', 'SW2SKULL'],
    ],
    'lumps': [
        'AMMO', 'APBX', 'APLS', 'ARM1', 'ARM2', 'BAL1', 'BAL2', 'BAL7',
        'BAR1', 'BBRN', 'BEXP', 'BFALL1', 'BFALL4', 'BFE1', 'BFE2', 'BFGF',
        'BFGG', 'BFS1', 'BFUG', 'BKEY', 'BLODGR1', 'BLODGR4', 'BLODRIP1', 'BLODRIP4',
        'BLOOD1', 'BLOOD3', 'BLUD', 'BON1', 'BON2', 'BOS2', 'BOSF', 'BOSS',
        'BOSSBACK', 'BPAK', 'BROK', 'BRS1', 'BSKU', 'BSPI', 'CAND', 'CBRA',
        'CELL', 'CELP', 'CEYE', 'CHGF', 'CHGG', 'CLIP', 'COL1', 'COL2',
        'COL3', 'COL4', 'COL5', 'COL6', 'COLORMAP', 'COLU', 'CPOS', 'CREDIT',
        'CSAW', 'CYBR', 'DBRAIN1', 'DBRAIN4', 'E1M1', 'ELEC', 'EMPTY', 'END0',
        'ENDPIC', 'FATB', 'FATT', 'FBXP', 'FCAN', 'FIRE', 'FIREBLU1', 'FIREBLU2',
        'FIRELAV3', 'FIRELAVA', 'FIREMAG1', 'FIREMAG3', 'FIREWALA', 'FIREWALL', 'FLOOR4_8', 'FSKU',
        'FWATER1', 'FWATER4', 'F_END', 'F_SKY1', 'F_START', 'GOR1', 'GOR2', 'GOR3',
        'GOR4', 'GOR5', 'GSTFONT1', 'GSTFONT3', 'HDB1', 'HDB2', 'HDB3', 'HDB4',
        'HDB5', 'HDB6', 'HEAD', 'HELP2', 'IFOG', 'INTERPIC', 'IWAD', 'KEEN',
        'KOSSTBAR', 'KOSSTBR1', 'LAUN', 'LAVA1', 'LAVA4', 'MANF', 'MEDI', 'MEGA',
        'MFLR8_3', 'MFLR8_4', 'MGUN', 'MISF', 'MISG', 'MISL', 'M_ARUN', 'M_DETAIL',
        'M_DOOM', 'M_ENDGAM', 'M_EPI1', 'M_EPI2', 'M_EPI3', 'M_EPI4', 'M_EPISOD', 'M_GAMMA',
        'M_GDHIGH', 'M_GDLOW', 'M_HURT', 'M_JKILL', 'M_LOADG', 'M_LSCNTR', 'M_LSLEFT', 'M_LSRGHT',
        'M_MESSG', 'M_MSGOFF', 'M_MSGON', 'M_MUSVOL', 'M_NEWG', 'M_NGAME', 'M_NMARE', 'M_OPTION',
        'M_OPTTTL', 'M_ROUGH', 'M_SAVEG', 'M_SFXVOL', 'M_SKILL', 'M_SKULL1', 'M_SKULL2', 'M_SVOL',
        'M_THERML', 'M_THERMM', 'M_THERMO', 'M_THERMR', 'M_ULTRA', 'NUKAGE1', 'NUKAGE3', 'PAIN',
        'PFUB1', 'PFUB2', 'PINS', 'PINV', 'PISF', 'PISG', 'PLAS', 'PLAY',
        'PLAYPAL', 'PLAYPAL0', 'PLSE', 'PLSF', 'PLSG', 'PLSS', 'PMAP', 'PNAMES',
        'POB1', 'POB2', 'POL1', 'POL2', 'POL3', 'POL4', 'POL5', 'POL6',
        'POSS', 'PSTR', 'PUFF', 'PUNG', 'PVIS', 'RKEY', 'ROCK', 'ROCKRED1',
        'ROCKRED3', 'RROCK05', 'RROCK07', 'RROCK08', 'RROCK13', 'RROCK14', 'RROCK17', 'RROCK19',
        'RSKU', 'SARG', 'SAWG', 'SBOX', 'SFALL1', 'SFALL4', 'SFLR6_1', 'SGN2',
        'SHEL', 'SHOT', 'SHT2', 'SHTF', 'SHTG', 'SKEL', 'SKUL', 'SKY1',
        'SKY2', 'SKY3', 'SKY4', 'SLADRIP1', 'SLADRIP3', 'SLIME01', 'SLIME04', 'SLIME05',
        'SLIME08', 'SLIME09', 'SLIME12', 'SLIME16', 'SMBT', 'SMGT', 'SMIT', 'SMRT',
        'SMT2', 'SOUL', 'SPID', 'SPOS', 'SSWV', 'STFDEAD0', 'STFGOD0', 'STIM',
        'STTNUM0', 'STTPRCNT', 'SUIT', 'SW1BLUE', 'SW1BRCOM', 'SW1BRIK', 'SW1BRN1', 'SW1BRN2',
        'SW1BRNGN', 'SW1BROWN', 'SW1CMT', 'SW1COMM', 'SW1COMP', 'SW1DIRT', 'SW1EXIT', 'SW1GARG',
        'SW1GRAY', 'SW1GRAY1', 'SW1GSTON', 'SW1HOT', 'SW1LION', 'SW1MARB', 'SW1MET2', 'SW1METAL',
        'SW1MOD1', 'SW1PANEL', 'SW1PIPE', 'SW1ROCK', 'SW1SATYR', 'SW1SKIN', 'SW1SKULL', 'SW1SLAD',
        'SW1STARG', 'SW1STON1', 'SW1STON2', 'SW1STON6', 'SW1STONE', 'SW1STRTN', 'SW1TEK', 'SW1VINE',
        'SW1WDMET', 'SW1WOOD', 'SW1ZIM', 'SW2BLUE', 'SW2BRCOM', 'SW2BRIK', 'SW2BRN1', 'SW2BRN2',
        'SW2BRNGN', 'SW2BROWN', 'SW2CMT', 'SW2COMM', 'SW2COMP', 'SW2DIRT', 'SW2EXIT', 'SW2GARG',
        'SW2GRAY', 'SW2GRAY1', 'SW2GSTON', 'SW2HOT', 'SW2LION', 'SW2MARB', 'SW2MET2', 'SW2METAL',
        'SW2MOD1', 'SW2PANEL', 'SW2PIPE', 'SW2ROCK', 'SW2SATYR', 'SW2SKIN', 'SW2SKULL', 'SW2SLAD',
        'SW2STARG', 'SW2STON1', 'SW2STON2', 'SW2STON6', 'SW2STONE', 'SW2STRTN', 'SW2TEK', 'SW2VINE',
        'SW2WDMET', 'SW2WOOD', 'SW2ZIM', 'SWATER1', 'SWATER4', 'S_END', 'S_START', 'TBLU',
        'TEXTURE1', 'TEXTURE2', 'TFOG', 'TGRN', 'TITLEPIC', 'TLMP', 'TLP2', 'TNT1',
        'TRE1', 'TRE2', 'TRED', 'TROO', 'VICTORY2', 'VILE', 'WFALL1', 'WFALL4',
        'WICOLON', 'WIENTER', 'WIF', 'WIMINUS', 'WIMSTT', 'WIOSTI', 'WIOSTK', 'WIPAR',
        'WIPCNT', 'WISCRT2', 'WISPLAT', 'WISUCKS', 'WITIME', 'WIURH0', 'WIURH1', 'YKEY',
        'YSKU',
    ],
    'patterns': ['CWILV\\d\\d', 'END\\d', 'MAP\\d\\d', 'STCFN\\d\\d\\d', 'STFEVL\\d', 'STFKILL\\d', 'STFOUCH\\d', 'STFST\\d\\d', 'STFTL\\d0', 'STFTR\\d0', 'STGNUM\\d', 'STKEYS\\d', 'STTNUM\\d', 'STYSNUM\\d', 'WILV\\d\\d', 'WIMAP\\d', 'WINUM\\d'],
}
# --- koniec tabel ---

# Czego info.c NIE mowi: kto co wystrzeliwuje i co upuszcza (to siedzi w kodzie akcji,
# p_enemy.c / p_inter.c). Tylko typy, ktore moga wystapic w Doom 1 / Freedoom Phase 1.
SPAWNS = {
    'MT_POSSESSED': ['MT_CLIP'], 'MT_SHOTGUY': ['MT_SHOTGUN'], 'MT_CHAINGUY': ['MT_CHAINGUN'],
    'MT_WOLFSS': ['MT_CLIP'], 'MT_TROOP': ['MT_TROOPSHOT'], 'MT_HEAD': ['MT_HEADSHOT'],
    'MT_BRUISER': ['MT_BRUISERSHOT'], 'MT_KNIGHT': ['MT_BRUISERSHOT'], 'MT_CYBORG': ['MT_ROCKET'],
    'MT_FATSO': ['MT_FATSHOT'], 'MT_BABY': ['MT_ARACHPLAZ'], 'MT_UNDEAD': ['MT_TRACER', 'MT_SMOKE'],
    'MT_PAIN': ['MT_SKULL'], 'MT_VILE': ['MT_FIRE'],
}
# Zawsze potrzebne: gracz, efekty trafien i teleportow, pociski broni gracza.
ALWAYS = ['MT_PLAYER', 'MT_PUFF', 'MT_BLOOD', 'MT_TFOG', 'MT_IFOG', 'MT_ROCKET', 'MT_PLASMA',
          'MT_BFG', 'MT_EXTRABFG',
          'MT_MISC27']   # kod GBADoom CF_ENEMY_ROCKETS: kazdy wrog upuszcza wyrzutnie (p_inter.c)
# Sprite'y ustawiane z kodu, a nie z mobjinfo: zgnieciony drzwiami/sufitem trup przechodzi
# w S_GIBS (p_map.c, PIT_ChangeSector), a S_GIBS to SPR_POL5.
ALWAYS_SPRITES = {'POL5'}
# Lumpy, ktore silnik czyta, choc ich nazwy nie ma w src/doom jako napisu.
ENGINE_EXTRA = {'PLAYPAL', 'COLORMAP', 'TEXTURE1', 'TEXTURE2', 'PNAMES'}


def engine():
    return ENGINE


# ---------------------------------------------------------------------------------------
# Przygotowanie zrodla (raz) i skladanie wyniku (dla kazdego zestawu map)
# ---------------------------------------------------------------------------------------
class Source:
    pass


def prepare(lumps, kind):
    t = engine()
    src = Source()
    src.kind = kind
    src.lumps = lumps
    src.idx = idx = last_index(lumps)
    src.get = lambda n: lumps[idx[n]].data if n in idx else None
    src.texnames, src.texpatches = texture_dir(lumps, idx)
    src.texnum = {}
    for i, n in enumerate(src.texnames):
        src.texnum.setdefault(n, i)      # GetTextureNumForName bierze PIERWSZE trafienie
    src.pnames = read_pnames(lumps, idx)
    pp = src.get('PLAYPAL')
    if len(pp) < 768 * 14:
        raise WadError(f'PLAYPAL ma {len(pp)} B - silnik potrzebuje 14 palet ({768 * 14} B)')
    src.playpal = bytes(pp[:768])
    if len(src.get('COLORMAP')) < 34 * 256:
        raise WadError(f'COLORMAP ma {len(src.get("COLORMAP"))} B - silnik czyta 34 tablice po 256 B')
    # flaty: kolejnosc miedzy F_START a F_END (numer flatu = pozycja; podznaczniki tez sie licza)
    fs, fe = idx['F_START'], idx['F_END']
    if fe < fs:
        raise WadError('F_END przed F_START')
    src.flat_lumps = [l.name for l in lumps[fs + 1:fe]]
    src.flatset = {n for n in src.flat_lumps if not n.endswith(('_START', '_END'))}
    for n in src.flatset:
        if len(lumps[idx[n]].data) not in (0, 4096):
            # silnik rysuje flat jako 64x64 bez granic
            raise WadError(f'plaska tekstura {n} ma {len(lumps[idx[n]].data)} B zamiast 4096')
    # mapy
    src.maps = {}
    src.order = []
    for mi, l in enumerate(lumps):
        if is_map_marker(l.name) and l.name.startswith('E1'):
            if l.name in src.maps:
                raise WadError(f'mapa {l.name} wystepuje w pliku dwa razy - uszkodzony albo przerobiony IWAD')
            src.maps[l.name] = analyze_map(lumps, mi, src.texnum, src.flatset)
            src.order.append(l.name)
    # Silnik woluje je z nazwy przy kazdym poziomie (niebo) i na koncu epizodu (tlo tekstu).
    if 'SKY1' not in src.texnum:
        raise WadError('brak tekstury SKY1 (niebo epizodu 1)')
    for f in ('F_SKY1', 'FLOOR4_8'):
        if f not in src.flatset or len(lumps[idx[f]].data) != 4096:
            raise WadError(f'brak plaskiej tekstury {f} (4096 B)')
    if 'E1M1' not in src.maps:
        raise WadError('brak mapy E1M1 - "Nowa gra" zawsze od niej startuje')
    if src.maps['E1M1'].problem:
        raise WadError(f'E1M1 nie nadaje sie dla silnika: {src.maps["E1M1"].problem}')
    # pasek stanu - jeden dla kazdego zestawu map
    src.stbar, src.stbar_at, src.stbar_removed, src.stbar_canvas, src.stbar_pat = make_stbar(src.get, pp)
    # sprite'y: rodziny potworow i przedmiotow z mobjinfo
    src.mobj = t['mobj']
    src.lit = set(t['lumps']) | ENGINE_EXTRA
    src.pats = [re.compile(p) for p in t['patterns']]
    src.anims = [tuple(a) for a in t['anims']]
    src.switches = [tuple(s) for s in t['switches']]
    src.black = Lump('HELP2', black_patch())
    src.m_arun = Lump('M_ARUN', text_patch(src.get, 'ALWAYS RUN'))
    src.m_gamma = Lump('M_GAMMA', text_patch(src.get, 'GAMMA'))
    src.gamma_pals = [Lump(f'PLAYPAL{k}', p) for k, p in enumerate(gamma_palettes(bytes(pp)), 1)]
    src.checked = set()
    s0, s1 = idx['S_START'], idx['S_END']
    src.sprites = [l for l in lumps[s0 + 1:s1] if not l.name.endswith(('_START', '_END'))]
    # PNAMES wielkimi literami (GbaWadUtil ProcessPNames)
    pn = src.get('PNAMES')
    n = struct.unpack_from('<I', pn, 0)[0]
    src.pnames_lump = Lump('PNAMES', struct.pack('<I', n) +
                           b''.join(nm.encode('latin-1').ljust(8, b'\0')[:8] for nm in src.pnames))
    return src


def engine_wants(src, n):
    return n in src.lit or any(p.fullmatch(n) for p in src.pats)


def is_graphic(n):
    return n not in ENGINE_EXTRA and n not in ('ENDOOM', 'GENMIDI', 'DMXGUS', 'DEHACKED', 'KOSINFO')


def assemble(src, keep, gamma=False, no_title=False):
    """Lista lumpow wyniku dla map `keep`, statystyki i usuniete rodziny sprite'ow."""
    keep = [m for m in src.order if m in keep]
    used_tex, used_flat, used_ed = set(), set(), set()
    for m in keep:
        mi = src.maps[m]
        used_tex |= mi.used_tex
        used_flat |= mi.used_flat
        used_ed |= mi.used_ednum
    used_tex.add('SKY1')                  # niebo epizodu 1 (r_sky.c)
    used_flat |= {'FLOOR4_8', 'F_SKY1'}   # tlo tekstu finalu E1 (f_finale.c) i niebo
    # animacje i przelaczniki: cala sekwencja albo nic
    tex_idx = {n: i for i, n in reversed(list(enumerate(src.texnames)))}
    for sw1, sw2 in src.switches:
        if sw1 in used_tex or sw2 in used_tex:
            used_tex |= {sw1, sw2}
    flat_pos = {n: i for i, n in enumerate(src.flat_lumps)}
    for istex, end, start in src.anims:
        if istex and start in tex_idx and end in tex_idx:
            rng = src.texnames[tex_idx[start]:tex_idx[end] + 1]
            if used_tex & set(rng):
                used_tex |= set(rng)
        if not istex and start in flat_pos and end in flat_pos:
            rng = src.flat_lumps[flat_pos[start]:flat_pos[end] + 1]
            if used_flat & set(rng):
                used_flat |= set(rng)
    # Tekstura nr 0: "-" w sidedefie dostaje numer 0 (GbaWadUtil), a P_LoadSideDefs laduje
    # WSZYSTKIE trzy tekstury kazdego sidedefu (R_GetTexture) - wiec tekstura 0 jest zawsze.
    if src.texnames:
        used_tex.add(src.texnames[0])
    used_patch = set()
    for i, n in enumerate(src.texnames):
        if n in used_tex:
            for p in src.texpatches[i]:
                if not 0 <= p < len(src.pnames):
                    raise WadError(f'tekstura {n} wskazuje latke nr {p} spoza PNAMES')
                if src.pnames[p] not in src.idx:
                    raise WadError(f'tekstura {n} uzywa latki {src.pnames[p]}, ktorej nie ma w WAD-zie')
                used_patch.add(src.pnames[p])
    # sprite'y: usuwamy tylko rodziny nalezace WYLACZNIE do niepotrzebnych typow
    need_mt = set(ALWAYS) | {mt for mt, (ed, _) in src.mobj.items() if ed in used_ed}
    for mt in list(need_mt):
        need_mt |= set(SPAWNS.get(mt, []))
    need_spr, other_spr = set(), set()
    for mt, (_, sprs) in src.mobj.items():
        (need_spr if mt in need_mt else other_spr).update(sprs)
    drop_spr = (other_spr - need_spr) - ALWAYS_SPRITES   # psprite broni nie sa w mobj - zostaja

    out, stats, counts = [], {}, {}

    def add(cat, l):
        out.append(l)
        if not l.alias:
            stats[cat] = stats.get(cat, 0) + len(l.data)
            counts[cat] = counts.get(cat, 0) + 1
    in_map = in_spr = in_pat = in_flat = False
    black_added, black_name = False, None
    for l in src.lumps:
        n = l.name
        if is_map_marker(n):
            in_map = n in keep
            if in_map:
                for c in src.maps[n].conv:
                    add('mapy', c)
            continue
        if n in MAP_CHILD:                 # te nazwy wystepuja tylko za znacznikiem mapy
            continue
        in_map = False
        if n in ('S_START', 'SS_START'):
            in_spr = True
        if n in ('S_END', 'SS_END'):
            in_spr = False
        if n in ('P_START', 'PP_START'):
            in_pat = True
        if n in ('P_END', 'PP_END'):
            in_pat = False
        if n in ('F_START', 'FF_START'):
            in_flat = True
        if n in ('F_END', 'FF_END'):
            in_flat = False
        if n.endswith(('_START', '_END')):
            add('znaczniki', Lump(n, b''))
            continue
        if in_spr:
            if n[:4] in drop_spr:
                continue
            check_patch(src, l)
            add('sprite', l)
            continue
        if in_pat:
            if n in used_patch:
                check_patch(src, l)
                add('latki', l)
            continue
        if in_flat:
            # Numer flatu = pozycja miedzy F_START a F_END, a animacje licza zakresy po pozycji -
            # dlatego nieuzywany wpis zostaje (0 B), a znika tylko tresc. Pusty wpis o nazwie
            # znacznika TNT/Plutonii dostaje inna nazwe: nikt go nie szuka, a CheckIWAD2 by go znalazl.
            if n in used_flat:
                if len(l.data) != 4096:
                    raise WadError(f'plaska tekstura {n} jest uzywana, a ma {len(l.data)} B zamiast 4096')
                add('flaty', l)
            else:
                add('flaty', Lump('KOSPUSTY' if n in COMMERCIAL_MARKERS else n, b''))
            continue
        if n == 'PNAMES':
            add('grafika', src.pnames_lump)
            continue
        # Dzwiek, muzyka i dema: silnik ich nie gra (i_audio.c to zaslepki), GbaWadUtil tez je wycina.
        if n.startswith(('D_', 'DS', 'DP', 'DEMO', 'GENMIDI')) or n in ('DMXGUS', 'DMXGUSC', 'ENDOOM', 'DEHACKED'):
            continue
        if n in BLACK_NAMES or (no_title and n == 'TITLEPIC'):
            if not black_added:
                black_added = True
                add('grafika', Lump(n, src.black.data))
                black_name = n
            else:
                add('grafika', Lump(n, b'', alias=black_name))
            continue
        if not engine_wants(src, n):
            continue
        if is_graphic(n):
            # Menu, font HUD-u, przerywnik: V_DrawPatch sprawdza tylko poczatek postu, nie koniec -
            # post dluzszy niz latka pisalby za buforem klatki. Poprawna grafika tego nie ma.
            check_patch(src, l, full=True)
            gw, gh = struct.unpack_from('<hh', l.data, 0)
            if gw > 320 or gh > 200:
                raise WadError(f'grafika {n} ma {gw}x{gh} - wieksza niz ekran 320x200')
        add('grafika', l)
    # Ekrany, ktore silnik epizodu 1 woluje z nazwy (final: HELP2/CREDIT, przerywnik: WIMAP0) -
    # gdyby WAD ich nie mial, gra stanelaby z I_Error na koncu mapy; czarna latka wystarczy.
    have = {l.name for l in out}
    for n in ('HELP2', 'CREDIT', 'WIMAP0'):
        if n not in have:
            if not black_added:
                black_added, black_name = True, n
                add('grafika', Lump(n, src.black.data))
            else:
                add('grafika', Lump(n, b'', alias=black_name))
    add('pasek', Lump('KOSSTBAR', src.stbar))
    if gamma:
        for p in src.gamma_pals:
            add('palety', p)
    else:
        for k in range(1, 6):
            add('palety', Lump(f'PLAYPAL{k}', b'', alias='PLAYPAL'))
    add('menu', src.m_arun)
    add('menu', src.m_gamma)
    info = (KOSINFO_MAGIC + b'\n' + f'zrodlo={src.kind}\nmapy={",".join(keep)}\npasek=KOSSTBR1\n'.encode())
    add('kosinfo', Lump('KOSINFO', info))
    names = {l.name for l in out}
    for mk in COMMERCIAL_MARKERS:
        if mk in names:
            raise WadError(f'lump {mk} w wyniku przestawilby silnik w tryb DOOM II (d_main.c CheckIWAD2)')
    for mk in ('F_START', 'F_END', 'S_START', 'S_END'):
        if mk not in names:
            raise WadError(f'brak {mk} w wyniku')
    if len(out) > MAX_LUMPS_OUT:
        raise WadError(f'{len(out)} lumpow - silnik przyjmie najwyzej {MAX_LUMPS_OUT}')
    return out, stats, counts, drop_spr


def check_patch(src, l, full=False):
    if (id(l), full) not in src.checked:
        patch_check(l.data, l.name, full)
        src.checked.add((id(l), full))


# ---------------------------------------------------------------------------------------
# Zapis .kwad
# ---------------------------------------------------------------------------------------
def layout(lumps):
    """Polozenia lumpow w pliku: (wpisy katalogu, dlugosc przed katalogiem, bloki danych).
    Identyczne tresci dziela jedno miejsce - silnik tylko czyta."""
    size = 12
    at, where, entries, blocks = {}, {}, [], []
    for l in lumps:
        if l.alias:
            entries.append((l.name, None, l.alias))
            continue
        if not len(l.data):
            entries.append((l.name, 12, 0))
            where[l.name] = (12, 0)
            continue
        k = l.key()
        if k not in at:
            size += -size % 4          # Xtensa nie czyta slowa spod nierownego adresu
            at[k] = size
            blocks.append((size, l.data))
            size += len(l.data)
        where[l.name] = (at[k], len(l.data))
        entries.append((l.name, at[k], len(l.data)))
    size += -size % 4
    return entries, size, blocks, where


def kwad_size(lumps):
    entries, size, _, _ = layout(lumps)
    return size + 16 * len(entries) + 4


def kwad_bytes(lumps):
    entries, size, blocks, where = layout(lumps)
    blob = bytearray(size)
    blob[0:4] = b'IWAD'
    for pos, data in blocks:
        blob[pos:pos + len(data)] = data
    for name, fp, sz in entries:
        if fp is None:
            fp, sz = where[sz]
        blob += struct.pack('<II8s', fp, sz, name.encode('latin-1')[:8])
    struct.pack_into('<II', blob, 4, len(entries), size)
    blob += struct.pack('<I', zlib.crc32(blob) & 0xFFFFFFFF)
    return bytes(blob)


def cap_from_image(img):
    if not os.path.isfile(img):
        raise WadError(f'{img}: nie ma takiego obrazu silnika')
    sz = os.path.getsize(img)
    base = (sz + 65535) & ~65535
    return SLOT_OTA0 - base - HDR_SECTOR


# ---------------------------------------------------------------------------------------
# Plan: ktore mapy
# ---------------------------------------------------------------------------------------
def reachable(src, keep):
    """E1M9 ma sens tylko z mapa, ktora ma tajne wyjscie (g_game.c KosNextMap)."""
    return 'E1M9' not in keep or any(src.maps[m].secret for m in keep if m != 'E1M9')


def plan_max(src, cap, gamma, no_title):
    """Najwiecej map, ktore sie zmieszcza (E1M1 zawsze), przy remisie wczesniejsze mapy.
    Epizod ma najwyzej 9 map, wiec wystarcza przejrzec 256 zestawow od najlepszego w dol
    i wziac pierwszy, ktory sie miesci."""
    usable = [m for m in src.order if not src.maps[m].problem]
    others = [m for m in usable if m != 'E1M1']
    cands = []
    for mask in range(1 << len(others)):
        keep = [m for m in src.order if m == 'E1M1' or (m in others and mask >> others.index(m) & 1)]
        if reachable(src, keep):
            cands.append(((-len(keep), [src.order.index(m) for m in keep]), keep))
    for _, keep in sorted(cands):
        if kwad_size(assemble(src, keep, gamma, no_title)[0]) <= cap:
            return keep
    return []


def plan_consecutive(src, cap, gamma, no_title):
    rows = []
    for k in range(1, len(src.order) + 1):
        keep = src.order[:k]
        if any(src.maps[m].problem for m in keep):
            break
        rows.append((keep, kwad_size(assemble(src, keep, gamma, no_title)[0])))
    return rows


# ---------------------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------------------
def fmt(n):
    return f'{n:,}'.replace(',', ' ')


def report(src, keep, out, stats, counts, drop, cap):
    size = kwad_size(out)
    print(f'mapy w pliku: {", ".join(keep)}  ({len(keep)} z {len(src.order)} map epizodu 1)')
    left = [m for m in src.order if m not in keep]
    if left:
        why = []
        for m in left:
            if src.maps[m].problem:
                why.append(f'{m} ({src.maps[m].problem})')
            elif not reachable(src, keep + [m]):
                why.append(f'{m} (bez mapy z tajnym wyjsciem nie da sie na nia wejsc)')
            else:
                why.append(f'{m} (brak miejsca)')
        print('pominiete: ' + ', '.join(why))
    print('zawartosc:')
    for c, v in sorted(stats.items(), key=lambda kv: -kv[1]):
        print(f'  {c:10s} {fmt(v):>11s} B  ({counts[c]} lumpow)')
    print(f'  {"katalog":10s} {fmt(16 * len(out) + 16):>11s} B  ({len(out)} wpisow + naglowek i CRC)')
    print(f'pasek stanu: KOSSTBAR z STBAR+STARMS+STTPRCNT tego WAD-u, wyciete kolumny tla '
          f'{",".join(map(str, src.stbar_removed))} (i x >= {PC_CROP}: tabela amunicji)')
    fams = sorted({l.name[:4] for l in src.sprites} & drop)
    print(f'usuniete rodziny sprite\'ow (potwory i przedmioty spoza wybranych map): {" ".join(fams) or "-"}')
    print(f'razem {fmt(size)} B z {fmt(cap)} B - zapas {fmt(cap - size)} B')


def main(argv=None):
    ap = argparse.ArgumentParser(description='Dane gry dla DOOM na K-OS (plik /doom/doom.kwad)')
    ap.add_argument('wejscie', nargs='?', help='freedoom1.wad, doom1.wad albo doom.wad')
    ap.add_argument('wyjscie', nargs='?', help='plik wynikowy, np. doom.kwad')
    ap.add_argument('--maps', help='np. E1M1,E1M2,E1M3 (domyslnie: najwiecej map, ile sie zmiesci)')
    ap.add_argument('--limit', type=lambda v: int(v, 0), help=f'miejsce na dane w B (domyslnie {DEFAULT_CAP})')
    ap.add_argument('--obraz', help='policz miejsce z obrazu silnika (kos/bin/<plytka>/doom.bin)')
    ap.add_argument('--gamma', action='store_true', help='prawdziwe palety jasnosci PLAYPAL1-5 (+53 760 B)')
    ap.add_argument('--bez-tytulu', action='store_true', help='TITLEPIC na czarno (-~68 kB)')
    ap.add_argument('--budzet', action='store_true', help='tylko policz, co sie zmiesci')
    ap.add_argument('--kolejne', action='store_true',
                    help='zamiast najwiekszej liczby map: najdluzszy ciag E1M1, E1M2, ... bez przeskokow')
    ap.add_argument('--analiza', action='store_true', help='rozbicie pliku WAD na kategorie')
    ap.add_argument('--podglad', metavar='PNG', help='zapisz obraz paska stanu (PC i K-OS) do pliku PNG')
    ap.add_argument('--lista', action='store_true', help='wypisz kazdy lump wyniku')
    ap.add_argument('--tabele', action='store_true', help=argparse.SUPPRESS)
    a = ap.parse_args(argv)

    if a.tabele:
        sys.stdout.write(tables_literal(engine_tables_from_src()))
        return 0
    if not a.wejscie:
        ap.error('podaj plik WAD')
    ident, lumps, raw = read_wad(a.wejscie)
    if a.analiza:
        cats = {}
        for l in lumps:
            n = l.name
            c = ('mapy' if is_map_marker(n) or (n in MAP_CHILD) else 'muzyka' if n.startswith('D_') else
                 'dzwieki' if n.startswith(('DS', 'DP')) else 'dema' if n.startswith('DEMO') else 'inne')
            cats[c] = cats.get(c, 0) + len(l.data)
        print(f'{a.wejscie}: {ident.decode()} {len(lumps)} lumpow, {fmt(len(raw))} B')
        for k, v in sorted(cats.items(), key=lambda kv: -kv[1]):
            print(f'  {k:10s} {fmt(v):>11s} B')
        return 0

    kind = identify(ident, lumps)
    cap = a.limit or (cap_from_image(a.obraz) if a.obraz else DEFAULT_CAP)
    print(f'wejscie: {a.wejscie} = {KINDS[kind]} (rozpoznane po mapach i lumpach, nie po sumie kontrolnej)')
    print(f'         {fmt(len(raw))} B, sha1 {hashlib.sha1(raw).hexdigest()}')
    src = prepare(lumps, kind)
    print(f'mapy epizodu 1: {len(src.order)}; miejsce na dane: {fmt(cap)} B ({cap // 1024} kB)')
    for m in src.order:
        if src.maps[m].problem:
            print(f'  UWAGA {m}: {src.maps[m].problem} - pomijam')

    if a.budzet:
        print('\nkolejne mapy od E1M1:')
        for keep, sz in plan_consecutive(src, cap, a.gamma, a.bez_tytulu):
            label = ','.join(keep) if len(keep) <= 3 else f'{keep[0]}..{keep[-1]}'
            print(f'  {label:28s} {fmt(sz):>11s} B  ' +
                  ('miesci sie' if sz <= cap else f'ZA DUZO o {fmt(sz - cap)} B'))
        base = kwad_size(assemble(src, ['E1M1'], a.gamma, a.bez_tytulu)[0])
        print('\npojedyncze mapy (mapa w formacie silnika; koszt dodania do samego E1M1; RAM poziomu - szacunek):')
        for m in src.order:
            mi = src.maps[m]
            if mi.problem:
                print(f'  {m:6s} odpada: {mi.problem}')
                continue
            add = kwad_size(assemble(src, ['E1M1', m], a.gamma, a.bez_tytulu)[0]) - base if m != 'E1M1' else base
            sec = '  tajne wyjscie' if mi.secret else ''
            print(f'  {m:6s} mapa {fmt(mi.size):>9s} B  {"razem" if m == "E1M1" else "+"} {fmt(add):>9s} B  '
                  f'RAM ~{mi.ram // 1024} kB{sec}')
        best = plan_max(src, cap, a.gamma, a.bez_tytulu)
        if best:
            out = assemble(src, best, a.gamma, a.bez_tytulu)
            print(f'\nplan (najwiecej map): {",".join(best)}  = {fmt(kwad_size(out[0]))} B')
        else:
            print('\nnawet sama E1M1 sie nie miesci - sprobuj --bez-tytulu')
        return 0

    if not a.wyjscie:
        ap.error('brak pliku wyjsciowego (albo uzyj --budzet)')
    if os.path.isdir(a.wyjscie):         # np. /Volumes/KARTA/doom -> .../doom/doom.kwad
        a.wyjscie = os.path.join(a.wyjscie, 'doom.kwad')
    # realpath i samefile: dowiazanie albo inna sciezka do tego samego pliku tez sie liczy -
    # os.replace podmienilby wtedy oryginalny WAD uzytkownika.
    if os.path.realpath(a.wyjscie) == os.path.realpath(a.wejscie) or \
            (os.path.exists(a.wyjscie) and os.path.samefile(a.wyjscie, a.wejscie)):
        raise WadError('plik wyjsciowy nie moze byc plikiem wejsciowym')
    if a.maps:
        keep = [m.strip().upper() for m in a.maps.split(',') if m.strip()]
        bad = [m for m in keep if m not in src.maps]
        if bad:
            raise WadError(f'nie ma takich map w epizodzie 1: {", ".join(bad)}')
        if 'E1M1' not in keep:
            raise WadError('E1M1 musi zostac - "Nowa gra" zawsze startuje od E1M1')
        broken = [f'{m} ({src.maps[m].problem})' for m in keep if src.maps[m].problem]
        if broken:
            raise WadError('te mapy nie nadaja sie dla silnika: ' + ', '.join(broken))
        keep = [m for m in src.order if m in keep]
    elif a.kolejne:
        fit = [k for k, sz in plan_consecutive(src, cap, a.gamma, a.bez_tytulu) if sz <= cap]
        keep = fit[-1] if fit else []
        if not keep:
            raise WadError(f'nawet sama E1M1 sie nie miesci w {fmt(cap)} B - sprobuj --bez-tytulu')
    else:
        keep = plan_max(src, cap, a.gamma, a.bez_tytulu)
        if not keep:
            raise WadError(f'nawet sama E1M1 sie nie miesci w {fmt(cap)} B - sprobuj --bez-tytulu')
    out, stats, counts, drop = assemble(src, keep, a.gamma, a.bez_tytulu)
    report(src, keep, out, stats, counts, drop, cap)
    if a.lista:
        for l in out:
            print(f'    {l.name:8s} {"-> " + l.alias if l.alias else fmt(len(l.data)) + " B"}')
    if a.podglad:
        stbar_preview(a.podglad, src.stbar, src.stbar_at, src.stbar_canvas, src.stbar_pat, src.playpal)
        print(f'podglad paska: {a.podglad}')
    blob = kwad_bytes(out)
    if len(blob) > cap:
        raise WadError(f'wynik ma {fmt(len(blob))} B, a miejsca jest {fmt(cap)} B - mniej map w --maps')
    tmp = a.wyjscie + '.part'
    try:
        with open(tmp, 'wb') as f:      # najpierw obok, potem podmiana: przerwany zapis nie
            f.write(blob)               # zostawia na karcie polowy pliku pod dobra nazwa
        os.replace(tmp, a.wyjscie)
    except OSError as e:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise WadError(f'{a.wyjscie}: nie moge zapisac ({e.strerror})')
    print(f'wynik: {a.wyjscie}  {fmt(len(blob))} B')
    print('Poloz go na karcie SD jako /doom/doom.kwad.')
    if kind == 'freedoom1':
        print('Z Freedoomu: wolno go dawac dalej na licencji BSD-3-Clause Freedoomu (z jej tekstem).')
    else:
        print('Z DOOM id Software: tylko do wlasnego uzytku - nie udostepniaj tego pliku.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except WadError as e:
        print(f'BLAD: {e}', file=sys.stderr)
        sys.exit(2)
