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
#include "car.h"
#include "fx.h"

#include <math.h>
#include <stdlib.h>
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

/* ---- la pista (M2b paso 3) ----
 *
 * Mismo papel que arriba: el paso 3 no tiene oraculo, asi que la unica manera de saber
 * si la camara, la proyeccion y el recorte estan bien es MIRAR un frame. Esto pinta lo
 * que track_draw emite, con buffer de profundidad, porque la pista es opaca y no vale el
 * algoritmo del pintor.
 */

typedef struct {
    uint8_t *rgb;
    float   *z;               /* 1/w: cuanto mayor, mas cerca */
    int      w, h;
    const uint16_t *atlas;
    int      aw, ah;
    uint32_t pintados;
    int      cielo;           /* dibujando el cielo: no escribe profundidad */
    int      plano;           /* el cuadro en curso no lleva textura */
    int      resta;           /* mezcla B*(1-F): lo que el PVR hace con sus factores */
    float    zmul;            /* acerca lo dibujado a la camara (z de 1/w) */
} Pista;

static void tri_pista(Pista *P, const TrackVert *a, const TrackVert *b,
                      const TrackVert *c)
{
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    int minx, maxx, miny, maxy, x, y;

    if (area == 0.0f)
        return;
    /* Los cuadros de la pista vienen en los dos sentidos de giro (hay materiales de
     * doble cara), asi que se rellenan los dos: el signo del area dice cual es. */
    if (area < 0.0f) {
        const TrackVert *s = b;

        b = c;
        c = s;
        area = -area;
    }
    minx = (int)floorf(fminf(fminf(a->x, b->x), c->x));
    maxx = (int)ceilf(fmaxf(fmaxf(a->x, b->x), c->x));
    miny = (int)floorf(fminf(fminf(a->y, b->y), c->y));
    maxy = (int)ceilf(fmaxf(fmaxf(a->y, b->y), c->y));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > P->w - 1) maxx = P->w - 1;
    if (maxy > P->h - 1) maxy = P->h - 1;

    for (y = miny; y <= maxy; y++) {
        for (x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x)) / area;
            float w1 = ((c->x - b->x) * (py - b->y) - (c->y - b->y) * (px - b->x)) / area;
            float w2 = 1.0f - w0 - w1;
            float la, lb, lc, iz;
            size_t o;
            int tu, tv;
            uint16_t t;
            int r, g, bb;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;
            /* w1 es el peso de a, w2 el de b y w0 el de c */
            la = w1; lb = w2; lc = w0;
            iz = la * a->z + lb * b->z + lc * c->z;
            o = (size_t)y * P->w + x;
            iz *= P->zmul;
            /* el efecto va con GEQUAL y sin escribir: tiene que poder empatar con lo opaco */
            if (P->resta ? iz < P->z[o] : iz <= P->z[o])
                continue;                /* hay algo mas cerca */
            if (P->plano) {
                /* sin textura: PS1 pinta el color del vertice tal cual */
                r = (int)((a->argb >> 16) & 0xFF);
                g = (int)((a->argb >> 8) & 0xFF);
                bb = (int)(a->argb & 0xFF);
                if (!P->cielo)
                    P->z[o] = iz;
                P->rgb[o * 3u + 0u] = (uint8_t)CLAMP255(r);
                P->rgb[o * 3u + 1u] = (uint8_t)CLAMP255(g);
                P->rgb[o * 3u + 2u] = (uint8_t)CLAMP255(bb);
                P->pintados++;
                continue;
            }

            tu = (int)((la * a->u + lb * b->u + lc * c->u) * (float)P->aw);
            tv = (int)((la * a->v + lb * b->v + lc * c->v) * (float)P->ah);
            if (tu < 0) tu = 0;
            if (tv < 0) tv = 0;
            if (tu >= P->aw) tu = P->aw - 1;
            if (tv >= P->ah) tv = P->ah - 1;
            t = P->atlas[(size_t)tv * P->aw + tu];
            if (!(t & 0x8000u))
                continue;                /* color clave */

            /* El color del vertice ya viene doblado por track.c, asi que aqui la
             * modulacion es la del PVR: textura * color, con 0xFF como el 1,0. */
            r = (int)(((t >> 10) & 0x1F) << 3) * (int)((a->argb >> 16) & 0xFF) / 255;
            g = (int)(((t >> 5) & 0x1F) << 3) * (int)((a->argb >> 8) & 0xFF) / 255;
            bb = (int)((t & 0x1F) << 3) * (int)(a->argb & 0xFF) / 255;
            if (P->resta) {
                /* B - F, exacto y recortado a cero: lo que el PVR consigue invirtiendo,
                 * sumando F con ONE / ONE y volviendo a invertir (ver render.c). */
                int fr = CLAMP255(r), fg = CLAMP255(g), fb = CLAMP255(bb);
                int br = P->rgb[o * 3u + 0u], bg = P->rgb[o * 3u + 1u], bbl = P->rgb[o * 3u + 2u];

                P->rgb[o * 3u + 0u] = (uint8_t)(br > fr ? br - fr : 0);
                P->rgb[o * 3u + 1u] = (uint8_t)(bg > fg ? bg - fg : 0);
                P->rgb[o * 3u + 2u] = (uint8_t)(bbl > fb ? bbl - fb : 0);
                P->pintados++;
                continue;
            }
            if (!P->cielo)
                P->z[o] = iz;
            P->rgb[o * 3u + 0u] = (uint8_t)CLAMP255(r);
            P->rgb[o * 3u + 1u] = (uint8_t)CLAMP255(g);
            P->rgb[o * 3u + 2u] = (uint8_t)CLAMP255(bb);
            P->pintados++;
        }
    }
}

/* ---- los destellos (M2c paso 6) ----
 *
 * El mismo calculo que en Dreamcast (glow_depth) y las mezclas de PS1 tal cual: el modo 0
 * (mitad y mitad), 1 (suma), 2 (resta) y 3 (suma un cuarto). Prueba la profundidad como el
 * PVR, GEQUAL y sin escribirla. */
typedef struct {
    float x, y, z;
    int r, g, b, u, v;
} GV;

static void tri_destello(Pista *P, const GV *a, const GV *b, const GV *c,
                         const TexEntry *e, const uint16_t *texbuf, int semi, int abr)
{
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    int minx, maxx, miny, maxy, x, y;

    if (area == 0.0f)
        return;
    if (area < 0.0f) {
        const GV *s = b;

        b = c;
        c = s;
        area = -area;
    }
    minx = (int)floorf(fminf(fminf(a->x, b->x), c->x));
    maxx = (int)ceilf(fmaxf(fmaxf(a->x, b->x), c->x));
    miny = (int)floorf(fminf(fminf(a->y, b->y), c->y));
    maxy = (int)ceilf(fmaxf(fmaxf(a->y, b->y), c->y));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > P->w - 1) maxx = P->w - 1;
    if (maxy > P->h - 1) maxy = P->h - 1;
    for (y = miny; y <= maxy; y++) {
        for (x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x)) / area;
            float w1 = ((c->x - b->x) * (py - b->y) - (c->y - b->y) * (px - b->x)) / area;
            float w2 = 1.0f - w0 - w1, iz;
            size_t o = (size_t)y * P->w + x;
            int f[3], k;
            uint8_t *d;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;
            /* w1 es el peso de a, w2 el de b y w0 el de c */
            iz = w1 * a->z + w2 * b->z + w0 * c->z;
            if (iz < P->z[o])
                continue;
            f[0] = (int)(w1 * a->r + w2 * b->r + w0 * c->r);
            f[1] = (int)(w1 * a->g + w2 * b->g + w0 * c->g);
            f[2] = (int)(w1 * a->b + w2 * b->b + w0 * c->b);
            if (e != NULL) {
                int tu = (int)(w1 * a->u + w2 * b->u + w0 * c->u) - e->u0;
                int tv = (int)(w1 * a->v + w2 * b->v + w0 * c->v) - e->v0;
                uint16_t tx;

                if (tu < 0) tu = 0;
                if (tv < 0) tv = 0;
                if (tu >= e->w) tu = e->w - 1;
                if (tv >= e->h) tv = e->h - 1;
                tx = texbuf[e->offset + (size_t)tv * e->w + tu];
                if (!(tx & 0x8000u))
                    continue;
                /* modulacion de PS1: textura * color, con 0x80 como el 1,0 */
                f[0] = (((tx >> 10) & 0x1F) << 3) * f[0] / 128;
                f[1] = (((tx >> 5) & 0x1F) << 3) * f[1] / 128;
                f[2] = ((tx & 0x1F) << 3) * f[2] / 128;
            }
            d = P->rgb + o * 3u;
            for (k = 0; k < 3; k++) {
                int fv = CLAMP255(f[k]), bv = d[k], r;

                if (!semi)
                    r = fv;
                else if (abr == 1)
                    r = bv + fv;
                else if (abr == 2)
                    r = bv - fv;
                else if (abr == 3)
                    r = bv + fv / 4;
                else
                    r = (bv + fv) / 2;
                d[k] = (uint8_t)CLAMP255(r);
            }
            P->pintados++;
        }
    }
}

/* Un cuadro de PS1 (TL, TR, BL, BR) con su z por esquina, o la Z de fondo si es NULL. */
static void cuadro_destello(Pista *P, const GameHost *h, const uint16_t *texbuf,
                            const OverlayQuad *q, const float *z, float sx, float sy)
{
    const TexEntry *e;
    GV v[4];
    int k;

    if (q->kind != OVL_QUAD)
        return;
    e = q->has_tex ? tex_find(&h->texcache, q->tex.tpage, q->tex.clut) : NULL;
    if (q->has_tex && e == NULL)
        return;
    for (k = 0; k < 4; k++) {
        v[k].x = (float)q->pts[k][0] * sx;
        v[k].y = (float)q->pts[k][1] * sy;
        v[k].z = z != NULL ? z[k] : 0.0001f;
        v[k].r = (int)(q->colours[k] & 0xFFu);
        v[k].g = (int)((q->colours[k] >> 8) & 0xFFu);
        v[k].b = (int)((q->colours[k] >> 16) & 0xFFu);
        v[k].u = q->has_tex ? q->tex.uv[k][0] : 0;
        v[k].v = q->has_tex ? q->tex.uv[k][1] : 0;
    }
    tri_destello(P, &v[0], &v[1], &v[2], e, texbuf, q->semi, q->abr);
    tri_destello(P, &v[1], &v[3], &v[2], e, texbuf, q->semi, q->abr);
}

static void emite(void *user, const TrackVert *v, uint32_t n, const TrackMat *m)
{
    Pista *P = (Pista *)user;
    uint32_t k;

    P->plano = (m == NULL);
    for (k = 2; k < n; k++)
        tri_pista(P, &v[0], &v[k - 1u], &v[k]);
}

int raster_track(const GameHost *h, const Track *track, const uint16_t *texbuf,
                 const char *path, int w, int hh)
{
    static uint8_t rgb[640 * 480 * 3];
    static float zbuf[640 * 480];
    Pista P;
    TrackStats st, sst, ost;
    /* PS1_CAMBACK=<metros>[,<altura>]: SOLO DEL ARNES DE PC. Retrasa y sube la camara para
     * poder ver desde fuera lo que la camara de capo no ensena (los coches, sus sombras y
     * las marcas de neumatico, que quedan debajo y detras de ella). No es del juego. */
    RaceView vista = h->view[0];
    {
        const char *e = getenv("PS1_CAMBACK");

        if (e != NULL) {
            double atras = 0.0, alto = 0.0;
            double fx = vista.camera_rot[6], fy = vista.camera_rot[7], fz = vista.camera_rot[8];
            double nf = sqrt(fx * fx + fy * fy + fz * fz);

            sscanf(e, "%lf,%lf", &atras, &alto);
            if (nf > 0.0) {
                vista.camera_pos[0] -= (int32_t)(fx / nf * atras * 65536.0);
                vista.camera_pos[1] -= (int32_t)(fy / nf * atras * 65536.0);
                vista.camera_pos[2] -= (int32_t)(fz / nf * atras * 65536.0);
            }
            vista.camera_pos[1] += (int32_t)(alto * 65536.0);   /* y crece hacia arriba */
        }
    }
    uint32_t coches_tri = 0, efectos_tri = 0, marcas_tri = 0, marcas_det = 0;
    FILE *f;

    if (w <= 0 || hh <= 0 || (size_t)w * hh * 3u > sizeof rgb || h->views_n == 0u)
        return 0;
    {
        uint32_t cenit = sky_zenith(&h->sky);
        size_t i;

        for (i = 0; i < (size_t)w * hh; i++) {
            rgb[i * 3u + 0u] = (uint8_t)((cenit >> 16) & 0xFFu);
            rgb[i * 3u + 1u] = (uint8_t)((cenit >> 8) & 0xFFu);
            rgb[i * 3u + 2u] = (uint8_t)(cenit & 0xFFu);
        }
    }
    memset(zbuf, 0, (size_t)w * hh * sizeof zbuf[0]);
    P.rgb = rgb; P.z = zbuf; P.w = w; P.h = hh;
    P.atlas = track->atlas; P.aw = track->atlas_w; P.ah = track->atlas_h;
    P.pintados = 0;
    P.cielo = 0;
    P.plano = 0;
    P.resta = 0;
    P.zmul = 1.0f;

    /* El cielo primero: va detras de todo y no escribe profundidad. */
    P.cielo = 1;
    sky_draw(track, &h->sky, &vista, (float)w, (float)hh, emite, &P, &sst);
    P.cielo = 0;
    /* el flare del sol, sobre el cielo y debajo del mundo (Z de fondo) */
    if (h->display[2] != 0u && h->display[3] != 0u && getenv("PS1_CAMBACK") == NULL) {
        uint32_t i;

        for (i = 0; i < h->sky_quads_n; i++)
            cuadro_destello(&P, h, texbuf, &h->sky_quad[i], NULL,
                            (float)w / (float)h->display[2], (float)hh / (float)h->display[3]);
    }
    track_draw(track, &vista, h->light_ok ? h->light_table : NULL,
               (float)w, (float)hh, emite, &P, &st);
    objects_draw(track, h->objects, h->objects_n, &vista,
                 h->light_ok ? h->light_table : NULL,
                 (float)w, (float)hh, emite, &P, &ost);
    /* Los coches: cada uno con su modelo horneado, que se carga la primera vez que
     * aparece su codigo. Su atlas es otro, asi que se cambia de textura. */
    {
        static CarModel modelo;
        uint32_t ci;

        for (ci = 0; ci < h->cars_n && ci < CARS_MAX; ci++) {
            const CarDraw *d = &h->car_draw[ci];
            char ruta[64];
            TrackStats cst;

            if (d->code[0] == 0)
                continue;
            if (strncmp(modelo.code, d->code, sizeof modelo.code) != 0) {
                car_free(&modelo);
                snprintf(ruta, sizeof ruta, "dc/build/baked/%s.CAR", d->code);
                if (!car_load(&modelo, ruta)) {
                    printf("  coche: no pude cargar %s\n", ruta);
                    continue;
                }
                strncpy(modelo.code, d->code, sizeof modelo.code - 1u);
            }
            P.atlas = modelo.atlas; P.aw = modelo.atlas_w; P.ah = modelo.atlas_h;
            car_draw(&modelo, d, &vista, 0, (float)w, (float)hh, emite, &P, &cst);
            coches_tri += cst.quads_out;
        }
        P.atlas = track->atlas; P.aw = track->atlas_w; P.ah = track->atlas_h;

        /* Los efectos que el original dibuja RESTANDO: la sombra de cada coche en el
         * suelo y la de sus pasos de rueda. Van despues de todo lo opaco. */
        P.resta = 1;
        P.zmul = 1.003f;
        {
            /* las marcas de neumatico, en el suelo y por debajo de las sombras */
            TrackStats kst;

            if (getenv("PS1_NOFX") == NULL)
                skids_draw(track, h->skids, h->skids_n, &vista, (float)w, (float)hh,
                           emite, &P, &kst);
            marcas_tri += kst.quads_out;
            marcas_det += kst.behind;
        }
        for (ci = 0; ci < h->cars_n && ci < CARS_MAX && getenv("PS1_NOFX") == NULL; ci++) {
            const CarDraw *d = &h->car_draw[ci];
            TrackStats est;

            if (d->code[0] == 0)
                continue;
            P.atlas = track->atlas; P.aw = track->atlas_w; P.ah = track->atlas_h;
            car_shadow(track, d, &vista, (float)w, (float)hh, emite, &P, &est);
            efectos_tri += est.quads_out;
            if (strncmp(modelo.code, d->code, sizeof modelo.code) == 0 && modelo.raw != NULL) {
                P.atlas = modelo.atlas; P.aw = modelo.atlas_w; P.ah = modelo.atlas_h;
                car_draw(&modelo, d, &vista, 1, (float)w, (float)hh, emite, &P, &est);
                efectos_tri += est.quads_out;
            }
        }
        P.resta = 0;
        P.zmul = 1.0f;
        P.atlas = track->atlas; P.aw = track->atlas_w; P.ah = track->atlas_h;
    }
    /* Los destellos, encima de lo restado. Con PS1_CAMBACK no: son 2D de la camara del
     * juego y no se moverian con la otra. */
    if (h->display[2] != 0u && h->display[3] != 0u && getenv("PS1_CAMBACK") == NULL &&
        getenv("PS1_NOFX") == NULL) {
        static float zg[GLOW_QUADS_MAX * 4u];
        float sx = (float)w / (float)h->display[2], sy = (float)hh / (float)h->display[3];
        uint32_t i, k, vistos = 0, antes = P.pintados;
        int pase;

        /* primero los que mezclan o suman y despues los que restan, como en Dreamcast */
        for (pase = 0; pase < 2; pase++) {
            for (i = 0; i < h->glows_n; i++) {
                const Glow *g = &h->glows[i];

                if (!glow_depth(&h->view[0], g, &h->glow_quad[g->first], &zg[g->first * 4u]))
                    continue;
                vistos += (pase == 0);
                for (k = 0; k < g->n; k++) {
                    const OverlayQuad *q = &h->glow_quad[g->first + k];
                    int resta = q->semi && q->abr == 2u;

                    if (resta == pase)
                        cuadro_destello(&P, h, texbuf, q, &zg[(g->first + k) * 4u], sx, sy);
                }
            }
        }
        printf("  destellos: %u de %u delante de la camara, %u pixeles; flare del sol %u cuadros\n",
               (unsigned)vistos, (unsigned)h->glows_n, (unsigned)(P.pintados - antes),
               (unsigned)h->sky_quads_n);
    }
    printf("  efectos restados: %u triangulos, marcas de neumatico: %u de %u\n",
           (unsigned)efectos_tri, (unsigned)marcas_tri, (unsigned)h->skids_n);
    printf("  (marcas: %u triangulos detras de la camara)\n", (unsigned)marcas_det);
    printf("  coches: %u dibujados -> %u triangulos\n",
           (unsigned)h->cars_n, (unsigned)coches_tri);
    printf("  objetos: %u dibujados -> %u triangulos\n",
           (unsigned)h->objects_n, (unsigned)ost.quads_out);
    printf("  cielo: %u triangulos (%u recortados, %u detras)\n",
           (unsigned)sst.quads_out, (unsigned)sst.clipped, (unsigned)sst.behind);
    printf("  pista: %u bloques, %u cuadros -> %u triangulos (%u recortados, %u detras), "
           "%u pixeles\n",
           (unsigned)st.blocks_drawn, (unsigned)st.quads_in, (unsigned)st.quads_out,
           (unsigned)st.clipped, (unsigned)st.behind, (unsigned)P.pintados);

    f = fopen(path, "wb");
    if (f == NULL)
        return 0;
    fprintf(f, "P6\n%d %d\n255\n", w, hh);
    fwrite(rgb, 1, (size_t)w * hh * 3u, f);
    fclose(f);
    return 1;
}
