/* Los videos del front end (VIDEO_*): el "video wall". M3.
 *
 * Traduccion de src/ps1/video.rs y de su llamada en src/ps1/host/mod.rs. El juego abre un
 * stream (VIDEO_create), le da un archivo (VIDEO_spoolfile), lo arranca y, cada frame, pide
 * con VIDEO_updateframexy que el frame que toca se escriba en la VRAM en (x, y); de ahi lo
 * dibuja como textura. Los clips son .DCT ("mTCD": por frame, una cabecera de 16 bytes con
 * el tamano, el ancho y el alto, y el bitstream del MDEC), a 15 fps y sin sonido.
 *
 * El handle es un bloque del heap del juego (el VIDEOSTRUCT, "VIDW"), que se pide con una
 * llamada anidada a RESERVE_MEM como la referencia: sus llamantes lo liberan despues de
 * VIDEO_destroy, y la asignacion se ve en la RAM, asi que tiene que coincidir.
 *
 * Diferencia con la referencia, a proposito: no se carga el archivo entero (los hay de 3,7
 * MB). Se lee frame a frame del disco, saltando cabeceras cuando el reloj va por delante.
 */
#include "host.h"
#include "mdec.h"

#include <stdlib.h>
#include <string.h>

#define VIDEO_CREATE        0x800511B0u
#define VIDEO_DESTROY       0x80051328u
#define VIDEO_SPOOLFILE     0x80051380u
#define VIDEO_STARTPLAYBACK 0x800513DCu
#define VIDEO_ABORTPLAYBACK 0x8005143Cu
#define VIDEO_STATE         0x80051480u
#define VIDEO_UPDATEFRAMEXY 0x80051528u

#define RESERVE_MEM         0x800E5F28u
#define VIDEO_BLOCK_NAME    0x80056F08u
#define VIDW                0x57444956u

#define STREAMS   4
/* Se lee por bloques: un frame son 1,6-5 KB, asi que 64 KB cubren 15-40 frames. Leer cada
 * frame por separado (fseek + 2 fread) costaba la mitad de los fps del menu en Flycast. */
#define BLOQUE    (64u * 1024u)
#define PIX_MAX   (320u * 256u)

typedef struct {
    uint32_t handle;                        /* 0: libre */
    char     file[32];
    FILE    *f;
    uint32_t size;
    uint32_t pos, next;                     /* donde empieza el frame `next` */
    uint8_t *buf;                           /* BLOQUE bytes del archivo desde buf_off */
    uint32_t buf_off, buf_n;
    uint32_t state;                         /* VIDEO_state: 0 parado o terminado, 3 en marcha */
    uint64_t start;                         /* reloj (1/1920 s) al arrancar */
    float    fps;
    int64_t  shown;                         /* ultimo frame escrito */
} Stream;

static Stream   s_st[STREAMS];
static uint16_t *s_pix;

static Stream *busca(uint32_t handle)
{
    for (int i = 0; i < STREAMS; i++)
        if (handle != 0u && s_st[i].handle == handle)
            return &s_st[i];
    return NULL;
}

static void cierra(Stream *s)
{
    if (s->f != NULL)
        fclose(s->f);
    s->f = NULL;
    s->buf_n = 0;
}

/* `len` bytes del archivo desde `off`, del bloque leido (y si no estan, se lee otro que
 * empiece ahi). NULL si no se puede. */
static const uint8_t *trae(Stream *s, uint32_t off, uint32_t len)
{
    if (len > BLOQUE || off + len > s->size)
        return NULL;
    if (off < s->buf_off || off + len > s->buf_off + s->buf_n) {
        uint32_t n = s->size - off < BLOQUE ? s->size - off : BLOQUE;

        if (s->buf == NULL && (s->buf = malloc(BLOQUE)) == NULL)
            return NULL;
        s->buf_n = 0;
        if (fseek(s->f, (long)off, SEEK_SET) != 0 || fread(s->buf, 1, n, s->f) != n)
            return NULL;
        s->buf_off = off;
        s->buf_n = n;
    }
    return s->buf + (off - s->buf_off);
}

/* La cabecera del frame en s->pos: su tamano total (0 si no hay mas) y sus dimensiones. */
static uint32_t cabecera(Stream *s, int *w, int *hh)
{
    const uint8_t *c = trae(s, s->pos, 16u);

    if (c == NULL || memcmp(c, "mTCD", 4) != 0)
        return 0;
    uint32_t n = c[4] | (c[5] << 8) | ((uint32_t)c[6] << 16) | ((uint32_t)c[7] << 24);
    if (n < 16u || s->pos + n > s->size)
        return 0;
    *w = c[12] | (c[13] << 8);
    *hh = c[14] | (c[15] << 8);
    return n;
}

/* VIDEO_updateframexy: el frame que toca, a la VRAM en (x, y). */
static uint32_t actualiza(GameHost *h, Stream *s, uint32_t x, uint32_t y)
{
    int w = 0, hh = 0;
    uint32_t n;
    int64_t due;
    const uint8_t *bs;

    if (s->state != 3u || s->f == NULL)
        return 0;
    due = (int64_t)((float)(h->clock - s->start) / 1920.0f * s->fps);
    if (due <= s->shown)
        return 0;
    /* hasta el frame que toca; si el archivo se acaba antes, el video termino */
    for (;;) {
        n = cabecera(s, &w, &hh);
        if (n == 0u) {
            s->state = 0;
            return 0;
        }
        if ((int64_t)s->next == due)
            break;
        s->pos += n;
        s->next++;
    }
    s->shown = due;
    if ((uint32_t)(w * hh) > PIX_MAX || (bs = trae(s, s->pos + 16u, n - 16u)) == NULL) {
        s->state = 0;
        return 0;
    }
    s->pos += n;
    s->next++;
    mdec_frame_ps1(bs, n - 16u, w, hh, s_pix);
    for (int j = 0; j < hh; j++)
        for (int i = 0; i < w; i++) {
            uint32_t vx = x + (uint32_t)i, vy = y + (uint32_t)j;
            if (vx < VRAM_W && vy < VRAM_H)
                h->vram[vy * VRAM_W + vx] = s_pix[j * w + i];
        }
    h->vram_writes++;
    h->video_frames++;
    tex_dirty(&h->texcache, x, y, (uint32_t)w, (uint32_t)hh);
    return 1;
}

int host_video_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    uint32_t a0 = c->r[4], a1 = c->r[5], a2 = c->r[6];
    Stream *s;

    switch (addr) {
    case VIDEO_CREATE:
    case VIDEO_DESTROY:
    case VIDEO_SPOOLFILE:
    case VIDEO_STARTPLAYBACK:
    case VIDEO_ABORTPLAYBACK:
    case VIDEO_STATE:
    case VIDEO_UPDATEFRAMEXY:
        break;
    default:
        return 0;
    }
    h->video_calls++;
    h->last_video_call = addr;
    if (s_pix == NULL)
        s_pix = malloc(PIX_MAX * sizeof *s_pix);
    s = busca(a0);
    switch (addr) {
    case VIDEO_CREATE: {
        /* un bloque del heap para el handle, como el VIDEOSTRUCT ("VIDW") de VIDEO_create */
        uint32_t args[3] = { VIDEO_BLOCK_NAME, 64u, 0u };
        uint32_t block = ps1_call_nested(c, RESERVE_MEM, args, 3u);

        if (block != 0u) {
            ps1_w32(c, block, VIDW);
            for (int i = 0; i < STREAMS; i++)
                if (s_st[i].handle == 0u) {
                    memset(&s_st[i], 0, sizeof s_st[i]);
                    s_st[i].handle = block;
                    s_st[i].fps = 15.0f;
                    s_st[i].shown = -1;
                    break;
                }
        }
        *ret = block;
        return 1;
    }
    case VIDEO_DESTROY:
        if (s != NULL) {
            cierra(s);
            free(s->buf);
            s->buf = NULL;
            s->handle = 0;
        }
        *ret = 0;
        return 1;
    case VIDEO_SPOOLFILE:
        /* VIDEO_spoolfile(handle, archivo) */
        if (s != NULL) {
            unsigned k = 0;
            while (k + 1u < sizeof s->file && ps1_r8(c, a1 + k) != 0u) {
                s->file[k] = (char)ps1_r8(c, a1 + k);
                k++;
            }
            s->file[k] = '\0';
        }
        *ret = 1;
        return 1;
    case VIDEO_STARTPLAYBACK:
        if (s == NULL) {
            *ret = 0;
            return 1;
        }
        if (s->file[0] != '\0') {
            cierra(s);
            if (h->files.log)
                fprintf(stderr, "video %s (frame %llu)\n", s->file, (unsigned long long)h->frames);
            if (host_disk_open(h, s->file, &s->f, &s->size)) {
                s->pos = 0;
                s->next = 0;
                s->fps = 15.0f;
                s->state = 3;
                s->start = h->clock;
                s->shown = -1;
            }
        }
        *ret = 1;
        return 1;
    case VIDEO_ABORTPLAYBACK:
        if (s != NULL)
            s->state = 0;
        *ret = 0;
        return 1;
    case VIDEO_STATE:
        *ret = s != NULL ? s->state : 0u;
        return 1;
    case VIDEO_UPDATEFRAMEXY: {
        uint64_t t0 = h->now_us ? h->now_us() : 0u;

        *ret = s != NULL ? actualiza(h, s, a1, a2) : 0u;
        if (h->now_us)
            h->video_us += h->now_us() - t0;
        return 1;
    }
    }
    return 0;
}
