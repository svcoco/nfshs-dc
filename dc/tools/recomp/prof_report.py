"""Informe del perfil por funcion (M8). Une lo estatico (prof_blocks.csv, prof_funcs.csv, que escribe
`recomp.py --prof`) con lo medido (el CSV que escribe nfshs-prof al salir) y da, por frame de
carrera: instrucciones MIPS ejecutadas, accesos a memoria, y que funciones se llevan el peso.

    make -C dc perfil
    NFSHS_PROF=dc/build/prof/out/into_race.csv NFSHS_PROF_FROM=500 \
        dc/build/prof/nfshs-prof --script dc/test/diff/scripts/into_race.txt --unlock
    python -I dc/tools/recomp/prof_report.py dc/build/prof/out/into_race.csv [--top 40] [--csv out.csv]

Todo sale de contar ejecuciones reales de cada bloque basico; el coste en ciclos NO se mide aqui
(el PC no es un SH-4), solo se cuenta trabajo. Los bytes de codigo por instruccion MIPS (14,6 con
-Os, 33,5 con -O2) son los medidos en M1b sobre el codigo generado entero.
"""
import argparse
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, '..', '..', 'build', 'prof', 'gen')


def cargar(run_csv, gen):
    blocks = []
    with open(os.path.join(gen, 'prof_blocks.csv')) as fh:
        for r in csv.DictReader(fh):
            blocks.append((int(r['func'], 16), int(r['instr']), int(r['nop']), int(r['ld']),
                           int(r['st']), int(r['gte']), int(r['br'])))
    funcs = []
    with open(os.path.join(gen, 'prof_funcs.csv')) as fh:
        for r in csv.DictReader(fh):
            funcs.append((int(r['start'], 16), int(r['instrs']), int(r['hot']), r['name']))
    frames = 0
    cnt = {}
    ent = {}
    with open(run_csv) as fh:
        for line in fh:
            p = line.strip().split(',')
            if p[0] == 'frames':
                frames = int(p[1])
            elif p[0] == 'B':
                cnt[int(p[1])] = tuple(int(x) for x in p[2:6])
            elif p[0] == 'F':
                ent[int(p[1])] = int(p[2])
    return blocks, funcs, frames, cnt, ent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run')
    ap.add_argument('--gen', default=GEN)
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--csv')
    a = ap.parse_args()
    blocks, funcs, frames, cnt, ent = cargar(a.run, a.gen)
    if frames <= 0:
        sys.exit('el CSV no trae frames (usa NFSHS_PROF_FROM y una ejecucion que pase de ese frame)')
    findex = {f[0]: i for i, f in enumerate(funcs)}

    # por funcion: instr, nop, ld, st, gte, br, lentos (scratch, io, otros), entradas
    agg = {}
    uniq_instr = 0                       # instrucciones MIPS distintas ejecutadas (huella)
    uniq_by_func = {}
    for i, (c, scr, io, oth) in cnt.items():
        f, n, nop, ld, st, gte, br = blocks[i]
        g = agg.setdefault(f, [0] * 9)
        g[0] += c * n; g[1] += c * nop; g[2] += c * ld; g[3] += c * st; g[4] += c * gte
        g[5] += c * br; g[6] += scr; g[7] += io; g[8] += oth
        uniq_instr += n + nop
        uniq_by_func[f] = uniq_by_func.get(f, 0) + n + nop

    tot = [sum(v[k] for v in agg.values()) for k in range(9)]
    entries = sum(ent.values())
    pf = lambda x: x / frames

    print(f'Perfil de {frames} frames de carrera ({len(agg)} funciones con actividad de {len(funcs)})')
    print()
    print('POR FRAME (todo el codigo recompilado; el host y el GTE no entran)')
    print(f'  instrucciones MIPS ejecutadas ..... {pf(tot[0]):>12,.0f}   (+ {pf(tot[1]):,.0f} nops que no generan codigo)')
    print(f'  loads / stores .................... {pf(tot[2]):>12,.0f} / {pf(tot[3]):,.0f}   = {pf(tot[2] + tot[3]):,.0f} accesos '
          f'({100 * (tot[2] + tot[3]) / tot[0]:.1f} % de las instrucciones)')
    print(f'  ramas y saltos .................... {pf(tot[5]):>12,.0f}')
    print(f'  instrucciones del GTE (cop2) ...... {pf(tot[4]):>12,.0f}')
    print(f'  entradas a funcion ................ {pf(entries):>12,.0f}   ({tot[0] / max(entries, 1):.1f} instrucciones por entrada)')
    lentos = tot[6] + tot[7] + tot[8]
    acc = tot[2] + tot[3]
    print(f'  accesos por la ruta lenta ......... {pf(lentos):>12,.0f}   = {100 * lentos / acc:.1f} % de los accesos '
          f'(scratchpad {pf(tot[6]):,.0f}, E/S {pf(tot[7]):,.0f}, otros {pf(tot[8]):,.0f})')
    print()
    print('HUELLA DE CODIGO (instrucciones MIPS distintas ejecutadas en la ventana)')
    print(f'  {uniq_instr:,} instrucciones = {uniq_instr * 4 / 1024:.0f} KB de MIPS;'
          f' en SH-4 ~{uniq_instr * 14.6 / 1024:.0f} KB con -Os o ~{uniq_instr * 33.5 / 1024:.0f} KB con -O2 '
          f'(estimacion; I-cache = 8 KB)')
    print()

    # concentracion
    orden = sorted(agg.items(), key=lambda kv: -kv[1][0])
    tot_i = tot[0]
    print('CONCENTRACION: cuantas funciones suman cada fraccion de las instrucciones ejecutadas')
    acum, marcas = 0, [0.5, 0.8, 0.9, 0.95, 0.99]
    k = 0
    huella = 0
    salida = []
    for n, (f, v) in enumerate(orden, 1):
        acum += v[0]
        huella += uniq_by_func[f]
        while k < len(marcas) and acum >= marcas[k] * tot_i:
            salida.append(f'  {int(marcas[k] * 100):>3} %  ->  {n:>4} funciones  '
                          f'({huella:,} instr MIPS estaticas ejecutadas, ~{huella * 14.6 / 1024:.0f} KB en -Os)')
            k += 1
    print('\n'.join(salida))
    hot_i = sum(v[0] for f, v in agg.items() if funcs[findex[f]][2])
    print(f'  lista caliente (hot_funcs.txt): {100 * hot_i / tot_i:.1f} % de las instrucciones ejecutadas')
    print()

    print(f'LAS {a.top} FUNCIONES CON MAS INSTRUCCIONES')
    print(f'  {"funcion":<11}{"nombre":<24}{"instr/fr":>10}{"%":>6}{"acum":>6}{"entr/fr":>9}{"i/entr":>8}'
          f'{"acc/fr":>9}{"lent%":>7}{"tam":>6}{"hue":>6} hot')
    acum = 0
    filas = []
    for f, v in orden:
        fi = findex[f]
        e = ent.get(fi, 0)
        accs = v[2] + v[3]
        filas.append((f, funcs[fi][3], pf(v[0]), 100 * v[0] / tot_i, pf(e), v[0] / max(e, 1), pf(accs),
                      100 * (v[6] + v[7] + v[8]) / accs if accs else 0.0, funcs[fi][1], uniq_by_func[f],
                      funcs[fi][2]))
    for f, nm, ipf, pc, epf, ipe, apf, lent, tam, hue, hot in filas[:a.top]:
        acum += pc
        print(f'  {f:#010x}  {nm[:22]:<24}{ipf:>10,.0f}{pc:>6.1f}{acum:>6.1f}{epf:>9,.1f}{ipe:>8,.0f}'
              f'{apf:>9,.0f}{lent:>7.1f}{tam:>6}{hue:>6}  {"si" if hot else ""}')
    print('  (instr/fr = instrucciones MIPS por frame; i/entr = por entrada; lent% = accesos por la ruta lenta;')
    print('   tam = instrucciones de la funcion; hue = las que se ejecutaron en la ventana)')

    if a.csv:
        with open(a.csv, 'w', newline='') as fh:
            w = csv.writer(fh)
            w.writerow(['func', 'nombre', 'instr_frame', 'pct', 'entradas_frame', 'instr_por_entrada',
                        'accesos_frame', 'lentos_pct', 'tam', 'huella', 'hot'])
            for r in filas:
                w.writerow([f'{r[0]:#x}', r[1], f'{r[2]:.1f}', f'{r[3]:.3f}', f'{r[4]:.2f}', f'{r[5]:.1f}',
                            f'{r[6]:.1f}', f'{r[7]:.2f}', r[8], r[9], int(r[10])])
        print(f'\nCSV completo en {a.csv}')


if __name__ == '__main__':
    main()
