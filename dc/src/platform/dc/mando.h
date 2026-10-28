/* M5: el mando de Dreamcast como DualShock analogico (ver mando.c). */
#ifndef PLATFORM_DC_MANDO_H
#define PLATFORM_DC_MANDO_H

#include "host.h"

/* Los dos puertos: botones segun el contexto, ejes, si hay mando, y la vibracion. Una vez
 * por frame, cuando no manda un guion. */
void mando_lee(GameHost *h, Cpu *c);

/* El mando del puerto 1 en botones de PS1, con la asignacion de los menus (las peliculas). */
uint16_t dc_read_pad(void);

#endif /* PLATFORM_DC_MANDO_H */
