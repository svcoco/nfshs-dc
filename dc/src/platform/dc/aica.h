/* El sonido de KOS con el driver ARM que funciona en Flycast. Ver aica.c. */
#ifndef PLATFORM_DC_AICA_H
#define PLATFORM_DC_AICA_H

/* snd_stream listo para usar (la primera vez lo inicializa). 0 si no hay sonido. */
int aica_listo(void);

#endif /* PLATFORM_DC_AICA_H */
