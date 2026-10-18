/* libmcrd: la memory card.
 *
 * Portado de src/ps1/memcard.rs de la referencia. Lo esencial, y lo que faltaba, es el
 * **modelo de comando pendiente**: las funciones de la tarjeta no devuelven el
 * resultado, lo dejan pendiente, y el juego lo recoge con `MemCardSync`, que devuelve
 *
 *   1   hay un comando terminado: escribe cual y su resultado
 *   -1  no hay nada pendiente
 *
 * Devolver siempre 1 (como hacia el primer intento de este port) deja al juego
 * releyendo un comando fantasma para siempre, que es exactamente el bucle en el que se
 * quedaba el front end.
 *
 * ESTADO: la tarjeta se presenta **formateada y vacia** en la ranura 1. El modelo
 * completo de 128 KB con bloques y cadenas (crear, escribir y borrar partidas) esta
 * pendiente; hara falta para que las partidas funcionen y para que el test diferencial
 * cuadre con la referencia, que si lo modela.
 */
#include "host.h"

/* codigos de error de libmcrd */
#define ERR_NONE             0u
#define ERR_CARD_NOT_EXIST   1u
#define ERR_FILE_NOT_EXIST   5u
#define ERR_ALREADY_EXIST    6u

/* que funcion dejo el comando pendiente */
#define FUNC_EXIST       1u
#define FUNC_ACCEPT      2u
#define FUNC_READ_FILE   3u
#define FUNC_WRITE_FILE  4u

static void set_pending(GameHost *h, uint32_t cmd, uint32_t result)
{
    h->mc_pending = 1;
    h->mc_cmd = cmd;
    h->mc_result = result;
}

int host_memcard_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    uint32_t a0 = c->r[4], a1 = c->r[5], a2 = c->r[6], a3 = c->r[7];
    /* la ranura 1 es el canal con los 4 bits bajos a 0 */
    int slot1 = (a0 & 0xFu) == 0u;

    switch (addr) {
    case 0x800FB458u:                       /* MemCardInit */
    case 0x800FB484u:                       /* MemCardEnd */
    case 0x800FB4A4u:                       /* MemCardStart */
    case 0x800FB4F4u:                       /* MemCardStop */
    case 0x800FC494u:                       /* MemCardCallback */
        *ret = 0;
        return 1;

    case 0x800FB534u:                       /* MemCardExist(canal) */
    case 0x800FB770u:                       /* MemCardAccept(canal) */
        set_pending(h, (addr == 0x800FB534u) ? FUNC_EXIST : FUNC_ACCEPT,
                    slot1 ? ERR_NONE : ERR_CARD_NOT_EXIST);
        *ret = 1;
        return 1;

    case 0x800FC4A8u:                       /* MemCardSync(modo, &comando, &resultado) */
        if (h->mc_pending) {
            h->mc_pending = 0;
            if (a1 != 0u)
                ps1_w32(c, a1, h->mc_cmd);
            if (a2 != 0u)
                ps1_w32(c, a2, h->mc_result);
            h->mc_syncs++;
            *ret = 1;
        } else {
            *ret = 0xFFFFFFFFu;            /* nada pendiente */
        }
        return 1;

    case 0x800FC234u:                       /* MemCardGetDirentry(canal, patron, entradas, &n, primera, max) */
        /* tarjeta vacia: ninguna partida */
        if (a3 != 0u)
            ps1_w32(c, a3, 0u);
        *ret = 0;
        return 1;

    case 0x800FBDF4u:                       /* MemCardReadFile */
        set_pending(h, FUNC_READ_FILE, slot1 ? ERR_FILE_NOT_EXIST : ERR_CARD_NOT_EXIST);
        *ret = 1;
        return 1;
    case 0x800FC014u:                       /* MemCardWriteFile */
        /* sin el modelo de bloques todavia no se puede guardar */
        set_pending(h, FUNC_WRITE_FILE, slot1 ? ERR_FILE_NOT_EXIST : ERR_CARD_NOT_EXIST);
        h->mc_writes_refused++;
        *ret = 1;
        return 1;

    /* estas devuelven el error directamente, sin pasar por el pendiente */
    case 0x800FC5C4u:                       /* MemCardCreateFile */
        *ret = slot1 ? ERR_ALREADY_EXIST : ERR_CARD_NOT_EXIST;
        h->mc_writes_refused++;
        return 1;
    case 0x800FC7CCu:                       /* MemCardDeleteFile */
        *ret = ERR_FILE_NOT_EXIST;
        return 1;
    case 0x800FC988u:                       /* MemCardFormat */
        *ret = slot1 ? ERR_NONE : ERR_CARD_NOT_EXIST;
        return 1;

    default:
        return 0;
    }
}
