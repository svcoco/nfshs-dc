/* M4: reproduce un rastro del SPU de la referencia (NFSHS_SPUTRACE) contra el SPU en C y
 * compara las muestras una a una.
 *
 *   spureplay RASTRO [salida.raw]
 *
 * El rastro trae las escrituras de la CPU, lo que lee cada DMA de la RAM principal y las
 * muestras que produjo cada run(). Si se da `salida.raw`, escribe ahi las muestras del
 * SPU en C (PCM estereo de 16 bits a 44,1 kHz, sin cabecera). Imprime tambien el tiempo
 * de mezcla por segundo de audio en esta maquina.
 */
#include "psxspu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAIN_RAM (2u * 1024u * 1024u)

static uint32_t u32le(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(int argc, char **argv)
{
    static PsxSpu spu;
    static uint8_t ram[MAIN_RAM];
    FILE *f, *out = NULL;
    int16_t *got = NULL;
    uint64_t muestras = 0, distintas = 0, escrituras = 0, dmas = 0;
    int64_t primera = -1;
    double segundos = 0.0;
    size_t cap = 0;
    int c;

    if (argc < 2) {
        fprintf(stderr, "uso: spureplay RASTRO [salida.raw]\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        fprintf(stderr, "%s: no se puede leer\n", argv[1]);
        return 1;
    }
    if (argc > 2)
        out = fopen(argv[2], "wb");
    psxspu_reset(&spu);
    while ((c = fgetc(f)) != EOF) {
        uint8_t h[9];

        if (c == 'W') {
            if (fread(h, 1, 9, f) != 9u)
                break;
            psxspu_write(&spu, u32le(h), u32le(h + 4), h[8], ram, MAIN_RAM);
            escrituras++;
        } else if (c == 'D') {
            uint32_t src, len;
            if (fread(h, 1, 8, f) != 8u)
                break;
            src = u32le(h);
            len = u32le(h + 4);
            for (uint32_t k = 0; k < len; k++) {
                int b = fgetc(f);
                ram[(src + k) & (MAIN_RAM - 1u)] = (uint8_t)b;
            }
            dmas++;
        } else if (c == 'R') {
            uint32_t n;
            clock_t t;
            if (fread(h, 1, 4, f) != 4u)
                break;
            n = u32le(h);
            if ((size_t)n * 2u > cap) {
                cap = (size_t)n * 2u;
                got = realloc(got, cap * sizeof *got);
            }
            t = clock();
            psxspu_run(&spu, got, n);
            segundos += (double)(clock() - t) / CLOCKS_PER_SEC;
            for (uint32_t k = 0; k < 2u * n; k++) {
                uint8_t e[2];
                int16_t want;
                if (fread(e, 1, 2, f) != 2u)
                    break;
                want = (int16_t)(e[0] | (e[1] << 8));
                if (want != got[k]) {
                    if (primera < 0)
                        primera = (int64_t)(muestras + k / 2u);
                    distintas++;
                }
            }
            if (out)
                fwrite(got, 2, 2u * n, out);
            muestras += n;
        } else {
            fprintf(stderr, "registro desconocido '%c' tras %llu muestras\n", c, (unsigned long long)muestras);
            return 1;
        }
    }
    fclose(f);
    if (out)
        fclose(out);
    printf("%llu escrituras, %llu DMA, %llu muestras estereo (%.1f s de audio)\n",
           (unsigned long long)escrituras, (unsigned long long)dmas, (unsigned long long)muestras,
           muestras / (double)PSXSPU_RATE);
    printf("mezcla: %.2f ms por segundo de audio\n",
           muestras ? 1000.0 * segundos / (muestras / (double)PSXSPU_RATE) : 0.0);
    if (distintas == 0) {
        printf("OK: las %llu muestras identicas a la referencia\n", (unsigned long long)muestras);
        return 0;
    }
    printf("FALLO: %llu valores distintos; la primera muestra distinta es la %lld (%.3f s)\n",
           (unsigned long long)distintas, (long long)primera, primera / (double)PSXSPU_RATE);
    return 1;
}
