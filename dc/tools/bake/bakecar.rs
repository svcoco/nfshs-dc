//! Bakes a car for the Dreamcast port: its VIV's GEO and texture pages into one file the
//! SH-4 can draw from (M2c, step 4).
//!
//! It reuses `assets::load_car`, which already knows the hard parts: the VIV naming
//! (`ZZZ<code>H.VIV` for the player's, `S` for the others), the two 256x256 texture pages,
//! the paint, and the parts with their wheel and hub. What is left here is writing it out.
//!
//! # What goes in
//!
//! A car model is **parts**, indexed exactly like the game's `R3DCar_ObjectVisible`, so the
//! port can take the 57 bytes it already records and use them as the index. Each part is a
//! list of **triangles** (not quads like the track). Wheel parts carry which wheel they are
//! and where their hub is, because the original spins and steers them around it.
//!
//! The vertices come out of `load_car` already in world units and with their UVs in the
//! atlas, so the SH-4 only transforms and submits.
//!
//! # Format (little-endian)
//!
//! ```text
//! header  "NFSC", u32 version=1, u16 parts, u16 atlas_w, u16 atlas_h, u16 pad,
//!         u32 verts, u32 atlas_offset, f32 half_extents[3]            (36 bytes)
//! parts   parts * 24 bytes: u32 vert_off, u32 vert_n (a multiple of 3), u8 wheel
//!         (0xFF none), u8 subtract, u16 pad, f32 hub[3]
//! verts   verts * 24 bytes: f32 x, y, z, f32 u, v, u32 argb
//! atlas   atlas_size * atlas_size ARGB1555 texels
//! ```
use anyhow::{Result, bail};

use crate::assets;

fn put16(o: &mut Vec<u8>, v: u16) {
    o.extend_from_slice(&v.to_le_bytes());
}

fn put32(o: &mut Vec<u8>, v: u32) {
    o.extend_from_slice(&v.to_le_bytes());
}

fn putf(o: &mut Vec<u8>, v: f32) {
    o.extend_from_slice(&v.to_le_bytes());
}

/// `colour` is the paint as `R3DCar_Instantiate3DCar` reads it (see `CarState::colour`).
pub fn bake(data: &assets::GameData, code: &str, player: bool, colour: u8,
            out: &std::path::Path) -> Result<()> {
    // `load_car` packs into a square shelf atlas, and a car's two 256x256 pages plus the
    // loader's white texel do not fit in 512 **by one row**; it warns and drops a page
    // without failing, que es peor. Asi que se carga en uno holgado y despues se recorta
    // a 512x256: solo las dos paginas, una al lado de la otra, y las UV se recolocan.
    // Son 0,25 MB de VRAM del PVR por coche en vez de 2.
    let mut atlas = assets::Atlas::new(1024);
    let car = assets::load_car(data, &mut atlas, code, player, false, None, colour)?;
    if car.parts.is_empty() {
        bail!("{code}: el modelo no tiene partes");
    }
    const PAGE: u32 = 256;
    let (aw, ah) = (2 * PAGE, PAGE);
    for (i, r) in car.page_rects.iter().enumerate() {
        if r.w != 0 && (r.w != PAGE || r.h != PAGE) {
            bail!("{code}: la pagina {i} mide {}x{}, esperaba {PAGE}x{PAGE}", r.w, r.h);
        }
    }
    // el recorte: cada pagina a su mitad
    let mut rgba = vec![0u8; (aw * ah * 4) as usize];
    for (i, r) in car.page_rects.iter().enumerate() {
        if r.w == 0 {
            continue;
        }
        for y in 0..PAGE {
            let src = (((r.y + y) * atlas.size + r.x) * 4) as usize;
            let dst = ((y * aw + i as u32 * PAGE) * 4) as usize;
            rgba[dst..dst + (PAGE * 4) as usize]
                .copy_from_slice(&atlas.rgba[src..src + (PAGE * 4) as usize]);
        }
    }
    // y las UV, de la posicion que tenian en el atlas grande a la del recorte
    let remap = |uv: [f32; 2]| -> [f32; 2] {
        let (px, py) = (uv[0] * car.atlas_size, uv[1] * car.atlas_size);
        for (i, r) in car.page_rects.iter().enumerate() {
            if r.w != 0 && px >= r.x as f32 && px <= (r.x + r.w) as f32
                && py >= r.y as f32 && py <= (r.y + r.h) as f32 {
                return [(px - r.x as f32 + i as f32 * PAGE as f32) / aw as f32,
                        (py - r.y as f32) / ah as f32];
            }
        }
        [0.0, 0.0]
    };

    let (mut parts, mut verts) = (Vec::new(), Vec::new());
    let mut n_verts = 0u32;
    for p in &car.parts {
        // the vertices come in triangles; anything else would mean the GEO was misread
        if p.verts.len() % 3 != 0 {
            bail!("{code}: una parte tiene {} vertices, no multiplo de 3", p.verts.len());
        }
        put32(&mut parts, n_verts);
        put32(&mut parts, p.verts.len() as u32);
        parts.push(p.wheel.map_or(0xFF, |w| w as u8));
        parts.push(p.subtract as u8);
        put16(&mut parts, 0);
        for k in 0..3 {
            putf(&mut parts, p.hub[k]);
        }
        for v in &p.verts {
            for k in 0..3 {
                putf(&mut verts, v.pos[k]);
            }
            let uv = remap(v.uv);
            putf(&mut verts, uv[0]);
            putf(&mut verts, uv[1]);
            // the colour comes as 0..1 floats with 1.0 neutral; the PVR wants ARGB bytes
            let b = |x: f32| (x.clamp(0.0, 1.0) * 255.0 + 0.5) as u32;
            put32(&mut verts, 0xFF00_0000 | (b(v.color[0]) << 16) | (b(v.color[1]) << 8)
                              | b(v.color[2]));
        }
        n_verts += p.verts.len() as u32;
    }

    // the atlas, RGBA8 -> ARGB1555 (alpha 0 is the PS1's colour 0: not drawn)
    let mut texels = Vec::with_capacity((aw * ah * 2) as usize);
    for px in rgba.chunks_exact(4) {
        let c: u16 = if px[3] < 128 {
            0
        } else {
            0x8000 | ((px[0] as u16 >> 3) << 10) | ((px[1] as u16 >> 3) << 5) | (px[2] as u16 >> 3)
        };
        texels.extend_from_slice(&c.to_le_bytes());
    }

    let mut o = Vec::new();
    o.extend_from_slice(b"NFSC");
    put32(&mut o, 1);
    put16(&mut o, car.parts.len() as u16);
    put16(&mut o, aw as u16);
    put16(&mut o, ah as u16);
    put16(&mut o, 0);
    put32(&mut o, n_verts);
    let atlas_off = 36 + parts.len() as u32 + verts.len() as u32;
    put32(&mut o, atlas_off);
    for k in 0..3 {
        putf(&mut o, car.half_extents[k]);
    }
    o.extend_from_slice(&parts);
    o.extend_from_slice(&verts);
    o.extend_from_slice(&texels);
    std::fs::write(out, &o)?;

    let ruedas = car.parts.iter().filter(|p| p.wheel.is_some()).count();
    println!(
        "{}: {} partes ({ruedas} ruedas), {n_verts} vertices ({} triangulos), \
atlas {aw}x{ah}; {} KB",
        out.display(),
        car.parts.len(),
        n_verts / 3,
        o.len() / 1024
    );
    Ok(())
}
