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

Estado (2026-10-23): **los cinco pasos cerrados**. Falta el criterio de salida del hito,
que es mirar el front end en 480i en una tele. **Decision de Jorge (2026-10-26):** esa
prueba pasa a M7 (consola real, 480i y 480p) y M2 se cierra en emulador.

El paso 1 va primero porque es **codigo portable y tiene oraculo**: la referencia produce
la misma lista, asi que se puede comparar igual que se compara la RAM, sin tocar un
Dreamcast. Del paso 2 en adelante ya no hay oraculo y hay que mirar la pantalla.

## 3bis. Paso 1 hecho: la grabacion (2026-10-23)

`dc/src/host/overlay.c` convierte el dibujo del front end en una lista de cuadros en
coordenadas de pantalla, por las **tres vias** que usa el juego:

- el **emisor de formas** (`0x8004E74C`), que es el grueso: lee la descripcion de la
  forma en la RAM del juego y la parte en varios cuadros, porque una pagina de textura
  de PS1 solo tiene 256 texeles de ancho;
- `PSXDrawSquare` y `PSXDrawTransSquare`, cuadros planos (el transparente se dibuja
  varias veces encima de si mismo, que es como el original oscurece);
- las rutinas que **construyen sus propias primitivas** (DrawTV y companeras), que ya se
  corrian anidadas: ahora se graba lo que escriben en el buffer de primitivas, parseando
  el juego estandar de la GPU de PS1 (`0x20-0x3F` poligonos, `0x60-0x7F` sprites,
  `0x40-0x5F` lineas, `0xE1` modo de dibujo).

### Medido, 120 frames de front end

| | |
|---|---|
| Cuadros grabados | **34 525** |
| Maximo en un frame | **300** |
| Primitivas que no se supieron grabar | **0** |
| Cuadros que no cupieron | **0** |
| Combinaciones `(pagina, paleta)` distintas, en total | **63** |
| Combinaciones **vivas a la vez en un frame** | **54** |

(Cifras con el texto ya incluido; las de una medida anterior sin el eran menores.)

**Las 54 son el numero que manda, y avisan de un problema.** Si la cache convirtiera una
pagina de textura entera por combinacion (256x256 texeles a 16 bits = 128 KB), serian
**6,9 MB de los 8 MB de VRAM del PVR**. O sea que el paso 3 **no puede** cachear paginas
enteras: tiene que convertir solo el rectangulo que los UV usan de verdad, que en un
sprite de interfaz es pequeno. Queda anotado antes de disenarla.

Sin regresion: 227/227 unitarias, 120 frames identicos y la misma divergencia conocida en
el 232. Era de esperar —la grabacion solo **lee** la RAM del invitado— pero se comprobo.
El arbol portable sigue compilando para SH-4 sin un solo aviso.

### El oraculo: **34 525 comandos identicos** (2026-10-23)

Paso 1 cerrado. Los dos lados vuelcan su lista de dibujo por frame en el mismo formato
—el port con `--overlaydump`, la referencia con `NFSHS_DIFF_OVERLAY`— y
[`cmpovl.py`](../../dc/test/diff/cmpovl.py) las compara comando a comando. Va dentro de
`make diff`, asi que no se puede romper sin enterarse.

```
comandos (cuadros y recortes): C 34525, referencia 34525
comandos de la referencia que el port no produce todavia: 0
OK: 34525 comandos identicos en 120 frames
```

**El oraculo encontro dos cosas que las cifras sueltas no habrian visto:**

1. **Faltaban ~91 cuadros por frame: el texto.** `Font_TextXY` en el front end no produce
   un comando de texto, produce **un cuadro por caracter** recortado de la imagen de 4
   bits de la fuente. El port lo contaba y lo descartaba. Portado como
   `overlay_font_quads`, con sus dos rutinas de pintado: la normal (semitransparente, en
   la paleta y color actuales) y la del reflejo en el suelo del concesionario (opaca,
   volteada y con paleta propia).
2. **Faltaban los recortes.** `SetDrawArea` es un **hook**, no una HLE, y el host solo
   tenia enganchado `systemtask`: nadie lo miraba. El front end lo usa 11 veces por frame
   para recortar el texto que se desplaza, y sin eso el paso 4 habria pintado cosas fuera
   de su sitio. La lista pasa a tener tipo de comando (`OVL_QUAD`, `OVL_CLIP`).

Eso deja la grabacion del front end **completa**: cero comandos de la referencia sin
producir.

**En carrera la lista NO coincide, y es lo esperado**: 68 361 comandos contra 210 157. La
referencia graba ademas la tabla de ordenacion de las vistas de carrera (HUD,
cuentavueltas, mapa) y toda la geometria del mundo, que es **M2b y M2c**. El oraculo de
M2a mide el front end, que es su criterio de salida.

## 3ter. Paso 2 hecho: modos de video (2026-10-23)

`dc/src/platform/dc/video.c`. Elige el modo por el cable y permite forzarlo.

| Cable | Modo |
|---|---|
| VGA | **480p VGA** (31 kHz) |
| RGB/SCART o compuesto | **480i**, NTSC o PAL segun la region de la consola |

Para forzarlo, el disco puede llevar `/cd/VIDEO.TXT` con `vga`, `ntsc` o `pal`;
`mkdisc.sh --video ntsc` lo escribe. Forzar VGA sin cable VGA **no se obedece**: la
consola solo saca 31 kHz por ese cable y la tele se quedaria en negro, asi que se avisa
y se cae al entrelazado.

Comprobado en Flycast con la BIOS real, cambiandole el cable:

```
cable VGA        -> video: 480p VGA   (cable: VGA)
cable compuesto  -> video: 480i NTSC  (cable: compuesto)
cable VGA + VIDEO.TXT=ntsc -> video: 480i NTSC  (cable: VGA)  [forzado]
```

### Un dato medido que corrige un detalle de CLAUDE.md seccion 3

La seccion 3 dice que «las coordenadas 2D se escalan x2». Medido con `NFSHS_ENVDBG`
sobre los dos guiones, **el juego no dibuja a 320x240**: su pantalla es de **512x240**,
con doble buffer en la VRAM de PS1 (en y=0 e y=256), tanto en el front end como en
carrera. Las sub-areas de 320x240 que aparecen son vistas, no la pantalla.

Asi que el x2 vale para la vertical (240 -> 480) pero no para la horizontal: 512 -> 640
es **x1,25**. La regla que si se sostiene, y que es lo que la seccion 3 pide de verdad,
es **llevar el area de pantalla de PS1 a 640x480**. No cambia ninguna decision; cambia
un factor que habria salido mal en el paso 4.

## 3quater. Paso 3 hecho: la cache de texturas (2026-10-23)

`dc/src/render/texcache.c`, portable. Convierte las paginas de 4 y 8 bits de la VRAM de
PS1 (mas su paleta) a ARGB1555, que es lo que el PVR entiende sin shaders.

### Lo que decidio el diseno, medido con `dc/tools/ovlstats.py`

| En el peor frame del front end | |
|---|---|
| Guardando la **pagina entera** por combinacion | **6,75 MB** — no cabe en los 8 del PVR |
| Guardando solo el **rectangulo que usan los UV** | **0,66 MB** |
| Profundidad de las paginas | 8 bits (5 280 usos) y 4 bits (842); **ninguna de 16** |
| Escrituras a la VRAM de PS1 | 6,6 por frame |

De ahi las tres decisiones: se convierte el **rectangulo**, no la pagina; la cache
**persiste entre frames** (la VRAM casi no cambia); y se invalida **por solape**, no en
bloque.

### Como asigna, y por que asi

Asignador **por tope**: una vez convertida, una entrada no se mueve. El rectangulo del
frame **no se acumula** entre frames —hacerlo crecia sin parar hasta desbordar— sino que
se recalcula y solo se reconvierte si lo que pide el frame no cabe en lo ya convertido.
Cuando el buffer se llena se vacia entero y se empieza de cero, que con una interfaz
estatica pasa muy de vez en cuando.

### Medido en 120 frames

| | |
|---|---|
| Conversiones en toda la ejecucion | **63** (438 976 texeles) |
| Pico de texeles vivos | **438 976** (0,84 MB) |
| Invalidaciones por escritura a la VRAM | 0 |
| Veces que no cupo algo | 0 |

### Y medido en 3 000 frames, que es donde aparecio el fallo

120 frames se quedan cortos. La tabla tiene 128 entradas y **al principio no expulsaba
nada**, asi que a la larga se llenaba de combinaciones viejas:

| | sin expulsion | con expulsion |
|---|---|---|
| Conversiones | 267 | 380 |
| Invalidaciones | 89 | 101 |
| Expulsiones | — | 39 |
| Vaciados del buffer | 4 | 4 |
| **Peticiones sin textura** | **13 654** | **0** |
| Cuadros sin dibujar en el frame 2 800 | 11 | 0 |

Se expulsa la entrada que lleva **mas tiempo sin pedirse**, y nunca una del frame en
curso, que se estaria dibujando ahora mismo. Sus texeles quedan huerfanos en el asignador
por tope hasta el siguiente vaciado: cuatro vaciados en 3 000 frames es mucho mas barato
que no dibujar el cuadro.

63 conversiones para 63 texturas distintas: **cada una se convierte una sola vez**. En
SH-4 eso quiere decir que el coste es del arranque, no del frame.

Las 0 invalidaciones no son un fallo: el juego escribe en los buffers de pantalla
(x < 512) y las paginas de textura estan a la derecha, asi que no se pisan.

### La validacion: verlo

Este paso no tiene oraculo. Para poder mirarlo hay dos herramientas nuevas:

- `dc/tools/texview.py` compone las texturas convertidas en una imagen.
- `dc/src/platform/pc/raster.c` es un **rasterizador por software SOLO del arnes de
  PC** —en Dreamcast dibuja el PVR— que pinta la lista de dibujo con las texturas ya
  convertidas. `--shot <frame> <archivo.ppm>`, y `dc/tools/ppm2png.py` para verlo.

Con eso el front end sale reconocible: el menu *game setup*, sus cuatro opciones, el
icono, el texto de ayuda y la foto del coche, cada cosa en su sitio.

**Y encontro algo que el PVR tambien va a necesitar: el orden.** La lista se guarda con
el **ultimo dibujado primero**, porque los paquetes entran en la tabla de ordenacion por
la cabeza. Dibujandola en orden de construccion solo se ve el texto sobre negro; hay que
recorrerla del final al principio.

### Un detalle que el PVR tendra que resolver

PS1 marca con el bit STP los texeles que se mezclan en las primitivas semitransparentes,
y la referencia los guarda como alfa 128. ARGB1555 solo tiene **un bit** de alfa, asi que
esa distincion no cabe en el formato. **Resuelto en el paso 4 por modo de mezcla del
cuadro entero**, no por texel; si algun efecto lo necesita por texel, se replantea.

## 3quinquies. Pasos 4 y 5 hechos (2026-10-23)

### Paso 4: el dibujo en el PVR

`dc/src/platform/dc/render.c`. Cada cuadro es una tira de dos triangulos en la lista
**translucida** con el **autosort desactivado**: manda el orden de envio, porque esto es
un algoritmo del pintor y no una escena con profundidad. Sin test ni escritura de Z y
sin descarte de caras.

| Decision | Por que |
|---|---|
| Escalado **x1,25 horizontal y x2 vertical** | el front end dibuja en 512x240 y la salida es 640x480. **No es uniforme.** En carrera la pantalla de PS1 es 320x240, o sea x2 en los dos ejes |
| Color de vertice **duplicado y saturado** | en PS1 el 0x80 es el 1,0 y modula la textura; lo que PS1 podia pasar de 1,0 se pierde |
| Recorte **en software** sobre los cuadros alineados a los ejes, ajustando los UV | el recorte por hardware del PVR va por baldosas de 32x32 y no da el grano que necesita el texto desplazado del front end |
| Resubida de texturas **solo cuando la cache convierte algo** | 380 conversiones en 3 000 frames: casi nunca |

**El encuadre se midio, no se estimo.** Dibujando cuatro marcadores de 32x32 en las
esquinas de 640x480: el de (0,0) cae en el **primer pixel** de la zona dibujada y mide
exactamente 32x32 de pantalla. Por serie, `render: modo 640x480  bitmap en 172,40` y
`pantalla de PS1 512x240 -> 640x480`.

Lo que parecia un desplazamiento era la **captura**: PowerShell pide el rectangulo del
cliente en pixeles logicos y `PrintWindow` dibuja en fisicos, asi que con Windows al
125 % la imagen salia recortada por la derecha y por abajo. Capturando a x1,25 sale la
pantalla entera.

**Resultado:** el front end dibujado en Dreamcast a **~60 fps** (69-78 sin dibujar), con
el menu *game setup*, sus cuatro opciones, el icono, la foto del coche y el texto de
ayuda en su sitio, igual que el rasterizador de PC para el mismo frame.

### Paso 5: los ayudantes del HUD

Medido antes de escribir codigo: en 3 000 frames de front end los cuatro ayudantes
**no se llaman ni una vez**, no queda ninguna primitiva sin grabar y no hay ningun cuadro
sin dibujar. El front end ya se lee entero, que es el criterio del paso.

`Rect`, `ThinBox`, `PauseBox`, `Shape` y `Bitmap` son del **HUD de carrera**, y en
carrera la lista 2D sale hoy **vacia** (0 cuadros en el frame 900) porque la vista de
carrera todavia no se dibuja. Portarlos ahora seria escribir a ciegas contra un escenario
que no existe: van con **M2c**. El port los deja correr y los **cuenta**
(`views_hud_unported`), asi que el primer guion que los toque lo dira en el informe en
vez de divergir en silencio.

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
