"""M4: la API de sonido del juego, por analisis estatico.

1. Las funciones que tocan los registros del SPU (0x1F801C00-0x1F801DFF) o del DMA 4: el
   fondo del driver de EA.
2. Hacia arriba por el grafo de llamadas (`jal` y punteros cargados con lui/addiu): todo lo
   que llega a ellas es "el driver"; se para en las funciones que reciben llamadas desde
   fuera de la region del driver.
3. Se imprimen las funciones del driver que el resto del juego llama, con cuantos sitios las
   llaman: esa es la API que el middleware tiene que atrapar.

Uso (desde la raiz): python -I dc/tools/recomp/snd_api.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.argv = [sys.argv[0]]
import recomp  # noqa: E402

e = recomp.e
HOOKS = {}
for line in open(os.path.join(recomp.UPSTREAM_TOOLS, 'hook_funcs.txt')):
    if line.strip() and not line.startswith('#'):
        a, *rest = line.split(None, 1)
        HOOKS[int(a, 16)] = rest[0].split('(')[0].strip() if rest else ''

funcs = recomp.collect(recomp.all_starts())
calls = {f: recomp.calls_of(f) for f in funcs}
callers = {}
for f, cs in calls.items():
    for t in cs:
        callers.setdefault(t, set()).add(f)


def toca_spu(f):
    """lui rX, 0x1F80 y una carga o escritura con desplazamiento del SPU o del DMA 4."""
    hi = set()
    for q in range(f, recomp.func_end(f), 4):
        w = e.word(q)
        op, rt, rs, imm = w >> 26, (w >> 16) & 31, (w >> 21) & 31, w & 0xFFFF
        if op == 15 and imm == 0x1F80:
            hi.add(rt)
        elif op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B) and rs in hi:
            if 0x1C00 <= imm < 0x1E00 or 0x10C0 <= imm < 0x10D0:
                return True
    return False


# Medido con NFSHS_SNDCALLERS (las cadenas de llamadas que escriben en el SPU): el fondo del
# driver esta en 0x800FF000-0x8010D000 y la capa que llama el juego en 0x800E7000-0x800EB000
# (SNDplay 0x800E861C, SNDSTRM_queuefile 0x800EA594). Esa capa es la API.
API_LO, API_HI = 0x800E7000, 0x800EB000
DRV_LO, DRV_HI = 0x800E7000, 0x8010D000
# Las funciones mas internas de las cadenas que escriben en el SPU (NFSHS_SNDCALLERS, carrera
# larga): la API de verdad es la que llega a alguna de ellas.
ESCRIBEN = {0x800FFBE4, 0x80100AA4, 0x801004A0, 0x8010B4BC, 0x8010B6C0, 0x800FFB3C, 0x800FFD40,
            0x800FFAEC, 0x800FFB14, 0x800FFB7C, 0x80100BB8, 0x801000AC, 0x800EF3D8}
llega = set(ESCRIBEN)
cambio = True
while cambio:
    cambio = False
    for f, cs in calls.items():
        if f not in llega and cs & llega:
            llega.add(f)
            cambio = True
api = {}
for f in funcs:
    if not (API_LO <= f < API_HI) or f not in llega:
        continue
    de_fuera = sorted(c for c in callers.get(f, ()) if not (DRV_LO <= c < DRV_HI))
    if de_fuera:
        api[f] = de_fuera
print(f'funciones de la API de sonido (0x800E7000-0x800EB000, llegan al SPU) llamadas por el juego: {len(api)}')
for f in sorted(api):
    print(f'  {f:#010x}  {len(api[f]):3} llamantes  {HOOKS.get(f, ""):24}  ' +
          ' '.join(f'{c:08x}' for c in api[f][:8]) + (' ...' if len(api[f]) > 8 else ''))
