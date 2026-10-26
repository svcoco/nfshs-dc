/* El SPU de PS1 completo: ver psxspu.h. Traduccion de src/ps1/spu.rs, funcion a funcion
 * y con los mismos nombres de campo, para poder ir y volver entre los dos. */
#include "psxspu.h"

#include <string.h>

#define SPU_BASE   0x1F801C00u
#define DMA4_MADR  0x1F8010C0u
#define DMA4_BCR   0x1F8010C4u
#define DMA4_CHCR  0x1F8010C8u
#define DPCR       0x1F8010F0u
#define DICR       0x1F8010F4u

enum { SPU_OFF, SPU_ATTACK, SPU_DECAY, SPU_SUSTAIN, SPU_RELEASE };

/* Los filtros de prediccion del ADPCM (coeficientes positivo y negativo / 64). */
static const int32_t FILTERS[5][2] = { {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60} };

/* Los pesos de la interpolacion gaussiana de 4 puntos del SPU (psx-spx). */
static const int16_t GAUSS[512] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 21, 22, 24, 25, 27, 28, 30, 32, 33, 35, 37,
    39, 41, 44, 46, 48, 51, 53, 56, 58, 61, 64, 67, 70, 73, 77, 80,
    84, 87, 91, 95, 99, 103, 107, 111, 116, 120, 125, 130, 135, 140, 145, 150,
    156, 161, 167, 173, 179, 186, 192, 199, 205, 212, 219, 227, 234, 242, 250, 257,
    266, 274, 283, 291, 300, 309, 319, 328, 338, 348, 358, 369, 379, 390, 401, 412,
    424, 436, 448, 460, 473, 485, 498, 512, 525, 539, 553, 567, 582, 597, 612, 627,
    643, 659, 675, 692, 708, 726, 743, 761, 779, 797, 816, 835, 854, 874, 894, 914,
    935, 956, 977, 999, 1020, 1043, 1066, 1089, 1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
    1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508, 1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
    1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031, 2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
    2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668, 2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
    3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427, 3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
    3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315, 4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
    4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332, 5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
    5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475, 6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
    7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735, 7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
    8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097, 9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
    9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542, 10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
    11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045, 12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
    12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577, 13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
    14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104, 15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
    15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592, 16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
    17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001, 18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
    18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295, 19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
    19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439, 20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
    21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399, 21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
    21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148, 22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
    22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662, 22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
    22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927, 22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
};

static int32_t sign15(uint16_t v)
{
    return (int32_t)((uint32_t)v << 17) >> 16;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Un paso de envolvente (ADSR o barrido de volumen): `rate` = shift << 2 | step. */
static int32_t env_tick(SpuEnv *e, int32_t level, uint32_t rate, int decreasing, int exponential)
{
    int32_t shift = (int32_t)(rate >> 2);
    int32_t step = decreasing ? -8 + (int32_t)(rate & 3u) : 7 - (int32_t)(rate & 3u);
    int32_t cycles = 1 << (shift - 11 > 0 ? shift - 11 : 0);

    step = (int32_t)((uint32_t)step << (11 - shift > 0 ? 11 - shift : 0));
    if (exponential && !decreasing && level > 0x6000)
        cycles *= 4;
    if (exponential && decreasing)
        step = (step * level) >> 15;
    e->counter -= 1;
    if (e->counter > 0)
        return level;
    e->counter = cycles;
    return clampi(level + step, 0, 0x7FFF);
}

static uint16_t reg(const PsxSpu *s, uint32_t off)
{
    return s->regs[(off >> 1) & 0x1FFu];
}

static uint16_t spucnt(const PsxSpu *s)
{
    return reg(s, 0x1AA);
}

void psxspu_reset(PsxSpu *s)
{
    memset(s, 0, sizeof *s);
    s->noise_level = 1;
    s->dpcr = 0x07654321u;
}

/* La RAM de sonido tocada en [addr, addr + len): la direccion de IRQ dentro la levanta. */
static void check_irq(PsxSpu *s, uint32_t addr, uint32_t len)
{
    uint32_t irq;

    if ((spucnt(s) & 0x40u) == 0u || s->irq_flag)
        return;
    irq = (uint32_t)reg(s, 0x1A4) << 3;
    if (((irq - addr) & (PSXSPU_RAM - 1u)) < len) {
        s->irq_flag = 1;
        s->irqs |= 1u << 9;
    }
}

static void key_on(PsxSpu *s, unsigned v)
{
    uint32_t base = (uint32_t)v << 4;
    SpuVoice *vc = &s->voices[v];

    vc->addr = (uint32_t)reg(s, base + 6u) << 3;
    vc->counter = 0;
    vc->has_block = 0;
    vc->ignore_loop = 0;
    vc->s1 = 0;
    vc->s2 = 0;
    memset(vc->buf, 0, sizeof vc->buf);
    vc->level = 0;
    vc->env.counter = 0;
    vc->phase = SPU_ATTACK;
    s->endx &= ~(1u << v);
    s->keyons |= 1u << v;
}

static void key_off(PsxSpu *s, unsigned v)
{
    SpuVoice *vc = &s->voices[v];

    if (vc->phase != SPU_OFF) {
        vc->phase = SPU_RELEASE;
        vc->env.counter = 0;
    }
}

static void write_reg(PsxSpu *s, uint32_t off, uint16_t v)
{
    off &= 0x3FEu;
    s->regs[off >> 1] = v;
    if (off <= 0x17Fu) {
        unsigned voice = off >> 4;

        switch (off & 0xFu) {
        case 0x0:
        case 0x2:
            /* un volumen fijo vale enseguida */
            if ((v & 0x8000u) == 0u)
                s->voices[voice].vol[(off & 2u) >> 1] = sign15(v);
            break;
        case 0xC:
            s->voices[voice].level = (int16_t)v > 0 ? (int16_t)v : 0;
            break;
        case 0xE: {
            SpuVoice *vc = &s->voices[voice];
            vc->repeat = (uint32_t)v << 3;
            vc->ignore_loop |= vc->phase != SPU_OFF;
            break;
        }
        default:
            break;
        }
        return;
    }
    switch (off) {
    case 0x180:
    case 0x182:
        if ((v & 0x8000u) == 0u)
            s->main_vol[(off & 2u) >> 1] = sign15(v);
        break;
    case 0x188:
    case 0x18A: {
        uint32_t bits = (uint32_t)v << (off == 0x18Au ? 16 : 0);
        for (unsigned i = 0; i < 24u; i++)
            if ((bits >> i) & 1u)
                key_on(s, i);
        break;
    }
    case 0x18C:
    case 0x18E: {
        uint32_t bits = (uint32_t)v << (off == 0x18Cu ? 0 : 16);
        for (unsigned i = 0; i < 24u; i++)
            if ((bits >> i) & 1u)
                key_off(s, i);
        break;
    }
    case 0x1A2:
        s->reverb_addr = (uint32_t)v << 3;
        s->reverb_pos = s->reverb_addr;
        break;
    case 0x1A6:
        s->transfer_addr = (uint32_t)v << 3;
        break;
    case 0x1A8: {
        /* escritura manual por la FIFO de datos */
        uint32_t a = s->transfer_addr & (PSXSPU_RAM - 2u);
        s->ram[a] = (uint8_t)v;
        s->ram[a + 1u] = (uint8_t)(v >> 8);
        check_irq(s, a, 2);
        s->transfer_addr = (s->transfer_addr + 2u) & (PSXSPU_RAM - 1u);
        break;
    }
    case 0x1AA:
        if ((v & 0x40u) == 0u)
            s->irq_flag = 0;
        break;
    default:
        break;
    }
}

static uint16_t read_reg(const PsxSpu *s, uint32_t off)
{
    off &= 0x3FEu;
    if (off <= 0x17Fu && (off & 0xFu) == 0xCu)
        return (uint16_t)s->voices[off >> 4].level;
    if (off <= 0x17Fu && (off & 0xFu) == 0xEu)
        return (uint16_t)(s->voices[off >> 4].repeat >> 3);
    switch (off) {
    case 0x19C:
        return (uint16_t)s->endx;
    case 0x19E:
        return (uint16_t)(s->endx >> 16);
    case 0x1A6:
        return (uint16_t)(s->transfer_addr >> 3);
    case 0x1AE: {
        uint16_t cnt = spucnt(s), dma = (uint16_t)((cnt >> 4) & 3u);
        return (uint16_t)((cnt & 0x3Fu) | (uint16_t)(s->irq_flag << 6) |
                          (uint16_t)((dma >= 2u) << 7) | (uint16_t)((dma == 2u) << 8) |
                          (uint16_t)((dma == 3u) << 9));
    }
    case 0x1B8:
    case 0x1BA:
        return (uint16_t)s->main_vol[(off & 2u) >> 1];
    default:
        if (off >= 0x200u && off <= 0x25Fu)
            return (uint16_t)s->voices[(off - 0x200u) >> 2].vol[(off & 2u) >> 1];
        return reg(s, off);
    }
}

/* El canal 4 de DMA: `words` palabras entre la RAM principal en MADR y la de sonido en la
 * direccion de transferencia. */
static void dma4(PsxSpu *s, uint8_t *ram, uint32_t ram_size)
{
    uint32_t madr = s->dma4[0], bcr = s->dma4[1], chcr = s->dma4[2];
    uint32_t words, src, start = s->transfer_addr;

    if (((chcr >> 9) & 3u) == 0u)
        words = (bcr & 0xFFFFu) ? (bcr & 0xFFFFu) : 0x10000u;
    else
        words = (bcr & 0xFFFFu) * (bcr >> 16);
    src = madr & 0x1FFFFCu;
    for (uint32_t k = 0; k < words * 4u; k++) {
        uint32_t a = s->transfer_addr;
        if (chcr & 1u)
            s->ram[a] = ram[src & (ram_size - 1u)];
        else
            ram[src & (ram_size - 1u)] = s->ram[a];
        src++;
        s->transfer_addr = (s->transfer_addr + 1u) & (PSXSPU_RAM - 1u);
    }
    check_irq(s, start, words * 4u);
    if (((chcr >> 9) & 3u) == 1u) {
        s->dma4[0] = src;
        s->dma4[1] &= 0xFFFFu;
    }
    s->dma4[2] &= ~0x01000000u;
    if (s->dicr & (1u << 20))
        s->dicr |= 1u << 28;
    s->irqs |= 1u << 20;
}

/* El bloque ADPCM siguiente de la voz `v`, a su buffer. */
static void decode_block(PsxSpu *s, unsigned v)
{
    SpuVoice *vc = &s->voices[v];
    uint32_t addr = vc->addr & (PSXSPU_RAM - 16u);
    const uint8_t *b;
    int32_t shift, k0, k1;
    unsigned f;

    check_irq(s, addr, 16);
    b = &s->ram[addr];
    shift = (b[0] & 0xFu) <= 12u ? (int32_t)(b[0] & 0xFu) : 9;
    f = (unsigned)(b[0] >> 4) < 4u ? (unsigned)(b[0] >> 4) : 4u;
    k0 = FILTERS[f][0];
    k1 = FILTERS[f][1];
    vc->block_flags = b[1];
    vc->buf[0] = vc->buf[28];
    vc->buf[1] = vc->buf[29];
    vc->buf[2] = vc->buf[30];
    for (unsigned i = 0; i < 28u; i++) {
        uint8_t byte = b[2 + i / 2u];
        int32_t nib = (i & 1u) == 0u ? (byte & 0xF) : (byte >> 4);
        int32_t raw = (int32_t)((uint32_t)((int32_t)((uint32_t)nib << 28) >> 28) << 12) >> shift;
        int32_t smp = clampi(raw + ((vc->s1 * k0 + vc->s2 * k1 + 32) >> 6), -32768, 32767);
        vc->s2 = vc->s1;
        vc->s1 = smp;
        vc->buf[3 + i] = (int16_t)smp;
    }
    if ((vc->block_flags & 4u) && !vc->ignore_loop)
        vc->repeat = addr;
    vc->has_block = 1;
}

static void adsr(PsxSpu *s, unsigned v)
{
    uint32_t base = (uint32_t)v << 4;
    uint32_t lo = reg(s, base + 8u), hi = reg(s, base + 10u);
    SpuVoice *vc = &s->voices[v];

    switch (vc->phase) {
    case SPU_ATTACK:
        vc->level = env_tick(&vc->env, vc->level, (lo >> 8) & 0x7Fu, 0, (lo & 0x8000u) != 0u);
        if (vc->level >= 0x7FFF) {
            vc->phase = SPU_DECAY;
            vc->env.counter = 0;
        }
        break;
    case SPU_DECAY: {
        int32_t sustain = (int32_t)(((lo & 0xFu) + 1u) * 0x800u);
        vc->level = env_tick(&vc->env, vc->level, ((lo >> 4) & 0xFu) << 2, 1, 1);
        if (vc->level <= sustain) {
            vc->phase = SPU_SUSTAIN;
            vc->env.counter = 0;
        }
        break;
    }
    case SPU_SUSTAIN:
        vc->level = env_tick(&vc->env, vc->level, (hi >> 6) & 0x7Fu, (hi & 0x4000u) != 0u,
                             (hi & 0x8000u) != 0u);
        break;
    case SPU_RELEASE:
        vc->level = env_tick(&vc->env, vc->level, (hi & 0x1Fu) << 2, 1, (hi & 0x20u) != 0u);
        if (vc->level == 0)
            vc->phase = SPU_OFF;
        break;
    default:
        break;
    }
}

/* Un registro de volumen en modo barrido mueve el volumen actual un paso. */
static int32_t sweep(SpuEnv *env, int32_t cur, uint16_t r)
{
    int decreasing, exponential, negative;
    int32_t mag, m;

    if ((r & 0x8000u) == 0u)
        return sign15(r);
    decreasing = (r & 0x2000u) != 0u;
    exponential = (r & 0x4000u) != 0u;
    negative = (r & 0x1000u) != 0u;
    mag = clampi(negative ? -cur : cur, 0, 0x7FFF);
    m = env_tick(env, mag, r & 0x7Fu, decreasing, exponential);
    return negative ? -m : m;
}

static void noise_step(PsxSpu *s)
{
    uint16_t cnt = spucnt(s);
    int32_t shift = (cnt >> 10) & 0xF, step = ((cnt >> 8) & 3) + 4;

    s->noise_timer -= step;
    if (s->noise_timer < 0) {
        uint16_t l = s->noise_level;
        uint16_t parity = (uint16_t)(((l >> 15) ^ (l >> 12) ^ (l >> 11) ^ (l >> 10) ^ 1u) & 1u);
        s->noise_level = (uint16_t)((l << 1) | parity);
        s->noise_timer += 0x20000 >> shift;
        if (s->noise_timer < 0)
            s->noise_timer += 0x20000 >> shift;
    }
}

/* La muestra siguiente de una voz tras su envolvente; 0 en *suena si esta callada. */
static int32_t voice_sample(PsxSpu *s, unsigned v, int32_t prev_out, int *suena)
{
    SpuVoice *vc = &s->voices[v];
    uint32_t base = (uint32_t)v << 4, step, pmon, non;
    uint32_t i, f;
    int32_t interp, sample, out;

    if (vc->phase == SPU_OFF) {
        vc->last_out = 0;
        *suena = 0;
        return 0;
    }
    if (!vc->has_block)
        decode_block(s, v);
    step = reg(s, base + 4u);
    if (step > 0x4000u)
        step = 0x4000u;
    pmon = reg(s, 0x190) | ((uint32_t)reg(s, 0x192) << 16);
    if (v > 0u && ((pmon >> v) & 1u)) {
        int64_t factor = (int64_t)prev_out + 0x8000;
        int64_t st = ((int64_t)step * factor) >> 15;
        step = (uint32_t)(st < 0 ? 0 : st > 0x3FFF ? 0x3FFF : st);
    }
    non = reg(s, 0x194) | ((uint32_t)reg(s, 0x196) << 16);
    if (vc->level == 0 || s->solo_estado) {
        out = 0;                            /* (x * 0) >> 15: sin interpolar, mismo resultado */
    } else {
        /* la gaussiana sobre las muestras i-3..i (la interpolacion del chip) */
        i = vc->counter >> 12;
        f = (vc->counter >> 4) & 0xFFu;
        interp = ((GAUSS[0xFF - f] * vc->buf[i]) >> 15) + ((GAUSS[0x1FF - f] * vc->buf[i + 1]) >> 15) +
                 ((GAUSS[0x100 + f] * vc->buf[i + 2]) >> 15) + ((GAUSS[f] * vc->buf[i + 3]) >> 15);
        interp = clampi(interp, -32768, 32767);
        sample = ((non >> v) & 1u) ? (int32_t)(int16_t)s->noise_level : interp;
        out = (sample * vc->level) >> 15;
    }
    vc->last_out = out;
    vc->counter += step;
    if ((vc->counter >> 12) >= 28u) {
        /* bloque acabado: al siguiente, o a la direccion de repeticion / silencio si es el final */
        uint8_t flags = vc->block_flags;
        vc->counter -= 28u << 12;
        vc->has_block = 0;
        if (flags & 1u) {
            vc->addr = vc->repeat;
            if ((flags & 2u) == 0u) {
                vc->phase = SPU_OFF;
                vc->level = 0;
            }
            s->endx |= 1u << v;
        } else {
            vc->addr = (vc->addr + 16u) & (PSXSPU_RAM - 1u);
        }
    }
    adsr(s, v);
    *suena = 1;
    return out;
}

/* Muestra del buffer de reverb a `off` bytes de la posicion actual (da la vuelta en el
 * buffer); -1 si no hay buffer. */
static int32_t rv_addr(const PsxSpu *s, int64_t off)
{
    int32_t base = (int32_t)s->reverb_addr, size = (int32_t)PSXSPU_RAM - base, r;

    if (size <= 0)
        return -1;
    /* El rem_euclid de la referencia, sin division de 64 bits (en SH-4 es por software y
     * la reverb la hace ~20 veces por muestra): los desplazamientos caben en 32 bits y casi
     * siempre basta con una vuelta; si no, el modulo de 32. */
    r = (int32_t)s->reverb_pos - base + (int32_t)off;
    if (r >= size) {
        r -= size;
        if (r >= size)
            r %= size;
    } else if (r < 0) {
        r += size;
        if (r < 0) {
            r %= size;
            if (r < 0)
                r += size;
        }
    }
    return (int32_t)((uint32_t)(base + r) & (PSXSPU_RAM - 2u));
}

static int32_t rv(const PsxSpu *s, int64_t off)
{
    int32_t a = rv_addr(s, off);
    return a < 0 ? 0 : (int16_t)(s->ram[a] | (s->ram[a + 1] << 8));
}

static void rv_write(PsxSpu *s, int64_t off, int32_t v)
{
    int32_t a = rv_addr(s, off);
    if (a >= 0) {
        int16_t x = (int16_t)clampi(v, -32768, 32767);
        s->ram[a] = (uint8_t)x;
        s->ram[a + 1] = (uint8_t)((uint16_t)x >> 8);
    }
}

#define MUL(a, b) (((a) * (b)) >> 15)

/* La unidad de reverb (22,05 kHz): los dos lados desde la entrada, segun psx-spx. */
static void reverb(PsxSpu *s)
{
    const uint16_t *r = &s->regs[0x1C0 >> 1];
#define M(i)   ((int64_t)r[i] << 3)
#define VOL(i) ((int32_t)(int16_t)r[i])
    int32_t lin = MUL(s->reverb_in[0], VOL(30)), rin = MUL(s->reverb_in[1], VOL(31));
    int64_t d_apf1 = M(0), d_apf2 = M(1);
    int32_t v_iir = VOL(2), v_wall = VOL(7), v_apf1 = VOL(8), v_apf2 = VOL(9);
    int enabled = (spucnt(s) & 0x80u) != 0u;

    if (enabled) {
        const int64_t tgt[4] = { M(10), M(11), M(18), M(19) };
        const int32_t in[4] = { lin, rin, lin, rin };
        const int64_t src[4] = { M(16), M(17), M(25), M(24) };
        for (int k = 0; k < 4; k++) {
            int32_t prev = rv(s, tgt[k] - 2);
            int32_t v = MUL(in[k] + MUL(rv(s, src[k]), v_wall) - prev, v_iir) + prev;
            rv_write(s, tgt[k], v);
        }
    }
    for (int side = 0; side < 2; side++) {
        const int64_t combs[4] = { M(12 + side), M(14 + side), M(20 + side), M(22 + side) };
        const int64_t apf[2] = { M(26 + side), M(28 + side) };
        const int64_t d[2] = { d_apf1, d_apf2 };
        const int32_t vv[2] = { v_apf1, v_apf2 };
        int32_t o = 0;

        for (int k = 0; k < 4; k++)
            o += MUL(VOL(3 + k), rv(s, combs[k]));
        o = clampi(o, -32768, 32767);
        for (int k = 0; k < 2; k++) {
            int32_t t = rv(s, apf[k] - d[k]);
            o = clampi(o - MUL(vv[k], t), -32768, 32767);
            if (enabled)
                rv_write(s, apf[k], o);
            o = clampi(MUL(o, vv[k]) + t, -32768, 32767);
        }
        s->reverb_out[side] = MUL(o, (int32_t)(int16_t)reg(s, 0x184u + 2u * (uint32_t)side));
    }
#undef M
#undef VOL
    /* a la siguiente posicion del buffer */
    s->reverb_pos += 2u;
    if (s->reverb_pos >= PSXSPU_RAM)
        s->reverb_pos = s->reverb_addr;
}

void psxspu_run(PsxSpu *s, int16_t *out, unsigned n)
{
    for (unsigned k = 0; k < n; k++) {
        int32_t l = 0, r = 0, rl = 0, rr = 0, prev = 0;
        uint32_t eon = reg(s, 0x198) | ((uint32_t)reg(s, 0x19A) << 16);
        uint16_t cnt;
        int32_t ol = 0, orr = 0;

        noise_step(s);
        for (unsigned v = 0; v < 24u; v++) {
            int suena;
            int32_t smp = voice_sample(s, v, prev, &suena);
            uint32_t base = (uint32_t)v << 4;
            SpuVoice *vc = &s->voices[v];

            prev = vc->last_out;
            for (int side = 0; side < 2; side++)
                vc->vol[side] = sweep(&vc->vol_env[side], vc->vol[side], reg(s, base + 2u * (uint32_t)side));
            if (!suena)
                continue;
            {
                int32_t a = (smp * vc->vol[0]) >> 15, b = (smp * vc->vol[1]) >> 15;
                l += a;
                r += b;
                if ((eon >> v) & 1u) {
                    rl += a;
                    rr += b;
                }
            }
        }
        if (s->solo_estado) {
            s->odd = !s->odd;
            for (int side = 0; side < 2; side++)
                s->main_vol[side] = sweep(&s->main_env[side], s->main_vol[side], reg(s, 0x180u + 2u * (uint32_t)side));
            out[2 * k] = out[2 * k + 1] = 0;
            continue;
        }
        /* la reverb, a media frecuencia */
        s->reverb_in[0] = s->odd ? (s->reverb_in[0] + rl) >> 1 : rl;
        s->reverb_in[1] = s->odd ? (s->reverb_in[1] + rr) >> 1 : rr;
        if (s->odd)
            reverb(s);
        s->odd = !s->odd;
        for (int side = 0; side < 2; side++)
            s->main_vol[side] = sweep(&s->main_env[side], s->main_vol[side], reg(s, 0x180u + 2u * (uint32_t)side));
        cnt = spucnt(s);
        if ((cnt & 0x8000u) && (cnt & 0x4000u)) {
            ol = ((clampi(l, -32768, 32767) * s->main_vol[0]) >> 15) + s->reverb_out[0];
            orr = ((clampi(r, -32768, 32767) * s->main_vol[1]) >> 15) + s->reverb_out[1];
        }
        out[2 * k] = (int16_t)clampi(ol, -32768, 32767);
        out[2 * k + 1] = (int16_t)clampi(orr, -32768, 32767);
    }
}

uint32_t psxspu_read(PsxSpu *s, uint32_t a, unsigned n)
{
    uint32_t word;

    if (a >= SPU_BASE && a < SPU_BASE + 0x400u) {
        uint32_t off = a - SPU_BASE;
        if (n == 4u)
            return read_reg(s, off) | ((uint32_t)read_reg(s, off + 2u) << 16);
        if (n == 2u)
            return read_reg(s, off);
        return (uint32_t)(read_reg(s, off) >> ((a & 1u) * 8u)) & 0xFFu;
    }
    switch (a & ~3u) {
    case DMA4_MADR: word = s->dma4[0]; break;
    case DMA4_BCR:  word = s->dma4[1]; break;
    case DMA4_CHCR: word = s->dma4[2]; break;
    case DPCR:      word = s->dpcr; break;
    case DICR:      word = s->dicr; break;
    default:        word = 0; break;
    }
    return word >> ((a & 3u) * 8u);
}

void psxspu_write(PsxSpu *s, uint32_t a, uint32_t v, unsigned n, uint8_t *ram, uint32_t ram_size)
{
    switch (a) {
    case DMA4_MADR:
        s->dma4[0] = v & 0xFFFFFFu;
        return;
    case DMA4_BCR:
        s->dma4[1] = v;
        return;
    case DMA4_CHCR:
        s->dma4[2] = v;
        if (v & 0x01000000u)
            dma4(s, ram, ram_size);
        return;
    case DPCR:
        s->dpcr = v;
        return;
    case DICR: {
        /* las banderas (24..30) se reconocen escribiendo 1 */
        uint32_t flags = s->dicr & 0x7F000000u & ~(v & 0x7F000000u);
        s->dicr = (v & 0x00FF803Fu) | flags;
        return;
    }
    default:
        break;
    }
    if (a >= SPU_BASE && a < SPU_BASE + 0x400u) {
        uint32_t off = a - SPU_BASE;
        if (n == 4u) {
            write_reg(s, off, (uint16_t)v);
            write_reg(s, off + 2u, (uint16_t)(v >> 16));
        } else {
            write_reg(s, off, (uint16_t)v);
        }
    }
}

uint32_t psxspu_take_irqs(PsxSpu *s)
{
    uint32_t i = s->irqs;
    s->irqs = 0;
    return i;
}
