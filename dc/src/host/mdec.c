/* El MDEC y el XA ADPCM de PS1: ver mdec.h. Traduccion de src/formats/str.rs. */
#include "mdec.h"

#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- tablas */

/* MPEG-1 tabla B.14 (los coeficientes AC despues del primero): codigo, run, level; el
 * bit de signo va detras del codigo. */
static const struct { const char *code; uint8_t run; uint8_t level; } AC_CODES[] = {
    {"11", 0, 1}, {"011", 1, 1}, {"0100", 0, 2}, {"0101", 2, 1}, {"00101", 0, 3}, {"00111", 3, 1}, {"00110", 4, 1},
    {"000110", 1, 2}, {"000111", 5, 1}, {"000101", 6, 1}, {"000100", 7, 1}, {"0000110", 0, 4}, {"0000100", 2, 2},
    {"0000111", 8, 1}, {"0000101", 9, 1}, {"00100110", 0, 5}, {"00100001", 0, 6}, {"00100101", 1, 3}, {"00100100", 3, 2},
    {"00100111", 10, 1}, {"00100011", 11, 1}, {"00100010", 12, 1}, {"00100000", 13, 1}, {"0000001010", 0, 7},
    {"0000001100", 1, 4}, {"0000001011", 2, 3}, {"0000001111", 4, 2}, {"0000001001", 5, 2}, {"0000001110", 14, 1},
    {"0000001101", 15, 1}, {"0000001000", 16, 1}, {"000000011101", 0, 8}, {"000000011000", 0, 9}, {"000000010011", 0, 10},
    {"000000010000", 0, 11}, {"000000011011", 1, 5}, {"000000010100", 2, 4}, {"000000011100", 3, 3}, {"000000010010", 4, 3},
    {"000000011110", 6, 2}, {"000000010101", 7, 2}, {"000000010001", 8, 2}, {"000000011111", 17, 1}, {"000000011010", 18, 1},
    {"000000011001", 19, 1}, {"000000010111", 20, 1}, {"000000010110", 21, 1}, {"0000000011010", 0, 12}, {"0000000011001", 0, 13},
    {"0000000011000", 0, 14}, {"0000000010111", 0, 15}, {"0000000010110", 1, 6}, {"0000000010101", 1, 7}, {"0000000010100", 2, 5},
    {"0000000010011", 3, 4}, {"0000000010010", 5, 3}, {"0000000010001", 9, 2}, {"0000000010000", 10, 2}, {"0000000011111", 22, 1},
    {"0000000011110", 23, 1}, {"0000000011101", 24, 1}, {"0000000011100", 25, 1}, {"0000000011011", 26, 1},
    {"00000000011111", 0, 16}, {"00000000011110", 0, 17}, {"00000000011101", 0, 18}, {"00000000011100", 0, 19},
    {"00000000011011", 0, 20}, {"00000000011010", 0, 21}, {"00000000011001", 0, 22}, {"00000000011000", 0, 23},
    {"00000000010111", 0, 24}, {"00000000010110", 0, 25}, {"00000000010101", 0, 26}, {"00000000010100", 0, 27},
    {"00000000010011", 0, 28}, {"00000000010010", 0, 29}, {"00000000010001", 0, 30}, {"00000000010000", 0, 31},
    {"000000000011000", 0, 32}, {"000000000010111", 0, 33}, {"000000000010110", 0, 34}, {"000000000010101", 0, 35},
    {"000000000010100", 0, 36}, {"000000000010011", 0, 37}, {"000000000010010", 0, 38}, {"000000000010001", 0, 39},
    {"000000000010000", 0, 40}, {"000000000011111", 1, 8}, {"000000000011110", 1, 9}, {"000000000011101", 1, 10},
    {"000000000011100", 1, 11}, {"000000000011011", 1, 12}, {"000000000011010", 1, 13}, {"000000000011001", 1, 14},
    {"0000000000010011", 1, 15}, {"0000000000010010", 1, 16}, {"0000000000010001", 1, 17}, {"0000000000010000", 1, 18},
    {"0000000000010100", 6, 3}, {"0000000000011010", 11, 2}, {"0000000000011001", 12, 2}, {"0000000000011000", 13, 2},
    {"0000000000010111", 14, 2}, {"0000000000010110", 15, 2}, {"0000000000010101", 16, 2}, {"0000000000011111", 27, 1},
    {"0000000000011110", 28, 1}, {"0000000000011101", 29, 1}, {"0000000000011100", 30, 1}, {"0000000000011011", 31, 1},
};

/* La tabla de cuantizacion del MDEC (MPEG-1 intra por omision, DC = 2), en orden natural. */
static const int32_t QUANT[64] = {
    2, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83,
};

static const uint8_t ZIGZAG[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* Codigos por los 16 bits siguientes: con alguno de los 6 primeros a 1 bastan los 8
 * primeros (todos los codigos con menos de 6 ceros delante miden 8 o menos); con los 6 a
 * 0, los 10 restantes. len 0 = no hay codigo. */
typedef struct { uint8_t len, run; uint16_t level; } Vlc;
static Vlc s_short[256], s_long[1024];
static float s_cos[8][8];                   /* [x][u] */
static float s_k4, s_p, s_q, s_c1, s_c3, s_c5, s_c7;   /* los de la IDCT rapida */
static int s_ready;

void mdec_init(void)
{
    if (s_ready)
        return;
    for (unsigned i = 0; i < sizeof AC_CODES / sizeof AC_CODES[0]; i++) {
        const char *c = AC_CODES[i].code;
        unsigned len = (unsigned)strlen(c), v = 0;
        for (unsigned k = 0; k < len; k++)
            v = (v << 1) | (unsigned)(c[k] - '0');
        Vlc e = { (uint8_t)len, AC_CODES[i].run, AC_CODES[i].level };
        unsigned top = v << (16 - len);     /* el codigo alineado a 16 bits */
        if (top >= 0x0400u) {
            unsigned base = top >> 8, n = 1u << (8 - len);
            for (unsigned k = 0; k < n; k++)
                s_short[base + k] = e;
        } else {
            unsigned base = top & 0x3FFu, n = 1u << (16 - len);
            for (unsigned k = 0; k < n; k++)
                s_long[base + k] = e;
        }
    }
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++) {
            float c = u == 0 ? 0.70710677f : 1.0f;
            s_cos[x][u] = c * cosf((float)(2 * x + 1) * (float)u * 3.14159265358979323846f / 16.0f) / 2.0f;
        }
#ifndef MDEC_EXACT
    s_k4 = s_cos[0][0];
    s_p = s_cos[0][2];
    s_q = s_cos[0][6];
    s_c1 = s_cos[0][1];
    s_c3 = s_cos[0][3];
    s_c5 = s_cos[0][5];
    s_c7 = s_cos[0][7];
#endif
    s_ready = 1;
}

/* ---------------------------------------------------------------- bits */

/* Palabras de 16 bits little-endian, el bit mas alto primero. Pasado el final, ceros (como
 * la referencia, que solo da por buena una palabra entera). */
typedef struct {
    const uint8_t *d;
    uint32_t words;
    uint32_t pos;
} Bits;

static inline uint32_t word_at(const Bits *b, uint32_t w)
{
    return w < b->words ? (uint32_t)b->d[2 * w] | ((uint32_t)b->d[2 * w + 1] << 8) : 0;
}

/* Los 16 bits desde pos. */
static inline uint32_t peek16(const Bits *b)
{
    uint32_t w = b->pos >> 4, s = b->pos & 15;
    uint32_t v = (word_at(b, w) << 16) | word_at(b, w + 1);
    return (v >> (16 - s)) & 0xFFFFu;
}

static inline uint32_t get(Bits *b, unsigned n)
{
    uint32_t v = peek16(b) >> (16 - n);
    b->pos += n;
    return v;
}

#ifndef MDEC_EXACT
/* La misma IDCT con la simetria par/impar de los cosenos: 22 productos por transformada de
 * 8 puntos en vez de 64, y sin hacer nada con lo que es cero. No redondea igual que la de
 * la referencia (que suma en orden): la diferencia esta medida en docs/hitos/M3.md. */
/* in[0..7] con paso `si`; `mask`: que entradas pueden no ser cero. */
static inline void idct8(const float *in, int si, float *out, int so, unsigned mask)
{
    float i0 = in[0], i4 = in[4 * si];
    if (mask == 1u) {
        float v = s_k4 * i0;
        for (int k = 0; k < 8; k++)
            out[k * so] = v;
        return;
    }
    float i2 = in[2 * si], i6 = in[6 * si];
    float e0 = s_k4 * (i0 + i4), e1 = s_k4 * (i0 - i4);
    float f0 = s_p * i2 + s_q * i6, f1 = s_q * i2 - s_p * i6;
    float E0 = e0 + f0, E3 = e0 - f0, E1 = e1 + f1, E2 = e1 - f1;
    if (!(mask & 0xAAu)) {
        out[0] = out[7 * so] = E0;
        out[so] = out[6 * so] = E1;
        out[2 * so] = out[5 * so] = E2;
        out[3 * so] = out[4 * so] = E3;
        return;
    }
    float i1 = in[si], i3 = in[3 * si], i5 = in[5 * si], i7 = in[7 * si];
    float O0 = s_c1 * i1 + s_c3 * i3 + s_c5 * i5 + s_c7 * i7;
    float O1 = s_c3 * i1 - s_c7 * i3 - s_c1 * i5 - s_c5 * i7;
    float O2 = s_c5 * i1 - s_c1 * i3 + s_c7 * i5 + s_c3 * i7;
    float O3 = s_c7 * i1 - s_c5 * i3 + s_c3 * i5 - s_c1 * i7;
    out[0] = E0 + O0;
    out[7 * so] = E0 - O0;
    out[so] = E1 + O1;
    out[6 * so] = E1 - O1;
    out[2 * so] = E2 + O2;
    out[5 * so] = E2 - O2;
    out[3 * so] = E3 + O3;
    out[4 * so] = E3 - O3;
}

static void idct_fast(const float *coef, unsigned rows, const uint8_t *cols, float *out)
{
    float tmp[64];
    if (!rows) {
        memset(out, 0, 64 * sizeof(float));
        return;
    }
    if (rows == 1u && cols[0] == 1u) {
        float v = s_k4 * s_k4 * coef[0];
        for (int i = 0; i < 64; i++)
            out[i] = v;
        return;
    }
    for (int y = 0; y < 8; y++)
        if (rows & (1u << y))
            idct8(&coef[y * 8], 1, &tmp[y * 8], 1, cols[y]);
        else
            memset(&tmp[y * 8], 0, 8 * sizeof(float));
    for (int x = 0; x < 8; x++)
        idct8(&tmp[x], 8, &out[x], 8, rows);
}
#endif

/* Un bloque de 8x8: DC (10 bits con signo) y pares run/level hasta el fin de bloque ("10").
 * Devuelve la IDCT en out. */
static void block(Bits *b, uint32_t qscale, float *out)
{
    float coef[64];
    uint8_t rows = 0;                       /* filas con algun coeficiente */
    uint8_t cols[8] = {0};                  /* por fila, sus columnas con coeficiente */
    memset(coef, 0, sizeof coef);
    int32_t dc = ((int32_t)(get(b, 10) << 22)) >> 22;
    coef[0] = (float)(dc * QUANT[0]);
    if (dc) {
        rows = 1;
        cols[0] = 1;
    }
    int k = 0;
    for (;;) {
        uint32_t p = peek16(b);
        int run, level;
        if ((p >> 14) == 2u) {
            b->pos += 2;
            break;
        }
        if ((p >> 10) == 1u) {
            /* escape: run de 6 bits y level de 10 con signo */
            b->pos += 6;
            run = (int)get(b, 6);
            level = ((int32_t)(get(b, 10) << 22)) >> 22;
        } else {
            const Vlc *e = p >= 0x0400u ? &s_short[p >> 8] : &s_long[p & 0x3FFu];
            if (!e->len)
                break;                      /* corrupto: termina el bloque */
            b->pos += e->len;
            run = e->run;
            level = get(b, 1) ? -(int)e->level : (int)e->level;
        }
        k += run + 1;
        if (k >= 64)
            break;
        int n = ZIGZAG[k];
        coef[n] = (float)((level * (int32_t)(QUANT[n] * qscale) + 4) / 8);
        rows |= (uint8_t)(1u << (n >> 3));
        cols[n >> 3] |= (uint8_t)(1u << (n & 7));
    }
#ifndef MDEC_EXACT
    idct_fast(coef, rows, cols, out);
#else
    /* IDCT separable, en el orden de la referencia; las filas vacias dan cero */
    float tmp[64];
    for (int y = 0; y < 8; y++) {
        if (!(rows & (1u << y))) {
            memset(&tmp[y * 8], 0, 8 * sizeof(float));
            continue;
        }
        const float *c = &coef[y * 8];
        for (int x = 0; x < 8; x++) {
            const float *w = s_cos[x];
            float s = 0.0f;
            for (int u = 0; u < 8; u++)
                s += w[u] * c[u];
            tmp[y * 8 + x] = s;
        }
    }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            const float *w = s_cos[y];
            float s = 0.0f;
            for (int v = 0; v < 8; v++)
                if (rows & (1u << v))
                    s += w[v] * tmp[v * 8 + x];
            out[y * 8 + x] = s;
        }
#endif
}

void mdec_decode(const uint8_t *bs, uint32_t len, int w, int h, MdecSink sink, void *ctx)
{
    MdecBlock mb;
    mdec_init();
    if (len < 8)
        return;
    uint32_t qscale = (uint32_t)bs[4] | ((uint32_t)bs[5] << 8);
    Bits b = { bs + 8, (len - 8) / 2, 0 };
    int mbw = (w + 15) / 16, mbh = (h + 15) / 16;
    for (int mx = 0; mx < mbw; mx++)
        for (int my = 0; my < mbh; my++) {
            block(&b, qscale, mb.cr);
            block(&b, qscale, mb.cb);
            for (int i = 0; i < 4; i++)
                block(&b, qscale, mb.y[i]);
            sink(ctx, mx, my, &mb);
        }
}

/* ---------------------------------------------------------------- salidas */

static inline uint8_t sat(float v)
{
    return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (uint8_t)v;   /* trunca, como `as u8` */
}

typedef struct { int w, h; uint8_t *rgba; uint16_t *px; uint8_t *mbs; } Out;

static void put_rgb(void *ctx, int mx, int my, const MdecBlock *b)
{
    const Out *o = ctx;
    for (int py = 0; py < 16; py++)
        for (int px = 0; px < 16; px++) {
            int x = mx * 16 + px, y = my * 16 + py;
            if (x >= o->w || y >= o->h)
                continue;
            float l = b->y[(py / 8) * 2 + px / 8][(py % 8) * 8 + px % 8] + 128.0f;
            int c = (py / 2) * 8 + px / 2;
            float cr = b->cr[c], cb = b->cb[c];
            uint8_t r = sat(l + 1.402f * cr);
            uint8_t g = sat(l - 0.3437f * cb - 0.7143f * cr);
            uint8_t bl = sat(l + 1.772f * cb);
            int i = y * o->w + x;
            if (o->rgba) {
                o->rgba[4 * i] = r;
                o->rgba[4 * i + 1] = g;
                o->rgba[4 * i + 2] = bl;
                o->rgba[4 * i + 3] = 255;
            } else {
                o->px[i] = (uint16_t)(0x8000u | (r >> 3) | ((g >> 3) << 5) | ((bl >> 3) << 10));
            }
        }
}

void mdec_frame_rgba(const uint8_t *bs, uint32_t len, int w, int h, uint8_t *rgba)
{
    Out o = { w, h, rgba, NULL, NULL };
    memset(rgba, 0, (size_t)w * h * 4);
    mdec_decode(bs, len, w, h, put_rgb, &o);
}

void mdec_frame_ps1(const uint8_t *bs, uint32_t len, int w, int h, uint16_t *px)
{
    Out o = { w, h, NULL, px, NULL };
    mdec_decode(bs, len, w, h, put_rgb, &o);
}

static void put_yuv(void *ctx, int mx, int my, const MdecBlock *b)
{
    const Out *o = ctx;
    uint8_t *m = o->mbs + ((size_t)my * ((o->w + 15) / 16) + mx) * MDEC_YUV_MB;
    for (int i = 0; i < 64; i++) {
        m[i] = sat(b->cb[i] + 128.0f);
        m[64 + i] = sat(b->cr[i] + 128.0f);
    }
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 64; i++)
            m[128 + 64 * k + i] = sat(b->y[k][i] + 128.0f);
}

void mdec_frame_yuv420(const uint8_t *bs, uint32_t len, int w, int h, uint8_t *mbs)
{
    Out o = { w, h, NULL, NULL, mbs };
    mdec_decode(bs, len, w, h, put_yuv, &o);
}

/* ---------------------------------------------------------------- XA ADPCM */

static const int32_t K0[5] = {0, 60, 115, 98, 122};
static const int32_t K1[5] = {0, 0, -52, -55, -60};

int xa_sound_group(const uint8_t *g, int stereo, int32_t prev[2][2], int16_t *out)
{
    int16_t units[8][28];
    for (int u = 0; u < 8; u++) {
        uint8_t param = g[4 + u];
        int range = (param & 0xF) < 12 ? (param & 0xF) : 12;
        int filter = (param >> 4) < 4 ? (param >> 4) : 4;
        int ch = stereo ? (u & 1) : 0;
        for (int j = 0; j < 28; j++) {
            uint8_t byte = g[16 + j * 4 + u / 2];
            uint8_t nib = (u & 1) == 0 ? (byte & 0xF) : (byte >> 4);
            int32_t raw = ((int32_t)(int16_t)(uint16_t)(nib << 12)) >> range;
            int32_t v = raw + (prev[ch][0] * K0[filter] + prev[ch][1] * K1[filter] + 32) / 64;
            v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
            prev[ch][1] = prev[ch][0];
            prev[ch][0] = v;
            units[u][j] = (int16_t)v;
        }
    }
    int n = 0;
    if (stereo) {
        for (int p = 0; p < 8; p += 2)
            for (int j = 0; j < 28; j++) {
                out[n++] = units[p][j];
                out[n++] = units[p + 1][j];
            }
    } else {
        for (int u = 0; u < 8; u++)
            for (int j = 0; j < 28; j++)
                out[n++] = units[u][j];
    }
    return n;
}

/* ---------------------------------------------------------------- STR */

void str_reset(StrDemux *s)
{
    memset(s, 0, sizeof *s);
    s->frame = ~0u;
    s->stereo = 1;
    s->rate = 37800;
}

/* Junta los trozos recibidos en orden (los que falten se saltan, como la referencia). */
static uint32_t compact(StrDemux *s)
{
    uint32_t n = 0;
    for (unsigned i = 0; i < s->chunks && i < STR_MAX_CHUNKS; i++)
        if (s->got[i]) {
            if (n != i * STR_CHUNK)
                memmove(&s->data[n], &s->data[i * STR_CHUNK], STR_CHUNK);
            n += STR_CHUNK;
        }
    return n;
}

uint32_t str_flush(StrDemux *s)
{
    if (!s->have || s->have >= s->chunks)
        return 0;
    uint32_t n = compact(s);
    s->have = 0;
    return n;
}

int str_feed(StrDemux *s, const uint8_t *sec, int16_t *pcm, int *npcm, uint32_t *frame_len)
{
    uint8_t submode = sec[2], coding = sec[3];
    const uint8_t *body = sec + 8;
    if (npcm)
        *npcm = 0;
    if (submode & 0x04) {
        s->stereo = coding & 1;
        s->rate = (coding & 4) ? 18900 : 37800;
        if (pcm) {
            int n = 0;
            for (int gi = 0; gi < 18; gi++)
                n += xa_sound_group(body + gi * 128, s->stereo, s->prev, pcm + n);
            *npcm = n;
        }
        return 0;
    }
    if (!(submode & 0x02) || (body[0] | (body[1] << 8)) != 0x0160)
        return 0;
    uint32_t chunk = body[4] | (body[5] << 8);
    uint32_t chunks = body[6] | (body[7] << 8);
    uint32_t frame = body[8] | (body[9] << 8) | ((uint32_t)body[10] << 16) | ((uint32_t)body[11] << 24);
    if (frame != s->frame) {
        if (s->frame != ~0u && s->have && s->have < s->chunks) {
            /* empieza otro frame sin haber completado este: se entrega lo que hay y el
             * sector se vuelve a meter */
            *frame_len = compact(s);
            s->have = 0;
            s->chunks = 0;
            return 2;
        }
        s->frame = frame;
        s->chunks = (uint16_t)(chunks ? chunks : 1);
        s->have = 0;
        memset(s->got, 0, sizeof s->got);
    }
    s->width = (uint16_t)(body[16] | (body[17] << 8));
    s->height = (uint16_t)(body[18] | (body[19] << 8));
    if (chunk < s->chunks && chunk < STR_MAX_CHUNKS && !s->got[chunk]) {
        memcpy(&s->data[chunk * STR_CHUNK], body + 32, STR_CHUNK);
        s->got[chunk] = 1;
        if (++s->have == s->chunks) {
            *frame_len = compact(s);
            return 1;
        }
    }
    return 0;
}
