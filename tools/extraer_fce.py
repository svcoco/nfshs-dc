#!/usr/bin/env python3
"""Extractor BIGF / FCE4 de NFS High Stakes. Solo biblioteca estándar.

Uso: python extraer_fce.py CAR.VIV salida
Por defecto exporta TODOS los LOD; use --lod high para una selección práctica.
No aplica los colores de pintura del motor EA. El alfa TGA se conserva como
dato en *_rgba.png; el material usa PNG RGB opaco para evitar falsos agujeros.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path, PureWindowsPath
import struct
import sys
import zlib

HEADER = 0x2038


def unpack(data, fmt, off):
    size = struct.calcsize(fmt)
    if off < 0 or off + size > len(data):
        raise ValueError(f"Lectura fuera del archivo: {off:#x}+{size}")
    return struct.unpack_from(fmt, data, off)


def u32(data, off):
    return unpack(data, '<I', off)[0]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_viv(data):
    if data[:4] != b'BIGF' or len(data) < 16:
        raise ValueError('Se esperaba BIGF')
    declared, count, directory_end = unpack(data, '>III', 4)
    if declared != len(data):
        raise ValueError('El tamaño BIGF declarado no coincide con el archivo')
    if count > (len(data) - 16) // 9:
        raise ValueError('Número de entradas BIGF imposible')
    pos, entries, used = 16, [], set()
    for _ in range(count):
        off, size = unpack(data, '>II', pos)
        end = data.find(b'\0', pos + 8)
        if end < 0:
            raise ValueError('Nombre BIGF sin terminador')
        name = data[pos + 8:end].decode('ascii', errors='strict')
        win = PureWindowsPath(name)
        pieces = name.replace('\\', '/').split('/')
        if (win.drive or win.root or any(p in ('', '.', '..') for p in pieces)
                or any(':' in p for p in pieces)):
            raise ValueError(f'Ruta BIGF insegura: {name!r}')
        key = '/'.join(pieces).casefold()
        if key in used:
            raise ValueError('Nombres BIGF duplicados')
        used.add(key)
        if off < 16 or size > len(data) - off:
            raise ValueError('Entrada BIGF fuera de rango')
        entries.append({'name': name, 'path': pieces, 'offset': off,
                        'size': size, 'data': data[off:off + size]})
        pos = end + 1
    if any(e['offset'] < pos for e in entries):
        raise ValueError('Una entrada invade el directorio BIGF')
    intervals = sorted((e['offset'], e['offset'] + e['size']) for e in entries)
    if any(a[1] > b[0] for a, b in zip(intervals, intervals[1:])):
        raise ValueError('Entradas BIGF solapadas')
    return entries, {'size': declared, 'entries': count,
                     'field_0x0c_be': directory_end, 'parsed_directory_end': pos}


def vectors(data, off, count):
    if count > (len(data) - off) // 12 or off < HEADER:
        raise ValueError('Tabla vectorial fuera de rango')
    result = [unpack(data, '<3f', off + i * 12) for i in range(count)]
    if any(not math.isfinite(x) for v in result for x in v):
        raise ValueError('Coordenada no finita')
    return result


def bbox(points):
    if not points:
        return None
    return {'min': [min(p[a] for p in points) for a in range(3)],
            'max': [max(p[a] for p in points) for a in range(3)]}


def read_fce(data):
    if len(data) < HEADER or u32(data, 0) != 0x00101014:
        raise ValueError('Solo se admite FCE4 NFS4, firma 14 10 10 00')
    nt, nv, arts = unpack(data, '<III', 8)
    nd, np = u32(data, 0x58), u32(data, 0x11c)
    if nd > 16 or np > 64 or u32(data, 0x820) > 16:
        raise ValueError('Contadores FCE4 fuera de rango')
    specs = [(0x14, 12*nv), (0x18, 12*nv), (0x1c, 56*nt),
             (0x20, 32*nv), (0x24, 12*nv), (0x28, 12*nv),
             (0x2c, 12*nv), (0x30, 12*nv), (0x34, 12*nv),
             (0x38, 12*nv), (0x3c, 4*nv), (0x40, 4*nv),
             (0x44, 4*nv), (0x48, 12*nt)]
    tables = {}
    for field, size in specs:
        off = HEADER + u32(data, field)
        if off > len(data) or size > len(data) - off:
            raise ValueError(f'Tabla FCE fuera de rango: {field:#x}')
        tables[f'{field:#04x}'] = {'absolute_offset': off, 'size': size,
                                  'sha256': sha(data[off:off+size])}
    intervals = sorted((x['absolute_offset'], x['absolute_offset'] + x['size'])
                       for x in tables.values() if x['size'])
    if any(a[1] > b[0] for a, b in zip(intervals, intervals[1:])):
        raise ValueError('Tablas FCE solapadas')
    pos = vectors(data, HEADER + u32(data, 0x14), nv)
    normals = vectors(data, HEADER + u32(data, 0x18), nv)
    damaged = vectors(data, HEADER + u32(data, 0x34), nv)
    damaged_normals = vectors(data, HEADER + u32(data, 0x38), nv)
    animation = list(unpack(data, '<' + 'I'*nv, HEADER + u32(data, 0x40)))
    def name(off):
        return data[off:off+64].split(b'\0', 1)[0].decode('ascii', 'strict')
    triangles = []
    for i in range(nt):
        off = HEADER + u32(data, 0x1c) + 56*i
        values = unpack(data, '<8I6f', off)
        if any(not math.isfinite(x) for x in values[8:]):
            raise ValueError('UV no finita')
        triangles.append({'page': values[0], 'indices': values[1:4],
                          'unknown': values[4:7], 'flags': values[7],
                          'u': values[8:11], 'v': values[11:14]})
    parts, covered_v, covered_t = [], set(), set()
    for i in range(np):
        fv, nvp, ft, ntp = [u32(data, off+4*i)
                             for off in (0x420, 0x520, 0x620, 0x720)]
        if fv+nvp > nv or ft+ntp > nt:
            raise ValueError('Rango de parte fuera de tabla')
        vr, tr = set(range(fv, fv+nvp)), set(range(ft, ft+ntp))
        if covered_v & vr or covered_t & tr:
            raise ValueError('Partes con rangos solapados')
        covered_v.update(vr)
        covered_t.update(tr)
        pivot = unpack(data, '<3f', 0x120+12*i)
        if not all(map(math.isfinite, pivot)):
            raise ValueError('Pivote no finito')
        world = [tuple(p[a]+pivot[a] for a in range(3)) for p in pos[fv:fv+nvp]]
        for t in triangles[ft:ft+ntp]:
            if any(x >= nvp for x in t['indices']):
                raise ValueError('Índice triangular local fuera de parte')
        parts.append({'name': name(0xe28+64*i), 'first_vertex': fv,
                      'vertices': nvp, 'first_triangle': ft, 'triangles': ntp,
                      'pivot': pivot, 'bbox': bbox(world)})
    if len(covered_v) != nv or len(covered_t) != nt:
        raise ValueError('Existen elementos no asignados a partes')
    dummies = [{'name': name(0xa28+64*i),
                'position': unpack(data, '<3f', 0x5c+12*i)} for i in range(nd)]
    return {'vertices': nv, 'triangles': nt, 'arts': arts, 'parts': parts,
            'dummies': dummies, 'tables': tables, 'positions': pos,
            'normals': normals, 'damaged_positions': damaged,
            'damaged_normals': damaged_normals, 'animation': animation,
            'faces': triangles, 'half_size': unpack(data, '<3f', 0x4c),
            'colors': [list(data[o:o+4*u32(data, 0x820)])
                       for o in (0x824, 0x864, 0x8a4, 0x8e4)],
            'flags': dict(Counter(hex(t['flags']) for t in triangles)),
            'pages': dict(Counter(t['page'] for t in triangles)),
            'damaged_vertices_changed': sum(a != b for a,b in zip(pos, damaged))}


def read_tga(data):
    ident, cmap, typ, first, length, depth, x, y, w, h, bits, desc = unpack(
        data, '<BBBHHBHHHHBB', 0)
    if cmap != 0 or typ != 2 or bits not in (24, 32) or desc & 0xc0:
        raise ValueError('TGA admitida: true-color tipo 2, 24/32 bits, sin entrelazado')
    if not w or not h:
        raise ValueError('Dimensiones TGA vacías')
    stride = bits // 8
    start = 18 + ident
    if start + w*h*stride > len(data):
        raise ValueError('Píxeles TGA truncados')
    rgba = bytearray(w*h*4)
    for sy in range(h):
        dy = sy if desc & 0x20 else h-1-sy
        for sx in range(w):
            dx = w-1-sx if desc & 0x10 else sx
            off = start+(sy*w+sx)*stride
            b,g,r = data[off:off+3]
            a = data[off+3] if stride == 4 else 255
            dest = (dy*w+dx)*4
            rgba[dest:dest+4] = bytes((r,g,b,a))
    return bytes(rgba), {'width': w, 'height': h, 'bits': bits, 'type': typ,
                        'descriptor': desc, 'pixel_offset': start,
                        'footer_2_0': data[-18:] == b'TRUEVISION-XFILE.\0',
                        'alpha_histogram': dict(Counter(rgba[3::4]))}


def write_png(path, w, h, pixels, channels):
    def chunk(tag, payload):
        return (struct.pack('>I', len(payload)) + tag + payload +
                struct.pack('>I', zlib.crc32(tag+payload) & 0xffffffff))
    rows = b''.join(b'\0'+pixels[y*w*channels:(y+1)*w*channels] for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' +
                     chunk(b'IHDR', struct.pack('>IIBBBBB', w,h,8,
                                               6 if channels == 4 else 2,0,0,0)) +
                     chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def selected(part, lod):
    n = part['name']
    if lod == 'all':
        return True
    if lod == 'high':
        return n.startswith(':H') or n.startswith(':O')
    if lod == 'medium':
        return n.startswith(':M') or n in (':OT', ':OL', ':OS')
    return n == {'low': ':LB', 'tiny': ':TB'}[lod]


def export_obj(model, target, texture_stem, lod):
    parts = [p for p in model['parts'] if selected(p, lod)]
    if not parts:
        raise ValueError('La selección de LOD está vacía')
    materials = sorted({(t['page'], t['flags']) for p in parts
                        for t in model['faces'][p['first_triangle']:
                                                 p['first_triangle']+p['triangles']]})
    with target.with_suffix('.mtl').open('w', encoding='utf-8') as f:
        for page, flags in materials:
            texture = texture_stem[:-2] + f'{page:02d}.png'
            f.write(f'newmtl page{page}_flags{flags:08x}\n'
                    f'# FCE flags: 0x{flags:08x}; efectos EA no reproducidos\n'
                    'Ka 1 1 1\nKd 1 1 1\nd 1\nillum 1\n'
                    f'map_Kd {texture}\n\n')
    with target.open('w', encoding='utf-8') as f:
        f.write(f'# Ejes FCE conservados: +X derecha, +Y arriba, +Z delante\n'
                f'# Unidades documentadas: metros. LOD: {lod}\n'
                f'mtllib {target.with_suffix(".mtl").name}\n')
        vo, to = 0, 0
        for p in parts:
            n = p['name']
            # OBJ separa nombres de grupo por espacios: se conserva la forma
            # original en o/comentario/JSON y se escapa el token g si hace falta.
            group = ''.join(c if c.isalnum() or c in ':_-' else f'%{ord(c):02X}' for c in n)
            f.write(f'\n# original_part_name {json.dumps(n)}\no {n}\ng {group or "unnamed"}\n')
            f.write('# pivot ' + ' '.join(map(str, p['pivot'])) + '\n')
            fv, nv = p['first_vertex'], p['vertices']
            for v in model['positions'][fv:fv+nv]:
                world = [v[a]+p['pivot'][a] for a in range(3)]
                f.write('v ' + ' '.join(f'{x:.9g}' for x in world) + '\n')
            for v in model['normals'][fv:fv+nv]:
                f.write('vn ' + ' '.join(f'{x:.9g}' for x in v) + '\n')
            faces = model['faces'][p['first_triangle']:p['first_triangle']+p['triangles']]
            for t in faces:
                for u,v in zip(t['u'], t['v']):
                    f.write(f'vt {u:.9g} {1-v:.9g}\n')
            for i,t in enumerate(faces):
                f.write(f'usemtl page{t["page"]}_flags{t["flags"]:08x}\n')
                # Sin reflexión de Z, se invierte el orden FCE para OBJ CCW.
                corners = []
                for j in (0,2,1):
                    vi = vo+t['indices'][j]+1
                    corners.append(f'{vi}/{to+3*i+j+1}/{vi}')
                f.write('f ' + ' '.join(corners) + '\n')
            vo += nv
            to += 3*len(faces)
    return sum(p['triangles'] for p in parts)


def public_model(m):
    return {k:v for k,v in m.items() if k not in ('positions', 'normals',
            'damaged_positions', 'damaged_normals', 'animation', 'faces')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('viv', type=Path)
    parser.add_argument('salida', type=Path)
    parser.add_argument('--lod', choices=('all','high','medium','low','tiny'), default='all')
    args = parser.parse_args()
    data = args.viv.read_bytes()
    entries, header = read_viv(data)
    models, images, pixels = {}, {}, {}
    # Validar antes de escribir.
    for e in entries:
        name = e['name'].lower()
        if name.endswith('.fce'):
            models[name] = read_fce(e['data'])
        elif name.endswith('.tga'):
            pixels[name], images[name] = read_tga(e['data'])
    if 'car.fce' not in models:
        raise ValueError('No existe car.fce en este BIGF')
    out = args.salida.resolve()
    if args.viv.resolve().is_relative_to(out):
        raise ValueError('Use una carpeta de salida separada del archivo de origen')
    out.mkdir(parents=True, exist_ok=True)
    for e in entries:
        dest = out.joinpath(*e['path']).resolve()
        if not dest.is_relative_to(out):
            raise ValueError('Destino fuera de salida (posible enlace simbólico)')
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(e['data'])
    for name, info in images.items():
        dest = out.joinpath(*name.replace('\\','/').split('/'))
        rgba = pixels[name]
        rgb = bytes(c for i,c in enumerate(rgba) if i % 4 != 3)
        write_png(dest.with_suffix('.png'), info['width'], info['height'], rgb, 3)
        write_png(dest.with_name(dest.stem+'_rgba.png'), info['width'], info['height'], rgba, 4)
    exported = export_obj(models['car.fce'], out/'car.obj', 'car00', args.lod)
    report = {'source_sha256': sha(data), 'bigf': header,
              'entries': [{k:v for k,v in e.items() if k not in ('data','path')} |
                          {'sha256': sha(e['data'])} for e in entries],
              'models': {k:public_model(m) for k,m in models.items()},
              'images': images, 'exported_triangles': exported, 'lod': args.lod,
              'axes': '+X derecha, +Y arriba, +Z delante; metros según documentación'}
    # Conserva los datos que OBJ no representa, para la adaptación al motor.
    (out/'car_metadata.json').write_text(json.dumps(models['car.fce'], indent=2,
                                                   ensure_ascii=False), encoding='utf-8')
    (out/'informe.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    lines = [f'BIGF: {len(data)} bytes; {len(entries)} entradas',
             '+X derecha; +Y arriba; +Z delante. Metros según documentación.',
             'Cajas globales: posición local + pivote; todos los LOD incluidos.',
             '| Parte | Vértices | Triángulos | min xyz | max xyz |',
             '|---|---:|---:|---|---|']
    for p in models['car.fce']['parts']:
        box = p['bbox']
        fmt = lambda v: '(' + ', '.join(f'{x:.6f}' for x in v) + ')'
        lines.append(f'| {p["name"]} | {p["vertices"]} | {p["triangles"]} | '
                     f'{fmt(box["min"]) if box else "vacía"} | '
                     f'{fmt(box["max"]) if box else "vacía"} |')
    for name,m in models.items():
        lines.append(f'{name}: {m["vertices"]} vértices; {m["triangles"]} triángulos; '
                     f'{len(m["parts"])} partes; {len(m["dummies"])} dummies.')
    lines.append(f'OBJ: {exported} triángulos, selección {args.lod}.')
    text = '\n'.join(lines) + '\n'
    (out/'informe.md').write_text(text, encoding='utf-8')
    print(text)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, UnicodeError, struct.error) as exc:
        sys.exit(f'Error: {exc}')
