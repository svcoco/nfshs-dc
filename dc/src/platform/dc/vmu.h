/* M6: la memory card de PS1 en la VMU (ver vmu.c). */
#ifndef PLATFORM_DC_VMU_H
#define PLATFORM_DC_VMU_H

#include "host.h"

/* Las ranuras 1 y 2 sobre las VMU de los puertos A y B: monta lo que haya y deja al host
 * guardando en ellas. Despues de host_init. */
void tarjeta_vmu_init(GameHost *h);

/* Escrituras hechas en la VMU, las que fallaron y lo que tardo la ultima (para el informe). */
void tarjeta_vmu_cifras(uint32_t *escritas, uint32_t *fallos, uint32_t *ultimo_ms);

#endif /* PLATFORM_DC_VMU_H */
