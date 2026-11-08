"""Inspect CDI v3.5 audio/data images and their ISO9660 boot files (no emulator)."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

def u32(data, at):
    return struct.unpack_from('<I', data, at)[0]

def inspect(path, extract=None):
    path = Path(path)
    with path.open('rb') as f:
        f.seek(-8, 2)
        version, size = struct.unpack('<II', f.read(8))
        if version != 0x80000006 or not 8 < size < path.stat().st_size:
            raise ValueError('Expected CDI v3.5 trailer')
        f.seek(-size, 2)
        trailer = f.read(size)
        marker = bytes.fromhex('000001000000ffffffff') * 2
        starts = []
        at = trailer.find(marker)
        while at >= 0:
            starts.append(at)
            at = trailer.find(marker, at + len(marker))
        if len(starts) != 3:
            raise ValueError('Expected two tracks and a disc descriptor')
        ends = [at + 25 + trailer[at + 24] for at in starts]
        tracks = []
        offset = 0
        for end in ends[:2]:
            base = end + 31
            t = dict(pregap=u32(trailer, base + 2), sectors=u32(trailer, base + 6),
                     mode=u32(trailer, base + 16), lba=u32(trailer, base + 32),
                     total=u32(trailer, base + 36), sector_bytes=(2048,2336,2352)[u32(trailer, base + 56)],
                     total_copy=u32(trailer, base + 65), lba_copy=u32(trailer, base + 184), offset=offset)
            tracks.append(t)
            offset += t['total'] * t['sector_bytes']
        t = tracks[1]
        data_start = t['offset'] + t['pregap'] * t['sector_bytes']
        def sector(index):
            f.seek(data_start + index * t['sector_bytes'] + (8 if t['sector_bytes'] == 2336 else 0))
            return f.read(2048)
        ip = b''.join(sector(i) for i in range(16))
        pvd = sector(16)
        if pvd[:7] != b'\x01CD001\x01':
            raise ValueError('Missing ISO9660 PVD')
        root_lba, root_size = u32(pvd,158), u32(pvd,166)
        def extent(lba, length):
            start = lba - t['lba']
            if start < 0 or start + (length+2047)//2048 > t['sectors']:
                raise ValueError('ISO extent outside data track')
            return b''.join(sector(i) for i in range(start, start+(length+2047)//2048))[:length]
        entries = []
        def directory(lba, length, prefix=''):
            data = extent(lba,length)
            pos = 0
            while pos < len(data):
                n = data[pos]
                if not n:
                    pos = (pos//2048+1)*2048
                    continue
                rec = data[pos:pos+n]
                name = rec[33:33+rec[32]]
                pos += n
                if name in (b'\x00', b'\x01'):
                    continue
                name = prefix + name.decode('ascii').split(';')[0]
                loc, length = u32(rec,2), u32(rec,10)
                if rec[25] & 2:
                    directory(loc,length,name+'/')
                else:
                    if loc < t['lba'] or loc+(length+2047)//2048 > t['lba']+t['sectors']:
                        raise ValueError('File outside track: '+name)
                    entries.append(dict(name=name,lba=loc,bytes=length))
        directory(root_lba,root_size)
        boot_name = ip[96:112].decode('ascii').strip()
        boot = next(e for e in entries if e['name'] == boot_name)
        boot_data = extent(boot['lba'],boot['bytes'])
        disc_end = u32(trailer,ends[2]+31)
        expected_end = t['lba']+t['total']
        report = dict(path=str(path.resolve()),version=hex(version),bytes=path.stat().st_size,
            trailer_bytes=size,sessions=struct.unpack_from('<H',trailer)[0],tracks=tracks,
            payload_matches_descriptors=offset == path.stat().st_size-size,
            disc_end_fad=disc_end,expected_disc_end_fad=expected_end,
            disc_end_matches=disc_end==expected_end,
            disc_label=trailer[ends[2]+36:ends[2]+36+trailer[ends[2]+35]].decode('ascii'),
            ip_header=ip[:256].decode('ascii',errors='replace'),
            iso_volume_sectors=u32(pvd,80),root_lba=root_lba,files=len(entries),boot=boot,
            boot_sha256=hashlib.sha256(boot_data).hexdigest(),
            fits_80min=t['lba']+t['sectors']<=360000,
            duration_minutes=(t['lba']+t['sectors'])/4500)
        if extract:
            out = Path(extract)
            out.mkdir(parents=True,exist_ok=True)
            (out/'IP.BIN').write_bytes(ip)
            (out/'1ST_READ.BIN').write_bytes(boot_data)
            with (out/'datos.iso').open('wb') as dest:
                for i in range(t['sectors']):
                    dest.write(sector(i))
            (out/'files.json').write_text(json.dumps(entries,indent=2))
        return report

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('cdi')
    parser.add_argument('--extract')
    parser.add_argument('--report')
    args = parser.parse_args()
    report = inspect(args.cdi,args.extract)
    output = json.dumps(report,indent=2)
    print(output)
    if args.report:
        Path(args.report).write_text(output+'\n')
    if not report['payload_matches_descriptors'] or not report['disc_end_matches']:
        sys.exit('CDI inconsistente: longitud de pistas o final global incorrectos')
