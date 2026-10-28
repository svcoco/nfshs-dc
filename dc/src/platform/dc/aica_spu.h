/* El middleware de audio en Dreamcast (M4). Ver aica_spu.c. */
#ifndef PLATFORM_DC_AICA_SPU_H
#define PLATFORM_DC_AICA_SPU_H

#include "spu.h"

/* Conecta el stub del SPU al AICA. 0 si no hay sonido (el juego sigue, mudo). */
int  aica_spu_init(SpuStub *spu);
void aica_spu_report(void);

#endif /* PLATFORM_DC_AICA_SPU_H */
