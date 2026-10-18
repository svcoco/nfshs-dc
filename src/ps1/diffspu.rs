//! El mismo stub del SPU que el port en C (dc/src/host/spu.c), para el test diferencial
//! de M1a.
//!
//! Por que existe: el modelo completo del SPU de `spu.rs` reproduce voces, envolventes y
//! ENDX, y el driver de sonido del juego **lee** esos registros y programa el chip segun
//! lo que encuentra. Con un stub a un lado y el modelo fiel al otro, el juego toma
//! caminos distintos: en la referencia la IRQ del SPU llega y su manejador corre, y en el
//! port no. Eso hacia imposible comparar la logica recompilada, que es lo que M1a valida.
//!
//! La decision (Jorge, 2026-10-18) es neutralizar el SPU **en los dos lados** para M1a y
//! abordar el SPU fiel en M3, con el audio y su propio criterio. Este modulo es la mitad
//! de esa decision: no sintetiza nada, acepta escrituras, y solo modela el canal 4 de DMA
//! con su interrupcion de fin y la IRQ por direccion.
//!
//! Se activa con `NFSHS_DIFF_OUT` (modo diff), igual que el salto de los frames de carga.

use super::IoBus;

const SPU_BASE: u32 = 0x1F80_1C00;
const SPU_REG_BYTES: u32 = 0x400;
const SPU_RAM_SIZE: u32 = 0x8_0000;

const DMA4_MADR: u32 = 0x1F80_10C0;
const DMA4_BCR: u32 = 0x1F80_10C4;
const DMA4_CHCR: u32 = 0x1F80_10C8;
const DPCR: u32 = 0x1F80_10F0;
const DICR: u32 = 0x1F80_10F4;

pub struct DiffSpu {
    reg: Vec<u16>,
    ram: Vec<u8>,
    transfer_addr: u32,
    dma4: [u32; 3],
    dpcr: u32,
    dicr: u32,
    irqs: u32,
    irq_flag: bool,
}

impl Default for DiffSpu {
    fn default() -> Self {
        Self {
            reg: vec![0; (SPU_REG_BYTES / 2) as usize],
            ram: vec![0; SPU_RAM_SIZE as usize],
            transfer_addr: 0,
            dma4: [0; 3],
            dpcr: 0,
            dicr: 0,
            irqs: 0,
            irq_flag: false,
        }
    }
}

impl DiffSpu {
    fn spucnt(&self) -> u16 {
        self.reg[(0x1AA / 2) as usize]
    }

    /// La RAM de sonido se ha tocado en [addr, addr + len): si la direccion de IRQ cae
    /// dentro, se levanta la linea 9 de I_STAT.
    fn check_irq(&mut self, addr: u32, len: u32) {
        if self.spucnt() & 0x40 == 0 || self.irq_flag {
            return;
        }
        let irq = (self.reg[(0x1A4 / 2) as usize] as u32) << 3;
        if irq.wrapping_sub(addr) & (SPU_RAM_SIZE - 1) < len {
            self.irq_flag = true;
            self.irqs |= 1 << 9;
        }
    }

    fn read_reg(&self, off: u32) -> u16 {
        let off = off & 0x3FE;
        // volumen de envolvente de las voces y direccion de repeticion: no se
        // sintetiza, asi que 0 (el driver da las voces por terminadas)
        if off < 0x180 && (off & 0xF) == 0xC {
            return 0;
        }
        if off < 0x180 && (off & 0xF) == 0xE {
            return 0;
        }
        match off {
            0x1A6 => (self.transfer_addr >> 3) as u16,
            0x1AE => {
                let cnt = self.spucnt();
                let dma = (cnt >> 4) & 3;
                (cnt & 0x3F)
                    | ((self.irq_flag as u16) << 6)
                    | (((dma >= 2) as u16) << 7)
                    | (((dma == 2) as u16) << 8)
                    | (((dma == 3) as u16) << 9)
            }
            _ => self.reg[(off / 2) as usize],
        }
    }

    fn write_reg(&mut self, off: u32, v: u16) {
        let off = off & 0x3FE;
        if off == 0x1A6 {
            self.transfer_addr = (v as u32) << 3;
            return;
        }
        if off == 0x1A8 {
            // escritura manual por la FIFO de datos: dos bytes en la RAM de sonido
            let a = self.transfer_addr & (SPU_RAM_SIZE - 2);
            self.ram[a as usize] = v as u8;
            self.ram[a as usize + 1] = (v >> 8) as u8;
            self.check_irq(a, 2);
            self.transfer_addr = (self.transfer_addr + 2) & (SPU_RAM_SIZE - 1);
            return;
        }
        if off == 0x1AA && v & 0x40 == 0 {
            self.irq_flag = false; // escribir SPUCNT sin el bit 6 reconoce la IRQ
        }
        self.reg[(off / 2) as usize] = v;
    }

    /// Canal 4 de DMA: palabras de 32 bits entre la RAM principal y la de sonido.
    fn dma4_run(&mut self, ram: &mut [u8]) {
        let [madr, bcr, chcr] = self.dma4;
        let words = match (chcr >> 9) & 3 {
            0 => match bcr & 0xFFFF {
                0 => 0x1_0000,
                n => n,
            },
            _ => (bcr & 0xFFFF) * (bcr >> 16),
        };
        let bytes = words * 4;
        let mut src = madr & 0x1F_FFFC;
        let start = self.transfer_addr;
        let to_spu = chcr & 1 != 0;

        for _ in 0..bytes {
            let a = (self.transfer_addr & (SPU_RAM_SIZE - 1)) as usize;
            let s = (src as usize) & (ram.len() - 1);
            if to_spu {
                self.ram[a] = ram[s];
            } else {
                ram[s] = self.ram[a];
            }
            src += 1;
            self.transfer_addr = (self.transfer_addr + 1) & (SPU_RAM_SIZE - 1);
        }

        if (chcr >> 9) & 3 == 1 {
            self.dma4[0] = src;
            self.dma4[1] &= 0xFFFF;
        }
        self.check_irq(start, bytes);
        self.dma4[2] &= !0x0100_0000;
        if self.dicr & (1 << 20) != 0 {
            self.dicr |= 1 << 28;
        }
        self.irqs |= 1 << 20; // bit 16 + canal 4
    }
}

impl IoBus for DiffSpu {
    fn read(&mut self, a: u32, n: u32) -> u32 {
        if (SPU_BASE..SPU_BASE + SPU_REG_BYTES).contains(&a) {
            let off = a - SPU_BASE;
            return match n {
                4 => self.read_reg(off) as u32 | (self.read_reg(off + 2) as u32) << 16,
                2 => self.read_reg(off) as u32,
                _ => (self.read_reg(off) >> ((a & 1) * 8)) as u32 & 0xFF,
            };
        }
        let word = match a & !3 {
            DMA4_MADR => self.dma4[0],
            DMA4_BCR => self.dma4[1],
            DMA4_CHCR => self.dma4[2],
            DPCR => self.dpcr,
            DICR => self.dicr,
            _ => 0,
        };
        word >> ((a & 3) * 8)
    }

    fn write(&mut self, a: u32, v: u32, n: u32, ram: &mut [u8]) {
        match a {
            DMA4_MADR => {
                self.dma4[0] = v & 0xFF_FFFF;
                return;
            }
            DMA4_BCR => {
                self.dma4[1] = v;
                return;
            }
            DMA4_CHCR => {
                self.dma4[2] = v;
                if v & 0x0100_0000 != 0 {
                    self.dma4_run(ram);
                }
                return;
            }
            DPCR => {
                self.dpcr = v;
                return;
            }
            DICR => {
                let flags = self.dicr & 0x7F00_0000 & !(v & 0x7F00_0000);
                self.dicr = (v & 0x00FF_803F) | flags;
                return;
            }
            _ => {}
        }
        if (SPU_BASE..SPU_BASE + SPU_REG_BYTES).contains(&a) {
            let off = a - SPU_BASE;
            match n {
                4 => {
                    self.write_reg(off, v as u16);
                    self.write_reg(off + 2, (v >> 16) as u16);
                }
                2 => self.write_reg(off, v as u16),
                _ => {
                    let cur = self.read_reg(off) as u32;
                    let sh = (a & 1) * 8;
                    self.write_reg(off, ((cur & !(0xFF << sh)) | ((v & 0xFF) << sh)) as u16);
                }
            }
        }
    }

    fn take_irqs(&mut self) -> u32 {
        std::mem::take(&mut self.irqs)
    }

    fn as_any(&mut self) -> &mut dyn std::any::Any {
        self
    }
}
