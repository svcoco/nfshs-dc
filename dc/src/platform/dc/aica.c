/* El sonido de KOS (snd_stream) con el driver ARM del AICA anterior al 2026-06-10.
 *
 * MEDIDO (M3, docs/hitos/M3.md): con el driver de nuestro KOS (8e704086), en Flycast 2.6
 * el ARM del AICA no avanza nunca su reloj (la interrupcion de timer no llega), se queda en
 * su espera y no atiende ningun comando: snd_stream solo entrega el prellenado y no suena
 * nada. El cambio esta en el commit 8690c6dd de KOS ("aica: Enhanced driver
 * initialization": reconfigura las fuentes de interrupcion y habilita la FIQ con
 * `msr CPSR_c`). Con el driver de justo antes, el reloj late a 4 410 Hz y el audio fluye.
 * El lado SH-4 del protocolo (aica_comm.h) no ha cambiado desde entonces.
 *
 * Asi que se carga ese driver encima del de KOS despues de snd_stream_init, con las mismas
 * llamadas que usa snd_init. El binario no se versiona: el Makefile lo saca del historial
 * de git de KOS (build/dc/aica/aica_drv.c). Si es fallo de Flycast o de KOS, y si en una
 * consola hace falta, se mira en M7.
 */
#include <kos.h>
#include <dc/sound/stream.h>
#include <dc/spu.h>

#include "aica.h"

extern const int aica_drv_viejo_size;
extern const unsigned char aica_drv_viejo_data[];

int aica_listo(void)
{
    static int listo;

    if (listo)
        return listo > 0;
    if (snd_stream_init() < 0) {
        listo = -1;
        return 0;
    }
    spu_disable();
    spu_memload_sq(0, (void *)aica_drv_viejo_data, (aica_drv_viejo_size + 3) & ~3);
    spu_enable();
    thd_sleep(10);
    listo = 1;
    return 1;
}
