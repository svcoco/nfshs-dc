/* El showroom: el coche de la pantalla de seleccion, que la referencia dibuja ella.
 *
 * Portado de la rama 0x800B3428 de game_call (src/ps1/host/game.rs). Lo que importa para
 * M1a no es lo que se dibuja (eso es M2a) sino **lo que toca el estado de la maquina**,
 * porque el original NO corre en su sitio: lo corre el host, anidado, y por partes.
 *
 * Fue la quinta divergencia que encontro el test diferencial, y la primera dentro del
 * camino a una carrera: con `into_race.txt` los frames 1-184 salian identicos y el 185
 * divergia en 32 752 bytes, con los `gte[]` distintos por primera vez (el front end no
 * ejecuta ni una instruccion cop2). La primera divergencia por direccion caia a 58 bytes
 * de SHOWROOM_TEX.
 *
 * Lo que hace la referencia, en este orden:
 *   1. el nivel de luz en el cache del coche (a la mitad si toca el reflejo);
 *   2. DrawC_PrimStart anidado, que construye los mapas de reflejo en CAR_CACHE;
 *   3. si no es el reflejo, la sombra del suelo con capture_prims;
 *   4. una segunda llamada anidada a **si misma** con un inner_hook que salta el dibujado
 *      de la carroceria, para quedarse solo con los halos de los faros.
 *
 * Y desde M2c paso 7 tambien se GRABA lo que hay que dibujar, como la referencia: el coche
 * (OverlayCmd::ShowroomCar) con su pose, partes, camara y proyeccion; sus halos detras; la
 * sombra, aparte hasta que la recoge el suelo; y el suelo (ShowroomFloor): el anillo con
 * sus luces que giran y el foco.
 */
#include "host.h"

#include <string.h>

#define R3DCAR_INSERT_MENU2    0x800B3428u   /* R3DCar_InsertCarFacetMenuII */
#define DRAWC_PRIM_START       0x800BFAC8u
#define DRAWC_SHADOW_PRIM_CLIP 0x800C5054u
#define R3DCAR_CENTER          0x8013E674u
#define CAR_CACHE              0x1F800000u   /* el scratchpad */
#define HOST_GP                0x8013D83Cu
#define CAR_SHADOW_ARG         0x80117BA0u

#define R3DCAR_SHOWROOM        0x800B3850u   /* R3DCar_Showroom: la plataforma giratoria */
#define SHOWROOM_RING          0x8005181Cu   /* 64 vertices del anillo */
#define SHOWROOM_RIM           0x80051754u   /* 32 vertices del borde + el centro */
#define SHOWROOM_RING_ON       0x8005199Cu   /* si es 0, no hay anillo */
#define SHOWROOM_TEX           0x801222CCu   /* puntero a la textura del anillo */
#define SHOWROOM_LIGHTS        0x801207A4u   /* los colores de las luces que giran */
#define TICKS                  0x8013EF8Cu
#define RENDER_VIEW            0x80118234u
#define OBJECT_VISIBLE         0x80117B64u
#define DRAW_CAR               0x8003AC64u   /* DrawCar(carInfo): el coche que se ensena */

/* Los codigos de modelo por tipo de coche (race.rs: CAR_TYPES, y la policia desde el 22). */
static const char *const TIPOS[28] = {
    "MSLK", "BMWZ", "", "", "CMRO", "TRAM", "ADB7", "JXKR", "BMWM", "CORV", "F550", "P993",
    "F375", "DIAB", "MCLK", "MCF1", "X993", "", "XCF1", "BNS1", "BNS2", "",
    "CCPR", "", "CBMW", "CCOV", "CCMR", "CDBL"
};

/* Las seis rutinas de dibujado de la carroceria, que el paso de halos salta. */
static const uint32_t CAR_BODY_DRAWING[6] = {
    0x800C22E0u, 0x800C0AC8u, 0x800C5054u, 0x800C4C68u, 0x800C4034u, 0x800C0A08u
};

/* HALO_PASS de la referencia. Global porque inner_hook es un puntero a funcion sin
 * contexto, igual que el thread-local de alli. */
static int halo_pass;

/* inner_hook: se consulta mientras corre el host, para las llamadas de dentro de la
 * llamada anidada. En el paso de halos, el dibujado de la carroceria se da por hecho
 * (v0 = 0) y no corre: asi solo quedan los halos en el buffer de primitivas. */
static int car_facet_hook(Cpu *c, uint32_t addr)
{
    uint32_t i;
    if (!halo_pass)
        return 0;
    for (i = 0; i < 6u; i++) {
        if (CAR_BODY_DRAWING[i] == addr) {
            c->r[2] = 0;
            return 1;
        }
    }
    return 0;
}

/* Un vertice de una tabla de 3 enteros de 16 bits. */
static int32_t vtx(Cpu *c, uint32_t base, uint32_t i, uint32_t k)
{
    return (int16_t)ps1_r16(c, base + 6u * i + 2u * k);
}

/* RTPS: un vertice a pantalla con el GTE como esta montado; deja la posicion en `p` (SXY2).
 * El orden de las proyecciones importa: los 64 registros del GTE se comparan cada frame. */
static void gte_project(Cpu *c, uint32_t base, uint32_t i, int16_t p[2])
{
    uint32_t x = (uint16_t)vtx(c, base, i, 0);
    uint32_t y = (uint16_t)vtx(c, base, i, 1);
    uint32_t z = (uint16_t)vtx(c, base, i, 2);
    uint32_t sxy;

    ps1_gte_write(c, 0, x | (y << 16));
    ps1_gte_write(c, 1, z);
    ps1_gte_command(c, 0x00180001u);
    sxy = ps1_gte_read(c, 14);
    p[0] = (int16_t)sxy;
    p[1] = (int16_t)(sxy >> 16);
}

/* Un cuadro del suelo, a la tabla del suelo. */
static void suelo_cuadro(GameHost *h, const int16_t pts[4][2], uint32_t colour,
                         const QuadTex *tex)
{
    OverlayQuad *q;
    uint32_t k;

    if (h->sroom_floor_n >= SROOM_FLOOR_MAX)
        return;
    q = &h->sroom_floor[h->sroom_floor_n++];
    memset(q, 0, sizeof *q);
    q->kind = OVL_QUAD;
    for (k = 0; k < 4u; k++) {
        q->pts[k][0] = pts[k][0];
        q->pts[k][1] = pts[k][1];
        q->colours[k] = colour;
    }
    if (tex != NULL) {
        q->tex = *tex;
        q->has_tex = 1;
    }
    q->semi = 1;
    q->abr = 1;
}

/* Un marcador en la lista 2D (ver OVL_SR_CAR). */
static void marcador(GameHost *h, uint8_t kind, uint32_t indice, uint32_t n)
{
    Overlay *o = &h->overlay;
    OverlayQuad *q;

    if (o->n >= OVERLAY_MAX) {
        o->dropped++;
        return;
    }
    q = &o->quad[o->n++];
    memset(q, 0, sizeof *q);
    q->kind = kind;
    q->pts[0][0] = (int16_t)indice;
    q->pts[0][1] = (int16_t)n;
}

/* La camara de la vista en el GTE: su matriz transpuesta con la y volteada, como la monta
 * el juego. Portado de GameHost::showroom_gte. */
static void showroom_gte(Cpu *c, uint32_t view)
{
    int32_t m[9], t[3];
    uint32_t i;
    for (i = 0; i < 9u; i++)
        m[i] = (int16_t)((int32_t)ps1_r32(c, view + 68u + 4u * i) >> 4);
    for (i = 0; i < 3u; i++)
        t[i] = (int32_t)ps1_r32(c, view + 56u + 4u * i) >> 8;

#define PAIR(a, b) ((uint32_t)(uint16_t)(a) | ((uint32_t)(uint16_t)(b) << 16))
    /* filas: (m0 m3 m6) / (-m1 -m4 -m7) / (m2 m5 m8) */
    ps1_gte_write(c, 32, PAIR(m[0], m[3]));
    ps1_gte_write(c, 33, PAIR(m[6], -m[1]));
    ps1_gte_write(c, 34, PAIR(-m[4], -m[7]));
    ps1_gte_write(c, 35, PAIR(m[2], m[5]));
    ps1_gte_write(c, 36, (uint32_t)(uint16_t)m[8]);
#undef PAIR
    ps1_gte_write(c, 37, (uint32_t)t[0]);
    ps1_gte_write(c, 38, (uint32_t)(-t[1]));
    ps1_gte_write(c, 39, (uint32_t)t[2]);
}

/* La plataforma giratoria: el original no corre, y el host proyecta sus vertices.
 *
 * Por que hay que replicar las proyecciones y no solo saltarse el original: los 64
 * registros del GTE entran en la comparacion por frame, asi que el estado en el que los
 * deja la referencia es parte del criterio. Son 193 RTPS en este orden: 128 del anillo
 * (32 cuadros de 4 puntos, solo si SHOWROOM_RING_ON no es cero), 1 del centro y 64 del
 * borde. Las primitivas en si son M2a. */
static int showroom_floor(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    uint32_t view, k, i, primero, spot;
    int8_t seg[32];
    int32_t t, head[2], dir[2];
    int16_t centro[2];

    if (addr != R3DCAR_SHOWROOM || h->mode != HOST_FRONTEND)
        return 0;

    /* las luces que persiguen: dos tiras de cinco segmentos desde el contador de ticks (la
     * segunda enfrente, o girando al reves); cada segmento toma la luz mas tenue que lo
     * cubre (DrawC_ShowroomPrims) */
    t = ((int32_t)ps1_r32(c, TICKS) % 256) >> 3;
    if (ps1_r32(c, HOST_GP + 4792u) == 0u) {
        head[0] = t; head[1] = (t + 16) & 31; dir[0] = -1; dir[1] = -1;
    } else {
        head[0] = t; head[1] = 32 - t; dir[0] = -1; dir[1] = 1;
    }
    memset(seg, -1, sizeof seg);
    for (k = 0; k < 2u; k++)
        for (i = 0; i < 5u; i++) {
            uint32_t s = (uint32_t)((head[k] + (int32_t)i * dir[k]) & 31);

            if ((int32_t)seg[s] < (int32_t)i)
                seg[s] = (int8_t)i;
        }

    view = c->r[4];
    showroom_gte(c, view);

    /* la sombra del coche primero (la lista se dibuja al reves: queda encima), al reves */
    primero = h->sroom_floor_n;
    for (k = h->sroom_shadow_n; k > 0u && h->sroom_floor_n < SROOM_FLOOR_MAX; k--)
        h->sroom_floor[h->sroom_floor_n++] = h->sroom_shadow[k - 1u];
    h->sroom_shadow_n = 0;

    if (ps1_r32(c, SHOWROOM_RING_ON) != 0u) {
        /* el anillo: 32 cuadros con textura, sumando (ChangeTPage(.., 1)), cada uno en el
         * color base y otra vez en el de su luz */
        uint32_t tex_at = ps1_r32(c, SHOWROOM_TEX), w[4];
        QuadTex tx;

        for (i = 0; i < 4u; i++)
            w[i] = ps1_r32(c, tex_at + 4u * i);
        tx.clut = (uint16_t)(w[0] >> 16);
        tx.tpage = (uint16_t)((((uint16_t)(w[1] >> 16)) & (uint16_t)~0x60u) | (1u << 5));
        for (i = 0; i < 4u; i++) {
            tx.uv[i][0] = (uint16_t)(w[i] & 0xFFu);
            tx.uv[i][1] = (uint16_t)((w[i] >> 8) & 0xFFu);
        }
        for (k = 0; k < 32u; k++) {
            uint32_t a = 2u * k, b = (2u * k + 2u) % 64u;
            int16_t pts[4][2];

            gte_project(c, SHOWROOM_RING, a, pts[0]);
            gte_project(c, SHOWROOM_RING, b, pts[1]);
            gte_project(c, SHOWROOM_RING, a + 1u, pts[2]);
            gte_project(c, SHOWROOM_RING, b + 1u, pts[3]);
            suelo_cuadro(h, (const int16_t (*)[2])pts, 0x300A00u, &tx);
            if (seg[k] >= 0)
                suelo_cuadro(h, (const int16_t (*)[2])pts,
                             ps1_r32(c, SHOWROOM_LIGHTS + 4u * (uint32_t)seg[k]) & 0xFFFFFFu, &tx);
        }
    }

    /* DrawC_SpotPrims: la luz en el suelo, un abanico de triangulos sumando desde el centro
     * (el nivel de luz del cache del coche) hasta un borde negro; el centro sigue a los 32
     * vertices del borde */
    spot = ps1_r32(c, CAR_CACHE + 140u) & 0xFFFFFFu;
    gte_project(c, SHOWROOM_RIM, 32u, centro);
    for (i = 0; i < 32u; i++) {
        int16_t pts[4][2];

        gte_project(c, SHOWROOM_RIM, i, pts[1]);
        gte_project(c, SHOWROOM_RIM, (i + 1u) % 32u, pts[2]);
        pts[0][0] = centro[0]; pts[0][1] = centro[1];
        pts[3][0] = pts[2][0]; pts[3][1] = pts[2][1];
        if (h->sroom_floor_n < SROOM_FLOOR_MAX) {
            OverlayQuad *q = &h->sroom_floor[h->sroom_floor_n++];
            uint32_t j;

            memset(q, 0, sizeof *q);
            q->kind = OVL_QUAD;
            for (j = 0; j < 4u; j++) {
                q->pts[j][0] = pts[j][0];
                q->pts[j][1] = pts[j][1];
            }
            q->colours[0] = spot;
            q->semi = 1;
            q->abr = 1;
        }
    }
    marcador(h, OVL_SR_FLOOR, primero, h->sroom_floor_n - primero);

    h->showroom_floors++;
    *ret = 0;
    return 1;
}

int host_showroom_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    if (addr == DRAW_CAR) {
        /* DrawCar(carInfo): el coche que ensena el showroom (tipo y pintura); el objeto del
         * coche conserva un codigo anterior. El original corre. */
        h->sroom_type = (int8_t)ps1_r8(c, c->r[4]);
        h->sroom_paint = (uint8_t)ps1_r8(c, c->r[4] + 1u);
        h->sroom_type_ok = 1;
        return 0;
    }
    if (showroom_floor(h, c, addr, ret))
        return 1;

    uint32_t a0, a1, a2, a3, flag, level, args[4];
    int ok;

    if (addr != R3DCAR_INSERT_MENU2 || h->mode != HOST_FRONTEND || h->capturing)
        return 0;

    a0 = c->r[4]; a1 = c->r[5]; a2 = c->r[6]; a3 = c->r[7];
    flag = ps1_r32(c, HOST_GP + 3532u);
    /* el nivel de luz, a la mitad para el reflejo; se deja en el cache del coche como
     * hace el original, porque la luz del suelo lo usa */
    level = ((flag & 0x80u) ? (a1 >> 1) : a1) & 0xFFu;
    ps1_w32(c, CAR_CACHE + 140u, level * 0x010101u);

    /* DrawC_PrimStart, como lo llama aqui el original: los mapas de reflejo */
    args[0] = R3DCAR_CENTER; args[1] = a0; args[2] = level; args[3] = CAR_CACHE;
    ok = (int32_t)ps1_call_nested(c, DRAWC_PRIM_START, args, 4u) >= 0;

    /* la sombra en el suelo (no para el reflejo): primitivas del original, que van
     * dibujadas con el suelo antes del coche; se guardan aparte hasta que las recoge el
     * suelo, como la referencia */
    if (ok && (flag & 0x80u) == 0u) {
        uint32_t n0 = h->overlay.n, n;

        c->r[4] = CAR_SHADOW_ARG;
        c->r[5] = CAR_CACHE;
        (void)host_capture_prims(h, c, DRAWC_SHADOW_PRIM_CLIP);
        n = h->overlay.n - n0;
        if (n > SROOM_SHADOW_MAX)
            n = SROOM_SHADOW_MAX;
        memcpy(h->sroom_shadow, &h->overlay.quad[n0], n * sizeof(OverlayQuad));
        h->sroom_shadow_n = n;
        h->overlay.n = n0;
    }

    /* los halos de los faros: el original, con su dibujado de carroceria saltado */
    {
        uint32_t ini = 0, fin = 0;

        if (ok) {
            int saved = h->capturing;

            ini = ps1_r32(c, 0x1F800004u);
            args[0] = a0; args[1] = a1; args[2] = a2; args[3] = a3;
            halo_pass = 1;
            c->inner_hook = car_facet_hook;
            h->capturing = 1;
            (void)ps1_call_nested(c, R3DCAR_INSERT_MENU2, args, 4u);
            h->capturing = saved;
            c->inner_hook = NULL;
            halo_pass = 0;
            fin = ps1_r32(c, 0x1F800004u);
        }

        /* el coche, como lo graba la referencia (ShowroomCar) */
        if (h->sroom_n < SROOM_MAX) {
            ShowroomCar *s = &h->sroom[h->sroom_n];
            CarDraw *d = &s->d;
            uint32_t i;

            memset(s, 0, sizeof *s);
            for (i = 0; i < 4u; i++)
                d->code[i] = (char)ps1_r8(c, a0 + 0x240u + i);
            /* el modelo es el elegido (el de DrawCar): el objeto guarda un codigo anterior */
            if (h->sroom_type_ok && h->sroom_type >= 0 && h->sroom_type < 28 &&
                TIPOS[h->sroom_type][0] != 0)
                memcpy(d->code, TIPOS[h->sroom_type], 4);
            for (i = 0; i < 3u; i++) {
                d->pos[i] = (int32_t)ps1_r32(c, a0 + 0xA0u + 4u * i);
                d->dimension[i] = (int32_t)ps1_r32(c, a0 + 0x134u + 4u * i);
            }
            for (i = 0; i < 9u; i++)
                d->orient[i] = (int32_t)ps1_r32(c, a0 + 0xF0u + 4u * i);
            d->wheel_rot[0] = (uint16_t)ps1_r32(c, a0 + 0xD0u);
            d->wheel_rot[1] = (uint16_t)ps1_r32(c, a0 + 0xD4u);
            for (i = 0; i < 57u; i++)
                d->parts[i] = (uint8_t)ps1_r8(c, OBJECT_VISIBLE + i);
            /* el nivel de luz hace de tono: 128 es la textura tal cual */
            d->light = level * 0x010101u;
            d->env_ok = (uint8_t)ok;
            if (ok) {
                for (i = 0; i < 4u; i++) {
                    d->env_a[i] = ps1_r32(c, CAR_CACHE + 100u + 4u * i);
                    d->env_b[i] = ps1_r32(c, CAR_CACHE + 116u + 4u * i);
                }
                d->env_scroll = ps1_r32(c, CAR_CACHE + 132u);
                d->env_colour = ps1_r32(c, CAR_CACHE + 144u) & 0xFFFFFFu;
                d->env_yaw = (int16_t)ps1_r16(c, R3DCAR_CENTER + 0x14u);
            }
            s->colour = (uint8_t)((ps1_r16(c, a0 + 0x840u) & 15u) |
                                  ((ps1_r8(c, a0 + 0x8B3u) & 3u) << 4));
            d->paint = s->colour;
            s->mirrored = (uint8_t)((ps1_r32(c, 0x8013E608u) & 0x80u) != 0u);
            s->level = level;
            for (i = 0; i < 3u; i++)
                s->cam_pos[i] = (int32_t)ps1_r32(c, RENDER_VIEW + 8u + 4u * i);
            for (i = 0; i < 9u; i++)
                s->cam_rot[i] = (int32_t)ps1_r32(c, RENDER_VIEW + 0x14u + 4u * i);
            s->h = c->gte_regs[32 + 26] & 0xFFFFu;
            s->ofx = (int32_t)c->gte_regs[32 + 24];
            s->ofy = (int32_t)c->gte_regs[32 + 25];
            marcador(h, OVL_SR_CAR, h->sroom_n, 1u);
            h->sroom_n++;
        }

        /* sus halos, detras del coche (encima al dibujar) */
        if (ok && fin > ini && fin - ini < 0x10000u)
            overlay_record_packets(h, c, ini, fin);
    }

    h->showroom_cars++;
    *ret = 0;
    return 1;
}
