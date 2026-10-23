/* Modos de video de Dreamcast (M2a, paso 2). Solo plataforma: no lo incluye nada de
 * src/ps1rt, src/host ni gen. */
#ifndef PLATFORM_DC_VIDEO_H
#define PLATFORM_DC_VIDEO_H

/* Elige y fija el modo de video. `forzar` puede ser NULL (detectar por el cable) o
 * "vga", "ntsc" o "pal". Devuelve 1 si lo pedido se pudo dar tal cual. */
int video_init(const char *forzar);

/* Para el informe en pantalla: "480p VGA", "480i NTSC"... */
const char *video_mode_name(void);
/* Que dijo la deteccion, en texto. */
const char *video_cable_name(void);
/* Si el modo elegido no es el que pedia el cable (por haberlo forzado). */
int video_forced(void);

#endif /* PLATFORM_DC_VIDEO_H */
