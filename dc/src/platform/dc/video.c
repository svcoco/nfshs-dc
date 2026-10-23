/* Modos de video de Dreamcast: 480p por VGA y 480i por compuesto o RGB.
 *
 * La decision de CLAUDE.md seccion 3 es renderizar a **640x480 en los dos modos** y
 * elegir con `vid_check_cable`, con posibilidad de forzarlo. Aqui va esa eleccion; el
 * escalado del dibujo 2D es el paso 4.
 *
 * Sobre el escalado, un dato medido que corrige un detalle de esa decision: el juego NO
 * dibuja a 320x240. Su pantalla es de **512x240** (doble buffer en la VRAM de PS1, en
 * y=0 e y=256), tanto en el front end como en carrera, con sub-areas de 320x240 para
 * las vistas. Asi que el "x2" de la seccion 3 vale para la vertical (240 -> 480) pero
 * no para la horizontal: 512 -> 640 es x1,25. La regla general que si se sostiene es
 * **llevar el area de pantalla de PS1 a 640x480**, que es lo que la seccion 3 pide de
 * verdad. Medido con NFSHS_ENVDBG sobre los dos guiones.
 *
 * Por que 480i y no 240p: a 15 kHz el Dreamcast da 480 lineas entrelazadas. Dibujar a
 * 240p seria mas fiel al original, pero la seccion 3 fija 640x480 en los dos modos para
 * que el renderer sea uno solo. Si a la hora de mirarlo el texto no se lee, es cuando
 * toca discutirlo con datos.
 */
#include <kos.h>

#include <string.h>

#include "video.h"

static const char *s_modo = "sin iniciar";
static const char *s_cable = "?";
static int s_forzado;

static int igual(const char *a, const char *b)
{
    return a != NULL && strcmp(a, b) == 0;
}

int video_init(const char *forzar)
{
    int8_t cable = vid_check_cable();
    int quiere_vga;
    int dm;

    switch (cable) {
    case CT_VGA:       s_cable = "VGA";        break;
    case CT_RGB:       s_cable = "RGB/SCART";  break;
    case CT_COMPOSITE: s_cable = "compuesto";  break;
    default:           s_cable = "desconocido"; break;
    }

    quiere_vga = (cable == CT_VGA);
    s_forzado = 0;

    if (igual(forzar, "vga")) {
        dm = DM_640x480_VGA;
        s_modo = "480p VGA";
        s_forzado = !quiere_vga;
    } else if (igual(forzar, "ntsc")) {
        dm = DM_640x480_NTSC_IL;
        s_modo = "480i NTSC";
        s_forzado = quiere_vga;
    } else if (igual(forzar, "pal")) {
        dm = DM_640x480_PAL_IL;
        s_modo = "480i PAL";
        s_forzado = quiere_vga;
    } else if (quiere_vga) {
        dm = DM_640x480_VGA;
        s_modo = "480p VGA";
    } else {
        /* Sin VGA toca entrelazado, y la frecuencia la manda la region de la consola:
         * en PAL son 50 Hz y en lo demas 60. */
        int pal = (flashrom_get_region() == FLASHROM_REGION_EUROPE);
        dm = pal ? DM_640x480_PAL_IL : DM_640x480_NTSC_IL;
        s_modo = pal ? "480i PAL" : "480i NTSC";
    }

    /* Forzar VGA sin cable VGA no pinta nada en una tele: la consola solo saca 31 kHz
     * por ese cable. Se avisa y se cae al modo que el cable admite. */
    if (dm == DM_640x480_VGA && !quiere_vga) {
        dm = (flashrom_get_region() == FLASHROM_REGION_EUROPE) ? DM_640x480_PAL_IL
                                                               : DM_640x480_NTSC_IL;
        s_modo = (dm == DM_640x480_PAL_IL) ? "480i PAL (VGA pedido sin cable VGA)"
                                           : "480i NTSC (VGA pedido sin cable VGA)";
        s_forzado = 0;
        vid_set_mode_ex(&vid_builtin[dm]);
        return 0;
    }

    vid_set_mode_ex(&vid_builtin[dm]);
    return 1;
}

const char *video_mode_name(void)
{
    return s_modo;
}

const char *video_cable_name(void)
{
    return s_cable;
}

int video_forced(void)
{
    return s_forzado;
}
