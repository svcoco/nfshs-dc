"""M3: sectores de las peliculas (MOVIES/*.XA) leidos de la imagen .bin: video STR, audio XA y relleno.

Uso: python -I dc/tools/bake/xa_scan.py <imagen.bin>
"""
import struct, sys, collections
img = open(sys.argv[1], 'rb')
def raw(lba):
    img.seek(lba * 2352); return img.read(2352)
def ud(lba): return raw(lba)[24:24 + 2048]
pvd = ud(16)
root = pvd[156:156 + 34]
def entries(lba, size):
    out = []
    for s in range((size + 2047) // 2048):
        d = ud(lba + s); o = 0
        while o < 2048 and d[o]:
            l = d[o]; ext = struct.unpack_from('<I', d, o + 2)[0]; sz = struct.unpack_from('<I', d, o + 10)[0]
            fl = d[o + 25]; nl = d[o + 32]; name = d[o + 33:o + 33 + nl].decode('latin1')
            out.append((name, ext, sz, fl)); o += l
    return out
rl, rs = struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]
mv = [e for e in entries(rl, rs) if e[0].startswith('MOVIES')][0]
for name, ext, sz, fl in entries(mv[1], mv[2]):
    if not name.endswith(';1'): continue
    n = (sz + 2047) // 2048
    st = collections.Counter(); frames = set(); w = h = 0; coding = set(); chans = collections.Counter(); vsect = 0
    for i in range(n):
        r = raw(ext + i); sub = r[16:24]; body = r[24:]
        if sub[2] & 4:
            st['aud'] += 1; coding.add(sub[3]); chans[sub[1]] += 1
        elif sub[2] & 2 and struct.unpack_from('<H', body, 0)[0] == 0x160:
            st['vid'] += 1; frames.add(struct.unpack_from('<I', body, 8)[0]); w, h = struct.unpack_from('<HH', body, 16)
            ver = struct.unpack_from('<H', body, 2)[0]  # 0x8001
        else:
            st['otro'] += 1
    print(name, ext, sz, n, dict(st), len(frames), 'frames', w, 'x', h, 'fps %.2f' % (150 * len(frames) / n), 'dur %.1fs' % (n / 150), 'coding', [hex(c) for c in coding], 'chans', dict(chans))
