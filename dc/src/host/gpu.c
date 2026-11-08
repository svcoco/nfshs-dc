/* libgpu reemplazada: el espejo de VRAM y las operaciones de imagen.
 *
 * La PS1 no se emula, asi que lo que el juego "dibuja" no se pinta: en M2a la lista de
 * dibujo pasara a grabarse para que el PVR la reproduzca. Lo que si hace falta ya es el
 * **espejo de VRAM**, porque el juego escribe texturas y paletas y luego las vuelve a
 * leer (StoreImage), y si no se guardan, lee basura.
 *
 * Portado de src/ps1/host/gpu.rs. Todas las direcciones estan en hle_funcs.txt.
 */
#include "host.h"

#include <string.h>

/* Lee un RECT del invitado: x, y, ancho y alto como cuatro enteros de 16 bits con
 * signo, con los negativos llevados a 0 (igual que Self::rect de la referencia). */
static void read_rect(Cpu *c, uint32_t p, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h)
{
    int32_t vx = (int32_t)(int16_t)(uint16_t)ps1_r16(c, p);
    int32_t vy = (int32_t)(int16_t)(uint16_t)ps1_r16(c, p + 2u);
    int32_t vw = (int32_t)(int16_t)(uint16_t)ps1_r16(c, p + 4u);
    int32_t vh = (int32_t)(int16_t)(uint16_t)ps1_r16(c, p + 6u);
    *x = (uint32_t)(vx > 0 ? vx : 0);
    *y = (uint32_t)(vy > 0 ? vy : 0);
    *w = (uint32_t)(vw > 0 ? vw : 0);
    *h = (uint32_t)(vh > 0 ? vh : 0);
}

static void vram_put(GameHost *h, uint32_t x, uint32_t y, uint16_t v)
{
    if (x < VRAM_W && y < VRAM_H)
        h->vram[y * VRAM_W + x] = v;
}

static uint16_t vram_get(const GameHost *h, uint32_t x, uint32_t y)
{
    if (x < VRAM_W && y < VRAM_H)
        return h->vram[y * VRAM_W + x];
    return 0;
}

/* M8: las operaciones de imagen van por filas. Cuantos pixeles de una fila que empieza en x
 * y mide w caen dentro de la VRAM (los de fuera se ignoran, como en vram_put). */
static uint32_t dentro_x(uint32_t x, uint32_t w)
{
    if (x >= VRAM_W)
        return 0;
    return w < VRAM_W - x ? w : VRAM_W - x;
}

/* Un puntero a `bytes` de la RAM del invitado a partir de p, si estan seguidos, alineados a 2
 * y sin efectos (ni scratchpad ni IO); si no, NULL y se va por ps1_r16/ps1_w16. */
static uint8_t *ram_seguida(Cpu *c, uint32_t p, uint32_t bytes)
{
    uint32_t o = p & PS1_RAM_MASK;

    if ((p & (0x1F800000u | 1u)) != 0u || bytes > PS1_RAM_SIZE - o)
        return NULL;
    return c->ram + o;
}

/* M8: las ultimas tablas que limpio ClearOTagR con 1 500 entradas o mas. DrawOTag empieza por
 * la ultima entrada (ot + (n - 1) * 4), y la cadena pasa por todas las entradas: el juego mete
 * sus paquetes entre ellas sin romperla. Asi que si DrawOTag empieza en una de estas, la lista
 * tiene al menos n enlaces y no hace falta recorrerla para saberlo (overlay_ot_len). */
#define OT_LARGAS 4u
static uint32_t s_ot_larga[OT_LARGAS];
static uint32_t s_ot_larga_i;

static int ot_larga(uint32_t inicio)
{
    uint32_t k;

    for (k = 0; k < OT_LARGAS; k++)
        if (s_ot_larga[k] == inicio)
            return 1;
    return 0;
}

int host_gpu_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    uint32_t a0 = c->r[4], a1 = c->r[5], a2 = c->r[6], a3 = c->r[7];
    uint32_t x, y, w, hh, i, j;

    switch (addr) {
    case 0x800EDFCCu:                       /* ResetGraph */
    case 0x800EE140u:                       /* SetDispMask */
    case 0x800EE1D8u:                       /* DrawSync */
    case 0x800F0318u:                       /* DrawSyncInternal */
    case 0x801053B8u:                       /* GPU_cw */
        *ret = 0;
        return 1;
    case 0x800F24B8u:                       /* GetVideoMode: NTSC */
        *ret = 0;
        return 1;

    case 0x800EE35Cu: {                     /* ClearImage(rect, r, g, b) */
        /* el color de 24 bits se empaqueta a los 15 bits de la VRAM */
        uint16_t col = (uint16_t)(((a1 >> 3) & 31u)
                                  | (((a2 >> 3) & 31u) << 5)
                                  | (((a3 >> 3) & 31u) << 10));
        uint32_t wd;

        read_rect(c, a0, &x, &y, &w, &hh);
        wd = dentro_x(x, w);
        for (j = 0; j < hh && y + j < VRAM_H; j++) {
            uint16_t *d = &h->vram[(y + j) * VRAM_W + x];

            for (i = 0; i < wd; i++)
                d[i] = col;
        }
        h->vram_writes++;
        /* lo reescrito tumba las texturas convertidas que lo usaban */
        tex_dirty(&h->texcache, x, y, w, hh);
        *ret = 0;
        return 1;
    }
    case 0x800EE3ECu: {                     /* LoadImage(rect, datos) */
        uint32_t p = a1;
        const uint8_t *src;

        read_rect(c, a0, &x, &y, &w, &hh);
        src = ram_seguida(c, p, w * hh * 2u);
        if (src != NULL) {
            uint32_t wd = dentro_x(x, w);

            for (j = 0; j < hh && y + j < VRAM_H; j++)
                memcpy(&h->vram[(y + j) * VRAM_W + x], src + (size_t)j * w * 2u, wd * 2u);
        } else {
            for (j = 0; j < hh; j++)
                for (i = 0; i < w; i++) {
                    vram_put(h, x + i, y + j, (uint16_t)ps1_r16(c, p));
                    p += 2u;
                }
        }
        h->vram_writes++;
        tex_dirty(&h->texcache, x, y, w, hh);
        *ret = 0;
        return 1;
    }
    case 0x800EE44Cu: {                     /* StoreImage(rect, datos) */
        uint32_t p = a1;
        uint8_t *dst;

        read_rect(c, a0, &x, &y, &w, &hh);
        dst = ram_seguida(c, p, w * hh * 2u);
        if (dst != NULL) {
            uint32_t wd = dentro_x(x, w);

            /* lo de fuera de la VRAM se lee como 0 (vram_get) */
            for (j = 0; j < hh; j++) {
                uint8_t *fila = dst + (size_t)j * w * 2u;

                if (y + j < VRAM_H) {
                    memcpy(fila, &h->vram[(y + j) * VRAM_W + x], wd * 2u);
                    memset(fila + wd * 2u, 0, (w - wd) * 2u);
                } else {
                    memset(fila, 0, w * 2u);
                }
            }
        } else {
            for (j = 0; j < hh; j++)
                for (i = 0; i < w; i++) {
                    ps1_w16(c, p, vram_get(h, x + i, y + j));
                    p += 2u;
                }
        }
        h->vram_reads++;
        *ret = 0;
        return 1;
    }
    case 0x800EE4ACu: {                     /* MoveImage(rect, x, y) */
        /* se lee todo el origen antes de escribir, para que un solape no se pise */
        uint32_t n, k;
        uint16_t *tmp;
        read_rect(c, a0, &x, &y, &w, &hh);
        n = w * hh;
        if (n == 0u) {
            *ret = 0;
            return 1;
        }
        if (n > VRAM_W * VRAM_H)
            ps1_panic(c, "MoveImage mas grande que la VRAM");
        tmp = h->vram_tmp;
        {
            uint32_t wd = dentro_x(x, w);

            for (j = 0; j < hh; j++) {
                uint16_t *fila = tmp + j * w;

                if (y + j < VRAM_H) {
                    memcpy(fila, &h->vram[(y + j) * VRAM_W + x], wd * 2u);
                    memset(fila + wd, 0, (w - wd) * 2u);
                } else {
                    memset(fila, 0, w * 2u);
                }
            }
            wd = dentro_x(a1, w);
            for (j = 0; j < hh && a2 + j < VRAM_H; j++)
                memcpy(&h->vram[(a2 + j) * VRAM_W + a1], tmp + j * w, wd * 2u);
        }
        (void)k;
        h->vram_writes++;
        /* MoveImage escribe en el DESTINO, que es (a1, a2) */
        tex_dirty(&h->texcache, a1, a2, w, hh);
        *ret = 0;
        return 1;
    }
    case 0x800EE564u: {                     /* ClearOTagR(ot, n) */
        /* Cada entrada enlaza con la ANTERIOR y la primera termina la lista: es la
         * lista de dibujo invertida de libgpu. El juego la recorre, asi que hay que
         * construirla de verdad en la RAM del invitado. */
        ps1_u32a *ot;

        if (a1 >= 1500u) {
            s_ot_larga[s_ot_larga_i] = (a0 + (a1 - 1u) * 4u) | 1u;   /* | 1: nunca es 0 */
            s_ot_larga_i = (s_ot_larga_i + 1u) % OT_LARGAS;
        }
        ot = (a0 & 3u) == 0u ? (ps1_u32a *)(void *)ram_seguida(c, a0, a1 * 4u) : NULL;

        if (ot != NULL) {
            if (a1 > 0u)
                ot[0] = 0x00FFFFFFu;
            for (i = 1; i < a1; i++)
                ot[i] = (a0 + (i - 1u) * 4u) & 0x00FFFFFFu;
        } else {
            for (i = 0; i < a1; i++) {
                uint32_t v = (i == 0u) ? 0x00FFFFFFu : ((a0 + (i - 1u) * 4u) & 0x00FFFFFFu);
                ps1_w32(c, a0 + i * 4u, v);
            }
        }
        *ret = a0;
        return 1;
    }
    case 0x800EE610u:                       /* DrawOTag */
        /* El dibujo lo hace el motor del host. En carrera, las tablas que van despues
         * de la del mundo (HUD, tacometro, mapa) se graban como 2D (M2c paso 5). La del
         * mundo, la primera y con miles de paquetes, solo se cuenta: su contenido lo
         * dibuja el renderer desde los assets. */
        h->otags++;
        if (h->mode == HOST_RACE && ps1_r32(c, SIM_PAUSED) == 0u) {
            if (h->hud_lists < HUD_LISTS_MAX)
                h->hud_start[h->hud_lists++] = h->overlay.n;
            if (h->otags_frame > 0u ||
                (!ot_larga(a0 | 1u) && overlay_ot_len(c, a0, 1500u) < 1500u))
                overlay_record_ot(h, c, a0);
            h->hud_end = h->overlay.n;
            h->otags_frame++;
        }
        *ret = 0;
        return 1;
    case 0x800EE680u:                       /* PutDrawEnv(env) */
        /* Un entorno a pantalla completa es el buffer de dibujo del frame; se anota su
         * esquina, que en M2a hara falta para situar lo que se graba.
         * (las vistas de pantalla partida son de ancho completo y media altura) */
        if (ps1_r16(c, a0 + 4u) >= 256u && ps1_r16(c, a0 + 6u) >= 200u) {
            h->draw_origin[0] = (int32_t)ps1_r16(c, a0);
            h->draw_origin[1] = (int32_t)ps1_r16(c, a0 + 2u);
        }
        *ret = a0;
        return 1;
    case 0x800EE740u:                       /* PutDispEnv(env) */
        h->display[0] = (uint16_t)ps1_r16(c, a0);
        h->display[1] = (uint16_t)ps1_r16(c, a0 + 2u);
        h->display[2] = (uint16_t)ps1_r16(c, a0 + 4u);
        h->display[3] = (uint16_t)ps1_r16(c, a0 + 6u);
        h->flips++;
        /* El flip del motor es una frontera de frame **solo en el front end** (igual que
         * src/ps1/host/gpu.rs, que lo condiciona a Mode::FrontEnd). En carrera la
         * frontera es el systemtask del tope del bucle principal, y contar tambien el
         * flip hacia dos frames por cada uno: el port alternaba flip y systemtask desde
         * el frame 232 mientras la referencia se quedaba en systemtask. No se veia antes
         * porque el port no tenia concepto de modo hasta que lo trajo views.c. */
        if (h->mode == HOST_FRONTEND && c->r[31] == RA_FLIP_PUT_DISP_ENV)
            host_frame_top(h, c);
        *ret = a0;
        return 1;
    default:
        return 0;
    }
}
