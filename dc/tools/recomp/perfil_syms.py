"""M8: la tabla de simbolos del muestreador (make dc-perfil). Lee la salida de `sh-elf-nm -n` del ELF
y escribe C con un array de datos: al enlazarlo no se mueve .text. Con --compara comprueba justo eso.

    perfil_syms.py NM.txt OUT.c
    perfil_syms.py --compara NM1.txt NM2.txt
"""
import sys


def texto(path):
    out = []
    for line in open(path):
        p = line.split()
        if len(p) == 3 and p[1] in 'Tt' and not p[2].startswith('.'):
            out.append((int(p[0], 16), p[2][1:] if p[2].startswith('_') else p[2]))
    return out


if sys.argv[1] == '--compara':
    a, b = texto(sys.argv[2]), texto(sys.argv[3])
    if a != b:
        d = next((x, y) for x, y in zip(a, b) if x != y) if len(a) == len(b) else (len(a), len(b))
        sys.exit(f'la tabla de simbolos ha movido el codigo: {d}')
    print(f'.text identico: {len(a)} simbolos')
    sys.exit(0)

sim = []
for addr, name in texto(sys.argv[1]):
    if name.startswith('f_'):
        # f_X es un salto a f_X_at: solo cuenta la de verdad, con el nombre corto
        if not name.endswith('_at'):
            continue
        name = name[:-3]
    sim.append((addr, name[:19]))
with open(sys.argv[2], 'w') as fh:
    fh.write('#include <stdint.h>\n')
    fh.write('typedef struct { uint32_t addr; char name[20]; } PerfilSim;\n')
    fh.write(f'const uint32_t perfil_tab_n = {len(sim)}u;\n')
    fh.write('const PerfilSim perfil_tab[] = {\n')
    for addr, name in sim:
        fh.write(f'    {{ {addr:#x}u, "{name}" }},\n')
    fh.write('};\n')
print(f'{len(sim)} simbolos')
