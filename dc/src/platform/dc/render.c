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

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "host.h"
#include "car.h"
#include "fx.h"
#include "render.h"

/* Staging en RAM principal: la cache convierte aqui y de aqui se sube al PVR. Medido:
 * el pico del front end son 438 976 texeles (0,84 MB). */
#define TEX_TEXELS (512u * 1024u)

static uint16_t  tex_staging[TEX_TEXELS];
static pvr_ptr_t tex_vram;
static uint64_t  subidas_vistas = (uint64_t)-1;   /* para saber cuando resubir */

static uint64_t s_frames, s_quads, s_strips, s_quads_destellos;

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
/* La pagina de mapas de reflejo de la pista (bake-refl), para los reflejos de los coches. */
static pvr_ptr_t s_refl;
static uint64_t  s_tri_reflejo;
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
    if (s_refl != NULL) {
        pvr_mem_free(s_refl);
        s_refl = NULL;
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

    /* y sus mapas de reflejo: sin ellos los coches se dibujan igual, sin reflejo */
    {
        static uint16_t refl[CAR_REFL_SIZE * CAR_REFL_SIZE];

        snprintf(ruta, sizeof ruta, "/cd/BAKED/%sR.RFL", may);
        if (!car_refl_load(refl, ruta)) {
            printf("reflejos: no pude cargar %s\n", ruta);
        } else if ((s_refl = pvr_mem_malloc(sizeof refl)) == NULL) {
            printf("reflejos: no caben en la VRAM del PVR\n");
        } else {
            pvr_txr_load(refl, s_refl, sizeof refl);
        }
    }
}

/* Modos de dibujo del mundo. El cielo no escribe profundidad (va detras de todo) y sus
 * cuadros son unos con textura y otros planos, asi que hay que cambiar de contexto sobre
 * la marcha: cada llamada al sumidero es una tira entera, asi que basta con mirar si el
 * modo cambio y emitir una cabecera nueva. */
#define MODO_CIELO_PLANO 0
#define MODO_CIELO_TEX   1
#define MODO_PISTA       2

static int s_modo;
static float s_zmul = 1.0f;   /* acerca lo dibujado a la camara; ver los efectos */
/* Fase del dibujo del mundo. Solo el cielo cambia de contexto sobre la marcha (sus
 * cuadros son unos planos y otros con textura); la pista y los coches lo fijan antes y
 * el sumidero no lo toca. */
#define FASE_CIELO 0
#define FASE_RESTO 1
static int s_fase;

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
    if (s_fase == FASE_CIELO)
        modo_pon(m != NULL ? MODO_CIELO_TEX : MODO_CIELO_PLANO);
    pv.flags = PVR_CMD_VERTEX;
    pv.oargb = 0;
    for (k = 0; k < n; k++) {
        uint32_t i = (n == 4u && k >= 2u) ? (5u - k) : k;

        if (k == n - 1u)
            pv.flags = PVR_CMD_VERTEX_EOL;
        pv.x = v[i].x;
        pv.y = v[i].y;
        pv.z = v[i].z * s_zmul;
        pv.u = v[i].u;
        pv.v = v[i].v;
        pv.argb = v[i].argb;
        pvr_prim(&pv, sizeof(pv));
    }
    s_tri += n - 2u;
}

/* ---- los coches (M2c paso 4) ----
 *
 * Cada modelo trae su propio atlas (512x256, un cuarto de megabyte), asi que hay una
 * cache pequena: los que caben a la vez en una vista. Se cargan la primera vez que su
 * codigo aparece y se quedan; el guion de una carrera no usa muchos mas.
 */
#define CARS_CACHE 6u

static struct {
    CarModel  modelo;
    pvr_ptr_t atlas;
} s_coches[CARS_CACHE];
static uint32_t s_coches_n;
static uint64_t s_tri_coches;

uint64_t render_car_tris(void) { return s_tri_coches; }
static uint64_t s_tri_efectos;
uint64_t render_fx_tris(void)  { return s_tri_efectos; }

/* Devuelve el indice en la cache, o 0xFFFFFFFF si no se pudo. */
static uint32_t coche_modelo(const char *code)
{
    char ruta[64];
    uint32_t i;
    CarModel *m;

    for (i = 0; i < s_coches_n; i++)
        if (strncmp(s_coches[i].modelo.code, code, 8) == 0)
            return i;
    if (s_coches_n >= CARS_CACHE)
        return 0xFFFFFFFFu;

    m = &s_coches[s_coches_n].modelo;
    snprintf(ruta, sizeof ruta, "/cd/BAKED/%s.CAR", code);
    if (!car_load(m, ruta)) {
        printf("coche: no pude cargar %s\n", ruta);
        /* se anota igualmente, con el codigo puesto, para no reintentarlo cada frame */
        memset(m, 0, sizeof(*m));
        strncpy(m->code, code, sizeof m->code - 1u);
        s_coches[s_coches_n].atlas = NULL;
        return s_coches_n++;
    }
    strncpy(m->code, code, sizeof m->code - 1u);
    s_coches[s_coches_n].atlas =
        pvr_mem_malloc((uint32_t)m->atlas_w * m->atlas_h * 2u);
    if (s_coches[s_coches_n].atlas == NULL) {
        printf("coche %s: no cabe su atlas en la VRAM del PVR\n", code);
        car_free(m);
        strncpy(m->code, code, sizeof m->code - 1u);
        return s_coches_n++;
    }
    pvr_txr_load((void *)m->atlas, s_coches[s_coches_n].atlas,
                 (uint32_t)m->atlas_w * m->atlas_h * 2u);
    printf("coche %s: %u partes, %u triangulos, atlas %ux%u\n", code,
           (unsigned)m->parts, (unsigned)(m->verts / 3u),
           (unsigned)m->atlas_w, (unsigned)m->atlas_h);
    return s_coches_n++;
}

static void coches_dibuja(GameHost *h)
{
    uint32_t ci;

    for (ci = 0; ci < h->cars_n && ci < CARS_MAX; ci++) {
        const CarDraw *d = &h->car_draw[ci];
        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        TrackStats cst;
        uint32_t k;

        if (d->code[0] == 0)
            continue;
        k = coche_modelo(d->code);
        if (k == 0xFFFFFFFFu || s_coches[k].modelo.raw == NULL)
            continue;
        /* cada coche tiene su atlas, asi que toca contexto nuevo */
        pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY,
                         PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
                         s_coches[k].modelo.atlas_w, s_coches[k].modelo.atlas_h,
                         s_coches[k].atlas, PVR_FILTER_BILINEAR);
        cxt.gen.culling = PVR_CULLING_NONE;
        pvr_poly_compile(&hdr, &cxt);
        pvr_prim(&hdr, sizeof(hdr));
        car_draw(&s_coches[k].modelo, d, &h->view[0], 0, 640.0f, 480.0f,
                 pista_emite, NULL, &cst);
        s_tri_coches += cst.quads_out;
    }
}

/* ---- los efectos que el original dibuja RESTANDO (M2c paso 6) ----
 *
 * La PS1 tiene un modo de semitransparencia que resta (B - F). El PVR no: sus dos factores
 * de mezcla se SUMAN. La tecnica conocida para restar con el PVR, sin shaders ni trabajo
 * del SH-4, sale de una identidad:
 *
 *        B - F  =  1 - ((1 - B) + F)
 *
 * Es decir: invertir lo que hay, SUMARLE F y volver a invertir. Las tres cosas son mezclas
 * del PVR:
 *
 *   invertir   color de origen BLANCO, factor de origen INVERSE-DEST-COLOUR (1 - B) y
 *              factor de destino ZERO              ->  1 - B
 *   sumar      factores ONE / ONE                  ->  (1 - B) + F, saturado a 1
 *   invertir   otra vez                            ->  B - F, saturado a 0
 *
 * Eso es el modo 2 de la PS1 tal cual, recortado a cero, no una aproximacion. (Una primera
 * version usaba ZERO / INVERSE-OTHER-COLOUR, o sea B*(1-F): oscurece en PROPORCION. Con las
 * marcas de neumatico, cuyo F es ~0,05, dejaba ~2,5 niveles de oscurecimiento sobre
 * carretera oscura contra los ~13 que resta la PS1, y no se veian.)
 *
 * Las inversiones solo se hacen sobre la CAJA que ocupan los efectos, que se calcula antes
 * con un recorrido sin dibujar, para no pagar la pantalla entera: invertir dos veces fuera
 * de los poligonos deja cada pixel exactamente como estaba.
 *
 * Va a la lista TRANSLUCIDA, porque en la opaca y en la punch-through no hay mezcla, y
 * ANTES del overlay 2D, que tiene que quedar encima. La profundidad de los efectos se prueba
 * contra lo opaco (GEQUAL, sin escribir), y los cuadros se acercan un poco a la camara para
 * que no se hundan en la superficie que cubren, ya que no hay un buffer de profundidad que
 * los desempate como en el original.
 *
 * KOS llama a los factores PVR_BLEND_DESTCOLOR / INVDESTCOLOR; en el factor de ORIGEN actuan
 * sobre el color del destino (lo que ya hay), y en el de DESTINO sobre el del origen.
 */
static void efecto_ctx(pvr_ptr_t atlas, uint32_t w, uint32_t hh);
static float s_bx0, s_by0, s_bx1, s_by1;     /* la caja de los efectos, en pantalla */

/* Sumidero que solo acumula la caja: no emite nada al PVR. */
static void caja_emite(void *user, const TrackVert *v, uint32_t n, const TrackMat *m)
{
    uint32_t k;

    (void)user;
    (void)m;
    for (k = 0; k < n; k++) {
        if (v[k].x < s_bx0) s_bx0 = v[k].x;
        if (v[k].x > s_bx1) s_bx1 = v[k].x;
        if (v[k].y < s_by0) s_by0 = v[k].y;
        if (v[k].y > s_by1) s_by1 = v[k].y;
    }
}

/* Recorre los efectos con el sumidero que se le de. Con `dibuja` a 0 es el recorrido de la
 * caja y no toca el PVR; con 1 pone el contexto de cada atlas antes de cada pieza. */
static void efectos_recorre(GameHost *h, TrackEmit emit, int dibuja)
{
    uint32_t ci;
    TrackStats kst;

    if (dibuja)
        efecto_ctx(s_atlas, s_track.atlas_w, s_track.atlas_h);
    /* las marcas de neumatico: en el suelo, asi que van antes que las sombras */
    skids_draw(&s_track, h->skids, h->skids_n, &h->view[0], 640.0f, 480.0f, emit, NULL, &kst);
    if (dibuja)
        s_tri_efectos += kst.quads_out;

    for (ci = 0; ci < h->cars_n && ci < CARS_MAX; ci++) {
        const CarDraw *d = &h->car_draw[ci];
        TrackStats est;
        uint32_t k;

        if (d->code[0] == 0)
            continue;
        /* la sombra del suelo usa SHAD, que vive en el atlas de la pista */
        if (dibuja)
            efecto_ctx(s_atlas, s_track.atlas_w, s_track.atlas_h);
        car_shadow(&s_track, d, &h->view[0], 640.0f, 480.0f, emit, NULL, &est);
        if (dibuja)
            s_tri_efectos += est.quads_out;

        /* y la de los pasos de rueda, que son partes del propio coche y su atlas */
        k = coche_modelo(d->code);
        if (k != 0xFFFFFFFFu && s_coches[k].modelo.raw != NULL && s_coches[k].atlas != NULL) {
            if (dibuja)
                efecto_ctx(s_coches[k].atlas, s_coches[k].modelo.atlas_w,
                           s_coches[k].modelo.atlas_h);
            car_draw(&s_coches[k].modelo, d, &h->view[0], CAR_PASS_SUBTRACT, 640.0f, 480.0f,
                     emit, NULL, &est);
            if (dibuja)
                s_tri_efectos += est.quads_out;
            /* y el mapa B del reflejo, que resta (modo 2 de su pagina de textura) */
            if (s_refl != NULL && car_env_abr(d, CAR_PASS_ENV_B) == 2) {
                if (dibuja)
                    efecto_ctx(s_refl, CAR_REFL_SIZE, CAR_REFL_SIZE);
                car_draw(&s_coches[k].modelo, d, &h->view[0], CAR_PASS_ENV_B, 640.0f, 480.0f,
                         emit, NULL, &est);
                if (dibuja)
                    s_tri_reflejo += est.quads_out;
            }
        }
    }
}

/* Invierte el color de un rectangulo de pantalla: 1 - B. */
static void inversion(float x0, float y0, float x1, float y1)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t pv;
    static const int qx[4] = { 0, 1, 0, 1 };
    static const int qy[4] = { 0, 0, 1, 1 };
    int k;

    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    cxt.blend.src = PVR_BLEND_INVDESTCOLOR;
    cxt.blend.dst = PVR_BLEND_ZERO;
    cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    cxt.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));
    pv.flags = PVR_CMD_VERTEX;
    pv.z = 1.0f;
    pv.oargb = 0;
    pv.u = 0.0f;
    pv.v = 0.0f;
    pv.argb = 0xFFFFFFFFu;
    for (k = 0; k < 4; k++) {
        if (k == 3)
            pv.flags = PVR_CMD_VERTEX_EOL;
        pv.x = qx[k] ? x1 : x0;
        pv.y = qy[k] ? y1 : y0;
        pvr_prim(&pv, sizeof(pv));
    }
}

static void efecto_ctx(pvr_ptr_t atlas, uint32_t w, uint32_t hh)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY,
                     PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
                     w, hh, atlas, PVR_FILTER_NONE);
    /* sumar F al color invertido */
    cxt.blend.src = PVR_BLEND_ONE;
    cxt.blend.dst = PVR_BLEND_ONE;
    cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    cxt.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));
}

/* El mapa A del reflejo de cada coche: se mezcla como dice su pagina de textura (en las
 * carreras medidas, siempre suma) y se prueba contra lo opaco sin escribir profundidad. Si
 * alguno restara, iria con el B en la caja de la resta; no se ha visto ninguno. */
static void reflejos_dibuja(GameHost *h)
{
    uint32_t ci;

    if (s_refl == NULL || h->views_n == 0u)
        return;
    s_fase = FASE_RESTO;
    for (ci = 0; ci < h->cars_n && ci < CARS_MAX; ci++) {
        const CarDraw *d = &h->car_draw[ci];
        int abr = car_env_abr(d, CAR_PASS_ENV_A);
        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        TrackStats est;
        uint32_t k;

        if (d->code[0] == 0 || abr < 0 || abr == 2)
            continue;
        k = coche_modelo(d->code);
        if (k == 0xFFFFFFFFu || s_coches[k].modelo.raw == NULL)
            continue;
        pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY,
                         PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
                         CAR_REFL_SIZE, CAR_REFL_SIZE, s_refl, PVR_FILTER_NONE);
        cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
        cxt.gen.culling = PVR_CULLING_NONE;
        /* modos de PS1 0, 1 y 3: mitad, suma y suma un cuarto (alfa 128 = mitad) */
        cxt.blend.src = abr == 1 ? PVR_BLEND_ONE : PVR_BLEND_SRCALPHA;
        cxt.blend.dst = abr == 0 ? PVR_BLEND_INVSRCALPHA : PVR_BLEND_ONE;
        pvr_poly_compile(&hdr, &cxt);
        pvr_prim(&hdr, sizeof(hdr));
        car_draw(&s_coches[k].modelo, d, &h->view[0], CAR_PASS_ENV_A, 640.0f, 480.0f,
                 pista_emite, NULL, &est);
        s_tri_reflejo += est.quads_out;
    }
}

uint64_t render_refl_tris(void) { return s_tri_reflejo; }

static void efectos_dibuja(GameHost *h)
{
    if (s_track.raw == NULL || s_atlas == NULL || h->views_n == 0u)
        return;
    s_fase = FASE_RESTO;         /* el sumidero no cambia de contexto por su cuenta */
    s_zmul = 1.003f;

    /* 1. la caja que ocupan los efectos, sin dibujar nada */
    s_bx0 = 1e9f; s_by0 = 1e9f; s_bx1 = -1e9f; s_by1 = -1e9f;
    efectos_recorre(h, caja_emite, 0);
    if (s_bx1 >= s_bx0 && s_by1 >= s_by0) {
        float x0 = floorf(s_bx0) - 1.0f, y0 = floorf(s_by0) - 1.0f;
        float x1 = ceilf(s_bx1) + 1.0f, y1 = ceilf(s_by1) + 1.0f;

        if (x0 < 0.0f) x0 = 0.0f;
        if (y0 < 0.0f) y0 = 0.0f;
        if (x1 > 640.0f) x1 = 640.0f;
        if (y1 > 480.0f) y1 = 480.0f;
        if (x1 > x0 && y1 > y0) {
            /* 2. invertir, 3. sumar los efectos, 4. invertir otra vez: B - F */
            inversion(x0, y0, x1, y1);
            efectos_recorre(h, pista_emite, 1);
            inversion(x0, y0, x1, y1);
        }
    }
    s_zmul = 1.0f;
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
    s_fase = FASE_CIELO;
    sky_draw(&s_track, &h->sky, &h->view[0], 640.0f, 480.0f, pista_emite, NULL, &sst);
    s_fase = FASE_RESTO;
    modo_pon(MODO_PISTA);
    track_draw(&s_track, &h->view[0], h->light_ok ? h->light_table : NULL,
               640.0f, 480.0f, pista_emite, NULL, &st);
    /* Los objetos van en la misma pasada: mismos materiales y mismo atlas. */
    objects_draw(&s_track, h->objects, h->objects_n, &h->view[0],
                 h->light_ok ? h->light_table : NULL,
                 640.0f, 480.0f, pista_emite, NULL, &ost);
    /* y los coches, cada uno con su atlas */
    coches_dibuja(h);
    s_tri_frames++;
    pvr_list_finish();
}

uint16_t *render_texbuf(void)   { return tex_staging; }
uint32_t  render_texcap(void)   { return TEX_TEXELS; }
uint64_t  render_frames(void)   { return s_frames; }
uint64_t  render_quads(void)    { return s_quads; }
uint64_t  render_glow_quads(void) { return s_quads_destellos; }

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

/* ---- recorte general ----
 *
 * Los cuadros que NO estan alineados a los ejes tambien hay que recortarlos: la aguja
 * del tacometro es una linea inclinada que el HUD recorta a su esfera de 28x28, y sin
 * recortarla se salia por debajo. Sutherland-Hodgman contra los cuatro bordes del
 * rectangulo, interpolando UV y color: de un cuadro salen hasta 8 vertices. */
typedef struct { float x, y, u, v, a, r, g, b; } P2;

static void p2_mezcla(P2 *o, const P2 *p, const P2 *q, float s)
{
    o->x = p->x + (q->x - p->x) * s;
    o->y = p->y + (q->y - p->y) * s;
    o->u = p->u + (q->u - p->u) * s;
    o->v = p->v + (q->v - p->v) * s;
    o->a = p->a + (q->a - p->a) * s;
    o->r = p->r + (q->r - p->r) * s;
    o->g = p->g + (q->g - p->g) * s;
    o->b = p->b + (q->b - p->b) * s;
}

/* Recorta contra un borde: eje 0 = x, 1 = y; `max` dice si el borde es el de arriba. */
static int p2_borde(const P2 *in, int n, P2 *out, int eje, int max, float lim)
{
    int i, m = 0;

    for (i = 0; i < n; i++) {
        const P2 *p = &in[i], *q = &in[(i + 1) % n];
        float vp = eje ? p->y : p->x, vq = eje ? q->y : q->x;
        int dp = max ? vp <= lim : vp >= lim;
        int dq = max ? vq <= lim : vq >= lim;

        if (dp)
            out[m++] = *p;
        if (dp != dq && vq != vp)
            p2_mezcla(&out[m++], p, q, (lim - vp) / (vq - vp));
    }
    return m;
}

static int p2_recorta(P2 *pol, int n, const Rect *c)
{
    P2 tmp[16];

    n = p2_borde(pol, n, tmp, 0, 0, c->x0);
    n = p2_borde(tmp, n, pol, 0, 1, c->x1);
    n = p2_borde(pol, n, tmp, 1, 0, c->y0);
    n = p2_borde(tmp, n, pol, 1, 1, c->y1);
    return n;
}

static uint32_t p2_argb(const P2 *p)
{
    uint32_t a = (uint32_t)(p->a + 0.5f), r = (uint32_t)(p->r + 0.5f);
    uint32_t g = (uint32_t)(p->g + 0.5f), b = (uint32_t)(p->b + 0.5f);

    return (a << 24) | ((r > 255u ? 255u : r) << 16) | ((g > 255u ? 255u : g) << 8) |
           (b > 255u ? 255u : b);
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
    return ((uint32_t)(!semi ? 255u : q->abr == 3u ? 64u : 128u) << 24) | (r << 16) | (g << 8) | b;
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

/* ---- los destellos (M2c paso 6) ----
 *
 * Farolas, halos de los coches, humo y el flare del sol: 2D que el juego construye
 * alrededor de un punto del mundo, grabado tal cual (host/glows.c). La camara del port es
 * la del juego, asi que caen donde los puso el original; lo que se les anade es una
 * profundidad (glow_depth), con la que el PVR los prueba contra lo opaco sin escribirla.
 *
 * `z` es NULL para el flare del sol: va a la profundidad del plano de fondo (la que deja
 * el cielo, que no escribe Z), asi que pasa donde solo hay cielo y lo tapa el mundo. Con
 * `sumar`, el cuadro se suma sin mas (ONE / ONE): es el paso del medio de la resta exacta.
 */
#define Z_FONDO 0.0001f       /* el plano de fondo de KOS (pvr_state.zclip) */

static float s_z_pintor;      /* el z del orden del pintor del clima (ver clima_dibuja) */

static void destello_quad(GameHost *h, const OverlayQuad *q, float sx, float sy,
                          const float *z, int sumar, int pintor)
{
    const TexEntry *e;
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t v;
    int k;

    if (q->kind != OVL_QUAD)
        return;
    e = q->has_tex ? tex_find(&h->texcache, q->tex.tpage, q->tex.clut) : NULL;
    if (q->has_tex && e == NULL)
        return;
    if (e != NULL)
        pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY,
                         PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
                         e->w, e->h, (pvr_ptr_t)((uint8_t *)tex_vram + e->offset * 2u),
                         PVR_FILTER_NONE);
    else
        pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    cxt.depth.comparison = pintor ? PVR_DEPTHCMP_ALWAYS : PVR_DEPTHCMP_GEQUAL;
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    cxt.gen.culling = PVR_CULLING_NONE;
    if (sumar) {
        cxt.blend.src = PVR_BLEND_ONE;
        cxt.blend.dst = PVR_BLEND_ONE;
    } else {
        modo_mezcla(&cxt, q->semi, q->abr);
    }
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    /* el orden de PS1 (TL, TR, BL, BR) ya es el de una tira */
    v.flags = PVR_CMD_VERTEX;
    v.oargb = 0;
    for (k = 0; k < 4; k++) {
        if (k == 3)
            v.flags = PVR_CMD_VERTEX_EOL;
        v.x = (float)q->pts[k][0] * sx;
        v.y = (float)q->pts[k][1] * sy;
        v.z = z != NULL ? z[k] : pintor ? s_z_pintor : Z_FONDO;
        v.u = e != NULL ? ((float)q->tex.uv[k][0] - (float)e->u0) / (float)e->w : 0.0f;
        v.v = e != NULL ? ((float)q->tex.uv[k][1] - (float)e->v0) / (float)e->h : 0.0f;
        v.argb = color_de(q, k, q->semi);
        pvr_prim(&v, sizeof(v));
    }
    s_quads_destellos++;
}

static int resta(const OverlayQuad *q)
{
    return q->kind == OVL_QUAD && q->semi && q->abr == 2u;
}

/* Los destellos de la vista: primero los que mezclan o suman, y despues los que restan (el
 * humo oscuro), con su propia caja de invertir, sumar e invertir. */
static void destellos_dibuja(GameHost *h, float sx, float sy)
{
    static float z[GLOW_QUADS_MAX * 4u];
    static uint8_t vale[GLOWS_MAX];
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    uint32_t i, k, c;
    int hay_resta = 0;

    if (h->views_n == 0u)
        return;
    for (i = 0; i < h->glows_n; i++) {
        const Glow *g = &h->glows[i];

        vale[i] = (uint8_t)glow_depth(&h->view[0], g, &h->glow_quad[g->first],
                                      &z[(uint32_t)g->first * 4u]);
        if (!vale[i])
            continue;
        for (k = 0; k < g->n; k++) {
            const OverlayQuad *q = &h->glow_quad[g->first + k];

            if (!resta(q)) {
                destello_quad(h, q, sx, sy, &z[(g->first + k) * 4u], 0, 0);
                continue;
            }
            hay_resta = 1;
            for (c = 0; c < 4u; c++) {
                float x = (float)q->pts[c][0] * sx, y = (float)q->pts[c][1] * sy;

                if (x < bx0) bx0 = x;
                if (x > bx1) bx1 = x;
                if (y < by0) by0 = y;
                if (y > by1) by1 = y;
            }
        }
    }
    if (!hay_resta)
        return;
    bx0 = floorf(bx0) - 1.0f; by0 = floorf(by0) - 1.0f;
    bx1 = ceilf(bx1) + 1.0f;  by1 = ceilf(by1) + 1.0f;
    if (bx0 < 0.0f) bx0 = 0.0f;
    if (by0 < 0.0f) by0 = 0.0f;
    if (bx1 > 640.0f) bx1 = 640.0f;
    if (by1 > 480.0f) by1 = 480.0f;
    if (bx1 <= bx0 || by1 <= by0)
        return;
    inversion(bx0, by0, bx1, by1);
    for (i = 0; i < h->glows_n; i++) {
        const Glow *g = &h->glows[i];

        if (!vale[i])
            continue;
        for (k = 0; k < g->n; k++)
            if (resta(&h->glow_quad[g->first + k]))
                destello_quad(h, &h->glow_quad[g->first + k], sx, sy,
                              &z[(g->first + k) * 4u], 1, 0);
    }
    inversion(bx0, by0, bx1, by1);
}

/* El clima (lluvia o nieve, y las gotas en la lente): 2D que el juego construye despues
 * del mundo, grabado tal cual (host/glows.c). Va encima de todo el 3D y de los destellos y
 * debajo del HUD, en el orden en que se grabo y sin prueba de profundidad, como en la
 * referencia (game/layers.rs). Lo que reste va con su propia caja de invertir, sumar e
 * invertir; en las carreras medidas no hay ninguno (lluvia sumando, gotas a medias). */
static void clima_dibuja(GameHost *h, float sx, float sy)
{
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    uint32_t i, c;
    int hay_resta = 0;

    if (h->views_n == 0u || h->weather_quads_n == 0u)
        return;
    s_z_pintor = 1.0f;
    for (i = 0; i < h->weather_quads_n; i++) {
        const OverlayQuad *q = &h->weather_quad[i];

        if (!resta(q)) {
            destello_quad(h, q, sx, sy, NULL, 0, 1);
            s_z_pintor += 0.001f;
            continue;
        }
        hay_resta = 1;
        for (c = 0; c < 4u; c++) {
            float x = (float)q->pts[c][0] * sx, y = (float)q->pts[c][1] * sy;

            if (x < bx0) bx0 = x;
            if (x > bx1) bx1 = x;
            if (y < by0) by0 = y;
            if (y > by1) by1 = y;
        }
    }
    if (!hay_resta)
        return;
    bx0 = floorf(bx0) - 1.0f; by0 = floorf(by0) - 1.0f;
    bx1 = ceilf(bx1) + 1.0f;  by1 = ceilf(by1) + 1.0f;
    if (bx0 < 0.0f) bx0 = 0.0f;
    if (by0 < 0.0f) by0 = 0.0f;
    if (bx1 > 640.0f) bx1 = 640.0f;
    if (by1 > 480.0f) by1 = 480.0f;
    if (bx1 <= bx0 || by1 <= by0)
        return;
    inversion(bx0, by0, bx1, by1);
    for (i = 0; i < h->weather_quads_n; i++)
        if (resta(&h->weather_quad[i]))
            destello_quad(h, &h->weather_quad[i], sx, sy, NULL, 1, 1);
    inversion(bx0, by0, bx1, by1);
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
    /* el flare del sol: sobre el cielo y debajo del mundo */
    if (h->views_n != 0u)
        for (i = 0; i < h->sky_quads_n; i++)
            destello_quad(h, &h->sky_quad[i], sx, sy, NULL, 0, 0);
    /* el mapa A del reflejo de los coches, que va antes que el B */
    reflejos_dibuja(h);
    /* lo que se resta, antes del 2D para que este quede encima */
    efectos_dibuja(h);
    /* los destellos, encima de lo restado y debajo del HUD, como en la referencia */
    destellos_dibuja(h, sx, sy);
    /* y el clima, encima de todo el 3D y debajo del HUD */
    clima_dibuja(h, sx, sy);

    for (i = h->overlay.n; i > 0u; i--) {
        const OverlayQuad *q = &h->overlay.quad[i - 1u];
        const TexEntry *e;
        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        pvr_vertex_t v;
        float px[4], py[4], pu[4], pv[4];
        int k, alineado, npol;
        P2 pol[16];

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

        /* El poligono en orden de perimetro (la tira TL, TR, BL, BR da 0, 1, 3, 2). */
        {
            static const int per[4] = { 0, 1, 3, 2 };

            for (k = 0; k < 4; k++) {
                uint32_t c = color_de(q, per[k], q->semi);

                pol[k].x = px[per[k]];
                pol[k].y = py[per[k]];
                pol[k].u = pu[per[k]];
                pol[k].v = pv[per[k]];
                pol[k].a = (float)(c >> 24);
                pol[k].r = (float)((c >> 16) & 0xFFu);
                pol[k].g = (float)((c >> 8) & 0xFFu);
                pol[k].b = (float)(c & 0xFFu);
            }
            npol = 4;
        }

        /* recorte: los alineados por el camino corto, que mueve tambien los texeles */
        alineado = (px[0] == px[2] && px[1] == px[3] &&
                    py[0] == py[1] && py[2] == py[3]);
        if (!alineado) {
            float bx0 = pol[0].x, bx1 = pol[0].x, by0 = pol[0].y, by1 = pol[0].y;

            for (k = 1; k < 4; k++) {
                if (pol[k].x < bx0) bx0 = pol[k].x;
                if (pol[k].x > bx1) bx1 = pol[k].x;
                if (pol[k].y < by0) by0 = pol[k].y;
                if (pol[k].y > by1) by1 = pol[k].y;
            }
            if (bx1 < clip.x0 || bx0 > clip.x1 || by1 < clip.y0 || by0 > clip.y1)
                continue;                      /* entero fuera */
            if (bx0 < clip.x0 || bx1 > clip.x1 || by0 < clip.y0 || by1 > clip.y1) {
                npol = p2_recorta(pol, 4, &clip);
                if (npol < 3)
                    continue;
            }
        }
        if (alineado) {
            if (!recorta(&px[0], &py[0], &px[1], &py[2],
                         &pu[0], &pv[0], &pu[1], &pv[2], &clip))
                continue;
            px[2] = px[0]; px[3] = px[1];
            py[1] = py[0]; py[3] = py[2];
            pu[2] = pu[0]; pu[3] = pu[1];
            pv[1] = pv[0]; pv[3] = pv[2];
            {
                static const int per[4] = { 0, 1, 3, 2 };

                for (k = 0; k < 4; k++) {
                    pol[k].x = px[per[k]];
                    pol[k].y = py[per[k]];
                    pol[k].u = pu[per[k]];
                    pol[k].v = pv[per[k]];
                }
            }
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

        /* El poligono va como tira en zigzag: p0, p1, p(n-1), p2, p(n-2)... que para un
         * cuadro sin recortar es la tira TL, TR, BL, BR de siempre. */
        v.flags = PVR_CMD_VERTEX;
        v.z = z;
        v.oargb = 0;
        for (k = 0; k < npol; k++) {
            int i = (k == 0) ? 0 : ((k & 1) ? (k + 1) / 2 : npol - k / 2);

            if (k == npol - 1)
                v.flags = PVR_CMD_VERTEX_EOL;
            v.x = pol[i].x;
            v.y = pol[i].y;
            v.u = pol[i].u;
            v.v = pol[i].v;
            v.argb = p2_argb(&pol[i]);
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
