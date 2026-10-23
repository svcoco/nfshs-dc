/* La lista de dibujo 2D del front end: lo que el juego decidio pintar este frame.
 *
 * Es el equivalente en C de OverlayCmd de src/ps1/host/overlay.rs. El port no dibuja
 * (eso es el PVR, pasos 3 y 4 de M2a): lo que hace aqui es **grabar**, y esa lista se
 * puede comparar contra la de la referencia igual que se compara la RAM. Por eso la
 * grabacion va antes que el PVR: todavia tiene oraculo.
 *
 * De momento solo hay un comando, el cuadro en coordenadas de pantalla, porque es lo
 * que producen las tres fuentes del front end:
 *
 *   - el emisor de formas (0x8004E74C), que es el grueso: ~134 por frame;
 *   - PSXDrawSquare y PSXDrawTransSquare, cuadros planos;
 *   - las rutinas que construyen sus propias primitivas (DrawTV y companeras), que se
 *     corren anidadas y se les graba lo que escriben en el buffer de primitivas.
 *
 * El texto y los ayudantes del HUD son comandos aparte y son el paso 5.
 */
#ifndef PS1_HOST_OVERLAY_H
#define PS1_HOST_OVERLAY_H

#include "ps1rt.h"

/* 150 primitivas por frame medidas en el front end; el tope es de cordura, y si se
 * alcanza se cuenta en overlay_dropped en vez de escribir fuera. */
#define OVERLAY_MAX 4096u

/* La textura de un cuadro en terminos de la VRAM de PS1: pagina de textura, paleta y
 * coordenadas de texel por esquina. Un borde puede valer 256 (el final de la pagina). */
typedef struct {
    uint16_t tpage;
    uint16_t clut;
    uint16_t uv[4][2];
} QuadTex;

/* Que es cada entrada de la lista. */
typedef enum {
    OVL_QUAD = 0,            /* un cuadro en coordenadas de pantalla */
    OVL_CLIP = 1             /* SetDrawArea: el dibujo que sigue va recortado a ese rect */
} OverlayKind;

/* Esquinas en orden: arriba-izquierda, arriba-derecha, abajo-izquierda, abajo-derecha.
 * En OVL_CLIP solo se usan pts[0] (esquina) y pts[1] (ancho y alto); el resto no se
 * mira. Se reaprovecha el hueco en vez de crecer el struct: la lista son 4096 entradas
 * y esto es RAM de Dreamcast. */
typedef struct {
    uint8_t  kind;
    int16_t  pts[4][2];
    uint32_t colours[4];     /* 0xBBGGRR; 0x80 es el 1,0 en los cuadros con textura */
    QuadTex  tex;
    uint8_t  has_tex;
    uint8_t  semi;           /* semitransparente */
    uint8_t  abr;            /* 0 mitad, 1 suma, 2 resta, 3 suma un cuarto */
} OverlayQuad;

typedef struct {
    OverlayQuad quad[OVERLAY_MAX];
    uint32_t n;
    uint64_t dropped;        /* cuadros que no cupieron: si no es 0, sube OVERLAY_MAX */
    uint64_t total;          /* cuadros grabados en toda la ejecucion */
    uint16_t draw_mode;      /* el E1 vigente: pagina de textura y modo de mezcla */
    int16_t  prim_offset[2]; /* desplazamiento de la vista que se esta grabando */

    /* Dimensionado de M2a paso 3: cuantas combinaciones distintas de pagina y paleta
     * pide el juego. Es lo que decide el tamano de la cache de texturas del PVR. */
    uint32_t combos[256];    /* (tpage << 16) | clut, sin repetir, de toda la ejecucion */
    uint32_t combos_n;
    uint64_t combos_lost;    /* vistas que no cupieron en la tabla de arriba */
    /* Las de UN frame: es lo que de verdad tiene que estar residente a la vez en la
     * VRAM del PVR, y por tanto lo que dimensiona la cache. El total de arriba solo
     * dice cuantas texturas distintas llega a tocar el front end entero. */
    uint32_t frame_combos[256];
    uint32_t frame_combos_n;
    uint32_t frame_combos_max;
} Overlay;

struct GameHost;

void overlay_reset(Overlay *o);
/* Un cuadro ya resuelto. `tex` puede ser NULL. */
void overlay_push(struct GameHost *h, const int16_t pts[4][2], const uint32_t colours[4],
                  const QuadTex *tex, int semi, uint8_t abr);
/* SetDrawArea en el front end: recorta lo que se dibuje a continuacion. */
void overlay_clip(struct GameHost *h, int16_t x, int16_t y, int16_t w, int16_t hh);
/* Un rectangulo plano: PSXDrawSquare y PSXDrawTransSquare. */
void overlay_square(struct GameHost *h, int32_t x, int32_t y, int32_t w, int32_t h_,
                    uint32_t colour, int semi);
/* El emisor de formas del front end (0x8004E74C). */
void overlay_shape_quads(struct GameHost *h, Cpu *c, uint32_t info, uint32_t flags,
                         int32_t x, int32_t y, const uint32_t colours[4], uint32_t abr);
/* Font_TextXY en el front end: un cuadro por caracter. */
void overlay_font_quads(struct GameHost *h, Cpu *c, uint32_t text, int32_t x, int32_t y);
/* Graba los paquetes de GPU escritos entre `start` y `end` del buffer de primitivas. */
void overlay_record_packets(struct GameHost *h, Cpu *c, uint32_t start, uint32_t end);

#endif /* PS1_HOST_OVERLAY_H */
