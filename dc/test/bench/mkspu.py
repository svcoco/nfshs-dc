"""M4: reduce un rastro del SPU (NFSHS_SPUTRACE) para el banco de pruebas en SH-4.

Quita las muestras de cada `R` y deja solo su numero (registro `r` n:u32), y al final una
suma de verificacion de todas las muestras de la referencia: el banco la recalcula con el
SPU en C y comprueba que en SH-4 sale lo mismo. Se corta tras `segundos` de audio.

Uso: python -I dc/test/bench/mkspu.py <rastro.spt> <salida.bin> [segundos]
"""
import struct
import sys


def main(src, dst, secs):
    d = open(src, 'rb').read()
    out = bytearray()
    o = 0
    muestras = 0
    suma = 0
    limite = int(secs * 44100)
    while o < len(d) and muestras < limite:
        c = d[o:o + 1]
        if c == b'W':
            out += d[o:o + 10]
            o += 10
        elif c == b'D':
            n = struct.unpack_from('<I', d, o + 5)[0]
            out += d[o:o + 9 + n]
            o += 9 + n
        elif c == b'R':
            n = struct.unpack_from('<I', d, o + 1)[0]
            out += b'r' + struct.pack('<I', n)
            for (v,) in struct.iter_unpack('<h', d[o + 5:o + 5 + 4 * n]):
                suma = (suma * 31 + (v & 0xFFFF)) & 0xFFFFFFFF
            o += 5 + 4 * n
            muestras += n
        else:
            sys.exit('registro desconocido %r en %d' % (c, o))
    out += b'E' + struct.pack('<II', muestras, suma)
    open(dst, 'wb').write(out)
    print('%s: %d bytes, %d muestras (%.1f s), suma %08x' % (dst, len(out), muestras, muestras / 44100, suma))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 1e9)
