/* Grabacion del dibujo 2D del front end. Portado de src/ps1/host/overlay.rs. */
#include "addr.h"
#include "host.h"

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
    q->kind = OVL_QUAD;
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
        int32_t dx, dy, nx, ny, n2;

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
        /* La normal de media unidad, sin coma flotante: en SH-4 el double es caro y
         * aqui solo hace falta el signo del desplazamiento de un pixel. */
        n2 = dx * dx + dy * dy;
        if (n2 == 0) {
            nx = 0; ny = 1;
        } else if (dx * dx >= dy * dy) {
            nx = 0; ny = (dx > 0) ? 1 : -1;
        } else {
            nx = (dy > 0) ? -1 : 1; ny = 0;
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

/* Recorre la lista del frame y le dice a la cache que texturas hacen falta; luego
 * convierte lo que no este vigente. Sin buffer de texeles no hace nada, que es el caso
 * del host de PC cuando solo se quiere grabar. */
void host_build_textures(GameHost *h)
{
    uint32_t i;

    if (h->tex_buf == NULL || h->tex_cap == 0u)
        return;
    tex_begin_frame(&h->texcache);
    for (i = 0; i < h->overlay.n; i++) {
        const OverlayQuad *q = &h->overlay.quad[i];

        if (q->kind == OVL_QUAD && q->has_tex)
            tex_need(&h->texcache, q->tex.tpage, q->tex.clut, q->tex.uv);
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

int overlay_dump_open(GameHost *h, const char *path)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL)
        return 0;
    fwrite("NFSO", 1, 4, f);
    put32o(f, 1u);
    h->ovl_dump = f;
    return 1;
}

void overlay_dump_frame(GameHost *h)
{
    FILE *f = h->ovl_dump;
    uint32_t i, k;

    if (f == NULL)
        return;
    put32o(f, (uint32_t)h->frames);
    put32o(f, h->overlay.n);
    put32o(f, 0u);                       /* el port no produce otros comandos todavia */
    for (i = 0; i < h->overlay.n; i++) {
        const OverlayQuad *q = &h->overlay.quad[i];

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
