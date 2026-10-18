/* GTE (COP2): semantica del banco de registros y todos los comandos.
 *
 * Portado de crates/ps1rt/src/gte.rs, que sigue la descripcion de psx-spx. Los
 * registros viven en Cpu::gte_regs: 0..31 datos, 32..63 control.
 *
 * Lo delicado aqui no son las formulas, son las saturaciones y el registro FLAG: el
 * juego puede leerlo con cfc2 31 para detectar recorte, asi que las banderas son
 * estado observable y cualquier diferencia rompe el test diferencial. Van tambien
 * tres rarezas del hardware que estan explicitas en la referencia y NO son errores:
 * la bandera de IR3 en RTP mira el valor desplazado 12 bits independientemente de sf,
 * MVMVA con cv == 2 solo lleva al MAC las dos ultimas columnas, y con mx == 3 el
 * hardware compone una "matriz basura" a partir de gte_regs[6].
 *
 * Toda la aritmetica va en int64_t. En SH-4 eso es software, pero el juego solo tiene
 * 809 instrucciones cop2 y estan en el codigo de dibujo; si pesa, se mide en M1b.
 */
#include "ps1rt.h"

#define FLAG 63u      /* 32 + 31 */

/* Tabla de la division Newton-Raphson del GTE, horneada en vez de calculada al
 * arrancar: T[i] = max(0, ((0x40000 / (i + 0x100) + 1) / 2) - 0x101). */
static const uint8_t unr_table[0x101] = {
    255, 253, 251, 249, 247, 245, 243, 241, 239, 238, 236, 234, 232,
    230, 228, 227, 225, 223, 221, 220, 218, 216, 214, 213, 211, 209,
    208, 206, 205, 203, 201, 200, 198, 197, 195, 193, 192, 190, 189,
    187, 186, 184, 183, 181, 180, 178, 177, 176, 174, 173, 171, 170,
    169, 167, 166, 164, 163, 162, 160, 159, 158, 156, 155, 154, 153,
    151, 150, 149, 148, 146, 145, 144, 143, 141, 140, 139, 138, 137,
    135, 134, 133, 132, 131, 130, 129, 127, 126, 125, 124, 123, 122,
    121, 120, 119, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108,
    107, 106, 105, 104, 103, 102, 101, 100,  99,  98,  97,  96,  95,
     94,  93,  93,  92,  91,  90,  89,  88,  87,  86,  85,  84,  83,
     83,  82,  81,  80,  79,  78,  77,  77,  76,  75,  74,  73,  72,
     72,  71,  70,  69,  68,  67,  67,  66,  65,  64,  63,  63,  62,
     61,  60,  60,  59,  58,  57,  57,  56,  55,  54,  54,  53,  52,
     51,  51,  50,  49,  49,  48,  47,  46,  46,  45,  44,  44,  43,
     42,  42,  41,  40,  40,  39,  38,  38,  37,  36,  36,  35,  34,
     34,  33,  32,  32,  31,  30,  30,  29,  29,  28,  27,  27,  26,
     25,  25,  24,  24,  23,  22,  22,  21,  21,  20,  20,  19,  18,
     18,  17,  17,  16,  15,  15,  14,  14,  13,  13,  12,  12,  11,
     10,  10,   9,   9,   8,   8,   7,   7,   6,   6,   5,   5,   4,
      4,   3,   3,   2,   2,   1,   1,   0,   0,   0,
};

typedef struct {
    uint32_t sf;
    int      lm;
    uint32_t mx;
    uint32_t v;
    uint32_t cv;
} Op;

static int64_t clamp64(int64_t v, int64_t lo, int64_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/* Ceros a la izquierda de 32 bits, con 0 -> 32 (como u32::leading_zeros de Rust). */
static uint32_t clz32(uint32_t v)
{
    if (v == 0)
        return 32u;
#if defined(__GNUC__)
    return (uint32_t)__builtin_clz(v);
#else
    {
        uint32_t n = 0;
        while ((v & 0x80000000u) == 0u) { v <<= 1; n++; }
        return n;
    }
#endif
}

/* Division sin signo del GTE, h * 0x20000 / sz3, saturada a 0x1FFFF.
 * Devuelve 0 si no hay resultado (el llamante pone la bandera 17), y en ese caso
 * *ok queda a 0: es el Option<u32> de la referencia. No es static porque las
 * pruebas la comprueban con los puntos de referencia de gte.rs. */
uint32_t ps1_gte_divide(Cpu *c, uint32_t h, uint32_t sz3, int *ok)
{
    uint32_t z;
    uint64_t n;
    int64_t d, u, r, idx;

    *ok = 0;
    if (h >= sz3 * 2u)
        return 0;
    /* ceros a la izquierda de sz3 TRUNCADO a 16 bits, no de los 32 */
    z = clz32((uint32_t)(uint16_t)sz3);
    z = (z >= 16u) ? (z - 16u) : 0u;
    if ((uint16_t)sz3 == 0u)
        z = 16u;

    n = (uint64_t)h << z;
    d = (int64_t)(uint32_t)(sz3 << z);
    idx = (d - 0x7FC0) >> 7;
    /* d normalizado cae en 0x8000..0xFFFF, asi que idx va de 0 a 0x100. Si se sale,
     * la referencia entra en panico por indice fuera de rango; aqui tambien, en vez
     * de leer fuera del array en silencio. */
    if (idx < 0 || idx > 0x100)
        ps1_panic(c, "indice de la tabla UNR del GTE fuera de rango");
    u = (int64_t)unr_table[idx] + 0x101;
    d = (0x02000080 - d * u) >> 8;
    d = (0x00000080 + d * u) >> 8;
    r = ((int64_t)n * d + 0x8000) >> 16;
    if (r > 0x1FFFF)
        r = 0x1FFFF;
    *ok = 1;
    return (uint32_t)r;
}

/* ---- acceso a registros con las rarezas de lectura y escritura del hardware ---- */

uint32_t ps1_gte_read(const Cpu *c, uint32_t r)
{
    const uint32_t *g = c->gte_regs;
    switch (r) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        return (uint32_t)(int32_t)(int16_t)(uint16_t)g[r];
    case 7: case 16: case 17: case 18: case 19:
        return g[r] & 0xFFFFu;
    case 15:
        return g[14];
    case 28: case 29: {
        /* IRGB / ORGB: IR1..3 comprimidos a 5 bits por componente */
        int32_t a = (int32_t)(int16_t)(uint16_t)g[9] >> 7;
        int32_t b = (int32_t)(int16_t)(uint16_t)g[10] >> 7;
        int32_t d = (int32_t)(int16_t)(uint16_t)g[11] >> 7;
        a = (int32_t)clamp64(a, 0, 0x1F);
        b = (int32_t)clamp64(b, 0, 0x1F);
        d = (int32_t)clamp64(d, 0, 0x1F);
        return (uint32_t)a | ((uint32_t)b << 5) | ((uint32_t)d << 10);
    }
    case 31: {
        uint32_t v = g[30];
        return clz32((v & 0x80000000u) ? ~v : v);
    }
    /* los registros de control de 16 bits se leen con el signo extendido */
    case 36: case 44: case 52: case 58: case 59: case 61: case 62:
        return (uint32_t)(int32_t)(int16_t)(uint16_t)g[r];
    default:
        return g[r];
    }
}

void ps1_gte_write(Cpu *c, uint32_t r, uint32_t v)
{
    uint32_t *g = c->gte_regs;
    switch (r) {
    case 15:                            /* SXYP: empuja la cola de vertices */
        g[12] = g[13];
        g[13] = g[14];
        g[14] = v;
        return;
    case 28:                            /* IRGB: expande a IR1..3 */
        g[28] = v & 0x7FFFu;
        g[9] = (v & 0x1Fu) << 7;
        g[10] = ((v >> 5) & 0x1Fu) << 7;
        g[11] = ((v >> 10) & 0x1Fu) << 7;
        return;
    case 29: case 31:                   /* ORGB y LZCR son de solo lectura */
        return;
    case 30:
        g[30] = v;
        return;
    case FLAG:
        g[FLAG] = (v & 0x7FFFF000u) | ((v & 0x7F87E000u) ? 0x80000000u : 0u);
        return;
    default:
        g[r] = v;
        return;
    }
}

/* ---- helpers ---- */

static int64_t gd(const Cpu *c, uint32_t r)   /* los 32 bits, con signo */
{
    return (int64_t)(int32_t)c->gte_regs[r];
}

static int64_t glo(const Cpu *c, uint32_t r)  /* mitad baja, con signo */
{
    return (int64_t)(int16_t)(uint16_t)c->gte_regs[r];
}

static int64_t ghi(const Cpu *c, uint32_t r)  /* mitad alta, con signo */
{
    return (int64_t)(int16_t)(uint16_t)(c->gte_regs[r] >> 16);
}

static int64_t gir(const Cpu *c, uint32_t i)  /* IR0..IR3 */
{
    return (int64_t)(int16_t)(uint16_t)c->gte_regs[8 + i];
}

static void gflag(Cpu *c, uint32_t bit)
{
    c->gte_regs[FLAG] |= 1u << bit;
}

/* Matriz 3x3 a partir del registro de control `base` (RT = 32, LLM = 40, LCM = 48). */
static void gmatrix(const Cpu *c, uint32_t base, int64_t m[3][3])
{
    uint32_t k;
    for (k = 0; k < 9u; k++) {
        int64_t e = (k % 2u == 0u) ? glo(c, base + k / 2u) : ghi(c, base + k / 2u);
        m[k / 3u][k % 3u] = e;
    }
}

static void gvector(const Cpu *c, uint32_t v, int64_t out[3])
{
    if (v <= 2u) {
        uint32_t b = v * 2u;
        out[0] = glo(c, b);
        out[1] = ghi(c, b);
        out[2] = glo(c, b + 1u);
    } else {
        out[0] = gir(c, 1);
        out[1] = gir(c, 2);
        out[2] = gir(c, 3);
    }
}

/* Comprueba el desbordamiento de 44 bits de MAC1..3, desplaza y guarda. */
static int64_t set_mac(Cpu *c, uint32_t i, int64_t v, uint32_t shift)
{
    int64_t r;
    if (v > 0x7FFFFFFFFFFLL)
        gflag(c, 31u - i);
    else if (v < -0x80000000000LL)
        gflag(c, 28u - i);
    r = v >> shift;
    c->gte_regs[24 + i] = (uint32_t)r;
    return r;
}

static int64_t set_mac0(Cpu *c, int64_t v)
{
    if (v > 0x7FFFFFFFLL)
        gflag(c, 16u);
    else if (v < -0x80000000LL)
        gflag(c, 15u);
    c->gte_regs[24] = (uint32_t)v;
    return v;
}

static void set_ir(Cpu *c, uint32_t i, int64_t v, int lm)
{
    int64_t min = lm ? 0 : -0x8000;
    int64_t cl = clamp64(v, min, 0x7FFF);
    if (cl != v)
        gflag(c, 25u - i);
    c->gte_regs[8 + i] = (uint32_t)cl;
}

static void set_ir0(Cpu *c, int64_t v)
{
    int64_t cl = clamp64(v, 0, 0x1000);
    if (cl != v)
        gflag(c, 12u);
    c->gte_regs[8] = (uint32_t)cl;
}

static void macs_to_irs(Cpu *c, int lm)
{
    uint32_t i;
    for (i = 1u; i <= 3u; i++)
        set_ir(c, i, (int64_t)(int32_t)c->gte_regs[24 + i], lm);
}

static void push_sz(Cpu *c, int64_t v)
{
    int64_t cl = clamp64(v, 0, 0xFFFF);
    uint32_t *g = c->gte_regs;
    if (cl != v)
        gflag(c, 18u);
    g[16] = g[17];
    g[17] = g[18];
    g[18] = g[19];
    g[19] = (uint32_t)cl;
}

static void push_sxy(Cpu *c, int64_t x, int64_t y)
{
    int64_t cx = clamp64(x, -0x400, 0x3FF);
    int64_t cy = clamp64(y, -0x400, 0x3FF);
    uint32_t *g = c->gte_regs;
    if (cx != x)
        gflag(c, 14u);
    if (cy != y)
        gflag(c, 13u);
    g[12] = g[13];
    g[13] = g[14];
    g[14] = (uint32_t)(uint16_t)cx | ((uint32_t)(uint16_t)cy << 16);
}

static void push_color(Cpu *c)
{
    uint32_t code = c->gte_regs[6] >> 24;
    uint32_t col[3];
    uint32_t i, *g;
    for (i = 0; i < 3u; i++) {
        int64_t v = (int64_t)(int32_t)c->gte_regs[25 + i] >> 4;
        int64_t s = clamp64(v, 0, 0xFF);
        if (s != v)
            gflag(c, 21u - i);
        col[i] = (uint32_t)s;
    }
    g = c->gte_regs;
    g[20] = g[21];
    g[21] = g[22];
    g[22] = col[0] | (col[1] << 8) | (col[2] << 16) | (code << 24);
}

/* MAC = (cv * 0x1000 + M * V) >> shift */
static void mul_mat_vec(Cpu *c, int64_t m[3][3], const int64_t v[3],
                        const int64_t t[3], Op o)
{
    uint32_t i, j;
    for (i = 0; i < 3u; i++) {
        int64_t acc = t[i] << 12;
        for (j = 0; j < 3u; j++)
            acc += m[i][j] * v[j];
        set_mac(c, i + 1u, acc, o.sf * 12u);
    }
    macs_to_irs(c, o.lm);
}

static void rtp(Cpu *c, uint32_t vi, Op o, int last)
{
    int64_t m[3][3], v[3], tr[3], mac[3], raw3, ir3, x, y;
    uint32_t i, h, nn;
    int ok;
    gmatrix(c, 32u, m);
    gvector(c, vi, v);
    tr[0] = gd(c, 37u);
    tr[1] = gd(c, 38u);
    tr[2] = gd(c, 39u);
    for (i = 0; i < 3u; i++) {
        int64_t acc = (tr[i] << 12) + m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
        mac[i] = set_mac(c, i + 1u, acc, o.sf * 12u);
    }
    set_ir(c, 1u, mac[0], o.lm);
    set_ir(c, 2u, mac[1], o.lm);
    /* rareza de IR3: la bandera de saturacion mira el valor desplazado 12 bits
     * independientemente de sf, asi que IR3 se escribe a mano y la bandera sale de
     * recalcular el producto */
    ir3 = clamp64(mac[2], o.lm ? 0 : -0x8000, 0x7FFF);
    c->gte_regs[11] = (uint32_t)ir3;
    raw3 = ((tr[2] << 12) + m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]) >> 12;
    if (raw3 != clamp64(raw3, -0x8000, 0x7FFF))
        gflag(c, 22u);

    push_sz(c, mac[2] >> ((1u - o.sf) * 12u));
    h = c->gte_regs[32u + 26u] & 0xFFFFu;
    nn = ps1_gte_divide(c, h, c->gte_regs[19], &ok);
    if (!ok) {
        gflag(c, 17u);
        nn = 0x1FFFFu;
    }
    x = set_mac0(c, (int64_t)nn * gir(c, 1) + gd(c, 32u + 24u));
    y = set_mac0(c, (int64_t)nn * gir(c, 2) + gd(c, 32u + 25u));
    push_sxy(c, x >> 16, y >> 16);
    if (last) {
        int64_t dq = set_mac0(c, (int64_t)nn * glo(c, 32u + 27u) + gd(c, 32u + 28u));
        set_ir0(c, dq >> 12);
    }
}

/* Depth cue: interpola MAC1..3 (sin desplazar) hacia el color lejano con IR0. */
static void depth_cue(Cpu *c, const int64_t mac[3], Op o)
{
    int64_t fc[3], ir0;
    uint32_t i;
    fc[0] = gd(c, 32u + 21u);
    fc[1] = gd(c, 32u + 22u);
    fc[2] = gd(c, 32u + 23u);
    ir0 = gir(c, 0);
    for (i = 0; i < 3u; i++) {
        int64_t t = ((fc[i] << 12) - mac[i]) >> (o.sf * 12u);
        set_ir(c, i + 1u, t, 0);
    }
    for (i = 0; i < 3u; i++) {
        int64_t v = gir(c, i + 1u) * ir0 + mac[i];
        set_mac(c, i + 1u, v, o.sf * 12u);
    }
    macs_to_irs(c, o.lm);
    push_color(c);
}

static void rgbc(const Cpu *c, int64_t out[3])
{
    uint32_t v = c->gte_regs[6];
    out[0] = (int64_t)(v & 0xFFu);
    out[1] = (int64_t)((v >> 8) & 0xFFu);
    out[2] = (int64_t)((v >> 16) & 0xFFu);
}

/* BK + LCM * IR, y despues la parte de color (CC) o de profundidad (DP). */
static void color_stage(Cpu *c, Op o, uint32_t kind)
{
    int64_t lcm[3][3], ir[3], bk[3], col[3], mac[3];
    uint32_t i;
    gmatrix(c, 48u, lcm);
    gvector(c, 3u, ir);
    bk[0] = gd(c, 32u + 13u);
    bk[1] = gd(c, 32u + 14u);
    bk[2] = gd(c, 32u + 15u);
    mul_mat_vec(c, lcm, ir, bk, o);
    if (kind == 0u) {
        push_color(c);
    } else if (kind == 1u) {
        rgbc(c, col);
        for (i = 0; i < 3u; i++)
            set_mac(c, i + 1u, (col[i] * gir(c, i + 1u)) << 4, o.sf * 12u);
        macs_to_irs(c, o.lm);
        push_color(c);
    } else {
        rgbc(c, col);
        for (i = 0; i < 3u; i++)
            mac[i] = (col[i] * gir(c, i + 1u)) << 4;
        depth_cue(c, mac, o);
    }
}

/* Cadena de color por normal para el vertice vi: kind 0 = NC, 1 = NCC, 2 = NCD. */
static void nc(Cpu *c, uint32_t vi, Op o, uint32_t kind)
{
    int64_t llm[3][3], v[3];
    static const int64_t zero[3] = { 0, 0, 0 };
    gmatrix(c, 40u, llm);
    gvector(c, vi, v);
    mul_mat_vec(c, llm, v, zero, o);
    color_stage(c, o, kind);
}

static void set_otz(Cpu *c, int64_t v)
{
    int64_t cl = clamp64(v, 0, 0xFFFF);
    if (cl != v)
        gflag(c, 18u);
    c->gte_regs[7] = (uint32_t)cl;
}

void ps1_gte_command(Cpu *c, uint32_t w)
{
    Op o;
    uint32_t i, v;
    o.sf = (w >> 19) & 1u;
    o.lm = (w & (1u << 10)) != 0u;
    o.mx = (w >> 17) & 3u;
    o.v = (w >> 15) & 3u;
    o.cv = (w >> 13) & 3u;
    c->gte_regs[FLAG] = 0;

    switch (w & 0x3Fu) {
    case 0x01:                                          /* RTPS */
        rtp(c, 0u, o, 1);
        break;
    case 0x30:                                          /* RTPT */
        rtp(c, 0u, o, 0);
        rtp(c, 1u, o, 0);
        rtp(c, 2u, o, 1);
        break;
    case 0x06: {                                        /* NCLIP */
        int64_t x0 = glo(c, 12u), y0 = ghi(c, 12u);
        int64_t x1 = glo(c, 13u), y1 = ghi(c, 13u);
        int64_t x2 = glo(c, 14u), y2 = ghi(c, 14u);
        set_mac0(c, x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1);
        break;
    }
    case 0x0C: {                                        /* OP */
        int64_t d1 = glo(c, 32u), d2 = glo(c, 34u), d3 = glo(c, 36u);
        int64_t i1 = gir(c, 1), i2 = gir(c, 2), i3 = gir(c, 3);
        set_mac(c, 1u, i3 * d2 - i2 * d3, o.sf * 12u);
        set_mac(c, 2u, i1 * d3 - i3 * d1, o.sf * 12u);
        set_mac(c, 3u, i2 * d1 - i1 * d2, o.sf * 12u);
        macs_to_irs(c, o.lm);
        break;
    }
    case 0x10: {                                        /* DPCS */
        int64_t col[3], mac[3];
        rgbc(c, col);
        for (i = 0; i < 3u; i++)
            mac[i] = col[i] << 16;
        depth_cue(c, mac, o);
        break;
    }
    case 0x2A: {                                        /* DPCT */
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            uint32_t cv = c->gte_regs[20];
            int64_t mac[3];
            mac[0] = (int64_t)(cv & 0xFFu) << 16;
            mac[1] = (int64_t)((cv >> 8) & 0xFFu) << 16;
            mac[2] = (int64_t)((cv >> 16) & 0xFFu) << 16;
            depth_cue(c, mac, o);
        }
        break;
    }
    case 0x11: {                                        /* INTPL */
        int64_t ir[3], mac[3];
        gvector(c, 3u, ir);
        for (i = 0; i < 3u; i++)
            mac[i] = ir[i] << 12;
        depth_cue(c, mac, o);
        break;
    }
    case 0x29: {                                        /* DPCL */
        int64_t col[3], mac[3];
        rgbc(c, col);
        for (i = 0; i < 3u; i++)
            mac[i] = (col[i] * gir(c, i + 1u)) << 4;
        depth_cue(c, mac, o);
        break;
    }
    case 0x12: {                                        /* MVMVA */
        int64_t m[3][3], vec[3], t[3];
        if (o.mx == 0u) {
            gmatrix(c, 32u, m);
        } else if (o.mx == 1u) {
            gmatrix(c, 40u, m);
        } else if (o.mx == 2u) {
            gmatrix(c, 48u, m);
        } else {
            /* matriz basura del hardware */
            int64_t r = (int64_t)(c->gte_regs[6] & 0xFFu) * 16;
            m[0][0] = -r;           m[0][1] = r;            m[0][2] = gir(c, 0);
            m[1][0] = glo(c, 33u);  m[1][1] = glo(c, 33u);  m[1][2] = glo(c, 33u);
            m[2][0] = ghi(c, 34u);  m[2][1] = ghi(c, 34u);  m[2][2] = ghi(c, 34u);
        }
        gvector(c, o.v, vec);
        if (o.cv == 0u) {
            t[0] = gd(c, 37u); t[1] = gd(c, 38u); t[2] = gd(c, 39u);
        } else if (o.cv == 1u) {
            t[0] = gd(c, 45u); t[1] = gd(c, 46u); t[2] = gd(c, 47u);
        } else if (o.cv == 2u) {
            t[0] = gd(c, 53u); t[1] = gd(c, 54u); t[2] = gd(c, 55u);
        } else {
            t[0] = t[1] = t[2] = 0;
        }
        if (o.cv == 2u) {
            /* error del hardware: solo las dos ultimas columnas llegan al MAC; la
             * primera se usa unicamente para poner banderas */
            for (i = 0; i < 3u; i++) {
                int64_t first = (t[i] << 12) + m[i][0] * vec[0];
                set_ir(c, i + 1u, first >> (o.sf * 12u), 0);
                set_mac(c, i + 1u, m[i][1] * vec[1] + m[i][2] * vec[2], o.sf * 12u);
            }
            macs_to_irs(c, o.lm);
        } else {
            mul_mat_vec(c, m, vec, t, o);
        }
        break;
    }
    case 0x13: nc(c, 0u, o, 2u); break;                 /* NCDS */
    case 0x16:                                          /* NCDT */
        for (v = 0; v < 3u; v++)
            nc(c, v, o, 2u);
        break;
    case 0x1B: nc(c, 0u, o, 1u); break;                 /* NCCS */
    case 0x3F:                                          /* NCCT */
        for (v = 0; v < 3u; v++)
            nc(c, v, o, 1u);
        break;
    case 0x1E: nc(c, 0u, o, 0u); break;                 /* NCS */
    case 0x20:                                          /* NCT */
        for (v = 0; v < 3u; v++)
            nc(c, v, o, 0u);
        break;
    case 0x1C: color_stage(c, o, 1u); break;            /* CC */
    case 0x14: color_stage(c, o, 2u); break;            /* CDP */
    case 0x28:                                          /* SQR */
        for (i = 1u; i <= 3u; i++)
            set_mac(c, i, gir(c, i) * gir(c, i), o.sf * 12u);
        macs_to_irs(c, o.lm);
        break;
    case 0x2D: {                                        /* AVSZ3 */
        uint32_t sum = ps1_gte_read(c, 17u) + ps1_gte_read(c, 18u) + ps1_gte_read(c, 19u);
        int64_t m = set_mac0(c, glo(c, 32u + 29u) * (int64_t)sum);
        set_otz(c, m >> 12);
        break;
    }
    case 0x2E: {                                        /* AVSZ4 */
        uint32_t sum = ps1_gte_read(c, 16u) + ps1_gte_read(c, 17u) +
                       ps1_gte_read(c, 18u) + ps1_gte_read(c, 19u);
        int64_t m = set_mac0(c, glo(c, 32u + 30u) * (int64_t)sum);
        set_otz(c, m >> 12);
        break;
    }
    case 0x3D: {                                        /* GPF */
        int64_t ir0 = gir(c, 0);
        for (i = 1u; i <= 3u; i++)
            set_mac(c, i, gir(c, i) * ir0, o.sf * 12u);
        macs_to_irs(c, o.lm);
        push_color(c);
        break;
    }
    case 0x3E: {                                        /* GPL */
        int64_t ir0 = gir(c, 0);
        for (i = 1u; i <= 3u; i++) {
            int64_t acc = ((int64_t)(int32_t)c->gte_regs[24 + i] << (o.sf * 12u)) +
                          gir(c, i) * ir0;
            set_mac(c, i, acc, o.sf * 12u);
        }
        macs_to_irs(c, o.lm);
        push_color(c);
        break;
    }
    default:
        break;
    }

    /* bit 31 de FLAG = OR de los bits de error */
    if (c->gte_regs[FLAG] & 0x7F87E000u)
        c->gte_regs[FLAG] |= 0x80000000u;
}

/* Instruccion COP2: comando del GTE, movimiento de registro o carga/almacenamiento. */
void ps1_gte(Cpu *c, uint32_t w)
{
    uint32_t op = w >> 26;
    uint32_t rs = (w >> 21) & 31u;
    uint32_t rt = (w >> 16) & 31u;
    uint32_t rd = (w >> 11) & 31u;

    if (op == 18u) {
        if (w & (1u << 25)) {
            c->gte_commands++;
            ps1_gte_command(c, w & 0x01FFFFFFu);
            return;
        }
        switch (rs) {
        case 0: {                                   /* mfc2 */
            uint32_t v = ps1_gte_read(c, rd);
            if (rt != 0u)
                c->r[rt] = v;
            return;
        }
        case 2: {                                   /* cfc2 */
            uint32_t v = ps1_gte_read(c, 32u + rd);
            if (rt != 0u)
                c->r[rt] = v;
            return;
        }
        case 4:                                     /* mtc2 */
            ps1_gte_write(c, rd, c->r[rt]);
            return;
        case 6:                                     /* ctc2 */
            ps1_gte_write(c, 32u + rd, c->r[rt]);
            return;
        default:
            return;
        }
    }
    if (op == 50u || op == 58u) {                   /* lwc2 / swc2 */
        uint32_t base = (w >> 21) & 31u;
        uint32_t off = (uint32_t)(int32_t)(int16_t)(uint16_t)(w & 0xFFFFu);
        uint32_t addr = c->r[base] + off;
        if (op == 50u) {
            ps1_gte_write(c, rt, ps1_r32(c, addr));
        } else {
            ps1_w32(c, addr, ps1_gte_read(c, rt));
        }
    }
}
