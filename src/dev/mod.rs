//! Developer commands: headless runs and screenshots, comparisons with DuckStation
//! captures, asset viewers and codec checks (`nfshs <command> ...`).

mod headless;
pub mod refcmp;
pub mod soft;

// The Dreamcast port's track bake. It lives under dc/ because it is the port's code
// (CLAUDE.md section 8), and it is pulled in here so it can reuse src/formats.
#[path = "../../dc/tools/bake/bake.rs"]
mod bake;
#[path = "../../dc/tools/bake/bakecar.rs"]
mod bakecar;

use crate::{app, assets, formats, game, opt, ps1, render};
use anyhow::{Context as _, Result};
use std::path::Path;

pub use headless::{front_end_play, screenshot};

/// The commands [`run`] knows.
pub const COMMANDS: &[&str] = &["ref-compare", "match-shapes", "shot", "dump-shape-pal", "dump-shape", "crop", "dump-psh", "dump-track", "bake-track", "bake-car", "bake-refl", "car-pal-stats", "car-pal-tuples", "verify-decompress", "dump-audio", "boot-race", "boot-main", "dump-movie", "fe-play", "contact", "heap-free", "dump-car"];

/// Run developer command `cmd` (`args` as on the command line); false if there is none.
pub fn run(cmd: &str, args: &[String], data: &assets::GameData, cfg: &game::RaceConfig) -> Result<bool> {
    let (track, variant, car, laps) = (cfg.track, cfg.variant, cfg.player_car, cfg.laps);
    match cmd {
        "ref-compare" => refcmp::compare(data, Path::new(&args[2]))?,
        // match-shapes REF.png FILE.PSH NAME...: where each shape appears in a reference picture
        "match-shapes" => refcmp::match_shapes(Path::new(&args[2]), Path::new(&args[3]), &args[4..])?,
        "shot" => {
            let secs: f32 = opt(args, "--time").unwrap_or("0").parse()?;
            let keys = opt(args, "--keys").unwrap_or("");
            let input = game::Input {
                up: keys.contains('u'),
                down: keys.contains('d'),
                left: keys.contains('l'),
                right: keys.contains('r'),
                handbrake: keys.contains('h'),
                autopilot: keys.contains('a'),
                ..Default::default()
            };
            let out = opt(args, "--out").unwrap_or("shot.png");
            // --taps "s@5,d@5.5,x@6": press Start / Down / Up / accept (x) briefly at those times
            let taps: Vec<(char, f32)> = opt(args, "--taps")
                .unwrap_or("")
                .split(',')
                .filter_map(|t| {
                    let (k, at) = t.split_once('@')?;
                    Some((k.chars().next()?, at.parse().ok()?))
                })
                .collect();
            screenshot(data, cfg, Path::new(out), secs, input, &taps)?;
        }
        "dump-shape-pal" => {
            // dump-shape-pal FILE.PSH NAME PALETTE_SHAPE out.png: a shape drawn with another
            // shape's palette
            let psh = formats::psh::Psh::parse(&std::fs::read(&args[2])?)?;
            let sh = psh.shapes.iter().find(|s| s.name.eq_ignore_ascii_case(&args[3])).context("no such shape")?;
            let pal = psh.shapes.iter().find(|s| s.name.eq_ignore_ascii_case(&args[4])).context("no palette shape")?;
            println!("{} indices {} palette {} / {}", sh.name, sh.indices.len(), sh.palette.len(), pal.palette.len());
            let rgba: Vec<u8> = sh.indices.iter().flat_map(|&i| formats::psh::ps1_color(pal.palette.get(i as usize).copied().unwrap_or(0))).collect();
            let img = image::RgbaImage::from_raw(sh.width as u32, sh.height as u32, rgba).context("size")?;
            image::imageops::resize(&img, img.width() * 2, img.height() * 2, image::imageops::FilterType::Nearest).save(&args[5])?;
        }
        "dump-shape" => {
            // dump-shape FILE.PSH NAME out.png [scale]: one texture shape as PNG
            let psh = formats::psh::Psh::parse(&std::fs::read(&args[2])?)?;
            let sh = psh.shapes.iter().find(|s| s.name.eq_ignore_ascii_case(&args[3])).context("no such shape")?;
            let img = image::RgbaImage::from_raw(sh.width as u32, sh.height as u32, sh.rgba.clone()).context("shape size")?;
            let k: u32 = args.get(5).and_then(|v| v.parse().ok()).unwrap_or(4);
            image::imageops::resize(&img, img.width() * k, img.height() * k, image::imageops::FilterType::Nearest).save(&args[4])?;
        }
        "crop" => {
            // crop IN.png X Y W H OUT.png [scale]: a zoomed piece of a picture (for comparing details)
            let n = |i: usize| -> Result<u32> { Ok(args[i].parse()?) };
            let img = image::open(&args[2])?.to_rgba8();
            let piece = image::imageops::crop_imm(&img, n(3)?, n(4)?, n(5)?, n(6)?).to_image();
            let k: u32 = args.get(8).and_then(|v| v.parse().ok()).unwrap_or(8);
            image::imageops::resize(&piece, piece.width() * k, piece.height() * k, image::imageops::FilterType::Nearest).save(&args[7])?;
        }
        "dump-psh" => dump_psh(Path::new(&args[2]), Path::new(&args[3]))?,
        "dump-track" => dump_track(&args[2], &args[3], Path::new(&args[4]), args.get(5).map(String::as_str))?,
        // bake-track TRACK.GRP TRACK.PSH OUT.TRK: the Dreamcast port's track bake
        "bake-track" => bake::bake(&args[2], &args[3], Path::new(&args[4]))?,
        // bake-car CODE OUT.CAR [--rival]: the Dreamcast port's car bake (all 64 paints)
        "bake-car" => bakecar::bake(data, &args[2], !args.iter().any(|a| a == "--rival"), Path::new(&args[3]))?,
        // bake-refl NN OUT.RFL: the track's reflection maps for the cars' reflections
        "car-pal-tuples" => bakecar::pal_tuples(data, &args[2])?,
        "car-pal-stats" => bakecar::pal_stats(data, &args[2])?,
        "bake-refl" => bakecar::bake_reflections(data, args[2].parse()?, Path::new(&args[3]))?,
        "verify-decompress" => {
            // compare against reference outputs produced by tools/mipsemu.py (re/dec/<name>.bin)
            let (mut ok, mut bad) = (0, 0);
            for f in &args[2..] {
                let src = std::fs::read(f)?;
                let name = Path::new(f).file_name().unwrap().to_string_lossy().to_string();
                let reference = std::fs::read(format!("re/dec/{name}.bin"))?;
                match formats::compress::decompress(&src) {
                    Ok(out) if out == reference => ok += 1,
                    Ok(out) => {
                        bad += 1;
                        let first = out.iter().zip(&reference).position(|(a, b)| a != b);
                        println!("MISMATCH {name}: len {} vs {} first diff {:?}", out.len(), reference.len(), first);
                    }
                    Err(e) => {
                        bad += 1;
                        println!("ERROR {name}: {e}");
                    }
                }
            }
            println!("{ok} ok, {bad} bad");
        }
        "dump-audio" => {
            // dump-audio <file.bnk|.viv|stream> <outdir>: decode banks/streams to WAV with stats
            let d = std::fs::read(&args[2])?;
            let out = Path::new(&args[3]);
            std::fs::create_dir_all(out)?;
            let mut items: Vec<(String, Vec<u8>)> = Vec::new();
            if d.starts_with(b"BIGF") {
                items = formats::viv::Viv::parse(&d)?.entries;
            } else {
                items.push((Path::new(&args[2]).file_name().unwrap().to_string_lossy().into(), d));
            }
            let stats = |pcm: &[i16]| {
                let n = pcm.len().max(1) as f64;
                let rms = (pcm.iter().map(|&v| (v as f64).powi(2)).sum::<f64>() / n).sqrt();
                let diff = pcm.windows(2).map(|w| (w[1] as f64 - w[0] as f64).abs()).sum::<f64>() / n;
                (rms, diff)
            };
            for (name, data) in items {
                let safe: String = name.chars().map(|c| if c.is_ascii_alphanumeric() || c == '.' { c } else { '_' }).collect();
                if data.starts_with(b"BNKl") {
                    let bank = formats::audio::Bank::parse(&data)?;
                    for (i, s) in bank.sounds.iter().enumerate() {
                        if let Some(s) = s {
                            let (rms, diff) = stats(&s.pcm);
                            println!("{name}#{i}: {} ch {} Hz {} samples loop={:?}-{:?} rms={rms:.0} meandiff={diff:.0}", s.channels, s.rate, s.pcm.len(), s.loop_start, s.loop_end);
                            formats::audio::write_wav(&out.join(format!("{safe}_{i:02}.wav")), s)?;
                        }
                    }
                } else if data.starts_with(b"SCHl") {
                    let s = formats::audio::decode_stream(&data)?;
                    let (rms, diff) = stats(&s.pcm);
                    println!("{name}: {} ch {} Hz {:.1}s rms={rms:.0} meandiff={diff:.0}", s.channels, s.rate, s.pcm.len() as f32 / s.channels as f32 / s.rate as f32);
                    formats::audio::write_wav(&out.join(format!("{safe}.wav")), &s)?;
                }
            }
        }
        "boot-race" => {
            // boot-race [--track N --variant V --car C --opponents A,B --laps N]: run the
            // recompiled original race simulation headless. BOOT_SCRIPT="secs:buttons,..."
            // (x cross, s square, c circle, t triangle, l/r/u/d pad, 1-4 L1/R1/L2/R2, - none;
            // a: an analog pad (DualShock), L/R: its left stick fully left/right);
            // GS_SET="hexoff=value,..." pokes GameSetup words.
            use ps1::race::{FrameInput, RaceSetup, RaceThread, pad};
            let mut cars = vec![car.to_string()];
            cars.extend(cfg.opponents.iter().cloned());
            let setup = RaceSetup { track, variant: variant.to_string(), cars, laps: laps as u32 };
            let gs = setup.game_setup()?;
            let t = std::time::Instant::now();
            let mut sim = RaceThread::start(data.root.clone(), gs)?;
            println!("booted into race in {:?}", t.elapsed());
            let show = |s: &ps1::race::Snapshot, label: &str| {
                for (k, c) in s.cars.iter().enumerate() {
                    let f = |v: i32| v as f32 / 65536.0;
                    println!(
                        "  {label} car[{k}] {:4} pos=({:8.2},{:6.2},{:8.2}) speed={:6.2} m/s ({:5.1} mph) rpm={:5} gear={} steer={} lap={}",
                        c.code,
                        f(c.position[0]),
                        f(c.position[1]),
                        f(c.position[2]),
                        f(c.speed),
                        f(c.speed) * 2.237,
                        c.rpm,
                        c.gear,
                        c.steering,
                        c.lap
                    );
                }
            };
            show(&sim.last, "boot");
            let show_cam = |s: &ps1::race::Snapshot| {
                let f = |v: i32| v as f32 / 65536.0;
                println!(
                    "  env {:?}
  camera mode={} H={} display={:?} pos=({:.2},{:.2},{:.2}) rot={:?}",
                    s.view_env,
                    s.camera_mode,
                    s.projection,
                    s.display,
                    f(s.camera_pos[0]),
                    f(s.camera_pos[1]),
                    f(s.camera_pos[2]),
                    s.camera_rot
                );
            };
            let script = std::env::var("BOOT_SCRIPT").unwrap_or_else(|_| "8:-,3:x".into());
            let mut stage = sim.last.countdown;
            for st in script.split(',').filter(|s| !s.is_empty()) {
                let (secs, b) = st.split_once(':').unwrap_or((st, "-"));
                let secs: f32 = secs.parse()?;
                let mut buttons = 0u16;
                let lx = if b.contains('L') { 0x00 } else if b.contains('R') { 0xFF } else { 0x80 };
                let sticks = b.contains(['a', 'L', 'R']).then_some([0x80, 0x80, lx, 0x80]);
                for ch in b.chars() {
                    buttons |= match ch {
                        'x' => pad::CROSS,
                        's' => pad::SQUARE,
                        'c' => pad::CIRCLE,
                        't' => pad::TRIANGLE,
                        'l' => pad::LEFT,
                        'r' => pad::RIGHT,
                        'u' => pad::UP,
                        'd' => pad::DOWN,
                        '1' => pad::L1,
                        '2' => pad::R1,
                        '3' => pad::L2,
                        '4' => pad::R2,
                        'p' => pad::START,
                        _ => 0,
                    };
                }
                let t = std::time::Instant::now();
                let tick0 = sim.last.ticks;
                let trace = std::env::var_os("BOOT_TRACE").is_some();
                for _ in 0..(secs * 60.0).round() as u32 {
                    let prev: Vec<[i32; 3]> = sim.last.cars.iter().map(|c| c.position).collect();
                    let s = sim.step(FrameInput { buttons, buttons2: 0, want_ram: false, sticks: [sticks, None] })?;
                    if trace {
                        let d: Vec<String> = s
                            .cars
                            .iter()
                            .zip(&prev)
                            .take(3)
                            .map(|(c, p)| {
                                let dx = (c.position[0] - p[0]) as f32 / 65536.0;
                                let dz = (c.position[2] - p[2]) as f32 / 65536.0;
                                format!("{:6.3}", (dx * dx + dz * dz).sqrt())
                            })
                            .collect();
                        println!("  frame {} ticks {} sim {} moved {}", s.frame, s.ticks, s.sim_ticks, d.join(" "));
                    }
                    if s.countdown != stage {
                        println!("  frame {}: countdown stage {}", s.frame, s.countdown);
                        stage = s.countdown;
                    }
                }
                println!("{st}: frame {} in {:?}, game ticks +{}, sim ticks {}, overlay {:?}", sim.last.frame, t.elapsed(), sim.last.ticks.wrapping_sub(tick0), sim.last.sim_ticks, sim.last.overlay);
                show(&sim.last, st);
                show_cam(&sim.last);
            }
            if std::env::var_os("BOOT_RAM").is_some() {
                let s = sim.step(FrameInput { buttons: 0, buttons2: 0, want_ram: true, ..Default::default() })?;
                std::fs::create_dir_all("re/boot")?;
                std::fs::write("re/boot/ram.bin", s.ram.as_deref().unwrap_or_default())?;
                println!("RAM written to re/boot/ram.bin");
            }
        }
        "boot-main" => {
            // boot-main: run the original main (front end, then races) headless, for exploring
            let exe = std::fs::read(data.root.join("NFS4.EXE"))?;
            let mut mem = ps1::Mem::default();
            mem.load_exe(&exe);
            for a in (ps1::race::addr::BSS_START..ps1::race::addr::BSS_END).step_by(4) {
                mem.w32(a, 0);
            }
            let mut cpu = ps1::new_cpu(mem);
            cpu.host = Some(Box::new(ps1::host::GameHost::new(data.root.clone())));
            cpu.trace = std::env::var("PS1_TRACE").is_ok();
            cpu.tick_limit = std::env::var("TICKS").ok().and_then(|t| t.parse().ok()).unwrap_or(2_000_000_000);
            // busy-waits on the timer (a race frame stays far below this between host calls)
            cpu.spin_limit = 20_000;
            // BOOT_SCRIPT="secs:buttons,...": pad input per stretch of frames (as boot-race; 'p'
            // Start); the run ends after the script
            let mut script: Vec<(u32, u16)> = Vec::new();
            for st in std::env::var("BOOT_SCRIPT").unwrap_or_else(|_| "30:-".into()).split(',').filter(|s| !s.is_empty()) {
                let (secs, b) = st.split_once(':').unwrap_or((st, "-"));
                let mut buttons = 0u16;
                for ch in b.chars() {
                    use ps1::race::pad;
                    buttons |= match ch {
                        'x' => pad::CROSS, 's' => pad::SQUARE, 'c' => pad::CIRCLE, 't' => pad::TRIANGLE,
                        'l' => pad::LEFT, 'r' => pad::RIGHT, 'u' => pad::UP, 'd' => pad::DOWN, 'p' => pad::START, 'e' => pad::SELECT,
                        _ => 0,
                    };
                }
                script.push(((secs.parse::<f32>()? * 60.0).round() as u32, buttons));
            }
            let mut frame = 0u32;
            // FE_SHOT=frame,frame,...:dir: render those front-end frames to dir/fe_<frame>.png
            let shot: Option<(Vec<u32>, String)> = std::env::var("FE_SHOT")
                .ok()
                .and_then(|v| v.split_once(':').map(|(f, o)| (f.split(',').filter_map(|n| n.parse().ok()).collect(), o.to_string())));
            let shot_at: Vec<u32> = shot.as_ref().map(|s| s.0.clone()).unwrap_or_default();
            let grabbed = std::rc::Rc::new(std::cell::RefCell::new(Vec::new()));
            let grab = grabbed.clone();
            let started = std::time::Instant::now();
            let ram_at: Option<u32> = std::env::var("BOOT_RAM_FRAME").ok().and_then(|f| f.parse().ok());
            // --- test diferencial del port en C (dc/test/diff) -------------------------
            // NFSHS_DIFF_OUT: vuelca el estado por frame en el formato de
            // dc/src/host/dump.c, para comparar con el lado C.
            // NFSHS_DIFF_SCRIPT: entrada guionizada en el mismo formato que lee el host
            // en C ("<campo> <botones>", "#end N"), para que los dos lados reciban
            // exactamente la misma secuencia.
            let mut diff_out: Option<std::io::BufWriter<std::fs::File>> = std::env::var("NFSHS_DIFF_OUT")
                .ok()
                .map(|path| {
                    use std::io::Write;
                    let mut f = std::io::BufWriter::new(std::fs::File::create(&path).expect("NFSHS_DIFF_OUT"));
                    f.write_all(b"NFSD").expect("cabecera");
                    for v in [1u32, 4096, 512, 0] {
                        f.write_all(&v.to_le_bytes()).expect("cabecera");
                    }
                    f
                });
            // NFSHS_DIFF_OVERLAY: vuelca la LISTA DE DIBUJO por frame, para comparar la
            // grabacion 2D del port en C (dc/src/host/overlay.c) contra esta, que es la
            // que alimenta al renderer de referencia. Formato, todo little-endian:
            //   cabecera: "NFSO" u32 version=3 (la 2 anade el showroom al final de cada frame;
            //   la 3, los comandos de la pausa: Text, Shape, ThinBox, PauseBox y Rect)
            //   por frame: u32 frame, u32 cuadros, u32 otros_comandos, y por cuadro
            //              i16 pts[4][2], u32 colores[4], u8 hay_textura, u8 semi,
            //              u8 abr, u8 relleno, u16 tpage, u16 clut, u16 uv[4][2]
            let mut ovl_out: Option<std::io::BufWriter<std::fs::File>> = std::env::var("NFSHS_DIFF_OVERLAY")
                .ok()
                .map(|path| {
                    use std::io::Write;
                    let mut f = std::io::BufWriter::new(std::fs::File::create(&path).expect("NFSHS_DIFF_OVERLAY"));
                    f.write_all(b"NFSO").expect("cabecera");
                    f.write_all(&3u32.to_le_bytes()).expect("cabecera");
                    f
                });
            // NFSHS_DIFF_VIEW: dumps the RACE VIEW per frame, to compare the C port's
            // capture (dc/src/host/game.c) against this one. The game does not build the
            // world's mesh in RAM: this is all a renderer gets from it. Little-endian:
            //   header: "NFSV" u32 version=1
            //   per frame: u32 frame, u32 views, and per view u32 player, i16 rect[4],
            //              i32 camera_pos[3], i32 camera_rot[9], u32 projection,
            //              u16 chunks, then per chunk i16 block, u16 detail
            let mut view_out: Option<std::io::BufWriter<std::fs::File>> = std::env::var("NFSHS_DIFF_VIEW")
                .ok()
                .map(|path| {
                    use std::io::Write;
                    let mut f = std::io::BufWriter::new(std::fs::File::create(&path).expect("NFSHS_DIFF_VIEW"));
                    f.write_all(b"NFSV").expect("cabecera");
                    f.write_all(&5u32.to_le_bytes()).expect("cabecera");
                    f
                });
            let mut diff_steps: Vec<(u32, u16)> = Vec::new();
            let mut diff_end = 0u32;
            if let Ok(path) = std::env::var("NFSHS_DIFF_SCRIPT") {
                let text = std::fs::read_to_string(&path).expect("NFSHS_DIFF_SCRIPT");
                for line in text.lines() {
                    let l = line.trim();
                    if let Some(rest) = l.strip_prefix("#end") {
                        diff_end = rest.trim().parse().unwrap_or(0);
                        continue;
                    }
                    if l.is_empty() || l.starts_with('#') {
                        continue;
                    }
                    let mut it = l.split_whitespace();
                    let Some(at) = it.next().and_then(|t| t.parse::<u32>().ok()) else { continue };
                    let mut buttons = 0u16;
                    for name in it.next().unwrap_or("-").split([',', '+']) {
                        use ps1::race::pad;
                        // "0x1234": the buttons in hexadecimal, as the C host also reads them
                        if let Some(h) = name.trim().strip_prefix("0x").or_else(|| name.trim().strip_prefix("0X")) {
                            buttons |= u16::from_str_radix(h, 16).unwrap_or(0);
                            continue;
                        }
                        buttons |= match name.trim().to_ascii_uppercase().as_str() {
                            "SELECT" => pad::SELECT, "START" => pad::START,
                            "UP" => pad::UP, "RIGHT" => pad::RIGHT,
                            "DOWN" => pad::DOWN, "LEFT" => pad::LEFT,
                            "L1" => pad::L1, "R1" => pad::R1,
                            "L2" => pad::L2, "R2" => pad::R2,
                            "TRIANGLE" => pad::TRIANGLE, "CIRCLE" => pad::CIRCLE,
                            "CROSS" => pad::CROSS, "SQUARE" => pad::SQUARE,
                            _ => 0,
                        };
                    }
                    diff_steps.push((at, buttons));
                }
            }
            // NFSHS_DIFF_AUTOPILOT=FILE: in races, the test autopilot (game/ai.rs) drives the
            // player's car through the pad, on top of the script; every frame's buttons go to
            // FILE as a script in the C port's format, so that the port gets the very same
            // input (a whole race to the finish line, which no fixed script survives)
            let mut pilot: Option<(std::fs::File, game::ai::Driver, Vec<glam::Vec3>, usize, u32, u16)> = std::env::var_os("NFSHS_DIFF_AUTOPILOT")
                .map(|p| (std::fs::File::create(p).expect("NFSHS_DIFF_AUTOPILOT"), game::ai::Driver::default(), Vec::new(), 0, u32::MAX, 0xFFFF));
            let pilot_root = data.root.clone();
            let on_frame: ps1::host::FrameFn = Box::new(move |c, info| {
                frame += 1;
                if let Some(out) = ovl_out.as_mut() {
                    use std::io::Write;
                    use ps1::host::OverlayCmd;
                    let quads: Vec<&OverlayCmd> = info.overlay.iter()
                        .filter(|c| matches!(c, OverlayCmd::Quad { .. } | OverlayCmd::Clip { .. }))
                        .collect();
                    let otros = info.overlay.len() - quads.len();
                    let mut rec: Vec<u8> = Vec::with_capacity(12 + 56 * quads.len());
                    rec.extend_from_slice(&frame.to_le_bytes());
                    rec.extend_from_slice(&(quads.len() as u32).to_le_bytes());
                    rec.extend_from_slice(&(otros as u32).to_le_bytes());
                    for q in quads {
                        if let OverlayCmd::Clip { x, y, w, h } = q {
                            // mismo registro, con el tipo a 1: solo se miran las dos
                            // primeras esquinas (origen y tamano)
                            for v in [*x, *y, *w, *h, 0, 0, 0, 0] {
                                rec.extend_from_slice(&v.to_le_bytes());
                            }
                            rec.extend_from_slice(&[0u8; 16]);
                            rec.extend_from_slice(&[1u8, 0, 0, 0]);
                            rec.extend_from_slice(&[0u8; 20]);
                            continue;
                        }
                        let OverlayCmd::Quad { pts, colours, tex, semi, abr } = q else { continue };
                        for p in pts {
                            rec.extend_from_slice(&p[0].to_le_bytes());
                            rec.extend_from_slice(&p[1].to_le_bytes());
                        }
                        for c in colours {
                            rec.extend_from_slice(&c.to_le_bytes());
                        }
                        rec.extend_from_slice(&[0u8, u8::from(tex.is_some()), u8::from(*semi), *abr]);
                        let (tpage, clut, uv) = match tex {
                            Some(t) => (t.tpage, t.clut, t.uv),
                            None => (0, 0, [[0u16; 2]; 4]),
                        };
                        rec.extend_from_slice(&tpage.to_le_bytes());
                        rec.extend_from_slice(&clut.to_le_bytes());
                        for t in uv {
                            rec.extend_from_slice(&t[0].to_le_bytes());
                            rec.extend_from_slice(&t[1].to_le_bytes());
                        }
                    }
                    // the showroom (version 2): each car and each floor with where it sits in
                    // the list (how many quads and clips before it), as dc/src/host/overlay.c
                    {
                        use ps1::host::OverlayCmd;
                        let mut before = 0u32;
                        let mut cars: Vec<u8> = Vec::new();
                        let mut floors: Vec<u8> = Vec::new();
                        let (mut ncar, mut nfloor) = (0u32, 0u32);
                        let mut huds: Vec<u8> = Vec::new();
                        let mut nhud = 0u32;
                        let quad = |r: &mut Vec<u8>, q: &OverlayCmd| {
                            if let OverlayCmd::Quad { pts, colours, tex, semi, abr } = q {
                                for p in pts {
                                    r.extend_from_slice(&p[0].to_le_bytes());
                                    r.extend_from_slice(&p[1].to_le_bytes());
                                }
                                for c in colours {
                                    r.extend_from_slice(&c.to_le_bytes());
                                }
                                r.extend_from_slice(&[0u8, u8::from(tex.is_some()), u8::from(*semi), *abr]);
                                let (tpage, clut, uv) = match tex {
                                    Some(t) => (t.tpage, t.clut, t.uv),
                                    None => (0, 0, [[0u16; 2]; 4]),
                                };
                                r.extend_from_slice(&tpage.to_le_bytes());
                                r.extend_from_slice(&clut.to_le_bytes());
                                for t in uv {
                                    r.extend_from_slice(&t[0].to_le_bytes());
                                    r.extend_from_slice(&t[1].to_le_bytes());
                                }
                            }
                        };
                        for cmd in info.overlay.iter() {
                            match cmd {
                                OverlayCmd::Quad { .. } | OverlayCmd::Clip { .. } => before += 1,
                                OverlayCmd::ShowroomCar { car, parts, cam_pos, cam_rot, h, ofx, ofy, mirrored, level, env } => {
                                    ncar += 1;
                                    let r = &mut cars;
                                    r.extend_from_slice(&before.to_le_bytes());
                                    let mut code = [0u8; 4];
                                    for (k, b) in car.code.bytes().take(4).enumerate() {
                                        code[k] = b;
                                    }
                                    r.extend_from_slice(&code);
                                    r.extend_from_slice(&[car.colour, u8::from(*mirrored), 0, 0]);
                                    r.extend_from_slice(&level.to_le_bytes());
                                    for v in car.position.iter().chain(car.orient.iter()).chain(car.dimension.iter()) {
                                        r.extend_from_slice(&v.to_le_bytes());
                                    }
                                    let mut p57 = [0u8; 60];
                                    p57[..parts.len().min(57)].copy_from_slice(&parts[..parts.len().min(57)]);
                                    r.extend_from_slice(&p57);
                                    for v in cam_pos.iter().chain(cam_rot.iter()) {
                                        r.extend_from_slice(&v.to_le_bytes());
                                    }
                                    r.extend_from_slice(&h.to_le_bytes());
                                    r.extend_from_slice(&ofx.to_le_bytes());
                                    r.extend_from_slice(&ofy.to_le_bytes());
                                    r.extend_from_slice(&[u8::from(env.is_some()), 0, 0, 0]);
                                    let e = env.unwrap_or_default();
                                    for m in [e.a, e.b] {
                                        for w in m.unwrap_or([0; 4]) {
                                            r.extend_from_slice(&w.to_le_bytes());
                                        }
                                    }
                                    r.extend_from_slice(&e.scroll.to_le_bytes());
                                    r.extend_from_slice(&e.colour.to_le_bytes());
                                    r.extend_from_slice(&e.yaw.to_le_bytes());
                                    r.extend_from_slice(&[0u8, 0]);
                                }
                                OverlayCmd::Text { .. } | OverlayCmd::Shape { .. } | OverlayCmd::ThinBox { .. } | OverlayCmd::PauseBox { .. } | OverlayCmd::Rect { .. } => {
                                    // (version 3) the pause's commands: where they sit and their
                                    // parameters, 120 bytes (the text up to 96, zero padded)
                                    nhud += 1;
                                    let (mut kind, mut semi, mut shape, mut pal, mut space) = (0u8, 0u8, 0u8, 0u8, 0u8);
                                    let (mut dx0, mut dx1, mut x, mut y, mut w, mut hh, mut colour) = (0i8, 0i8, 0i16, 0i16, 0i16, 0i16, 0u32);
                                    let mut text = [0u8; 96];
                                    let mut len = 0u8;
                                    match cmd {
                                        OverlayCmd::Text { x: a, y: b, colour: p, space: s, text: tx } => {
                                            kind = 1;
                                            (x, y, pal, space) = (*a, *b, *p, *s);
                                            let n = tx.len().min(96);
                                            text[..n].copy_from_slice(&tx[..n]);
                                            len = n as u8;
                                        }
                                        OverlayCmd::Shape { shape: s, x: a, y: b, colour: col } => {
                                            kind = 2;
                                            (shape, x, y, colour) = (*s, *a, *b, *col);
                                        }
                                        OverlayCmd::ThinBox { x: a, y: b, w: c2, h: d } => {
                                            kind = 3;
                                            (x, y, w, hh) = (*a, *b, *c2, *d);
                                        }
                                        OverlayCmd::PauseBox { x: a, y: b, w: c2, h: d } => {
                                            kind = 4;
                                            (x, y, w, hh) = (*a, *b, *c2, *d);
                                        }
                                        OverlayCmd::Rect { semi: s, x: a, y: b, w: c2, h: d, colour: col, dx0: e0, dx1: e1 } => {
                                            kind = 5;
                                            (semi, x, y, w, hh, colour, dx0, dx1) = (u8::from(*s), *a, *b, *c2, *d, *col, *e0, *e1);
                                        }
                                        _ => {}
                                    }
                                    let r = &mut huds;
                                    r.extend_from_slice(&before.to_le_bytes());
                                    r.extend_from_slice(&[kind, semi, shape, pal, space, len, dx0 as u8, dx1 as u8]);
                                    for v in [x, y, w, hh] {
                                        r.extend_from_slice(&v.to_le_bytes());
                                    }
                                    r.extend_from_slice(&colour.to_le_bytes());
                                    r.extend_from_slice(&text);
                                }
                                OverlayCmd::ShowroomFloor(v) => {
                                    nfloor += 1;
                                    let qs: Vec<&OverlayCmd> = v.iter().filter(|q| matches!(q, OverlayCmd::Quad { .. })).collect();
                                    floors.extend_from_slice(&before.to_le_bytes());
                                    floors.extend_from_slice(&(qs.len() as u32).to_le_bytes());
                                    for q in qs {
                                        quad(&mut floors, q);
                                    }
                                }
                                _ => {}
                            }
                        }
                        rec.extend_from_slice(&ncar.to_le_bytes());
                        rec.extend_from_slice(&cars);
                        rec.extend_from_slice(&nfloor.to_le_bytes());
                        rec.extend_from_slice(&floors);
                        rec.extend_from_slice(&nhud.to_le_bytes());
                        rec.extend_from_slice(&huds);
                    }
                    out.write_all(&rec).expect("NFSHS_DIFF_OVERLAY");
                }
                if let Some(out) = view_out.as_mut() {
                    use std::io::Write;
                    let mut rec: Vec<u8> = Vec::new();
                    rec.extend_from_slice(&frame.to_le_bytes());
                    rec.extend_from_slice(&(info.views.len() as u32).to_le_bytes());
                    for v in &info.views {
                        rec.extend_from_slice(&v.player.to_le_bytes());
                        for t in v.rect {
                            rec.extend_from_slice(&t.to_le_bytes());
                        }
                        for t in v.camera_pos {
                            rec.extend_from_slice(&t.to_le_bytes());
                        }
                        for t in v.camera_rot {
                            rec.extend_from_slice(&t.to_le_bytes());
                        }
                        rec.extend_from_slice(&v.projection.to_le_bytes());
                        rec.extend_from_slice(&(v.chunks.len() as u16).to_le_bytes());
                        for (block, detail) in &v.chunks {
                            rec.extend_from_slice(&block.to_le_bytes());
                            rec.extend_from_slice(&detail.to_le_bytes());
                        }
                    }
                    // the scenery objects the original draws, after the views
                    let objs: &[ps1::host::ObjectDraw] =
                        info.views.first().map_or(&[], |v| v.objects.as_slice());
                    rec.extend_from_slice(&(objs.len() as u32).to_le_bytes());
                    for o in objs {
                        rec.extend_from_slice(&o.model.to_le_bytes());
                        rec.push(o.billboard as u8);
                        rec.push(0);
                        for t in o.matrix {
                            rec.extend_from_slice(&t.to_le_bytes());
                        }
                        for t in o.pos {
                            rec.extend_from_slice(&t.to_le_bytes());
                        }
                    }
                    // the cars, as raw capture words (99 per car), after the objects
                    let cap = &c.capture;
                    rec.extend_from_slice(&(cap.len() as u32).to_le_bytes());
                    for w in cap {
                        rec.extend_from_slice(&w.to_le_bytes());
                    }
                    // the tyre marks, read from the game's RAM as the reference reads them
                    let skids = ps1::race::read_skids(&mut |a| c.m.r32(a));
                    rec.extend_from_slice(&(skids.len() as u32).to_le_bytes());
                    for q in &skids {
                        for corner in q.corners {
                            for v in corner {
                                rec.extend_from_slice(&v.to_le_bytes());
                            }
                        }
                        for col in q.colour {
                            rec.extend_from_slice(&col.to_le_bytes());
                        }
                        rec.extend_from_slice(&(q.tex as u16).to_le_bytes());
                        rec.extend_from_slice(&q.slice.to_le_bytes());
                    }
                    // the glows (anchor, car, smoke, quads), then the sun's flare and the
                    // weather, as loose quads; each quad as in NFSHS_DIFF_OVERLAY
                    fn quads(cmds: &[ps1::host::OverlayCmd]) -> Vec<u8> {
                        use ps1::host::OverlayCmd;
                        let mut r = Vec::new();
                        for q in cmds {
                            match q {
                                OverlayCmd::Clip { x, y, w, h } => {
                                    for v in [*x, *y, *w, *h, 0, 0, 0, 0] {
                                        r.extend_from_slice(&v.to_le_bytes());
                                    }
                                    r.extend_from_slice(&[0u8; 16]);
                                    r.extend_from_slice(&[1u8, 0, 0, 0]);
                                    r.extend_from_slice(&[0u8; 20]);
                                }
                                OverlayCmd::Quad { pts, colours, tex, semi, abr } => {
                                    for p in pts {
                                        r.extend_from_slice(&p[0].to_le_bytes());
                                        r.extend_from_slice(&p[1].to_le_bytes());
                                    }
                                    for c in colours {
                                        r.extend_from_slice(&c.to_le_bytes());
                                    }
                                    r.extend_from_slice(&[0u8, u8::from(tex.is_some()), u8::from(*semi), *abr]);
                                    let (tpage, clut, uv) = match tex {
                                        Some(t) => (t.tpage, t.clut, t.uv),
                                        None => (0, 0, [[0u16; 2]; 4]),
                                    };
                                    r.extend_from_slice(&tpage.to_le_bytes());
                                    r.extend_from_slice(&clut.to_le_bytes());
                                    for t in uv {
                                        r.extend_from_slice(&t[0].to_le_bytes());
                                        r.extend_from_slice(&t[1].to_le_bytes());
                                    }
                                }
                                _ => {}
                            }
                        }
                        r
                    }
                    fn n_quads(cmds: &[ps1::host::OverlayCmd]) -> usize {
                        cmds.iter().filter(|c| matches!(c, ps1::host::OverlayCmd::Quad { .. } | ps1::host::OverlayCmd::Clip { .. })).count()
                    }
                    rec.extend_from_slice(&(info.glows.len() as u32).to_le_bytes());
                    for g in &info.glows {
                        for v in g.anchor {
                            rec.extend_from_slice(&v.to_le_bytes());
                        }
                        rec.extend_from_slice(&g.car.unwrap_or(0).to_le_bytes());
                        rec.extend_from_slice(&[u8::from(g.smoke), 0]);
                        rec.extend_from_slice(&(n_quads(&g.cmds) as u16).to_le_bytes());
                        rec.extend_from_slice(&quads(&g.cmds));
                    }
                    rec.extend_from_slice(&(n_quads(&info.sky_overlay) as u32).to_le_bytes());
                    rec.extend_from_slice(&quads(&info.sky_overlay));
                    rec.extend_from_slice(&(n_quads(&info.weather) as u32).to_le_bytes());
                    rec.extend_from_slice(&quads(&info.weather));
                    out.write_all(&rec).expect("NFSHS_DIFF_VIEW");
                }
                if let Some(out) = diff_out.as_mut() {
                    use std::io::Write;
                    let mut rec: Vec<u8> = Vec::with_capacity(4636);
                    rec.extend_from_slice(&frame.to_le_bytes());
                    rec.extend_from_slice(&c.ticks.to_le_bytes());
                    // NFSHS_FRAMEDUMP="frame:archivo": la segunda pasada del test
                    // diferencial. Los hashes por pagina dicen *que* pagina cambia; para
                    // ver *que bytes*, hay que volcar los 2 MB en crudo en este mismo
                    // punto (la frontera del frame), no en una frontera de llamada.
                    if let Ok(spec) = std::env::var("NFSHS_FRAMEDUMP") {
                        if let Some((at, path)) = spec.split_once(':') {
                            if at.parse::<u32>().map_or(false, |a| a == frame) {
                                std::fs::write(path, &c.m.ram).ok();
                                println!("RAM volcada en la frontera del frame {frame} -> {path}");
                            }
                        }
                    }
                    for v in c.r {
                        rec.extend_from_slice(&v.to_le_bytes());
                    }
                    rec.extend_from_slice(&c.hi.to_le_bytes());
                    rec.extend_from_slice(&c.lo.to_le_bytes());
                    for v in c.cop0 {
                        rec.extend_from_slice(&v.to_le_bytes());
                    }
                    for v in c.gte_regs {
                        rec.extend_from_slice(&v.to_le_bytes());
                    }
                    rec.extend_from_slice(&c.cont.to_le_bytes());
                    rec.extend_from_slice(&(c.stack.len() as u32).to_le_bytes());
                    // FNV-1a de 64 bits por pagina de 4 KB, igual que el lado C
                    for page in c.m.ram.chunks(4096) {
                        let mut h: u64 = 0xCBF2_9CE4_8422_2325;
                        for &b in page {
                            h ^= b as u64;
                            h = h.wrapping_mul(0x0000_0100_0000_01B3);
                        }
                        rec.extend_from_slice(&h.to_le_bytes());
                    }
                    out.write_all(&rec).expect("volcado");
                }
                if !diff_steps.is_empty() {
                    if diff_end != 0 && frame >= diff_end {
                        return None;
                    }
                    let mut b = diff_steps.iter().rev().find(|(n, _)| *n <= frame).map_or(0, |(_, b)| *b);
                    if let Some((log, driver, path, progress, track, last)) = pilot.as_mut() {
                        use ps1::race::{addr, pad};
                        let player = c.m.r32(addr::HUMAN_CARS);
                        if info.front_end || player == 0 {
                            path.clear();
                            *track = u32::MAX;
                        } else if c.m.r32(addr::SIM_PAUSED) == 0 {
                            // the track's centre line, once per race (the variants share it)
                            let t = c.m.r32(addr::GAMESETUP + 0x3C);
                            if *track != t {
                                *track = t;
                                *driver = game::ai::Driver::default();
                                path.clear();
                                let data = assets::GameData::new(&pilot_root);
                                if let Ok(tr) = assets::load_track(&data, &mut assets::Atlas::new(2048), t, "", &[], None) {
                                    *path = tr.track.vroad.iter().map(|v| assets::ps1_to_world(v.pos)).collect();
                                    if path.len() < 2 {
                                        *path = tr.nodes.clone();
                                    }
                                }
                                *progress = usize::MAX;
                            }
                            let n = path.len();
                            if n >= 2 {
                                let p = game::original::pose(&ps1::race::CarState::read(&mut c.m, player));
                                let car = game::car::Car { pos: p.pos, vel: p.vel, yaw: p.forward.x.atan2(p.forward.z), up: p.up, rot: p.rot, wheel_pose: (p.wheel_rot, p.wheel_steer) };
                                // the nearest centre line point: everywhere at first, then near the last
                                let (from, window) = if *progress == usize::MAX { (0, n / 2 + 1) } else { (*progress, 24.min(n / 2)) };
                                let mut best = (f32::MAX, from);
                                for k in 0..window * 2 {
                                    let i = (from + n * 2 + k - window) % n;
                                    let d = path[i].distance_squared(car.pos);
                                    if d < best.0 {
                                        best = (d, i);
                                    }
                                }
                                *progress = best.1;
                                let ctl = driver.controls(&car, path, *progress, 1.0 / 60.0);
                                for (on, bit) in [(ctl.throttle > 0.5, pad::CROSS), (ctl.brake > 0.5, pad::SQUARE), (ctl.steer < -0.2, pad::LEFT), (ctl.steer > 0.2, pad::RIGHT)] {
                                    if on {
                                        b |= bit;
                                    }
                                }
                            }
                        }
                        if b != *last {
                            use std::io::Write;
                            let _ = writeln!(log, "{frame} {}", if b == 0 { "-".to_string() } else { format!("{b:#06x}") });
                            *last = b;
                        }
                    }
                    return Some([b.into(), 0.into()]);
                }
                if Some(frame) == ram_at {
                    let _ = std::fs::create_dir_all("re/boot");
                    let _ = std::fs::write("re/boot/fe_ram.bin", &c.m.ram);
                }
                if shot_at.contains(&frame) {
                    grab.borrow_mut().push((frame, info.overlay.clone(), info.vram.clone(), (info.display[2], info.display[3])));
                }
                let mut t = frame;
                for &(n, b) in &script {
                    if t <= n {
                        if std::env::var_os("BOOT_FRAMES").is_some() {
                            println!("frame {frame} t {:.2}s ticks {} calls {} field {} {} overlay {} display {:?}", started.elapsed().as_secs_f32(), c.ticks, c.calls, info.field, if info.front_end { "fe" } else { "race" }, info.overlay.len(), info.display);
                            if std::env::var("BOOT_DUMP_FRAME").ok().and_then(|f| f.parse::<u32>().ok()) == Some(frame) {
                                for o in &info.overlay {
                                    println!("  {o:?}");
                                }
                            }
                        }
                        return Some([b.into(), 0.into()]);
                    }
                    t -= n;
                }
                None
            });
            let host = cpu.host.as_mut().expect("host");
            let host = unsafe { &mut *(host.as_mut() as *mut dyn ps1::Host as *mut ps1::host::GameHost) };
            host.frame.on_frame = Some(on_frame);
            let t0 = std::time::Instant::now();
            let r = ps1::race::call(&mut cpu, 0x800A_52A4, &[]);
            println!("main returned: {r:?}");
            if let Ok(path) = std::env::var("NFSHS_CALLCOUNT") {
                let host = cpu.host.as_mut().expect("host");
                let host = unsafe { &mut *(host.as_mut() as *mut dyn ps1::Host as *mut ps1::host::GameHost) };
                let mut v: Vec<(u32, u64, u64)> = host.call_counts.iter().map(|(a, (n, f))| (*a, *n, *f)).collect();
                v.sort();
                let mut out = String::new();
                for (a, n, f) in v {
                    out.push_str(&format!("{a:#010x} {n} {f}
"));
                }
                std::fs::write(&path, out)?;
                println!("call counts -> {path}");
            }
            println!("{:?}: {} calls, {} interpreted, {} backward branches", t0.elapsed(), cpu.calls, cpu.interpreted, cpu.ticks);
            if let Some((_, dir)) = shot {
                let mut r = None;
                for (f, overlay, vram, screen) in grabbed.borrow_mut().drain(..) {
                    let Some(vram) = vram else { continue };
                    // FE_VRAM=x,y,w,h: also that VRAM region (16-bit pixels) as dir/vram_<frame>.png
                    if let Some(r) = std::env::var("FE_VRAM").ok().map(|v| v.split(',').filter_map(|n| n.parse::<usize>().ok()).collect::<Vec<_>>()).filter(|r| r.len() == 4) {
                        let mut img = image::RgbaImage::new(r[2] as u32, r[3] as u32);
                        for (x, y, p) in img.enumerate_pixels_mut() {
                            let c = vram[(r[1] + y as usize) * 1024 + r[0] + x as usize];
                            let rgba = crate::formats::psh::ps1_color(c);
                            *p = image::Rgba([rgba[0], rgba[1], rgba[2], 255]);
                        }
                        img.save(std::path::Path::new(&dir).join(format!("vram_{f}.png")))?;
                    }
                    let showroom = game::fe::Showroom::load(data, &game::fe::FeScene::car_codes(&overlay));
                    let scene = game::fe::FeScene::build(&overlay, &vram, screen, 960.0, 720.0, showroom.as_ref());
                    let n = (scene.under.len() + scene.over.len()) / 6;
                    let r = match &mut r {
                        Some(r) => r,
                        None => r.insert(pollster::block_on(render::Renderer::new(None, (960, 720), &scene.atlas))?),
                    };
                    let out = std::path::Path::new(&dir).join(format!("fe_{f}.png"));
                    app::draw_front_end(r, scene, true).context("shot")?.save(&out)?;
                    println!("front-end frame {f} written ({n} quads)");
                }
            }
        }
        "dump-movie" => {
            // dump-movie FILE.XA FRAME out.png [audio.wav]: one decoded frame (and the audio)
            let bytes = std::fs::read(&args[2])?;
            let m = if bytes.starts_with(b"mTCD") { formats::str::Movie::parse_dct(&bytes) } else { formats::str::Movie::parse(&bytes) };
            println!("{}x{}, {} frames, {} audio samples at {} Hz{}", m.width, m.height, m.frames.len(), m.audio.len(), m.rate, if m.stereo { " stereo" } else { "" });
            let n: usize = args[3].parse()?;
            let rgba = formats::str::Decoder::default().frame(&m.frames[n], m.width, m.height);
            image::RgbaImage::from_raw(m.width, m.height, rgba).context("image")?.save(&args[4])?;
            if let Some(wav) = args.get(5) {
                let mut out = Vec::new();
                let ch = if m.stereo { 2u16 } else { 1 };
                let bytes = (m.audio.len() * 2) as u32;
                out.extend_from_slice(b"RIFF");
                out.extend_from_slice(&(36 + bytes).to_le_bytes());
                out.extend_from_slice(b"WAVEfmt ");
                out.extend_from_slice(&16u32.to_le_bytes());
                out.extend_from_slice(&1u16.to_le_bytes());
                out.extend_from_slice(&ch.to_le_bytes());
                out.extend_from_slice(&m.rate.to_le_bytes());
                out.extend_from_slice(&(m.rate * 2 * ch as u32).to_le_bytes());
                out.extend_from_slice(&(2 * ch).to_le_bytes());
                out.extend_from_slice(&16u16.to_le_bytes());
                out.extend_from_slice(b"data");
                out.extend_from_slice(&bytes.to_le_bytes());
                for s in &m.audio {
                    out.extend_from_slice(&s.to_le_bytes());
                }
                std::fs::write(wav, out)?;
            }
        }
        "fe-play" => {
            // fe-play SCRIPT SHOTS DIR: the app's path through the original (front end, race,
            // back) headless. SCRIPT "secs:keys,..." (x Cross/accept, t Triangle, c Circle,
            // s Square, p Start, u/d/l/r d-pad; in races u accelerates as in the window; g a
            // gamepad that step, so a plugged in or pulled out pad;
            // "keys|keys" the second controller's after the bar, as PS1 buttons), SHOTS
            // "frame,frame,..." screenshots into DIR
            let mut script = Vec::new();
            for st in args[2].split(',').filter(|s| !s.is_empty()) {
                let (secs, keys) = st.split_once(':').unwrap_or((st, "-"));
                // "keys|keys2": the second controller's after a bar
                let (keys, keys2) = keys.split_once('|').unwrap_or((keys, ""));
                let pad2 = {
                    use ps1::race::pad;
                    let mut b = 0;
                    for (k, bit) in [('u', pad::UP), ('d', pad::DOWN), ('l', pad::LEFT), ('r', pad::RIGHT), ('x', pad::CROSS), ('t', pad::TRIANGLE), ('c', pad::CIRCLE), ('s', pad::SQUARE), ('p', pad::START), ('e', pad::SELECT)] {
                        if keys2.contains(k) {
                            b |= bit;
                        }
                    }
                    b
                };
                // (FEPLAY_PAD=stick: the directions are the stick's alone)
                let dpad = std::env::var("FEPLAY_PAD").as_deref() != Ok("stick");
                let i = game::Input {
                    pad2,
                    up: dpad && keys.contains('u'),
                    down: dpad && keys.contains('d'),
                    left: dpad && keys.contains('l'),
                    right: dpad && keys.contains('r'),
                    accept: keys.contains('x'),
                    back: keys.contains('t'),
                    // (Triangle in a race: the camera)
                    camera: keys.contains('t'),
                    circle: keys.contains('c'),
                    square: keys.contains('s'),
                    start: keys.contains('p'),
                    select: keys.contains('e'),
                    autopilot: keys.contains('a'),
                    // FEPLAY_PAD=dpad|stick: a gamepad, its directions from the d-pad or the
                    // left stick (as the window's gamepad input has them)
                    // ('g': this step's on a gamepad whatever FEPLAY_PAD says, as if one had
                    // just been plugged in)
                    sticks: match std::env::var("FEPLAY_PAD").as_deref() {
                        _ if keys.contains('g') => Some([0x80; 4]),
                        Ok("dpad") => Some([0x80; 4]),
                        Ok("stick") => {
                            let axis = |neg: char, pos: char| if keys.contains(neg) { 0x00 } else if keys.contains(pos) { 0xFF } else { 0x80 };
                            Some([0x80, 0x80, axis('l', 'r'), axis('u', 'd')])
                        }
                        _ => None,
                    },
                    ..Default::default()
                };
                script.push((secs.parse::<f32>()?, i));
            }
            let shots: Vec<u32> = args[3].split(',').filter_map(|s| s.parse().ok()).collect();
            front_end_play(data, &script, &shots, Path::new(&args[4]))?;
            // the script is done: don't wait for the original to wind down (it would run on,
            // unpaced, through its results and front end)
            use std::io::Write;
            std::io::stdout().flush().ok();
            std::process::exit(0);
        }
        "contact" => {
            // contact OUT COLS IMG...: the images at half size in a grid (for reviewing shots)
            let cols: u32 = args[3].parse()?;
            let imgs: Vec<image::RgbaImage> = args[4..].iter().map(|p| image::open(p).map(|i| i.to_rgba8())).collect::<Result<_, _>>()?;
            let (w, h) = imgs.iter().fold((0, 0), |(w, h), i| (w.max(i.width() / 2), h.max(i.height() / 2)));
            let rows = (imgs.len() as u32).div_ceil(cols);
            let mut out = image::RgbaImage::new(w * cols, h * rows);
            for (k, i) in imgs.iter().enumerate() {
                let s = image::imageops::resize(i, i.width() / 2, i.height() / 2, image::imageops::FilterType::Triangle);
                image::imageops::overlay(&mut out, &s, ((k as u32 % cols) * w) as i64, ((k as u32 / cols) * h) as i64);
            }
            out.save(&args[2])?;
        }
        "heap-free" => {
            // heap-free <ram.bin>: the game's largest free heap block in a RAM image
            let mut mem = ps1::Mem::default();
            mem.ram.copy_from_slice(&std::fs::read(&args[2])?);
            let mut cpu = ps1::new_cpu(mem);
            cpu.host = Some(Box::new(ps1::host::GameHost::new(data.root.clone())));
            println!("largest free block: {:#x}", ps1::race::call(&mut cpu, 0x800E_5E70, &[])?);
        }
        "dump-car" => dump_car(Path::new(&args[2]), Path::new(&args[3]), args.get(4).map(String::as_str))?,
        _ => return Ok(false),
    }
    Ok(true)
}

/// Pack every shape of a PSH (or every PSH inside a VIV) onto a sheet PNG.
fn dump_psh(src: &Path, out: &Path) -> Result<()> {
    let d = std::fs::read(src)?;
    let mut pshs = Vec::new();
    if &d[..4] == b"BIGF" {
        for (n, e) in formats::viv::Viv::parse(&d)?.entries {
            if e.starts_with(b"SHPP") {
                let psh = formats::psh::Psh::parse(&e)?;
                println!("-- {n}: {}", psh.shapes.iter().map(|s| s.name.as_str()).collect::<Vec<_>>().join(" "));
                pshs.push(psh);
            }
        }
    } else {
        pshs.push(formats::psh::Psh::parse(&d)?);
    }
    let shapes: Vec<_> = pshs.iter().flat_map(|p| p.shapes.iter()).collect();
    let sheet_w = 1024u32;
    let (mut x, mut y, mut row_h) = (0u32, 0u32, 0u32);
    let mut placed = Vec::new();
    for s in &shapes {
        let (w, h) = (s.width as u32 * 2, s.height as u32 * 2);
        if x + w > sheet_w {
            x = 0;
            y += row_h + 2;
            row_h = 0;
        }
        placed.push((x, y));
        x += w + 2;
        row_h = row_h.max(h);
        println!(
            "{:4} kind={:#x} {}x{} center={:?} vram={:?} pal={} abr={}",
            s.name, s.kind, s.width, s.height, s.center, s.vram, s.palette.len(), s.translucency
        );
    }
    let mut img = image::RgbaImage::from_pixel(sheet_w, y + row_h + 2, image::Rgba([255, 0, 255, 255]));
    for (s, &(px, py)) in shapes.iter().zip(&placed) {
        for yy in 0..s.height as u32 * 2 {
            for xx in 0..s.width as u32 * 2 {
                let i = ((yy / 2) * s.width as u32 + xx / 2) as usize * 4;
                let mut c = [s.rgba[i], s.rgba[i + 1], s.rgba[i + 2], 255];
                if s.rgba[i + 3] == 0 {
                    c = if (xx / 4 + yy / 4) % 2 == 0 { [90, 90, 90, 255] } else { [60, 60, 60, 255] };
                }
                img.put_pixel(px + xx, py + yy, image::Rgba(c));
            }
        }
    }
    img.save(out)?;
    Ok(())
}

/// Build the 256x256 car texture page from every PSH inside a car VIV.
fn car_atlas(viv: &formats::viv::Viv) -> Result<formats::psh::Atlas> {
    let mut atlas = formats::psh::Atlas::new(256);
    // body page variants: skip the duplicate "d" sheet and the "g"/"i" extras for now
    for (name, data) in &viv.entries {
        let n = name.to_ascii_lowercase();
        // (the variant letter follows the base name, "zz" and four letters: zzsand.psh is none)
        let Some(stem) = n.strip_suffix(".psh") else { continue };
        if matches!(stem.chars().nth(6), Some('d' | 'g' | 'i')) {
            continue;
        }
        let psh = formats::psh::Psh::parse(data)?;
        atlas.place(&psh.shapes, 64);
    }
    Ok(atlas)
}

fn dump_car(src: &Path, out: &Path, parts: Option<&str>) -> Result<()> {
    use glam::{Mat4, Vec2, Vec3};
    let viv = formats::viv::Viv::parse(&std::fs::read(src)?)?;
    let (_, geo) = viv.find_suffix(".geo").ok_or_else(|| anyhow::anyhow!("no geo"))?;
    let geo = formats::geo::Geo::parse(geo)?;
    let atlas = car_atlas(&viv)?;
    image::RgbaImage::from_raw(256, 256, atlas.rgba.clone()).unwrap().save(out.with_extension("atlas.png"))?;
    let sel: Vec<usize> = match parts {
        Some(p) => p.split(',').map(|x| x.parse().unwrap()).collect(),
        None => (0..geo.parts.len()).collect(),
    };
    let mut tris = Vec::new();
    for &pi in &sel {
        let part = &geo.parts[pi];
        if parts.is_some() {
            let tex: std::collections::BTreeSet<u16> = part.polys.iter().map(|p| p.texture as u16).collect();
            println!("part {pi}: {} polys, textures {tex:?}, flags {:x?}", part.polys.len(), part.polys.iter().map(|p| (p.flags, p.flags2)).collect::<std::collections::BTreeSet<_>>());
        }
        let off = Vec3::new(part.position[0] as f32, part.position[1] as f32, part.position[2] as f32) / 256.0;
        for poly in &part.polys {
            let p = poly.v.map(|i| {
                let v = part.verts.get(i as usize).copied().unwrap_or([0; 3]);
                Vec3::new(v[0] as f32, v[1] as f32, v[2] as f32) + off
            });
            let uv = poly.uv.map(|t| Vec2::new(t[0] as f32 + 0.5, t[1] as f32 + 0.5));
            tris.push(soft::Tri { p, uv, tex: 0, color: [255, 255, 255, 255] });
            // PS1 has no backface culling unless requested; emit reverse winding too
            tris.push(soft::Tri { p: [p[0], p[2], p[1]], uv: [uv[0], uv[2], uv[1]], tex: 0, color: [255; 4] });
        }
    }
    let tex = vec![soft::Texture { w: 256, h: 256, rgba: atlas.rgba }];
    let proj = Mat4::perspective_rh_gl(0.7, 4.0 / 3.0, 10.0, 10000.0);
    let views = [Vec3::new(2000.0, 0.0, 0.0), Vec3::new(-900.0, 400.0, -1100.0), Vec3::new(0.0, 1800.0, 1.0)];
    let mut sheet = image::RgbaImage::new(640 * 3, 480);
    for (k, eye) in views.iter().enumerate() {
        let view = Mat4::look_at_rh(*eye, Vec3::ZERO, Vec3::Y);
        let img = soft::render(&tris, &tex, proj * view, 640, 480, [40, 60, 90, 255]);
        image::imageops::overlay(&mut sheet, &img, 640 * k as i64, 0);
    }
    sheet.save(out)?;
    Ok(())
}

fn dump_track(grp: &str, psh: &str, out: &Path, view: Option<&str>) -> Result<()> {
    use glam::{Mat4, Vec2, Vec3};
    let track = formats::grp::Track::parse(&std::fs::read(grp)?)?;
    let psh = formats::psh::Psh::parse(&std::fs::read(psh)?)?;
    println!("nodes={} blocks={} textures={} vroad={}", track.nodes.len(), track.blocks.len(), track.textures.len(), track.vroad.len());
    if let Some(n) = std::env::var_os("NFSHS_BLOCKS") {
        // NFSHS_BLOCKS=N lists N blocks (anything unparsable lists them all), for sizing
        // a port's per-frame budget: the game picks which blocks to draw each frame.
        let n = n.to_str().and_then(|s| s.parse().ok()).unwrap_or(usize::MAX);
        for (i, b) in track.blocks.iter().enumerate().take(n) {
            let ymax = |m: &formats::grp::Mesh| m.verts.iter().map(|v| v[1] as f32 / 64.0).fold(f32::MIN, f32::max);
            let ymin = |m: &formats::grp::Mesh| m.verts.iter().map(|v| v[1] as f32 / 64.0).fold(f32::MAX, f32::min);
            println!(
                "block {i}: origin {:?} ground {}+{} far quads (y {:.1}..{:.1}), detail {} (y {:.1}..{:.1}), placements {}",
                b.origin, b.ground.quads.len(), b.ground.far.len(), ymin(&b.ground), ymax(&b.ground), b.detail.quads.len(), ymin(&b.detail), ymax(&b.detail), b.placements.len()
            );
        }
    }
    let textures: Vec<soft::Texture> = psh
        .shapes
        .iter()
        .map(|s| soft::Texture { w: s.width.max(1) as u32, h: s.height.max(1) as u32, rgba: s.rgba.clone() })
        .collect();
    let mut tris = Vec::new();
    for b in &track.blocks {
        for mesh in [&b.ground, &b.detail] {
            for q in &mesh.quads {
                let te = track.textures.get(q.tex as usize);
                let shape = te.map(|t| t.shape as usize).unwrap_or(usize::MAX);
                let (w, h) = textures.get(shape).map(|t| (t.w as f32, t.h as f32)).unwrap_or((1.0, 1.0));
                let p: Vec<Vec3> = q
                    .v
                    .iter()
                    .map(|&i| {
                        let v = mesh.verts.get(i as usize).copied().unwrap_or([0; 4]);
                        let w = b.origin + Vec3::new(v[0] as f32, v[1] as f32, v[2] as f32) / 64.0;
                        Vec3::new(-w.x, w.y, w.z)
                    })
                    .collect();
                let uv = [Vec2::new(0.0, 0.0), Vec2::new(w, 0.0), Vec2::new(w, h), Vec2::new(0.0, h)];
                for (a, b2, c) in [(0, 1, 2), (0, 2, 3)] {
                    tris.push(soft::Tri { p: [p[a], p[b2], p[c]], uv: [uv[a], uv[b2], uv[c]], tex: shape, color: [255, 0, 255, 255] });
                }
            }
        }
    }
    let flip = |v: Vec3| Vec3::new(-v.x, v.y, v.z);
    let nodes: Vec<Vec3> = track.nodes.iter().map(|&v| flip(v)).collect();
    let n = nodes.len();
    let (eye, target, fov) = match view {
        Some(v) => {
            let i: usize = v.parse()?;
            let a = nodes[i % n];
            let b = nodes[(i + 3) % n];
            let dir = (b - a).normalize();
            (a - dir * 12.0 + Vec3::Y * 5.0, b + Vec3::Y * 1.0, 1.1)
        }
        None => {
            let min = nodes.iter().fold(Vec3::splat(f32::MAX), |m, v| m.min(*v));
            let max = nodes.iter().fold(Vec3::splat(f32::MIN), |m, v| m.max(*v));
            let c = (min + max) * 0.5;
            let ext = (max - min).max_element();
            (c + Vec3::new(0.0, ext * 1.2, 1.0), c, 0.9)
        }
    };
    let proj = Mat4::perspective_rh_gl(fov, 4.0 / 3.0, 0.5, 20000.0);
    let img = soft::render(&tris, &textures, proj * Mat4::look_at_rh(eye, target, Vec3::Y), 960, 720, [110, 150, 200, 255]);
    img.save(out)?;
    Ok(())
}

