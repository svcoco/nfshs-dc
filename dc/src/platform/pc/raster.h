/* Rasterizador por software del arnes de PC. Ver raster.c. */
#ifndef PC_RASTER_H
#define PC_RASTER_H

#include <stdint.h>

struct GameHost;

/* Dibuja la lista del frame en curso con las texturas ya convertidas y la escribe
 * como PPM. Devuelve 0 si no pudo. */
int raster_dump(const struct GameHost *host, const uint16_t *texbuf, const char *path,
                int ancho, int alto);

#endif /* PC_RASTER_H */
