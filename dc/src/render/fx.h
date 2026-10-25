/* Efectos de carrera (M2c, paso 6): las marcas de neumatico, que el original dibuja
 * restando, y la profundidad de los destellos.
 *
 * Portable: sin cabeceras de KOS. Producen vertices de pantalla por el mismo sumidero que el
 * resto del renderer; la resta exacta la hacen las mezclas del PVR (ver render.c), asi que
 * aqui solo se decide DONDE va cada cuadro, de que color y a que profundidad.
 */
#ifndef PS1_RENDER_FX_H
#define PS1_RENDER_FX_H

#include "track.h"
#include "overlay.h"

struct SkidQuad;
struct Glow;

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

/* La profundidad de un destello (farola, halo de coche o bocanada de humo): sus cuadros ya
 * estan en la pantalla de PS1, donde los puso el juego, y solo les falta el 1/w con el que
 * el PVR los prueba contra lo opaco. Escribe 4 valores por cuadro en `z` (los `g->n`
 * cuadros desde `q`) y devuelve 0 si el destello queda detras de la camara.
 *
 * La regla es la de la referencia (src/game/layers.rs), que el original resuelve
 * ordenandolos en su tabla a la profundidad de su luz:
 *   - una luz, algo por DELANTE de ella (el doble de su tamano mas un metro, como mucho
 *     la mitad de la distancia): por encima de su farol y la pared en la que esta, por
 *     debajo de lo que haya mas cerca;
 *   - un halo de coche, por delante del coche entero (su radio mas 0,2 m);
 *   - una bocanada, 0,5 m por delante de su punto, y por debajo de el con la profundidad
 *     de la carretera que tapa: es un dibujo plano de pie sobre el suelo, y la carretera mas
 *     cercana no debe comersele la base. */
int glow_depth(const struct RaceView *view, const struct Glow *g, const OverlayQuad *q,
               float *z);

#endif /* PS1_RENDER_FX_H */
