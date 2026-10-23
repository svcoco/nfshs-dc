/* Cache de texturas de la VRAM de PS1 al PVR. Ver texcache.h para el porque. */
#include "texcache.h"

#include <string.h>

/* ---- la VRAM de PS1 ----
 *
 * La pagina de textura sale de `tpage`: los 4 bits bajos son su X en unidades de 64
 * halfwords, el bit 4 es su Y (0 o 256) y los bits 7 y 8 la profundidad. La paleta sale
 * de `clut`: X en unidades de 16 halfwords y los 9 bits altos, la Y.
 *
 * Un texel vale 0 cuando es transparente, y eso es asi tanto si el color viene de la
 * paleta como si la pagina es de 16 bits: el 0x0000 de PS1 es "no pintes aqui".
 */
#define PAGE_X(tp)   (((uint32_t)(tp) & 0xFu) * 64u)
#define PAGE_Y(tp)   ((((uint32_t)(tp) >> 4) & 1u) * 256u)
#define PAGE_MODE(tp) (((uint32_t)(tp) >> 7) & 3u)
#define CLUT_X(cl)   (((uint32_t)(cl) & 0x3Fu) * 16u)
#define CLUT_Y(cl)   (((uint32_t)(cl) >> 6) & 0x1FFu)

static uint16_t vram_at(const uint16_t *vram, uint32_t x, uint32_t y)
{
    if (x >= TEX_VRAM_W || y >= TEX_VRAM_H)
        return 0;
    return vram[y * TEX_VRAM_W + x];
}

/* PS1 guarda el color como bit de semitransparencia + B G R de 5 bits; el PVR quiere
 * ARGB1555, o sea alfa + R G B. Ademas hay que intercambiar rojo y azul. */
static uint16_t a_pvr(uint16_t c)
{
    uint16_t r, g, b;

    if (c == 0u)
        return 0;                      /* transparente */
    r = (uint16_t)(c & 0x1Fu);
    g = (uint16_t)((c >> 5) & 0x1Fu);
    b = (uint16_t)((c >> 10) & 0x1Fu);
    return (uint16_t)(0x8000u | (r << 10) | (g << 5) | b);
}

void tex_convert(const uint16_t *vram, uint16_t tpage, uint16_t clut,
                 uint16_t u0, uint16_t v0, uint16_t w, uint16_t h, uint16_t *dst)
{
    uint32_t px = PAGE_X(tpage), py = PAGE_Y(tpage), modo = PAGE_MODE(tpage);
    uint32_t cx = CLUT_X(clut), cy = CLUT_Y(clut);
    uint32_t j, i;

    for (j = 0; j < h; j++) {
        uint32_t y = py + v0 + j;
        uint16_t *fila = dst + (size_t)j * w;

        for (i = 0; i < w; i++) {
            uint32_t u = (uint32_t)u0 + i;
            uint16_t c;

            switch (modo) {
            case 0: {                  /* 4 bits: cuatro texeles por halfword */
                uint16_t hw = vram_at(vram, px + (u >> 2), y);
                uint32_t idx = (hw >> ((u & 3u) * 4u)) & 0xFu;
                c = vram_at(vram, cx + idx, cy);
                break;
            }
            case 1: {                  /* 8 bits: dos por halfword */
                uint16_t hw = vram_at(vram, px + (u >> 1), y);
                uint32_t idx = (hw >> ((u & 1u) * 8u)) & 0xFFu;
                c = vram_at(vram, cx + idx, cy);
                break;
            }
            default:                   /* 16 bits: el color esta en la propia pagina */
                c = vram_at(vram, px + u, y);
                break;
            }
            fila[i] = a_pvr(c);
        }
    }
}

/* ---- la cache ---- */

static uint16_t pot(uint32_t n)
{
    uint32_t p = 8u;                   /* el PVR no admite texturas de menos de 8 */

    while (p < n && p < 1024u)
        p *= 2u;
    return (uint16_t)p;
}

void tex_reset(TexCache *tc)
{
    memset(tc, 0, sizeof(*tc));
}

void tex_begin_frame(TexCache *tc)
{
    uint32_t i;

    for (i = 0; i < tc->n; i++) {
        tc->e[i].usada = 0;
        tc->e[i].fu0 = 0xFFFFu;          /* rectangulo del frame, vacio */
        tc->e[i].fv0 = 0xFFFFu;
        tc->e[i].fu1 = 0;
        tc->e[i].fv1 = 0;
    }
}

void tex_need(TexCache *tc, uint16_t tpage, uint16_t clut, const uint16_t uv[4][2])
{
    uint32_t i, k;
    uint16_t u0 = uv[0][0], v0 = uv[0][1], u1 = uv[0][0], v1 = uv[0][1];
    TexEntry *e = NULL;

    for (k = 1; k < 4u; k++) {
        if (uv[k][0] < u0) u0 = uv[k][0];
        if (uv[k][0] > u1) u1 = uv[k][0];
        if (uv[k][1] < v0) v0 = uv[k][1];
        if (uv[k][1] > v1) v1 = uv[k][1];
    }

    for (i = 0; i < tc->n; i++)
        if (tc->e[i].tpage == tpage && tc->e[i].clut == clut) {
            e = &tc->e[i];
            break;
        }
    if (e == NULL) {
        if (tc->n >= TEX_MAX) {
            tc->sin_sitio++;
            return;
        }
        e = &tc->e[tc->n++];
        memset(e, 0, sizeof(*e));
        e->tpage = tpage;
        e->clut = clut;
        e->fu0 = 0xFFFFu;
        e->fv0 = 0xFFFFu;
    }
    /* union de lo que pide ESTE frame, nada mas */
    if (u0 < e->fu0) e->fu0 = u0;
    if (v0 < e->fv0) e->fv0 = v0;
    if (u1 > e->fu1) e->fu1 = u1;
    if (v1 > e->fv1) e->fv1 = v1;
    e->usada = 1;
}

uint32_t tex_build(TexCache *tc, const uint16_t *vram, uint16_t *dst, uint32_t cap)
{
    uint32_t i, vivos = 0, hechas = 0, intento;

    /* Asignador por tope: una vez convertida, una entrada NO se mueve. Compactar cada
     * frame cambiaria todos los offsets y obligaria a reconvertirlo todo. Cuando el
     * buffer se llena se vacia entero y se vuelve a empezar, que con una interfaz
     * estatica pasa muy de vez en cuando. */
    for (intento = 0; intento < 2u; intento++) {
        int lleno = 0;

        vivos = 0;
        for (i = 0; i < tc->n; i++) {
            TexEntry *e = &tc->e[i];
            uint16_t w, h;
            uint32_t n;

            if (!e->usada)
                continue;
            /* si lo convertido ya cubre lo que pide el frame, se reutiliza */
            if (e->lista && e->fu0 >= e->u0 && e->fv0 >= e->v0 &&
                e->fu1 < (uint32_t)e->u0 + e->w && e->fv1 < (uint32_t)e->v0 + e->h) {
                vivos += (uint32_t)e->w * e->h;
                continue;
            }
            w = pot((uint32_t)(e->fu1 - e->fu0) + 1u);
            h = pot((uint32_t)(e->fv1 - e->fv0) + 1u);
            n = (uint32_t)w * h;
            if (tc->top + n > cap) {
                lleno = 1;
                break;
            }
            e->u0 = e->fu0;
            e->v0 = e->fv0;
            e->w = w;
            e->h = h;
            e->offset = tc->top;
            tc->top += n;
            tex_convert(vram, e->tpage, e->clut, e->u0, e->v0, w, h, dst + e->offset);
            e->lista = 1;
            tc->conversiones++;
            tc->texels_conv += n;
            hechas++;
            vivos += n;
        }
        if (!lleno)
            break;
        /* se vacia y se repite: en la segunda pasada todo entra desde cero */
        for (i = 0; i < tc->n; i++)
            tc->e[i].lista = 0;
        tc->top = 0;
        tc->vaciados++;
    }

    tc->texels = vivos;
    if (tc->top > tc->pico_texels)
        tc->pico_texels = tc->top;
    return hechas;
}

const TexEntry *tex_find(const TexCache *tc, uint16_t tpage, uint16_t clut)
{
    uint32_t i;

    for (i = 0; i < tc->n; i++)
        if (tc->e[i].tpage == tpage && tc->e[i].clut == clut && tc->e[i].lista)
            return &tc->e[i];
    return NULL;
}

void tex_dirty(TexCache *tc, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t i;

    for (i = 0; i < tc->n; i++) {
        TexEntry *e = &tc->e[i];
        uint32_t modo = PAGE_MODE(e->tpage);
        /* el rectangulo de la entrada, en halfwords de la VRAM */
        uint32_t ex = PAGE_X(e->tpage) + (modo == 0u ? (e->u0 >> 2) : modo == 1u ? (e->u0 >> 1) : e->u0);
        uint32_t ew = (modo == 0u ? (e->w >> 2) : modo == 1u ? (e->w >> 1) : e->w) + 1u;
        uint32_t ey = PAGE_Y(e->tpage) + e->v0;
        uint32_t cx = CLUT_X(e->clut), cy = CLUT_Y(e->clut);
        uint32_t cw = (modo == 0u) ? 16u : 256u;
        int toca_pagina = !(x >= ex + ew || x + w <= ex || y >= ey + e->h || y + h <= ey);
        /* la paleta tambien cuenta: si se reescribe, el color cambia */
        int toca_paleta = (modo != 2u) &&
                          !(x >= cx + cw || x + w <= cx || y >= cy + 1u || y + h <= cy);

        if (e->lista && (toca_pagina || toca_paleta)) {
            e->lista = 0;
            tc->invalidadas++;
        }
    }
}
