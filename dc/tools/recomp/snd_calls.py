"""M4: resumen de un registro NFSHS_SNDAPI: por funcion, llamadas, llamantes y argumentos.

Uso: python -I dc/tools/recomp/snd_calls.py REGISTRO [FUNCION_HEX ...]
"""
import collections
import sys

reg = sys.argv[1]
solo = {int(x, 16) for x in sys.argv[2:]}
n = collections.Counter()
llamantes = collections.defaultdict(collections.Counter)
args = collections.defaultdict(collections.Counter)
frames = set()
for line in open(reg):
    fr, f, c, a0, a1, a2, a3 = line.split()
    f = int(f, 16)
    frames.add(int(fr))
    n[f] += 1
    llamantes[f][c] += 1
    args[f][(a0, a1, a2, a3)] += 1
total = max(frames) - min(frames) + 1 if frames else 1
print(f'{reg}: {sum(n.values())} llamadas en {total} frames')
for f, k in sorted(n.items()):
    if solo and f not in solo:
        continue
    ls = ' '.join(f'{c}x{m}' for c, m in llamantes[f].most_common(4))
    print(f'{f:08x} {k:7} ({k / total:6.2f}/frame)  llamantes {ls}')
    for a, m in args[f].most_common(3 if not solo else 12):
        print(f'           {m:6}x  a0={a[0]} a1={a[1]} a2={a[2]} a3={a[3]}')
