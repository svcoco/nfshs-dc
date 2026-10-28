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
 *   - VOZ LARGA (sin fin en 65 534 muestras, lo maximo de un canal del AICA: LSA/LEA son de
 *     16 bits; es la musica en stream, dos voces sobre anillos de ~86 000 muestras): tambien
 *     va a un canal del AICA, en bucle sobre un ANILLO propio en su RAM. Un hilo sigue la
 *     posicion del canal y escribe por delante los bloques que la voz de PS1 va leyendo
 *     (siguiente bloque, o la repeticion al acabar uno con bandera de fin), ya en PCM16 y a
 *     su frecuencia original. El tono, el volumen y el panorama los hace el AICA, como en
 *     las demas voces; la CPU solo decodifica los bloques, sin remuestrear ni mezclar.
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
#define ANILLO_BLQ 256u                            /* bloques del anillo de una voz larga */
#define ANILLO     (ANILLO_BLQ * 28u)              /* 7 168 muestras: 325 ms a 22 050 Hz */
#define DELANTE    (96u * 28u)                     /* por delante del canal: 122 ms a 22 050 Hz */
#define ANILLOS    4u
#define NADA       0xFFFFFFFFu
#define VALIDO_MS  2000u                           /* ver valido() */
/* Las peliculas piden su stream (2 x 16 KB) mientras suenan: se deja sitio. */
#define RESERVA    (2u * (SND_STREAM_BUFFER_MAX / 4u) + 2048u)

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
    /* VOZ LARGA: su canal toca en bucle el anillo `anillo`; aqui va la voz de PS1 que lo
     * alimenta (como en el SPU) y la cuenta de muestras escritas y tocadas. */
    uint8_t  larga;
    uint8_t  ignora_bucle;                         /* se escribio la repeticion en marcha */
    int      anillo;                               /* -1: sin anillo (no suena) */
    uint32_t addr, repeat;
    int32_t  s1, s2;
    uint32_t escritas, tocadas, fin;               /* muestras; fin: NADA si sigue */
    uint32_t ult_pos;                              /* ultima posicion del canal en el anillo */
    uint32_t lo, hi;                               /* bloques que ha leido (para s_de_larga) */
    uint64_t t_us;                                 /* cuando se miro */
    uint8_t  parada;                               /* espera a que el driver escriba */
    uint32_t keyon_ms;
    uint32_t vueltas;                              /* saltos a la repeticion (vueltas al anillo) */
} Voz;

/* El bloque de la RAM de sonido de cada tramo (28 muestras) escrito en un anillo: para la IRQ
 * por direccion y para ver si el driver pisa algo que aun no ha sonado. */
#define SIN_BLOQUE 0xFFFFu

static uint32_t s_base;                            /* el espejo en la RAM del AICA; 0: no hay */
static int16_t  s_hist[BLOQUES][2];                /* las dos ultimas muestras de cada bloque */
static int16_t  s_pcm[28u * 64u];                  /* bloques decodificados antes de subirlos */
static uint8_t  s_de_larga[BLOQUES];               /* lo esta leyendo una voz larga */
static uint8_t  s_sucio[BLOQUES];                  /* cambio y el espejo no lo tiene */
static int16_t  s_anpcm[DELANTE] __attribute__((aligned(32)));   /* lo mismo, del hilo */
static Voz      s_v[VOCES];
static uint16_t s_main[2] = { 0x7FFF, 0x7FFF };
static SpuSink  s_sink;
static uint32_t s_anillo[ANILLOS];                 /* en la RAM del AICA */
static uint8_t  s_anillo_usado[ANILLOS];
static uint16_t s_tramo[ANILLOS][ANILLO_BLQ];
static unsigned s_nanillos;
static mutex_t  s_mtx = MUTEX_INITIALIZER;         /* el juego y el hilo de las voces largas */
static volatile int s_irq;                         /* una voz larga leyo la direccion de IRQ */
static uint32_t s_subido_ms[BLOQUES];              /* cuando subio el driver cada bloque */

/* estadisticas, para el informe */
static uint32_t s_keyons, s_bloques, s_largos, s_sin_anillo, s_vacios, s_largas_max;
static uint64_t s_hilo_us, s_bus_us, s_subida_us;
static uint32_t s_n_subidas, s_saltados, s_n_registros, s_n_niveles, s_tarde;
static uint32_t s_dist[6], s_irqs, s_tarde_ant, s_paradas;

/* ---- muestras ---- */

/* Una muestra: el nibble ya esta en los 4 bits altos de `n`, asi que `n >> (16 + shift)`
 * es el `(nib << 12) >> shift` con signo del SPU, sin pasos intermedios. */
#define MUESTRA(n)                                                       \
    do {                                                                 \
        int32_t v_ = ((int32_t)(n) >> sh) + ((s1 * k0 + s2 * k1 + 32) >> 6); \
        v_ = v_ < -32768 ? -32768 : v_ > 32767 ? 32767 : v_;             \
        s2 = s1;                                                         \
        s1 = v_;                                                         \
        *out++ = (int16_t)v_;                                            \
    } while (0)

static void decodifica(const uint8_t *b, int32_t *ps1, int32_t *ps2, int16_t *out)
{
    unsigned sh = (b[0] & 0xFu) <= 12u ? 16u + (b[0] & 0xFu) : 16u + 9u;
    unsigned f = (unsigned)(b[0] >> 4) < 4u ? (unsigned)(b[0] >> 4) : 4u;
    int32_t k0 = FILTROS[f][0], k1 = FILTROS[f][1], s1 = *ps1, s2 = *ps2;

    for (unsigned i = 2; i < 16u; i++) {
        uint32_t byte = b[i];
        MUESTRA(byte << 28);
        MUESTRA((byte & 0xF0u) << 24);
    }
    *ps1 = s1;
    *ps2 = s2;
}

/* Los bloques [b0, b1) de la RAM de sonido, a PCM16 en el espejo (b1 puede pasar del final:
 * da la vuelta). La historia del decodificador sale del bloque anterior (lo que el SPU haria
 * al tocarlos seguidos).
 *   - En una SUBIDA se saltan los bloques que esta leyendo una voz larga: la musica en
 *     stream no se toca desde el espejo sino desde su anillo, asi que convertirlos seria
 *     trabajo tirado (son casi todas las subidas en carrera). Quedan SUCIOS.
 *   - En el KEY-ON de una voz normal (`solo_sucios`) se convierten los sucios que va a
 *     tocar, por si alguna vez una muestra corta cae en esa zona. */
static void espejo(const uint8_t *ram, uint32_t b0, uint32_t b1, int solo_sucios)
{
    uint32_t n = 0, inicio = 0;

    for (uint32_t b = b0;; b++) {
        uint32_t k = b % BLOQUES;
        int va = b < b1 && (solo_sucios ? s_sucio[k] : !s_de_larga[k]);
        if (n != 0u && (!va || n == 64u || k == 0u)) {
            uint64_t t = timer_us_gettime64();
            spu_memload(s_base + inicio * PCM_BLOQUE, s_pcm, n * PCM_BLOQUE);
            s_bus_us += timer_us_gettime64() - t;
            s_bloques += n;
            n = 0;
        }
        if (b >= b1)
            break;
        if (va) {
            uint32_t prev = (k + BLOQUES - 1u) % BLOQUES;
            int32_t s1 = s_hist[prev][0], s2 = s_hist[prev][1];
            if (n == 0u)
                inicio = k;
            decodifica(&ram[k * 16u], &s1, &s2, &s_pcm[n * 28u]);
            s_hist[k][0] = (int16_t)s1;
            s_hist[k][1] = (int16_t)s2;
            s_sucio[k] = 0;
            n++;
        } else if (!solo_sucios) {
            s_sucio[k] = 1;
            s_saltados++;
        }
    }
}

static void pisados(Voz *v, uint32_t b0, uint32_t b1);

static void subida(void *ctx, const uint8_t *ram, uint32_t addr, uint32_t len)
{
    uint64_t t;
    uint32_t b0, b1;

    (void)ctx;
    if (s_base == 0u || len == 0u)
        return;
    s_n_subidas++;
    t = timer_us_gettime64();
    b0 = addr / 16u;
    b1 = (addr + len + 15u) / 16u;
    {
        uint32_t ms = (uint32_t)timer_ms_gettime64();
        for (uint32_t b = b0; b < b1; b++)
            s_subido_ms[b % BLOQUES] = ms;
    }
    espejo(ram, b0, b1, 0);
    /* Lo que una voz larga ya escribio en su anillo y aun no ha sonado no deberia cambiar
     * (ver pisados()). */
    mutex_lock(&s_mtx);
    for (unsigned n = 0; n < VOCES; n++) {
        Voz *v = &s_v[n];
        if (!v->suena || !v->larga || v->anillo < 0)
            continue;
        /* DIAG: a cuantos bloques por delante de lo que suena escribe el driver */
        if (v->lo < v->hi && b0 >= v->lo && b0 <= v->hi && v->tocadas < v->escritas) {
            uint32_t p = s_tramo[v->anillo][(v->tocadas / 28u) % ANILLO_BLQ];
            uint32_t largo = v->hi - v->lo + 1u;
            if (p >= v->lo && p <= v->hi) {
                uint32_t d = (b0 + largo - p) % largo;
                s_dist[d < 128u ? 0u : d < 256u ? 1u : d < 512u ? 2u : d < 1024u ? 3u :
                       d < 2048u ? 4u : 5u]++;
            }
        }
        pisados(v, b0, b1);
    }
    mutex_unlock(&s_mtx);
    s_subida_us += timer_us_gettime64() - t;
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

static SpuStub *s_spu;                             /* la RAM de sonido de PS1 */

/* ---- voces largas: un anillo por voz ---- */

/* Calla la voz (key-off, o un key-on encima) y suelta su anillo. */
static void calla(Voz *v)
{
    if (v->suena && (!v->larga || v->anillo >= 0))
        canal(v->canal, AICA_CH_CMD_STOP, 0, 0, 0, 0, 0, 0, 0);
    if (v->larga && v->anillo >= 0) {
        s_anillo_usado[v->anillo] = 0;
        if (v->lo <= v->hi)                        /* su zona vuelve a ir al espejo */
            memset(&s_de_larga[v->lo], 0, v->hi - v->lo + 1u);
    }
    v->suena = 0;
    v->larga = 0;
    v->anillo = -1;
}

/* El siguiente bloque que lee la voz larga, como el SPU (psxspu.c): la bandera 4 marca la
 * repeticion (salvo que el driver la haya escrito en marcha) y la 1 salta a ella; sin la 2,
 * la voz acaba al terminar el bloque. */
static void bloque_larga(Voz *v, int16_t *out)
{
    uint16_t *t = &s_tramo[v->anillo][(v->escritas / 28u) % ANILLO_BLQ];
    const uint8_t *blk;
    uint8_t flags;
    uint32_t k;

    if (v->fin != NADA) {
        *t = SIN_BLOQUE;
        memset(out, 0, 56);                        /* acabo: silencio hasta que se calle */
        v->escritas += 28u;
        return;
    }
    k = (v->addr & (PS1_SPU_RAM_SIZE - 16u)) / 16u;
    *t = (uint16_t)k;
    s_de_larga[k] = 1;
    if (k < v->lo)
        v->lo = k;
    if (k > v->hi)
        v->hi = k;
    blk = &s_spu->ram[k * 16u];
    flags = blk[1];
    decodifica(blk, &v->s1, &v->s2, out);
    if ((flags & 4u) && !v->ignora_bucle)
        v->repeat = v->addr;
    if (flags & 1u) {
        v->vueltas++;
        v->addr = v->repeat;
        if ((flags & 2u) == 0u)
            v->fin = v->escritas + 28u;
    } else {
        v->addr = (v->addr + 16u) & (PS1_SPU_RAM_SIZE - 1u);
    }
    v->escritas += 28u;
}

/* Escribe en el anillo hasta `hasta` muestras (en bloques; el anillo es de bloques enteros,
 * asi que ninguno queda partido en el borde). */
/* El siguiente bloque de la voz larga ya lo escribio el driver para lo que toca ahora:
 *   - para ESTE tema: subio despues de VALIDO_MS antes del key-on. En la primera vuelta el
 *     driver escribe por delante de la voz, y si el AICA va algo adelantado (el juego se paro
 *     abriendo el stream) leeria el tema anterior;
 *   - y, pasada la primera vuelta, en la ULTIMA vuelta (mas VALIDO_MS de margen): en la PS1
 *     el driver reescribe cada trozo justo despues de que suene, y si deja de hacerlo es que
 *     el juego esta parado en una carga (aqui una llamada que no suelta el control); tocar
 *     entonces repetiria la vuelta anterior.
 * Si no vale, el canal espera (rellena()). */
static int valido(const Voz *v)
{
    uint32_t k = (v->addr & (PS1_SPU_RAM_SIZE - 16u)) / 16u, cuando = s_subido_ms[k];

    if (v->fin != NADA)
        return 1;
    if ((int32_t)(cuando - (v->keyon_ms - VALIDO_MS)) < 0)
        return 0;
    if (v->vueltas != 0u && v->freq != 0u) {
        uint32_t vuelta = (v->hi - v->lo + 1u) * 28000u / v->freq;
        uint32_t ahora = (uint32_t)timer_ms_gettime64();
        if ((int32_t)(cuando - (ahora - vuelta - VALIDO_MS)) < 0)
            return 0;
    }
    return 1;
}

static void escribe_larga(Voz *v, uint32_t hasta)
{
    while ((int32_t)(hasta - v->escritas) > 0 && valido(v)) {
        uint32_t ini = v->escritas % ANILLO, n = 0;
        while ((int32_t)(hasta - v->escritas) > 0 && n < DELANTE && ini + n < ANILLO && valido(v)) {
            bloque_larga(v, &s_anpcm[n]);
            n += 28u;
        }
        uint64_t t = timer_us_gettime64();
        spu_memload(s_anillo[v->anillo] + ini * 2u, s_anpcm, n * 2u);
        s_bus_us += timer_us_gettime64() - t;
    }
}

/* El driver cambio los bloques [b0, b1): cuenta los que ya estaban escritos en el anillo y
 * aun no han sonado. En la PS1 el driver rellena el trozo que la voz ACABA de tocar (unos 23 ms
 * por detras de ella, medido en la referencia): si pisa lo que el AICA aun no toco, el AICA va
 * desfasado de donde el driver cree que va la voz. Lo correcto entonces es lo ya escrito, asi
 * que no se reescribe: solo se cuenta, y debe ser 0. */
static void pisados(Voz *v, uint32_t b0, uint32_t b1)
{
    /* lo tocado se miro hace hasta ~20 ms (el hilo): se estima por donde va ahora */
    uint32_t ahora = v->tocadas + (uint32_t)((timer_us_gettime64() - v->t_us) * v->freq / 1000000u);

    if (v->parada)
        return;                                    /* lo escrito no sonara: retoma() rehace */
    for (uint32_t j = ahora / 28u; j < v->escritas / 28u; j++) {
        uint32_t k = s_tramo[v->anillo][j % ANILLO_BLQ];
        if (k != SIN_BLOQUE && ((k >= b0 && k < b1) || (k + BLOQUES >= b0 && k + BLOQUES < b1)))
            s_tarde++;
    }
}

/* Avanza la cuenta de lo tocado con la posicion del canal y escribe por delante. La posicion
 * que publica el driver ARM se refresca cada ~2 ms y justo tras el key-on puede ser la del
 * uso anterior del canal, asi que se contrasta con la que da el tiempo: si discrepa en mas de
 * un cuarto de anillo, manda el tiempo. */
/* Retoma el canal parado: las cuentas vuelven a 0 y el anillo se rellena desde donde iba la
 * voz de PS1 (lo que quedaba escrito sin sonar, como mucho ~20 ms, se pierde). */
static void retoma(Voz *v, uint64_t ahora)
{
    v->escritas = v->tocadas = 0;
    v->ult_pos = 0;
    escribe_larga(v, DELANTE);
    canal(v->canal, AICA_CH_CMD_START, s_anillo[v->anillo], ANILLO, 1, 0, v->freq, v->vol, v->pan);
    v->t_us = ahora;
    v->parada = 0;
}

static void rellena(Voz *v, uint64_t ahora)
{
    if (v->parada) {
        if (valido(v))
            retoma(v, ahora);
        return;
    }
    uint32_t pos = snd_get_pos((unsigned)v->canal) % ANILLO;
    uint32_t esperado = (uint32_t)((ahora - v->t_us) * v->freq / 1000000u);
    uint32_t d = (pos + ANILLO - v->ult_pos) % ANILLO, delta;

    if (esperado > d)
        d += (esperado - d + ANILLO / 2u) / ANILLO * ANILLO;
    if ((d > esperado ? d - esperado : esperado - d) > ANILLO / 4u) {
        delta = esperado;
        v->ult_pos = (v->ult_pos + esperado) % ANILLO;
    } else {
        delta = d;
        v->ult_pos = pos;
    }
    v->t_us = ahora;
    /* La IRQ por direccion: el SPU la levanta cuando una voz lee el bloque de la direccion
     * de IRQ. Con ella el driver de EA sabe por donde va su stream de musica (la pone al
     * principio del anillo: una vez por vuelta) y lo rellena a ese paso. */
    {
        uint32_t irq = ((uint32_t)s_spu->reg[0x1A4u / 2u] << 3) / 16u;
        for (uint32_t j = (v->tocadas + 27u) / 28u; j * 28u <= v->tocadas + delta && j < v->escritas / 28u; j++)
            if (s_tramo[v->anillo][j % ANILLO_BLQ] == irq) {
                s_irq = 1;
                s_irqs++;
            }
    }
    v->tocadas += delta;
    if (v->fin != NADA && (int32_t)(v->tocadas - v->fin) >= 0) {
        calla(v);                                  /* la muestra acabo */
        return;
    }
    if (!valido(v) && (int32_t)(v->escritas - v->tocadas) < (int32_t)(v->freq * 30u / 1000u)) {
        /* lo escrito se acaba y lo siguiente aun no lo ha escrito el driver: el canal espera
           (antes de dar la vuelta al anillo y tocar lo de hace una vuelta) */
        canal(v->canal, AICA_CH_CMD_STOP, 0, 0, 0, 0, 0, 0, 0);
        v->parada = 1;
        s_paradas++;
        return;
    }
    if ((int32_t)(v->tocadas - v->escritas) > 0) {
        /* el canal alcanzo lo escrito (el hilo no corrio a tiempo): la voz de PS1 tambien
           avanzo, asi que se saltan esos bloques para seguir donde va */
        s_vacios++;
        while ((int32_t)(v->tocadas - v->escritas) > 0) {
            int16_t basura[28];
            bloque_larga(v, basura);
        }
    }
    escribe_larga(v, v->tocadas + DELANTE);
}

static void key_on_larga(Voz *v, const uint16_t *regs, unsigned n, uint32_t sb)
{
    unsigned a, largas = 0;

    s_largos++;
    s_keyons++;
    v->larga = 1;
    v->freq = frecuencia(regs, n);
    volumen(v, regs, n);
    for (a = 0; a < s_nanillos && s_anillo_usado[a]; a++)
        ;
    if (v->freq == 0u)
        return;
    v->suena = 1;                                  /* para el driver suena, con anillo o sin el */
    if (a == s_nanillos) {
        s_sin_anillo++;
        printf("audio: voz larga %u sin anillo libre: no suena\n", n);
        return;
    }
    s_anillo_usado[a] = 1;
    v->anillo = (int)a;
    v->addr = sb * 16u;
    v->repeat = (uint32_t)regs[n * 8u + 7u] << 3;
    v->ignora_bucle = 0;
    v->s1 = v->s2 = 0;
    v->escritas = v->tocadas = 0;
    v->fin = NADA;
    v->ult_pos = 0;
    v->lo = BLOQUES;
    v->hi = 0;
    v->parada = 0;
    v->keyon_ms = (uint32_t)timer_ms_gettime64();
    v->vueltas = 0;
    escribe_larga(v, DELANTE);
    if (v->escritas != 0u)
        canal(v->canal, AICA_CH_CMD_START, s_anillo[a], ANILLO, 1, 0, v->freq, v->vol, v->pan);
    else
        v->parada = 1;                             /* aun no hay nada del tema: espera */
    v->t_us = timer_us_gettime64();
    for (unsigned i = 0; i < VOCES; i++)
        largas += s_v[i].suena && s_v[i].larga;
    if (largas > s_largas_max)
        s_largas_max = largas;
    printf("audio: voz larga %u en %#lx a %lu Hz, anillo %u (repeticion %#lx)\n", n,
           (unsigned long)(sb * 16u), (unsigned long)v->freq, a, (unsigned long)v->repeat);
}

/* El hilo de las voces largas: corre aunque el juego este parado (cargando), como el SPU,
 * que sigue leyendo su RAM pase lo que pase en la CPU. */
static void *hilo_largas(void *arg)
{
    (void)arg;
    for (;;) {
        uint64_t t0;
        thd_sleep(20);
        mutex_lock(&s_mtx);
        t0 = timer_us_gettime64();
        for (unsigned n = 0; n < VOCES; n++)
            if (s_v[n].suena && s_v[n].larga && s_v[n].anillo >= 0)
                rellena(&s_v[n], t0);
        s_hilo_us += timer_us_gettime64() - t0;
        mutex_unlock(&s_mtx);
    }
    return NULL;
}

/* ---- peticiones del driver ---- */

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
    calla(v);
    if (k == LARGO_MAX / 28u) {
        key_on_larga(v, regs, n, sb);
        return;
    }
    if (bucle && (lb == BLOQUES || lb < sb)) {
        uint32_t rep = ((uint32_t)regs[n * 8u + 7u] << 3) / 16u % BLOQUES;
        lb = (rep >= sb && rep <= eb) ? rep : sb;
    }
    if (eb < sb)
        eb = BLOQUES - 1u;                         /* no da la vuelta al final de la RAM */
    espejo(ram, sb, eb + 1u, 1);                   /* lo que se dejo sin convertir */
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
    calla(&s_v[n]);
}

/* El volumen o el panorama de una voz que suena, al canal si cambio. */
static void revolumen(Voz *v, const uint16_t *regs, unsigned n)
{
    uint32_t vol = v->vol, pan = v->pan;

    volumen(v, regs, n);
    if (v->suena && (!v->larga || v->anillo >= 0) && (vol != v->vol || pan != v->pan))
        canal(v->canal, AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_VOL | AICA_CH_UPDATE_SET_PAN,
              0, 0, 0, 0, 0, v->vol, v->pan);
}

static void registro_(const uint16_t *regs, uint32_t off, uint16_t val)
{
    if (off < 0x180u) {
        unsigned n = off >> 4;
        Voz *v = &s_v[n];
        switch (off & 0xFu) {
        case 0x0:
        case 0x2:
            if (v->suena)
                revolumen(v, regs, n);
            break;
        case 0x4:
            if (v->suena && (!v->larga || v->anillo >= 0)) {
                uint32_t f = frecuencia(regs, n);
                if (f != v->freq && f != 0u) {
                    v->freq = f;
                    canal(v->canal, AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_FREQ, 0, 0, 0, 0, f, 0, 0);
                }
            }
            break;
        case 0xE:
            /* la repeticion escrita en marcha manda sobre la bandera 4 (como el SPU) */
            v->repeat = (uint32_t)val << 3;
            if (v->suena)
                v->ignora_bucle = 1;
            break;
        default:
            break;
        }
        return;
    }
    switch (off) {
    case 0x180:
    case 0x182:
        if ((val & 0x8000u) == 0u) {
            s_main[(off - 0x180u) >> 1] = (uint16_t)mag(val, 0x7FFF);
            for (unsigned n = 0; n < VOCES; n++)
                if (s_v[n].suena)
                    revolumen(&s_v[n], regs, n);
        }
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

static void registro(void *ctx, const uint16_t *regs, uint32_t off, uint16_t val)
{
    (void)ctx;
    if (s_base == 0u)
        return;
    s_n_registros++;
    mutex_lock(&s_mtx);
    registro_(regs, off, val);
    mutex_unlock(&s_mtx);
}

static uint16_t nivel(void *ctx, unsigned n)
{
    Voz *v;

    (void)ctx;
    s_n_niveles++;
    if (n >= VOCES)
        return 0;
    v = &s_v[n];
    if (v->suena && !v->larga && !v->bucle && timer_us_gettime64() >= v->fin_us)
        v->suena = 0;                              /* la muestra ya acabo */
    return v->suena ? 0x7FFFu : 0u;
}

static int irq_voz(void *ctx)
{
    (void)ctx;
    if (!s_irq)
        return 0;
    s_irq = 0;
    return 1;
}

int aica_spu_init(SpuStub *spu)
{
    uint32_t libre;
    kthread_attr_t attr = { 0 };

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
        s_v[n].anillo = -1;
        if (s_v[n].canal < 0) {
            printf("audio: sin canal del AICA para la voz %u\n", n);
            return 0;
        }
    }
    /* los anillos de las voces largas, con lo que quede despues del sitio de las peliculas */
    while (s_nanillos < ANILLOS && snd_mem_available() >= RESERVA + ANILLO * 2u) {
        s_anillo[s_nanillos] = snd_mem_malloc(ANILLO * 2u);
        if (s_anillo[s_nanillos] == 0u)
            break;
        s_nanillos++;
    }
    s_spu = spu;
    attr.prio = PRIO_DEFAULT - 2;                  /* por delante del juego cuando despierta */
    attr.label = "audio";
    thd_create_ex(&attr, hilo_largas, NULL);
    printf("audio: %u anillos de %u muestras para las voces largas; RAM del AICA que queda %lu\n",
           s_nanillos, (unsigned)ANILLO, (unsigned long)snd_mem_available());
    s_sink.ctx = NULL;
    s_sink.upload = subida;
    s_sink.reg = registro;
    s_sink.level = nivel;
    s_sink.irq = irq_voz;
    spu->sink = &s_sink;
    return 1;
}

void aica_spu_report(void)
{
    printf("        audio: key-ons %lu  bloques convertidos %lu  voces largas %lu (a la vez %lu, "
           "sin anillo %lu, alcanzadas %lu, pisados %lu (+%lu), IRQ %lu, esperas %lu, hilo %lu ms)\n",
           (unsigned long)s_keyons, (unsigned long)s_bloques, (unsigned long)s_largos,
           (unsigned long)s_largas_max, (unsigned long)s_sin_anillo, (unsigned long)s_vacios, (unsigned long)s_tarde, (unsigned long)(s_tarde - s_tarde_ant), (unsigned long)s_irqs, (unsigned long)s_paradas,
           (unsigned long)(s_hilo_us / 1000u));
    s_tarde_ant = s_tarde;
    printf("        audio: subidas %lu (bloques saltados %lu)  registros %lu  niveles %lu  subida %lu ms  bus %lu ms\n",
           (unsigned long)s_n_subidas, (unsigned long)s_saltados,
           (unsigned long)s_n_registros, (unsigned long)s_n_niveles,
           (unsigned long)(s_subida_us / 1000u), (unsigned long)(s_bus_us / 1000u));
    printf("        audio: rellenos por delante <128 %lu  <256 %lu  <512 %lu  <1024 %lu  <2048 %lu  mas %lu\n",
           (unsigned long)s_dist[0], (unsigned long)s_dist[1], (unsigned long)s_dist[2],
           (unsigned long)s_dist[3], (unsigned long)s_dist[4], (unsigned long)s_dist[5]);
}
