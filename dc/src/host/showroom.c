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
 */
#include "host.h"

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

/* RTPS: un vertice a pantalla con el GTE como esta montado. No se guarda el resultado:
 * aqui solo importa que el GTE quede en el mismo estado que en la referencia, porque sus
 * 64 registros se comparan en cada frame. Lo que se dibuja es M2a. */
static void gte_project(Cpu *c, uint32_t base, uint32_t i)
{
    uint32_t x = (uint16_t)vtx(c, base, i, 0);
    uint32_t y = (uint16_t)vtx(c, base, i, 1);
    uint32_t z = (uint16_t)vtx(c, base, i, 2);
    ps1_gte_write(c, 0, x | (y << 16));
    ps1_gte_write(c, 1, z);
    ps1_gte_command(c, 0x00180001u);
    (void)ps1_gte_read(c, 14);
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
    uint32_t view, k, i;

    if (addr != R3DCAR_SHOWROOM || h->mode != HOST_FRONTEND)
        return 0;

    view = c->r[4];
    showroom_gte(c, view);

    if (ps1_r32(c, SHOWROOM_RING_ON) != 0u) {
        for (k = 0; k < 32u; k++) {
            uint32_t a = 2u * k, b = (2u * k + 2u) % 64u;
            gte_project(c, SHOWROOM_RING, a);
            gte_project(c, SHOWROOM_RING, b);
            gte_project(c, SHOWROOM_RING, a + 1u);
            gte_project(c, SHOWROOM_RING, b + 1u);
        }
    }

    /* DrawC_SpotPrims: el centro sigue a los 32 vertices del borde */
    gte_project(c, SHOWROOM_RIM, 32u);
    for (i = 0; i < 32u; i++) {
        gte_project(c, SHOWROOM_RIM, i);
        gte_project(c, SHOWROOM_RIM, (i + 1u) % 32u);
    }

    h->showroom_floors++;
    *ret = 0;
    return 1;
}

int host_showroom_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
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
     * dibujadas con el suelo antes del coche */
    if (ok && (flag & 0x80u) == 0u) {
        c->r[4] = CAR_SHADOW_ARG;
        c->r[5] = CAR_CACHE;
        (void)host_capture_prims(h, c, DRAWC_SHADOW_PRIM_CLIP);
    }

    /* los halos de los faros: el original, con su dibujado de carroceria saltado */
    if (ok) {
        int saved = h->capturing;
        args[0] = a0; args[1] = a1; args[2] = a2; args[3] = a3;
        halo_pass = 1;
        c->inner_hook = car_facet_hook;
        h->capturing = 1;
        (void)ps1_call_nested(c, R3DCAR_INSERT_MENU2, args, 4u);
        h->capturing = saved;
        c->inner_hook = NULL;
        halo_pass = 0;
    }

    h->showroom_cars++;
    *ret = 0;
    return 1;
}
