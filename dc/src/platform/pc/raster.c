/* Rasterizador por software, SOLO para el arnes de PC.
 *
 * No es parte del port: en Dreamcast dibuja el PVR. Esto existe porque el paso 3 de M2a
 * (la cache de texturas) y el 4 (el dibujo) no tienen oraculo, y la unica forma de saber
 * si la grabacion y la conversion estan bien es **ver un frame**. Dibuja la lista de
 * dibujo tal cual, con las texturas ya convertidas, y escribe un PPM.
 *
 * Tambien sirve de referencia para el paso 4: lo que salga del PVR tiene que parecerse
 * a esto.
 */
#include "host.h"
#include "raster.h"

#include <stdio.h>
#include <string.h>

#define CLAMP255(v) ((v) < 0 ? 0 : ((v) > 255 ? 255 : (v)))

typedef struct {
    uint8_t *rgb;
    int w, h;
    int cx0, cy0, cx1, cy1;      /* recorte vigente */
} Lienzo;

/* Un vertice ya resuelto: posicion, color y texel. */
typedef struct {
    int x, y;
    int r, g, b;
    int u, v;
} Vert;

static void punto(Lienzo *L, int x, int y, int r, int g, int b, int semi)
{
    uint8_t *p;

    if (x < L->cx0 || x >= L->cx1 || y < L->cy0 || y >= L->cy1)
        return;
    if (x < 0 || y < 0 || x >= L->w || y >= L->h)
        return;
    p = L->rgb + ((size_t)y * L->w + x) * 3u;
    if (semi) {                  /* la mitad y la mitad, que es el modo 0 de PS1 */
        p[0] = (uint8_t)((p[0] + r) / 2);
        p[1] = (uint8_t)((p[1] + g) / 2);
        p[2] = (uint8_t)((p[2] + b) / 2);
    } else {
        p[0] = (uint8_t)r;
        p[1] = (uint8_t)g;
        p[2] = (uint8_t)b;
    }
}

/* Un triangulo con interpolacion afin de color y texel. */
static void triangulo(Lienzo *L, const Vert *a, const Vert *b, const Vert *c,
                      const TexEntry *e, const uint16_t *texbuf, int semi)
{
    int minx = a->x, maxx = a->x, miny = a->y, maxy = a->y;
    int area, x, y;

    if (b->x < minx) minx = b->x;
    if (b->x > maxx) maxx = b->x;
    if (c->x < minx) minx = c->x;
    if (c->x > maxx) maxx = c->x;
    if (b->y < miny) miny = b->y;
    if (b->y > maxy) maxy = b->y;
    if (c->y < miny) miny = c->y;
    if (c->y > maxy) maxy = c->y;
    if (minx < L->cx0) minx = L->cx0;
    if (miny < L->cy0) miny = L->cy0;
    if (maxx > L->cx1) maxx = L->cx1;
    if (maxy > L->cy1) maxy = L->cy1;

    area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (area == 0)
        return;

    for (y = miny; y <= maxy; y++) {
        for (x = minx; x <= maxx; x++) {
            int w0 = (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
            int w1 = (c->x - b->x) * (y - b->y) - (c->y - b->y) * (x - b->x);
            int w2 = (a->x - c->x) * (y - c->y) - (a->y - c->y) * (x - c->x);
            int la, lb, lc, r, g, bb;

            if (area > 0) {
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
            } else {
                if (w0 > 0 || w1 > 0 || w2 > 0)
                    continue;
            }
            /* pesos baricentricos: w1 es el del vertice a, y asi */
            la = w1; lb = w2; lc = w0;
            r = (la * a->r + lb * b->r + lc * c->r) / area;
            g = (la * a->g + lb * b->g + lc * c->g) / area;
            bb = (la * a->b + lb * b->b + lc * c->b) / area;

            if (e != NULL) {
                int u = (la * a->u + lb * b->u + lc * c->u) / area;
                int v = (la * a->v + lb * b->v + lc * c->v) / area;
                int tu = u - e->u0, tv = v - e->v0;
                uint16_t t;

                if (tu < 0) tu = 0;
                if (tv < 0) tv = 0;
                if (tu >= e->w) tu = e->w - 1;
                if (tv >= e->h) tv = e->h - 1;
                t = texbuf[e->offset + (size_t)tv * e->w + tu];
                if (!(t & 0x8000u))
                    continue;            /* color clave: no se pinta */
                /* modulacion de PS1: textura * color * 2, con 0x80 como el 1,0 */
                r = (((t >> 10) & 0x1F) << 3) * r / 128;
                g = (((t >> 5) & 0x1F) << 3) * g / 128;
                bb = ((t & 0x1F) << 3) * bb / 128;
            }
            punto(L, x, y, CLAMP255(r), CLAMP255(g), CLAMP255(bb), semi);
        }
    }
}

static void vertice(Vert *o, const OverlayQuad *q, int k)
{
    o->x = q->pts[k][0];
    o->y = q->pts[k][1];
    o->r = (int)(q->colours[k] & 0xFFu);
    o->g = (int)((q->colours[k] >> 8) & 0xFFu);
    o->b = (int)((q->colours[k] >> 16) & 0xFFu);
    o->u = q->has_tex ? q->tex.uv[k][0] : 0;
    o->v = q->has_tex ? q->tex.uv[k][1] : 0;
}

int raster_dump(const GameHost *h, const uint16_t *texbuf, const char *path,
                int w, int hh)
{
    static uint8_t rgb[1024 * 512 * 3];
    Lienzo L;
    uint32_t i, saltados = 0, pintados = 0;
    FILE *f;

    if (w <= 0 || hh <= 0 || (size_t)w * hh * 3u > sizeof rgb)
        return 0;
    memset(rgb, 0, (size_t)w * hh * 3u);
    L.rgb = rgb; L.w = w; L.h = hh;
    L.cx0 = 0; L.cy0 = 0; L.cx1 = w; L.cy1 = hh;

    /* La lista se guarda con el ULTIMO dibujado primero: los paquetes entran en la
     * tabla de ordenacion por la cabeza, asi que el orden de construccion es el inverso
     * al de dibujo. Se recorre del final al principio. */
    for (i = h->overlay.n; i > 0u; i--) {
        const OverlayQuad *q = &h->overlay.quad[i - 1u];
        const TexEntry *e;
        Vert v[4];
        int k;

        if (q->kind == OVL_CLIP) {
            L.cx0 = q->pts[0][0];
            L.cy0 = q->pts[0][1];
            L.cx1 = q->pts[0][0] + q->pts[1][0];
            L.cy1 = q->pts[0][1] + q->pts[1][1];
            continue;
        }
        e = q->has_tex ? tex_find(&h->texcache, q->tex.tpage, q->tex.clut) : NULL;
        if (q->has_tex && e == NULL) {
            saltados++;
            continue;                    /* sin textura convertida no se pinta */
        }
        for (k = 0; k < 4; k++)
            vertice(&v[k], q, k);
        /* el cuadro son dos triangulos: (TL,TR,BL) y (TR,BR,BL) */
        triangulo(&L, &v[0], &v[1], &v[2], e, texbuf, q->semi);
        triangulo(&L, &v[1], &v[3], &v[2], e, texbuf, q->semi);
        pintados++;
    }

    printf("  raster: %u cuadros pintados, %u saltados por falta de textura\n",
           (unsigned)pintados, (unsigned)saltados);
    f = fopen(path, "wb");
    if (f == NULL)
        return 0;
    fprintf(f, "P6\n%d %d\n255\n", w, hh);
    fwrite(rgb, 1, (size_t)w * hh * 3u, f);
    fclose(f);
    return 1;
}
