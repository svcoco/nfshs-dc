"""Convierte un PPM (P6) a PNG, para poder mirarlo.

El rasterizador del arnes de PC escribe PPM porque es trivial desde C; zlib, que es lo
unico que hace falta para un PNG, esta en la biblioteca estandar de Python.

    uso: python -I dc/tools/ppm2png.py <entrada.ppm> <salida.png> [escala]
"""
import struct
import sys
import zlib


def main():
    if len(sys.argv) < 3:
        raise SystemExit('uso: ppm2png.py <entrada.ppm> <salida.png> [escala]')
    escala = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    d = open(sys.argv[1], 'rb').read()
    if not d.startswith(b'P6'):
        raise SystemExit('no es un PPM binario')
    campos, o = [], 2
    while len(campos) < 3:
        while o < len(d) and d[o:o + 1].isspace():
            o += 1
        if d[o:o + 1] == b'#':
            while d[o:o + 1] not in (b'\n', b''):
                o += 1
            continue
        ini = o
        while o < len(d) and not d[o:o + 1].isspace():
            o += 1
        campos.append(int(d[ini:o]))
    o += 1
    w, h, _max = campos
    pix = d[o:o + w * h * 3]

    W, H = w * escala, h * escala
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        fila = pix[(y // escala) * w * 3:((y // escala) + 1) * w * 3]
        if escala == 1:
            raw += fila
        else:
            for x in range(W):
                k = (x // escala) * 3
                raw += fila[k:k + 3]

    def trozo(tipo, datos):
        c = tipo + datos
        return struct.pack('>I', len(datos)) + c + struct.pack('>I', zlib.crc32(c))

    with open(sys.argv[2], 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(trozo(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)))
        f.write(trozo(b'IDAT', zlib.compress(bytes(raw), 6)))
        f.write(trozo(b'IEND', b''))
    print('%dx%d -> %s' % (W, H, sys.argv[2]))


if __name__ == '__main__':
    main()
