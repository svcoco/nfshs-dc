"""Extrae los archivos de la imagen del disco de PS1 a gamedata/.

La imagen es MODE2/2352: sectores crudos de 2352 bytes con 16 de sincronismo y
cabecera, 8 de subcabecera (Mode 2 Form 1) y 2048 de datos de usuario, mas EDC/ECC.
El sistema de archivos es ISO 9660 normal sobre esos 2048 bytes utiles.

Se escribe en Python a proposito: el `nfshs extract` de la referencia hace lo mismo pero
necesita cargo, y esto hace falta antes de tener Rust instalado. En M2 la herramienta de
bake volvera a leer el disco, asi que este codigo se reutiliza.

    uso: python -I dc/tools/extract_iso.py [IMAGEN] [DESTINO] [--list] [--force]

Por omision la imagen es el unico .bin de gamedata/ y el destino, gamedata/.
"""
import os
import struct
import sys

RAW = 2352          # tamano del sector en el archivo
DATA_OFF = 24       # 16 de sincronismo/cabecera + 8 de subcabecera (Mode 2 Form 1)
DATA = 2048         # datos de usuario por sector
PVD_LBA = 16


class Image:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.size = os.path.getsize(path)
        if self.size % RAW != 0:
            raise SystemExit(f'{path}: {self.size} bytes no es multiplo de {RAW}; '
                             'se esperaba una imagen MODE2/2352')
        self.sectors = self.size // RAW

    def sector(self, lba):
        if not 0 <= lba < self.sectors:
            raise SystemExit(f'sector {lba} fuera de la imagen ({self.sectors})')
        self.f.seek(lba * RAW + DATA_OFF)
        return self.f.read(DATA)

    def read(self, lba, length):
        out = bytearray()
        while len(out) < length:
            out += self.sector(lba)
            lba += 1
        return bytes(out[:length])


def records(block):
    """Recorre los registros de directorio de un bloque de datos."""
    i = 0
    while i < len(block):
        ln = block[i]
        if ln == 0:
            # el resto del sector es relleno: salta al siguiente
            i = (i // DATA + 1) * DATA
            continue
        yield block[i:i + ln]
        i += ln


def parse_record(r):
    lba, size = struct.unpack_from('<I', r, 2)[0], struct.unpack_from('<I', r, 10)[0]
    flags = r[25]
    name_len = r[32]
    name = r[33:33 + name_len]
    return lba, size, flags, name


def walk(img, lba, size, prefix=''):
    """Devuelve [(ruta, lba, bytes)] de todos los archivos bajo un directorio."""
    out = []
    for r in records(img.read(lba, size)):
        child_lba, child_size, flags, raw = parse_record(r)
        if len(raw) == 1 and raw[0] in (0, 1):
            continue                      # "." y ".."
        name = raw.decode('latin1')
        if ';' in name:                   # version del archivo: FRONT.BIN;1
            name = name.split(';', 1)[0]
        path = prefix + name
        if flags & 0x02:
            out += walk(img, child_lba, child_size, path + '/')
        else:
            out.append((path, child_lba, child_size))
    return out


def main(argv):
    opts = [a for a in argv if a.startswith('--')]
    args = [a for a in argv if not a.startswith('--')]
    root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    gamedata = os.path.join(root, 'gamedata')

    if args:
        image = args[0]
    else:
        bins = [f for f in os.listdir(gamedata) if f.lower().endswith('.bin')
                and os.path.getsize(os.path.join(gamedata, f)) > 100 * 1024 * 1024]
        if len(bins) != 1:
            raise SystemExit(f'esperaba exactamente una imagen .bin grande en {gamedata}, '
                             f'encontre {bins}')
        image = os.path.join(gamedata, bins[0])
    dest = args[1] if len(args) > 1 else gamedata

    img = Image(image)
    pvd = img.sector(PVD_LBA)
    if pvd[1:6] != b'CD001':
        raise SystemExit(f'el sector {PVD_LBA} no es un descriptor ISO 9660 '
                         f'(encontre {pvd[1:6]!r})')
    vol = pvd[40:72].decode('latin1').strip()
    root_rec = pvd[156:190]
    r_lba, r_size, _, _ = parse_record(root_rec)
    files = walk(img, r_lba, r_size)
    total = sum(s for _, _, s in files)
    print(f'imagen:  {os.path.basename(image)}  ({img.sectors} sectores)')
    print(f'volumen: "{vol}"')
    print(f'archivos: {len(files)}, {total / 1048576:.1f} MB\n')

    if '--list' in opts:
        for path, lba, size in sorted(files):
            print(f'  {size:>10}  lba {lba:>7}  {path}')
        return

    written = skipped = 0
    for path, lba, size in sorted(files):
        out = os.path.join(dest, path.replace('/', os.sep))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        if os.path.exists(out) and '--force' not in opts:
            if os.path.getsize(out) == size:
                skipped += 1
                continue
        with open(out, 'wb') as fh:
            fh.write(img.read(lba, size))
        written += 1
    print(f'escritos: {written}   ya estaban: {skipped}   destino: {dest}')
    print('\n(--list para ver el contenido sin escribir, --force para reescribir)')


main(sys.argv[1:])
