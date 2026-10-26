/* M3: decodifica una pelicula entera con el MDEC en C y la escribe en RGBA crudo, para
 * compararla con `nfshs dump-movie FILE all OUT` (la referencia). Si se le da un cuarto
 * argumento, escribe ahi el audio (PCM de 16 bits, sin cabecera).
 *
 *   mdec_dump FILE.XA|FILE.DCT OUT.rgba [OUT.pcm]
 *
 * Imprime frames, tamano y el tiempo de decodificacion por frame en esta maquina.
 */
#include "mdec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t *load(const char *p, long *n)
{
    FILE *f = fopen(p, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)*n);
    if (fread(d, 1, (size_t)*n, f) != (size_t)*n) {
        fclose(f);
        free(d);
        return NULL;
    }
    fclose(f);
    return d;
}

static double secs;
static long frames;

static void decode(const uint8_t *bs, uint32_t len, int w, int h, FILE *out)
{
    static uint8_t rgba[640 * 480 * 4];
    clock_t t = clock();
    mdec_frame_rgba(bs, len, w, h, rgba);
    secs += (double)(clock() - t) / CLOCKS_PER_SEC;
    frames++;
    fwrite(rgba, 4, (size_t)w * h, out);
}

int main(int argc, char **argv)
{
    long n;
    if (argc < 3) {
        fprintf(stderr, "uso: mdec_dump FILE OUT.rgba [OUT.pcm]\n");
        return 2;
    }
    uint8_t *d = load(argv[1], &n);
    if (!d) {
        fprintf(stderr, "%s: no se puede leer\n", argv[1]);
        return 1;
    }
    FILE *out = fopen(argv[2], "wb");
    FILE *pcm = argc > 3 ? fopen(argv[3], "wb") : NULL;
    int w = 0, h = 0;
    mdec_init();
    if (n >= 4 && !memcmp(d, "mTCD", 4)) {
        long o = 0;
        while (o + 16 <= n && !memcmp(d + o, "mTCD", 4)) {
            uint32_t size = d[o + 4] | (d[o + 5] << 8) | (d[o + 6] << 16) | ((uint32_t)d[o + 7] << 24);
            if (size < 16 || o + (long)size > n)
                break;
            w = d[o + 12] | (d[o + 13] << 8);
            h = d[o + 14] | (d[o + 15] << 8);
            decode(d + o + 16, size - 16, w, h, out);
            o += size;
        }
    } else {
        static StrDemux s;
        static int16_t samples[4096];
        str_reset(&s);
        for (long o = 0; o + STR_SECTOR <= n; o += STR_SECTOR) {
            uint32_t len;
            int np, r = str_feed(&s, d + o, samples, &np, &len);
            if (pcm && np)
                fwrite(samples, 2, (size_t)np, pcm);
            if (r) {
                w = s.width;
                h = s.height;
                decode(s.data, len, w, h, out);
                if (r == 2)
                    o -= STR_SECTOR;        /* el sector abre el frame siguiente */
            }
        }
        /* el ultimo, si quedo a medias */
        uint32_t len = str_flush(&s);
        if (len)
            decode(s.data, len, w, h, out);
    }
    fclose(out);
    if (pcm)
        fclose(pcm);
    printf("%ld frames de %dx%d; %.3f ms por frame\n", frames, w, h, frames ? 1000.0 * secs / frames : 0.0);
    return 0;
}
