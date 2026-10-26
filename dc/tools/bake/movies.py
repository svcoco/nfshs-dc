"""M3: extrae las peliculas (MOVIES/*.XA) de la imagen .bin con sus sectores enteros.

Cada sector sale como lo lee el reproductor: subcabecera de 8 bytes + 2 328 bytes (2 336 en
total), igual que `nfshs extract`. Las copias de 2 048 bytes por sector que deja una
extraccion ISO normal pierden el audio XA (modo 2, forma 2), ver docs/hitos/M3.md.

Uso: python -I dc/tools/bake/movies.py <imagen.bin> <dir_salida>
"""
import os
import struct
import sys

RAW, OFS = 2352, 24


def main(image, out):
    img = open(image, 'rb')

    def raw(lba):
        img.seek(lba * RAW)
        return img.read(RAW)

    def entries(lba, size):
        res = []
        for s in range((size + 2047) // 2048):
            d = raw(lba + s)[OFS:OFS + 2048]
            o = 0
            while o < 2048 and d[o]:
                ext, sz = struct.unpack_from('<I', d, o + 2)[0], struct.unpack_from('<I', d, o + 10)[0]
                nl = d[o + 32]
                res.append((d[o + 33:o + 33 + nl].decode('latin1'), ext, sz))
                o += d[o]
        return res

    pvd = raw(16)[OFS:OFS + 2048]
    if pvd[1:6] != b'CD001':
        sys.exit('%s: no es una imagen MODE2/2352 con ISO 9660' % image)
    root = pvd[156:156 + 34]
    top = entries(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0])
    movies = [e for e in top if e[0] == 'MOVIES']
    if not movies:
        sys.exit('sin directorio MOVIES')
    os.makedirs(out, exist_ok=True)
    for name, ext, sz in entries(movies[0][1], movies[0][2]):
        if not name.endswith(';1'):
            continue
        n = (sz + 2047) // 2048
        with open(os.path.join(out, name[:-2]), 'wb') as f:
            for i in range(n):
                f.write(raw(ext + i)[16:])
        print('%-12s %6d sectores' % (name[:-2], n))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
