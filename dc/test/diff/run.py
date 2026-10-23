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

    os.makedirs(OUT, exist_ok=True)
    c_dump = os.path.join(OUT, 'c.d1')
    rust_dump = os.path.join(OUT, 'rust.d1')
    # La lista de dibujo 2D: el oraculo de la grabacion de M2a. Va con el mismo guion y
    # en la misma ejecucion, asi que no cuesta nada tenerlo siempre puesto.
    c_ovl = os.path.join(OUT, 'c.ovl')
    rust_ovl = os.path.join(OUT, 'rust.ovl')
    t0 = time.time() - 1          # margen por la resolucion del mtime

    if solo != 'rust':
        print('lado C:')
        # --unlock: las dos partes con pistas y coches disponibles, que es el
        # comportamiento por defecto de la referencia (unlock_everything). No es
        # cosmetico: con todo bloqueado el juego no deja entrar a una carrera, asi que
        # sin esto no existe el escenario de carrera que pide el criterio de M1a. En el
        # port la bandera esta apagada por defecto (ver dc/src/host/unlock.c).
        run([C_EXE, '--script', script, '--ticks', ticks, '--unlock', '--dump', c_dump,
             '--overlaydump', c_ovl],
            {}, os.path.join(OUT, 'c.log'), 'C')
        n = fresh(c_dump, t0, 'C')
        print(f'  {n} frames')

    if solo != 'c':
        print('referencia Rust:')
        run([RUST_EXE, 'boot-main'],
            {'NFSHS_DIFF_SCRIPT': script,
             'NFSHS_DIFF_OUT': rust_dump,
             'NFSHS_DIFF_OVERLAY': rust_ovl,
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
    return rc or rc2


sys.exit(main(sys.argv[1:]))
