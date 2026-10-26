/* El middleware de audio en Dreamcast (M4): las peticiones del driver de EA a su hardware,
 * hechas sonar con el AICA.
 *
 * Decision de Jorge (docs/hitos/M4.md, secciones 5 y 7): no se emula el SPU; la logica de
 * EA sigue corriendo recompilada y lo que pide al hardware llega aqui por la salida del
 * stub del SPU (host/spu.h, SpuSink):
 *
 *   - SUBIDA de muestras (DMA o FIFO): cada bloque ADPCM de PS1 (16 bytes, 28 muestras) se
 *     decodifica a PCM16 en un ESPEJO dentro de la RAM del AICA: el bloque b va a
 *     base + b * 56. Los 512 KB de RAM de sonido de PS1 son 1,75 MB de PCM16.
 *   - KEY-ON de una voz: se buscan en los bloques el final de la muestra (bandera 1) y su
 *     bucle (bandera 2, con el inicio en el ultimo bloque con bandera 4, o en el registro de
 *     repeticion), y se arranca un canal del AICA en el espejo.
 *   - TONO, VOLUMEN y PANORAMA: se trasladan al canal cuando cambian (tono 0x1000 = 44,1 kHz).
 *   - KEY-OFF: se calla el canal.
 *   - El driver LEE el nivel de envolvente de cada voz para saber si termino: se responde si
 *     el canal sigue sonando (por tiempo, en las muestras sin bucle).
 *
 * Lo que queda fuera de esta primera version, a proposito: la forma del ADSR (se aplica el
 * volumen entero desde el key-on y el key-off calla en seco), la reverb, y los barridos de
 * volumen (se mantiene el ultimo volumen fijo).
 */
#include <kos.h>
#include <dc/sound/sound.h>
#include <dc/sound/aica_comm.h>
#include <dc/sound/sfxmgr.h>
#include <dc/sound/stream.h>
#include <dc/spu.h>

#include <stdio.h>
#include <string.h>

#include "aica.h"
#include "aica_spu.h"
#include "spu.h"

#define BLOQUES    (PS1_SPU_RAM_SIZE / 16u)        /* 32 768 bloques de 16 bytes */
#define PCM_BLOQUE 56u                             /* 28 muestras de 16 bits */
#define VOCES      24u
#define LARGO_MAX  65534u                          /* LSA/LEA del AICA son de 16 bits */

static const int32_t FILTROS[5][2] = { {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60} };

typedef struct {
    uint8_t  suena;
    uint8_t  bucle;
    int      canal;                                /* canal del AICA */
    uint32_t largo;                                /* muestras */
    uint32_t freq;                                 /* Hz */
    uint32_t vol, pan;                             /* 0-255 */
    uint64_t fin_us;                               /* sin bucle: cuando acaba */
    uint16_t vl, vr;                               /* ultimos volumenes fijos (magnitud) */
    /* VOZ LARGA (no cabe en un canal del AICA, que llega a 65 534 muestras: la musica en
     * stream): la mezcla el SH-4 desde la RAM de sonido, como el SPU pero sin gaussiana. */
    uint8_t  sw;
    uint8_t  tiene, flags;
    uint32_t addr, repeat, counter;
    int32_t  s1, s2;
    int16_t  buf[28];
} Voz;

static uint32_t s_base;                            /* el espejo en la RAM del AICA; 0: no hay */
static int16_t  s_hist[BLOQUES][2];                /* las dos ultimas muestras de cada bloque */
static int16_t  s_pcm[28u * 64u];                  /* bloques decodificados antes de subirlos */
static Voz      s_v[VOCES];
static uint16_t s_main[2] = { 0x7FFF, 0x7FFF };
static SpuSink  s_sink;
static snd_stream_hnd_t s_mezcla = SND_STREAM_INVALID;   /* las voces largas */
static int16_t  s_mez[4096 * 2] __attribute__((aligned(32)));

/* estadisticas, para el informe */
static uint32_t s_keyons, s_bloques, s_largos;

/* ---- muestras ---- */

static void decodifica(const uint8_t *b, int32_t *s1, int32_t *s2, int16_t *out)
{
    int32_t shift = (b[0] & 0xFu) <= 12u ? (int32_t)(b[0] & 0xFu) : 9;
    unsigned f = (unsigned)(b[0] >> 4) < 4u ? (unsigned)(b[0] >> 4) : 4u;
    int32_t k0 = FILTROS[f][0], k1 = FILTROS[f][1];

    for (unsigned i = 0; i < 28u; i++) {
        uint8_t byte = b[2 + i / 2u];
        int32_t nib = (i & 1u) == 0u ? (byte & 0xF) : (byte >> 4);
        int32_t raw = (int32_t)((uint32_t)((int32_t)((uint32_t)nib << 28) >> 28) << 12) >> shift;
        int32_t v = raw + ((*s1 * k0 + *s2 * k1 + 32) >> 6);
        v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
        *s2 = *s1;
        *s1 = v;
        out[i] = (int16_t)v;
    }
}

/* La RAM de sonido cambio: los bloques tocados, a PCM16 en el espejo. La historia del
 * decodificador sale del bloque anterior (lo que el SPU haria al tocarlos seguidos). */
static void subida(void *ctx, const uint8_t *ram, uint32_t addr, uint32_t len)
{
    uint32_t b0 = addr / 16u, b1 = (addr + len + 15u) / 16u, b = b0;

    (void)ctx;
    if (s_base == 0u || len == 0u)
        return;
    while (b < b1) {
        uint32_t n = 0, inicio = b % BLOQUES;
        while (b < b1 && n < 64u && (b % BLOQUES) >= inicio) {
            uint32_t k = b % BLOQUES, prev = (k + BLOQUES - 1u) % BLOQUES;
            int32_t s1 = s_hist[prev][0], s2 = s_hist[prev][1];
            decodifica(&ram[k * 16u], &s1, &s2, &s_pcm[n * 28u]);
            s_hist[k][0] = (int16_t)s1;
            s_hist[k][1] = (int16_t)s2;
            n++;
            b++;
            if (k == BLOQUES - 1u)
                break;                                /* da la vuelta: otro tramo */
        }
        spu_memload(s_base + inicio * PCM_BLOQUE, s_pcm, n * PCM_BLOQUE);
        s_bloques += n;
    }
}

/* ---- canales ---- */

static void canal(int ch, uint32_t cmd, uint32_t base, uint32_t largo, int bucle,
                  uint32_t lstart, uint32_t freq, uint32_t vol, uint32_t pan)
{
    AICA_CMDSTR_CHANNEL(tmp, c, chan);

    c->cmd = AICA_CMD_CHAN;
    c->timestamp = 0;
    c->size = AICA_CMDSTR_CHANNEL_SIZE;
    c->cmd_id = (uint32_t)ch;
    chan->cmd = cmd;
    chan->base = base;
    chan->type = AICA_SM_16BIT;
    chan->length = largo;
    chan->loop = (uint32_t)bucle;
    chan->loopstart = lstart;
    chan->loopend = largo;
    chan->freq = freq;
    chan->vol = vol;
    chan->pan = pan;
    snd_sh4_to_aica(tmp, c->size);
}

/* La magnitud de un volumen fijo de PS1 (15 bits con signo: el signo invierte la fase);
 * en modo barrido se queda el ultimo fijo. */
static uint32_t mag(uint16_t r, uint16_t antes)
{
    int32_t s;

    if (r & 0x8000u)
        return antes;
    s = (int32_t)((uint32_t)r << 17) >> 16;
    return (uint32_t)(s < 0 ? -s : s);
}

/* Volumen y panorama del AICA desde los volumenes de la voz y el principal. */
static void volumen(Voz *v, const uint16_t *regs, unsigned n)
{
    uint32_t l, r, m;

    v->vl = (uint16_t)mag(regs[n * 8u + 0u], v->vl);
    v->vr = (uint16_t)mag(regs[n * 8u + 1u], v->vr);
    l = v->vl;
    r = v->vr;
    m = (s_main[0] > s_main[1] ? s_main[0] : s_main[1]);
    v->vol = ((l > r ? l : r) * 255u / 0x7FFFu) * m / 0x7FFFu;
    if (v->vol > 255u)
        v->vol = 255u;
    v->pan = (l + r) ? (r * 255u) / (l + r) : 128u;
}

static uint32_t frecuencia(const uint16_t *regs, unsigned n)
{
    uint32_t p = regs[n * 8u + 2u];
    if (p > 0x4000u)
        p = 0x4000u;
    return (p * 44100u) / 4096u;
}

static void key_on(const uint16_t *regs, unsigned n, const uint8_t *ram)
{
    Voz *v = &s_v[n];
    uint32_t sb = ((uint32_t)regs[n * 8u + 3u] << 3) / 16u % BLOQUES;
    uint32_t eb = sb, lb = BLOQUES, k;
    int bucle = 0;

    /* hasta el bloque con la bandera de fin (o hasta el tope del AICA) */
    for (k = 0; k < LARGO_MAX / 28u; k++) {
        uint32_t b = (sb + k) % BLOQUES;
        uint8_t flags = ram[b * 16u + 1u];
        if (flags & 4u)
            lb = b;
        if (flags & 1u) {
            eb = b;
            bucle = (flags & 2u) != 0u;
            break;
        }
        eb = b;
    }
    if (v->suena && !v->sw)
        canal(v->canal, AICA_CH_CMD_STOP, 0, 0, 0, 0, 0, 0, 0);
    v->sw = 0;
    if (k == LARGO_MAX / 28u) {
        /* no cabe en un canal del AICA: a la mezcla del SH-4 */
        s_largos++;
        v->sw = 1;
        v->addr = sb * 16u;
        v->repeat = lb != BLOQUES ? lb * 16u : ((uint32_t)regs[n * 8u + 7u] << 3);
        v->counter = 0;
        v->tiene = 0;
        v->s1 = v->s2 = 0;
        volumen(v, regs, n);
        v->suena = 1;
        s_keyons++;
        printf("audio: voz %u en %#lx sin fin en %u bloques (tono %#x, repeticion %#lx)\n", n,
               (unsigned long)(sb * 16u), (unsigned)k, (unsigned)regs[n * 8u + 2u],
               (unsigned long)((uint32_t)regs[n * 8u + 7u] << 3));
        return;
    }
    if (bucle && (lb == BLOQUES || lb < sb)) {
        uint32_t rep = ((uint32_t)regs[n * 8u + 7u] << 3) / 16u % BLOQUES;
        lb = (rep >= sb && rep <= eb) ? rep : sb;
    }
    if (eb < sb)
        eb = BLOQUES - 1u;                         /* no da la vuelta al final de la RAM */
    v->largo = (eb - sb + 1u) * 28u;
    if (v->largo > LARGO_MAX)
        v->largo = LARGO_MAX;
    v->bucle = (uint8_t)bucle;
    v->freq = frecuencia(regs, n);
    volumen(v, regs, n);
    if (v->freq == 0u) {
        v->suena = 0;
        return;
    }
    if (s_keyons < 40u)
        printf("audio: key-on v%u en %#lx: %lu muestras%s (desde %lu) a %lu Hz, vol %lu pan %lu\n", n,
               (unsigned long)(sb * 16u), (unsigned long)v->largo, bucle ? " en bucle" : "",
               (unsigned long)(bucle ? (lb - sb) * 28u : 0u), (unsigned long)v->freq,
               (unsigned long)v->vol, (unsigned long)v->pan);
    canal(v->canal, AICA_CH_CMD_START, s_base + sb * PCM_BLOQUE, v->largo, bucle,
          bucle ? (lb - sb) * 28u : 0u, v->freq, v->vol, v->pan);
    v->suena = 1;
    v->fin_us = timer_us_gettime64() + (uint64_t)v->largo * 1000000u / v->freq;
    s_keyons++;
}

static void key_off(unsigned n)
{
    Voz *v = &s_v[n];

    if (!v->suena)
        return;
    if (!v->sw)
        canal(v->canal, AICA_CH_CMD_STOP, 0, 0, 0, 0, 0, 0, 0);
    v->suena = 0;
}

static SpuStub *s_spu;                             /* para leer la RAM de sonido en el key-on */

static void registro(void *ctx, const uint16_t *regs, uint32_t off, uint16_t val)
{
    (void)ctx;
    if (s_base == 0u)
        return;
    if (off < 0x180u) {
        unsigned n = off >> 4;
        Voz *v = &s_v[n];
        switch (off & 0xFu) {
        case 0x0:
        case 0x2:
            if (v->suena && v->sw) {
                volumen(v, regs, n);
            } else if (v->suena) {
                uint32_t vol = v->vol, pan = v->pan;
                volumen(v, regs, n);
                if (vol != v->vol || pan != v->pan)
                    canal(v->canal, AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_VOL | AICA_CH_UPDATE_SET_PAN,
                          0, 0, 0, 0, 0, v->vol, v->pan);
            }
            break;
        case 0x4:
            if (v->suena && !v->sw) {
                uint32_t f = frecuencia(regs, n);
                if (f != v->freq && f != 0u) {
                    v->freq = f;
                    canal(v->canal, AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_FREQ, 0, 0, 0, 0, f, 0, 0);
                }
            }
            break;
        default:
            break;
        }
        return;
    }
    switch (off) {
    case 0x180:
    case 0x182:
        if ((val & 0x8000u) == 0u)
            s_main[(off - 0x180u) >> 1] = (uint16_t)mag(val, 0x7FFF);
        break;
    case 0x188:
    case 0x18A:
        for (unsigned i = 0; i < 16u; i++)
            if ((val >> i) & 1u) {
                unsigned n = i + (off == 0x18Au ? 16u : 0u);
                if (n < VOCES)
                    key_on(regs, n, s_spu->ram);
            }
        break;
    case 0x18C:
    case 0x18E:
        for (unsigned i = 0; i < 16u; i++)
            if ((val >> i) & 1u) {
                unsigned n = i + (off == 0x18Eu ? 16u : 0u);
                if (n < VOCES)
                    key_off(n);
            }
        break;
    default:
        break;
    }
}

static uint16_t nivel(void *ctx, unsigned n)
{
    Voz *v;

    (void)ctx;
    if (n >= VOCES)
        return 0;
    v = &s_v[n];
    if (v->suena && !v->bucle && timer_us_gettime64() >= v->fin_us)
        v->suena = 0;                              /* la muestra ya acabo */
    return v->suena ? 0x7FFFu : 0u;
}

/* ---- voces largas: la mezcla del SH-4 ---- */

/* Una muestra de la voz larga `n` (y avanza como el SPU: tono 0x1000 = una muestra por
 * cada una de salida a 44,1 kHz; al acabar el bloque, al siguiente o a la repeticion). */
static int32_t muestra_larga(Voz *v, unsigned n)
{
    const uint8_t *ram = s_spu->ram;
    uint32_t pitch = s_spu->reg[n * 8u + 2u], i, f;
    int32_t a, b;

    if (pitch > 0x4000u)
        pitch = 0x4000u;
    if (!v->tiene) {
        const uint8_t *blk = &ram[v->addr & (PS1_SPU_RAM_SIZE - 16u)];
        v->flags = blk[1];
        decodifica(blk, &v->s1, &v->s2, v->buf);
        if (v->flags & 4u)
            v->repeat = v->addr;
        v->tiene = 1;
    }
    i = v->counter >> 12;
    f = v->counter & 0xFFFu;
    a = v->buf[i];
    b = i < 27u ? v->buf[i + 1] : a;
    v->counter += pitch;
    if ((v->counter >> 12) >= 28u) {
        v->counter -= 28u << 12;
        v->tiene = 0;
        if (v->flags & 1u) {
            v->addr = v->repeat;
            if ((v->flags & 2u) == 0u)
                v->suena = 0;
        } else {
            v->addr = (v->addr + 16u) & (PS1_SPU_RAM_SIZE - 1u);
        }
    }
    return a + (((b - a) * (int32_t)f) >> 12);
}

static void *mezcla_cb(snd_stream_hnd_t hnd, int bytes, int *got)
{
    uint32_t pares = (uint32_t)bytes / 4u;
    int32_t ml = s_main[0], mr = s_main[1];

    unsigned lista[VOCES], nl = 0;

    (void)hnd;
    if (pares > 4096u)
        pares = 4096u;
    for (unsigned n = 0; n < VOCES; n++)
        if (s_v[n].suena && s_v[n].sw)
            lista[nl++] = n;
    if (nl == 0u) {
        memset(s_mez, 0, pares * 4u);
        *got = (int)(pares * 4u);
        return s_mez;
    }
    for (uint32_t k = 0; k < pares; k++) {
        int32_t l = 0, r = 0;
        for (unsigned j = 0; j < nl; j++) {
            Voz *v = &s_v[lista[j]];
            int32_t s;
            if (!v->suena)
                continue;
            s = muestra_larga(v, lista[j]);
            l += (s * (int32_t)v->vl) >> 15;
            r += (s * (int32_t)v->vr) >> 15;
        }
        l = (l * ml) >> 15;
        r = (r * mr) >> 15;
        s_mez[2 * k] = (int16_t)(l < -32768 ? -32768 : l > 32767 ? 32767 : l);
        s_mez[2 * k + 1] = (int16_t)(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
    }
    *got = (int)(pares * 4u);
    return s_mez;
}

void aica_spu_poll(void)
{
    if (s_mezcla != SND_STREAM_INVALID)
        snd_stream_poll(s_mezcla);
}

int aica_spu_init(SpuStub *spu)
{
    uint32_t libre;

    if (!aica_listo()) {
        printf("audio: sin AICA, el juego va mudo\n");
        return 0;
    }
    libre = snd_mem_available();
    s_base = snd_mem_malloc(BLOQUES * PCM_BLOQUE);
    printf("audio: RAM del AICA libre %lu, espejo PCM16 de %lu bytes en %#lx\n",
           (unsigned long)libre, (unsigned long)(BLOQUES * PCM_BLOQUE), (unsigned long)s_base);
    if (s_base == 0u)
        return 0;
    for (unsigned n = 0; n < VOCES; n++) {
        s_v[n].canal = snd_sfx_chn_alloc();
        if (s_v[n].canal < 0) {
            printf("audio: sin canal del AICA para la voz %u\n", n);
            return 0;
        }
    }
    s_spu = spu;
    s_mezcla = snd_stream_alloc(mezcla_cb, 8192);
    if (s_mezcla != SND_STREAM_INVALID)
        snd_stream_start(s_mezcla, 44100, 1);
    printf("audio: mezcla de voces largas %s; RAM del AICA que queda %lu\n",
           s_mezcla != SND_STREAM_INVALID ? "lista" : "SIN STREAM", (unsigned long)snd_mem_available());
    s_sink.ctx = NULL;
    s_sink.upload = subida;
    s_sink.reg = registro;
    s_sink.level = nivel;
    spu->sink = &s_sink;
    return 1;
}

void aica_spu_report(void)
{
    printf("        audio: key-ons %lu  bloques convertidos %lu  muestras recortadas %lu\n",
           (unsigned long)s_keyons, (unsigned long)s_bloques, (unsigned long)s_largos);
}
