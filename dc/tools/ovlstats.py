"""Dimensiona la cache de texturas del PVR a partir de una lista de dibujo grabada.

Lee el volcado que escribe `--overlaydump` (formato en dc/src/host/overlay.c) y, por
frame, agrupa los cuadros con textura por su combinacion de pagina y paleta, y mide el
rectangulo de texeles que de verdad usan los UV. Con eso dice cuanta VRAM del PVR
costaria la cache, que es la pregunta que decide el diseno del paso 3 de M2a:

  - guardar la PAGINA entera por combinacion (256x256) es lo caro y lo que hay que
    evitar;
  - guardar solo el RECTANGULO usado, redondeado a potencia de dos (que es lo que el
    PVR admite como textura), es lo que se quiere saber si cabe.

    uso: python -I dc/tools/ovlstats.py dc/test/diff/out/c.ovl
"""
import collections
import struct
import sys

Q = 56   # bytes por comando


def pot(n):
    """La potencia de dos >= n, con el minimo de 8 que pide el PVR."""
    p = 8
    while p < n:
        p *= 2
    return p


def leer(path):
    d = open(path, 'rb').read()
    if d[:4] != b'NFSO':
        raise SystemExit('%s: no es un volcado de lista de dibujo' % path)
    o, frames = 8, []
    while o + 12 <= len(d):
        frame, n, _otros = struct.unpack_from('<III', d, o)
        o += 12
        if o + Q * n > len(d):
            break
        frames.append(d[o:o + Q * n])
        o += Q * n
    return frames


def main():
    if len(sys.argv) != 2:
        raise SystemExit('uso: ovlstats.py <volcado.ovl>')
    frames = leer(sys.argv[1])

    peor_pagina = peor_rect = 0
    peor_combos = 0
    bits_vistos = collections.Counter()
    tam = collections.Counter()
    total_combos = {}

    for datos in frames:
        cajas = {}
        for k in range(len(datos) // Q):
            b = datos[k * Q:(k + 1) * Q]
            kind, tex = b[32], b[33]
            if kind != 0 or not tex:
                continue
            tpage, clut = struct.unpack_from('<2H', b, 36)
            uv = struct.unpack_from('<8H', b, 40)
            us = uv[0::2]
            vs = uv[1::2]
            caja = cajas.get((tpage, clut))
            if caja is None:
                cajas[(tpage, clut)] = [min(us), min(vs), max(us), max(vs)]
            else:
                caja[0] = min(caja[0], min(us))
                caja[1] = min(caja[1], min(vs))
                caja[2] = max(caja[2], max(us))
                caja[3] = max(caja[3], max(vs))

        pagina = len(cajas) * 256 * 256 * 2
        rect = 0
        for (tpage, clut), (u0, v0, u1, v1) in cajas.items():
            w, h = pot(u1 - u0), pot(v1 - v0)
            rect += w * h * 2
            bits_vistos[(tpage >> 7) & 3] += 1
            tam[(w, h)] += 1
            total_combos[(tpage, clut)] = True
        peor_pagina = max(peor_pagina, pagina)
        peor_rect = max(peor_rect, rect)
        peor_combos = max(peor_combos, len(cajas))

    modos = {0: '4 bits', 1: '8 bits', 2: '16 bits', 3: '?'}
    print('frames: %d' % len(frames))
    print('combinaciones (pagina, paleta): %d distintas, %d como maximo a la vez'
          % (len(total_combos), peor_combos))
    print()
    print('VRAM del PVR en el peor frame, a 16 bits por texel:')
    print('  guardando la pagina entera : %8.2f MB   <- no cabe' % (peor_pagina / 1048576))
    print('  guardando el rectangulo    : %8.2f MB' % (peor_rect / 1048576))
    print()
    print('profundidad de las paginas usadas:')
    for m, n in bits_vistos.most_common():
        print('  %-8s %d' % (modos.get(m, '?'), n))
    print()
    print('tamanos de textura mas comunes (ya redondeados a potencia de dos):')
    for (w, h), n in tam.most_common(8):
        print('  %4dx%-4d %d' % (w, h, n))


if __name__ == '__main__':
    main()
