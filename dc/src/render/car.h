/* Los coches en pantalla (M2c, paso 4).
 *
 * Portable: no incluye cabeceras de KOS. Produce los mismos vertices de pantalla que
 * track.c, por el mismo sumidero, asi que el PVR y el rasterizador de PC los pintan igual.
 *
 * ---- lo que hay que saber ----
 *
 * Un modelo de coche son **partes**, indexadas exactamente como los 57 bytes de
 * `R3DCar_ObjectVisible` que el host ya graba: un valor de 1 a 16 dice que esa parte se
 * dibuja, y de 17 en adelante es un halo de luz, que es 2D y va por otro lado.
 *
 * Sus poligonos son **triangulos**, no cuadros como la pista, y sus vertices vienen del
 * horneado ya en unidades de mundo y con las UV en su atlas.
 *
 * Las **ruedas** giran alrededor de su buje: las delanteras por `wheel_rot[0]` y ademas
 * viran, las traseras por `wheel_rot[1]`.
 *
 * El **origen del modelo esta a ras de suelo** y la posicion que simula el juego es el
 * centro de la caja del cuerpo, asi que hay que bajar el coche por la media extension en
 * Y de esa caja.
 */
#ifndef PS1_RENDER_CAR_H
#define PS1_RENDER_CAR_H

#include "track.h"

struct CarDraw;

typedef struct {
    uint32_t vert_off, vert_n;   /* en vertices; vert_n es multiplo de 3 */
    uint8_t  wheel;              /* 0..3, o 0xFF si no es rueda */
    uint8_t  subtract;           /* se resta (sombra del paso de rueda): no se dibuja */
    float    hub[3];
} CarPart;

typedef struct {
    uint8_t        *raw;
    uint32_t        size;
    char            code[8];
    CarPart        *part;
    const float    *vert;        /* 6 floats por vertice: x y z u v, y el color en el 6 */
    const uint16_t *atlas;
    uint16_t        parts, atlas_w, atlas_h;
    uint32_t        verts;
    float           half_extents[3];
} CarModel;

int  car_load(CarModel *m, const char *path);
void car_free(CarModel *m);

/* Dibuja un coche. `m` es su modelo y `d` lo que el juego decidio de el este frame. */
void car_draw(const CarModel *m, const struct CarDraw *d, const struct RaceView *view,
              float sw, float sh, TrackEmit emit, void *user, TrackStats *st);

#endif /* PS1_RENDER_CAR_H */
