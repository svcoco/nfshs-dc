/* Los videos de PS1 (M3): el bitstream del MDEC (version 2) y el audio XA ADPCM.
 *
 * Traduccion de src/formats/str.rs de la referencia, con la misma aritmetica (IDCT en
 * float, separable, y el mismo YCbCr -> RGB) para poder compararlos frame a frame. Es
 * PORTABLE: lo usan el host de PC y el de Dreamcast.
 *
 * Un frame son macrobloques de 16x16 que bajan por columnas (mx por fuera, my por dentro),
 * cada uno con Cr, Cb y cuatro bloques de Y.
 */
#ifndef NFSHS_MDEC_H
#define NFSHS_MDEC_H

#include <stdint.h>

/* Un macrobloque ya decodificado (despues de la IDCT, sin el +128). */
typedef struct {
    float cr[64], cb[64];
    float y[4][64];                         /* arriba izq., arriba der., abajo izq., abajo der. */
} MdecBlock;

/* Recibe cada macrobloque en el orden del bitstream. */
typedef void (*MdecSink)(void *ctx, int mx, int my, const MdecBlock *b);

void mdec_init(void);

/* Decodifica un frame (`bs`: cabecera de 8 bytes + bitstream) de w x h. */
void mdec_decode(const uint8_t *bs, uint32_t len, int w, int h, MdecSink sink, void *ctx);

/* Salidas: RGBA de 8 bits como la referencia; 16 bits de PS1 con el bit 15 puesto (lo
 * que el juego espera en la VRAM, DecDCTin modo 2); y macrobloques YUV420 para el
 * conversor YUV del PVR (U 8x8, V 8x8 y los cuatro Y de 8x8: 384 bytes, en orden de
 * filas: my * ceil(w/16) + mx). */
void mdec_frame_rgba(const uint8_t *bs, uint32_t len, int w, int h, uint8_t *rgba);
void mdec_frame_ps1(const uint8_t *bs, uint32_t len, int w, int h, uint16_t *px);
void mdec_frame_yuv420(const uint8_t *bs, uint32_t len, int w, int h, uint8_t *mbs);

#define MDEC_YUV_MB 384

/* ---------------------------------------------------------------- STR/XA

 * Un sector de pelicula tal como lo deja dc/tools/bake/movies.py: subcabecera de 8
 * bytes + 2 328. Los de video llevan 0x0160 y trozos de 2 016 bytes del frame; los de
 * audio, 18 grupos de 128 bytes de XA ADPCM. */
#define STR_SECTOR      2336
#define STR_CHUNK       2016
#define STR_MAX_CHUNKS  16

typedef struct {
    /* video: el frame que se esta juntando */
    uint32_t frame;                         /* su numero; ~0 si no hay ninguno */
    uint16_t chunks, have;
    uint16_t width, height;
    uint8_t  got[STR_MAX_CHUNKS];
    uint8_t  data[STR_MAX_CHUNKS * STR_CHUNK];
    /* audio */
    int32_t  prev[2][2];
    uint8_t  stereo;
    uint32_t rate;
} StrDemux;

void str_reset(StrDemux *s);

/* Mete un sector. Devuelve 1 si con el se completa un frame (queda en s->data, con
 * str_frame_len bytes), o si empieza otro antes de completar el anterior (entonces se
 * entrega lo que hubiera, como hace la referencia). Las muestras de audio del sector se
 * escriben en `pcm` (hasta 4 032 int16; *npcm dice cuantas) si `pcm` no es NULL. */
int str_feed(StrDemux *s, const uint8_t *sector, int16_t *pcm, int *npcm, uint32_t *frame_len);

/* Al acabar el archivo: si queda un frame a medias, lo junta y devuelve su longitud (0 si no). */
uint32_t str_flush(StrDemux *s);

/* Decodifica un grupo de sonido de 128 bytes (224 muestras). */
int xa_sound_group(const uint8_t *g, int stereo, int32_t prev[2][2], int16_t *out);

#endif
