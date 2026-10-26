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
//! header  "NFSC", u32 version=3, u16 parts, u16 atlas_w, u16 atlas_h, u16 pad,
//!         u32 verts, u32 atlas_offset, f32 half_extents[3], u32 env_offset,
//!         u32 series (44 bytes)
//! parts   parts * 24 bytes: u32 vert_off, u32 vert_n (a multiple of 3), u8 wheel
//!         (0xFF none), u8 subtract, u16 pad, f32 hub[3]
//! verts   verts * 24 bytes: f32 x, y, z, f32 u, v, u32 argb
//! atlas   atlas_w * atlas_h u16 indices, then `series` * 64 ARGB1555 colours: per index
//!         its colour with each of the 64 paints (series-major)
//! env     parts * 8 bytes: u32 tris (0: the part is not reflection-mapped), u32 the GEO
//!         part's vertex count; then, for each reflection-mapped part in order, per
//!         triangle 28 bytes: u16 polygon flags, u16 pad, and per corner u16 its GEO
//!         vertex, i16 normal[3] (the GEO's / 64, PS1 axes)
//! ```
//!
//! The reflection data is what DrawC_Prim's environment path needs (version 2, M2c step
//! 6): the normals pick the texels of the track's reflection maps, the flags say which
//! polygons also take map B, and the GEO vertex lets the port use the normals the game
//! itself has in memory instead, as the reference does when the counts match.
//!
//! # The paint (version 3)
//!
//! A car's paint is `CarState::colour`: the colour 0..15 and the car's two palette flags, 64
//! values. Instead of a file per paint, the car is loaded with each of the 64 and every texel
//! keeps the series of colours it takes; texels with the same series share an index. The port
//! expands the texture for the paint it needs when it loads the car (the SH-4 prepares a
//! texture once, it does not draw), so the result is exactly what `load_car` gives for that
//! paint. Measured: 431 to 575 series per car, so the indices are 16-bit.
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
/// The car's two texture pages cropped side by side (512x256) as ARGB1555, for one paint.
fn paginas(data: &assets::GameData, code: &str, player: bool, paint: u8)
           -> Result<(Vec<u16>, Vec<assets::Rect>)> {
    const PAGE: u32 = 256;
    let mut atlas = assets::Atlas::new(1024);
    let car = assets::load_car(data, &mut atlas, code, player, false, None, paint)?;
    let mut out = vec![0u16; (2 * PAGE * PAGE) as usize];
    for (i, r) in car.page_rects.iter().enumerate() {
        if r.w == 0 {
            continue;
        }
        for y in 0..PAGE {
            for x in 0..PAGE {
                let o = (((r.y + y) * atlas.size + r.x + x) * 4) as usize;
                let px = &atlas.rgba[o..o + 4];
                // alpha 0 is the PS1's colour 0: not drawn
                out[(y * 2 * PAGE + i as u32 * PAGE + x) as usize] = if px[3] < 128 {
                    0
                } else {
                    0x8000 | ((px[0] as u16 >> 3) << 10) | ((px[1] as u16 >> 3) << 5) | (px[2] as u16 >> 3)
                };
            }
        }
    }
    Ok((out, car.page_rects.to_vec()))
}

pub fn bake(data: &assets::GameData, code: &str, player: bool,
            out: &std::path::Path) -> Result<()> {
    let colour = 0u8;
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

    // the atlas: per texel the index of its colour series over the 64 paints
    let mut series: Vec<Vec<u16>> = vec![Vec::with_capacity(64); (aw * ah) as usize];
    for paint in 0..64u8 {
        let (texels, rects) = paginas(data, code, player, paint)?;
        if rects.iter().zip(&car.page_rects).any(|(a, b)| (a.x, a.y, a.w, a.h) != (b.x, b.y, b.w, b.h)) {
            bail!("{code}: la pintura {paint} coloca las paginas en otro sitio (las UV no valdrian)");
        }
        for (s, c) in series.iter_mut().zip(texels) {
            s.push(c);
        }
    }
    let mut index_of: std::collections::HashMap<Vec<u16>, u16> = std::collections::HashMap::new();
    let mut table: Vec<Vec<u16>> = Vec::new();
    let mut indices: Vec<u16> = Vec::with_capacity(series.len());
    for s in series {
        let k = match index_of.get(&s) {
            Some(&k) => k,
            None => {
                if table.len() >= 65536 {
                    bail!("{code}: mas de 65 536 series de color");
                }
                let k = table.len() as u16;
                index_of.insert(s.clone(), k);
                table.push(s);
                k
            }
        };
        indices.push(k);
    }
    let mut texels = Vec::with_capacity(indices.len() * 2 + table.len() * 128);
    for k in &indices {
        texels.extend_from_slice(&k.to_le_bytes());
    }
    for s in &table {
        for c in s {
            texels.extend_from_slice(&c.to_le_bytes());
        }
    }

    // the reflection-mapped parts (DrawC_Prim's environment path)
    let mut env = Vec::new();
    for p in &car.parts {
        let tris = p.env.as_ref().map_or(0, |(_, f)| f.len());
        put32(&mut env, tris as u32);
        put32(&mut env, p.geo_vertices as u32);
    }
    for p in &car.parts {
        let Some((normals, flags)) = &p.env else { continue };
        if normals.len() != 3 * flags.len() || p.env_vertices.len() != normals.len()
            || flags.len() * 3 != p.verts.len() {
            bail!("{code}: datos de reflejo incoherentes en una parte");
        }
        for (t, &fl) in flags.iter().enumerate() {
            put16(&mut env, fl);
            put16(&mut env, 0);
            for k in 0..3 {
                put16(&mut env, p.env_vertices[t * 3 + k]);
                for c in 0..3 {
                    put16(&mut env, normals[t * 3 + k][c] as u16);
                }
            }
        }
    }

    let mut o = Vec::new();
    o.extend_from_slice(b"NFSC");
    put32(&mut o, 3);
    put16(&mut o, car.parts.len() as u16);
    put16(&mut o, aw as u16);
    put16(&mut o, ah as u16);
    put16(&mut o, 0);
    put32(&mut o, n_verts);
    let atlas_off = 44 + parts.len() as u32 + verts.len() as u32;
    put32(&mut o, atlas_off);
    for k in 0..3 {
        putf(&mut o, car.half_extents[k]);
    }
    put32(&mut o, atlas_off + texels.len() as u32);
    put32(&mut o, table.len() as u32);
    o.extend_from_slice(&parts);
    o.extend_from_slice(&verts);
    o.extend_from_slice(&texels);
    o.extend_from_slice(&env);
    std::fs::write(out, &o)?;

    let ruedas = car.parts.iter().filter(|p| p.wheel.is_some()).count();
    let reflejo: usize = car.parts.iter().filter_map(|p| p.env.as_ref()).map(|(_, f)| f.len()).sum();
    println!("  pintura: {} series de color para las 64 pinturas", table.len());
    println!("  reflejo: {reflejo} triangulos en {} partes",
             car.parts.iter().filter(|p| p.env.is_some()).count());
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

/// Bakes a track's reflection maps (`ZTR<nn>R.PSH`) for the cars' reflections (M2c step 6),
/// laid out as the game loads them in VRAM: a 256x256 page with each 128x64 map at u 128,
/// v 64 * n, so the map coordinates `DrawC_PrimStart` leaves in the car cache index it as
/// they are. Format: `"NFSR"`, u32 version=1, then 256 * 256 ARGB1555 texels (alpha 0 is
/// the PS1's colour 0: not drawn).
pub fn bake_reflections(data: &assets::GameData, track_no: u32, out: &std::path::Path) -> Result<()> {
    let mut atlas = assets::Atlas::new(512);
    let Some(r) = assets::load_reflection_maps(data, &mut atlas, track_no) else {
        bail!("ZTR{track_no:02}R.PSH: no lo encuentro o no se lee");
    };
    let mut o = Vec::new();
    o.extend_from_slice(b"NFSR");
    put32(&mut o, 1);
    let mut opacos = 0usize;
    for y in 0..256u32 {
        for x in 0..256u32 {
            let i = (((r.y + y) * atlas.size + r.x + x) * 4) as usize;
            let px = &atlas.rgba[i..i + 4];
            let c: u16 = if px[3] < 128 {
                0
            } else {
                opacos += 1;
                0x8000 | ((px[0] as u16 >> 3) << 10) | ((px[1] as u16 >> 3) << 5) | (px[2] as u16 >> 3)
            };
            put16(&mut o, c);
        }
    }
    std::fs::write(out, &o)?;
    println!("{}: mapas de reflejo 256x256, {opacos} texeles con color", out.display());
    Ok(())
}

/// car-pal-stats CODE: how a car's textures would fit as PVR palettised textures (measuring
/// before deciding the paint format): per texture sheet, its shapes by kind, how many distinct
/// palettes they use and how many colours there are in all.
pub fn pal_stats(data: &assets::GameData, code: &str) -> Result<()> {
    use crate::formats::{psh::Psh, viv::Viv};
    let viv = Viv::parse(&data.read(&format!("ZZZ{code}H.VIV"))?)?;
    for (n, d) in &viv.entries {
        let n = n.to_ascii_lowercase();
        if !n.ends_with(".psh") {
            continue;
        }
        let psh = Psh::parse(d)?;
        let mut kinds = std::collections::BTreeMap::<u8, usize>::new();
        let mut pals = std::collections::BTreeSet::<Vec<u16>>::new();
        let mut colours = std::collections::BTreeSet::<u16>::new();
        let mut paint_rows = 0usize;
        for (i, s) in psh.shapes.iter().enumerate() {
            *kinds.entry(s.kind).or_default() += 1;
            if s.kind == 0x40 || s.kind == 0x7B {
                pals.insert(s.palette.clone());
                colours.extend(s.palette.iter().copied());
            }
            if s.kind == 0x41 && s.width == 1 && s.height == 1 && i > 0 && psh.shapes[i - 1].kind == 0x40 {
                paint_rows = paint_rows.max(s.palette.len() / 16);
            }
        }
        let area: usize = psh.shapes.iter().filter(|s| s.kind != 0x41).map(|s| s.width as usize * s.height as usize).sum();
        println!("{n}: {} formas {kinds:x?}, {} paletas distintas, {} colores, filas de pintura {paint_rows}, area {area} texeles",
                 psh.shapes.len(), pals.len(), colours.len());
        let nombres: Vec<String> = psh.shapes.iter().filter(|s| s.kind != 0x41).map(|s| format!("{}@{},{} {}x{}", s.name, s.vram.0, s.vram.1, s.width, s.height)).collect();
        println!("    {}", nombres.join("  "));
    }
    Ok(())
}

/// car-pal-tuples CODE: loads the car with each of the 64 paints (colour 0..15 and the two
/// palette flags) and counts the distinct colour series per texel of the cropped atlas: how
/// many indices the disc format needs.
pub fn pal_tuples(data: &assets::GameData, code: &str) -> Result<()> {
    let mut series: Vec<Vec<u16>> = Vec::new();
    for paint in 0..64u8 {
        let mut atlas = assets::Atlas::new(1024);
        let car = assets::load_car(data, &mut atlas, code, true, false, None, paint)?;
        let mut page = vec![0u16; 512 * 256];
        for (i, r) in car.page_rects.iter().enumerate() {
            if r.w == 0 {
                continue;
            }
            for y in 0..256u32 {
                for x in 0..256u32 {
                    let o = (((r.y + y) * atlas.size + r.x + x) * 4) as usize;
                    let px = &atlas.rgba[o..o + 4];
                    let c = if px[3] < 128 { 0 } else { 0x8000 | ((px[0] as u16 >> 3) << 10) | ((px[1] as u16 >> 3) << 5) | (px[2] as u16 >> 3) };
                    page[(y * 512 + i as u32 * 256 + x) as usize] = c;
                }
            }
        }
        if series.is_empty() {
            series = vec![Vec::with_capacity(64); page.len()];
        }
        for (s, c) in series.iter_mut().zip(page) {
            s.push(c);
        }
    }
    let total: std::collections::HashSet<&Vec<u16>> = series.iter().collect();
    let left: std::collections::HashSet<&Vec<u16>> = series.iter().enumerate().filter(|(i, _)| i % 512 < 256).map(|(_, s)| s).collect();
    let right: std::collections::HashSet<&Vec<u16>> = series.iter().enumerate().filter(|(i, _)| i % 512 >= 256).map(|(_, s)| s).collect();
    println!("{code}: {} series distintas en el atlas (pagina 0: {}, pagina 1: {})", total.len(), left.len(), right.len());
    Ok(())
}
