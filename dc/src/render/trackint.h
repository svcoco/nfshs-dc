/* Lo que track.c comparte con car.c: la camara resuelta, el vertice en espacio de camara
 * y el recorte por el plano cercano. No es interfaz publica del renderer; existe para no
 * duplicar la transformacion en dos sitios. */
#ifndef PS1_RENDER_TRACKINT_H
#define PS1_RENDER_TRACKINT_H

#include "track.h"
#include "vmath.h"

/* Plano cercano, en metros. Por delante de el no se puede dividir por z. */
#define NEAR 0.25f

/* La banda de guarda: el PVR no recorta y sus coordenadas de pantalla tienen un alcance
 * finito. Sin este recorte, un vertice recien cortado por el plano cercano sale a decenas de
 * miles de pixeles (la banda del horizonte tiene radio 32000: 32000 * 190 / 0,25 = 2,4e7).
 * Una pantalla de margen a cada lado es de sobra: lo que cae fuera de la pantalla no se pinta. */
#define GUARD_X0 (-640.0f)
#define GUARD_X1 1280.0f
#define GUARD_Y0 (-480.0f)
#define GUARD_Y1 960.0f

typedef struct {
    float cam[3];             /* posicion, en metros y en espacio de mundo */
    float row[3][3];          /* derecha, arriba y adelante, divididas por |adelante| */
    float focal;              /* projection, en pixeles de la pantalla de PS1 */
    float fx, fy;             /* focal * kx y focal * ky */
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

/* De espacio de camara a pantalla; z tiene que ser > 0. */
static inline void proyecta(const View *vw, const Cv *c, TrackVert *o)
{
    float inv = vm_inv(c->z);

    o->x = vw->ox + c->x * vw->fx * inv;
    o->y = vw->oy - c->y * vw->fy * inv;
    o->z = inv;               /* el PVR quiere 1/w */
    o->u = c->u;
    o->v = c->v;
    o->argb = c->argb;
}

/* Con z > 0, un punto de pantalla dentro de estos limites esta dentro de la banda de guarda. */
static inline int dentro_guarda(float x, float y)
{
    return x >= GUARD_X0 && x <= GUARD_X1 && y >= GUARD_Y0 && y <= GUARD_Y1;
}

/* Recorta un triangulo contra el plano cercano (y, a pantalla partida, contra los bordes
 * de arriba y abajo de la vista) y lo emite: de 3 a 6 vertices, un poligono convexo. */
void triangulo(const View *vw, const Cv *tri, const TrackMat *m,
               TrackEmit emit, void *user, TrackStats *st);

#endif /* PS1_RENDER_TRACKINT_H */
