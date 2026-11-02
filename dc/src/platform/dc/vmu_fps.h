/* M7: contador de fps en la pantalla de la VMU (ver vmu_fps.c). */
#ifndef PLATFORM_DC_VMU_FPS_H
#define PLATFORM_DC_VMU_FPS_H

#include "host.h"

/* Lanza el hilo que pinta los fps de host->frames en las VMU con pantalla. */
void vmu_fps_init(const GameHost *h);

/* Checkpoint que se muestra a la izquierda de los fps. Si el juego se cuelga, es el ultimo
 * que llego a marcar. */
enum { PUNTO_ARRANQUE = 0, PUNTO_PELICULA = 1, PUNTO_MENUS = 2, PUNTO_CARRERA = 3 };
void vmu_fps_punto(unsigned n);

#endif /* PLATFORM_DC_VMU_FPS_H */
