# M2a: el front end en pantalla

**Criterio de salida (CLAUDE.md §4):** front end navegable y legible en 480p y 480i.

Es el primer hito con **imagen**. Hasta M1b el port cuenta lo que el juego quiere dibujar
y no dibuja nada: no hay una sola llamada al PVR en el arbol.

## 1. Lo que el front end pide, medido

120 frames del guion `front_end_120.txt`, en el host de PC:

| | |
|---|---|
| Formas 2D (`0x8004E74C`) | **16 079** (~134 por frame) |
| Cuadros (`PSXDrawSquare`, `PSXDrawTransSquare`) | **2 040** (~17 por frame) |
| Listas de dibujo (`DrawOTag`) | 119, una por frame |
| Flips (`PutDispEnv`) | 122 |
| Escrituras a la VRAM de PS1 | 790 (~6,6 por frame) |
| Lecturas de VRAM | 0 |
| Area de pantalla del ultimo `PutDispEnv` | **512x240** |

~150 primitivas por frame es **poca cosa para el PVR**. El trabajo de M2a no esta en el
volumen de triangulos: esta en traducir la VRAM y las paletas de PS1 a texturas del PVR,
y en que el resultado se lea bien en una tele.

**Ojo con la resolucion.** El front end reporta `512x240` y una carrera reporto
`320x240`. CLAUDE.md §3 dice «las coordenadas 2D se escalan x2», que cuadra con 320x240
-> 640x480 pero no con 512x240 -> 1024x480. **Hay que medir el modo de verdad antes de
fijar el escalado**, y puede que no sea un factor unico.

## 2. El contrato: que es "dibujar" en este port

La referencia convierte el dibujo del juego en una **lista de comandos**
(`src/ps1/host/overlay.rs`), y su renderer la pinta. El port tiene que producir la misma
lista. Los comandos que importan para el front end:

| Comando | De donde sale |
|---|---|
| `Quad` | un poligono o sprite en coordenadas de pantalla, con color por vertice y, opcionalmente, textura (`tpage`, `clut`, UV) y semitransparencia |
| `Text` | `Font_TextXY` |
| `Rect`, `ThinBox`, `PauseBox` | los ayudantes del HUD |
| `Shape` | `Hud_FBuildSprite` |
| `Clip` | `SetDrawArea` |
| `Bitmap` | `Draw_DrawDirectScreen` |
| `ShowroomCar`, `ShowroomFloor`, `Movie` | el concesionario y los videos: **no son M2a** |

Las primitivas crudas que hay que parsear son el juego estandar de la GPU de PS1:
`0x20-0x3F` poligonos, `0x60-0x7F` sprites, `0x40-0x5F` lineas y `0xE1` modo de dibujo.

## 3. Orden de trabajo, y por que ese

| # | Paso | Cierra con |
|---|---|---|
| 1 | **Grabacion en C** (`host/overlay.c`): de los paquetes del OT a la lista de comandos | la lista del port y la de la referencia **coinciden** en el guion del front end |
| 2 | **Modos de video** (`platform/dc/video.c`): 480p a 31 kHz y 480i a 15 kHz, con `vid_check_cable` y forzado | arranca en los dos y lo dice en pantalla |
| 3 | **VRAM y paletas -> texturas del PVR**: cache por `(tpage, clut, profundidad)`, invalidada al escribir la VRAM | cuantas texturas y cuanta VRAM del PVR hacen falta, medido |
| 4 | **Dibujo 2D en el PVR**: cuadros con color por vertice, textura, color key y semitransparencia | el front end se ve |
| 5 | **Texto y ayudantes del HUD** | el front end se **lee** |

El paso 1 va primero porque es **codigo portable y tiene oraculo**: la referencia produce
la misma lista, asi que se puede comparar igual que se compara la RAM, sin tocar un
Dreamcast. Del paso 2 en adelante ya no hay oraculo y hay que mirar la pantalla.

## 4. Lo que ya esta hecho y se aprovecha

- `host_capture_prims` (views.c) **ya corre las rutinas de dibujo del front end en un
  marco anidado** y marca `capturing`. Lo que falta es grabar lo que escriben.
- El **espejo de la VRAM** (1024x512 de 16 bits, 1 MB) ya se mantiene en `gpu.c`, con
  `vram_writes` contadas.
- `SetDrawArea` y `PutDispEnv` ya se interceptan y se anota la esquina del buffer.
- El **mando ya esta mapeado** (maple -> botones de PS1), asi que en cuanto haya imagen
  el front end se puede navegar a mano.

## 5. Riesgos propios de M2a

- **VRAM del PVR** (CLAUDE.md §11): PS1 sube texturas y paletas de forma dinamica. Si el
  numero de combinaciones `(tpage, clut)` por frame es alto, la cache puede hacer thrash.
  El paso 3 lo mide antes de disenarla.
- **El PVR no tiene blending sustractivo ni shaders**: el x2 de color de PS1 se calcula
  por vertice en el SH-4. Para el front end puede bastar; las sombras y reflejos
  sustractivos son problema de M2c.
- **Legibilidad**: el original dibuja para 240 lineas entrelazadas en una tele CRT. A
  480p el texto fino puede quedar peor, no mejor. Es el criterio de salida, asi que se
  mira pronto.
