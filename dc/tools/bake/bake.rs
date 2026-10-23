//! Bakes a track for the Dreamcast port: GRP + PSH -> a single file the SH-4 can draw
//! from without parsing anything (M2b, step 2).
//!
//! It lives under `dc/` because it is the port's code, and it is pulled into `src/dev`
//! with `#[path]` so it can reuse `src/formats` (CLAUDE.md section 3 and 8).
//!
//! # What goes in, and what does not
//!
//! The game does **not** build the world's mesh in RAM: it only says which blocks are
//! visible and at what detail. The mesh is in the GRP, so everything that can be decided
//! on the PC is decided here:
//!
//! - **UV corners** are resolved per material, flags and all, so the SH-4 only looks up a
//!   rect and four corner slots.
//! - **Textures** are packed into one atlas, already ARGB1555 for the PVR.
//! - **Vertex colour is NOT baked.** It comes from `Chunk_lightTable`, which lives in the
//!   game's RAM and changes with the track, the variant and the weather, so each vertex
//!   keeps its *shade index* and the port looks the colour up at run time.
//!
//! # Format (little-endian throughout)
//!
//! ```text
//! header   "NFST", u32 version, u16 blocks, u16 materials,
//!          u16 atlas_w, u16 atlas_h, u32 verts, u32 quads, u32 atlas_offset
//! blocks   blocks * 44 bytes: i32 origin[3] (1/64 m), then five (u32 offset, u16 count)
//!          pairs - ground verts, ground quads, ground far quads, detail verts,
//!          detail quads - and u16 of padding
//! materials materials * 16 bytes: u16 atlas x, y, w, h, u8 corner[4] (which rect
//!          corner each of the four vertices takes), u8 flags (1 doble cara,
//!          2 semitransparente, 4 suma en vez de mitad, 8 textura que se desplaza),
//!          u8 pad[3]
//! verts    verts * 8 bytes: i16 x, y, z (1/64 m, relative to the block origin),
//!          u8 shade (index into Chunk_lightTable), u8 pad
//! quads    quads * 10 bytes: u16 v[4] (into the block's pool), u16 material
//! atlas    atlas_w * atlas_h ARGB1555 texels
//! ```
use anyhow::{Result, bail};

use crate::formats::{grp, psh::Psh};

/// Texels of padding around each shape in the atlas, so a neighbour never bleeds in.
const PAD: u32 = 1;
/// The PVR takes textures up to 1024 on a side.
const ATLAS_MAX: u32 = 1024;

struct Rect {
    x: u32,
    y: u32,
    w: u32,
    h: u32,
}

/// A shelf packer, best-fit by width.
///
/// Shapes go in tallest first, so a shelf's height is set by its first shape and nothing
/// taller can follow. The gain over filling shelves in order is the **best fit**: once a
/// shelf is open, it takes the widest shape still left that fits in what remains of the
/// row. Measured over the 40 tracks, that is the difference between needing 1024x1024
/// and fitting in 1024x512 for most of them, which is a megabyte of the PVR's eight.
struct Atlas {
    w: u32,
    h: u32,
    texels: Vec<u16>,
}

impl Atlas {
    fn new(w: u32) -> Self {
        Atlas { w, h: 0, texels: Vec::new() }
    }

    /// Lays `items` (index, width, height) out and returns a rect for each, in the same
    /// order. None if something did not fit at all.
    fn pack(&mut self, items: &[(u16, u32, u32)]) -> Option<Vec<(u16, Rect)>> {
        let mut quedan: Vec<(u16, u32, u32)> = items.to_vec();
        quedan.sort_by_key(|&(_, w, h)| (std::cmp::Reverse(h), std::cmp::Reverse(w)));
        let mut salida = Vec::with_capacity(items.len());
        let mut y = 0u32;

        while !quedan.is_empty() {
            // la primera que queda es la mas alta: fija la altura de la balda
            let alto = quedan[0].2 + PAD;
            if y + alto > ATLAS_MAX {
                return None;
            }
            // la mas alta abre la balda y fija su altura; luego se van metiendo las
            // siguientes EN ORDEN DE ALTURA mientras quepan a lo ancho.
            //
            // Probado y descartado (2026-10-23): llenar el resto de la fila con la mas
            // ANCHA que quepa. Suena mejor y es peor, medido sobre las 40 pistas: arrastra
            // piezas anchas y bajas a baldas altas y desperdicia la altura. Las 40 pasaban
            // a necesitar 1024x1024 (600-977 filas) en vez de 24 de ellas caber en
            // 1024x512.
            let mut x = 0u32;
            while !quedan.is_empty() {
                let Some(k) = quedan.iter().position(|it| x + it.1 + PAD <= self.w) else {
                    break;
                };
                let (id, w, h) = quedan.remove(k);
                salida.push((id, Rect { x, y, w, h }));
                x += w + PAD;
            }
            y += alto;
            self.h = y;
        }
        Some(salida)
    }

    /// Grows to the final power-of-two height and returns the texels.
    fn finish(mut self) -> (u32, u32, Vec<u16>) {
        let h = self.h.next_power_of_two().max(8);
        self.texels.resize((self.w * h) as usize, 0);
        (self.w, h, self.texels)
    }

    fn blit(&mut self, r: &Rect, rgba: &[u8]) {
        let need = ((r.y + r.h).next_power_of_two().max(8) * self.w) as usize;
        if self.texels.len() < need {
            self.texels.resize(need, 0);
        }
        for y in 0..r.h {
            for x in 0..r.w {
                let i = ((y * r.w + x) * 4) as usize;
                let c = rgba.get(i..i + 4).map_or(0u16, |p| {
                    if p[3] < 128 {
                        0 // the PS1's colour 0: not drawn
                    } else {
                        0x8000
                            | ((p[0] as u16 >> 3) << 10)
                            | ((p[1] as u16 >> 3) << 5)
                            | (p[2] as u16 >> 3)
                    }
                });
                self.texels[((r.y + y) * self.w + r.x + x) as usize] = c;
            }
        }
    }
}

fn put16(o: &mut Vec<u8>, v: u16) {
    o.extend_from_slice(&v.to_le_bytes());
}

fn put32(o: &mut Vec<u8>, v: u32) {
    o.extend_from_slice(&v.to_le_bytes());
}

/// The four corner slots a material's UVs take, as the original works them out.
///
/// The pixmap stores its corners as top-left, top-right, bottom-left, bottom-right.
/// `Trk_Material::uvFlag` reorders them (`Track_ProcessFlipAndUVFlags`: 0x10 flips U, 0x40
/// flips V, then one of 0x02 / 0x04 / 0x08 rotates by 90 / 180 / 270), and then
/// `DrawW_DrawQuad` sends the quad's points 1, 0, 2, 3 as the polygon's vertices.
fn corners(flags: u8) -> [u8; 4] {
    let mut p: [u8; 4] = [0, 1, 2, 3];
    let remap = |p: [u8; 4], o: [usize; 4]| o.map(|i| p[i]);
    if flags & 0x10 != 0 {
        p = remap(p, [1, 0, 3, 2]);
    }
    if flags & 0x40 != 0 {
        p = remap(p, [2, 3, 0, 1]);
    }
    if flags & 0x02 != 0 {
        p = remap(p, [2, 0, 3, 1]);
    } else if flags & 0x04 != 0 {
        p = remap(p, [3, 2, 1, 0]);
    } else if flags & 0x08 != 0 {
        p = remap(p, [1, 3, 0, 2]);
    }
    [p[1], p[0], p[2], p[3]]
}

pub fn bake(grp_path: &str, psh_path: &str, out: &std::path::Path) -> Result<()> {
    let track = grp::Track::parse(&std::fs::read(grp_path)?)?;
    let shapes = Psh::parse(&std::fs::read(psh_path)?)?;

    // ---- the atlas: one rect per PSH shape the materials actually use ----
    let used: std::collections::BTreeSet<u16> =
        track.textures.iter().map(|t| t.shape).collect();
    // a 1x1 shape is one texel over the whole polygon; "TRAN" is colour 0, invisible
    let items: Vec<(u16, u32, u32)> = used
        .iter()
        .filter_map(|&s| {
            let sh = shapes.shapes.get(s as usize)?;
            Some((s, sh.width.max(1) as u32, sh.height.max(1) as u32))
        })
        .collect();
    let mut atlas = Atlas::new(ATLAS_MAX);
    let Some(puestas) = atlas.pack(&items) else {
        bail!("las formas no caben en un atlas de {ATLAS_MAX}x{ATLAS_MAX}");
    };
    let mut rects: std::collections::HashMap<u16, Rect> = std::collections::HashMap::new();
    for (s, r) in puestas {
        if let Some(sh) = shapes.shapes.get(s as usize) {
            atlas.blit(&r, &sh.rgba);
        }
        rects.insert(s, r);
    }

    // ---- materials ----
    let mut mats = Vec::new();
    for t in &track.textures {
        let r = rects.get(&t.shape);
        let (x, y, w, h) = r.map_or((0, 0, 1, 1), |r| (r.x, r.y, r.w, r.h));
        // A polygon is semi-transparent when its texture's palette holds an STP colour,
        // whatever the material says; with no palette, the material's bit 0 stands for it.
        let sh = shapes.shapes.get(t.shape as usize);
        let semi = sh.is_some_and(|s| {
            if s.palette.is_empty() { t.flags & 1 != 0 } else { s.palette.iter().any(|&c| c > 0x8000) }
        });
        let mut fl = 0u8;
        if t.mode & 0x10 != 0 {
            fl |= 1; // drawn without the back-face test
        }
        if semi {
            fl |= 2;
            if sh.map(|s| s.translucency) == Some(1) {
                fl |= 4; // added, not halved
            }
        }
        if t.mode & 0x80 != 0 {
            fl |= 8; // a scrolling material: M2c animates it, M2b draws it still
        }
        put16(&mut mats, x as u16);
        put16(&mut mats, y as u16);
        put16(&mut mats, w as u16);
        put16(&mut mats, h as u16);
        mats.extend_from_slice(&corners(t.flags));
        mats.push(fl);
        mats.extend_from_slice(&[0, 0, 0]);
    }

    // ---- blocks, vertex pools and quads ----
    let (mut blocks, mut verts, mut quads) = (Vec::new(), Vec::new(), Vec::new());
    let mut n_verts = 0u32;
    let mut n_quads = 0u32;
    let pool = |m: &grp::Mesh, verts: &mut Vec<u8>, n: &mut u32| -> (u32, u16) {
        let off = *n;
        for v in &m.verts {
            for k in 0..3 {
                put16(verts, v[k] as u16);
            }
            verts.push(v[3].clamp(0, 255) as u8);
            verts.push(0);
        }
        *n += m.verts.len() as u32;
        (off, m.verts.len() as u16)
    };
    let faces = |qs: &[grp::Quad], quads: &mut Vec<u8>, n: &mut u32| -> (u32, u16) {
        let off = *n;
        for q in qs {
            for k in 0..4 {
                put16(quads, q.v[k]);
            }
            put16(quads, q.tex);
        }
        *n += qs.len() as u32;
        (off, qs.len() as u16)
    };

    for b in &track.blocks {
        let (gv, gvn) = pool(&b.ground, &mut verts, &mut n_verts);
        let (gq, gqn) = faces(&b.ground.quads, &mut quads, &mut n_quads);
        let (gf, gfn) = faces(&b.ground.far, &mut quads, &mut n_quads);
        let (dv, dvn) = pool(&b.detail, &mut verts, &mut n_verts);
        let (dq, dqn) = faces(&b.detail.quads, &mut quads, &mut n_quads);
        for k in 0..3 {
            put32(&mut blocks, ((b.origin[k] * 64.0).round() as i32) as u32);
        }
        for (off, n) in [(gv, gvn), (gq, gqn), (gf, gfn), (dv, dvn), (dq, dqn)] {
            put32(&mut blocks, off);
            put16(&mut blocks, n);
        }
        put16(&mut blocks, 0); // relleno, para que el registro mida 44
    }

    let area: u32 = rects.values().map(|r| (r.w + PAD) * (r.h + PAD)).sum();
    let (usado, formas) = (atlas.h, rects.len());
    let (aw, ah, texels) = atlas.finish();
    let mut out_buf = Vec::new();
    out_buf.extend_from_slice(b"NFST");
    put32(&mut out_buf, 1);
    put16(&mut out_buf, track.blocks.len() as u16);
    put16(&mut out_buf, track.textures.len() as u16);
    put16(&mut out_buf, aw as u16);
    put16(&mut out_buf, ah as u16);
    put32(&mut out_buf, n_verts);
    put32(&mut out_buf, n_quads);
    let atlas_off = out_buf.len() as u32 + 4 + blocks.len() as u32 + mats.len() as u32
        + verts.len() as u32 + quads.len() as u32;
    put32(&mut out_buf, atlas_off);
    out_buf.extend_from_slice(&blocks);
    out_buf.extend_from_slice(&mats);
    out_buf.extend_from_slice(&verts);
    out_buf.extend_from_slice(&quads);
    for t in &texels {
        put16(&mut out_buf, *t);
    }
    std::fs::write(out, &out_buf)?;
    verifica(&out_buf)?;

    let geom = blocks.len() + mats.len() + verts.len() + quads.len();
    println!(
        "{}: {} bloques, {} materiales, {} vertices, {} cuadros",
        out.display(),
        track.blocks.len(),
        track.textures.len(),
        n_verts,
        n_quads
    );
    println!(
        "  geometria {} KB (bloques {}, materiales {}, vertices {}, cuadros {})",
        geom / 1024,
        blocks.len(),
        mats.len(),
        verts.len(),
        quads.len()
    );
    println!(
        "  atlas {aw}x{ah} ARGB1555 = {} KB ({formas} formas, {usado} filas de {ah}, \
{:.0}% ocupado); total del archivo {} KB",
        aw * ah * 2 / 1024,
        100.0 * area as f32 / (aw * ah) as f32,
        out_buf.len() / 1024
    );
    Ok(())
}

/// Relee lo escrito y comprueba que el SH-4 no se va a salir de ningun arreglo: que cada
/// tramo cae dentro del archivo, que todo indice de vertice cae dentro de SU charco y que
/// todo material existe. Un fallo aqui en el PC es media hora; en el Dreamcast, una
/// excepcion de direccion sin pista de donde viene.
fn verifica(d: &[u8]) -> Result<()> {
    let u16a = |o: usize| u16::from_le_bytes([d[o], d[o + 1]]) as usize;
    let u32a = |o: usize| u32::from_le_bytes([d[o], d[o + 1], d[o + 2], d[o + 3]]) as usize;

    if &d[..4] != b"NFST" {
        bail!("cabecera equivocada");
    }
    let (nb, nm) = (u16a(8), u16a(10));
    let (nv, nq, atlas_off) = (u32a(16), u32a(20), u32a(24));
    let (aw, ah) = (u16a(12), u16a(14));
    let blk = 28;
    let mat = blk + nb * 44;
    let vtx = mat + nm * 16;
    let qd = vtx + nv * 8;
    if qd + nq * 10 != atlas_off {
        bail!("el atlas no empieza donde dice la cabecera");
    }
    if atlas_off + aw * ah * 2 != d.len() {
        bail!("el archivo no mide lo que dicen sus tramos");
    }
    for b in 0..nb {
        let o = blk + b * 44;
        // los cinco pares (offset, cuenta): vertices y cuadros de suelo, lejanos y detalle
        let par = |k: usize| (u32a(o + 12 + k * 6), u16a(o + 16 + k * 6));
        let (gv, gvn) = par(0);
        let (dv, dvn) = par(3);
        for (vo, vn) in [(gv, gvn), (dv, dvn)] {
            if vo + vn > nv {
                bail!("bloque {b}: charco de vertices fuera de rango");
            }
        }
        for (k, (vo, vn)) in [(1, (gv, gvn)), (2, (gv, gvn)), (4, (dv, dvn))] {
            let (qo, qn) = par(k);
            if qo + qn > nq {
                bail!("bloque {b}: cuadros fuera de rango");
            }
            for i in 0..qn {
                let q = qd + (qo + i) * 10;
                for c in 0..4 {
                    if u16a(q + c * 2) as usize >= vn {
                        bail!("bloque {b} cuadro {i}: vertice fuera de su charco");
                    }
                }
                if u16a(q + 8) >= nm {
                    bail!("bloque {b} cuadro {i}: material inexistente");
                }
                let _ = vo;
            }
        }
    }
    Ok(())
}
