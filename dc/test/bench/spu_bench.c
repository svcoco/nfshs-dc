/* M4: cuanto cuesta en SH-4 el SPU por software, con el sonido de una carrera real.
 *
 * Reproduce un rastro reducido (dc/test/bench/mkspu.py) con el SPU en C, mide el tiempo de
 * la mezcla por segundo de audio y comprueba con una suma de verificacion que las muestras
 * salen identicas a las de la referencia. El rastro va dentro del ELF (build/, no se
 * versiona: lleva muestras del disco).
 */
#include <kos.h>

#include "psxspu.h"
#include "spu_trace.h"                      /* spu_trace[], spu_trace_size */

KOS_INIT_FLAGS(INIT_DEFAULT);

#define MAIN_RAM (2u * 1024u * 1024u)

static PsxSpu spu;
static uint8_t ram[MAIN_RAM];
static int16_t buf[2 * 4096];

static uint32_t u32le(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(int argc, char **argv)
{
    const uint8_t *p = spu_trace, *end = spu_trace + spu_trace_size;
    uint64_t t_mezcla = 0, muestras = 0;
    uint32_t suma = 0, want_n = 0, want_sum = 0;

    (void)argc;
    (void)argv;
    dbgio_dev_select("fb");
    dbgio_enable();
    printf("\nM4: SPU por software en SH-4 (rastro de una carrera)\n\n");
    psxspu_reset(&spu);
#ifdef SOLO_ESTADO
    spu.solo_estado = 1;
    printf("(solo el estado, sin sintetizar: la suma no coincidira)\n");
#endif
    while (p < end) {
        char c = (char)*p++;
        if (c == 'W') {
            psxspu_write(&spu, u32le(p), u32le(p + 4), p[8], ram, MAIN_RAM);
            p += 9;
        } else if (c == 'D') {
            uint32_t src = u32le(p), len = u32le(p + 4);
            p += 8;
            for (uint32_t k = 0; k < len; k++)
                ram[(src + k) & (MAIN_RAM - 1u)] = p[k];
            p += len;
        } else if (c == 'r') {
            uint32_t n = u32le(p);
            p += 4;
            while (n > 0u) {
                uint32_t m = n > 4096u ? 4096u : n;
                uint64_t t = timer_us_gettime64();
                psxspu_run(&spu, buf, m);
                t_mezcla += timer_us_gettime64() - t;
                for (uint32_t k = 0; k < 2u * m; k++)
                    suma = suma * 31u + (uint16_t)buf[k];
                muestras += m;
                n -= m;
            }
        } else if (c == 'E') {
            want_n = u32le(p);
            want_sum = u32le(p + 4);
            break;
        } else {
            printf("registro desconocido %02x\n", (unsigned)(uint8_t)c);
            break;
        }
    }
    printf("%lu muestras estereo (%lu.%01lu s de audio)\n", (unsigned long)muestras,
           (unsigned long)(muestras / 44100u), (unsigned long)(muestras % 44100u * 10u / 44100u));
    printf("mezcla: %lu ms en total, %lu ms por segundo de audio (%lu %% de la CPU)\n",
           (unsigned long)(t_mezcla / 1000u),
           (unsigned long)(muestras ? t_mezcla * 44100u / muestras / 1000u : 0u),
           (unsigned long)(muestras ? t_mezcla * 44100u / muestras / 10000u : 0u));
    printf("suma %08lx, referencia %08lx (%lu muestras): %s\n", (unsigned long)suma,
           (unsigned long)want_sum, (unsigned long)want_n,
           (suma == want_sum && muestras == want_n) ? "IDENTICAS" : "DISTINTAS");
    printf("\n[fin]\n");
    for (;;)
        thd_sleep(100);
    return 0;
}
