/* Efectos de carrera que el original dibuja restando (M2c, paso 6).
 *
 * Portable: sin cabeceras de KOS. Producen vertices de pantalla por el mismo sumidero que el
 * resto del renderer; la resta B*(1-F) la hace la mezcla del PVR (ver render.c), asi que
 * aqui solo se decide DONDE va cada cuadro y de que color.
 */
#ifndef PS1_RENDER_FX_H
#define PS1_RENDER_FX_H

#include "track.h"

struct SkidQuad;

/* Las marcas de neumatico (Draw_kCtrlSkidmark): cada cuadro une una entrada con la anterior,
 * texturado con SKD0 o SKD1 y con el color de cada una (degradado entre las dos).
 *
 * Se dibujan las de las secciones de pista que estan en la lista de trozos de la vista
 * (BWorld_IsSliceInBuildList: una seccion es un nodo y su trozo, el nodo / 8) y a menos de
 * 50 m (Draw_CircleClip, que estima la distancia como el mayor de |dx| y |dz| mas la cuarta
 * parte del menor). */
void skids_draw(const Track *t, const struct SkidQuad *sk, uint32_t n,
                const struct RaceView *view, float sw, float sh,
                TrackEmit emit, void *user, TrackStats *st);

#endif /* PS1_RENDER_FX_H */
