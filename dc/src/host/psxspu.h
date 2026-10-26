/* El SPU de PS1 completo (M4): traduccion de src/ps1/spu.rs.
 *
 * 512 KB de RAM de sonido que se llenan por DMA, 24 voces ADPCM con envolvente ADSR, ruido,
 * modulacion de tono, la unidad de reverb y la IRQ por direccion de RAM que usan los
 * streams del driver de EA. Corre a 44,1 kHz del reloj virtual; la mezcla sale en estereo
 * intercalado. El driver de sonido del juego LEE el estado de las voces (nivel de
 * envolvente, ENDX) y decide segun eso, asi que el modelo tiene que ser el fiel, no un
 * stub: es la razon por la que el test diferencial lo neutralizaba hasta ahora.
 *
 * Misma aritmetica que la referencia (enteros, con el desbordamiento que da -fwrapv), para
 * poder compararlos. PORTABLE: sin cabeceras de KOS.
 */
#ifndef NFSHS_PSXSPU_H
#define NFSHS_PSXSPU_H

#include <stdint.h>

#define PSXSPU_RAM   0x80000u
#define PSXSPU_RATE  44100u

typedef struct {
    int32_t counter;
} SpuEnv;

typedef struct {
    uint32_t addr, repeat;
    uint8_t  ignore_loop;
    uint32_t counter;                       /* fraccion de 12 bits; indice de muestra encima */
    int16_t  buf[31];                       /* las 28 del bloque tras las 3 ultimas del anterior */
    uint8_t  block_flags;
    uint8_t  has_block;
    int32_t  s1, s2;
    uint8_t  phase;                         /* SPU_OFF.. SPU_RELEASE */
    int32_t  level;
    SpuEnv   env;
    int32_t  last_out;
    int32_t  vol[2];
    SpuEnv   vol_env[2];
} SpuVoice;

typedef struct {
    uint8_t  ram[PSXSPU_RAM];
    uint16_t regs[0x200];
    SpuVoice voices[24];
    uint32_t transfer_addr;
    uint8_t  irq_flag;
    uint32_t endx;
    int32_t  noise_timer;
    uint16_t noise_level;
    uint32_t reverb_addr, reverb_pos;
    int32_t  reverb_in[2], reverb_out[2];
    uint8_t  odd;
    int32_t  main_vol[2];
    SpuEnv   main_env[2];
    uint32_t dma4[3];                       /* MADR, BCR, CHCR */
    uint32_t dpcr, dicr;
    uint32_t irqs;
    uint32_t keyons;
    /* MEDIDA (M4): solo el estado que lee el juego (posiciones, envolventes, ENDX, IRQ),
     * sin interpolar, mezclar ni reverb; la salida queda en silencio. */
    uint8_t  solo_estado;
} PsxSpu;

void     psxspu_reset(PsxSpu *s);
/* Lo que lee y escribe la CPU en 0x1F801C00.. y en los registros del DMA 4. `ram` es la
 * RAM principal de PS1 (para el DMA). */
uint32_t psxspu_read(PsxSpu *s, uint32_t a, unsigned n);
void     psxspu_write(PsxSpu *s, uint32_t a, uint32_t v, unsigned n, uint8_t *ram, uint32_t ram_size);
uint32_t psxspu_take_irqs(PsxSpu *s);
/* `n` muestras estereo a `out` (2n int16, izquierda y derecha intercaladas). */
void     psxspu_run(PsxSpu *s, int16_t *out, unsigned n);

#endif
