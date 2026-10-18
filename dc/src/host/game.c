/* Funciones del propio juego que el host reemplaza: los videos y, en M2, el dibujo
 * del front end y los cambios de modo.
 *
 * Portado de src/ps1/host/game.rs, solo la parte que el arranque necesita.
 */
#include "host.h"

/* Bandera que el juego pone para indicar que un video se salto. */
#define MOVIE_SKIPPED 0x80052EBCu

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
        *ret = 0;
        return 1;

    default:
        return 0;
    }
}
