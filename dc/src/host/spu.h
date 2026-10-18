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

#define SPU_BASE      0x1F801C00u
#define SPU_REG_BYTES 0x400u
#define SPU_RAM_SIZE  0x80000u      /* 512 KB de RAM de sonido */

#define DMA4_MADR     0x1F8010C0u
#define DMA4_BCR      0x1F8010C4u
#define DMA4_CHCR     0x1F8010C8u
#define DMA_DPCR      0x1F8010F0u
#define DMA_DICR      0x1F8010F4u

typedef struct {
    Ps1IoBus iface;                        /* lo que ve ps1rt */
    uint16_t reg[SPU_REG_BYTES / 2u];
    uint8_t  ram[SPU_RAM_SIZE];
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

void spu_init(SpuStub *s);
void spu_attach(SpuStub *s, Cpu *c);

#endif /* PS1_HOST_SPU_H */
