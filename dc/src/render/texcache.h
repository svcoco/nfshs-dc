/* Cache de texturas: de la VRAM de PS1 a texturas del PVR (M2a, paso 3).
 *
 * El juego referencia sus texturas como (pagina, paleta, UV) sobre una VRAM de PS1 de
 * 1024x512 halfwords, con las paginas en 4, 8 o 16 bits. El PVR no tiene shaders, asi
 * que las paletizadas hay que expandirlas en el SH-4 a ARGB1555.
 *
 * ---- lo que decide el diseno, medido sobre 120 frames del front end ----
 *
 *   combinaciones (pagina, paleta): 63 distintas, 54 a la vez como maximo
 *   guardando la PAGINA entera por combinacion : 6,75 MB  <- no cabe en los 8 del PVR
 *   guardando solo el RECTANGULO que usan los UV: 0,66 MB
 *   profundidad de las paginas usadas: 8 bits (5280) y 4 bits (842); ninguna de 16
 *   escrituras a la VRAM de PS1: 6,6 por frame
 *
 * De ahi salen las tres decisiones de esta cache:
 *
 *   1. Se convierte el **rectangulo que los UV usan**, no la pagina. Es la diferencia
 *      entre no caber y ocupar menos de un megabyte.
 *   2. La cache **persiste entre frames**. Convertir 0,66 MB por frame en un SH-4 seria
 *      carisimo, y no hace falta: la VRAM casi no cambia.
 *   3. Se invalida por **solape**: cada escritura a la VRAM tumba solo las entradas cuyo
 *      rectangulo toca. Invalidarlo todo en cada escritura costaria reconversiones de
 *      sobra.
 *
 * Portable: no incluye cabeceras de KOS. La conversion produce texeles en un buffer que
 * da el llamante; donde vive ese buffer (memoria del PVR en Dreamcast, montones en PC)
 * lo decide la plataforma.
 */
#ifndef PS1_RENDER_TEXCACHE_H
#define PS1_RENDER_TEXCACHE_H

#include "ps1rt.h"

#define TEX_MAX      128u     /* entradas vivas; medidas 54 como maximo */
#define TEX_VRAM_W  1024u
#define TEX_VRAM_H   512u

typedef struct {
    uint16_t tpage, clut;
    uint16_t u0, v0;          /* esquina del rectangulo CONVERTIDO */
    uint16_t w, h;            /* su tamano, potencia de dos */
    uint32_t offset;          /* donde empieza en el buffer de texeles del llamante */
    /* El rectangulo que pide el frame en curso. Si cabe dentro del convertido, se
     * reutiliza tal cual; si no, se convierte de nuevo. El rectangulo del frame NO se
     * acumula entre frames: hacerlo crecia sin parar y acababa desbordando la cache. */
    uint16_t fu0, fv0, fu1, fv1;
    uint8_t  usada;           /* pedida en el frame en curso */
    uint8_t  lista;           /* ya convertida y vigente */
} TexEntry;

typedef struct {
    TexEntry e[TEX_MAX];
    uint32_t n;
    uint32_t top;             /* asignador por tope: los offsets no se mueven */
    uint32_t texels;          /* texeles reservados en el buffer */
    /* medidas */
    uint64_t conversiones;    /* cuantas veces se ha convertido algo */
    uint64_t texels_conv;     /* y cuantos texeles ha costado */
    uint64_t invalidadas;
    uint64_t sin_sitio;       /* entradas o texeles que no cupieron */
    uint32_t pico_texels;     /* el maximo de texeles vivos a la vez */
    uint64_t vaciados;        /* veces que se lleno y hubo que empezar de cero */
} TexCache;

void tex_reset(TexCache *tc);

/* Empieza un frame: marca todas las entradas como no pedidas. */
void tex_begin_frame(TexCache *tc);

/* Declara que este frame se usa ese rectangulo de UV de esa pagina y paleta. Crece el
 * rectangulo de la entrada si hace falta (y entonces habra que reconvertirla). */
void tex_need(TexCache *tc, uint16_t tpage, uint16_t clut, const uint16_t uv[4][2]);

/* Convierte lo que haga falta al buffer `dst`, de `cap` texeles. Devuelve cuantas
 * entradas convirtio. Llamar despues de todos los tex_need del frame. */
uint32_t tex_build(TexCache *tc, const uint16_t *vram, uint16_t *dst, uint32_t cap);

/* La entrada de esa pagina y paleta, o NULL. */
const TexEntry *tex_find(const TexCache *tc, uint16_t tpage, uint16_t clut);

/* El juego escribio ese rectangulo de la VRAM (en halfwords): tumba lo que solape. */
void tex_dirty(TexCache *tc, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Convierte un rectangulo suelto. La usa tex_build y sirve para probarla aparte. */
void tex_convert(const uint16_t *vram, uint16_t tpage, uint16_t clut,
                 uint16_t u0, uint16_t v0, uint16_t w, uint16_t h, uint16_t *dst);

#endif /* PS1_RENDER_TEXCACHE_H */
