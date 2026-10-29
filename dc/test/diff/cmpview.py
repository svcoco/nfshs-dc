"""Compara la vista de carrera y los objetos del escenario del port contra la referencia.

Es el oraculo de la vista (M2b paso 1), los objetos (M2c paso 1) y los coches
(M2c paso 3), y de las marcas de neumatico y los destellos (M2c paso 6). Los coches se comparan **en crudo**, palabra a palabra (99 por coche),
que es mas estricto que comparar campos. El juego
no construye el mundo en su RAM: deja preparada la camara, dice que trozos de pista se
ven y **dibuja** los objetos. Si esta captura coincide, el port tiene los datos correctos
aunque todavia no los dibuje.

Formato en dc/src/host/game.c y src/dev/mod.rs (NFSHS_DIFF_VIEW), version 6: desde M5 los
objetos, los destellos, el sol y el clima van en cada vista, para comparar tambien la
segunda a pantalla partida.

    uso: python -I dc/test/diff/cmpview.py <c.view> <rust.view> [--max N]
"""
import struct
import sys

CUADRO = 56   # un cuadro 2D, como en el volcado de la lista de dibujo


def cuadros(d, o, n):
    """n cuadros 2D en crudo (se comparan byte a byte)."""
    return [d[o + CUADRO * i:o + CUADRO * (i + 1)] for i in range(n)], o + CUADRO * n


def leer(path):
    """Devuelve {frame: (vistas, coches, marcas)}; cada vista con lo suyo."""
    d = open(path, 'rb').read()
    if d[:4] != b'NFSV':
        raise SystemExit(f'{path}: no es un volcado de vista de carrera')
    version, = struct.unpack_from('<I', d, 4)
    if version != 6:
        raise SystemExit(f'{path}: version {version}, esperaba 6')
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
            n, = struct.unpack_from('<I', d, o); o += 4
            objetos = []
            for _ in range(n):
                model, bb = struct.unpack_from('<HB', d, o)
                mat = struct.unpack_from('<9i', d, o + 4)
                pos = struct.unpack_from('<3i', d, o + 40)
                o += 52
                objetos.append((model, bb, mat, pos))
            v['objetos'] = objetos
            n, = struct.unpack_from('<I', d, o); o += 4
            destellos = []
            for _ in range(n):
                ancla = struct.unpack_from('<3i', d, o)
                coche, humo, nq = struct.unpack_from('<IBxH', d, o + 12)
                o += 20
                qs, o = cuadros(d, o, nq)
                destellos.append((ancla, coche, humo, qs))
            v['destellos'] = destellos
            n, = struct.unpack_from('<I', d, o); o += 4
            v['sol'], o = cuadros(d, o, n)
            n, = struct.unpack_from('<I', d, o); o += 4
            v['clima'], o = cuadros(d, o, n)
            vistas.append(v)
        n, = struct.unpack_from('<I', d, o); o += 4
        coches = struct.unpack_from('<%dI' % n, d, o) if n else ()
        o += 4 * n
        n, = struct.unpack_from('<I', d, o); o += 4
        marcas = []
        for _ in range(n):
            marcas.append(struct.unpack_from('<12i2IHh', d, o))
            o += 12 * 4 + 2 * 4 + 4
        frames[frame] = (vistas, coches, marcas)
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
    partidas = sum(len(a[f][0]) > 1 for f in comunes)
    por_vista = [v for f in comunes for v in a[f][0]]
    objetos = sum(len(v['objetos']) for v in por_vista)
    coches = sum(len(a[f][1]) // 99 for f in comunes)
    marcas = sum(len(a[f][2]) for f in comunes)
    destellos = sum(len(v['destellos']) for v in por_vista)
    humo = sum(sum(g[2] for g in v['destellos']) for v in por_vista)
    halos = sum(sum(g[1] != 0 for g in v['destellos']) for v in por_vista)
    sol = sum(len(v['sol']) for v in por_vista)
    clima = sum(len(v['clima']) for v in por_vista)
    if vistas == 0 and coches == 0 and marcas == 0:
        print('  sin vistas de carrera en el guion: nada que comparar')
        return 0

    fallos = 0
    for f in comunes:
        (va, ca, ma), (vb, cb, mb) = a[f], b[f]
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
            oa, ob = x['objetos'], y['objetos']
            if oa != ob:
                if len(oa) != len(ob):
                    print(f'  frame {f} vista {i}: {len(oa)} objetos contra {len(ob)}')
                else:
                    k = next(j for j in range(len(oa)) if oa[j] != ob[j])
                    print(f'  frame {f} vista {i} objeto {k} de {len(oa)}:')
                    print(f'    C   {oa[k]}')
                    print(f'    ref {ob[k]}')
                fallos += 1
            ga, gb = x['destellos'], y['destellos']
            if ga != gb:
                if len(ga) != len(gb):
                    print(f'  frame {f} vista {i}: {len(ga)} destellos contra {len(gb)}')
                else:
                    k = next(j for j in range(len(ga)) if ga[j] != gb[j])
                    p, q = ga[k], gb[k]
                    print(f'  frame {f} vista {i} destello {k} de {len(ga)}: ancla {p[0]} / {q[0]}, '
                          f'coche {p[1]:#x} / {q[1]:#x}, humo {p[2]} / {q[2]}, '
                          f'{len(p[3])} / {len(q[3])} cuadros')
                fallos += 1
            for nombre in ('sol', 'clima'):
                p, q = x[nombre], y[nombre]
                if p != q:
                    print(f'  frame {f} vista {i}: {nombre}, {len(p)} cuadros contra {len(q)}'
                          + ('' if len(p) != len(q) else ' (distintos)'))
                    fallos += 1
        if ca != cb:
            if len(ca) != len(cb):
                print(f'  frame {f}: {len(ca) // 99} coches contra {len(cb) // 99}')
            else:
                k = next(i for i in range(len(ca)) if ca[i] != cb[i])
                print(f'  frame {f} coche {k // 99}, palabra {k % 99}: '
                      f'C {ca[k]:#x} contra {cb[k]:#x}')
            fallos += 1
        if ma != mb:
            if len(ma) != len(mb):
                print(f'  frame {f}: {len(ma)} marcas contra {len(mb)}')
            else:
                k = next(i for i in range(len(ma)) if ma[i] != mb[i])
                print(f'  frame {f} marca {k} de {len(ma)}:')
                print(f'    C   {ma[k]}')
                print(f'    ref {mb[k]}')
            fallos += 1
        if fallos >= tope:
            print(f'  ... (cortado en {tope} diferencias)')
            break

    if fallos:
        print(f'\nFALLO: {fallos} diferencias en la vista, los objetos, los coches, las marcas o los destellos')
        return 1
    print(f'\nOK: {vistas} vistas ({partidas} frames a pantalla partida), {objetos} objetos, '
          f'{coches} coches y {marcas} marcas identicos en {len(comunes)} frames')
    print(f'OK: {destellos} destellos identicos ({halos} halos de coche, {humo} bocanadas de humo), '
          f'{sol} cuadros del flare del sol y {clima} de clima')
    return 0


sys.exit(main(sys.argv[1:]))
