/* Las peliculas a pantalla completa (M3). Ver movie.c. */
#ifndef PLATFORM_DC_MOVIE_H
#define PLATFORM_DC_MOVIE_H

#include <stdint.h>

struct GameHost;

/* Movie_Play: reproduce MOVIES/<name> y devuelve los botones de PS1 que la cortaron. */
uint16_t movie_play(struct GameHost *h, const char *name);

/* El mando de Dreamcast en botones de PS1 (main.c). */
uint16_t dc_read_pad(void);

#endif /* PLATFORM_DC_MOVIE_H */
