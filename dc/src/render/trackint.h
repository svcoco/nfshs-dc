/* Lo que track.c comparte con car.c: la camara resuelta, el vertice en espacio de camara
 * y el recorte por el plano cercano. No es interfaz publica del renderer; existe para no
 * duplicar la transformacion en dos sitios. */
#ifndef PS1_RENDER_TRACKINT_H
#define PS1_RENDER_TRACKINT_H

#include "track.h"

typedef struct {
    float cam[3];             /* posicion, en metros y en espacio de mundo */
    float row[3][3];          /* derecha, arriba y adelante, divididas por |adelante| */
    float focal;              /* projection, en pixeles de la pantalla de PS1 */
    float ox, oy;             /* centro de la pantalla de salida */
    float kx, ky;             /* de pixeles de PS1 a pixeles de salida */
    /* M5: a pantalla partida, lo que caiga fuera de la franja y0..y1 de la vista se recorta
     * aqui, en espacio de camara, igual que el plano cercano: el PVR solo recorta a tiles de
     * 32 pixeles y la frontera entre las dos vistas cae en medio de una fila de tiles. */
    int   recorta_y;
    float y0, y1;
} View;

/* Un vertice en espacio de camara: x derecha, y arriba, z adelante (metros). */
typedef struct {
    float x, y, z;
    float u, v;               /* atlas, 0..1 */
    uint32_t argb;
} Cv;

void view_init(View *vw, const struct RaceView *v, float sw, float sh);

/* Recorta un triangulo contra el plano cercano (y, a pantalla partida, contra los bordes
 * de arriba y abajo de la vista) y lo emite: de 3 a 6 vertices, un poligono convexo. */
void triangulo(const View *vw, const Cv *tri, const TrackMat *m,
               TrackEmit emit, void *user, TrackStats *st);

#endif /* PS1_RENDER_TRACKINT_H */
