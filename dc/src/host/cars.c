/* Grabacion de los coches de carrera (M2c, paso 3).
 *
 * El juego decide por frame **que se dibuja de cada coche**: que partes del modelo, con
 * que luz, con que sombra en el suelo y con que mapas de reflejo. No lo deja escrito en
 * ningun sitio: lo dibuja. Asi que, igual que con los objetos, el port corre
 * `R3DCar_InsertAllListFacet` **anidada** con un hook interno que sustituye
 * `R3DCar_InsertCarFacetII` y, en vez de construir primitivas, **anota** la decision.
 *
 * Lo que se anota por coche es una lista de palabras de 99, en el mismo orden que la
 * referencia (`src/ps1/host/cars.rs`), para poder compararla palabra a palabra:
 *
 *   [0]      el Car_tObj
 *   [1]      la luz de la pista en el coche (BWorldSm_QuadLight)
 *   [2]      si hay sombra
 *   [3..15)  las cuatro esquinas de la sombra, relativas a la posicion del coche en x y z
 *   [15]     el color de la sombra
 *   [16..73) los 57 bytes de R3DCar_ObjectVisible: que partes se dibujan
 *   [73]     si DrawC_PrimStart dio bueno
 *   [74..82) los mapas de reflejo del cache del coche
 *   [82..85) su color, su desplazamiento y el giro de la camara alrededor del coche
 *   [85..99) los codigos de luz y abolladura por textura
 *
 * Portado de src/ps1/host/cars.rs.
 */
#include "addr.h"
#include "host.h"

#include <string.h>

static void car_env_normals(Cpu *c, uint32_t car, CarDraw *d);

/* Las seis rutinas de dibujado de la carroceria, que el paso de halos salta. Son las
 * mismas que ya usa showroom.c. */
static const uint32_t CAR_BODY[6] = {
    0x800C22E0u, 0x800C0AC8u, 0x800C5054u, 0x800C4C68u, 0x800C4034u, 0x800C0A08u
};

/* El hook interno no lleva contexto: el destino tiene que ser global. */
static GameHost *s_host;
static int s_halo_pass;
static int s_visibility_pass;

static void anota(GameHost *h, uint32_t w)
{
    if (h->cars_cap < CAR_CAPTURE_MAX)
        h->cars_cap_buf[h->cars_cap++] = w;
    else
        h->cars_lost++;
}

static int car_hook(Cpu *c, uint32_t addr)
{
    GameHost *h = s_host;
    uint32_t car, light, pos[3], i, nivel, r, g, b;
    uint32_t args[4];
    int ok;

    if (h == NULL)
        return 0;

    if (s_halo_pass) {
        /* en el paso de halos la carroceria se da por dibujada y no corre */
        for (i = 0; i < 6u; i++)
            if (CAR_BODY[i] == addr) {
                c->r[2] = 0;
                return 1;
            }
        return 0;
    }

    /* El cono de visibilidad del original es el 4:3 de PS1. `car_cone_width` lo ensancha
     * dividiendo la fila x de la vista, que es la mejora de la referencia: con 1 queda el
     * comportamiento del original. La RAM se restaura, solo cambia lo que devuelve. */
    if (addr == ADDR_R3DCAR_VISIBILITY && !s_visibility_pass) {
        uint32_t view = c->r[5];
        uint32_t fila[3];
        uint32_t depth;

        car = c->r[4];
        for (i = 0; i < 3u; i++) {
            fila[i] = ps1_r32(c, view + 20u + 4u * i);
            ps1_w32(c, view + 20u + 4u * i,
                    (uint32_t)((int32_t)fila[i] / (int32_t)h->car_cone_width));
        }
        s_visibility_pass = 1;
        args[0] = car;
        args[1] = view;
        depth = ps1_call_nested(c, ADDR_R3DCAR_VISIBILITY, args, 2u);
        s_visibility_pass = 0;
        for (i = 0; i < 3u; i++)
            ps1_w32(c, view + 20u + 4u * i, fila[i]);
        c->r[2] = depth;
        return 1;
    }

    if (addr != ADDR_R3DCAR_INSERT_CAR_FACET_II)
        return 0;

    car = c->r[4];
    /* no se dibuja este frame: el original vuelve enseguida, y su R3DCar_ObjectVisible
     * sigue siendo el de otro coche */
    if ((int16_t)ps1_r16(c, car + ADDR_CAR_DETAIL) < 0) {
        c->r[2] = 0;
        return 1;
    }

    args[0] = car + 8u;
    light = ps1_call_nested(c, ADDR_BWORLDSM_QUAD_LIGHT, args, 1u) & 0xFFFFFFu;
    anota(h, car);
    anota(h, light);
    anota(h, ps1_r32(c, ADDR_SHADOW_ON));

    /* Newton_CalcRealShadowCoordinates dejo las esquinas en car + 488; se guardan
     * relativas a la posicion del coche en x y z, porque el coche se dibuja en una pose
     * interpolada */
    pos[0] = ps1_r32(c, car + 0xA0u);
    pos[1] = 0;
    pos[2] = ps1_r32(c, car + 0xA8u);
    for (i = 0; i < 12u; i++)
        anota(h, ps1_r32(c, car + 488u + 4u * i) - pos[i % 3u]);
    anota(h, ps1_r32(c, ADDR_SHADOW_COLOUR) & 0xFFFFFFu);
    for (i = 0; i < 57u; i++)
        anota(h, ps1_r8(c, ADDR_R3DCAR_OBJECT_VISIBLE + i));

    /* DrawC_PrimStart prepara el dibujo del coche (nivel de luz, mapas de reflejo
     * elegidos por la posicion en la pista, su color y su desplazamiento) en el cache
     * del coche, que vive en el scratchpad */
    r = light & 0xFFu;
    g = (light >> 8) & 0xFFu;
    b = (light >> 16) & 0xFFu;
    nivel = (r + g + b) / 3u;
    if (nivel < 24u)
        nivel = 24u;
    ps1_w32(c, ADDR_CAR_CACHE + 140u, nivel * 0x010101u);
    args[0] = ADDR_R3DCAR_CENTER;
    args[1] = car;
    args[2] = nivel;
    args[3] = ADDR_CAR_CACHE;
    ok = (int32_t)ps1_call_nested(c, ADDR_DRAWC_PRIM_START, args, 4u) >= 0;
    anota(h, (uint32_t)ok);
    for (i = 0; i < 8u; i++)
        anota(h, ps1_r32(c, ADDR_CAR_CACHE + 100u + 4u * i));
    anota(h, ps1_r32(c, ADDR_CAR_CACHE + 132u));
    anota(h, ps1_r32(c, ADDR_CAR_CACHE + 144u));
    anota(h, ps1_r16(c, ADDR_R3DCAR_CENTER + 0x14u));
    for (i = 0; i < 26u; i++)
        anota(h, ps1_r32(c, ADDR_LIGHT_CODES + 4u * i));

    /* Y aparte, lo que hace falta para DIBUJARLO. Va separado de la captura porque esa
     * tiene que seguir siendo palabra a palabra la de la referencia. */
    if (h->cars_n < CARS_MAX) {
        CarDraw *d = &h->car_draw[h->cars_n];

        for (i = 0; i < 4u; i++)
            d->code[i] = (char)ps1_r8(c, car + 0x240u + i);
        d->code[4] = 0;
        for (i = 0; i < 3u; i++) {
            d->pos[i] = (int32_t)ps1_r32(c, car + 0xA0u + 4u * i);
            d->dimension[i] = (int32_t)ps1_r32(c, car + 0x134u + 4u * i);
        }
        for (i = 0; i < 9u; i++)
            d->orient[i] = (int32_t)ps1_r32(c, car + 0xF0u + 4u * i);
        d->light = light;
        for (i = 0; i < 4u; i++) {
            d->env_a[i] = ps1_r32(c, ADDR_CAR_CACHE + 100u + 4u * i);
            d->env_b[i] = ps1_r32(c, ADDR_CAR_CACHE + 116u + 4u * i);
        }
        d->env_scroll = ps1_r32(c, ADDR_CAR_CACHE + 132u);
        d->env_colour = ps1_r32(c, ADDR_CAR_CACHE + 144u) & 0xFFFFFFu;
        d->env_yaw = (int16_t)ps1_r16(c, ADDR_R3DCAR_CENTER + 0x14u);
        car_env_normals(c, car, d);
        d->shadow_on = (uint8_t)(ps1_r32(c, ADDR_SHADOW_ON) != 0u);
        d->env_ok = (uint8_t)ok;
        d->parked = (uint8_t)((ps1_r32(c, car + 0x570u) & 4u) != 0u);
        for (i = 0; i < 12u; i++)
            d->shadow[i / 3u][i % 3u] = (int32_t)(ps1_r32(c, car + 488u + 4u * i) - pos[i % 3u]);
        {
            uint32_t sc = ps1_r32(c, ADDR_SHADOW_COLOUR);

            d->shadow_rgb[0] = (uint8_t)sc;
            d->shadow_rgb[1] = (uint8_t)(sc >> 8);
            d->shadow_rgb[2] = (uint8_t)(sc >> 16);
        }
        d->wheel_rot[0] = (uint16_t)ps1_r32(c, car + 0xD0u);
        d->wheel_rot[1] = (uint16_t)ps1_r32(c, car + 0xD4u);
        /* El giro de las ruedas delanteras: el original escala la direccion con la
         * velocidad por encima de 30 m/s. Copiado de CarState::wheel_steer. */
        {
            int64_t s = (int32_t)ps1_r32(c, car + 0x450u);
            int64_t vel = (int32_t)ps1_r32(c, car + 0x420u);

            if (vel > 0x1E0000) {
                int64_t den = (vel * 2184) >> 16;

                if (den != 0)
                    s = ((s << 32) / den) >> 16;
            }
            d->wheel_steer = (int32_t)(s * 32);
        }
        for (i = 0; i < 57u; i++)
            d->parts[i] = (uint8_t)ps1_r8(c, ADDR_R3DCAR_OBJECT_VISIBLE + i);
    }
    h->cars_n++;

    /* y despues el original, que anade los halos de los faros y los frenos al buffer de
     * primitivas; todo lo demas se salta. El tramo se anota y se graba al terminar todos
     * los coches (host_record_halos), como la referencia. */
    {
        uint32_t ini = ps1_r32(c, 0x1F800004u), fin;

        s_halo_pass = 1;
        args[0] = car;
        (void)ps1_call_nested(c, ADDR_R3DCAR_INSERT_CAR_FACET_II, args, 1u);
        s_halo_pass = 0;
        fin = ps1_r32(c, 0x1F800004u);
        if (fin > ini && fin - ini < 0x10000u && h->halo_rng_n < 16u) {
            h->halo_rng[h->halo_rng_n][0] = ini;
            h->halo_rng[h->halo_rng_n][1] = fin;
            h->halo_rng[h->halo_rng_n][2] = car;
            h->halo_rng_n++;
        }
    }
    c->r[2] = 0;
    return 1;
}

/* Las normales de reflejo que el juego tiene en memoria para el modelo del coche: las de
 * sus objetos de parte cargados (0x80117BC0, por nivel de detalle y ranura de modelo), que
 * NO son las del GEO (medido: 0 de 1 176 esquinas iguales, con otra escala). Solo los
 * coches de carrera tienen ranura; el trafico se queda sin ellas, como en la referencia
 * (read_env_normals de src/ps1/race.rs). */
static void car_env_normals(Cpu *c, uint32_t car, CarDraw *d)
{
    static const uint8_t pis[CAR_ENV_PARTS] = { 0, 8, 10, 41, 42, 45, 46 };
    int16_t slot = (int16_t)ps1_r16(c, car + 0x8BCu);
    uint32_t lod = ps1_r8(c, car + 0x842u) >> 7, partes, k, usadas = 0;

    memset(d->env_n, 0, sizeof d->env_n);
    if (slot < 0 || slot >= 50)
        return;
    partes = ps1_r32(c, 0x80117BC0u + lod * 200u + (uint32_t)slot * 4u);
    if (partes < 0x80000000u || partes >= 0x80200000u)
        return;
    for (k = 0; k < CAR_ENV_PARTS; k++) {
        uint32_t pa = ps1_r32(c, partes + 4u * pis[k]), nv, np, j;

        if (pa < 0x80000000u || pa >= 0x80200000u)
            continue;
        nv = ps1_r16(c, pa);
        np = ps1_r32(c, pa + 20u);
        if (nv == 0u || nv > 512u || np < 0x80000000u || np >= 0x80200000u ||
            usadas + nv > CAR_ENV_NORMALS)
            continue;
        d->env_first[k] = (uint16_t)usadas;
        d->env_n[k] = (uint16_t)nv;
        for (j = 0; j < nv; j++) {
            d->env_norm[usadas + j][0] = (int16_t)ps1_r16(c, np + 6u * j);
            d->env_norm[usadas + j][1] = (int16_t)ps1_r16(c, np + 6u * j + 2u);
            d->env_norm[usadas + j][2] = (int16_t)ps1_r16(c, np + 6u * j + 4u);
        }
        usadas += nv;
    }
}

void host_record_cars(GameHost *h, Cpu *c)
{
    uint32_t args[1];

    h->cars_cap = 0;
    h->cars_n = 0;
    h->halo_rng_n = 0;
    s_host = h;
    s_halo_pass = 0;
    s_visibility_pass = 0;
    c->inner_hook = car_hook;
    args[0] = ADDR_RENDER_VIEW;
    (void)ps1_call_nested(c, ADDR_R3DCAR_INSERT_ALL_LIST, args, 1u);
    c->inner_hook = NULL;
    s_host = NULL;
    h->cars_sum += h->cars_n;
    if (h->cars_n > h->cars_max)
        h->cars_max = h->cars_n;
}
