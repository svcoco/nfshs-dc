/* Dibujo 2D con el PVR (M2a, paso 4).
 *
 * Toma la lista de dibujo que graba el host y las texturas que convierte la cache, y
 * las pinta. Lo que hay que saber para entenderlo:
 *
 * **El orden.** La lista se guarda con el ULTIMO dibujado primero, porque los paquetes
 * del juego entran en la tabla de ordenacion por la cabeza. Se recorre del final al
 * principio, y el PVR se configura con el autosort DESACTIVADO para que respete el
 * orden de envio: esto es un overlay 2D, un algoritmo del pintor, no una escena con
 * profundidad.
 *
 * **Las coordenadas.** El juego dibuja en su pantalla de PS1 (512x240 medidos) y la
 * salida es 640x480, asi que se escala el area de pantalla entera: x1,25 en horizontal
 * y x2 en vertical. No es un x2 uniforme (ver docs/hitos/M2a.md).
 *
 * **El color.** En PS1 el 0x80 del color de vertice es el 1,0 y modula la textura, asi
 * que aqui se duplica y se satura. Lo que PS1 podia pasar de 1,0 se pierde; es la
 * aproximacion de siempre y el PVR no tiene shaders para hacer otra cosa.
 *
 * **El recorte** se hace en software sobre los cuadros alineados a los ejes, que son
 * todos los que lo usan. El recorte por hardware del PVR va por tiles de 32x32 y no
 * daria el recorte fino que el front end necesita para el texto que se desplaza.
 */
#include <kos.h>

#include <stdio.h>
#include <string.h>

#include "host.h"
#include "render.h"

/* Staging en RAM principal: la cache convierte aqui y de aqui se sube al PVR. Medido:
 * el pico del front end son 438 976 texeles (0,84 MB). */
#define TEX_TEXELS (512u * 1024u)

static uint16_t  tex_staging[TEX_TEXELS];
static pvr_ptr_t tex_vram;
static uint64_t  subidas_vistas = (uint64_t)-1;   /* para saber cuando resubir */

static uint64_t s_frames, s_quads, s_strips;

/* ---- la pista (M2b paso 3) ----
 *
 * La pista va a la lista OPACA con Z-buffer, al reves que el overlay 2D, que es
 * algoritmo del pintor en la lista translucida. Son dos regimenes distintos en el mismo
 * frame, y el PVR los lleva bien porque son listas separadas.
 *
 * Cual cargar lo dice el propio juego: el host se queda con el nombre del .GRP que abre
 * (host/files.c) y aqui se carga /cd/BAKED/<nombre>.TRK.
 */
static Track     s_track;
static char      s_track_cargada[16];
static pvr_ptr_t s_atlas;
static uint64_t  s_tri, s_tri_frames;

uint64_t render_tris(void)       { return s_tri; }
uint64_t render_tri_frames(void) { return s_tri_frames; }

static void pista_carga(const char *nombre)
{
    char ruta[64];
    char may[16];
    uint32_t i;

    if (s_track.raw != NULL)
        track_free(&s_track);
    if (s_atlas != NULL) {
        pvr_mem_free(s_atlas);
        s_atlas = NULL;
    }
    /* El nombre se anota ANTES de intentar nada: si la carga falla no se reintenta en
     * cada frame. Reintentarlo costaba un fopen por frame y hundia el ritmo a 4,5 fps. */
    strncpy(s_track_cargada, nombre, sizeof s_track_cargada - 1u);
    s_track_cargada[sizeof s_track_cargada - 1u] = 0;
    if (nombre[0] == 0)
        return;
    /* El juego abre "zTr06.grp" y el ISO del disco lo tiene en mayusculas. */
    for (i = 0; i + 1u < sizeof may && nombre[i] != 0; i++)
        may[i] = (char)((nombre[i] >= 'a' && nombre[i] <= 'z')
                        ? nombre[i] - 'a' + 'A' : nombre[i]);
    may[i] = 0;
    snprintf(ruta, sizeof ruta, "/cd/BAKED/%s.TRK", may);
    if (!track_load(&s_track, ruta)) {
        printf("pista: no pude cargar %s\n", ruta);
        return;
    }
    s_atlas = pvr_mem_malloc((uint32_t)s_track.atlas_w * s_track.atlas_h * 2u);
    if (s_atlas == NULL) {
        printf("pista: el atlas de %ux%u no cabe en la VRAM del PVR\n",
               (unsigned)s_track.atlas_w, (unsigned)s_track.atlas_h);
        track_free(&s_track);
        return;
    }
    pvr_txr_load((void *)s_track.atlas, s_atlas,
                 (uint32_t)s_track.atlas_w * s_track.atlas_h * 2u);
    printf("pista %s: %u bloques, %u cuadros, atlas %ux%u\n", nombre,
           (unsigned)s_track.blocks, (unsigned)s_track.quads,
           (unsigned)s_track.atlas_w, (unsigned)s_track.atlas_h);
}

/* Modos de dibujo del mundo. El cielo no escribe profundidad (va detras de todo) y sus
 * cuadros son unos con textura y otros planos, asi que hay que cambiar de contexto sobre
 * la marcha: cada llamada al sumidero es una tira entera, asi que basta con mirar si el
 * modo cambio y emitir una cabecera nueva. */
#define MODO_CIELO_PLANO 0
#define MODO_CIELO_TEX   1
#define MODO_PISTA       2

static int s_modo;

static void modo_pon(int modo)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;

    if (modo == s_modo)
        return;
    s_modo = modo;
    if (modo == MODO_CIELO_PLANO)
        pvr_poly_cxt_col(&cxt, PVR_LIST_PT_POLY);
    else
        pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY,
                         PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
                         s_track.atlas_w, s_track.atlas_h, s_atlas, PVR_FILTER_NONE);
    cxt.gen.culling = PVR_CULLING_NONE;
    if (modo != MODO_PISTA) {
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    }
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));
}

/* El sumidero. El PVR dibuja TIRAS: para el abanico (0,1,2,3) que emite track_draw, la
 * tira equivalente es 0, 1, 3, 2. */
static void pista_emite(void *user, const TrackVert *v, uint32_t n, const TrackMat *m)
{
    pvr_vertex_t pv;
    uint32_t k;

    (void)user;
    if (s_modo != MODO_PISTA)
        modo_pon(m != NULL ? MODO_CIELO_TEX : MODO_CIELO_PLANO);
    pv.flags = PVR_CMD_VERTEX;
    pv.oargb = 0;
    for (k = 0; k < n; k++) {
        uint32_t i = (n == 4u && k >= 2u) ? (5u - k) : k;

        if (k == n - 1u)
            pv.flags = PVR_CMD_VERTEX_EOL;
        pv.x = v[i].x;
        pv.y = v[i].y;
        pv.z = v[i].z;
        pv.u = v[i].u;
        pv.v = v[i].v;
        pv.argb = v[i].argb;
        pvr_prim(&pv, sizeof(pv));
    }
    s_tri += n - 2u;
}

static void pista_dibuja(GameHost *h)
{
    TrackStats st, sst, ost;

    if (strncmp(h->track_name, s_track_cargada, sizeof s_track_cargada) != 0)
        pista_carga(h->track_name);
    if (s_track.raw == NULL || h->views_n == 0u)
        return;
    /* Por encima del anillo mas alto de la cupula no hay geometria: ese hueco lo cubre
     * el color de fondo. */
    {
        uint32_t cz = sky_zenith(&h->sky);

        if (cz != 0u)
            pvr_set_bg_color((float)((cz >> 16) & 0xFFu) / 255.0f,
                             (float)((cz >> 8) & 0xFFu) / 255.0f,
                             (float)(cz & 0xFFu) / 255.0f);
    }

    pvr_list_begin(PVR_LIST_PT_POLY);
    /* El cielo primero, sin escribir profundidad; la pista despues, con Z. Sin descarte
     * de caras, porque el horneado no normaliza el sentido de giro (hay materiales de
     * doble cara) y del resto se encarga el Z-buffer. */
    s_modo = -1;
    sky_draw(&s_track, &h->sky, &h->view[0], 640.0f, 480.0f, pista_emite, NULL, &sst);
    modo_pon(MODO_PISTA);
    track_draw(&s_track, &h->view[0], h->light_ok ? h->light_table : NULL,
               640.0f, 480.0f, pista_emite, NULL, &st);
    /* Los objetos van en la misma pasada: mismos materiales y mismo atlas. */
    objects_draw(&s_track, h->objects, h->objects_n, &h->view[0],
                 h->light_ok ? h->light_table : NULL,
                 640.0f, 480.0f, pista_emite, NULL, &ost);
    s_tri_frames++;
    pvr_list_finish();
}

uint16_t *render_texbuf(void)   { return tex_staging; }
uint32_t  render_texcap(void)   { return TEX_TEXELS; }
uint64_t  render_frames(void)   { return s_frames; }
uint64_t  render_quads(void)    { return s_quads; }

int render_init(void)
{
    pvr_init_params_t p = {
        /* un overlay 2D no usa modificadores ni punch-through */
        /* OPACA para la pista (M2b) y TRANSLUCIDA para el overlay 2D (M2a). Dejar la
         * lista en BINSIZE_0 y mandarle geometria no da un error: deja la pantalla en
         * negro y hunde el ritmo a 4 fps.
         *
         * El mundo va en PUNCH-THROUGH y no en opaca: sus texturas tienen color clave
         * (los huecos entre las copas de los arboles) y la lista opaca no hace prueba de
         * alfa, asi que esos texeles salian NEGROS y dejaban un fleco en cada silueta. */
        .opb_sizes = { PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16,
                       PVR_BINSIZE_0, PVR_BINSIZE_16 },
        .vertex_buf_size = 512 * 1024,
        .dma_enabled = 0,
        .fsaa_enabled = 0,
        .autosort_disabled = 1,   /* manda el orden de envio: es un overlay 2D */
        .opb_overflow_count = 0
    };

    if (pvr_init(&p) < 0)
        return 0;
    tex_vram = pvr_mem_malloc(TEX_TEXELS * 2u);
    if (tex_vram == NULL)
        return 0;
    pvr_set_bg_color(0.0f, 0.0f, 0.0f);
    return 1;
}

/* ---- recorte ---- */

typedef struct { float x0, y0, x1, y1; } Rect;

/* Recorta un cuadro alineado a los ejes, moviendo tambien sus texeles. Devuelve 0 si
 * no queda nada. Los que no estan alineados se dejan pasar: en el front end no los usa
 * nadie con recorte activo. */
static int recorta(float *x0, float *y0, float *x1, float *y1,
                   float *u0, float *v0, float *u1, float *v1, const Rect *c)
{
    float w = *x1 - *x0, h = *y1 - *y0;

    if (w <= 0.0f || h <= 0.0f)
        return 0;
    if (*x0 < c->x0) {
        *u0 += (*u1 - *u0) * (c->x0 - *x0) / w;
        *x0 = c->x0;
    }
    if (*x1 > c->x1) {
        *u1 -= (*u1 - *u0) * (*x1 - c->x1) / w;
        *x1 = c->x1;
    }
    if (*y0 < c->y0) {
        *v0 += (*v1 - *v0) * (c->y0 - *y0) / h;
        *y0 = c->y0;
    }
    if (*y1 > c->y1) {
        *v1 -= (*v1 - *v0) * (*y1 - c->y1) / h;
        *y1 = c->y1;
    }
    return (*x1 > *x0 && *y1 > *y0);
}

/* ---- el frame ---- */

static uint32_t color_de(const OverlayQuad *q, int k, int semi)
{
    /* el 0x80 de PS1 es el 1,0: se duplica y se satura */
    uint32_t c = q->colours[k];
    uint32_t r = (c & 0xFFu) * 2u, g = ((c >> 8) & 0xFFu) * 2u, b = ((c >> 16) & 0xFFu) * 2u;

    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return ((uint32_t)(semi ? 128u : 255u) << 24) | (r << 16) | (g << 8) | b;
}

static void modo_mezcla(pvr_poly_cxt_t *cxt, int semi, uint8_t abr)
{
    if (!semi) {
        cxt->blend.src = PVR_BLEND_SRCALPHA;
        cxt->blend.dst = PVR_BLEND_INVSRCALPHA;
        return;
    }
    switch (abr) {
    case 1:                            /* suma */
        cxt->blend.src = PVR_BLEND_ONE;
        cxt->blend.dst = PVR_BLEND_ONE;
        break;
    case 3:                            /* suma un cuarto */
        cxt->blend.src = PVR_BLEND_SRCALPHA;
        cxt->blend.dst = PVR_BLEND_ONE;
        break;
    case 2:                            /* resta: el PVR no la tiene */
    default:                           /* 0: mitad y mitad */
        cxt->blend.src = PVR_BLEND_SRCALPHA;
        cxt->blend.dst = PVR_BLEND_INVSRCALPHA;
        break;
    }
}

void render_frame(GameHost *h)
{
    float sx, sy, z = 1.0f;
    Rect clip;
    uint32_t i;

    if (h->display[2] == 0u || h->display[3] == 0u)
        return;
    sx = 640.0f / (float)h->display[2];
    sy = 480.0f / (float)h->display[3];
    clip.x0 = 0.0f; clip.y0 = 0.0f; clip.x1 = 640.0f; clip.y1 = 480.0f;

    /* Las texturas solo se resuben cuando la cache ha convertido algo nuevo. Medido:
     * 63 conversiones en todo el front end, o sea casi nunca. */
    if (h->texcache.conversiones != subidas_vistas) {
        pvr_txr_load(tex_staging, tex_vram, h->texcache.top * 2u);
        subidas_vistas = h->texcache.conversiones;
    }

    pvr_wait_ready();
    pvr_scene_begin();
    /* Primero el mundo, opaco y con Z; despues el overlay 2D encima. */
    pista_dibuja(h);
    pvr_list_begin(PVR_LIST_TR_POLY);

    for (i = h->overlay.n; i > 0u; i--) {
        const OverlayQuad *q = &h->overlay.quad[i - 1u];
        const TexEntry *e;
        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        pvr_vertex_t v;
        float px[4], py[4], pu[4], pv[4];
        int k, alineado;

        if (q->kind == OVL_CLIP) {
            clip.x0 = (float)q->pts[0][0] * sx;
            clip.y0 = (float)q->pts[0][1] * sy;
            clip.x1 = (float)(q->pts[0][0] + q->pts[1][0]) * sx;
            clip.y1 = (float)(q->pts[0][1] + q->pts[1][1]) * sy;
            continue;
        }

        e = q->has_tex ? tex_find(&h->texcache, q->tex.tpage, q->tex.clut) : NULL;
        if (q->has_tex && e == NULL)
            continue;

        for (k = 0; k < 4; k++) {
            px[k] = (float)q->pts[k][0] * sx;
            py[k] = (float)q->pts[k][1] * sy;
            if (e != NULL) {
                pu[k] = ((float)q->tex.uv[k][0] - (float)e->u0) / (float)e->w;
                pv[k] = ((float)q->tex.uv[k][1] - (float)e->v0) / (float)e->h;
            } else {
                pu[k] = 0.0f;
                pv[k] = 0.0f;
            }
        }

        /* recorte, solo sobre los alineados a los ejes */
        alineado = (px[0] == px[2] && px[1] == px[3] &&
                    py[0] == py[1] && py[2] == py[3]);
        if (alineado) {
            if (!recorta(&px[0], &py[0], &px[1], &py[2],
                         &pu[0], &pv[0], &pu[1], &pv[2], &clip))
                continue;
            px[2] = px[0]; px[3] = px[1];
            py[1] = py[0]; py[3] = py[2];
            pu[2] = pu[0]; pu[3] = pu[1];
            pv[1] = pv[0]; pv[3] = pv[2];
        }

        if (e != NULL)
            pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY,
                             PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
                             e->w, e->h,
                             (pvr_ptr_t)((uint8_t *)tex_vram + e->offset * 2u),
                             PVR_FILTER_NONE);
        else
            pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
        /* orden del pintor: sin test ni escritura de profundidad */
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
        cxt.gen.culling = PVR_CULLING_NONE;
        modo_mezcla(&cxt, q->semi, q->abr);
        pvr_poly_compile(&hdr, &cxt);
        pvr_prim(&hdr, sizeof(hdr));

        /* El cuadro va como tira: TL, TR, BL, BR, que es el orden en que se graba. */
        v.flags = PVR_CMD_VERTEX;
        v.z = z;
        v.oargb = 0;
        for (k = 0; k < 4; k++) {
            if (k == 3)
                v.flags = PVR_CMD_VERTEX_EOL;
            v.x = px[k];
            v.y = py[k];
            v.u = pu[k];
            v.v = pv[k];
            v.argb = color_de(q, k, q->semi);
            pvr_prim(&v, sizeof(v));
        }
        z += 0.001f;
        s_quads++;
        s_strips++;
    }

    pvr_list_finish();
    pvr_scene_finish();
    s_frames++;
}
