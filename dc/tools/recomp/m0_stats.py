"""M0: cifras de la imagen combinada NFS4.EXE + FRONT.BIN.

Reproduce la tabla de la seccion 6 de CLAUDE.md. Se ejecuta desde cualquier directorio:
el preambulo se situa en <raiz>/tools, que es donde recomp.py espera encontrar
../gamedata/NFS4.EXE, ../gamedata/FRONT.BIN, hle_funcs.txt y hook_funcs.txt.

Salida esperada:
    TOTAL: 3674 funciones, 245518 instr (959 KB)
    HLE que son inicio de funcion: 119/119; hooks: 53/53
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
TOOLS = os.path.join(ROOT, 'tools')
os.chdir(TOOLS)
sys.path.insert(0, TOOLS)
sys.argv = ['m0_stats']
import recomp  # noqa: E402

e = recomp.e
OV0, OV1 = 0x80010000, 0x80010000 + len(recomp._front)
hle = {int(l.split()[0], 16) for l in open('hle_funcs.txt') if l.strip() and not l.startswith('#')}
recomp.HLE_SET |= hle
funcs = recomp.collect(recomp.all_starts())

# regiones de ceros >= 1 KB (hueco del overlay si falta FRONT.BIN, BSS, relleno)
runs, a = [], e.base
while a < e.end:
    if e.word(a) == 0:
        b = a
        while b < e.end and e.word(b) == 0:
            b += 4
        if (b - a) // 4 >= 256:
            runs.append((a, b))
        a = b
    else:
        a += 4
in_zero = lambda p: any(x <= p < y for x, y in runs)
real = [f for f in funcs if not in_zero(f)]
print(f'recompilador: {len(funcs)} funciones; espurias en ceros: {len(funcs) - len(real)}')


def stats(fs, name):
    cov = set()
    for f in fs:
        cov.update(p for p in range(f, recomp.func_end(f), 4) if not in_zero(p))
    c = dict(jal=0, jr=0, jalr=0, ld=0, st=0, cop2=0)
    for pc in cov:
        w = e.word(pc); op = w >> 26
        if op == 0:
            c['jr'] += (w & 63) == 8 and ((w >> 21) & 31) != 31
            c['jalr'] += (w & 63) == 9
        elif op == 3: c['jal'] += 1
        elif op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x32): c['ld'] += 1
        elif op in (0x28, 0x29, 0x2a, 0x2b, 0x2e, 0x3a): c['st'] += 1
        elif op == 0x12: c['cop2'] += 1
    sz = sorted((recomp.func_end(f) - f) // 4 for f in fs) or [0]
    print(f'{name}: {len(fs)} funciones, {len(cov)} instr ({len(cov) * 4 / 1024:.0f} KB), '
          f'mediana {sz[len(sz) // 2]}, max {sz[-1]}, {c}')
    return len(cov)


stats([f for f in real if OV0 <= f < OV1], 'FRONT.BIN')
stats([f for f in real if f >= OV1], 'NFS4.EXE')
total_instr = stats(real, 'TOTAL')
starts = set(recomp.all_starts())
hooks = [int(l.split()[0], 16) for l in open('hook_funcs.txt') if l.strip() and not l.startswith('#')]
print(f'HLE que son inicio de funcion: {len(hle & starts)}/{len(hle)}; '
      f'hooks: {sum(h in starts for h in hooks)}/{len(hooks)}')

# Anclajes de la seccion 6: si una edicion del analisis los rompe, falla ruidosamente
# en vez de dejar pasar cifras nuevas sin que nadie lo note.
EXPECTED_FUNCS, EXPECTED_INSTR = 3674, 245518
if (len(real), total_instr) != (EXPECTED_FUNCS, EXPECTED_INSTR):
    sys.exit(f'\nERROR: se esperaban {EXPECTED_FUNCS} funciones y {EXPECTED_INSTR} '
             f'instrucciones (CLAUDE.md seccion 6); salen {len(real)} y {total_instr}. '
             'Para y avisa antes de seguir.')
print(f'\nOK: anclajes de la seccion 6 reproducidos ({EXPECTED_FUNCS} funciones, '
      f'{EXPECTED_INSTR} instrucciones).')
