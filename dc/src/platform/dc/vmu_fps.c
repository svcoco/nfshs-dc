/* M7: contador de fps en la pantalla LCD de la VMU.
 *
 * En una Dreamcast real no hay ventana de Flycast con los contadores, y los mensajes por el
 * puerto serie se apagan (cuestan rendimiento), asi que los fps se leen en la VMU: un hilo
 * propio mira host.frames cada 500 ms y dibuja "FPS" y la cifra con una decimal.
 *
 * La pantalla es de 48x32 a 1 bit. Se dibuja en un bitmap natural (fila a fila, bit 7 =
 * pixel de la izquierda) y vmu_draw_lcd_rotated() lo gira 180 grados, que es como KOS
 * espera los bitmaps de pantalla (igual que su conversor de XBM). El borrado y el
 * dibujo toleran que el bus maple este ocupado (MAPLE_EAGAIN): el hilo de guardado de
 * vmu.c tiene el bus durante cada escritura y la cifra se pinta en el siguiente ciclo.
 */
#include <stdint.h>
#include <string.h>

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>

#include "vmu_fps.h"

#define ANCHO_B 6u            /* bytes por fila (48 px) */
#define ALTO    32u

/* Fuente 3x5 de cifras (3 bits por fila, bit 2 = izquierda). */
static const uint8_t cifra[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};
/* "FPS" en 3x5 */
static const uint8_t letra_f[5] = {7, 4, 6, 4, 4};
static const uint8_t letra_p[5] = {7, 5, 7, 4, 4};
static const uint8_t letra_s[5] = {7, 4, 7, 1, 7};

static const GameHost *s_host;
static volatile int s_corriendo = 1;
static volatile unsigned s_punto;       /* checkpoint alcanzado (ver vmu_fps.h) */

void vmu_fps_punto(unsigned n)
{
    s_punto = n;
}

static void punto(uint8_t *bm, unsigned x, unsigned y)
{
    if (x < 48u && y < ALTO)
        bm[y * ANCHO_B + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7u));
}

/* Glifo de 3x5 ampliado `k` veces con la esquina superior izquierda en (x, y). */
static void glifo(uint8_t *bm, const uint8_t *g, unsigned x, unsigned y, unsigned k)
{
    unsigned r, c, i, j;

    for (r = 0; r < 5u; r++)
        for (c = 0; c < 3u; c++)
            if (g[r] & (4u >> c))
                for (i = 0; i < k; i++)
                    for (j = 0; j < k; j++)
                        punto(bm, x + c * k + j, y + r * k + i);
}

/* `deci` = fps * 10 (se satura en 99.9). */
static void dibuja(uint8_t *bm, unsigned deci)
{
    unsigned x = 4u;
    unsigned d[3];

    memset(bm, 0, ANCHO_B * ALTO);
    if (deci > 999u)
        deci = 999u;
    d[0] = deci / 100u;
    d[1] = (deci / 10u) % 10u;
    d[2] = deci % 10u;

    glifo(bm, cifra[s_punto % 10u], 4u, 2u, 1u);
    glifo(bm, letra_f, 17u, 2u, 1u);
    glifo(bm, letra_p, 21u, 2u, 1u);
    glifo(bm, letra_s, 25u, 2u, 1u);

    /* cifras de 9x15 (k = 3): 2 + punto + 1 ocupan 3*9 + 2*3 + 4 = 37 columnas */
    if (d[0])
        glifo(bm, cifra[d[0]], x, 12u, 3u);
    x += 12u;
    glifo(bm, cifra[d[1]], x, 12u, 3u);
    x += 12u;
    punto(bm, x, 26u); punto(bm, x + 1u, 26u);
    punto(bm, x, 25u); punto(bm, x + 1u, 25u);
    x += 4u;
    glifo(bm, cifra[d[2]], x, 12u, 3u);
}

static void pinta(const uint8_t *bm)
{
    maple_device_t *dev;
    int i = 0;

    while ((dev = maple_enum_type(i++, MAPLE_FUNC_LCD)) != NULL)
        vmu_draw_lcd_rotated(dev, bm);   /* un fallo (bus ocupado) se reintenta en 500 ms */
}

static void *hilo(void *arg)
{
    uint8_t bm[ANCHO_B * ALTO];
    uint64_t t0 = timer_ms_gettime64();
    uint64_t f0 = s_host->frames;

    (void)arg;
    while (s_corriendo) {
        uint64_t t1, f1;

        thd_sleep(500);
        t1 = timer_ms_gettime64();
        f1 = s_host->frames;
        if (t1 > t0) {
            dibuja(bm, (unsigned)((f1 - f0) * 10000u / (t1 - t0)));
            pinta(bm);
        }
        t0 = t1;
        f0 = f1;
    }
    return NULL;
}

void vmu_fps_init(const GameHost *h)
{
    s_host = h;
    thd_create(1, hilo, NULL);
}
