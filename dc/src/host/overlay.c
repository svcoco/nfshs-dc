/* Grabacion del dibujo 2D del front end. Portado de src/ps1/host/overlay.rs. */
#include "addr.h"
#include "host.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void overlay_reset(Overlay *o)
{
    if (o->frame_combos_n > o->frame_combos_max)
        o->frame_combos_max = o->frame_combos_n;
    o->frame_combos_n = 0;
    o->n = 0;
}

/* Anota la combinacion de pagina y paleta, para dimensionar la cache de texturas. */
static void note_combo(Overlay *o, uint16_t tpage, uint16_t clut)
{
    uint32_t key = ((uint32_t)tpage << 16) | clut;
    uint32_t i;

    for (i = 0; i < o->combos_n; i++)
        if (o->combos[i] == key)
            return;
    if (o->combos_n >= sizeof(o->combos) / sizeof(o->combos[0]))
        o->combos_lost++;
    else
        o->combos[o->combos_n++] = key;
}

/* Lo mismo, pero solo de este frame. */
static void note_frame_combo(Overlay *o, uint16_t tpage, uint16_t clut)
{
    uint32_t key = ((uint32_t)tpage << 16) | clut;
    uint32_t i;

    for (i = 0; i < o->frame_combos_n; i++)
        if (o->frame_combos[i] == key)
            return;
    if (o->frame_combos_n < sizeof(o->frame_combos) / sizeof(o->frame_combos[0]))
        o->frame_combos[o->frame_combos_n++] = key;
}

void overlay_push(GameHost *h, const int16_t pts[4][2], const uint32_t colours[4],
                  const QuadTex *tex, int semi, uint8_t abr)
{
    Overlay *o = &h->overlay;
    OverlayQuad *q;
    uint32_t k;

    o->total++;
    if (o->n >= OVERLAY_MAX) {
        o->dropped++;
        return;
    }
    q = &o->quad[o->n++];
    q->kind = h->ovl_hud ? OVL_HUD_QUAD : OVL_QUAD;
    for (k = 0; k < 4u; k++) {
        /* el desplazamiento de la vista que se esta grabando */
        q->pts[k][0] = (int16_t)(pts[k][0] + o->prim_offset[0]);
        q->pts[k][1] = (int16_t)(pts[k][1] + o->prim_offset[1]);
        q->colours[k] = colours[k];
    }
    if (tex != NULL) {
        q->tex = *tex;
        q->has_tex = 1;
        note_combo(o, tex->tpage, tex->clut);
        note_frame_combo(o, tex->tpage, tex->clut);
    } else {
        memset(&q->tex, 0, sizeof(q->tex));
        q->has_tex = 0;
    }
    q->semi = (uint8_t)(semi != 0);
    q->abr = abr;
}

void overlay_clip(GameHost *h, int16_t x, int16_t y, int16_t w, int16_t hh)
{
    Overlay *o = &h->overlay;
    OverlayQuad *q;

    o->total++;
    if (o->n >= OVERLAY_MAX) {
        o->dropped++;
        return;
    }
    q = &o->quad[o->n++];
    memset(q, 0, sizeof(*q));
    q->kind = OVL_CLIP;
    q->pts[0][0] = x; q->pts[0][1] = y;
    q->pts[1][0] = w; q->pts[1][1] = hh;
}

static void rect_pts(int16_t out[4][2], int32_t x, int32_t y, int32_t w, int32_t h)
{
    out[0][0] = (int16_t)x;       out[0][1] = (int16_t)y;
    out[1][0] = (int16_t)(x + w); out[1][1] = (int16_t)y;
    out[2][0] = (int16_t)x;       out[2][1] = (int16_t)(y + h);
    out[3][0] = (int16_t)(x + w); out[3][1] = (int16_t)(y + h);
}

void overlay_square(GameHost *h, int32_t x, int32_t y, int32_t w, int32_t h_,
                    uint32_t colour, int semi)
{
    int16_t pts[4][2];
    uint32_t col[4];
    uint32_t k;

    rect_pts(pts, x, y, w, h_);
    for (k = 0; k < 4u; k++)
        col[k] = colour & 0xFFFFFFu;
    overlay_push(h, pts, col, NULL, semi, 0u);
}

void overlay_shape_quads(GameHost *h, Cpu *c, uint32_t info, uint32_t flags,
                         int32_t x, int32_t y, const uint32_t colours[4], uint32_t abr)
{
    /* La forma se describe en la RAM del juego: paleta, bits por pixel, modo, tamano y
     * de donde sale en la VRAM. Una forma ancha se parte en varios cuadros porque una
     * pagina de textura de PS1 solo tiene 256 texeles de ancho. */
    uint16_t clut = (uint16_t)ps1_r32(c, info + 4u);
    int32_t  bpp  = (int32_t)(uint8_t)ps1_r8(c, info + 8u);
    uint16_t mode = (uint16_t)(ps1_r8(c, info + 9u) & 3u);
    int32_t  w    = (int16_t)ps1_r16(c, info + 16u);
    int32_t  h0   = (int16_t)ps1_r16(c, info + 18u);
    int32_t  vx   = (int32_t)ps1_r16(c, info + 24u);
    uint16_t w26  = (uint16_t)ps1_r16(c, info + 26u);
    int32_t  top = y, hh = h0, v0 = (int32_t)(w26 & 0xFFu);
    int32_t  bottom, s1 = 0;

    if (bpp < 1)
        bpp = 1;
    if ((flags & 2u) != 0u) {            /* volteada en vertical */
        top += h0;
        hh = -h0;
        v0 -= 1;
    }
    bottom = top + hh;

    while (s1 < w) {
        int32_t first = (vx << 4) / bpp;
        int32_t col = vx + ((s1 * bpp) >> 4);
        int32_t page = ((col & ~63) << 4) / bpp;
        int32_t u = s1 + first - page;
        int32_t width = 255 - u;
        int32_t u0 = u, extra = 0, xa, xb;
        uint16_t tpage;
        QuadTex tex;
        int16_t pts[4][2];

        if (w - s1 < width)
            width = w - s1;
        tpage = (uint16_t)((mode << 7) | (uint16_t)((abr & 3u) << 5) |
                           ((w26 & 0x100u) >> 4) |
                           (uint16_t)((col & 0x3C0) >> 6) |
                           (uint16_t)((w26 & 0x200u) << 2));
        if ((flags & 4u) != 0u && h0 < 255) {   /* volteada en horizontal */
            u0 = u - 1;
            extra = 1;
        }
        if (width <= 0)
            width = 1;
        if ((flags & 4u) != 0u) {
            xa = w + x - s1 + extra - 1;
            xb = w + x - (s1 + width) + extra - 1;
        } else {
            xa = s1 + x;
            xb = s1 + x + width;
        }
        {
            uint16_t va = (uint16_t)(v0 & 0xFF);
            uint16_t ua = (uint16_t)(u0 & 0xFF);
            int32_t  vbi = (int32_t)va + h0;
            uint16_t vb = (uint16_t)(vbi < 0 ? 0 : vbi);
            uint16_t ub = (uint16_t)(ua + (uint16_t)width);

            tex.tpage = tpage;
            tex.clut = clut;
            tex.uv[0][0] = ua; tex.uv[0][1] = va;
            tex.uv[1][0] = ub; tex.uv[1][1] = va;
            tex.uv[2][0] = ua; tex.uv[2][1] = vb;
            tex.uv[3][0] = ub; tex.uv[3][1] = vb;
        }
        pts[0][0] = (int16_t)xa; pts[0][1] = (int16_t)top;
        pts[1][0] = (int16_t)xb; pts[1][1] = (int16_t)top;
        pts[2][0] = (int16_t)xa; pts[2][1] = (int16_t)bottom;
        pts[3][0] = (int16_t)xb; pts[3][1] = (int16_t)bottom;
        overlay_push(h, pts, colours, &tex, (flags & 1u) != 0u,
                     (uint8_t)((tpage >> 5) & 3u));
        s1 += width;
    }
}

/* ---- las rutinas que construyen sus propias primitivas ---- */

static void xy_of(uint32_t v, int16_t out[2])
{
    out[0] = (int16_t)(uint16_t)v;
    out[1] = (int16_t)(uint16_t)(v >> 16);
}

/* Una primitiva de la GPU de PS1, hasta donde el front end las usa. */
static void parse_prim(GameHost *h, Cpu *c, uint32_t a, uint32_t len)
{
    Overlay *o = &h->overlay;
    uint32_t w0 = ps1_r32(c, a);
    uint8_t  code = (uint8_t)(w0 >> 24);
    int semi = (code & 2u) != 0;
    int raw  = (code & 1u) != 0;
    int16_t  pts[4][2];
    uint32_t colours[4];
    QuadTex  tex;
    uint32_t k;

    if (code == 0xE1u) {
        o->draw_mode = (uint16_t)(w0 & 0x1FFu);
        return;
    }

    if (code >= 0x20u && code <= 0x3Fu) {
        /* poligono: 3 o 4 vertices, plano o Gouraud, con textura o sin ella */
        int textured = (code & 4u) != 0;
        int gouraud  = (code & 0x10u) != 0;
        uint32_t n = ((code & 8u) != 0) ? 4u : 3u;
        uint32_t per = 1u + (uint32_t)textured + (uint32_t)gouraud;
        uint16_t clut = 0, tpage = o->draw_mode;
        uint8_t abr;

        if (len < per * n)
            return;
        for (k = 0; k < 4u; k++)
            colours[k] = w0 & 0xFFFFFFu;
        memset(&tex, 0, sizeof(tex));
        for (k = 0; k < n; k++) {
            uint32_t base = 1u + per * k;
            if (gouraud && k > 0u)
                colours[k] = ps1_r32(c, a + 4u * (base - 1u)) & 0xFFFFFFu;
            xy_of(ps1_r32(c, a + 4u * base), pts[k]);
            if (textured) {
                uint32_t t = ps1_r32(c, a + 4u * (base + 1u));
                tex.uv[k][0] = (uint16_t)(uint8_t)t;
                tex.uv[k][1] = (uint16_t)(uint8_t)(t >> 8);
                if (k == 0u)
                    clut = (uint16_t)(t >> 16);
                else if (k == 1u)
                    tpage = (uint16_t)(t >> 16);
            }
        }
        if (n == 3u) {                     /* el triangulo se graba como cuadro */
            pts[3][0] = pts[2][0];
            pts[3][1] = pts[2][1];
            colours[3] = colours[2];
            tex.uv[3][0] = tex.uv[2][0];
            tex.uv[3][1] = tex.uv[2][1];
        }
        if (textured)
            o->draw_mode = (uint16_t)(tpage & 0x1FFu);
        if (textured && raw)
            for (k = 0; k < 4u; k++)
                colours[k] = 0x808080u;    /* sin modular: el 1,0 de PS1 es 0x80 */
        abr = (uint8_t)(((textured ? tpage : o->draw_mode) >> 5) & 3u);
        tex.tpage = tpage;
        tex.clut = clut;
        overlay_push(h, pts, colours, textured ? &tex : NULL, semi, abr);
        return;
    }

    if (code >= 0x60u && code <= 0x7Fu) {
        /* rectangulo o sprite, de tamano variable o de 1, 8 o 16 pixeles */
        int textured = (code & 4u) != 0;
        int16_t p[2];
        int32_t tw, th, x, y;
        uint16_t u0 = 0, v0 = 0, clut = 0;
        uint32_t kk = 2u;

        xy_of(ps1_r32(c, a + 4u), p);
        if (textured) {
            uint32_t t = ps1_r32(c, a + 8u);
            u0 = (uint16_t)(uint8_t)t;
            v0 = (uint16_t)(uint8_t)(t >> 8);
            clut = (uint16_t)(t >> 16);
            kk = 3u;
        }
        switch ((code >> 3) & 3u) {
        case 0: {
            uint32_t s = ps1_r32(c, a + 4u * kk);
            tw = (int32_t)(s & 0xFFFFu);
            th = (int32_t)(s >> 16);
            break;
        }
        case 1:  tw = 1;  th = 1;  break;
        case 2:  tw = 8;  th = 8;  break;
        default: tw = 16; th = 16; break;
        }
        x = p[0];
        y = p[1];
        for (k = 0; k < 4u; k++)
            colours[k] = (textured && raw) ? 0x808080u : (w0 & 0xFFFFFFu);
        rect_pts(pts, x, y, tw, th);
        if (textured) {
            uint16_t u1 = (uint16_t)(u0 + (uint16_t)tw);
            uint16_t v1 = (uint16_t)(v0 + (uint16_t)th);
            tex.tpage = o->draw_mode;
            tex.clut = clut;
            tex.uv[0][0] = u0; tex.uv[0][1] = v0;
            tex.uv[1][0] = u1; tex.uv[1][1] = v0;
            tex.uv[2][0] = u0; tex.uv[2][1] = v1;
            tex.uv[3][0] = u1; tex.uv[3][1] = v1;
        }
        overlay_push(h, pts, colours, textured ? &tex : NULL, semi,
                     (uint8_t)((o->draw_mode >> 5) & 3u));
        return;
    }

    if (code >= 0x40u && code <= 0x5Fu && (code & 8u) == 0u) {
        /* una linea: un cuadro de un pixel de ancho */
        int gouraud = (code & 0x10u) != 0;
        int16_t p0[2], p1[2];
        uint32_t c0 = w0 & 0xFFFFFFu, c1;
        int32_t dx, dy, nx, ny;

        xy_of(ps1_r32(c, a + 4u), p0);
        if (gouraud) {
            c1 = ps1_r32(c, a + 8u) & 0xFFFFFFu;
            xy_of(ps1_r32(c, a + 12u), p1);
        } else {
            c1 = c0;
            xy_of(ps1_r32(c, a + 8u), p1);
        }
        dx = p1[0] - p0[0];
        dy = p1[1] - p0[1];
        /* La normal de media unidad, redondeada, exactamente como la referencia en f32:
         * n = max(sqrt(dx*dx + dy*dy), 1), (round(-dy/n * 0,5), round(dx/n * 0,5)), y
         * (0, 1) si las dos salen 0. En pantalla solo da +-1 en horizontales y verticales,
         * pero NO es una regla de enteros: con una punta fuera de la pantalla (la lluvia
         * llega a x = 8 205) el dy*dy se pierde en el f32, la normal sale justo -0,5 y
         * redondea a -1. Por eso se reproduce cada operacion f32: en double, que es exacto
         * para estos enteros, y redondeando a float tras cada paso (redondear un double
         * a float da el mismo resultado que la operacion IEEE en f32 para producto,
         * suma, raiz y division). Asi no depende de que el compilador fusione operaciones
         * (el SH-4 tiene FMAC). */
        {
            float fx = (float)((double)dx * (double)dx);
            float fy = (float)((double)dy * (double)dy);
            float n = (float)sqrt((double)(float)((double)fx + (double)fy));
            float ux, uy;

            if (n < 1.0f)
                n = 1.0f;
            ux = (float)((double)-dy / (double)n) * 0.5f;
            uy = (float)((double)dx / (double)n) * 0.5f;
            /* round de Rust: la mitad se aleja del cero */
            nx = (int32_t)(ux < 0.0f ? -floor(0.5 - (double)ux) : floor((double)ux + 0.5));
            ny = (int32_t)(uy < 0.0f ? -floor(0.5 - (double)uy) : floor((double)uy + 0.5));
            if (nx == 0 && ny == 0)
                ny = 1;
        }
        pts[0][0] = p0[0];            pts[0][1] = p0[1];
        pts[1][0] = p1[0];            pts[1][1] = p1[1];
        pts[2][0] = (int16_t)(p0[0] + nx); pts[2][1] = (int16_t)(p0[1] + ny);
        pts[3][0] = (int16_t)(p1[0] + nx); pts[3][1] = (int16_t)(p1[1] + ny);
        colours[0] = c0; colours[1] = c1; colours[2] = c0; colours[3] = c1;
        overlay_push(h, pts, colours, NULL, semi, (uint8_t)((o->draw_mode >> 5) & 3u));
        return;
    }
    h->overlay_unknown++;
}

void overlay_record_packets(GameHost *h, Cpu *c, uint32_t start, uint32_t end)
{
    /* Los paquetes entran en la tabla de ordenacion por la cabeza, asi que se dibujan
     * al reves: un cambio de modo (E1) vale para los que se anadieron ANTES. Se resuelve
     * el modo recorriendo en orden de dibujo, y se graba en orden de construccion. */
    /* Estaticos y no en la pila: son 40 KB y la pila de un hilo de KOS son 32. La
     * grabacion es de un solo hilo, asi que compartirlos no cuesta nada. */
    static uint32_t pos[OVERLAY_MAX], lens[OVERLAY_MAX];
    static uint16_t modes[OVERLAY_MAX];
    uint32_t np = 0, p = start, i;
    uint16_t entrada = h->overlay.draw_mode, modo;

    while (p < end && end - p < 0x100000u && np < OVERLAY_MAX) {
        uint32_t len = ps1_r32(c, p) >> 24;
        if (len == 0u) {
            p += 4u;
            continue;
        }
        pos[np] = p;
        lens[np] = len;
        np++;
        p += 4u + 4u * len;
    }

    modo = entrada;
    for (i = np; i > 0u; i--) {
        uint32_t w0 = ps1_r32(c, pos[i - 1u] + 4u);
        if ((w0 >> 24) == 0xE1u)
            modo = (uint16_t)(w0 & 0x1FFu);
        modes[i - 1u] = modo;
    }
    for (i = 0; i < np; i++) {
        h->overlay.draw_mode = modes[i];
        if ((ps1_r32(c, pos[i] + 4u) >> 24) != 0xE1u)
            parse_prim(h, c, pos[i] + 4u, lens[i]);
    }
    h->overlay.draw_mode = entrada;
}

/* ---- el HUD de carrera (M2c paso 5) ----
 *
 * En carrera el juego construye el HUD (velocimetro, tacometro, marcha, tiempos, posicion,
 * mapa) con su propio codigo, en tablas de ordenacion, una por vista. El host no lo
 * sustituye: en cada DrawOTag graba la tabla entera, salvo la del mundo, que es la
 * primera y trae miles de paquetes. Portado de record_ot y frame_top de la referencia
 * (src/ps1/host/overlay.rs y frame.rs).
 */

/* Paquetes de una tabla, siguiendo los punteros hasta el final. */
uint32_t overlay_ot_len(Cpu *c, uint32_t start)
{
    uint32_t a = start, n = 0;

    while (n < 20000u) {
        uint32_t next = ps1_r32(c, a) & 0xFFFFFFu;

        n++;
        if (next == 0xFFFFFFu)
            break;
        a = 0x80000000u | next;
    }
    return n;
}

static int32_t sx11(uint32_t v)
{
    return ((int32_t)(v << 21)) >> 21;
}

/* Un comando de entorno suelto (E1..E5). */
static void entorno(GameHost *h, uint32_t w)
{
    int32_t ox = h->draw_origin[0], oy = h->draw_origin[1];

    switch (w >> 24) {
    case 0xE1u:
        h->overlay.draw_mode = (uint16_t)(w & 0x1FFu);
        break;
    case 0xE3u:
        h->area_tl[0] = (int32_t)(w & 0x3FFu) - ox;
        h->area_tl[1] = (int32_t)((w >> 10) & 0x3FFu) - oy;
        break;
    case 0xE4u: {
        int32_t x1 = (int32_t)(w & 0x3FFu) - ox;
        int32_t y1 = (int32_t)((w >> 10) & 0x3FFu) - oy;

        overlay_clip(h, (int16_t)h->area_tl[0], (int16_t)h->area_tl[1],
                     (int16_t)(x1 - h->area_tl[0] + 1), (int16_t)(y1 - h->area_tl[1] + 1));
        break;
    }
    case 0xE5u:
        h->overlay.prim_offset[0] = (int16_t)(sx11(w & 0x7FFu) - ox);
        h->overlay.prim_offset[1] = (int16_t)(sx11((w >> 11) & 0x7FFu) - oy);
        break;
    default:
        break;
    }
}

/* Recorre una tabla desde `start` en orden de dibujo y graba sus primitivas. */
void overlay_record_ot(GameHost *h, Cpu *c, uint32_t start)
{
    uint32_t a = start, i;

    h->overlay.prim_offset[0] = 0;
    h->overlay.prim_offset[1] = 0;
    /* cada lista empieza sin recortar (su propio entorno de dibujo, si lo trae, recorta) */
    overlay_clip(h, 0, 0, 1024, 512);

    for (i = 0; i < 20000u; i++) {
        uint32_t tag = ps1_r32(c, a);
        uint32_t len = tag >> 24, next = tag & 0xFFFFFFu, k;

        if (len > 0u) {
            uint8_t code = (uint8_t)ps1_r8(c, a + 7u);

            if (code >= 0xE0u) {
                /* El paquete de entorno de una vista (el de SetDrawEnv): el desplazamiento
                 * sale de su palabra E5 y el recorte del que se le dio a SetDrawEnv para
                 * ese desplazamiento (las palabras de area del paquete no las usa). */
                uint32_t e5 = 0;
                int hay_e5 = 0;

                for (k = 0; k < len; k++) {
                    uint32_t w = ps1_r32(c, a + 4u + 4u * k);

                    if ((w >> 24) == 0xE5u) {
                        e5 = w;
                        hay_e5 = 1;
                        break;
                    }
                }
                if (hay_e5) {
                    int32_t ofs[2], ox = h->draw_origin[0], oy = h->draw_origin[1];
                    const DrawEnvRec *e;

                    ofs[0] = sx11(e5 & 0x7FFu);
                    ofs[1] = sx11((e5 >> 11) & 0x7FFu);
                    /* Primero por la DIRECCION del paquete: el que se recorre es el mismo
                     * que se le dio a SetDrawEnv, sin ambiguedad. Buscar solo por el
                     * desplazamiento era ambiguo (el front end deja entradas viejas con el
                     * mismo) y en la referencia daba una respuesta distinta en cada
                     * ejecucion. El desplazamiento queda de respaldo, el mas reciente. */
                    e = host_draw_env(h, a, 0);
                    if (e == NULL) {
                        uint32_t jj;

                        for (jj = 0; jj < DRAW_ENVS_MAX; jj++) {
                            const DrawEnvRec *r = &h->draw_envs[jj];

                            if (r->pkt != 0u && r->ofs[0] == ofs[0] && r->ofs[1] == ofs[1] &&
                                (e == NULL || r->seq > e->seq))
                                e = r;
                        }
                    }
                    if (e != NULL)
                        overlay_clip(h, (int16_t)(e->clip[0] - ox), (int16_t)(e->clip[1] - oy),
                                     e->clip[2], e->clip[3]);
                    else
                        overlay_clip(h, 0, 0, 1024, 512);
                    h->overlay.prim_offset[0] = (int16_t)(ofs[0] - ox);
                    h->overlay.prim_offset[1] = (int16_t)(ofs[1] - oy);
                    for (k = 0; k < len; k++) {
                        uint32_t w = ps1_r32(c, a + 4u + 4u * k);

                        if ((w >> 24) == 0xE1u)
                            h->overlay.draw_mode = (uint16_t)(w & 0x1FFu);
                    }
                } else {
                    /* comandos de entorno, varios por paquete */
                    for (k = 0; k < len; k++)
                        entorno(h, ps1_r32(c, a + 4u + 4u * k));
                }
            } else {
                parse_prim(h, c, a + 4u, len);
            }
        }
        if (next == 0xFFFFFFu)
            break;
        a = 0x80000000u | next;
    }
}

/* La frontera de frame en carrera: de las tablas grabadas, las de las vistas de jugador
 * (la del mundo) se tiran y el resto es el HUD. Luego la lista entera se da la vuelta,
 * porque se guarda con lo ultimo dibujado primero. */
void overlay_race_compose(GameHost *h)
{
    Overlay *o = &h->overlay;
    uint32_t n = h->hud_lists, i;

    if (n >= 2u) {
        uint32_t vistas = h->views_n;
        uint32_t desde, hasta;

        if (vistas < 1u)
            vistas = 1u;
        if (vistas > n - 1u)
            vistas = n - 1u;
        desde = h->hud_start[0];
        hasta = h->hud_start[vistas];
        if (hasta > desde && hasta <= o->n) {
            memmove(&o->quad[desde], &o->quad[hasta],
                    (size_t)(o->n - hasta) * sizeof(o->quad[0]));
            o->n -= hasta - desde;
        }
    } else if (n == 1u) {
        /* Una sola tabla: la referencia acumula aqui el menu de una partida pausada sobre
         * el ultimo HUD completo. Todavia no esta portado; se cuenta para saber si pasa. */
        h->hud_single++;
    }
    for (i = 0; i < o->n / 2u; i++) {
        OverlayQuad t = o->quad[i];

        o->quad[i] = o->quad[o->n - 1u - i];
        o->quad[o->n - 1u - i] = t;
    }
    h->hud_lists = 0;
}

/* Recorre la lista del frame y le dice a la cache que texturas hacen falta; luego
 * convierte lo que no este vigente. Sin buffer de texeles no hace nada, que es el caso
 * del host de PC cuando solo se quiere grabar. */
static void necesita(GameHost *h, const OverlayQuad *q, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++)
        if ((q[i].kind == OVL_QUAD || q[i].kind == OVL_HUD_QUAD) && q[i].has_tex)
            tex_need(&h->texcache, q[i].tex.tpage, q[i].tex.clut, q[i].tex.uv);
}

void host_build_textures(GameHost *h)
{
    uint32_t i;

    if (h->tex_buf == NULL || h->tex_cap == 0u)
        return;
    tex_begin_frame(&h->texcache);
    necesita(h, h->overlay.quad, h->overlay.n);
    /* y las de los destellos, el flare del sol y el clima de cada vista de la carrera */
    for (i = 0; i < VIEWS_MAX; i++) {
        const ViewCap *vc = &h->cap[i];

        necesita(h, vc->glow_quad, vc->glow_quads_n);
        necesita(h, vc->sky_quad, vc->sky_quads_n);
        necesita(h, vc->weather_quad, vc->weather_quads_n);
    }
    necesita(h, h->sroom_floor, h->sroom_floor_n);
    /* y el mapa A del reflejo de los coches del showroom: 128 x 65 texeles desde su esquina,
     * como lo lee la referencia (FeScene::build) */
    for (i = 0; i < h->sroom_n && i < SROOM_MAX; i++) {
        const CarDraw *d = &h->sroom[i].d;
        uint16_t u0 = (uint16_t)(d->env_a[0] & 0xFFu), v0 = (uint16_t)((d->env_a[0] >> 8) & 0xFFu);
        uint16_t u1 = (uint16_t)(u0 + 127u > 255u ? 255u : u0 + 127u);
        uint16_t v1 = (uint16_t)(v0 + 64u > 255u ? 255u : v0 + 64u);
        uint16_t uv[4][2];

        if (!d->env_ok || d->env_a[0] == 0u)
            continue;
        uv[0][0] = u0; uv[0][1] = v0;
        uv[1][0] = u1; uv[1][1] = v0;
        uv[2][0] = u0; uv[2][1] = v1;
        uv[3][0] = u1; uv[3][1] = v1;
        tex_need(&h->texcache, (uint16_t)(d->env_a[1] >> 16), (uint16_t)(d->env_a[0] >> 16), uv);
    }
    tex_build(&h->texcache, h->vram, h->tex_buf, h->tex_cap);
}

#if PS1_DIAG
/* ---- volcado para el oraculo ----
 *
 * Mismo formato que escribe la referencia con NFSHS_DIFF_OVERLAY (src/dev/mod.rs), todo
 * little-endian:
 *   cabecera: "NFSO" u32 version=1
 *   por frame: u32 frame, u32 cuadros, u32 otros_comandos, y por cuadro
 *              i16 pts[4][2], u32 colores[4], u8 hay_textura, u8 semi, u8 abr,
 *              u8 relleno, u16 tpage, u16 clut, u16 uv[4][2]
 *
 * "otros_comandos" es lo que la referencia tiene y el port todavia no produce (texto,
 * cajas del HUD, concesionario, videos): en el port siempre es 0, y la comparacion lo
 * usa para decir cuanto falta. */
static void put16(FILE *f, uint16_t v)
{
    fputc((int)(v & 0xFFu), f);
    fputc((int)((v >> 8) & 0xFFu), f);
}

static void put32o(FILE *f, uint32_t v)
{
    put16(f, (uint16_t)v);
    put16(f, (uint16_t)(v >> 16));
}

/* Un cuadro en el formato del volcado (56 bytes); lo usa tambien el de la vista. */
void overlay_dump_quad(FILE *f, const OverlayQuad *q)
{
    uint32_t k;

    for (k = 0; k < 4u; k++) {
        put16(f, (uint16_t)q->pts[k][0]);
        put16(f, (uint16_t)q->pts[k][1]);
    }
    for (k = 0; k < 4u; k++)
        put32o(f, q->colours[k]);
    fputc(q->kind, f);
    fputc(q->has_tex, f);
    fputc(q->semi, f);
    fputc(q->abr, f);
    put16(f, q->has_tex ? q->tex.tpage : 0u);
    put16(f, q->has_tex ? q->tex.clut : 0u);
    for (k = 0; k < 4u; k++) {
        put16(f, q->has_tex ? q->tex.uv[k][0] : 0u);
        put16(f, q->has_tex ? q->tex.uv[k][1] : 0u);
    }
}

int overlay_dump_open(GameHost *h, const char *path)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL)
        return 0;
    fwrite("NFSO", 1, 4, f);
    put32o(f, 3u);   /* version 2: los coches y el suelo del showroom; 3: la pausa */
    h->ovl_dump = f;
    return 1;
}

void overlay_dump_frame(GameHost *h)
{
    FILE *f = h->ovl_dump;
    uint32_t i;

    if (f == NULL)
        return;
    put32o(f, (uint32_t)h->frames);
    {
        uint32_t nq = 0;

        for (i = 0; i < h->overlay.n; i++)
            nq += (h->overlay.quad[i].kind <= OVL_CLIP);
        put32o(f, nq);
    }
    /* los "otros comandos": de momento solo los de la pausa (ver OVL_HUD_CMD) */
    {
        uint32_t no = 0;

        for (i = 0; i < h->overlay.n; i++)
            no += (h->overlay.quad[i].kind == OVL_HUD_CMD);
        put32o(f, no);
    }
    for (i = 0; i < h->overlay.n; i++)
        if (h->overlay.quad[i].kind <= OVL_CLIP)
            overlay_dump_quad(f, &h->overlay.quad[i]);
    /* Despues de los cuadros, el showroom: cada coche y cada suelo con la posicion que
     * ocupa en la lista (cuantos cuadros y recortes van antes). */
    {
        uint32_t antes = 0, ncar = 0, nsuelo = 0, k;

        for (i = 0; i < h->overlay.n; i++) {
            uint8_t kd = h->overlay.quad[i].kind;

            ncar += (kd == OVL_SR_CAR);
            nsuelo += (kd == OVL_SR_FLOOR);
        }
        put32o(f, ncar);
        for (i = 0; i < h->overlay.n; i++) {
            const OverlayQuad *q = &h->overlay.quad[i];

            if (q->kind <= OVL_CLIP) {
                antes++;
                continue;
            }
            if (q->kind != OVL_SR_CAR)
                continue;
            {
                const ShowroomCar *s = &h->sroom[(uint16_t)q->pts[0][0]];
                const CarDraw *d = &s->d;

                put32o(f, antes);
                fwrite(d->code, 1, 4, f);
                fputc(s->colour, f);
                fputc(s->mirrored, f);
                put16(f, 0);
                put32o(f, s->level);
                for (k = 0; k < 3u; k++)
                    put32o(f, (uint32_t)d->pos[k]);
                for (k = 0; k < 9u; k++)
                    put32o(f, (uint32_t)d->orient[k]);
                for (k = 0; k < 3u; k++)
                    put32o(f, (uint32_t)d->dimension[k]);
                fwrite(d->parts, 1, 57, f);
                fputc(0, f); fputc(0, f); fputc(0, f);
                for (k = 0; k < 3u; k++)
                    put32o(f, (uint32_t)s->cam_pos[k]);
                for (k = 0; k < 9u; k++)
                    put32o(f, (uint32_t)s->cam_rot[k]);
                put32o(f, s->h);
                put32o(f, (uint32_t)s->ofx);
                put32o(f, (uint32_t)s->ofy);
                fputc(d->env_ok, f);
                fputc(0, f); fputc(0, f); fputc(0, f);
                for (k = 0; k < 4u; k++)
                    put32o(f, (d->env_ok && d->env_a[0] != 0u) ? d->env_a[k] : 0u);
                for (k = 0; k < 4u; k++)
                    put32o(f, (d->env_ok && d->env_b[0] != 0u) ? d->env_b[k] : 0u);
                put32o(f, d->env_ok ? d->env_scroll : 0u);
                put32o(f, d->env_ok ? d->env_colour : 0u);
                put16(f, d->env_ok ? (uint16_t)d->env_yaw : 0u);
                put16(f, 0);
            }
        }
        put32o(f, nsuelo);
        antes = 0;
        for (i = 0; i < h->overlay.n; i++) {
            const OverlayQuad *q = &h->overlay.quad[i];

            if (q->kind <= OVL_CLIP) {
                antes++;
                continue;
            }
            if (q->kind != OVL_SR_FLOOR)
                continue;
            put32o(f, antes);
            put32o(f, (uint16_t)q->pts[0][1]);
            for (k = 0; k < (uint16_t)q->pts[0][1]; k++)
                overlay_dump_quad(f, &h->sroom_floor[(uint16_t)q->pts[0][0] + k]);
        }
        /* Version 3: los comandos de la pausa, cada uno con su posicion en la lista y sus
         * parametros (120 bytes; el texto, hasta HUD_TEXT_MAX y con ceros detras). */
        {
            uint32_t no = 0;

            for (i = 0; i < h->overlay.n; i++)
                no += (h->overlay.quad[i].kind == OVL_HUD_CMD);
            put32o(f, no);
        }
        antes = 0;
        for (i = 0; i < h->overlay.n; i++) {
            const OverlayQuad *q = &h->overlay.quad[i];
            const HudCmd *m;

            if (q->kind <= OVL_CLIP) {
                antes++;
                continue;
            }
            if (q->kind != OVL_HUD_CMD)
                continue;
            m = &h->hud_cmd[(uint16_t)q->pts[0][0]];
            put32o(f, antes);
            fputc(m->kind, f);
            fputc(m->semi, f);
            fputc(m->shape, f);
            fputc(m->pal, f);
            fputc(m->space, f);
            fputc(m->len, f);
            fputc((uint8_t)m->dx0, f);
            fputc((uint8_t)m->dx1, f);
            put16(f, (uint16_t)m->x);
            put16(f, (uint16_t)m->y);
            put16(f, (uint16_t)m->w);
            put16(f, (uint16_t)m->h);
            put32o(f, m->colour);
            fwrite(m->text, 1, HUD_TEXT_MAX, f);
        }
    }
}

void overlay_dump_close(GameHost *h)
{
    if (h->ovl_dump != NULL) {
        fclose(h->ovl_dump);
        h->ovl_dump = NULL;
    }
}
#endif /* PS1_DIAG */

/* ---- el texto del front end ----
 *
 * Font_TextXY no produce un comando de texto en el front end: produce **un cuadro por
 * caracter**, recortado de la imagen de 4 bits de la fuente. (El comando de texto es la
 * otra rama, la de una carrera en pausa, y esa es del paso 5.)
 *
 * Hay dos rutinas que pintan glifos: la normal, semitransparente y en la paleta y color
 * actuales, y la del reflejo en el suelo del concesionario, que va opaca, volteada y con
 * paleta y color propios.
 */
static uint16_t glyph_code(Cpu *c, uint32_t a)
{
    return (uint16_t)(ps1_r8(c, a) | (ps1_r8(c, a + 1u) << 8));
}

void overlay_font_quads(GameHost *h, Cpu *c, uint32_t text, int32_t x, int32_t y)
{
    uint32_t blitter = ps1_r32(c, ADDR_FONT_BLITTER);
    int upside = (blitter == ADDR_FONT_UPSIDE_BLIT);
    uint32_t table = ps1_r32(c, ADDR_FONT_TABLE);
    uint32_t count = ps1_r32(c, ADDR_FONT_COUNT);
    uint32_t shape = ps1_r32(c, ADDR_FONT_SHAPE);
    uint32_t pos, i;
    uint16_t fx, fy, tpage, clut;
    uint32_t colour;
    int32_t space;

    if (table == 0u || shape == 0u)
        return;
    pos = ps1_r32(c, shape + 12u);
    fx = (uint16_t)(pos & 0xFFFu);
    fy = (uint16_t)((pos >> 16) & 0xFFFu);
    tpage = (uint16_t)((fx / 64u) | ((fy / 256u) << 4));
    if (upside) {
        clut = (uint16_t)ps1_r16(c, ADDR_FONT_UP_CLUT);
        colour = ps1_r32(c, ADDR_FONT_UP_COLOUR) & 0xFFFFFFu;
    } else {
        clut = (uint16_t)ps1_r16(c, ADDR_FONT_CURRENT_CLUT);
        colour = ps1_r32(c, ADDR_FONT_COLOUR) & 0xFFFFFFu;
    }
    space = (int32_t)ps1_r8(c, ADDR_FONT_SPACE);

    for (i = 0; i < 256u; i++) {
        uint16_t ch = (uint16_t)ps1_r8(c, text + i);
        uint32_t k, g = 0;
        int encontrado = 0;
        int32_t w, hh, u, v, adv, dx, dy;
        uint32_t colours[4];
        QuadTex tex;
        int16_t pts[4][2];
        uint32_t j;

        if (ch == 0u)
            break;
        if (ch < 33u) {                  /* espacio y controles: solo avanzan */
            x += space;
            continue;
        }
        k = (uint32_t)ch - 32u;
        /* el glifo suele estar en su sitio por indice; si no, se busca */
        if (k < count && glyph_code(c, table + 11u * k) == ch) {
            g = table + 11u * k;
            encontrado = 1;
        } else {
            for (j = 0; j < count; j++)
                if (glyph_code(c, table + 11u * j) == ch) {
                    g = table + 11u * j;
                    encontrado = 1;
                    break;
                }
        }
        if (!encontrado)
            continue;
        w   = (int32_t)ps1_r8(c, g + 2u);
        hh  = (int32_t)ps1_r8(c, g + 3u);
        u   = (int32_t)glyph_code(c, g + 4u);
        v   = (int32_t)glyph_code(c, g + 6u);
        adv = (int8_t)ps1_r8(c, g + 8u);
        dx  = (int8_t)ps1_r8(c, g + 9u);
        dy  = (int8_t)ps1_r8(c, g + 10u);

        if (w > 0 && hh > 0) {
            uint16_t u0 = (uint16_t)(u & 0xFF), v0, u1, v1;

            for (j = 0; j < 4u; j++)
                colours[j] = colour;
            tex.tpage = tpage;
            tex.clut = clut;
            if (upside) {
                /* la fila de arriba del glifo (desde un texel por encima) abajo */
                v0 = (uint16_t)(((((int32_t)fy + v) & 0xFF) - 1) & 0xFF);
            } else {
                v0 = (uint16_t)(((int32_t)fy + v) & 0xFF);
            }
            u1 = (uint16_t)(u0 + (uint16_t)w);
            v1 = (uint16_t)(v0 + (uint16_t)hh);
            tex.uv[0][0] = u0; tex.uv[0][1] = v0;
            tex.uv[1][0] = u1; tex.uv[1][1] = v0;
            tex.uv[2][0] = u0; tex.uv[2][1] = v1;
            tex.uv[3][0] = u1; tex.uv[3][1] = v1;
            if (upside) {
                int32_t top = y + 5 - hh - dy, bottom = y + 5 - dy;
                int32_t l = x + dx, r = x + dx + w;

                pts[0][0] = (int16_t)l; pts[0][1] = (int16_t)bottom;
                pts[1][0] = (int16_t)r; pts[1][1] = (int16_t)bottom;
                pts[2][0] = (int16_t)l; pts[2][1] = (int16_t)top;
                pts[3][0] = (int16_t)r; pts[3][1] = (int16_t)top;
                overlay_push(h, pts, colours, &tex, 0, 0u);
            } else {
                rect_pts(pts, x + dx, y + dy, w, hh);
                overlay_push(h, pts, colours, &tex, 1, 0u);
            }
        }
        x += adv;
    }
}

/* ---- la pausa (M2c) ----
 *
 * Los comandos que la referencia graba en una carrera en pausa (Font_TextXY y los cuatro
 * ayudantes del HUD), resueltos en cuadros como los dibuja su port del HUD (ps1hud.rs).
 * Las texturas son las del propio juego en la VRAM de PS1: las formas de HudPmx_gShapes
 * y la fuente. El dibujo recorre la lista del final al principio, asi que lo que va
 * primero se mete el ultimo: cada comando apila sus cuadros y los suelta al reves.
 */
typedef struct {
    int16_t  pts[4][2];
    uint32_t colour;
    int      con_tex, semi;
    QuadTex  tex;
    uint8_t  abr;
} Pieza;

#define PIEZAS_MAX 16u
static Pieza s_pz[PIEZAS_MAX];
static uint32_t s_pz_n;

static void pieza_plana(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t dx0, int32_t dx1,
                        uint32_t colour, int semi)
{
    Pieza *p;

    if (s_pz_n >= PIEZAS_MAX)
        return;
    p = &s_pz[s_pz_n++];
    memset(p, 0, sizeof *p);
    p->pts[0][0] = (int16_t)(x0 + dx0); p->pts[0][1] = (int16_t)y0;
    p->pts[1][0] = (int16_t)(x1 + dx1); p->pts[1][1] = (int16_t)y0;
    p->pts[2][0] = (int16_t)x0;         p->pts[2][1] = (int16_t)y1;
    p->pts[3][0] = (int16_t)x1;         p->pts[3][1] = (int16_t)y1;
    p->colour = colour & 0xFFFFFFu;
    p->semi = semi;
}

/* La forma `i` de HudPmx_gShapes en (x, y), con su textura de la VRAM (Hud_BuildGT4). */
static void pieza_forma(Cpu *c, uint32_t i, int32_t x, int32_t y, uint32_t colour, int semi)
{
    uint32_t e = ADDR_HUD_SHAPES + 20u * i;
    uint16_t u = (uint16_t)ps1_r8(c, e), v = (uint16_t)ps1_r8(c, e + 1u);
    uint16_t w = (uint16_t)ps1_r16(c, e + 16u), hh = (uint16_t)ps1_r16(c, e + 18u);
    Pieza *p;

    if (w == 0u || hh == 0u || s_pz_n >= PIEZAS_MAX)
        return;
    p = &s_pz[s_pz_n++];
    memset(p, 0, sizeof *p);
    p->pts[0][0] = (int16_t)x;       p->pts[0][1] = (int16_t)y;
    p->pts[1][0] = (int16_t)(x + w); p->pts[1][1] = (int16_t)y;
    p->pts[2][0] = (int16_t)x;       p->pts[2][1] = (int16_t)(y + hh);
    p->pts[3][0] = (int16_t)(x + w); p->pts[3][1] = (int16_t)(y + hh);
    p->colour = colour & 0xFFFFFFu;
    p->con_tex = 1;
    p->semi = semi;
    p->tex.clut = (uint16_t)ps1_r16(c, e + 2u);
    p->tex.tpage = (uint16_t)ps1_r16(c, e + 6u);
    p->tex.uv[0][0] = u;              p->tex.uv[0][1] = v;
    p->tex.uv[1][0] = (uint16_t)(u + w); p->tex.uv[1][1] = v;
    p->tex.uv[2][0] = u;              p->tex.uv[2][1] = (uint16_t)(v + hh);
    p->tex.uv[3][0] = (uint16_t)(u + w); p->tex.uv[3][1] = (uint16_t)(v + hh);
    p->abr = (uint8_t)((p->tex.tpage >> 5) & 3u);
}

/* El tamano de la esquina redondeada (la forma 17), 7 x 7 si no esta. */
static void esquina(Cpu *c, int32_t *cw, int32_t *ch)
{
    uint32_t e = ADDR_HUD_SHAPES + 20u * 17u;

    *cw = (int32_t)ps1_r16(c, e + 16u);
    *ch = (int32_t)ps1_r16(c, e + 18u);
    if (*cw == 0 || *ch == 0) {
        *cw = 7;
        *ch = 7;
    }
}

/* Hud_BlackThinBox: un marco negro de 2 pixeles con las esquinas redondeadas. */
static void pieza_marco(Cpu *c, int32_t x, int32_t y, int32_t w, int32_t hh)
{
    int32_t cw, ch;

    esquina(c, &cw, &ch);
    pieza_plana(x + cw - 2, y - 2, x + w - cw, y, 0, 0, 0u, 0);
    pieza_plana(x - 2, y + ch - 2, x, y + hh - ch + 2, 0, 0, 0u, 0);
    pieza_plana(x + w - 2, y + ch - 2, x + w, y + hh - ch + 2, 0, 0, 0u, 0);
    pieza_plana(x + cw - 2, y + hh, x + w - cw, y + hh + 2, 0, 0, 0u, 0);
    pieza_forma(c, 14u, x - 2, y - 2, 0u, 0);
    pieza_forma(c, 15u, x + w - cw, y - 2, 0u, 0);
    pieza_forma(c, 16u, x - 2, y + hh - ch + 2, 0u, 0);
    pieza_forma(c, 17u, x + w - cw, y + hh - ch + 2, 0u, 0);
}

/* Hud_RenderPauseBox: una caja roja oscura semitransparente bajo una franja negra de
 * titulo, con el marco y las esquinas y el borde de abajo negros. */
static void pieza_pausa(Cpu *c, int32_t x, int32_t y, int32_t w, int32_t hh)
{
    int32_t cw, ch;

    esquina(c, &cw, &ch);
    pieza_plana(x, y + 12, x + w, y + hh, 0, 0, 0x461414u, 1);
    pieza_plana(x, y, x + w, y + 12, 0, 0, 0u, 0);
    pieza_marco(c, x, y, w, hh);
    pieza_plana(x, y + hh - 2, x + w, y + hh, 0, 0, 0u, 0);
    pieza_forma(c, 17u, x + w - cw, y + hh - ch, 0u, 0);
    pieza_forma(c, 16u, x - 2, y + hh - ch, 0u, 0);
    pieza_forma(c, 15u, x + w - cw, y + 10, 0u, 0);
    pieza_forma(c, 14u, x - 2, y + 10, 0u, 0);
}

void overlay_hud_cmd(GameHost *h, Cpu *c, const HudCmd *cmd, uint32_t text)
{
    Overlay *o = &h->overlay;
    int16_t ofs0 = o->prim_offset[0], ofs1 = o->prim_offset[1];
    uint32_t i;

    if (h->hud_cmds_n >= HUD_CMDS_MAX || o->n >= OVERLAY_MAX) {
        o->dropped++;
        return;
    }
    h->hud_cmd[h->hud_cmds_n] = *cmd;
    /* el marcador */
    memset(&o->quad[o->n], 0, sizeof o->quad[0]);
    o->quad[o->n].kind = OVL_HUD_CMD;
    o->quad[o->n].pts[0][0] = (int16_t)h->hud_cmds_n;
    o->n++;
    h->hud_cmds_n++;

    /* y sus cuadros, en coordenadas de pantalla tal cual */
    o->prim_offset[0] = 0;
    o->prim_offset[1] = 0;
    h->ovl_hud = 1;
    if (cmd->kind == HUD_TEXT) {
        overlay_font_quads(h, c, text, cmd->x, cmd->y);
    } else {
        s_pz_n = 0;
        switch (cmd->kind) {
        case HUD_SHAPE:
            pieza_forma(c, cmd->shape, cmd->x, cmd->y, cmd->colour, 1);
            break;
        case HUD_THINBOX:
            pieza_marco(c, cmd->x, cmd->y, cmd->w, cmd->h);
            break;
        case HUD_PAUSEBOX:
            pieza_pausa(c, cmd->x, cmd->y, cmd->w, cmd->h);
            break;
        case HUD_RECT:
            pieza_plana(cmd->x, cmd->y, cmd->x + cmd->w, cmd->y + cmd->h, cmd->dx0, cmd->dx1,
                        cmd->colour, cmd->semi);
            break;
        default:
            break;
        }
        for (i = s_pz_n; i > 0u; i--) {
            const Pieza *p = &s_pz[i - 1u];
            uint32_t col[4];

            col[0] = col[1] = col[2] = col[3] = p->colour;
            overlay_push(h, p->pts, col, p->con_tex ? &p->tex : NULL, p->semi, p->abr);
        }
    }
    h->ovl_hud = 0;
    o->prim_offset[0] = ofs0;
    o->prim_offset[1] = ofs1;
}
