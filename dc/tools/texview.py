"""Compone en una imagen PNG las texturas que convirtio la cache del PVR.

El paso 3 de M2a no tiene oraculo: la unica forma de saber si la conversion de la VRAM
de PS1 (paginas de 4 y 8 bits mas paleta) a ARGB1555 esta bien es mirarla. Esto lee el
volcado de `--texdump` y las pega en una rejilla, con un tablero de ajedrez detras para
que se vea donde hay transparencia.

    uso: python -I dc/tools/texview.py <volcado.tex> <salida.png>
"""
import struct
import sys
import zlib


def png(path, w, h, rgb):
    """PNG RGB de 8 bits, sin filtros. zlib es de la biblioteca estandar."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def trozo(tipo, datos):
        c = tipo + datos
        return struct.pack('>I', len(datos)) + c + struct.pack('>I', zlib.crc32(c))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(trozo(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)))
        f.write(trozo(b'IDAT', zlib.compress(bytes(raw), 6)))
        f.write(trozo(b'IEND', b''))


def main():
    if len(sys.argv) != 3:
        raise SystemExit('uso: texview.py <volcado.tex> <salida.png>')
    d = open(sys.argv[1], 'rb').read()
    if d[:4] != b'NFST':
        raise SystemExit('no es un volcado de texturas')
    n = struct.unpack_from('<I', d, 4)[0]

    texturas, o = [], 8
    for _ in range(n):
        tpage, clut, u0, v0, w, h = struct.unpack_from('<6H', d, o)
        o += 12
        texels = d[o:o + w * h * 2]
        o += w * h * 2
        texturas.append((tpage, clut, w, h, texels))

    # rejilla: se ordenan por alto para que quede compacta
    texturas.sort(key=lambda t: (-t[3], -t[2]))
    cols = 6
    margen = 4
    anchos = [max((t[2] for t in texturas[i::cols]), default=0) for i in range(cols)]
    filas = [texturas[i:i + cols] for i in range(0, len(texturas), cols)]
    altos = [max((t[3] for t in f), default=0) for f in filas]
    W = sum(anchos) + margen * (cols + 1)
    H = sum(altos) + margen * (len(filas) + 1)

    img = bytearray(W * H * 3)
    for y in range(H):                    # tablero de fondo
        for x in range(W):
            v = 0x30 if ((x >> 3) + (y >> 3)) & 1 else 0x18
            img[(y * W + x) * 3:(y * W + x) * 3 + 3] = bytes((v, v, v))

    y0 = margen
    for fi, fila in enumerate(filas):
        x0 = margen
        for ci, (tpage, clut, w, h, texels) in enumerate(fila):
            for j in range(h):
                for i in range(w):
                    c = struct.unpack_from('<H', texels, (j * w + i) * 2)[0]
                    if not (c & 0x8000):          # transparente: se ve el tablero
                        continue
                    r = ((c >> 10) & 0x1F) << 3
                    g = ((c >> 5) & 0x1F) << 3
                    b = (c & 0x1F) << 3
                    px = ((y0 + j) * W + x0 + i) * 3
                    img[px:px + 3] = bytes((r, g, b))
            x0 += anchos[ci] + margen
        y0 += altos[fi] + margen

    png(sys.argv[2], W, H, img)
    print('%d texturas, %dx%d -> %s' % (len(texturas), W, H, sys.argv[2]))


if __name__ == '__main__':
    main()
