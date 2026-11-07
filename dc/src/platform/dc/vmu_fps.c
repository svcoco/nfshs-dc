/* M7: contador de fps en la pantalla LCD de la VMU.
 *
 * En una Dreamcast real no hay ventana de Flycast con los contadores, y los mensajes por el
 * puerto serie se apagan (cuestan rendimiento), asi que los fps se leen en la VMU: un hilo
 * propio mira host.frames cada 500 ms y dibuja "FPS" y la cifra con una decimal. Cambia de
 * pagina cada 2 s: fps; L, D y E; J, C y A; I, O y F (ver hilo()).
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

#include <dc/perfctr.h>

#include "aica_spu.h"
#include "render.h"
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

static const uint8_t letra_l[5] = {4, 4, 4, 4, 7};
static const uint8_t letra_d[5] = {6, 5, 5, 5, 6};
static const uint8_t letra_e[5] = {7, 4, 6, 4, 7};
static const uint8_t letra_j[5] = {1, 1, 1, 5, 7};
static const uint8_t letra_c[5] = {7, 4, 4, 4, 7};
static const uint8_t letra_a[5] = {7, 5, 7, 5, 5};
static const uint8_t letra_i[5] = {7, 2, 2, 2, 7};
static const uint8_t letra_o[5] = {7, 5, 5, 5, 7};

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

/* Una pagina de medicion: tres filas con una letra y un entero de hasta tres cifras (M8). */
static void dibuja_filas(uint8_t *bm, const uint8_t *const letra[3], const unsigned v[3])
{
    unsigned f;

    memset(bm, 0, ANCHO_B * ALTO);
    for (f = 0; f < 3u; f++) {
        unsigned y = f * 11u, n = v[f] > 999u ? 999u : v[f];

        glifo(bm, letra[f], 2u, y, 2u);
        if (n >= 100u)
            glifo(bm, cifra[n / 100u], 14u, y, 2u);
        if (n >= 10u)
            glifo(bm, cifra[(n / 10u) % 10u], 22u, y, 2u);
        glifo(bm, cifra[n % 10u], 30u, y, 2u);
    }
}

static void pinta(const uint8_t *bm)
{
    maple_device_t *dev;
    int i = 0;

    while ((dev = maple_enum_type(i++, MAPLE_FUNC_LCD)) != NULL)
        vmu_draw_lcd_rotated(dev, bm);   /* un fallo (bus ocupado) se reintenta en 500 ms */
}

/* Lo que se acumula y se reparte por frame. */
typedef struct Foto {
    uint64_t t_ms, frames, fields, dibujo, espera, archivos, audio, reloj;
} Foto;

static void foto(Foto *o)
{
    o->t_ms = timer_ms_gettime64();
    o->frames = s_host->frames;
    o->fields = s_host->fields;
    o->dibujo = s_host->tex_us + s_host->draw_us;
    o->espera = render_espera_us();
    o->archivos = s_host->file_us;
    o->audio = aica_spu_us();
    o->reloj = s_host->wait_us;
}

/* Los contadores del SH-4: PRFC0 es del reloj de KOS, asi que solo se usa PRFC1 y mide por
 * turnos, 4 s cada cosa: ciclos parados por fallos de la cache de instrucciones y por fallos
 * de la de datos. */
static void mide(perf_cntr_event_t ev)
{
#ifdef NFSHS_PERFIL
    (void)ev;                  /* PRFC1 es del muestreador; la pagina de I y O no vale aqui */
    return;
#endif
    perf_cntr_stop(PRFC1);
    perf_cntr_clear(PRFC1);
    perf_cntr_start(PRFC1, ev, PMCR_COUNT_CPU_CYCLES);
}

static unsigned ms_por_frame(uint64_t us, uint64_t frames)
{
    return (unsigned)((us + frames * 500u) / (frames * 1000u));
}

static void *hilo(void *arg)
{
    static const uint8_t *const FILAS_B[3] = { letra_l, letra_d, letra_e };
    static const uint8_t *const FILAS_C[3] = { letra_j, letra_c, letra_a };
    static const uint8_t *const FILAS_D[3] = { letra_i, letra_o, letra_f };
    uint8_t bm[ANCHO_B * ALTO];
    Foto a, b;
    int v0 = pvr_get_vbl_count();
    unsigned ciclo = 0, pct_i = 0, pct_o = 0;
    uint64_t t_cache = timer_ms_gettime64();

    (void)arg;
    foto(&a);
    mide(PMCR_PIPELINE_FREEZE_BY_ICACHE_MISS_MODE);
    while (s_corriendo) {
        unsigned pagina;
        int v1;

        thd_sleep(500);
        foto(&b);
        v1 = pvr_get_vbl_count();
        ciclo = (ciclo + 1u) % 16u;
        pagina = ciclo / 4u;

        if (ciclo % 8u == 0u && b.t_ms > t_cache) {
            /* 200 M de ciclos por segundo: 200 000 por ms */
            unsigned pct = (unsigned)(perf_cntr_count(PRFC1) * 100u /
                                      ((b.t_ms - t_cache) * 200000u));

            if (ciclo == 8u) {
                pct_i = pct;
                mide(PMCR_PIPELINE_FREEZE_BY_DCACHE_MISS_MODE);
            } else {
                pct_o = pct;
                mide(PMCR_PIPELINE_FREEZE_BY_ICACHE_MISS_MODE);
            }
            t_cache = b.t_ms;
        }

        /* Con un frame de mas de 500 ms no hay nada que repartir: sigue acumulando. */
        if (b.frames == a.frames)
            continue;
        if (pagina == 0u) {
            /* Los fps se miden contra los vblank del PVR, que no dependen del reloj del hilo. */
            if (v1 > v0)
                dibuja(bm, (unsigned)((b.frames - a.frames) * 600u / (unsigned)(v1 - v0)));
            else
                dibuja(bm, (unsigned)((b.frames - a.frames) * 10000u / (b.t_ms - a.t_ms)));
        } else {
            uint64_t df = b.frames - a.frames;
            unsigned periodo = (unsigned)((b.t_ms - a.t_ms) / df);
            unsigned d = ms_por_frame(b.dibujo - a.dibujo, df);
            unsigned e = ms_por_frame(b.espera - a.espera, df);
            unsigned cd = ms_por_frame(b.archivos - a.archivos, df);
            unsigned au = ms_por_frame(b.audio - a.audio, df);
            unsigned w = ms_por_frame(b.reloj - a.reloj, df);
            unsigned l = periodo > d ? periodo - d : 0u;   /* d ya incluye e */
            unsigned v[3];

            if (pagina == 1u) {
                /* L = logica, D = dibujo en la CPU, E = espera al PVR */
                v[0] = l; v[1] = d > e ? d - e : 0u; v[2] = e;
                dibuja_filas(bm, FILAS_B, v);
            } else if (pagina == 2u) {
                /* L repartida: J = juego e interrupciones, C = archivos y CD, A = audio */
                v[0] = l > cd + au + w ? l - cd - au - w : 0u; v[1] = cd; v[2] = au;
                dibuja_filas(bm, FILAS_C, v);
            } else {
                /* I y O = % del tiempo parado por la cache, F = campos por frame x 10 */
                v[0] = pct_i; v[1] = pct_o;
                v[2] = (unsigned)((b.fields - a.fields) * 10u / df);
                dibuja_filas(bm, FILAS_D, v);
            }
        }
        pinta(bm);
        a = b;
        v0 = v1;
    }
    return NULL;
}

void vmu_fps_init(const GameHost *h)
{
    s_host = h;
    thd_create(1, hilo, NULL);
}
