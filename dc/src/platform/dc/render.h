/* Dibujo 2D con el PVR. Ver render.c. */
#ifndef PLATFORM_DC_RENDER_H
#define PLATFORM_DC_RENDER_H

#include <stdint.h>

struct GameHost;

int render_init(void);
/* El buffer donde la cache convierte las texturas, y su tamano en texeles. */
uint16_t *render_texbuf(void);
uint32_t  render_texcap(void);
/* Dibuja la lista de dibujo del frame en curso. */
void render_frame(struct GameHost *h);
/* Para el informe. */
uint64_t render_frames(void);
uint64_t render_quads(void);

uint64_t render_tris(void);
uint64_t render_tri_frames(void);

uint64_t render_car_tris(void);

uint64_t render_fx_tris(void);

#endif /* PLATFORM_DC_RENDER_H */
