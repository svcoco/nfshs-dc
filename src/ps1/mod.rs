//! Bridge to the recompiled original game code: runtime (`ps1rt`), generated NFS4.EXE
//! (`nfs4rc`) and the host-side hardware replacements.

pub mod host;
pub mod memcard;
pub mod video;
pub mod race;
pub mod spu;
/// Stub del SPU para el test diferencial de M1a (ver el modulo).
pub mod diffspu;

pub use ps1rt::*;

/// A CPU wired to the recompiled NFS4.EXE.
pub fn new_cpu(m: Mem) -> Cpu {
    let mut c = Cpu::new(m);
    c.dispatch = nfs4rc::dispatch;
    c.dispatch_at = nfs4rc::dispatch_at;
    // the sound hardware the original's driver programs; interrupts enabled (COP0 SR IEc and
    // IM2) as the BIOS leaves them
    //
    // En modo diff (NFSHS_DIFF_OUT) se usa el mismo stub que el port en C: el modelo
    // fiel reproduce voces y ENDX, el driver de sonido los lee y programa el chip segun
    // lo que encuentra, y con un stub a un lado y el modelo al otro el juego toma
    // caminos distintos. Ver src/ps1/diffspu.rs.
    c.m.bus = if std::env::var_os("NFSHS_DIFF_OUT").is_some() {
        Some(Box::new(diffspu::DiffSpu::default()))
    } else {
        Some(Box::new(spu::Spu::default()))
    };
    c.cop0[12] = 0x401;
    c
}
