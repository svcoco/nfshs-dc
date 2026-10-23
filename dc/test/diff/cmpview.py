"""Compara la vista de carrera del port contra la de la referencia.

Es el oraculo del paso 1 de M2b. El juego no construye la malla del mundo en su RAM:
solo deja preparada la camara y la lista de trozos de pista que se ven. Eso es todo lo
que el renderer recibe de el, asi que si esta captura coincide, el port tiene los datos
correctos para dibujar aunque todavia no dibuje.

Formato en dc/src/host/game.c y src/dev/mod.rs (NFSHS_DIFF_VIEW).

    uso: python -I dc/test/diff/cmpview.py <c.view> <rust.view> [--max N]
"""
import struct
import sys


def leer(path):
    """Devuelve {frame: [vista, ...]}, cada vista como un diccionario de campos."""
    d = open(path, 'rb').read()
    if d[:4] != b'NFSV':
        raise SystemExit(f'{path}: no es un volcado de vista de carrera')
    o, frames = 8, {}
    while o + 8 <= len(d):
        frame, nv = struct.unpack_from('<II', d, o)
        o += 8
        vistas = []
        for _ in range(nv):
            v = {}
            v['player'], = struct.unpack_from('<I', d, o); o += 4
            v['rect'] = struct.unpack_from('<4h', d, o); o += 8
            v['pos'] = struct.unpack_from('<3i', d, o); o += 12
            v['rot'] = struct.unpack_from('<9i', d, o); o += 36
            v['proj'], = struct.unpack_from('<I', d, o); o += 4
            n, = struct.unpack_from('<H', d, o); o += 2
            pares = struct.unpack_from('<%dh' % (2 * n), d, o); o += 4 * n
            v['trozos'] = [(pares[2 * i], pares[2 * i + 1] & 0xFFFF) for i in range(n)]
            vistas.append(v)
        frames[frame] = vistas
        if o > len(d):
            raise SystemExit(f'{path}: volcado truncado')
    return frames


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    tope = int(argv[argv.index('--max') + 1]) if '--max' in argv else 20
    a, b = leer(argv[0]), leer(argv[1])

    comunes = sorted(set(a) & set(b))
    if not comunes:
        print('  ningun frame en comun: C %d frames, referencia %d' % (len(a), len(b)))
        return 1
    print('  C: %d frames, referencia: %d frames (%d en comun)'
          % (len(a), len(b), len(comunes)))

    vistas = sum(len(a[f]) for f in comunes)
    if vistas == 0:
        print('  sin vistas de carrera en el guion: nada que comparar')
        return 0

    fallos = 0
    for f in comunes:
        va, vb = a[f], b[f]
        if len(va) != len(vb):
            print(f'  frame {f}: {len(va)} vistas contra {len(vb)}')
            fallos += 1
        for i, (x, y) in enumerate(zip(va, vb)):
            for campo in ('player', 'rect', 'pos', 'rot', 'proj', 'trozos'):
                if x[campo] != y[campo]:
                    print(f'  frame {f} vista {i}: {campo}')
                    print(f'    C   {x[campo]}')
                    print(f'    ref {y[campo]}')
                    fallos += 1
                    break
        if fallos >= tope:
            print(f'  ... (cortado en {tope} diferencias)')
            break

    if fallos:
        print(f'\nFALLO: {fallos} frames con la vista distinta')
        return 1
    print(f'\nOK: {vistas} vistas identicas en {len(comunes)} frames')
    return 0


sys.exit(main(sys.argv[1:]))
