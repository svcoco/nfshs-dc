"""M3: compara el MDEC en C con el de la referencia, pelicula entera.

Para cada archivo, decodifica todos los frames con los dos (`nfshs dump-movie FILE all`
y `dc/build/mdec_dump`) y cuenta los bytes RGB distintos y la diferencia maxima. Con
.XA compara tambien el audio contra el WAV de la referencia.

Uso (desde la raiz): python -I dc/test/diff/cmpmdec.py FILE [FILE...]
"""
import os
import subprocess
import sys

NFSHS = os.path.join('target', 'release', 'nfshs')
DUMP = os.path.join('dc', 'build', 'mdec_dump')
OUT = os.path.join('dc', 'build', 'm3')


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit('%s: %s' % (cmd[0], r.stderr.strip()))
    return r.stdout


def main(files):
    os.makedirs(OUT, exist_ok=True)
    bad = 0
    for f in files:
        base = os.path.join(OUT, os.path.basename(f))
        xa = f.upper().endswith('.XA')
        # la referencia: el audio sale con un frame suelto y su WAV
        run([NFSHS, 'dump-movie', f, 'all', base + '.ref.rgba'])
        if xa:
            run([NFSHS, 'dump-movie', f, '0', base + '.ref.png', base + '.ref.wav'])
        info = run([DUMP, f, base + '.c.rgba'] + ([base + '.c.pcm'] if xa else [])).strip()
        a = open(base + '.ref.rgba', 'rb').read()
        b = open(base + '.c.rgba', 'rb').read()
        if len(a) != len(b):
            print('%s: tamanos distintos (ref %d, C %d)' % (f, len(a), len(b)))
            bad += 1
            continue
        diff = [abs(x - y) for x, y in zip(a, b) if x != y]
        line = '%s: %s; RGB distintos %d de %d (%.4f %%), dif. max %d' % (
            os.path.basename(f), info, len(diff), len(a), 100.0 * len(diff) / max(len(a), 1), max(diff) if diff else 0)
        if xa:
            wav = open(base + '.ref.wav', 'rb').read()[44:]
            pcm = open(base + '.c.pcm', 'rb').read()
            line += '; audio %s (%d muestras)' % ('identico' if wav == pcm else 'DISTINTO', len(pcm) // 2)
            bad += wav != pcm
        print(line)
        bad += len(diff) > 0 and max(diff) > 1
    return 1 if bad else 0


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1:]))
