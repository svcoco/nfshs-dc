/* Todas las pistas y coches disponibles en el front end.
 *
 * Portado de `unlock_everything` (src/ps1/host/mod.rs) de la referencia, que lo hace en
 * **cada frame del front end** porque cargar una partida los vuelve a fijar.
 *
 * Por que esta aqui y no es solo una comodidad: con todo bloqueado el juego no deja
 * entrar a una carrera ("you must buy a car from the car dealer before you can enter this
 * race"), asi que sin esto **no hay escenario de carrera** para el test diferencial
 * partiendo de una tarjeta vacia. La referencia lo trae encendido y se apaga con
 * NFSHS_LOCKED=1.
 *
 * En el port va **apagado por defecto** (--unlock lo enciende): el port es fiel al juego,
 * y si el Dreamcast acaba ofreciendo un "desbloquear todo" sera una decision de producto
 * de M5, no algo heredado sin querer de la herramienta de desarrollo de la referencia.
 */
#include "host.h"

/* tTrackManager y tCarManager (direcciones de la referencia). */
#define CAR_MANAGER   0x8011552Cu
#define TRACK_MANAGER 0x80115D08u

void host_unlock_everything(GameHost *h, Cpu *c)
{
    uint32_t count, table, k;

    if (!h->unlock)
        return;

    /* las once ranuras de pista: tTrackManager::SetTrackAvailable */
    for (k = 0; k < 11u; k++)
        ps1_w32(c, TRACK_MANAGER + 8u + 4u * k, 1u);

    count = ps1_r32(c, CAR_MANAGER);
    table = ps1_r32(c, CAR_MANAGER + 4u);
    /* antes de que el front end haya montado sus coches, la tabla no es de fiar */
    if (count < 1u || count > 64u || table < 0x80000000u || table >= 0x80200000u)
        return;

    /* clases 0..7 (carrera, bonus y policia; la 8 es trafico): disponible en +520 y
     * desbloqueado en +568, que es por el que van las listas de coches */
    for (k = 0; k < count; k++) {
        int32_t model = (int32_t)(int8_t)ps1_r8(c, table + 204u * k);
        uint32_t class_ = ps1_r8(c, table + 204u * k + 2u);
        if (model >= 0 && class_ <= 7u) {
            ps1_w8(c, CAR_MANAGER + 520u + (uint32_t)model, 1u);
            ps1_w8(c, CAR_MANAGER + 568u + (uint32_t)model, 1u);
        }
    }
    h->unlocks++;
}
