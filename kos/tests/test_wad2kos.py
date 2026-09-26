#!/usr/bin/env python3
# DOOM dla K-OS - (c) 2026 Piotr Korona. Licencja: GPL-2.0 lub pozniejsza (patrz LICENSE).
"""Testy wad2kos.py na SZTUCZNYCH WAD-ach zbudowanych tutaj (zadnych danych id ani Freedoom).

Dwa wzorce struktury: "shareware" (jak doom1.wad: E1M1-E1M9, TEXTURE1, HELP1/HELP2, dema, dzwieki)
i "freedoom" (jak freedoom1.wad: lump FREEDOOM, 4 epizody, TEXTURE2, DEHACKED/UMAPINFO, INTERPIC,
twarze z roznymi offsetami jak w freedoom/buildcfg.txt). Sprawdzane: przerobka GBA (uklady line_t/
seg_t/sidedef_t/vertex_t), wycinanie, pasek KOSSTBAR (te same warunki, co silnik: st_stuff.c
KosCheckPatch), format pliku (to samo, co kos.cpp: srcCheck/dataCheck/CRC), plan map oraz zle
i zlosliwe wejscia: kazde ma skonczyc sie komunikatem (WadError / kod 2), nie sladem stosu.
Uruchom: python3 kos/tests/test_wad2kos.py
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import wad2kos as w  # noqa: E402

FAILS = 0
TMP = tempfile.mkdtemp()


def check(cond, msg):
    global FAILS
    if not cond:
        FAILS += 1
        print('BLAD:', msg)


def raises(fn, fragment, msg):
    try:
        fn()
    except w.WadError as e:
        check(fragment.lower() in str(e).lower(), f'{msg}: komunikat "{e}" bez "{fragment}"')
        return
    except Exception as e:     # noqa: BLE001 - kazdy inny wyjatek to wlasnie blad testowany tutaj
        check(False, f'{msg}: zamiast WadError {type(e).__name__}: {e}')
        return
    check(False, f'{msg}: brak bledu')


# ---------------------------------------------------------------------------------------
# Budowanie sztucznych WAD-ow
# ---------------------------------------------------------------------------------------
def pat(wd, h, color=1, lo=0, to=0, fn=None):
    cols = []
    for x in range(wd):
        col = {}
        for y in range(h):
            v = fn(x, y) if fn else color
            if v is not None:
                col[y] = v
        cols.append(col)
    p = bytearray(w.patch_encode(wd, h, cols))
    struct.pack_into('<hh', p, 4, lo, to)
    return bytes(p)


def room(tex, flat, things, extra_verts=0, special=0, lines_override=None):
    # kwadrat 128x128: 4 wierzcholki, 4 linie jednostronne, 4 sidedefy, 1 sektor, 4 segi
    verts = [(0, 0), (128, 0), (128, 128), (0, 128)] + [(1, 1)] * extra_verts
    V = b''.join(struct.pack('<hh', *v) for v in verts)
    L = lines_override if lines_override is not None else b''.join(
        struct.pack('<HHHhhHH', i, (i + 1) % 4, 1, special if i == 0 else 0, 0, i, 0xFFFF) for i in range(4))
    S = b''.join(struct.pack('<hh8s8s8sh', 0, 0, b'-', b'-', tex.encode(), 0) for _ in range(4))
    ang = [0, 0x4000, -0x8000, -0x4000]   # BAM/65536 jako signed short, jak w pliku mapy
    G = b''.join(struct.pack('<HHhHhh', i, (i + 1) % 4, ang[i], i, 0, 3) for i in range(4))
    SS = struct.pack('<HH', 4, 0)
    SEC = struct.pack('<hh8s8shhh', 0, 128, flat.encode(), b'F_SKY1', 160, 0, 0)
    T = b''.join(struct.pack('<hhhhh', 64, 64, 0, t, 7) for t in [1] + things)
    BM = struct.pack('<hhhh', 0, 0, 1, 1) + struct.pack('<H', 5) + struct.pack('<HHHHHH', 0, 0, 1, 2, 3, 0xFFFF)
    return [('THINGS', T), ('LINEDEFS', L), ('SIDEDEFS', S), ('VERTEXES', V), ('SEGS', G),
            ('SSECTORS', SS), ('NODES', b''), ('SECTORS', SEC), ('REJECT', b'\0'), ('BLOCKMAP', BM)]


def texture_lump(defs):
    n = len(defs)
    body = bytearray()
    offs = []
    base = 4 + 4 * n
    for name, ps in defs:
        offs.append(base + len(body))
        body += name.encode().ljust(8, b'\0') + b'\0' * 4 + struct.pack('<hh', 64, 128) + b'\0' * 4
        body += struct.pack('<h', len(ps))
        for p in ps:
            body += struct.pack('<hhhhh', 0, 0, p, 1, 0)
    return struct.pack('<i', n) + b''.join(struct.pack('<i', o) for o in offs) + bytes(body)


def stbar_art(freedoom=False):
    """Pasek jak w DOOM: tlo w jednym kolorze, pionowe fazy miedzy segmentami, podpisy na dole."""
    borders = {0, 1, 47, 48, 103, 104, 142, 143, 177, 178, 235, 236, 249, 250, 318, 319}

    def bar(x, y):
        if x in borders:
            return 200 if x % 2 else 40
        if y >= 26 and (x // 3) % 3 == 0 and x not in range(144, 178):
            return 230            # "litery" podpisow
        return 90 + (x * 7 + y * 3) % 5 if freedoom else 96
    L = [('STBAR', pat(320, 32, fn=bar)),
         ('STARMS', pat(40, 32, fn=lambda x, y: 200 if x in (0, 39) else 230 if y >= 27 and x % 4 == 1 else 120))]
    L += [(f'STTNUM{k}', pat(14, 16, 180 + k)) for k in range(10)]
    L += [('STTPRCNT', pat(14, 16, 190)), ('STTMINUS', pat(14, 16, 191))]
    L += [(f'STYSNUM{k}', pat(4, 6, 160 + k)) for k in range(10)]
    L += [(f'STGNUM{k}', pat(4, 6, 140 + k)) for k in range(10)]
    L += [(f'STKEYS{k}', pat(7, 5 if k < 3 else 7, 170 + k)) for k in range(9 if freedoom else 6)]
    for f in w.FACES:
        lo, to, wd = -5, -2, 24
        if freedoom and f in ('STFTL30', 'STFTL40'):
            lo, to, wd = (-2, -1, 27) if f == 'STFTL30' else (-1, -1, 28)   # buildcfg.txt Freedoomu
        L.append((f, pat(wd, 29, 210, lo=lo, to=to)))
    L += [(f'STFB{k}', pat(34, 32, 30 + k)) for k in range(4)]
    L += [(f'STCFN{c:03d}', pat(5, 7, 250)) for c in range(33, 96)]
    return L


def make_iwad(kind='shareware', maps=None, drop=(), extra=(), big=None, secret_in=None, edit=None):
    freedoom = kind == 'freedoom'
    pal = b''.join(bytes([v, v, v]) for v in range(256))
    L = [('PLAYPAL', pal * 14), ('COLORMAP', b'\0' * 8704)]
    if freedoom:
        L += [('FREEDOOM', b''), ('DEHACKED', b'Patch File for DeHackEd\n'), ('UMAPINFO', b'map E1M1 {}'),
              ('GAMECONF', b'{}'), ('WADINFO', b'x'), ('DBIGFONT', b'x' * 300), ('LANGUAGE', b'x' * 200)]
    L += [('ENDOOM', b'e' * 4000), ('GENMIDI', b'g' * 100), ('DMXGUS', b'd' * 50)]
    if maps is None:
        maps = [f'E1M{i}' for i in range(1, 10)] + (['E2M1', 'E3M1', 'E4M1'] if freedoom else [])
    what = {  # mapa: (tekstura, flat, rzeczy)
        'E1M1': ('WALL1', 'FLOOR1', [3001]), 'E1M2': ('WALL2', 'FLOOR2', [3003]),
        'E1M3': ('WALL3', 'FLOOR1', [3004]), 'E1M4': ('WALL1', 'NUKAGE1', [9]),
        'E1M5': ('WALL2', 'FLOOR1', [3001]), 'E1M6': ('WALL1', 'FLOOR2', [3002]),
        'E1M7': ('WALL3', 'FLOOR1', [3001]), 'E1M8': ('WALL1', 'FLOOR1', [3003]),
        'E1M9': ('WALL2', 'FLOOR2', [3001]),
    }
    for m in maps:
        tex, flat, th = what.get(m, ('WALL1', 'FLOOR1', [3001]))
        L += [(m, b'')] + room(tex, flat, th, extra_verts=(big or {}).get(m, 0),
                               special=51 if m == secret_in else 0)
    tdefs = [('AASTINKY', [4]), ('WALL1', [0]), ('WALL2', [1]), ('WALL3', [5]), ('SKY1', [2]),
             ('SW1STON1', [3]), ('SW2STON1', [3])]
    if freedoom:
        L += [('TEXTURE1', texture_lump(tdefs[:4])), ('TEXTURE2', texture_lump(tdefs[4:]))]
    else:
        L += [('TEXTURE1', texture_lump(tdefs))]
    pn = [b'p_wall1', b'P_WALL2', b'P_SKY1', b'P_SW1', b'P_STINK', b'P_WALL3', b'P_NOPE']
    L += [('PNAMES', struct.pack('<I', len(pn)) + b''.join(p.ljust(8, b'\0') for p in pn))]
    L += [('DSPISTOL', b'x' * 100), ('DPPISTOL', b'p' * 40), ('D_E1M1', b'm' * 50),
          ('DEMO1', b'd' * 30), ('DEMO2', b'd' * 30)]
    full = pat(320, 200, 5)
    L += [('HELP1', full), ('HELP2', full), ('CREDIT', full), ('TITLEPIC', pat(320, 200, 2)),
          ('WIMAP0', full), ('WIA00000', pat(16, 16)), ('WIURH0', pat(8, 8)), ('WIURH1', pat(8, 8)),
          ('M_DOOM', pat(100, 40)), ('M_EPI1', pat(60, 10)), ('M_EPI2', pat(60, 10)), ('M_EPI3', pat(60, 10)),
          ('M_PAUSE', pat(40, 10))]
    if freedoom:
        L += [('INTERPIC', full), ('VICTORY2', full), ('ENDPIC', full), ('BOSSBACK', full),
              ('PFUB1', full), ('PFUB2', full), ('M_EPI4', pat(60, 10))] + [(f'END{k}', pat(13, 13)) for k in range(7)]
    L += stbar_art(freedoom)
    L += [('S_START', b''), ('PLAYA1', pat(8, 8)), ('PLAYB1', pat(8, 8)), ('TROOA1', pat(8, 8)),
          ('BOSSA1', pat(8, 8)), ('POSSA1', pat(8, 8)), ('PISGA0', pat(8, 8)), ('SARGA1', pat(8, 8)),
          ('BAL1A0', pat(6, 6)), ('BAL7A0', pat(6, 6)), ('PUFFA0', pat(4, 4)), ('BLUDA0', pat(4, 4)),
          ('POL5A0', pat(4, 4)), ('LAUNA0', pat(8, 8)), ('S_END', b'')]
    L += [('P_START', b''), ('P_STINK', pat(8, 8)), ('P_WALL1', pat(64, 128)), ('P_WALL2', pat(64, 128)),
          ('P_WALL3', pat(64, 128, 3)), ('P_SKY1', pat(64, 128)), ('P_SW1', pat(32, 32)), ('P_END', b'')]
    L += [('F_START', b''), ('F1_START', b''), ('FLOOR1', b'\1' * 4096), ('FLOOR2', b'\2' * 4096),
          ('NUKAGE1', b'\5' * 4096), ('NUKAGE2', b'\6' * 4096), ('NUKAGE3', b'\7' * 4096),
          ('FLOOR4_8', b'\3' * 4096), ('F_SKY1', b'\4' * 4096), ('F1_END', b''), ('F_END', b'')]
    L += list(extra)
    L = [e for e in L if e[0] not in drop]
    if edit:
        L = edit(L)
    return wadbytes('IWAD', L)


def wadbytes(ident, L):
    blob = bytearray(ident.encode() + b'\0' * 8)
    ents = []
    for n, d in L:
        ents.append((len(blob), len(d), n))
        blob += d
    dirofs = len(blob)
    for fp, sz, n in ents:
        blob += struct.pack('<II8s', fp, sz, n.encode())
    struct.pack_into('<II', blob, 4, len(ents), dirofs)
    return bytes(blob)


def save(name, blob):
    p = os.path.join(TMP, name)
    open(p, 'wb').write(blob)
    return p


def load(blob, name='t.wad'):
    ident, lumps, raw = w.read_wad(save(name, blob))
    kind = w.identify(ident, lumps)
    return w.prepare(lumps, kind)


def dir_of(blob):
    n, ofs = struct.unpack_from('<II', blob, 4)
    return [(nm.split(b'\0')[0].decode('latin-1'), fp, sz) for fp, sz, nm in
            (struct.unpack_from('<II8s', blob, ofs + 16 * i) for i in range(n))]


# ---------------------------------------------------------------------------------------
# Lustro sprawdzen silnika (kos.cpp srcCheck/dataCheck, st_stuff.c KosCheckPatch)
# ---------------------------------------------------------------------------------------
def engine_accepts(blob):
    errs = []
    if zlib.crc32(blob) & 0xFFFFFFFF != w.CRC_RESIDUE:
        errs.append('CRC')
    if blob[:4] != b'IWAD':
        errs.append('IWAD')
    n, d = struct.unpack_from('<II', blob, 4)
    if not n or n > w.MAX_LUMPS_OUT or d % 4 or d < 12 or d + 16 * n + 4 != len(blob):
        errs.append('katalog')
        return errs
    info = bar = False
    for i in range(n):
        fp, sz, nm = struct.unpack_from('<II8s', blob, d + 16 * i)
        if fp > d or (sz and (fp < 12 or fp % 4 or sz > d - fp)):
            errs.append(f'lump {nm}')
        if nm == b'KOSINFO\0':
            info = sz >= 8 and blob[fp:fp + 8] == w.KOSINFO_MAGIC
        if nm == b'KOSSTBAR':
            bar = sz == 64 + 240 * 32 and blob[fp:fp + 8] == w.STBAR_MAGIC
    if not info or not bar:
        errs.append('KOSINFO/KOSSTBAR')
    return errs


def stbar_engine_ok(blob):
    """KosCheckPatch z st_stuff.c: kazda latka w kazdym miejscu rysowania cala w pasku 240x32."""
    ents = {n: (fp, sz) for n, fp, sz in dir_of(blob)}
    fp, sz = ents['KOSSTBAR']
    v = struct.unpack_from('<8shh26h', blob, fp)
    at = {'ammo': v[3:5], 'health': v[5:7], 'armor': v[7:9], 'face': v[9:11],
          'arms': [v[11 + 2 * i:13 + 2 * i] for i in range(6)], 'keys': [v[23 + 2 * j:25 + 2 * j] for j in range(3)]}
    get = {n: blob[fp:fp + sz] for n, (fp, sz) in ents.items()}
    patw = {n: w.patch_check(get[n], n) for n in w.STBAR_LUMPS if n in get}
    patw['STTNUM0'] = w.patch_check(get['STTNUM0'], 'STTNUM0')
    bad = []
    for name, x, y in w.widget_draws({k: (vv[0],) for k, vv in patw.items()}, at):
        wd, h, lo, to = w.patch_check(get[name], name, full=True)
        x0, y0 = x - lo, 128 + y - to
        if x0 < 0 or x0 + wd > 240 or y0 < 128 or y0 + h > 160:
            bad.append((name, x, y))
    return bad, at


# ---------------------------------------------------------------------------------------
def test_basics():
    check(w.fixed_div(5 << 16, 3 << 16) == 109226, 'fixed_div 5/3')
    check(w.fixed_div(-(5 << 16), 3 << 16) == -109226, 'fixed_div ujemne obcina do zera jak C')
    check(w.fixed_div(1 << 30, 1) == 0x7FFFFFFF, 'fixed_div nasycenie +')
    check(w.fixed_div(-(1 << 30), 1) == -0x80000000, 'fixed_div nasycenie -')
    t = w.engine_tables_from_src()
    e = w.ENGINE
    check(t['mobj'] == e['mobj'] and t['lumps'] == e['lumps'] and t['patterns'] == e['patterns'] and
          [tuple(a) for a in t['anims']] == [tuple(a) for a in e['anims']] and t['switches'] == e['switches'],
          'tabele w wad2kos.py = tabele z src/doom (python3 kos/wad2kos.py --tabele)')
    check(zlib.crc32(b'abc' + struct.pack('<I', zlib.crc32(b'abc'))) == w.CRC_RESIDUE, 'reszta CRC')


def test_shareware():
    src = load(make_iwad('shareware'), 'doom1.wad')
    check(src.kind == 'doom-shareware', f'rozpoznanie shareware ({src.kind})')
    out, stats, counts, drop = w.assemble(src, ['E1M1'])
    blob = w.kwad_bytes(out)
    check(w.kwad_size(out) == len(blob), 'kwad_size = dlugosc pliku')
    check(engine_accepts(blob) == [], f'silnik przyjmie plik: {engine_accepts(blob)}')
    check(w.kwad_bytes(w.assemble(src, ['E1M1'])[0]) == blob, 'wynik powtarzalny bajt w bajt')
    d = dir_of(blob)
    names = [n for n, _, _ in d]
    by = {n: (fp, sz) for n, fp, sz in d}
    check('E1M1' in names and 'E1M2' not in names, 'E1M2 wycieta')
    check(names.count('LINEDEFS') == 1, 'jeden zestaw lumpow mapy')
    check('P_WALL1' in names and 'P_SKY1' in names and 'P_WALL2' not in names, 'latki: WALL1+SKY1 zostaja, WALL2 znika')
    check(by['FLOOR2'][1] == 0 and by['FLOOR1'][1] == 4096 and by['FLOOR4_8'][1] == 4096 and by['F_SKY1'][1] == 4096,
          'flaty: nieuzywany FLOOR2 pusty, reszta cala')
    fl = names[names.index('F_START') + 1:names.index('F_END')]
    check(fl == ['F1_START', 'FLOOR1', 'FLOOR2', 'NUKAGE1', 'NUKAGE2', 'NUKAGE3', 'FLOOR4_8', 'F_SKY1', 'F1_END'],
          f'flaty zachowuja pozycje {fl}')
    check('TROOA1' in names and 'PLAYA1' in names and 'PISGA0' in names, 'imp, gracz i bron zostaja')
    check('BAL1A0' in names, 'pocisk impa zostaje (SPAWNS)')
    check('BOSSA1' not in names and 'POSSA1' not in names, 'baron i zombie (brak na E1M1) wycieci')
    check('BOSS' in drop and 'PISG' not in drop, 'rodziny: BOSS usuniety, psprite PISG nie')
    for n in ('DSPISTOL', 'DPPISTOL', 'D_E1M1', 'DEMO1', 'GENMIDI', 'ENDOOM', 'DMXGUS', 'HELP1', 'WIA00000'):
        check(n not in names, f'{n} usuniety')
    check('STBAR' not in names and 'STARMS' not in names and 'KOSSTBAR' in names, 'pasek tylko jako KOSSTBAR')
    check(not any(n.startswith('STGANUM') for n in names), 'zadnych cyfr GBA Doom II (STGANUM)')
    check(by['HELP2'][0] == by['CREDIT'][0] == by['WIMAP0'][0] and by['HELP2'][1] < 2000, 'HELP2/CREDIT/WIMAP0 = jedna czarna latka')
    check(by['TITLEPIC'][1] > 2000, 'TITLEPIC zostaje')
    check(all(by[f'PLAYPAL{k}'] == by['PLAYPAL'] for k in range(1, 6)), 'PLAYPAL1-5 = alias PLAYPAL')
    check('M_ARUN' in names and 'M_GAMMA' in names, 'M_ARUN/M_GAMMA sa')
    kp, _ = by['KOSINFO']
    check(blob[kp:kp + 8] == w.KOSINFO_MAGIC and b'zrodlo=doom-shareware' in blob[kp:kp + 200], 'KOSINFO')
    for n, fp, sz in d:
        check(sz == 0 or fp % 4 == 0, f'{n} wyrownany do 4')
    check(by['M_EPI2'][1] > 0 and by['M_EPI3'][1] > 0, 'M_EPI2/3 zostaja (menu epizodow shareware)')

    # uklady GBA
    i = names.index('E1M1')
    vfp, vsz = d[i + 4][1], d[i + 4][2]
    check(vsz == 4 * 8, 'VERTEXES 8 B')
    check(struct.unpack_from('<ii', blob, vfp + 8) == (128 << 16, 0), 'wierzcholek 1 w 16.16')
    lfp, lsz = d[i + 2][1], d[i + 2][2]
    check(lsz == 4 * 56, 'LINEDEFS 56 B (line_t)')
    ln = struct.unpack_from('<iiiiIiiHHiiiiHhhH', blob, lfp + 56 * 1)   # linia 1: (128,0)->(128,128)
    check(ln[:4] == (128 << 16, 0, 128 << 16, 128 << 16) and ln[4] == 1, 'line_t v1/v2/lineno')
    check(ln[5] == 0 and ln[6] == 128 << 16 and ln[16] == 1, 'line_t dx/dy, pionowa')
    check(ln[9:13] == (128 << 16, 0, 128 << 16, 128 << 16), 'line_t bbox TOP,BOTTOM,LEFT,RIGHT')
    check(struct.unpack_from('<iiiiIiiHHiiiiHhhH', blob, lfp)[16] == 0, 'linia 0 pozioma')
    sfp, ssz = d[i + 5][1], d[i + 5][2]
    check(ssz == 4 * 32, 'SEGS 32 B (seg_t)')
    sg = struct.unpack_from('<iiiiiIHHHH', blob, sfp + 32 * 2)
    check(sg[4] == 3 << 16 and sg[5] == (0x8000 << 16) & 0xFFFFFFFF and sg[6:] == (2, 2, 0, 0xFFFF), 'seg_t pola')
    dfp, dsz = d[i + 3][1], d[i + 3][2]
    check(dsz == 4 * 12, 'SIDEDEFS 12 B')
    check(struct.unpack_from('<hhhhhh', blob, dfp) == (0, 0, 0, 0, 1, 0), 'sidedef: "-" = 0, WALL1 = tekstura 1')
    check('P_STINK' in names, 'latka tekstury nr 0 zostaje (silnik laduje ja dla kazdego "-")')
    check(blob[by['PNAMES'][0] + 4:by['PNAMES'][0] + 12] == b'P_WALL1\0', 'PNAMES wielkimi literami')

    # pasek: te same warunki co silnik
    bad, at = stbar_engine_ok(blob)
    check(bad == [], f'latki paska mieszcza sie wszedzie, gdzie silnik je rysuje: {bad[:3]}')
    check(len(src.stbar_removed) == w.PC_CROP - w.ST_W, 'wyciete dokladnie 10 kolumn tla')
    prot = set()
    for name, x, y in w.widget_draws(src.stbar_pat, {'ammo': w.PC_AMMO, 'health': w.PC_HEALTH, 'armor': w.PC_ARMOR,
                                                    'face': w.PC_FACE, 'arms': w.PC_ARMS, 'keys': w.PC_KEYS}):
        x0, x1, _, _ = w.box(src.stbar_pat[name], x, y)
        prot |= set(range(x0, x1))
    check(not prot & set(src.stbar_removed), 'zadna wycieta kolumna nie lezy pod widzetem')
    r = src.stbar_removed
    check(all(b - a >= w.SEAM_GAP for a, b in zip(r, r[1:])), f'szwy rozlozone co >= {w.SEAM_GAP} px: {r}')
    check(not set(r) & {0, 1, 47, 48, 103, 104, 142, 143, 177, 178, 235, 236}, f'fazy miedzy segmentami nietkniete: {r}')
    check(at['ammo'][0] < at['health'][0] < at['armor'][0] and at['keys'][0][0] <= 239, 'kolejnosc widzetow')

    # E1M2: tekstura WALL2 = numer 2, baron zostaje, imp nie
    out2 = w.assemble(src, ['E1M1', 'E1M2'])[0]
    b2 = w.kwad_bytes(out2)
    d2 = dir_of(b2)
    n2 = [n for n, _, _ in d2]
    j = n2.index('E1M2')
    check(struct.unpack_from('<hhhhhh', b2, d2[j + 3][1])[4] == 2, 'sidedef: WALL2 = tekstura 2')
    check('BOSSA1' in n2 and 'BAL7A0' in n2, 'E1M2: baron i jego pocisk sa')


def test_freedoom():
    src = load(make_iwad('freedoom'), 'freedoom1.wad')
    check(src.kind == 'freedoom1', f'rozpoznanie freedoom1 ({src.kind})')
    check(src.order == [f'E1M{i}' for i in range(1, 10)], 'tylko epizod 1')
    out, stats, counts, drop = w.assemble(src, ['E1M1'])
    blob = w.kwad_bytes(out)
    check(engine_accepts(blob) == [], f'silnik przyjmie plik Freedoomu: {engine_accepts(blob)}')
    names = [n for n, _, _ in dir_of(blob)]
    by = {n: (fp, sz) for n, fp, sz in dir_of(blob)}
    for n in ('FREEDOOM', 'DEHACKED', 'UMAPINFO', 'GAMECONF', 'WADINFO', 'DBIGFONT', 'LANGUAGE', 'E2M1', 'E4M1'):
        check(n not in names, f'{n} wyciety (silnik go nie czyta)')
    for n in ('INTERPIC', 'VICTORY2', 'ENDPIC', 'BOSSBACK', 'PFUB1', 'END3'):
        check(by.get(n) == by['HELP2'], f'{n}: tylko alias czarnej latki (to samo miejsce co HELP2)')
    check('TEXTURE2' in names, 'TEXTURE2 zostaje (numery tekstur)')
    bad, at = stbar_engine_ok(blob)
    check(bad == [], f'pasek Freedoomu (szersze twarze STFTL30/40) miesci sie: {bad[:3]}')
    check(b'zrodlo=freedoom1' in blob, 'KOSINFO: zrodlo freedoom1')


def test_plan():
    # E1M3 ogromna (+~240 kB wierzcholkow), tajne wyjscie na E1M3 -> E1M9 bez niej nieosiagalna
    big = {'E1M3': 30000}
    src = load(make_iwad('shareware', big=big, secret_in='E1M3'), 'plan.wad')
    one = w.kwad_size(w.assemble(src, ['E1M1'])[0])
    all_but = [m for m in src.order if m not in ('E1M3', 'E1M9')]
    cap = w.kwad_size(w.assemble(src, all_but)[0]) + 100
    check(w.kwad_size(w.assemble(src, src.order)[0]) > cap, 'z E1M3 sie nie miesci')
    rows = w.plan_consecutive(src, cap, False, False)
    fit = [k for k, sz in rows if sz <= cap]
    check(fit[-1] == ['E1M1', 'E1M2'], f'kolejne mapy koncza sie na E1M2 ({fit[-1] if fit else "-"})')
    best = w.plan_max(src, cap, False, False)
    check(best == all_but, f'plan najwiecej map pomija E1M3 i nieosiagalna E1M9: {best}')
    # tajne wyjscie na E1M2 -> E1M9 osiagalna i wchodzi
    src2 = load(make_iwad('shareware', big=big, secret_in='E1M2'), 'plan2.wad')
    cap2 = w.kwad_size(w.assemble(src2, [m for m in src2.order if m != 'E1M3'])[0]) + 100
    best2 = w.plan_max(src2, cap2, False, False)
    check(best2 == [m for m in src2.order if m != 'E1M3'], f'z tajnym wyjsciem na E1M2 E1M9 wchodzi: {best2}')
    check(w.plan_max(src, one - 1, False, False) == [], 'za malo miejsca nawet na E1M1 -> pusty plan')


def test_cli():
    p = save('cli.wad', make_iwad('shareware'))
    outp = os.path.join(TMP, 'doom.kwad')
    png = os.path.join(TMP, 'pasek.png')
    r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py'), p, outp, '--podglad', png],
                       capture_output=True, text=True)
    check(r.returncode == 0, f'CLI: kod {r.returncode}, {r.stderr[-300:]}')
    check('mapy w pliku: E1M1' in r.stdout and 'razem' in r.stdout, 'CLI wypisuje, co weszlo')
    blob = open(outp, 'rb').read()
    check(engine_accepts(blob) == [], 'plik z CLI przyjmie silnik')
    check(not os.path.exists(outp + '.part'), 'po zapisie nie zostaje .part')
    r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py'), p, outp + 'k', '--kolejne'],
                       capture_output=True, text=True)
    check(r.returncode == 0 and engine_accepts(open(outp + 'k', 'rb').read()) == [], f'--kolejne: {r.stderr[-200:]}')
    pngb = open(png, 'rb').read()
    check(pngb[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II', pngb[16:24]) == (960, 216), 'podglad PNG 960x216')
    r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py'), '--budzet', p],
                       capture_output=True, text=True)
    check(r.returncode == 0 and 'plan (najwiecej map)' in r.stdout, f'--budzet: {r.stdout[-200:]} {r.stderr[-200:]}')
    # zly plik: komunikat i kod 2, bez sladu stosu
    bad = save('zly.wad', b'IWAD' + struct.pack('<II', 0xFFFFFFFF, 12))
    r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py'), bad, outp + '2'],
                       capture_output=True, text=True)
    check(r.returncode == 2 and 'BLAD:' in r.stderr and 'Traceback' not in r.stderr, f'zly WAD w CLI: {r.returncode} {r.stderr[-200:]}')
    check(not os.path.exists(outp + '2'), 'zly WAD nie zostawia pliku wyniku')
    # za male miejsce
    r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py'), p, outp + '3', '--limit', '1000'],
                       capture_output=True, text=True)
    check(r.returncode == 2 and 'nawet sama E1M1' in r.stderr, f'--limit 1000: {r.stderr[-200:]}')
    r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py'), p, outp + '4', '--maps', 'E1M2'],
                       capture_output=True, text=True)
    check(r.returncode == 2 and 'E1M1 musi zostac' in r.stderr, '--maps bez E1M1')
    r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py'), outp, outp + '5'],
                       capture_output=True, text=True)
    check(r.returncode == 2 and 'juz jest plik z wad2kos.py' in r.stderr, '.kwad podany jako wejscie')
    link = os.path.join(TMP, 'dowiazanie.kwad')
    os.symlink(p, link)
    r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py'), p, link], capture_output=True, text=True)
    check(r.returncode == 2 and 'wejsciowym' in r.stderr and open(p, 'rb').read(4) == b'IWAD',
          'wyjscie jako dowiazanie do wejscia - odmowa, WAD nietkniety')
    for args, what in (([TMP, outp + '6'], 'katalog jako wejscie'),
                       ([p, os.path.join(TMP, 'nie', 'ma', 'doom.kwad')], 'zapis do nieistniejacego katalogu'),
                       ([p, outp + '7', '--obraz', os.path.join(TMP, 'brak.bin')], 'brak obrazu --obraz')):
        r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py')] + args, capture_output=True, text=True)
        check(r.returncode == 2 and 'BLAD:' in r.stderr and 'Traceback' not in r.stderr, f'{what}: {r.stderr[-200:]}')
    # sam plik wad2kos.py, bez zrodel silnika obok (tak dostaje go uzytkownik)
    solo = os.path.join(TMP, 'solo')
    os.makedirs(solo)
    import shutil
    shutil.copy(os.path.join(HERE, '..', 'wad2kos.py'), solo)
    r = subprocess.run([sys.executable, os.path.join(solo, 'wad2kos.py'), p, os.path.join(solo, 'doom.kwad')],
                       capture_output=True, text=True, cwd=solo)
    check(r.returncode == 0 and engine_accepts(open(os.path.join(solo, 'doom.kwad'), 'rb').read()) == [],
          f'wad2kos.py dziala jako jeden plik: {r.stderr[-300:]}')
    kat = os.path.join(TMP, 'karta', 'doom')
    os.makedirs(kat)
    r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'wad2kos.py'), p, kat], capture_output=True, text=True)
    check(r.returncode == 0 and os.path.isfile(os.path.join(kat, 'doom.kwad')), 'katalog jako wyjscie -> doom.kwad w srodku')


def test_bad_inputs():
    ok = make_iwad('shareware')

    def rd(blob, name='x.wad'):
        return lambda: w.read_wad(save(name, blob))
    raises(rd(b''), 'za krotki', 'pusty plik')
    raises(rd(b'\x00' * 100), 'nie jest plik WAD', 'losowe bajty')
    raises(rd(b'IWAD' + struct.pack('<II', 0xFFFFFFFF, 12)), 'katalog', 'numlumps = 2^32-1')
    raises(rd(b'IWAD' + struct.pack('<II', 3, 10 ** 9)), 'katalog', 'katalog za koncem pliku')
    raises(rd(ok[:len(ok) // 2]), 'katalog', 'plik uciety w polowie (katalog na koncu)')
    b = bytearray(ok)
    n, d = struct.unpack_from('<II', b, 4)
    struct.pack_into('<II', b, d, 10 ** 8, 16)          # pierwszy lump daleko za plikiem
    raises(rd(bytes(b)), 'poza plik', 'lump za koncem pliku')
    b = bytearray(ok)
    struct.pack_into('<II', b, d, 0, 0x7FFFFFFF)        # dlugosc absurdalna
    raises(rd(bytes(b)), 'poza plik', 'lump dlugosci 2 GB')

    def ident(blob):
        return lambda: w.identify(*w.read_wad(save('i.wad', blob))[:2])
    raises(ident(wadbytes('PWAD', [('E1M1', b'')])), 'PWAD', 'PWAD odrzucony')
    raises(ident(make_iwad('shareware', maps=['MAP01'])), 'DOOM II', 'DOOM II odrzucony')
    raises(ident(make_iwad('freedoom', maps=['MAP01'])), 'Phase 2', 'Freedoom Phase 2 odrzucony')
    raises(ident(make_iwad('freedoom', extra=[('FREEDM', b'')])), 'FreeDM', 'FreeDM odrzucony')
    raises(ident(make_iwad('shareware', drop=('STBAR', 'STARMS'))), 'Nieznany IWAD', 'Heretic-podobny (bez STBAR) odrzucony')
    raises(ident(make_iwad('shareware', maps=[f'E1M{i}' for i in range(1, 6)])), 'Nieznany IWAD', 'Chex-podobny (E1M1-E1M5) odrzucony')
    raises(ident(make_iwad('shareware', drop=('F_START',))), 'F_START', 'brak F_START')
    check(w.identify(*w.read_wad(save('f.wad', make_iwad('shareware', maps=[f'E{e}M{i}' for e in (1, 2, 3) for i in range(1, 10)])))[:2]) == 'doom-pelna', 'pelna wersja')

    raises(lambda: load(make_iwad('shareware', drop=('STFGOD0',))), 'STFGOD0', 'brak twarzy paska')
    raises(lambda: load(make_iwad('shareware', drop=('STTPRCNT',))), 'STTPRCNT', 'brak STTPRCNT')

    def corrupt(name, fn):
        return lambda L: [(n, fn(d) if n == name else d) for n, d in L]
    raises(lambda: load(make_iwad('shareware', edit=corrupt('STTNUM3', lambda d: d[:8] + struct.pack('<I', 99999) + d[12:]))),
           'STTNUM3', 'cyfra paska z kolumna poza lumpem')
    raises(lambda: load(make_iwad('shareware', edit=corrupt('STFST01', lambda d: pat(110, 29, 1, lo=-5, to=-2)))),
           'STFST01', 'twarz za szeroka na pasek (siega tabeli amunicji)')
    raises(lambda: load(make_iwad('shareware', edit=corrupt('STKEYS4', lambda d: pat(7, 30, 1)))),
           'STKEYS4', 'klucz za wysoki (wyszedlby pod pasek)')
    raises(lambda: load(make_iwad('shareware', edit=corrupt('TEXTURE1', lambda d: struct.pack('<i', 10 ** 6) + d[4:]))),
           'TEXTURE1', 'TEXTURE1 z absurdalna liczba tekstur')
    raises(lambda: load(make_iwad('shareware', edit=corrupt('PNAMES', lambda d: struct.pack('<I', 10 ** 6) + d[4:]))),
           'PNAMES', 'PNAMES z absurdalna liczba nazw')
    raises(lambda: load(make_iwad('shareware', edit=corrupt('FLOOR1', lambda d: d[:100]))), 'FLOOR1', 'flat 100 B')
    raises(lambda: load(make_iwad('shareware', edit=corrupt('PLAYPAL', lambda d: d[:768]))), 'PLAYPAL', 'PLAYPAL z 1 paleta')
    raises(lambda: load(make_iwad('shareware', edit=corrupt('COLORMAP', lambda d: d[:256]))), 'COLORMAP', 'krotki COLORMAP')
    def long_post(d):      # M_DOOM 100x40, ale post w kolumnie 0 ma 200 wierszy
        b = bytearray(d)
        o = struct.unpack_from('<I', b, 8)[0]
        b[o + 1] = 200
        return bytes(b) + b'\0' * 200
    s = load(make_iwad('shareware', edit=corrupt('M_DOOM', long_post)))
    raises(lambda: w.assemble(s, ['E1M1']), 'M_DOOM', 'grafika menu z postem dluzszym niz latka')
    s = load(make_iwad('shareware', edit=corrupt('TITLEPIC', lambda d: pat(320, 240, 2))))
    raises(lambda: w.assemble(s, ['E1M1']), 'TITLEPIC', 'grafika wieksza niz ekran')

    # zle mapy: odpadaja z planu; zla E1M1 = koniec
    def map_edit(mapname, child, fn):
        def e(L):
            out, cur = [], None
            for n, d in L:
                if w.is_map_marker(n):
                    cur = n
                elif n not in w.MAP_CHILD:
                    cur = None
                out.append((n, fn(d) if cur == mapname and n == child else d))
            return out
        return e
    bad_line = lambda d: struct.pack('<HHHhhHH', 0, 999, 1, 0, 0, 0, 0xFFFF) + d[14:]
    s = load(make_iwad('shareware', edit=map_edit('E1M2', 'LINEDEFS', bad_line)))
    check(s.maps['E1M2'].problem and 'wierzcholek' in s.maps['E1M2'].problem, 'E1M2 ze zla linia odpada')
    check('E1M2' not in w.plan_max(s, 10 ** 8, False, False), 'zla mapa poza planem')
    raises(lambda: load(make_iwad('shareware', edit=map_edit('E1M1', 'LINEDEFS', bad_line))), 'E1M1', 'zla E1M1 = blad')
    s = load(make_iwad('shareware', edit=map_edit('E1M3', 'BLOCKMAP', lambda d: d[:8] + struct.pack('<H', 40000) + d[10:])))
    check(s.maps['E1M3'].problem and 'BLOCKMAP' in s.maps['E1M3'].problem, 'BLOCKMAP > 32767 slow odpada')
    s = load(make_iwad('shareware', edit=map_edit('E1M3', 'BLOCKMAP', lambda d: d[:12] + struct.pack('<H', 700) + d[14:])))
    check(s.maps['E1M3'].problem and 'BLOCKMAP' in s.maps['E1M3'].problem, 'BLOCKMAP z linia spoza mapy odpada')
    node_cycle = struct.pack('<14h', 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -32768)   # dziecko 0 = sam wezel
    s = load(make_iwad('shareware', edit=map_edit('E1M4', 'NODES', lambda d: node_cycle)))
    check(s.maps['E1M4'].problem and 'cykl' in s.maps['E1M4'].problem, 'cykl w BSP odpada')
    s = load(make_iwad('shareware', edit=map_edit('E1M5', 'SECTORS', lambda d: d[:4] + b'NOSUCH\0\0' + d[12:])))
    check(s.maps['E1M5'].problem and 'NOSUCH' in s.maps['E1M5'].problem, 'nieistniejacy flat odpada')
    s = load(make_iwad('shareware', edit=map_edit('E1M6', 'THINGS', lambda d: struct.pack('<hhhhh', 0, 0, 0, 3001, 7))))
    check(s.maps['E1M6'].problem and 'gracza' in s.maps['E1M6'].problem, 'mapa bez startu gracza odpada')
    s = load(make_iwad('shareware', edit=map_edit('E1M7', 'SEGS', lambda d: d[:-1])))
    check(s.maps['E1M7'].problem and 'SEGS' in s.maps['E1M7'].problem, 'SEGS zlej dlugosci odpada')
    s = load(make_iwad('shareware', edit=map_edit('E1M8', 'REJECT', lambda d: b'')))
    check(not s.maps['E1M8'].problem and len(s.maps['E1M8'].conv[9].data) == 1, 'krotki REJECT dopelniony')

    # nieuzywany flat o nazwie znacznika Plutonii nie moze przestawic silnika w tryb DOOM II
    def add_wfall(L):
        i = [n for n, _ in L].index('F1_END')
        return L[:i] + [('WFALL1', b'\7' * 4096)] + L[i:]
    s = load(make_iwad('shareware', edit=add_wfall))
    names = [l.name for l in w.assemble(s, ['E1M1'])[0]]
    check('WFALL1' not in names and 'KOSPUSTY' in names, 'pusty WFALL1 przemianowany')

    # brak HELP2/CREDIT/WIMAP0 w WAD-zie: i tak musza byc w wyniku (silnik woluje je z nazwy)
    s = load(make_iwad('shareware', drop=('HELP2', 'CREDIT', 'WIMAP0')))
    b = w.kwad_bytes(w.assemble(s, ['E1M1'])[0])
    by = {n: (fp, sz) for n, fp, sz in dir_of(b)}
    check(all(n in by for n in ('HELP2', 'CREDIT', 'WIMAP0')) and by['HELP2'] == by['CREDIT'] == by['WIMAP0'],
          'HELP2/CREDIT/WIMAP0 dopisane jako czarna latka')

    # zlosliwe struktury, ktore bez zapamietywania pozycji kosztowalyby kwadrat dlugosci lumpu
    import time
    chain = b''.join(bytes([0, 0, 0, 0]) for _ in range(20000)) + b'\xff'      # 20 000 pustych postow
    evil = struct.pack('<hhhh', 4096, 8, 0, 0) + b''.join(struct.pack('<I', 8 + 4 * 4096 + 4 * (k % 20000))
                                                          for k in range(4096)) + chain
    t0 = time.time()
    raises(lambda: w.patch_check(evil, 'ZLA'), 'postow', 'latka z tysiacami pustych postow')
    check(time.time() - t0 < 2, f'zla latka sprawdzona szybko ({time.time() - t0:.1f} s)')
    cells = 1000
    big = struct.pack('<hhhh', 0, 0, cells, 1) + b''.join(struct.pack('<H', 4 + cells + k) for k in range(cells))
    big += struct.pack('<H', 0) * 30000 + struct.pack('<H', 0xFFFF)       # jedna dluga lista, linia 0
    t0 = time.time()
    s = load(make_iwad('shareware', edit=map_edit('E1M2', 'BLOCKMAP', lambda d: big)))
    check(not s.maps['E1M2'].problem and time.time() - t0 < 5,
          f'BLOCKMAP z zachodzacymi listami: bez bledu i szybko ({time.time() - t0:.1f} s, {s.maps["E1M2"].problem})')

    # dwa razy ta sama mapa, niebo, pusty uzywany flat
    def dup_map(L):
        i = [n for n, _ in L].index('E1M2')
        return L[:i] + L[i:i + 11] + L[i:i + 11] + L[i + 11:]
    raises(lambda: load(make_iwad('shareware', edit=dup_map)), 'dwa razy', 'mapa E1M2 dwa razy')
    s = load(make_iwad('shareware', edit=corrupt('FLOOR2', lambda d: b'')))    # E1M2 stoi na FLOOR2
    raises(lambda: w.assemble(s, ['E1M1', 'E1M2']), 'FLOOR2', 'pusty flat uzywany przez mape')
    check(len(w.assemble(s, ['E1M1'])[0]) > 0, 'pusty, ale nieuzywany flat nie przeszkadza')
    def no_sky(L):
        return [(n, texture_lump([('AASTINKY', [4]), ('WALL1', [0]), ('WALL2', [1]), ('WALL3', [5])])
                 if n == 'TEXTURE1' else d) for n, d in L]
    raises(lambda: load(make_iwad('shareware', edit=no_sky)), 'SKY1', 'brak tekstury SKY1')

    # identyczne tresci dziela miejsce
    s = load(make_iwad('shareware'))
    b = w.kwad_bytes(w.assemble(s, ['E1M1'])[0])
    by = {n: (fp, sz) for n, fp, sz in dir_of(b)}
    check(by['PLAYA1'][0] == by['PLAYB1'][0], 'identyczne sprite\'y w jednym miejscu')


def main():
    for t in (test_basics, test_shareware, test_freedoom, test_plan, test_cli, test_bad_inputs):
        try:
            t()
        except Exception as e:      # noqa: BLE001
            import traceback
            traceback.print_exc()
            check(False, f'{t.__name__}: wyjatek {type(e).__name__}: {e}')
    print('wad2kos: ' + ('OK' if not FAILS else f'{FAILS} bledow'))
    sys.exit(1 if FAILS else 0)


if __name__ == '__main__':
    main()
