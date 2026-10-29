"""Lanza los dos lados del test diferencial con la misma entrada y los compara.

Existe porque invocarlos a mano con prefijos de variables de entorno y tuberias dio
varios resultados falsos: volcados viejos que parecian nuevos, procesos muertos por
SIGPIPE a mitad de escritura, y trazas que no salian porque el entorno no llegaba al
proceso. Aqui el entorno se pasa explicito, y **se comprueba que cada volcado se acaba
de escribir** antes de comparar nada.

    uso: python -I dc/test/diff/run.py [--script FILE] [--ticks N] [--solo c|rust]
                                       [--keep]
"""
import os
import shutil
import subprocess
import sys
import time

_p = print
def print(*a, **k):      # salida sin buffer, para que el orden con los hijos sea fiable
    k.setdefault('flush', True)
    _p(*a, **k)

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    '..', '..', '..'))
OUT = os.path.join(ROOT, 'dc', 'test', 'diff', 'out')
C_EXE = os.path.join(ROOT, 'dc', 'build', 'nfshs-dc')
RUST_EXE = os.path.join(ROOT, 'target', 'release', 'nfshs.exe')
HDR = 20
REC = 4636


def frames(path):
    n = os.path.getsize(path)
    return (n - HDR) // REC, (n - HDR) % REC


def fresh(path, since, etiqueta):
    """Falla si el volcado no se acaba de escribir: es el error que mas veces me
    engano, comparar contra un archivo de una ejecucion anterior."""
    if not os.path.exists(path):
        raise SystemExit(f'{etiqueta}: no se escribio {path}')
    mt = os.path.getmtime(path)
    if mt < since:
        raise SystemExit(f'{etiqueta}: {path} es de ANTES de esta ejecucion '
                         f'(mtime {time.strftime("%H:%M:%S", time.localtime(mt))}); '
                         'el proceso no lo escribio')
    n, resto = frames(path)
    if n == 0:
        raise SystemExit(f'{etiqueta}: {path} no tiene ni un frame')
    if resto:
        print(f'  aviso: {etiqueta} tiene {resto} bytes de cola incompleta '
              '(proceso cortado a mitad de escritura?)')
    return n


def run(cmd, env_extra, log, etiqueta):
    env = dict(os.environ)
    env.update(env_extra)
    with open(log, 'w', encoding='utf-8', errors='replace') as fh:
        r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=fh,
                           stderr=subprocess.STDOUT, timeout=1800)
    print(f'  {etiqueta}: codigo {r.returncode}, registro en {os.path.relpath(log, ROOT)}')
    return r.returncode


def main(argv):
    script = 'dc/test/diff/scripts/into_race.txt'
    ticks = '2000000000'
    solo = None
    if '--script' in argv:
        script = argv[argv.index('--script') + 1]
    if '--ticks' in argv:
        ticks = argv[argv.index('--ticks') + 1]
    if '--solo' in argv:
        solo = argv[argv.index('--solo') + 1]
    # --tarjeta FILE: los dos lados empiezan con esa memory card (una copia cada uno)
    tarjeta = argv[argv.index('--tarjeta') + 1] if '--tarjeta' in argv else None

    os.makedirs(OUT, exist_ok=True)
    c_dump = os.path.join(OUT, 'c.d1')
    rust_dump = os.path.join(OUT, 'rust.d1')
    # La lista de dibujo 2D: el oraculo de la grabacion de M2a. Va con el mismo guion y
    # en la misma ejecucion, asi que no cuesta nada tenerlo siempre puesto.
    c_ovl = os.path.join(OUT, 'c.ovl')
    rust_ovl = os.path.join(OUT, 'rust.ovl')
    # La vista de carrera: el oraculo del paso 1 de M2b. En el front end no hay
    # ninguna, asi que con un guion de menus los dos lados salen vacios y coinciden.
    c_view = os.path.join(OUT, 'c.view')
    rust_view = os.path.join(OUT, 'rust.view')
    # La memory card (M6): los dos lados empiezan con una tarjeta nueva y al final se comparan
    # las dos imagenes. Sin esto la referencia usaria memcard.mcd junto a gamedata/, que
    # sobrevive de una ejecucion a otra.
    c_mcd = os.path.join(OUT, 'c.mcd')
    rust_mcd = os.path.join(OUT, 'rust.mcd')
    for f in (c_mcd, rust_mcd):
        if os.path.exists(f) and solo is None:
            os.remove(f)
        if tarjeta is not None:
            shutil.copyfile(tarjeta, f)
    t0 = time.time() - 1          # margen por la resolucion del mtime

    if solo != 'rust':
        print('lado C:')
        # --unlock: las dos partes con pistas y coches disponibles, que es el
        # comportamiento por defecto de la referencia (unlock_everything). No es
        # cosmetico: con todo bloqueado el juego no deja entrar a una carrera, asi que
        # sin esto no existe el escenario de carrera que pide el criterio de M1a. En el
        # port la bandera esta apagada por defecto (ver dc/src/host/unlock.c).
        run([C_EXE, '--script', script, '--ticks', ticks, '--unlock', '--dump', c_dump,
             '--overlaydump', c_ovl, '--viewdump', c_view, '--memcard', c_mcd,
             '--carcone', '2'],
            {}, os.path.join(OUT, 'c.log'), 'C')
        n = fresh(c_dump, t0, 'C')
        print(f'  {n} frames')

    if solo != 'c':
        print('referencia Rust:')
        run([RUST_EXE, 'boot-main'],
            {'NFSHS_DIFF_SCRIPT': script,
             'NFSHS_DIFF_OUT': rust_dump,
             'NFSHS_DIFF_OVERLAY': rust_ovl,
             'NFSHS_DIFF_VIEW': rust_view,
             'NFSHS_MEMCARD': rust_mcd,
             'NFSHS_FRAMESRC': '1',
             'TICKS': ticks},
            os.path.join(OUT, 'rust.log'), 'Rust')
        n = fresh(rust_dump, t0, 'Rust')
        print(f'  {n} frames')

    if solo:
        return 0
    print('\ncomparacion:')
    cmp_py = os.path.join(ROOT, 'dc', 'test', 'diff', 'cmp.py')
    rc = subprocess.run([sys.executable, '-I', cmp_py, c_dump, rust_dump],
                        cwd=ROOT).returncode
    print('\nlista de dibujo 2D:')
    cmpovl = os.path.join(ROOT, 'dc', 'test', 'diff', 'cmpovl.py')
    rc2 = subprocess.run([sys.executable, '-I', cmpovl, c_ovl, rust_ovl],
                         cwd=ROOT).returncode
    print('\nvista de carrera:')
    cmpview = os.path.join(ROOT, 'dc', 'test', 'diff', 'cmpview.py')
    rc3 = subprocess.run([sys.executable, '-I', cmpview, c_view, rust_view],
                         cwd=ROOT).returncode
    print('\nmemory card:')
    rc4 = cmp_tarjetas(c_mcd, rust_mcd)
    return rc or rc2 or rc3 or rc4


def cmp_tarjetas(c_mcd, rust_mcd):
    """Las dos imagenes de la tarjeta al terminar: iguales byte a byte, o ninguna escrita."""
    hay = [os.path.exists(f) for f in (c_mcd, rust_mcd)]
    if not any(hay):
        print('  ningun lado escribio en la tarjeta')
        return 0
    if not all(hay):
        print(f'FALLO: solo escribio en la tarjeta {"el lado C" if hay[0] else "la referencia"}')
        return 1
    a, b = open(c_mcd, 'rb').read(), open(rust_mcd, 'rb').read()
    if a != b:
        k = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        print(f'FALLO: las tarjetas difieren desde el byte {k:#x} (bloque {k // 8192}, '
              f'marco {k // 128})')
        return 1
    usados = sum(1 for i in range(1, 16) if a[128 * i] in (0x51, 0x52, 0x53))
    print(f'OK: tarjetas identicas ({usados} bloques usados)')
    return 0


sys.exit(main(sys.argv[1:]))
