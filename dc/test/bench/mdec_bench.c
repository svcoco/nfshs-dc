/* M3: cuanto cuesta en SH-4 decodificar los videos del juego.
 *
 * Decodifica 30 frames reales de cada tipo (intro 256x240, video wall 160x128 y 80x80)
 * por las salidas que usara el port (macrobloques YUV420 para el PVR y 16 bits de PS1
 * para la VRAM), mas el audio XA, y saca los tiempos a pantalla. `make dc-bench-mdec`.
 */
#include <kos.h>

#include "mdec.h"

typedef struct { int w, h; uint32_t len; const uint8_t *bs; } BenchFrame;
#include "mdec_frames.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

static uint8_t mbs[16 * 15 * MDEC_YUV_MB] __attribute__((aligned(32)));
static uint16_t px[256 * 240] __attribute__((aligned(32)));

static void bench(const char *name, const BenchFrame *f, int n, int yuv)
{
    uint64_t best = ~0ull, total = 0;
    for (int rep = 0; rep < 3; rep++)
        for (int i = 0; i < n; i++) {
            uint64_t t = timer_us_gettime64();
            if (yuv)
                mdec_frame_yuv420(f[i].bs, f[i].len, f[i].w, f[i].h, mbs);
            else
                mdec_frame_ps1(f[i].bs, f[i].len, f[i].w, f[i].h, px);
            uint64_t d = timer_us_gettime64() - t;
            total += d;
            if (d < best)
                best = d;
        }
    printf("%-22s %3dx%-3d %s  media %5.2f ms  min %5.2f ms\n", name, f[0].w, f[0].h,
           yuv ? "YUV420" : "PS1   ", total / 1000.0 / (3 * n), best / 1000.0);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    dbgio_dev_select("fb");
    dbgio_enable();
    printf("\nM3: MDEC en SH-4 (30 frames x 3)\n\n");
    mdec_init();
    bench("intro NFS4TITL", titl, 30, 1);
    bench("intro NFS4TITL", titl, 30, 0);
    bench("wall ZZZZTR03", tr03, 30, 0);
    bench("wall ZZZZVD01", vd01, 30, 0);
    {
        static int16_t pcm[4096];
        int32_t prev[2][2] = {{0, 0}, {0, 0}};
        uint64_t t = timer_us_gettime64();
        int n = 0;
        for (int s = 0; s < TITL_AUDIO_SECTORS; s++)
            for (int g = 0; g < 18; g++)
                n += xa_sound_group(titl_audio + s * STR_SECTOR + 8 + g * 128, 1, prev, pcm);
        uint64_t d = timer_us_gettime64() - t;
        printf("audio XA %d sectores (%d muestras): %.3f ms por sector\n", TITL_AUDIO_SECTORS, n,
               d / 1000.0 / TITL_AUDIO_SECTORS);
    }
    printf("\n[fin]\n");
    for (;;)
        thd_sleep(100);
    return 0;
}
