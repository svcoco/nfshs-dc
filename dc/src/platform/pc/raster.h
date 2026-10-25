/* Rasterizador por software del arnes de PC. Ver raster.c. */
#ifndef PC_RASTER_H
#define PC_RASTER_H

#include "host.h"   /* GameHost y Track: los tipos reales, para que el
                     * prototipo case con la definicion */

/* Dibuja la lista del frame en curso con las texturas ya convertidas y la escribe
 * como PPM. Devuelve 0 si no pudo. */
int raster_dump(const GameHost *host, const uint16_t *texbuf, const char *path,
                int ancho, int alto);

/* Dibuja la PISTA de la vista 0 (M2b paso 3) y la escribe como PPM. Es la unica forma
 * de ver si la transformacion y el recorte estan bien sin encender un Dreamcast.
 * Devuelve 0 si no pudo. */
int raster_track(const GameHost *host, const Track *track, const uint16_t *texbuf,
                 const char *path, int ancho, int alto);

#endif /* PC_RASTER_H */
