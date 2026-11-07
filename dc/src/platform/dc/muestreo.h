/* M8: muestreador en consola (make dc-perfil). Ver muestreo.c. */
#ifndef NFSHS_MUESTREO_H
#define NFSHS_MUESTREO_H

#include "host.h"

#ifdef NFSHS_PERFIL
/* Una vez por frame del juego. Al llenarse las muestras enseña el informe y no vuelve. */
void muestreo_frame(GameHost *h);
#else
#define muestreo_frame(h) ((void)0)
#endif

#endif
