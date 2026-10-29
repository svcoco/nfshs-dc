/* Los destellos de la carrera (M2c paso 6): farolas, humo, clima, el flare del sol y los
 * halos de los coches.
 *
 * Todo esto el juego lo construye como 2D: rutinas suyas que proyectan un punto del mundo
 * y escriben cuadros de pantalla en el buffer de primitivas. Se corren tal cual, anidadas,
 * y se graba lo que escriben, igual que hace la referencia (src/ps1/host/views.rs y
 * cars.rs). Como la camara del port ES la del juego, esos cuadros caen exactamente donde
 * los puso el original; lo unico que falta al dibujarlos es la profundidad, y para eso se
 * anota de que punto sale cada uno.
 *
 * El orden de las llamadas es el de la referencia y no es cosmetico: grabar paquetes mueve
 * el modo de dibujo vigente (draw_mode), y el HUD que se graba despues depende de el. Por
 * eso tambien se corre y se graba el clima, aunque dibujarlo sea otro paso.
 */
#include "host.h"
#include "addr.h"

#include <math.h>
#include <string.h>

#define ADDR_BWORLD_GLARE      0x8007E068u  /* BWorld_BuildGlareEffects(view, dcache, grupo) */
#define ADDR_TRACK_CHUNK_LIST  0x8013E79Cu  /* puntero a los trozos: 112 bytes cada uno */
#define ADDR_SOUFFLE_INSERT    0x800B8ABCu  /* Souffle_InsertFacet(view) */
#define ADDR_SFX_SOUFFLE_FACET 0x800DE39Cu  /* Sfx_BuildSouffleFacet: una bocanada */
#define ADDR_WEATHER_RENDER    0x800E493Cu  /* Weather_Render(view) */
#define ADDR_HRZ_SET_MATRIX    0x800D159Cu  /* Hrz_SetPSXMatrix(rotacion) */
#define ADDR_FLARE_SUN         0x800CFF30u  /* Flare_Sun */
#define ADDR_SKY_FLAGS         0x801245C8u  /* bit 4: el cielo tiene sol */
#define PRIM_PTR               0x1F800004u  /* el buffer de primitivas crece desde aqui */

/* Donde empieza cada destello en el buffer de primitivas y su punto en el mundo. */
#define MARKS_MAX 256u
static struct { uint32_t at; int32_t anchor[3]; } s_marks[MARKS_MAX];
static uint32_t s_marks_n;

static void marca(Cpu *c, uint32_t p)
{
    if (s_marks_n >= MARKS_MAX)
        return;
    s_marks[s_marks_n].at = ps1_r32(c, PRIM_PTR);
    s_marks[s_marks_n].anchor[0] = (int32_t)ps1_r32(c, p);
    s_marks[s_marks_n].anchor[1] = (int32_t)ps1_r32(c, p + 4u);
    s_marks[s_marks_n].anchor[2] = (int32_t)ps1_r32(c, p + 8u);
    s_marks_n++;
}

/* Las farolas: Flare_Halo y Flare_Halo2 reciben el punto (coorddef) en a3. */
static int glare_hook(Cpu *c, uint32_t addr)
{
    if (addr == ADDR_FLARE_HALO || addr == ADDR_FLARE_HALO2)
        marca(c, c->r[7]);
    return 0;
}

/* El humo: Souffle_tISouffle en a1, con su posicion en +12. */
static int smoke_hook(Cpu *c, uint32_t addr)
{
    if (addr == ADDR_SFX_SOUFFLE_FACET)
        marca(c, c->r[5] + 12u);
    return 0;
}

/* Graba los paquetes de [ini, fin) en `dst` sin tocar la lista 2D del frame (el HUD): se
 * graban en ella y se mueven. Devuelve cuantos cuadros quedaron. */
static uint32_t graba_en(GameHost *h, Cpu *c, uint32_t ini, uint32_t fin,
                         OverlayQuad *dst, uint32_t libres)
{
    uint32_t n0 = h->overlay.n, n;

    overlay_record_packets(h, c, ini, fin);
    n = h->overlay.n - n0;
    if (n > libres) {
        h->glows_lost += n - libres;
        n = libres;
    }
    memcpy(dst, &h->overlay.quad[n0], n * sizeof *dst);
    h->overlay.n = n0;
    return n;
}

/* Un destello por marca: sus cuadros van de su marca a la siguiente. */
static void graba_marcas(GameHost *h, Cpu *c, uint32_t fin, uint32_t car, int smoke)
{
    uint32_t k;

    for (k = 0; k < s_marks_n; k++) {
        uint32_t desde = s_marks[k].at;
        uint32_t hasta = (k + 1u < s_marks_n) ? s_marks[k + 1u].at : fin;
        uint32_t n;
        Glow *g;

        if (hasta <= desde)
            continue;
        if (h->vc->glows_n >= GLOWS_MAX) {
            h->glows_lost++;
            continue;
        }
        n = graba_en(h, c, desde, hasta, &h->vc->glow_quad[h->vc->glow_quads_n],
                     GLOW_QUADS_MAX - h->vc->glow_quads_n);
        if (n == 0u)
            continue;
        g = &h->vc->glows[h->vc->glows_n++];
        memcpy(g->anchor, s_marks[k].anchor, sizeof g->anchor);
        g->car = car;
        g->smoke = (uint8_t)smoke;
        g->radius = 0.0f;
        g->first = (uint16_t)h->vc->glow_quads_n;
        g->n = (uint16_t)n;
        h->vc->glow_quads_n += n;
    }
}

void host_glows_reset(GameHost *h)
{
    uint32_t k;

    for (k = 0; k < VIEWS_MAX; k++) {
        h->cap[k].glows_n = 0;
        h->cap[k].glow_quads_n = 0;
        h->cap[k].sky_quads_n = 0;
        h->cap[k].weather_quads_n = 0;
    }
    h->halo_rng_n = 0;
}

void host_record_glows(GameHost *h, Cpu *c, uint32_t chunks)
{
    uint32_t args[3];
    uint32_t ini, fin, i;

    /* la referencia los vacia en cada vista */
    h->vc->glows_n = 0;
    h->vc->glow_quads_n = 0;

    /* 1. las farolas de los trozos a la vista, como las llama BWorld_OnyxBuildFacets */
    ini = ps1_r32(c, PRIM_PTR);
    s_marks_n = 0;
    c->inner_hook = glare_hook;
    for (i = 0; i < chunks && i < 64u; i++) {
        int16_t chunk = (int16_t)ps1_r16(c, ADDR_CHUNK_BUILD_LIST_D + 4u * i);
        uint32_t grupo;

        if (chunk < 0)
            continue;
        grupo = ps1_r32(c, ps1_r32(c, ADDR_TRACK_CHUNK_LIST) + 112u * (uint32_t)chunk + 84u);
        if (grupo != 0u) {
            args[0] = ADDR_RENDER_VIEW;
            args[1] = ADDR_DCACHE;
            args[2] = grupo;
            (void)ps1_call_nested(c, ADDR_BWORLD_GLARE, args, 3u);
        }
    }
    c->inner_hook = NULL;
    fin = ps1_r32(c, PRIM_PTR);
    if (fin > ini && fin - ini < 0x40000u) {
        h->overlay.prim_offset[0] = 0;
        h->overlay.prim_offset[1] = 0;
        graba_marcas(h, c, fin, 0u, 0);
    }

    /* 2. el humo y el polvo de las ruedas, una bocanada por marca */
    ini = ps1_r32(c, PRIM_PTR);
    s_marks_n = 0;
    c->inner_hook = smoke_hook;
    args[0] = ADDR_RENDER_VIEW;
    (void)ps1_call_nested(c, ADDR_SOUFFLE_INSERT, args, 1u);
    c->inner_hook = NULL;
    fin = ps1_r32(c, PRIM_PTR);
    if (fin > ini && fin - ini < 0x40000u) {
        h->overlay.prim_offset[0] = 0;
        h->overlay.prim_offset[1] = 0;
        graba_marcas(h, c, fin, 0u, 1);
    }

    /* 3. el clima (lluvia o nieve, y las gotas en la lente), tras el mundo */
    ini = ps1_r32(c, PRIM_PTR);
    args[0] = ADDR_RENDER_VIEW;
    (void)ps1_call_nested(c, ADDR_WEATHER_RENDER, args, 1u);
    fin = ps1_r32(c, PRIM_PTR);
    if (fin > ini && fin - ini < 0x40000u) {
        h->overlay.prim_offset[0] = 0;
        h->overlay.prim_offset[1] = 0;
        h->vc->weather_quads_n = graba_en(h, c, ini, fin, h->vc->weather_quad, WEATHER_QUADS_MAX);
    }

    /* 4. el flare del sol (Hrz_BuildSky llama a Flare_Sun si el cielo lo tiene): con la
     * rotacion del horizonte en el GTE, sin traslacion, y el GTE restaurado despues */
    if (ps1_r32(c, ADDR_SKY_DOMETEX) != 0u && (ps1_r32(c, ADDR_SKY_FLAGS) & 4u) != 0u) {
        uint32_t rot[8], tr[3], n0, n, k;

        for (k = 0; k < 8u; k++)
            rot[k] = ps1_gte_read(c, 32u + k);
        for (k = 0; k < 3u; k++)
            tr[k] = ps1_gte_read(c, 37u + k);
        args[0] = ADDR_RENDER_VIEW + 68u;
        (void)ps1_call_nested(c, ADDR_HRZ_SET_MATRIX, args, 1u);
        for (k = 0; k < 3u; k++)
            ps1_gte_write(c, 37u + k, 0u);
        c->r[4] = 0x8013F0F8u;
        c->r[5] = ADDR_DCACHE;
        h->overlay.prim_offset[0] = 0;
        h->overlay.prim_offset[1] = 0;
        n0 = h->overlay.n;
        (void)host_capture_prims(h, c, ADDR_FLARE_SUN);
        n = h->overlay.n - n0;
        /* la referencia los acumula si hay varias vistas en el frame */
        if (n > SKY_QUADS_MAX - h->vc->sky_quads_n) {
            h->glows_lost += n - (SKY_QUADS_MAX - h->vc->sky_quads_n);
            n = SKY_QUADS_MAX - h->vc->sky_quads_n;
        }
        memcpy(&h->vc->sky_quad[h->vc->sky_quads_n], &h->overlay.quad[n0], n * sizeof(OverlayQuad));
        h->vc->sky_quads_n += n;
        h->overlay.n = n0;
        for (k = 0; k < 8u; k++)
            ps1_gte_write(c, 32u + k, rot[k]);
        for (k = 0; k < 3u; k++)
            ps1_gte_write(c, 37u + k, tr[k]);
    }
}

void host_record_halos(GameHost *h, Cpu *c)
{
    uint32_t k;

    h->overlay.prim_offset[0] = 0;
    h->overlay.prim_offset[1] = 0;
    for (k = 0; k < h->halo_rng_n; k++) {
        uint32_t car = h->halo_rng[k][2], n;
        Glow *g;

        if (h->vc->glows_n >= GLOWS_MAX) {
            h->glows_lost++;
            continue;
        }
        n = graba_en(h, c, h->halo_rng[k][0], h->halo_rng[k][1],
                     &h->vc->glow_quad[h->vc->glow_quads_n], GLOW_QUADS_MAX - h->vc->glow_quads_n);
        /* la referencia guarda el halo aunque no tenga cuadros */
        g = &h->vc->glows[h->vc->glows_n++];
        g->anchor[0] = (int32_t)ps1_r32(c, car + 0xA0u);
        g->anchor[1] = (int32_t)ps1_r32(c, car + 0xA4u);
        g->anchor[2] = (int32_t)ps1_r32(c, car + 0xA8u);
        g->car = car;
        g->smoke = 0;
        {
            /* las medias extensiones de su caja (Car_tObj + 0x134), 16.16 */
            float r2 = 0.0f;
            uint32_t i;

            for (i = 0; i < 3u; i++) {
                float e = (float)(int32_t)ps1_r32(c, car + 0x134u + 4u * i) / 65536.0f;

                r2 += e * e;
            }
            g->radius = sqrtf(r2);
        }
        g->first = (uint16_t)h->vc->glow_quads_n;
        g->n = (uint16_t)n;
        h->vc->glow_quads_n += n;
    }
    h->halo_rng_n = 0;
}
