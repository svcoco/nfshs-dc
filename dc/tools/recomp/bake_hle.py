"""Hornea tools/hle_funcs.txt en una tabla de C.

En PC el host lee el .txt al arrancar, que esta bien porque el archivo esta al lado.
En Dreamcast no: habria que llevarlo en el disco y parsearlo con sscanf para 119
direcciones que no cambian nunca entre compilaciones. Es dato de compilacion, asi que
se hornea.

Sale en dc/gen/, que esta en .gitignore, igual que el resto del codigo generado.
Ejecutar: python -I dc/tools/recomp/bake_hle.py [dir_de_salida]
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    '..', '..', '..'))
SRC = os.path.join(ROOT, 'tools', 'hle_funcs.txt')


def read_entries(path):
    """Devuelve [(addr, nombre)] unico por direccion, en orden de direccion.

    El archivo tiene 130 lineas y 119 direcciones unicas: hay direcciones repetidas
    con nombres distintos (la misma funcion sirve a varias cosas). Se queda la
    PRIMERA, que es lo mismo que hace host.c al leerlo en ejecucion; si se quedara la
    ultima, los nombres del informe no coincidirian entre las dos rutas.
    """
    seen, out = set(), []
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split(None, 1)
        addr = int(parts[0], 16)
        name = parts[1].strip() if len(parts) > 1 else '(sin nombre)'
        if addr in seen:
            continue
        seen.add(addr)
        out.append((addr, name))
    out.sort(key=lambda e: e[0])
    return out


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'dc', 'gen')
    entries = read_entries(SRC)

    lines = [
        '/* Generado por dc/tools/recomp/bake_hle.py desde tools/hle_funcs.txt.',
        ' * No editar a mano. */',
        '',
        '#include "host.h"',
        '',
        'const HostHleBaked host_hle_baked[] = {',
    ]
    for addr, name in entries:
        if len(name) >= 48:          # el campo de HleFunc
            name = name[:47]
        lines.append('    { %#010xu, "%s" },' % (addr, name.replace('"', '\\"')))
    lines += [
        '};',
        '',
        'const uint32_t host_hle_baked_n = %uu;' % len(entries),
        '',
    ]

    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, 'hle_table.c')
    with open(dst, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(lines))
    print('%s: %d direcciones unicas' % (os.path.relpath(dst, ROOT), len(entries)))


if __name__ == '__main__':
    main()
