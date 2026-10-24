/* La pista: del .TRK horneado a cuadros en coordenadas de pantalla (M2b, paso 3).
 *
 * Portable: no incluye cabeceras de KOS. Lo que sale de aqui son vertices ya proyectados
 * y recortados, y quien los pinta lo pone la plataforma (el PVR en Dreamcast, el
 * rasterizador por software en el arnes de PC). Asi el paso 3 se puede MIRAR en el PC
 * antes de tocar un Dreamcast, que es lo unico parecido a un oraculo que tiene.
 *
 * ---- el convenio de la camara, que es donde se juega el partido ----
 *
 * El juego deja la vista preparada en enteros de PS1 (ver RaceView en host.h):
 *
 *   - `camera_pos` va en **16.16**, y su X esta **espejada** respecto al mundo.
 *   - `camera_rot` son tres filas de tres: derecha, arriba y adelante. **No estan
 *     normalizadas**: el juego aplasta la imagen en vertical escalando la fila de
 *     "arriba" (sale sobre 0,85), asi que dividir las tres por la longitud de "adelante"
 *     reproduce ese aplastamiento sin tener que tratarlo aparte.
 *   - `projection` es la distancia focal en pixeles de PS1: la tangente de medio campo
 *     horizontal es (ancho / 2) / projection.
 *
 * Los vertices horneados van en 1/64 m relativos al origen de su bloque, y tambien con la
 * X espejada. La proyeccion se hace como la hace PS1 —sx = x * projection / z— y el
 * resultado se escala del area de pantalla de PS1 (320x240 en carrera) a 640x480.
 */
#ifndef PS1_RENDER_TRACK_H
#define PS1_RENDER_TRACK_H

#include "ps1rt.h"

/* La vista la define el host (host.h); aqui basta con el nombre. */
struct RaceView;

/* Indicadores que el juego pone en la parte alta de cada entrada de la lista de trozos.
 * La parte baja es la resolucion: distinta de cero pide el detalle completo. */
#define CHUNK_GROUND   1u   /* DrawW_DoTrough: el suelo */
#define CHUNK_OBJECTS  2u   /* DrawW_DoObjects: los objetos del bloque (M2c) */
#define CHUNK_LINES    4u   /* DrawW_DoLines: las lineas pintadas (M2c) */

/* Indicadores de un material, horneados en el PC. */
#define MAT_TWOSIDED   1u
#define MAT_SEMI       2u
#define MAT_SEMI_ADD   4u
#define MAT_SCROLL     8u

typedef struct {
    uint16_t x, y, w, h;      /* su rectangulo en el atlas, en texeles */
    uint8_t  corner[4];       /* que esquina del rectangulo toma cada vertice */
    uint8_t  flags;
    uint8_t  pad[3];
} TrackMat;

/* OJO: en el archivo un bloque son 44 bytes con pares (u32 offset, u16 cuenta) de SEIS
 * bytes. En C el compilador los rellenaria a ocho, asi que esta estructura **no se
 * superpone al archivo**: track_load la decodifica byte a byte. Eso ademas evita lecturas
 * desalineadas, que en SH-4 son una excepcion y no una lentitud. */
typedef struct {
    int32_t  origin[3];       /* 1/64 m */
    uint32_t off[5];
    uint16_t n[5];
} TrackBlock;

/* Los cinco tramos de un bloque, en el orden en que los hornea bake.rs. */
#define TB_GROUND_VERTS 0
#define TB_GROUND_QUADS 1
#define TB_GROUND_FAR   2
#define TB_DETAIL_VERTS 3
#define TB_DETAIL_QUADS 4

typedef struct {
    uint8_t         *raw;     /* el archivo entero, tal cual */
    uint32_t         size;
    TrackBlock       *block;   /* decodificado, no apunta al archivo */
    const TrackMat   *mat;
    const int16_t    *vert;   /* 4 int16 por vertice: x, y, z y la sombra en el byte bajo */
    const uint16_t   *quad;   /* 5 uint16 por cuadro: v[4] y el material */
    const uint16_t   *atlas;  /* ARGB1555 */
    uint16_t          blocks, mats, atlas_w, atlas_h;
    uint32_t          verts, quads;
} Track;

/* Un vertice listo para pintar. */
typedef struct {
    float    x, y;            /* pantalla, en pixeles de 640x480 */
    float    z;               /* 1/w, que es lo que el PVR quiere de profundidad */
    float    u, v;            /* 0..1 dentro del atlas */
    uint32_t argb;
} TrackVert;

/* Quien pinta. `n` es 3 o 4 y los vertices vienen como abanico. */
typedef void (*TrackEmit)(void *user, const TrackVert *v, uint32_t n, const TrackMat *m);

typedef struct {
    /* medidas del ultimo track_draw */
    uint32_t blocks_drawn;
    uint32_t quads_in;        /* cuadros mirados */
    uint32_t quads_out;       /* triangulos emitidos */
    uint32_t clipped;         /* triangulos que cruzaban el plano cercano */
    uint32_t behind;          /* triangulos enteros detras de la camara */
} TrackStats;

/* Carga un .TRK. Devuelve 0 si no pudo (y deja `t` a cero). */
int  track_load(Track *t, const char *path);
void track_free(Track *t);

/* Dibuja la vista. `light` son las 256 entradas de Chunk_lightTable (0xBBGGRR); si es
 * NULL, los vertices salen grises. `sw` y `sh` son el tamano de la pantalla de salida. */
void track_draw(const Track *t, const struct RaceView *view, const uint32_t *light,
                float sw, float sh, TrackEmit emit, void *user, TrackStats *st);

#endif /* PS1_RENDER_TRACK_H */
