"""Biseca en que llamada empieza a divergir la RAM entre el port en C y la referencia.

La secuencia de llamadas dice donde los dos lados hacen cosas distintas, pero una
diferencia de **datos** puede llevar muchas llamadas sin cambiar el flujo. Esto vuelca
los 2 MB de RAM en la misma llamada en los dos lados y compara, buscando por biseccion la
primera llamada en la que ya difieren.

    uso: python -I dc/test/diff/bisect.py [--script FILE] [--lo N] [--hi N]
         python -I dc/test/diff/bisect.py --at N        (una sola sonda)
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    '..', '..', '..'))
OUT = os.path.join(ROOT, 'dc', 'test', 'diff', 'out')
C_EXE = os.path.join(ROOT, 'dc', 'build', 'nfshs-dc')
RUST_EXE = os.path.join(ROOT, 'target', 'release', 'nfshs.exe')
PAGE = 4096


def probe(script, at):
    """Vuelca la RAM en la llamada `at` en los dos lados. Devuelve las paginas que
    difieren, o None si algun lado no llego a esa llamada."""
    c_ram = os.path.join(OUT, 'c_ram.bin')
    r_ram = os.path.join(OUT, 'rust_ram.bin')
    for p in (c_ram, r_ram):
        if os.path.exists(p):
            os.remove(p)

    subprocess.run([C_EXE, '--script', script, '--ticks', '2000000000',
                    '--unlock', '--ramdump', str(at), c_ram],
                   cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=1800)
    env = dict(os.environ)
    env.update({'NFSHS_DIFF_SCRIPT': script,
                'NFSHS_DIFF_OUT': os.path.join(OUT, 'rust_bisect.d1'),
                'NFSHS_RAMDUMP': f'{at}:{r_ram}',
                'TICKS': '2000000000'})
    subprocess.run([RUST_EXE, 'boot-main'], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800)

    if not (os.path.exists(c_ram) and os.path.exists(r_ram)):
        return None
    a = open(c_ram, 'rb').read()
    b = open(r_ram, 'rb').read()
    return [p for p in range(len(a) // PAGE)
            if a[p * PAGE:(p + 1) * PAGE] != b[p * PAGE:(p + 1) * PAGE]]


def main(argv):
    script = 'dc/test/diff/scripts/first_frames.txt'
    if '--script' in argv:
        script = argv[argv.index('--script') + 1]
    if '--at' in argv:
        at = int(argv[argv.index('--at') + 1])
        d = probe(script, at)
        if d is None:
            print(f'llamada {at}: algun lado no llego ahi')
            return 1
        print(f'llamada {at}: {len(d)} paginas distintas'
              + (f'  primeras: {d[:6]}' if d else '  (identicas)'))
        return 0

    lo = int(argv[argv.index('--lo') + 1]) if '--lo' in argv else 1000
    hi = int(argv[argv.index('--hi') + 1]) if '--hi' in argv else 15940

    d = probe(script, lo)
    if d is None:
        print(f'la sonda baja ({lo}) no se alcanzo; sube --lo')
        return 1
    if d:
        print(f'ya difieren en la llamada {lo} ({len(d)} paginas): baja --lo')
        return 1
    print(f'llamada {lo}: identicas ({len(d)} paginas)')

    d = probe(script, hi)
    if not d:
        print(f'llamada {hi}: identicas; sube --hi')
        return 1
    print(f'llamada {hi}: {len(d)} paginas distintas\n')

    while hi - lo > 1:
        mid = (lo + hi) // 2
        d = probe(script, mid)
        if d is None:
            print(f'  {mid}: no alcanzada, bajando el tope')
            hi = mid
            continue
        print(f'  {mid}: {len(d)} paginas distintas' + ('' if d else '  (identicas)'))
        if d:
            hi = mid
        else:
            lo = mid

    print(f'\nLa RAM empieza a divergir en la llamada {hi} '
          f'(identica en la {lo}).')
    d = probe(script, hi)
    print(f'paginas: {[hex(p * PAGE) for p in d[:8]]}')
    return 0


sys.exit(main(sys.argv[1:]))
