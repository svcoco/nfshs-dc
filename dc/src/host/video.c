/* Los videos del front end (VIDEO_*): las secuencias STR que el juego reproduce
 * entre pantallas.
 *
 * En M1a no hay video: los STR/MDEC son trabajo de M5, y la referencia los resuelve
 * con un modulo propio (src/ps1/video.rs) que no hace falta reproducir para que la
 * logica del front end corra. Aqui se informa de que no hay video disponible, y lo que
 * importa es MEDIR si el front end lo acepta y sigue, o si se queda esperando: si se
 * queda, el informe lo dira y habra que modelar el estado de reproduccion.
 */
#include "host.h"

int host_video_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    (void)c;
    switch (addr) {
    case 0x800511B0u:                       /* VIDEO_create */
    case 0x80051328u:                       /* VIDEO_destroy */
    case 0x80051380u:                       /* VIDEO_spoolfile */
    case 0x800513DCu:                       /* VIDEO_startplayback */
    case 0x8005143Cu:                       /* VIDEO_abortplayback */
    case 0x80051480u:                       /* VIDEO_state */
    case 0x80051528u:                       /* VIDEO_updateframexy */
        h->video_calls++;
        h->last_video_call = addr;
        *ret = 0;
        return 1;
    default:
        return 0;
    }
}
