#include "spu.h"

#include <stdio.h>

#include <string.h>

/* SPUCNT, el registro de control (0x1F801DAA). */
static uint16_t spucnt(const SpuStub *s)
{
    return s->reg[0x1AAu / 2u];
}

/* La RAM de sonido se ha tocado en [addr, addr + len): si la direccion de IRQ cae
 * dentro, se levanta la interrupcion del SPU (linea 9 de I_STAT).
 *
 * Esto es lo que el juego usa para saber que una transferencia llego a un punto, y sin
 * ello no corre su manejador de 0x80104338, que es la primera divergencia que encontro
 * el test diferencial. Portado de check_irq de src/ps1/spu.rs. */
static void check_irq(SpuStub *s, uint32_t addr, uint32_t len)
{
    uint32_t irq;
    if ((s->reg[0x1AAu / 2u] & 0x40u) == 0u || s->irq_flag)
        return;                             /* IRQ deshabilitada, o ya pendiente */
    irq = (uint32_t)s->reg[0x1A4u / 2u] << 3;   /* la direccion va en unidades de 8 */
    if (((irq - addr) & (SPU_RAM_SIZE - 1u)) < len) {
        s->irq_flag = 1;
        s->irqs |= 1u << 9;
        s->irqs_raised++;
    }
}

static uint16_t read_reg(const SpuStub *s, uint32_t off)
{
    off &= 0x3FEu;
    /* volumen de envolvente de cada voz (+0xC) y direccion de repeticion (+0xE).
     * Como no se sintetiza, el volumen es 0: el driver de sonido lo lee para saber
     * cuando una voz ha terminado, y asi las da todas por terminadas. */
    if (off < 0x180u && (off & 0xFu) == 0xCu)
        return 0;
    if (off < 0x180u && (off & 0xFu) == 0xEu)
        return 0;
    switch (off) {
    case 0x1A6u:                       /* direccion de transferencia, en unidades de 8 */
        return (uint16_t)(s->transfer_addr >> 3);
    case 0x1AEu: {                     /* SPUSTAT, derivado de SPUCNT como el hardware */
        uint16_t cnt = spucnt(s);
        uint16_t dma = (uint16_t)((cnt >> 4) & 3u);
        return (uint16_t)((cnt & 0x3Fu)
                          | (uint16_t)(s->irq_flag ? (1u << 6) : 0u)
                          | (uint16_t)((dma >= 2u) ? (1u << 7) : 0u)
                          | (uint16_t)((dma == 2u) ? (1u << 8) : 0u)
                          | (uint16_t)((dma == 3u) ? (1u << 9) : 0u));
    }
    default:
        return s->reg[off / 2u];
    }
}

static void write_reg(SpuStub *s, uint32_t off, uint16_t v)
{
    off &= 0x3FEu;
    if (off == 0x1A6u) {
        s->transfer_addr = (uint32_t)v << 3;
        return;
    }
    if (off == 0x1A8u) {
        /* escritura manual por la FIFO de datos: dos bytes en la RAM de sonido */
        uint32_t a = s->transfer_addr & (SPU_RAM_SIZE - 2u);
        s->ram[a] = (uint8_t)v;
        s->ram[a + 1u] = (uint8_t)(v >> 8);
        check_irq(s, a, 2u);
        s->transfer_addr = (s->transfer_addr + 2u) & (SPU_RAM_SIZE - 1u);
        return;
    }
    if (off == 0x1AAu && (v & 0x40u) == 0u)
        s->irq_flag = 0;                    /* escribir SPUCNT sin el bit 6 reconoce */
    s->reg[off / 2u] = v;
}

/* Canal 4 de DMA: mueve palabras de 32 bits entre la RAM principal (en MADR) y la RAM
 * de sonido (en transfer_addr), y al terminar levanta la interrupcion de fin, que es
 * lo que el juego esta esperando. */
static void dma4_run(SpuStub *s, Cpu *c)
{
    uint32_t madr = s->dma4[0], bcr = s->dma4[1], chcr = s->dma4[2];
    uint32_t mode = (chcr >> 9) & 3u;
    uint32_t words;
    uint32_t src, i, bytes, start;
    int to_spu = (chcr & 1u) != 0u;      /* 1: RAM principal -> RAM de sonido */

    if (mode == 0u) {
        words = bcr & 0xFFFFu;
        if (words == 0u)
            words = 0x10000u;
    } else {
        words = (bcr & 0xFFFFu) * (bcr >> 16);
    }
    bytes = words * 4u;
    src = madr & 0x1FFFFCu;
    start = s->transfer_addr;             /* antes de avanzarla, para el check_irq */

    for (i = 0; i < bytes; i++) {
        uint32_t a = s->transfer_addr & (SPU_RAM_SIZE - 1u);
        if (to_spu)
            s->ram[a] = (uint8_t)ps1_r8(c, 0x80000000u | (src & 0x1FFFFFu));
        else
            ps1_w8(c, 0x80000000u | (src & 0x1FFFFFu), s->ram[a]);
        src++;
        s->transfer_addr = (s->transfer_addr + 1u) & (SPU_RAM_SIZE - 1u);
    }

    if (mode == 1u) {
        s->dma4[0] = src;
        s->dma4[1] &= 0xFFFFu;
    }
    if (s->log)
        fprintf(stderr, "dma4: spu[%#x] %u bytes  SPUCNT=%#x (irq %s)  dir_irq=%#x\n",
                (unsigned)start, (unsigned)bytes, (unsigned)s->reg[0x1AAu / 2u],
                (s->reg[0x1AAu / 2u] & 0x40u) ? "ON" : "off",
                (unsigned)((uint32_t)s->reg[0x1A4u / 2u] << 3));
    check_irq(s, start, bytes);          /* la IRQ puede caer dentro de lo transferido */
    s->dma4[2] &= ~0x01000000u;          /* la transferencia ya no esta en curso */
    if (s->dicr & (1u << 20))
        s->dicr |= 1u << 28;
    s->irqs |= 1u << 20;                 /* bit 16 + canal 4 */
    s->dma_transfers++;
    s->dma_bytes += bytes;
    s->irqs_raised++;
}

static uint32_t bus_read(Cpu *c, void *self, uint32_t a, uint32_t n)
{
    SpuStub *s = (SpuStub *)self;
    uint32_t word;
    (void)c;
    s->reads++;

    if (a >= SPU_BASE && a < SPU_BASE + SPU_REG_BYTES) {
        uint32_t off = a - SPU_BASE;
        if (n == 4u)
            return (uint32_t)read_reg(s, off) | ((uint32_t)read_reg(s, off + 2u) << 16);
        if (n == 2u)
            return read_reg(s, off);
        return ((uint32_t)read_reg(s, off) >> ((a & 1u) * 8u)) & 0xFFu;
    }

    switch (a & ~3u) {
    case DMA4_MADR: word = s->dma4[0]; break;
    case DMA4_BCR:  word = s->dma4[1]; break;
    case DMA4_CHCR: word = s->dma4[2]; break;
    case DMA_DPCR:  word = s->dpcr; break;
    case DMA_DICR:  word = s->dicr; break;
    default:        word = 0; break;
    }
    return word >> ((a & 3u) * 8u);
}

static void bus_write(Cpu *c, void *self, uint32_t a, uint32_t v, uint32_t n)
{
    SpuStub *s = (SpuStub *)self;
    s->writes++;

    switch (a) {
    case DMA4_MADR:
        s->dma4[0] = v & 0xFFFFFFu;
        return;
    case DMA4_BCR:
        s->dma4[1] = v;
        return;
    case DMA4_CHCR:
        s->dma4[2] = v;
        if (v & 0x01000000u)             /* bit 24: arranca la transferencia */
            dma4_run(s, c);
        return;
    case DMA_DPCR:
        s->dpcr = v;
        return;
    case DMA_DICR: {
        /* las banderas (bits 24..30) se reconocen escribiendo 1 */
        uint32_t flags = s->dicr & 0x7F000000u & ~(v & 0x7F000000u);
        s->dicr = (v & 0x00FF803Fu) | flags;
        return;
    }
    default:
        break;
    }

    if (a >= SPU_BASE && a < SPU_BASE + SPU_REG_BYTES) {
        uint32_t off = a - SPU_BASE;
        if (n == 4u) {
            write_reg(s, off, (uint16_t)v);
            write_reg(s, off + 2u, (uint16_t)(v >> 16));
        } else if (n == 2u) {
            write_reg(s, off, (uint16_t)v);
        } else {
            uint32_t cur = read_reg(s, off);
            uint32_t sh = (a & 1u) * 8u;
            write_reg(s, off, (uint16_t)((cur & ~(0xFFu << sh)) | ((v & 0xFFu) << sh)));
        }
    }
}

static uint32_t bus_take_irqs(Cpu *c, void *self)
{
    SpuStub *s = (SpuStub *)self;
    uint32_t irqs = s->irqs;
    (void)c;
    s->irqs = 0;
    return irqs;
}

void spu_init(SpuStub *s)
{
    memset(s, 0, sizeof(*s));
    s->iface.read = bus_read;
    s->iface.write = bus_write;
    s->iface.take_irqs = bus_take_irqs;
    s->iface.self = s;
}

void spu_attach(SpuStub *s, Cpu *c)
{
    c->bus = &s->iface;
}
