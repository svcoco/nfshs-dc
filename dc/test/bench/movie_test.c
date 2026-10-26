/* M3: reproduce una pelicula sin arrancar el juego, para probar el reproductor deprisa.
 *
 * Primero un tono de 2 s por snd_stream y el reloj del ARM del AICA (si no avanza, el
 * driver no corre: ver src/platform/dc/aica.c); despues MOVIES/EADOLBY.XA (8 s) y la intro,
 * en bucle. La salida va por el puerto serie. Se mete en un CDI en lugar del juego:
 *
 *   kos-cc -O2 -Isrc/host -Isrc/ps1rt -Isrc/render -Isrc/platform/dc -Igen
 *       test/bench/movie_test.c src/platform/dc/movie.c src/platform/dc/aica.c
 *       src/host/mdec.c build/dc/aica/aica_drv.c -o build/dc/bench/movie_test.elf -lm
 *   sh tools/mkdisc/mkdisc.sh --elf dc/build/dc/bench/movie_test.elf ...   (desde la raiz)
 */
#include <kos.h>

#include "host.h"
#include "movie.h"
#include "aica.h"

#include <dc/sound/stream.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static GameHost host;

uint16_t dc_read_pad(void)
{
    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *st = dev ? (cont_state_t *)maple_dev_status(dev) : NULL;

    return st && (st->buttons & CONT_START) ? (uint16_t)(1u << 3) : 0u;
}

/* las mismas listas que render_init (platform/dc/render.c), sin arrastrar el renderer */
static int pvr_listo(void)
{
    pvr_init_params_t p = {
        .opb_sizes = { PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16 },
        .vertex_buf_size = 512 * 1024,
        .dma_enabled = 0,
        .fsaa_enabled = 0,
        .autosort_disabled = 1,
        .opb_overflow_count = 0
    };

    return pvr_init(&p) >= 0;
}

/* DIAG: un tono por snd_stream, sin nada mas, para ver si el AICA avanza */
static int16_t s_tono[8192] __attribute__((aligned(32)));
static uint32_t s_tono_llamadas;

static void *tono_cb(snd_stream_hnd_t hnd, int bytes, int *got)
{
    (void)hnd;
    s_tono_llamadas++;
    *got = bytes > (int)sizeof s_tono ? (int)sizeof s_tono : bytes;
    return s_tono;
}

static void tono(void)
{
    snd_stream_hnd_t h;
    uint64_t t;
    int i;

    for (i = 0; i < 8192; i += 2)
        s_tono[i] = s_tono[i + 1] = (int16_t)((i / 2) % 86 < 43 ? 8000 : -8000);
    printf("tono: sonido %s\n", aica_listo() ? "listo" : "sin sonido");
    {
        uint32_t c0 = g2_read_32(0xA0800000u + 0x21000u);

        thd_sleep(500);
        printf("tono: sys_mode %d  reg2800 0x%08lx  reloj ARM %lu -> %lu en 500 ms\n",
               hardware_sys_mode(NULL), (unsigned long)g2_read_32(0xA0702800u),
               (unsigned long)c0, (unsigned long)g2_read_32(0xA0800000u + 0x21000u));
    }
    h = snd_stream_alloc(tono_cb, SND_STREAM_BUFFER_MAX);
    snd_stream_start(h, 44100, 1);
    t = timer_ms_gettime64();
    while (timer_ms_gettime64() - t < 2000) {
        snd_stream_poll(h);
        thd_sleep(10);
    }
    snd_stream_stop(h);
    snd_stream_destroy(h);
    printf("tono: %u llamadas en 2 s\n", (unsigned)s_tono_llamadas);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    tono();
    host.gamedata = "/cd";
    if (!pvr_listo()) {
        printf("pvr_init fallo\n");
        return 1;
    }
    for (;;) {
        movie_play(&host, "EADOLBY.XA");
        movie_play(&host, "NFS4TITL.XA");
    }
    return 0;
}
