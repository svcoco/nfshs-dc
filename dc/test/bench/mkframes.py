"""M3: saca frames de video (y sectores de audio) a una cabecera C para mdec_bench.

Son datos del disco, asi que la cabecera va a build/ y no se versiona.

Uso: python -I dc/test/bench/mkframes.py <MOVIES_dir> <gamedata_dir> <salida.h>
"""
import os
import struct
import sys

N = 30
S = 2336


def xa_frames(path, first, n):
    d = open(path, 'rb').read()
    frames, audio, cur, parts = [], [], None, {}
    for o in range(0, len(d) - S + 1, S):
        s = d[o:o + S]
        body = s[8:]
        if s[2] & 4:
            if len(frames) >= first and len(audio) < n:
                audio.append(s)
            continue
        if not (s[2] & 2) or struct.unpack_from('<H', body, 0)[0] != 0x160:
            continue
        chunk, chunks = struct.unpack_from('<HH', body, 4)
        fr = struct.unpack_from('<I', body, 8)[0]
        w, h = struct.unpack_from('<HH', body, 16)
        if fr != cur:
            cur, parts = fr, {}
        parts[chunk] = body[32:32 + 2016]
        if len(parts) == chunks:
            frames.append((w, h, b''.join(parts[i] for i in sorted(parts))))
            if len(frames) >= first + n:
                break
    return frames[first:first + n], audio


def dct_frames(path, first, n):
    d = open(path, 'rb').read()
    out, o = [], 0
    while o + 16 <= len(d) and d[o:o + 4] == b'mTCD':
        size = struct.unpack_from('<I', d, o + 4)[0]
        w, h = struct.unpack_from('<HH', d, o + 12)
        out.append((w, h, d[o + 16:o + size]))
        o += size
    return out[first:first + n]


def arr(name, b):
    lines = ['static const uint8_t %s[%d] __attribute__((aligned(4))) = {' % (name, len(b))]
    for i in range(0, len(b), 24):
        lines.append(' ' + ','.join(str(x) for x in b[i:i + 24]) + ',')
    lines.append('};')
    return '\n'.join(lines)


def main(movies, gamedata, out):
    sets = [
        ('titl', xa_frames(os.path.join(movies, 'NFS4TITL.XA'), 600, N)),
        ('tr03', (dct_frames(os.path.join(gamedata, 'ZZZZTR03.DCT'), 100, N), [])),
        ('vd01', (dct_frames(os.path.join(gamedata, 'ZZZZVD01.DCT'), 100, N), [])),
    ]
    src = ['/* generado por dc/test/bench/mkframes.py: NO versionar (datos del disco) */', '#include <stdint.h>']
    for name, (frames, audio) in sets:
        for i, (w, h, b) in enumerate(frames):
            src.append(arr('%s_%d' % (name, i), b))
        src.append('static const BenchFrame %s[] = {%s};' % (
            name, ','.join('{%d,%d,%d,%s_%d}' % (w, h, len(b), name, i) for i, (w, h, b) in enumerate(frames))))
        if audio:
            src.append(arr('%s_audio' % name, b''.join(audio)))
            src.append('#define %s_AUDIO_SECTORS %d' % (name.upper(), len(audio)))
    open(out, 'w').write('\n'.join(src) + '\n')
    print('%s: %d bytes' % (out, os.path.getsize(out)))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
