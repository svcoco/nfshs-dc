/* Funciones del propio juego que el host reemplaza: los videos y, en M2, el dibujo
 * del front end y los cambios de modo.
 *
 * Portado de src/ps1/host/game.rs, solo la parte que el arranque necesita.
 */
#include "addr.h"
#include "host.h"

/* Bandera que el juego pone para indicar que un video se salto. */
#define MOVIE_SKIPPED 0x80052EBCu

/* Render_RenderPlayerView(vista, jugador): LA PARTE DE LOGICA.
 *
 * El mundo lo dibuja el host (por eso esta en la lista HLE), pero el original hace
 * aqui dentro trabajo que NO es dibujar y del que depende la simulacion: prepara la
 * camara, decide que trozos de pista entran, y calcula el HUD (velocidad, marcha,
 * tiempos, posicion) y la deteccion de sentido contrario.
 *
 * M1a dejo esta funcion como un contador vacio dando por hecho que todo lo que hacia
 * era grabar geometria para el renderer. **No era cierto**: medido contra la
 * referencia, sin esto la carrera entra y se queda CONGELADA (0 paginas de RAM
 * cambian, 2 ticks por frame, frente a ~3 000 de la referencia). Ver docs/hitos/M1b.md.
 *
 * Lo que sigue sin portarse, a proposito, es la GRABACION: cielo y destello del sol,
 * destellos de farolas, humo de ruedas, clima, facetas y halos de los coches, y los
 * objetos del escenario. Eso es lo que M2b y M2c escriben con el PVR delante.
 */
static void race_view_logic(GameHost *h, Cpu *c, uint32_t view, uint32_t player)
{
    uint32_t args[2];
    uint32_t n;

    /* Donde el original espera encontrar la vista y el jugador. */
    ps1_w32(c, ADDR_RENDER_VIEW, view);
    ps1_w32(c, ADDR_RENDER_VIEW + 4u, player);

    /* Camara, niebla y colores del entorno: deja el GTE programado. */
    args[0] = view;
    (void)ps1_call_nested(c, ADDR_START_WORLD_VIEW, args, 1u);

    /* Preparacion de BWorld_OnyxBuildFacets: matrices y lista de trozos de pista que
     * se ven este frame. La cuenta la guarda el juego en gp + 608. */
    args[0] = ADDR_RENDER_VIEW;
    args[1] = ADDR_DCACHE;
    (void)ps1_call_nested(c, ADDR_SETUP_BUILD_MAT, args, 2u);
    args[0] = ADDR_G_WORLD_MAT;
    args[1] = ADDR_DCACHE + 0x14u;
    (void)ps1_call_nested(c, ADDR_WORLD_SETUP_MAT, args, 2u);
    args[0] = ADDR_RENDER_VIEW;
    n = ps1_call_nested(c, ADDR_CHUNK_BUILD_LIST, args, 1u);
    ps1_w32(c, ADDR_CHUNK_BUILD_COUNT, n);
    h->race_chunks = n;

    /* El HUD: no es dibujo, son los numeros del juego (velocidad en unidades de
     * pantalla, marcha, tiempos, posicion) y el aviso de sentido contrario. */
    args[0] = player;
    (void)ps1_call_nested(c, ADDR_HUD_WRONG_WAY, args, 1u);
    (void)ps1_call_nested(c, ADDR_HUD_CALC, args, 1u);
}

int host_game_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    uint32_t a0 = c->r[4];

    switch (addr) {
    case 0x8004D1C4u:
        /* Movie_Play(video): los videos los reproduce el host. En M1a no hay video
         * (los STR/MDEC son M5), asi que no se reproduce nada y se devuelve la
         * bandera de "saltado" tal como la deja el juego, que es exactamente lo que
         * devuelve la referencia. */
        h->movies_played++;
        h->last_movie = a0 & 0xFFu;
        *ret = (uint32_t)(int32_t)(int16_t)(uint16_t)ps1_r16(c, MOVIE_SKIPPED);
        return 1;
    /* ---- dibujo que el host se queda ----
     * Estas son HLE porque en la referencia las dibuja su motor. En M1a no se dibuja
     * nada, asi que solo se cuentan: la cuenta ya dice cuanto dibujo pide el front
     * end, que es dato util para dimensionar M2a. La grabacion de verdad es M2. */
    case 0x8004E74Cu:                       /* emisor de formas del front end */
        h->fe_shapes++;
        *ret = 0;
        return 1;
    case 0x8004F12Cu:                       /* PSXDrawSquare */
    case 0x8004F3B8u:                       /* PSXDrawTransSquare */
        h->fe_squares++;
        *ret = 0;
        return 1;
    case 0x800B4024u:                       /* Render_RenderPlayerView: la vista 3D */
        h->player_views++;
        race_view_logic(h, c, a0, c->r[5]);
        *ret = 0;
        return 1;

    default:
        return 0;
    }
}
