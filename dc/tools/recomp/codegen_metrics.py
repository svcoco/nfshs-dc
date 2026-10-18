"""Metricas que dimensionan el backend C (no estaban en M0).

Mide, sobre las 3674 funciones recompiladas:
  - funciones con `jr reg` (tabla de salto): en el generador, esas hacen leader de TODAS
    sus instrucciones, asi que la suma de sus instrucciones es la cota superior del coste
    en tablas de salto del switch denso (4 bytes por entrada).
  - funciones que acaban como stub: gen_function lanza ValueError (bltzal/bgezal, special
    no soportado, cop0 raro, opcode desconocido, branch en delay slot) y safe_gen convierte
    la funcion entera en un panic. El conjunto exacto es un invariante de paridad: el
    backend C tiene que lanzar con las mismas palabras de instruccion.
  - leaders por funcion: cuantos casos tendra el switch de entrada.
  - loads con rt == 0: el generador emite `let _ = expr;`, o sea que el acceso SI se
    ejecuta. En C hay que emitir `(void)(...)` y los accesores no pueden ser pure/const.
  - accesos con base $zero: candidatos a resolver la direccion en tiempo de generacion.

Se ejecuta desde cualquier directorio.
"""
import os
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
TOOLS = os.path.join(ROOT, 'tools')
os.chdir(TOOLS)
sys.path.insert(0, TOOLS)
sys.argv = ['codegen_metrics']
import recomp  # noqa: E402

e = recomp.e
for line in open('hle_funcs.txt'):
    if line.strip() and not line.startswith('#'):
        recomp.HLE_SET.add(int(line.split()[0], 16))
funcs = recomp.collect(recomp.all_starts())
known = set(funcs)
recomp.collect_entry_points(funcs)
print(f'funciones: {len(funcs)}')

LOAD_OPS = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x32}
STORE_OPS = {0x28, 0x29, 0x2a, 0x2b, 0x2e, 0x3a}


def leaders_of(f, end, words):
    """Misma regla que el generador: inicio, destinos internos de b/j, pc+8 de cada
    branch, entry points ajenos dentro del rango, y TODAS las instrucciones si hay
    `jr reg`."""
    has_indirect = any((w >> 26) == 0 and (w & 63) == 8 and ((w >> 21) & 31) != 31
                       for w in words.values())
    leaders = {f}
    for pc, w in words.items():
        bi = recomp.branch_info(w, pc)
        if bi:
            if bi[0] in ('b', 'bal', 'j') and bi[2] is not None and f <= bi[2] < end:
                leaders.add(bi[2])
            leaders.add(pc + 8)
    leaders |= {x for x in recomp.ENTRY_POINTS if f <= x < end}
    if has_indirect:
        leaders |= set(words)
    return has_indirect, sorted(x for x in leaders if f <= x < end)


indirect, stubs, leader_counts = [], [], []
instr_indirect = 0
rt0_loads = zero_base = total_mem = 0
stub_reasons = Counter()

for f in funcs:
    end = recomp.func_end(f)
    words = {pc: e.word(pc) for pc in range(f, end, 4)}
    has_indirect, leaders = leaders_of(f, end, words)
    leader_counts.append(len(leaders))
    if has_indirect:
        indirect.append(f)
        instr_indirect += len(words)
    for w in words.values():
        op = w >> 26
        if op in LOAD_OPS or op in STORE_OPS:
            total_mem += 1
            if op in LOAD_OPS and ((w >> 16) & 31) == 0:
                rt0_loads += 1
            if ((w >> 21) & 31) == 0:
                zero_base += 1
    try:
        recomp.gen_function(f, known)
    except ValueError as ex:
        stubs.append(f)
        stub_reasons[str(ex).split(' at ')[0]] += 1

leader_counts.sort()
print(f'\nfunciones con `jr reg` (tabla de salto): {len(indirect)}')
print(f'  suma de sus instrucciones: {instr_indirect} '
      f'-> cota superior de tablas de salto: {instr_indirect * 4 / 1024:.0f} KB')
print(f'  las 5 mayores: ' + ', '.join(
    f'{f:#x} ({(recomp.func_end(f) - f) // 4} instr)'
    for f in sorted(indirect, key=lambda f: -(recomp.func_end(f) - f))[:5]))

print(f'\nfunciones stub (gen_function lanza): {len(stubs)}')
for reason, n in stub_reasons.most_common():
    print(f'  {n:4}  {reason}')
if stubs:
    print('  direcciones: ' + ', '.join(f'{f:#x}' for f in stubs[:20])
          + (' ...' if len(stubs) > 20 else ''))

print(f'\nleaders por funcion: mediana {leader_counts[len(leader_counts) // 2]}, '
      f'max {leader_counts[-1]}, media {sum(leader_counts) / len(leader_counts):.1f}')
print(f'  funciones con > 100 leaders: {sum(1 for n in leader_counts if n > 100)}')

print(f'\naccesos a memoria: {total_mem}')
print(f'  loads con rt == 0 (el acceso se ejecuta igual): {rt0_loads}')
print(f'  con base $zero (direccion constante en generacion): {zero_base} '
      f'({100 * zero_base / total_mem:.1f} %)')
