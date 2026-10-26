/* Las peliculas a pantalla completa en Dreamcast (M3): Movie_Play.
 *
 * El archivo es MOVIES/<nombre> con los sectores enteros (dc/tools/bake/movies.py), en el
 * disco bajo /cd/BAKED/FMV (no MOVIES: mkdisc.sh excluye todo lo que se llame asi, porque
 * las copias de MOVIES/ de gamedata no tienen el audio). Se lee por delante del video lo
 * justo para que el audio no se quede sin muestras; los frames se decodifican con el MDEC en C (host/mdec.c) a
 * macrobloques YUV420 que el CONVERSOR YUV DEL PVR pasa a una textura YUV422, y esa
 * textura se dibuja a pantalla completa. El audio XA va al AICA por snd_stream.
 *
 * El ritmo lo marca el reloj: el frame k sale en t0 + k / fps, con fps = 150 sectores por
 * segundo x frames / sectores (como la referencia), y el audio arranca en t0. El
 * rendimiento no es criterio aqui (docs/hitos/M3.md, seccion 3): mientras dura la pelicula
 * no corre nada mas.
 *
 * Una diferencia con el original, a proposito: un boton que ya estaba pulsado al empezar no
 * corta la pelicula hasta soltarlo (si no, la A con la que se entra al juego se saltaria la
 * intro antes de verla).
 */
#include <kos.h>
#include <dc/sound/stream.h>
#include <dc/sq.h>

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "mdec.h"
#include "movie.h"

#define LEE        16u                  /* sectores por lectura del disco */
#define COLA       16u                  /* frames comprimidos por delante (~1 s) */
#define AUDIO_MAX  (2u * 37800u)        /* muestras estereo en el anillo (2 s) */
#define AUDIO_POR_DELANTE (37800u / 2u) /* lo que se intenta tener leido (0,5 s) */
#define TEX        256u                 /* textura de 256x256 */

typedef struct {
    uint32_t len;
    uint8_t  data[STR_MAX_CHUNKS * STR_CHUNK];
} Comprimido;

typedef struct {
    FILE      *f;
    int        eof;
    uint8_t   *lee;                     /* LEE sectores leidos */
    uint32_t   lee_n, lee_at;
    StrDemux   dmx;
    Comprimido *cola;
    uint32_t   cola_ini, cola_n;
    /* audio: anillo de muestras estereo intercaladas */
    int16_t   *anillo;
    uint32_t   a_lee, a_esc, a_n;       /* en pares (muestras estereo) */
    int16_t    pcm[4096];
} Peli;

static Peli *s_p;                       /* para el callback del audio */
static int   s_snd_listo;

/* ---- disco ---- */

static const uint8_t *sector(Peli *p)
{
    if (p->lee_at == p->lee_n) {
        if (p->eof)
            return NULL;
        p->lee_n = (uint32_t)fread(p->lee, STR_SECTOR, LEE, p->f);
        p->lee_at = 0;
        if (p->lee_n < LEE)
            p->eof = 1;
        if (p->lee_n == 0u)
            return NULL;
    }
    return p->lee + (size_t)STR_SECTOR * p->lee_at++;
}

static void audio_mete(Peli *p, const int16_t *s, int n, int stereo)
{
    for (int i = 0; i < n; i += stereo ? 2 : 1) {
        if (p->a_n >= AUDIO_MAX)
            return;                     /* lleno: no deberia pasar con 2 s */
        int16_t *d = &p->anillo[2u * p->a_esc];
        d[0] = s[i];
        d[1] = stereo ? s[i + 1] : s[i];
        p->a_esc = (p->a_esc + 1u) % AUDIO_MAX;
        p->a_n++;
    }
}

/* Lee hasta tener la cola llena o el audio bien por delante, y siempre hasta tener al
 * menos un frame en la cola (o el final). */
static void llena(Peli *p)
{
    while (p->cola_n < COLA && (p->cola_n == 0u || p->a_n < AUDIO_POR_DELANTE)) {
        const uint8_t *s = sector(p);
        uint32_t len = 0;
        int np = 0, r;

        if (s == NULL) {
            len = str_flush(&p->dmx);
            if (len == 0u)
                return;
            r = 1;
        } else {
            r = str_feed(&p->dmx, s, p->pcm, &np, &len);
            if (np > 0)
                audio_mete(p, p->pcm, np, p->dmx.stereo);
            if (r == 2)
                p->lee_at--;            /* el sector abre el frame siguiente */
        }
        if (r) {
            Comprimido *c = &p->cola[(p->cola_ini + p->cola_n) % COLA];
            c->len = len;
            memcpy(c->data, p->dmx.data, len);
            p->cola_n++;
        }
    }
}

/* ---- audio ---- */

static void *audio_cb(snd_stream_hnd_t hnd, int bytes, int *got)
{
    Peli *p = s_p;
    uint32_t pares = (uint32_t)bytes / 4u, hasta_fin;

    (void)hnd;
    if (p == NULL || p->a_n == 0u) {
        *got = 0;
        return NULL;
    }
    if (pares > p->a_n)
        pares = p->a_n;
    hasta_fin = AUDIO_MAX - p->a_lee;   /* sin dar la vuelta: el resto en la siguiente */
    if (pares > hasta_fin)
        pares = hasta_fin;
    *got = (int)(pares * 4u);
    {
        void *r = &p->anillo[2u * p->a_lee];
        p->a_lee = (p->a_lee + pares) % AUDIO_MAX;
        p->a_n -= pares;
        return r;
    }
}

/* ---- video ---- */

/* El conversor YUV del PVR: los macrobloques YUV420, en orden de filas, a una textura
 * YUV422 sin twiddle de TEX de ancho. */
static void yuv_sube(pvr_ptr_t tex, const uint8_t *mbs, uint32_t mbw, uint32_t mbh)
{
    uint32_t n = mbw * mbh, espera = 0;

    PVR_SET(PVR_YUV_ADDR, ((uint32_t)tex) & 0xFFFFFFu);
    PVR_SET(PVR_YUV_CFG, (0u << 24) | ((mbh - 1u) << 8) | (mbw - 1u));
    (void)PVR_GET(PVR_YUV_CFG);
    sq_cpy((void *)PVR_TA_YUV_CONV, mbs, n * MDEC_YUV_MB);
    /* hasta que el PVR los haya convertido todos (ya lo estara: es mas rapido que el SQ) */
    while (PVR_GET(PVR_YUV_STAT) < n && ++espera < 1000000u)
        ;
}

static void dibuja(pvr_ptr_t tex, uint32_t w, uint32_t h)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t v;
    float u1 = (float)w / TEX, v1 = (float)h / TEX;

    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_PT_POLY);
    pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY, PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED,
                     TEX, TEX, tex, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof hdr);
    v.argb = 0xFFFFFFFFu;
    v.oargb = 0;
    v.z = 1.0f;
    v.flags = PVR_CMD_VERTEX;
    v.x = 0.0f;   v.y = 0.0f;   v.u = 0.0f; v.v = 0.0f; pvr_prim(&v, sizeof v);
    v.x = 640.0f; v.y = 0.0f;   v.u = u1;   v.v = 0.0f; pvr_prim(&v, sizeof v);
    v.x = 0.0f;   v.y = 480.0f; v.u = 0.0f; v.v = v1;   pvr_prim(&v, sizeof v);
    v.flags = PVR_CMD_VERTEX_EOL;
    v.x = 640.0f; v.y = 480.0f; v.u = u1;   v.v = v1;   pvr_prim(&v, sizeof v);
    pvr_list_finish();
    pvr_list_begin(PVR_LIST_TR_POLY);
    pvr_list_finish();
    pvr_scene_finish();
}

/* Cuantos frames de video tiene el archivo: del numero de frame del primer y el ultimo
 * sector de video (sin leerlo entero). */
static uint32_t cuenta_frames(FILE *f, uint32_t sectores)
{
    uint8_t s[STR_SECTOR];
    uint32_t primero = 0, ultimo = 0;

    for (uint32_t i = 0; i < 64u && i < sectores; i++) {
        fseek(f, (long)i * STR_SECTOR, SEEK_SET);
        if (fread(s, STR_SECTOR, 1, f) == 1u && (s[2] & 0x06u) == 0x02u && s[8] == 0x60u && s[9] == 0x01u) {
            primero = s[16] | (s[17] << 8) | ((uint32_t)s[18] << 16) | ((uint32_t)s[19] << 24);
            break;
        }
    }
    for (uint32_t i = 1; i <= 64u && i <= sectores; i++) {
        fseek(f, (long)(sectores - i) * STR_SECTOR, SEEK_SET);
        if (fread(s, STR_SECTOR, 1, f) == 1u && (s[2] & 0x06u) == 0x02u && s[8] == 0x60u && s[9] == 0x01u) {
            ultimo = s[16] | (s[17] << 8) | ((uint32_t)s[18] << 16) | ((uint32_t)s[19] << 24);
            break;
        }
    }
    fseek(f, 0, SEEK_SET);
    return ultimo >= primero && ultimo ? ultimo - primero + 1u : 0u;
}

uint16_t movie_play(GameHost *h, const char *name)
{
    char ruta[96];
    Peli *p;
    uint8_t *mbs = NULL;
    pvr_ptr_t tex[2] = { NULL, NULL };
    snd_stream_hnd_t snd = SND_STREAM_INVALID;
    uint16_t sujeto, corta = 0;
    uint32_t sectores, frames, k;
    long tam;
    float fps;
    uint64_t t0;

    /* con un guion de entrada manda el guion, y los guiones cuentan con que no hay video */
    if (h->pad_steps_n != 0u)
        return 0;
    snprintf(ruta, sizeof ruta, "/cd/BAKED/FMV/%s", name);
    p = calloc(1, sizeof *p);
    if (p == NULL)
        return 0;
    p->f = fopen(ruta, "rb");
    if (p->f == NULL) {
        printf("pelicula %s: no esta en el disco (make -C dc bake-movies)\n", name);
        free(p);
        return 0;
    }
    fseek(p->f, 0, SEEK_END);
    tam = ftell(p->f);
    fseek(p->f, 0, SEEK_SET);
    sectores = (uint32_t)(tam / STR_SECTOR);
    frames = cuenta_frames(p->f, sectores);
    fps = frames && sectores ? 150.0f * (float)frames / (float)sectores : 15.0f;

    p->lee = malloc((size_t)STR_SECTOR * LEE);
    p->cola = malloc(sizeof(Comprimido) * COLA);
    p->anillo = memalign(32, AUDIO_MAX * 4u);
    mbs = memalign(32, (TEX / 16u) * (TEX / 16u) * MDEC_YUV_MB);
    tex[0] = pvr_mem_malloc(TEX * TEX * 2u);
    tex[1] = pvr_mem_malloc(TEX * TEX * 2u);
    if (!p->lee || !p->cola || !p->anillo || !mbs || !tex[0] || !tex[1]) {
        printf("pelicula %s: sin memoria\n", name);
        goto fin;
    }
    str_reset(&p->dmx);
    mdec_init();
    llena(p);
    if (p->cola_n == 0u || p->dmx.width == 0u || p->dmx.width > TEX || p->dmx.height > TEX ||
        (p->dmx.width & 15u) || (p->dmx.width != TEX)) {
        printf("pelicula %s: formato no soportado (%ux%u)\n", name, p->dmx.width, p->dmx.height);
        goto fin;
    }
    printf("pelicula %s: %u frames de %ux%u a %.2f fps, audio %s a %u Hz\n", name,
           (unsigned)frames, p->dmx.width, p->dmx.height, (double)fps,
           p->dmx.stereo ? "estereo" : "mono", (unsigned)p->dmx.rate);

    if (!s_snd_listo) {
        snd_stream_init();
        s_snd_listo = 1;
    }
    s_p = p;
    snd = snd_stream_alloc(audio_cb, SND_STREAM_BUFFER_MAX / 4);
    if (snd != SND_STREAM_INVALID)
        snd_stream_start(snd, p->dmx.rate, 1);

    sujeto = dc_read_pad();
    t0 = timer_us_gettime64();
    for (k = 0;; k++) {
        uint32_t w = p->dmx.width, hh = p->dmx.height;
        uint32_t mbw = (w + 15u) / 16u, mbh = (hh + 15u) / 16u;
        uint64_t toca = t0 + (uint64_t)((double)k * 1000000.0 / (double)fps);
        Comprimido *c;

        llena(p);
        if (p->cola_n == 0u)
            break;
        c = &p->cola[p->cola_ini];
        mdec_frame_yuv420(c->data, c->len, (int)w, (int)hh, mbs);
        p->cola_ini = (p->cola_ini + 1u) % COLA;
        p->cola_n--;
        /* hasta que le toque: el audio, la lectura y el mando no esperan */
        for (;;) {
            uint16_t b = dc_read_pad();

            sujeto &= b;
            if (b & ~sujeto) {
                corta = b & (uint16_t)~sujeto;
                goto fin;
            }
            if (snd != SND_STREAM_INVALID)
                snd_stream_poll(snd);
            llena(p);
            if (timer_us_gettime64() >= toca)
                break;
            thd_pass();
        }
        yuv_sube(tex[k & 1u], mbs, mbw, mbh);
        dibuja(tex[k & 1u], w, hh);
    }
    /* el ultimo frame se queda en pantalla lo que dura, y el audio que quede suena */
    {
        uint64_t hasta = t0 + (uint64_t)((double)k * 1000000.0 / (double)fps);

        while (timer_us_gettime64() < hasta) {
            if (snd != SND_STREAM_INVALID)
                snd_stream_poll(snd);
            thd_pass();
        }
    }
fin:
    if (snd != SND_STREAM_INVALID) {
        snd_stream_stop(snd);
        snd_stream_destroy(snd);
    }
    s_p = NULL;
    if (tex[0]) pvr_mem_free(tex[0]);
    if (tex[1]) pvr_mem_free(tex[1]);
    free(mbs);
    free(p->anillo);
    free(p->cola);
    free(p->lee);
    fclose(p->f);
    free(p);
    if (corta)
        printf("pelicula %s: cortada en el frame %u (botones 0x%04x)\n", name, (unsigned)k, corta);
    return corta;
}
