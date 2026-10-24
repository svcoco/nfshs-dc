"""Compara la vista de carrera y los objetos del escenario del port contra la referencia.

Es el oraculo de la vista (M2b paso 1), los objetos (M2c paso 1) y los coches
(M2c paso 3). Los coches se comparan **en crudo**, palabra a palabra (99 por coche),
que es mas estricto que comparar campos. El juego
no construye el mundo en su RAM: deja preparada la camara, dice que trozos de pista se
ven y **dibuja** los objetos. Si esta captura coincide, el port tiene los datos correctos
aunque todavia no los dibuje.

Formato en dc/src/host/game.c y src/dev/mod.rs (NFSHS_DIFF_VIEW), version 2.

    uso: python -I dc/test/diff/cmpview.py <c.view> <rust.view> [--max N]
"""
import struct
import sys


def leer(path):
    """Devuelve {frame: (vistas, objetos)}."""
    d = open(path, 'rb').read()
    if d[:4] != b'NFSV':
        raise SystemExit(f'{path}: no es un volcado de vista de carrera')
    version, = struct.unpack_from('<I', d, 4)
    if version != 3:
        raise SystemExit(f'{path}: version {version}, esperaba 3')
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
        n, = struct.unpack_from('<I', d, o); o += 4
        objetos = []
        for _ in range(n):
            model, bb = struct.unpack_from('<HB', d, o)
            mat = struct.unpack_from('<9i', d, o + 4)
            pos = struct.unpack_from('<3i', d, o + 40)
            o += 52
            objetos.append((model, bb, mat, pos))
        n, = struct.unpack_from('<I', d, o); o += 4
        coches = struct.unpack_from('<%dI' % n, d, o) if n else ()
        o += 4 * n
        frames[frame] = (vistas, objetos, coches)
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

    vistas = sum(len(a[f][0]) for f in comunes)
    objetos = sum(len(a[f][1]) for f in comunes)
    coches = sum(len(a[f][2]) // 99 for f in comunes)
    if vistas == 0 and objetos == 0 and coches == 0:
        print('  sin vistas de carrera en el guion: nada que comparar')
        return 0

    fallos = 0
    for f in comunes:
        (va, oa, ca), (vb, ob, cb) = a[f], b[f]
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
        if oa != ob:
            if len(oa) != len(ob):
                print(f'  frame {f}: {len(oa)} objetos contra {len(ob)}')
            else:
                k = next(i for i in range(len(oa)) if oa[i] != ob[i])
                print(f'  frame {f} objeto {k} de {len(oa)}:')
                print(f'    C   {oa[k]}')
                print(f'    ref {ob[k]}')
            fallos += 1
        if ca != cb:
            if len(ca) != len(cb):
                print(f'  frame {f}: {len(ca) // 99} coches contra {len(cb) // 99}')
            else:
                k = next(i for i in range(len(ca)) if ca[i] != cb[i])
                print(f'  frame {f} coche {k // 99}, palabra {k % 99}: '
                      f'C {ca[k]:#x} contra {cb[k]:#x}')
            fallos += 1
        if fallos >= tope:
            print(f'  ... (cortado en {tope} diferencias)')
            break

    if fallos:
        print(f'\nFALLO: {fallos} frames con la vista, los objetos o los coches distintos')
        return 1
    print(f'\nOK: {vistas} vistas, {objetos} objetos y {coches} coches identicos '
          f'en {len(comunes)} frames')
    return 0


sys.exit(main(sys.argv[1:]))
