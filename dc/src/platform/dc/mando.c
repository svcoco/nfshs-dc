/* M5: el mando de Dreamcast como DualShock en modo analogico, y su vibracion.
 *
 * Decisiones de Jorge (2026-10-27, docs/hitos/M5.md):
 *   - El juego ve un DualShock analogico (tipo 0x73). Medido: el juego usa el stick izquierdo
 *     X (+6 del buffer) como direccion PROPORCIONAL; acelerador y freno son botones; tiene
 *     motores (PadSetAct).
 *   - Los botones, segun el contexto: el mando de Dreamcast tiene menos que el de PS1, asi
 *     que conduciendo los gatillos son acelerador y freno y la cruceta y A/X cubren lo que
 *     falta; en los menus, la pausa y el fin de carrera, todo es como en PS1.
 *
 * "Conduciendo": carrera, sin pausa, y la palabra del juego 0x80113A04 (+4 por jugador) a 1.
 * Medido con la carrera completa: vale 1 desde la cuenta atras de la salida hasta cruzar la
 * meta, y 0 en las estadisticas y la repeticion.
 */
#include <kos.h>
#include <dc/maple/purupuru.h>

#include "host.h"
#include "mando.h"

#define B_SELECT   (1u << 0)
#define B_START    (1u << 3)
#define B_UP       (1u << 4)
#define B_RIGHT    (1u << 5)
#define B_DOWN     (1u << 6)
#define B_LEFT     (1u << 7)
#define B_L2       (1u << 8)
#define B_R2       (1u << 9)
#define B_L1       (1u << 10)
#define B_R1       (1u << 11)
#define B_TRIANGLE (1u << 12)
#define B_CIRCLE   (1u << 13)
#define B_CROSS    (1u << 14)
#define B_SQUARE   (1u << 15)

#define RACE_CONTROL 0x80113A04u           /* 1 mientras el jugador conduce (+4 por jugador) */
#define ZONA_MUERTA  10                    /* del stick, alrededor del centro */
#define UMBRAL_MENU  128                   /* gatillo como L1/R1 */
#define UMBRAL_PEDAL 48                    /* gatillo como acelerador o freno */

static maple_device_t *mando_de(unsigned puerto)
{
    maple_device_t *d = maple_enum_dev((int)puerto, 0);
    return (d != NULL && (d->info.functions & MAPLE_FUNC_CONTROLLER)) ? d : NULL;
}

static uint8_t eje(int v)
{
    if (v > -ZONA_MUERTA && v < ZONA_MUERTA)
        return 0x80u;
    v += 128;
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static uint16_t botones_menu(const cont_state_t *st)
{
    uint16_t b = 0;

    if (st->buttons & CONT_DPAD_UP)    b |= B_UP;
    if (st->buttons & CONT_DPAD_RIGHT) b |= B_RIGHT;
    if (st->buttons & CONT_DPAD_DOWN)  b |= B_DOWN;
    if (st->buttons & CONT_DPAD_LEFT)  b |= B_LEFT;
    if (st->buttons & CONT_START)      b |= B_START;
    if (st->buttons & CONT_A)          b |= B_CROSS;
    if (st->buttons & CONT_B)          b |= B_CIRCLE;
    if (st->buttons & CONT_X)          b |= B_SQUARE;
    if (st->buttons & CONT_Y)          b |= B_TRIANGLE;
    if (st->ltrig > UMBRAL_MENU)       b |= B_L1;
    if (st->rtrig > UMBRAL_MENU)       b |= B_R1;
    return b;
}

/* Conduciendo (la configuracion 1 del juego, la de serie: ver la tabla en M5.md). */
static uint16_t botones_carrera(const cont_state_t *st)
{
    uint16_t b = 0;

    if (st->rtrig > UMBRAL_PEDAL)      b |= B_CROSS;       /* acelerar */
    if (st->ltrig > UMBRAL_PEDAL)      b |= B_SQUARE;      /* freno / marcha atras */
    if (st->buttons & CONT_A)          b |= B_R1;          /* subir marcha */
    if (st->buttons & CONT_B)          b |= B_CIRCLE;      /* freno de mano */
    if (st->buttons & CONT_X)          b |= B_R2;          /* bajar marcha */
    if (st->buttons & CONT_Y)          b |= B_TRIANGLE;    /* camaras */
    if (st->buttons & CONT_DPAD_UP)    b |= B_UP;          /* bocina / largas */
    if (st->buttons & CONT_DPAD_DOWN)  b |= B_L2;          /* mirar atras */
    if (st->buttons & CONT_DPAD_LEFT)  b |= B_L1;          /* modificador: policia, luces */
    if (st->buttons & CONT_DPAD_RIGHT) b |= B_SELECT;      /* reiniciar el coche */
    if (st->buttons & CONT_START)      b |= B_START;       /* pausa */
    return b;
}

uint16_t dc_read_pad(void)
{
    maple_device_t *d = mando_de(0);
    cont_state_t *st = d ? (cont_state_t *)maple_dev_status(d) : NULL;

    return st ? botones_menu(st) : 0u;
}

/* ---- vibracion ----
 * PadSetAct deja en actuators[] la direccion de los dos bytes de los motores del DualShock:
 * el pequeno (0/1) y el grande (0-255). Se pasan a la intensidad del Jump Pack (1-7) solo
 * cuando cambian (cada envio es una transaccion de maple). */
static uint8_t s_vib[2];

static maple_device_t *puru_de(unsigned puerto)
{
    for (int u = 1; u < MAPLE_UNIT_COUNT; u++) {
        maple_device_t *d = maple_enum_dev((int)puerto, u);
        if (d != NULL && (d->info.functions & MAPLE_FUNC_PURUPURU))
            return d;
    }
    return NULL;
}

static void vibra(GameHost *h, Cpu *c, unsigned puerto)
{
    uint8_t pot = 0;
    maple_device_t *d;

    if (h->actuators[puerto] != 0u) {
        uint32_t chico = ps1_r8(c, h->actuators[puerto]);
        uint32_t grande = ps1_r8(c, h->actuators[puerto] + 1u);
        if (grande != 0u)
            pot = (uint8_t)(1u + grande * 6u / 255u);
        else if (chico & 1u)
            pot = 2;
    }
    if (pot == s_vib[puerto])
        return;
    d = puru_de(puerto);
    if (d != NULL) {
        purupuru_effect_t e = { .raw = 0 };
        if (pot != 0u) {
            e.cont = true;
            e.motor = 1;
            e.fpow = pot;
            e.freq = 26;
        }
        purupuru_rumble(d, &e);
        h->vibraciones++;
    }
    s_vib[puerto] = pot;
}

void mando_lee(GameHost *h, Cpu *c)
{
    for (unsigned p = 0; p < 2u; p++) {
        maple_device_t *d = mando_de(p);
        cont_state_t *st = d ? (cont_state_t *)maple_dev_status(d) : NULL;
        int conduce;

        h->pad_ausente[p] = (uint8_t)(st == NULL);
        h->pad_tipo[p] = 0x73u;
        if (st == NULL) {
            h->pad_buttons[p] = 0;
            continue;
        }
        conduce = h->mode == HOST_RACE && !h->paused && ps1_r32(c, RACE_CONTROL + 4u * p) != 0u;
        h->pad_buttons[p] = conduce ? botones_carrera(st) : botones_menu(st);
        h->pad_ejes[p][0] = 0x80u;                  /* stick derecho: el Dreamcast no tiene */
        h->pad_ejes[p][1] = 0x80u;
        h->pad_ejes[p][2] = eje(st->joyx);          /* stick izquierdo */
        h->pad_ejes[p][3] = eje(st->joyy);
        vibra(h, c, p);
    }
}
