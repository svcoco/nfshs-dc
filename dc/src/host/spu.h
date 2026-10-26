/* Stub del SPU y su canal de DMA, como el hardware que programa el driver de sonido
 * original de EA.
 *
 * Esto NO sintetiza audio: la decision de CLAUDE.md seccion 3 es que en M1 y M2 el SPU
 * es un stub que acepta escrituras y mantiene el volumen de voz; la conversion a AICA
 * es M3. Lo que si modela, porque el arranque lo necesita, es el **canal 4 de DMA** y
 * su interrupcion de fin: sin ella el juego se queda esperando para siempre.
 *
 * Portado del modelo de src/ps1/spu.rs de la referencia, solo en la parte de registros
 * y DMA.
 */
#ifndef PS1_HOST_SPU_H
#define PS1_HOST_SPU_H

#include "ps1rt.h"

/* Prefijo ps1_ / PS1_ a proposito: KOS declara su propio `spu_init` y su propio
 * `SPU_RAM_SIZE` en dc/spu.h, que son el AICA del Dreamcast. Son cosas distintas con
 * el mismo nombre, y sin el prefijo el main de Dreamcast no compila. */
#define PS1_SPU_BASE      0x1F801C00u
#define PS1_SPU_REG_BYTES 0x400u
#define PS1_SPU_RAM_SIZE  0x80000u      /* 512 KB de RAM de sonido */

#define DMA4_MADR     0x1F8010C0u
#define DMA4_BCR      0x1F8010C4u
#define DMA4_CHCR     0x1F8010C8u
#define DMA_DPCR      0x1F8010F0u
#define DMA_DICR      0x1F8010F4u

/* M4: la salida del middleware de audio. El driver de EA pide al hardware por los
 * registros del SPU; el stub mantiene el estado logico (registros, RAM de sonido, DMA, IRQ)
 * y, si hay salida, le pasa cada peticion a la plataforma, que la hace sonar con el AICA
 * (dc/src/platform/dc/aica_spu.c). Sin salida (arnes de PC, test diferencial) el stub se
 * comporta igual que siempre. Decision de Jorge: docs/hitos/M4.md, secciones 5 y 7. */
typedef struct SpuSink {
    void *ctx;
    /* la RAM de sonido cambio en [addr, addr + len) (DMA o FIFO); `ram` es toda la RAM de
     * sonido (512 KB) y `addr + len` puede pasar del final (da la vuelta) */
    void     (*upload)(void *ctx, const uint8_t *ram, uint32_t addr, uint32_t len);
    /* escritura de un registro del SPU (desplazamiento desde 0x1F801C00), ya guardada en
     * `regs` */
    void     (*reg)(void *ctx, const uint16_t *regs, uint32_t off, uint16_t v);
    /* el driver lee el nivel de envolvente de la voz `v` (0: la voz termino) */
    uint16_t (*level)(void *ctx, unsigned v);
} SpuSink;

typedef struct {
    Ps1IoBus iface;                        /* lo que ve ps1rt */
    const SpuSink *sink;                   /* M4: el middleware de audio, o NULL */
    uint16_t reg[PS1_SPU_REG_BYTES / 2u];
    uint8_t  ram[PS1_SPU_RAM_SIZE];
    uint32_t transfer_addr;                /* donde escribe el DMA en la RAM de sonido */
    uint32_t dma4[3];                      /* MADR, BCR, CHCR */
    uint32_t dpcr, dicr;
    uint32_t irqs;                         /* pendientes para take_irqs */
    int      irq_flag;                     /* IRQ del SPU pendiente de reconocer */
    int      log;                          /* --spulog */

    /* medidas, para el informe */
    uint64_t reads, writes;
    uint64_t dma_transfers, dma_bytes;
    uint64_t irqs_raised;
} SpuStub;

void ps1_spu_init(SpuStub *s);
void ps1_spu_attach(SpuStub *s, Cpu *c);
/* DIAG (M4): las cadenas de llamadas que escribieron en el SPU (NFSHS_SNDCALLERS). */
void ps1_spu_callers_report(FILE *out, unsigned top);

#endif /* PS1_HOST_SPU_H */
