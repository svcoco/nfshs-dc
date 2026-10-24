# M2b: la pista y el cielo en el PVR

**Criterio de salida (CLAUDE.md §4):** una pista recorrible; ms/frame y poligonos medidos.

Es el hito que convierte el negro de la carrera en algo mirable. Hasta M2a el port dibuja
el 2D del front end; en carrera la lista de dibujo sale **vacia** (0 cuadros en el frame
900) porque la geometria del mundo no pasa por el OT: `Render_RenderPlayerView`
(`0x800B4024`) es HLE y el juego nunca llega a construirla.

## 1. El dimensionado, medido antes de disenar

### Lo que el juego pide por frame

`dc/test/diff/scripts/race_long.txt`, 5 000 frames de carrera, host de PC:

| | |
|---|---|
| Vistas de carrera preparadas | 4 769 |
| **Trozos de pista por vista** | **media 8,4, maximo 18** |
| Llamadas a `LoadImage` | 4 071 |
| Area de pantalla en carrera | **320x240** (el front end es 512x240) |

### Lo que hay en una pista

`ZTR00.GRP` (448 064 bytes), con `nfshs dump-track` y `NFSHS_BLOCKS=all`:

| | |
|---|---|
| Nodos / bloques | 129 / 129 |
| Texturas | 374 |
| Muestras de la carretera virtual | 1 029 |
| Cuadros de suelo (`ground`) | **16 610** (media 129 por bloque) |
| Cuadros de suelo lejano (`far`, el LOD) | 5 679 (media 44) |
| Cuadros de detalle (`detail`) | 1 436 (media 11) |
| Colocaciones de objetos (`placements`) | 1 089 (media 8,4) |

`far` **no se suma**: es la version de bajo detalle de `ground` para los bloques lejanos,
y el juego dice cual quiere en la propia lista de trozos.

### Y de ahi, el presupuesto

| | media | pico |
|---|---|---|
| Bloques por vista | 8,4 | 18 |
| Cuadros a pintar | ~1 180 | ~2 520 |
| **Triangulos a pintar** | **~2 360** | **~5 040** |

**El PVR no es el problema.** Un Dreamcast mueve del orden de 3 millones de poligonos por
segundo, o sea unos 50 000 por frame a 60 Hz: el pico de la pista es el **10 %** de eso.
El coste real va a estar en el **SH-4**, que tiene que transformar unos 5 000-10 000
vertices por frame *ademas* de correr la logica recompilada, y en los fallos de I-cache
que ya pesan con 3,43 MB de codigo.

**Y la pista entera cabe en RAM.** 18 046 cuadros son del orden de 200 KB de caras mas su
charco de vertices: **no hace falta streaming**, se carga la pista al entrar y se queda.
Eso quita de encima el problema mas caro que tendria el hito.

## 2. Las dos decisiones que no son obvias

### De donde sale la geometria: del asset, no de la RAM del juego

El juego **no** construye la malla del mundo en su RAM: solo dice **que bloques** se ven y
con **que nivel de detalle** (hasta 64 entradas en `0x801102B0`, pares de `i16` id y `u16`
detalle). La malla esta en el GRP. La referencia la parsea con `src/formats/grp.rs` y el
port la **hornea en el PC** (CLAUDE.md §3), que es la decision vigente.

### De donde salen las texturas: del PSH horneado

Medido: el juego llama a `LoadImage` 4 071 veces en una carrera, asi que **sus texturas si
pasan por la VRAM de PS1** y la cache de M2a las vera. Pero los cuadros del GRP referencian
su textura por **indice en la tabla del GRP** (`Trk_Material`), que apunta a una forma del
PSH; para resolverlo por la VRAM haria falta la tabla forma -> pagina que el juego arma al
cargar, y eso es ingenieria inversa que no esta hecha.

Por el PSH esta resuelto: es el camino que la referencia ya demuestra y la decision de §3
ya lo fija. **Las texturas de pista se hornean**; la cache de VRAM de M2a se queda para el
2D, que es lo que graba.

## 3. Orden de trabajo

| # | Paso | Cierra con |
|---|---|---|
| 1 | **Captura de la vista** en el host de C: camara, matriz, proyeccion, rectangulo y lista de trozos | **Hecho (2026-10-23)**: la captura del port y la de la referencia **coinciden** en el guion de carrera |
| 2 | **Horneado de la pista** (PC, Rust, reutilizando `src/formats`): GRP + PSH -> malla y texturas listas para el PVR | **Hecho (2026-10-23)**: tamano del horneado y de su atlas, medidos |
| 3 | **Transformacion** y **recorte por plano cercano**, y dibujo en el PVR | **Hecho (2026-10-23)**: la pista se ve en Dreamcast a 53-60 fps con 1 038 triangulos por frame |
| 4 | **El cielo** | el horizonte se ve |

El paso 1 va primero por la misma razon que en M2a: es **codigo portable y tiene
oraculo**. La referencia graba exactamente lo mismo en su `ViewCapture`, asi que se puede
comparar campo a campo dentro de `make diff`, sin tocar un Dreamcast y sin mirar una
pantalla. Del paso 2 en adelante ya hay que mirar.

### Que es la captura de la vista (paso 1)

Es poco codigo y esta todo localizado. La referencia lo lee asi (`src/ps1/host/views.rs`):

| Campo | De donde |
|---|---|
| `camera_pos[3]` | `RENDER_VIEW + 8`, tres `i32` |
| `camera_rot[9]` | `RENDER_VIEW + 0x14`, nueve `i32` |
| `projection` | `CAMERA_ZOOM` |
| `rect[4]` | `Draw_gViews[v]`: el recorte relativo al de la vista 0, y su tamano |
| `chunks` | `0x801102B0`, pares (`i16` bloque, `u16` detalle) |

La **cuenta** de trozos ya la calculaba el port (`race_view_logic` en `host/game.c`);
lo que faltaba era quedarse con la lista y con la camara.

**Cerrado el 2026-10-23 con oraculo:**

```
vista de carrera:
  C: 900 frames, referencia: 900 frames (900 en comun)
OK: 669 vistas identicas en 900 frames
```

Los dos lados vuelcan la vista por frame en el mismo formato (`NFSV`) y
`dc/test/diff/cmpview.py` las compara campo a campo dentro de `make diff`: jugador,
recorte, posicion, los nueve enteros de la matriz, proyeccion y la lista entera de
trozos con su nivel de detalle. En un guion de front end no hay ninguna vista, asi que
los dos lados salen vacios y el paso no estorba.

**Deuda que este guion destapo, anotada y no olvidada:** en los frames **126 a 130**, que
son los menus justo antes de entrar a la carrera, la lista 2D del port tiene **dos cuadros
menos** que la de la referencia (239 contra 241). No lo veia nadie porque
`front_end_120.txt` no llega hasta ahi. No bloquea M2b —es 2D de menus, no mundo—, pero
hay que mirarlo.

## 4. Riesgos propios de M2b

- **El SH-4 y los 64 bits.** La camara viene en enteros de PS1 y la transformacion natural
  del PVR es en `float` con FTRV. Hay que fijar el convenio una sola vez y medirlo, no
  mezclar.
- **I-cache.** El bucle de transformacion compite con 3,43 MB de codigo recompilado. Es el
  riesgo de CLAUDE.md §11 y aqui es donde se va a notar por primera vez.
- **Recorte por plano cercano.** El PVR no recorta: hay que partir los triangulos que
  cruzan el plano, en el SH-4, o se cuelan vertices detras de la camara.
- **El orden.** La pista es opaca y va a la lista `OP` con Z-buffer, al reves que el 2D de
  M2a, que es el algoritmo del pintor en la lista `TR`. Son dos regimenes distintos en el
  mismo frame.

## 5. Paso 2 hecho: el horneado (2026-10-23)

`dc/tools/bake/bake.rs`, invocado como `nfshs bake-track` y desde `make -C dc bake`. Vive
bajo `dc/` porque es codigo del port, y se compila dentro de la referencia Rust porque
reutiliza `src/formats` (CLAUDE.md seccion 3); lo unico que toca de upstream son dos
lineas en `src/dev/mod.rs`.

### Que se decide en el PC y que no

Todo lo que se puede resolver antes de arrancar, se resuelve:

- **Las esquinas de UV**, con sus indicadores (`Trk_Material::uvFlag`: 0x10 voltea U, 0x40
  voltea V, y luego 0x02 / 0x04 / 0x08 giran 90 / 180 / 270) ya aplicados y en el orden en
  que `DrawW_DrawQuad` manda los puntos. El SH-4 solo mira un rectangulo y cuatro ranuras.
- **Las texturas**, empaquetadas en un atlas y ya en ARGB1555 del PVR.
- **La doble cara** (`Trk_Material` bit 0x10) y la **semitransparencia**, que en PS1 sale
  de que la paleta de la forma tenga un color con STP, no de lo que diga el material.

Y una cosa **no** se hornea, a proposito: **el color de vertice**. Sale de
`Chunk_lightTable`, que vive en la RAM del juego y cambia con la pista, la variante y el
clima, asi que cada vertice se queda con su **indice de sombra** y el port busca el color
al dibujar. Es lo que mantiene la noche y la lluvia funcionando sin rehornear.

### El formato

Un archivo por pista y variante, todo little-endian:

| Tramo | Contenido |
|---|---|
| cabecera | `"NFST"`, version, bloques, materiales, ancho y alto del atlas, vertices, cuadros, offset del atlas |
| bloques | 44 bytes: origen `i32[3]` en 1/64 m y cinco pares (offset `u32`, cuenta `u16`): vertices y cuadros de suelo, cuadros lejanos, y vertices y cuadros de detalle |
| materiales | 16 bytes: rectangulo en el atlas, `u8 corner[4]`, indicadores |
| vertices | 8 bytes: `i16 x, y, z` en 1/64 m relativos al origen del bloque, `u8` indice de sombra |
| cuadros | 10 bytes: `u16 v[4]` dentro del charco de SU bloque, `u16` material |
| atlas | ARGB1555 |

El horneado **se verifica a si mismo**: relee lo escrito y comprueba que cada tramo cae
dentro del archivo, que todo indice de vertice cae dentro de su propio charco y que todo
material existe. Un fallo de esos en el PC cuesta media hora; en el Dreamcast es una
excepcion de direccion sin pista de donde viene.

### Medido, las 40 pistas

| | |
|---|---|
| Pistas horneadas sin error | **40 / 40** |
| Bloques | 105 a 136 |
| Vertices | ~21 000 a ~26 000 |
| Cuadros | ~18 000 a ~24 000 |
| Geometria | ~380 a 435 KB |
| Atlas | **24 pistas en 1024x512 (1 MB)**, 16 en 1024x1024 (2 MB) |
| Archivo entero | 1 375 a 2 497 KB |

Con 2 MB de atlas en el peor caso, mas el 1 MB de la cache de texturas de M2a y los dos
buffers de pantalla (1,2 MB), van ~4,2 MB de los 8 del PVR. Entra, pero M2c todavia tiene
que meter coches y objetos: si aprieta, el atlas se puede partir en varios, porque la
pista es opaca con Z-buffer y el orden de dibujo no importa.

### Un intento que se midio y se tiro

El empaquetador mete las formas **por orden de altura**, de mayor a menor, y las va
poniendo en baldas. Intente mejorarlo llenando el resto de cada fila con la forma **mas
ancha** que todavia cupiese, que es lo que suena bien. Medido sobre las 40 pistas, es
**peor**: arrastra piezas anchas y bajas a baldas altas y desperdicia la altura, y las 40
pasaban a necesitar 1024x1024 (600 a 977 filas) en vez de que 24 cupieran en 1024x512. El
codigo lo cuenta para que nadie lo reintente.

## 6. Paso 3 hecho: la pista en pantalla (2026-10-23)

`dc/src/render/track.c` (portable) lleva el .TRK a vertices de pantalla;
`dc/src/platform/dc/render.c` los manda al PVR y `dc/src/platform/pc/raster.c` los pinta
por software para poder **mirarlos en el PC** antes de encender nada.

### El convenio de la camara, que es donde se juega el partido

Sale de la referencia (`src/game/original.rs`) y conviene no volver a deducirlo:

- `camera_pos` va en **16.16** y su **X esta espejada** respecto al mundo horneado.
- `camera_rot` son tres filas —derecha, arriba, adelante— y **no estan normalizadas**: el
  juego aplasta la imagen en vertical escalando la fila de "arriba" (sale sobre 0,85).
  Dividir las tres por la longitud de "adelante" deja la z en metros y **conserva ese
  aplastamiento** sin tratarlo aparte.
- `projection` es la distancia focal en pixeles de PS1, y se proyecta como PS1:
  `sx = x * projection / z`. Luego se escala de la pantalla de PS1 (320x240 en carrera) a
  640x480.
- El **color de vertice** sale de `Chunk_lightTable` (puntero en `0x8013DB0C`), que el
  host relee solo cuando el puntero se mueve.

### Lo medido en Dreamcast (Flycast, BIOS real, `race_long.txt`)

| | |
|---|---|
| **Triangulos por frame** | **1 038** (1 811 067 en 1 744 frames) |
| **fps en carrera, dibujando** | **53-60** (16,7-18,7 ms por frame) |
| Bloques por vista | 9-10 |

Contra los **98-113 fps** que daba la carrera **sin dibujar** (M1b), dibujar la pista
cuesta del orden de la mitad del frame. Sigue por encima del minimo de 30 fps sostenidos
que fija CLAUDE.md seccion 3, y por ahora **sin FTRV**: la transformacion va en C con
`float` normal. Esa optimizacion es de M3 y ahora hay con que medirla.

### Cuatro fallos reales, y ninguno estaba donde parecia

1. **Un struct superpuesto al archivo.** El registro de bloque son 44 bytes con pares
   `(u32, u16)` de **seis**; en C el compilador los rellena a ocho y el struct mide 56.
   Superponerlo al archivo daba un fallo de segmentacion. Los bloques se **decodifican**
   byte a byte al cargar, que ademas evita lecturas desalineadas (en SH-4, una excepcion).
2. **El cuadro se partia como tira.** Sus cuatro vertices van en orden **ciclico**
   alrededor del cuadro, asi que se parte en (0,1,2) y (0,2,3). Partirlo en (0,1,2) y
   (1,3,2) dejaba huecos triangulares por toda la pista.
3. **La lista opaca del PVR estaba en `PVR_BINSIZE_0`.** Mandarle geometria a una lista
   sin buffer de punteros **no da error**: deja la pantalla en negro y hunde el ritmo a
   4 fps. M2a solo usaba la lista translucida, asi que nunca se habia notado.
4. **El nombre de la pista, en minusculas.** El juego abre `zTr06.grp` y el ISO del disco
   guarda `ZTR06.TRK`. Ademas la carga fallida **se reintentaba en cada frame**, lo que
   por si solo ya costaba un `fopen` por frame.

### Como sabe el port que pista cargar

No hace falta leerselo al juego por ningun sitio raro: el host se queda con el nombre del
**`.GRP` que el juego abre** (`host/files.c`) y la plataforma carga
`/cd/BAKED/<NOMBRE>.TRK`. `mkdisc.sh --baked` mete las pistas horneadas en el disco.
