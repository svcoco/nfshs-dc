"""Compara la lista de dibujo 2D del port contra la de la referencia.

Es el oraculo del paso 1 de M2a: los dos lados graban lo que el juego quiere pintar, y
si la grabacion del port es correcta las dos listas tienen que coincidir cuadro a
cuadro. Formato en dc/src/host/overlay.c y src/dev/mod.rs (NFSHS_DIFF_OVERLAY).

    uso: python -I dc/test/diff/cmpovl.py <c.ovl> <rust.ovl> [--max N]
"""
import struct
import sys

Q = 56   # bytes por cuadro
COCHE = 244   # un coche del showroom (version 2)
HUD = 120     # un comando de la pausa (version 3): posicion y parametros


def leer(path):
    d = open(path, 'rb').read()
    if d[:4] != b'NFSO':
        raise SystemExit(f'{path}: no es un volcado de lista de dibujo')
    version, = struct.unpack_from('<I', d, 4)
    o = 8
    frames = []
    while o + 12 <= len(d):
        frame, nq, otros = struct.unpack_from('<III', d, o)
        o += 12
        if o + Q * nq > len(d):
            break
        cuadros = d[o:o + Q * nq]
        o += Q * nq
        coches, suelos, huds = [], [], []
        if version >= 2:
            # el showroom: coches (posicion en la lista y sus datos) y suelos (posicion,
            # cuantos cuadros y los cuadros)
            n, = struct.unpack_from('<I', d, o)
            o += 4
            for _ in range(n):
                coches.append(d[o:o + COCHE])
                o += COCHE
            n, = struct.unpack_from('<I', d, o)
            o += 4
            for _ in range(n):
                at, nq2 = struct.unpack_from('<II', d, o)
                suelos.append((at, d[o + 8:o + 8 + Q * nq2]))
                o += 8 + Q * nq2
        if version >= 3:
            # la pausa: Text, Shape, ThinBox, PauseBox y Rect, con su posicion
            n, = struct.unpack_from('<I', d, o)
            o += 4
            for _ in range(n):
                huds.append(d[o:o + HUD])
                o += HUD
        frames.append((frame, otros, cuadros, coches, suelos, huds))
    return frames


def cmd(b):
    pts = struct.unpack_from('<8h', b, 0)
    col = struct.unpack_from('<4I', b, 16)
    kind, tex, semi, abr = b[32], b[33], b[34], b[35]
    tpage, clut = struct.unpack_from('<2H', b, 36)
    uv = struct.unpack_from('<8H', b, 40)
    if kind == 1:                       # recorte: solo origen y tamano
        return (kind, pts[:4])
    return kind, pts, col, tex, semi, abr, tpage, clut, uv


def desc(q):
    if q[0] == 1:
        x, y, w, h = q[1]
        return f'recorte {x},{y} {w}x{h}'
    _, pts, col, tex, semi, abr, tpage, clut, uv = q
    s = f'pts {pts} col {[hex(c) for c in col]} semi {semi} abr {abr}'
    return s + (f' tex tpage {tpage:#x} clut {clut:#x} uv {uv}' if tex else ' sin textura')


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    maxdif = 5
    if '--max' in sys.argv:
        maxdif = int(sys.argv[sys.argv.index('--max') + 1])
    a, b = leer(args[0]), leer(args[1])
    print(f'C: {len(a)} frames, referencia: {len(b)} frames')
    n = min(len(a), len(b))
    tot_c = sum(len(f[2]) // Q for f in a)
    tot_r = sum(len(f[2]) // Q for f in b)
    # los "otros" de la referencia incluyen el showroom y la pausa, comparados aparte
    otros = sum(f[1] - len(f[3]) - len(f[4]) - len(f[5]) for f in b)
    print(f'comandos (cuadros y recortes): C {tot_c}, referencia {tot_r}')
    print(f'comandos de la referencia que el port no produce todavia: {otros}')

    coches = sum(len(f[3]) for f in a)
    suelos = sum(len(f[4]) for f in a)
    huds = sum(len(f[5]) for f in a)
    malos = 0
    for i in range(n):
        fa, fb = a[i], b[i]
        if fa[3] != fb[3] or fa[4] != fb[4]:
            print(f'\nframe {fa[0]}: showroom distinto: {len(fa[3])} coches y {len(fa[4])} suelos '
                  f'contra {len(fb[3])} y {len(fb[4])}')
            for k, (x, y) in enumerate(zip(fa[3], fb[3])):
                if x != y:
                    j = next(j for j in range(len(x)) if x[j] != y[j])
                    print(f'  coche {k}: primer byte distinto {j} (C {x[j]} / ref {y[j]})')
                    break
            for k, (x, y) in enumerate(zip(fa[4], fb[4])):
                if x != y:
                    print(f'  suelo {k}: en {x[0]} / {y[0]}, {len(x[1]) // Q} / {len(y[1]) // Q} cuadros')
                    break
            malos += 1
            if malos >= maxdif:
                break
        if fa[5] != fb[5]:
            print(f'\nframe {fa[0]}: pausa distinta: {len(fa[5])} comandos contra {len(fb[5])}')
            for k, (x, y) in enumerate(zip(fa[5], fb[5])):
                if x != y:
                    ca = struct.unpack_from('<I8B4hI', x, 0)
                    cb = struct.unpack_from('<I8B4hI', y, 0)
                    print(f'  comando {k}: C {ca} {x[24:24 + x[9]]!r}')
                    print(f'             ref {cb} {y[24:24 + y[9]]!r}')
                    break
            malos += 1
            if malos >= maxdif:
                break
        na, nb = len(fa[2]) // Q, len(fb[2]) // Q
        if na != nb:
            print(f'\nframe {fa[0]}: {na} cuadros contra {nb} de la referencia')
            malos += 1
            if malos >= maxdif:
                break
            continue
        for k in range(na):
            qa = cmd(fa[2][k * Q:(k + 1) * Q])
            qb = cmd(fb[2][k * Q:(k + 1) * Q])
            if qa != qb:
                print(f'\nframe {fa[0]}, cuadro {k} de {na}:')
                print(f'  C    {desc(qa)}')
                print(f'  ref  {desc(qb)}')
                malos += 1
                break
        if malos >= maxdif:
            break
    if malos == 0 and len(a) == len(b):
        print(f'\nOK: {tot_c} cuadros identicos en {n} frames')
        print(f'OK: showroom identico: {coches} coches y {suelos} suelos')
        print(f'OK: pausa identica: {huds} comandos')
        return 0
    print(f'\n{malos} frames con diferencias (de {n} comparados)')
    return 1


if __name__ == '__main__':
    sys.exit(main())
