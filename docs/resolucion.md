# Resolución de salida y resolución interna

Investigado el 2026-10-25, a pregunta de Jorge: ¿no debería el modo NTSC ser 720×480 (DTV) en
vez de 640×480? ¿Se puede subir la resolución interna 2×?

## Resumen

| Pregunta | Respuesta |
|---|---|
| ¿720×480 en vez de 640×480? | El Dreamcast **ya usa la temporización de 720×480** (la de Rec. 601 y la DTV 480p), pero coloca **640 píxeles** activos en cada línea. 720 no es «la resolución NTSC» que falte: son las muestras de la norma, con píxeles no cuadrados y margen de sobrebarrido. Usar más sería un modo propio y daría como mucho un 10 % más de píxeles en horizontal. |
| ¿Resolución interna 2×? | **Ya la tenemos** en el 3D de la carrera: el juego dibuja a 320×240 y el PVR rasteriza la geometría directamente a 640×480 en coma flotante. Es el equivalente a «2×» en un emulador de PS1, con precisión sub-píxel. |
| ¿Más allá de 640×480? | Solo en **horizontal**: el FSAA del PVR dibuja a 1 280×480 y promedia a 640. En vertical el PVR no tiene supermuestreo. |

## 1. 640 contra 720: lo medido

Tabla de modos de KOS (`kernel/arch/dreamcast/hardware/video.c`):

| Modo | Líneas | Relojes por línea | Inicio de imagen | Ventana de borde |
|---|---|---|---|---|
| `DM_640x480_VGA` (480p) | 525 | 858 | 172 | 126–837 |
| `DM_640x480_NTSC_IL` (480i) | 525 | 858 | 164 | 126–837 |
| `DM_768x480_NTSC_IL` (480i) | 525 | 858 | 96 | 46–837 |

- **858 relojes × 525 líneas** es exactamente la temporización de Rec. 601 (13,5 MHz en 480i)
  y la de la DTV 480p (CEA-861 «720×480p», 27 MHz, 31,47 kHz). Es de donde sale el
  «720×480»: 720 muestras activas por línea.
- El Dreamcast dibuja **640** de esas muestras. En 480i son 640 / 13,5 MHz = 47,4 µs de los
  53,3 µs activos (720 muestras). La parte 4:3 de la imagen son unas 704 muestras, así que
  la imagen de 640 cubre el **91 %** de su ancho, con bordes que la sobreexploración de un CRT
  suele tapar. En 480p la proporción es la misma, a 27 MHz.
- Por eso **640×480 es lo nativo del Dreamcast** en los dos cables, y es lo que usan sus
  juegos. KOS trae además 768×480 entrelazado (más ancho que el área activa, para sobrebarrido).

**Un modo de 720 (o 704) es posible pero no compensa ahora.** Haría falta un `vid_mode_t` propio
(720 no es múltiplo de los tiles de 32 del PVR: se dibujaría a 736 y se recortaría), ajustar la
proyección para que el 4:3 ocupe 704 muestras y 0,15 MB más de VRAM por par de búferes. La
ganancia real es de 640 a 704 píxeles en horizontal (+10 %), sin nada en vertical.

## 2. La resolución interna del port

| Qué | PS1 | Port |
|---|---|---|
| 3D de la carrera | 320×240 (rectángulo de la vista, medido), coordenadas enteras del GTE | el PVR lo rasteriza a **640×480** desde la geometría en `float`: ×2 en los dos ejes, con precisión sub-píxel |
| 2D del front end | 512×240 | escalado a 640×480 (×1,25 y ×2) |
| HUD y texturas | texeles de PS1 | los mismos texeles, más grandes; el detalle nuevo es cosa de M7 (texturas escaladas) |

El 3D no se escala a partir de una imagen de PS1: se dibuja de nuevo a la resolución de salida.
Por eso «subir la resolución interna 2×» ya está hecho en lo que se puede hacer sin cambiar
el modo de vídeo.

## 3. Por encima de 640×480: el FSAA del PVR

- `pvr_init_params_t.fsaa_enabled`: el PVR usa tiles del doble de ancho, se le manda la
  geometría a **1 280×480** y, al escribir el framebuffer, promedia cada par de píxeles. Es
  supermuestreo **horizontal 2×**: suaviza los bordes casi verticales, no los casi horizontales.
- En vertical **no hay** supermuestreo por hardware. La única vía sería renderizar a una
  textura más alta y reducirla, con el límite de 1 024 texeles de las texturas y el coste de
  dibujar el frame dos veces. No se recomienda.
- En 480i KOS ya activa el **filtro vertical anti-parpadeo** del escalador del PVR.

**Prueba hecha (temporal, sin commit):** con el FSAA encendido y todo el renderer a 1 280 de
ancho, la carrera se dibuja bien en Flycast, con el mismo encuadre. La diferencia visual es
difícil de apreciar en una captura de Flycast, que escala la ventana con suavizado, y **no se
sabe si Flycast reproduce el escalador del PVR** con fidelidad. El cambio en el renderer es
pequeño: 15 sitios de `render.c` que suponen 640 de ancho.

**Coste: sin medir.** El FSAA duplica el trabajo de los tiles del PVR (el rasterizado), no el
del SH-4. Hoy la carrera está limitada por el SH-4 (grabar los coches, 30–43 fps en Flycast),
así que podría salir casi gratis, pero eso solo se sabe en una consola: Flycast no modela los
tiempos del PVR.

## Recomendación

1. **Mantener 640×480** en los dos cables. Es la resolución nativa del Dreamcast y la que
   tiene sentido con su temporización.
2. **No hacer un modo de 720**: ganancia pequeña y un modo de vídeo fuera de lo estándar.
3. **El FSAA horizontal, como opción a evaluar en M6** con una consola delante: es una técnica
   del propio PVR (encaja con el objetivo de arquitectura) y el código ya está probado en
   emulador. Si cabe en el presupuesto del PVR, puede ir encendido por omisión o como opción.

La decisión es de Jorge.
