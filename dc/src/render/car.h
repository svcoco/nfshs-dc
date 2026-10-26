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
    /* Reflejo (DrawC_Prim, camino de entorno): cuantos triangulos (0 si la parte no
     * refleja), cuantos vertices tiene la parte en el GEO, y sus registros de 28 bytes en
     * el .CAR (ver bakecar.rs). */
    uint32_t env_tris, env_geo;
    const uint8_t *env_rec;
} CarPart;

typedef struct {
    uint8_t        *raw;
    uint32_t        size;
    char            code[8];
    CarPart        *part;
    const float    *vert;        /* 6 floats por vertice: x y z u v, y el color en el 6 */
    const uint16_t *indices;     /* atlas_w * atlas_h: la serie de color de cada texel */
    const uint16_t *series;      /* series_n * 64: el color ARGB1555 de cada serie por pintura */
    uint32_t        series_n;
    uint16_t        parts, atlas_w, atlas_h;
    uint32_t        verts;
    float           half_extents[3];
} CarModel;

int  car_load(CarModel *m, const char *path);

/* La textura del coche con una pintura (CarState::colour, 0..63) en `out`, que tiene sitio para
 * atlas_w * atlas_h texeles ARGB1555. Es preparar una textura al cargar el coche o al cambiar
 * de pintura, no dibujar: el resultado es el que da el cargador de la referencia con esa
 * pintura (ver bakecar.rs, version 3). */
void car_paint(const CarModel *m, uint8_t paint, uint16_t *out);
void car_free(CarModel *m);

/* Las pasadas de un coche. Cada una va con su propia mezcla, asi que el que llama pone el
 * contexto del PVR antes. */
#define CAR_PASS_BODY     0   /* el coche, sin las partes marcadas `subtract` */
#define CAR_PASS_SUBTRACT 1   /* solo esas: la sombra de los pasos de rueda, que resta */
#define CAR_PASS_ENV_A    2   /* el reflejo, mapa A (sobre los mapas de la pista) */
#define CAR_PASS_ENV_B    3   /* el reflejo, mapa B: desplazado, en los poligonos 0x3F3 */

/* Dibuja un coche. `m` es su modelo y `d` lo que el juego decidio de el este frame.
 *
 * Los reflejos son el camino de entorno de DrawC_Prim: cada poligono de las partes que
 * reflejan (0, 8, 10, 41, 42, 45 y 46), solo los que miran a la camara, otra vez con un
 * mapa de reflejo de la pista. Las coordenadas salen de las normales: las del mapa A,
 * girando la normal por la guinada de la camara alrededor del coche, u = u0 + 64 + x y
 * v = v0 + |z|; las del mapa B, de la normal tal cual, u = u0 + 64 + y y
 * v = v0 + desplazamiento + z, dentro de la ventana de textura de 128 x 64 del mapa. Las
 * UV salen en la pagina de 256 x 256 de los mapas (bake-refl). Se acercan 0,01 m (A) o
 * 0,02 m (B) a la camara, para quedar sobre la carroceria. Las normales son las que el
 * juego tiene en memoria si el numero de vertices cuadra con el GEO; si no, las del GEO. */
void car_draw(const CarModel *m, const struct CarDraw *d, const struct RaceView *view,
              int pasada, float sw, float sh, TrackEmit emit, void *user, TrackStats *st);

/* El modo de mezcla de PS1 de un mapa de reflejo (el de su pagina de textura), o -1 si el
 * juego no le dio ese mapa al coche este frame. */
int car_env_abr(const struct CarDraw *d, int pasada);

/* Lee la pagina de mapas de reflejo de una pista (bake-refl: 256 x 256 ARGB1555) en
 * `texels`, que tiene que tener sitio para 65 536. Devuelve 0 si no pudo. */
#define CAR_REFL_SIZE 256u
int car_refl_load(uint16_t *texels, const char *path);

/* La sombra de un coche en el suelo (DrawC_ShadowPrim): la forma SHAD del atlas de la pista
 * sobre el cuadro que calculo Newton_CalcRealShadowCoordinates, con el color de la luz.
 * Se dibuja restando. No hace nada si el juego no la pide para ese coche. */
void car_shadow(const Track *t, const struct CarDraw *d, const struct RaceView *view,
                float sw, float sh, TrackEmit emit, void *user, TrackStats *st);

#endif /* PS1_RENDER_CAR_H */
