"""Envuelve un ISO en una imagen CDI (DiscJuggler) arrancable en Dreamcast.

Por que CDI y no GDI: la BIOS real **no arranca** nuestros GDI (reconoce el disco, lee
el ID de la IP.BIN y se va al menu), pero si arranca un CDI con disposicion MIL-CD, que
es la que usa todo el homebrew y la que se graba en un CD-R para hardware real.
Comprobado en Flycast con la BIOS real: ver docs/hitos/M1b.md.

Disposicion que se genera, la misma que produce cdi4dc:

    sesion 1, pista 1   audio mudo: 150 sectores de pregap + 302, de 2352 bytes
    sesion 2, pista 2   datos: 150 de pregap + el ISO, de 2048 bytes, con LBA 11702

Los sectores de datos van de **2048 bytes** a proposito. La plantilla de cdi4dc usa
2336 (Mode 2 Form 1), que obliga a calcular EDC y ECC por sector; con 2048 esa parte la
pone quien graba el disco y la imagen es el ISO tal cual.

El ISO tiene que construirse con `-C 0,11702`, porque los extents de ISO9660 se guardan
en absoluto y la pista empieza ahi.

La cabecera del CDI va al FINAL del archivo y sus ultimos 8 bytes son la version y su
tamano. La estructura de abajo se obtuvo analizando una imagen homebrew conocida; los
campos que se rellenan son estos, con desplazamiento dentro del descriptor de la pista:

    +65  u32  sectores de la pista          +115 u8   tamano de sector (0=2048)
    +91  u32  LBA de inicio                 +124 u32  idem total
    +95  u32  total con el pregap           +243 u32  idem LBA
"""
import os
import struct
import sys

CDI_TRAILER = bytes.fromhex(
    '0200010000000000000001000000ffffffff000001000000ffffffffab0010020c6e66736873'
    '2d64632e6364690000000000000000000000020000000000000000000080407e050000009800'
    '0200960000002e0100000000000000000000000000000000000000000000000000000000c401'
    '000000000000000000000000000000000000020000000000000000c401000000000000000000'
    '0000000000000000000000000000ffffffffffffffff01000000800000000200000010000000'
    '44ac000000000000000000000000000000000000000000000000000000000000000000000000'
    '0000000000000000ffffffff0000000000000000000000000000000000000000000000000001'
    '0000000000000001000000ffffffff000001000000ffffffffab0010020c6e667368732d6463'
    '2e6364690000000000000000000000020000000000000000000080407e050000009800020096'
    '0000000000000000000000000002000000000000000100000000000000000000000000000000'
    '0000000000000000000000000000000000000004000000000000000000000000000000000000'
    '0000000000000000000000ffffffffffffffff0100000080000000020000001000000044ac00'
    '0000000000000000000000000000000000000000000000000000000000000000000000000000'
    '0000000000ffffffff0000000000000000000000000200000000000000000000000000000000'
    '0000000001000000ffffffff000001000000ffffffffab0010020c6e667368732d64632e6364'
    '690000000000000000000000020000000000000000000080407e050000009800134900002032'
    '3430705375697465444320202020202020202020202020202020202020202000010000000100'
    '0000000000000000000000000000000000000000000000000000000000000006000080ad0200'
    '00'
)
# El nombre del archivo va DENTRO de cada descriptor, con su longitud delante, asi que
# los campos se miden desde el FINAL del nombre: cualquier otra referencia se descoloca
# en cuanto el nombre cambia de longitud. (Midiendolos desde el inicio del descriptor
# los valores caian 6 bytes tarde y la consola trataba el disco como CD de audio.)
CAMPOS = {            # desplazamiento desde el final del nombre del descriptor
    'sectores':  37,
    'lba':       63,
    'total':     67,
    'sector_sz': 87,
    'total2':   215 - 119,   # 96
    'lba2':     215,
}
PREGAP = 150          # sectores de pregap, guardados tambien en el archivo
AUDIO_SECTORS = 302   # pista 1: audio mudo, solo para la disposicion MIL-CD
DATA_LBA = 11702      # donde empieza la pista de datos; tiene que cuadrar con -C 0,N
NAME = b'nfshs-dc.cdi'


def build(iso_path, out_path):
    iso = open(iso_path, 'rb').read()
    if len(iso) % 2048:
        raise SystemExit('el ISO no es multiplo de 2048: %d' % len(iso))
    n = len(iso) // 2048

    # 1. el nombre primero, porque mueve todo lo que viene detras
    nombre = os.path.basename(out_path).encode()
    marca = bytes([len(nombre)]) + nombre
    t = bytearray(bytes(CDI_TRAILER).replace(bytes([len(NAME)]) + NAME, marca))

    # 2. la base del descriptor de la pista 2 es la SEGUNDA aparicion del nombre
    pos = []
    i = t.find(marca)
    while i >= 0:
        pos.append(i)
        i = t.find(marca, i + 1)
    if len(pos) < 2:
        raise SystemExit('no encuentro los descriptores en la cabecera del CDI')
    fin = pos[1] + len(marca)          # final del nombre de la pista de datos

    # 3. ya con la referencia correcta, los campos
    struct.pack_into('<I', t, fin + CAMPOS['sectores'], n)
    struct.pack_into('<I', t, fin + CAMPOS['lba'], DATA_LBA)
    struct.pack_into('<I', t, fin + CAMPOS['total'], PREGAP + n)
    t[fin + CAMPOS['sector_sz']] = 0
    struct.pack_into('<I', t, fin + CAMPOS['total2'], PREGAP + n)
    struct.pack_into('<I', t, fin + CAMPOS['lba2'], DATA_LBA)
    struct.pack_into('<I', t, len(t) - 4, len(t))

    with open(out_path, 'wb') as f:
        f.write(b'\0' * ((PREGAP + AUDIO_SECTORS) * 2352))
        f.write(b'\0' * (PREGAP * 2048))
        f.write(iso)
        f.write(bytes(t))
    print('%s: %d sectores, %.0f MB' % (out_path, n, os.path.getsize(out_path) / 1048576))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('uso: mkcdi.py <entrada.iso> <salida.cdi>')
    build(sys.argv[1], sys.argv[2])
