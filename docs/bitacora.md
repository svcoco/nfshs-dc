# Bitácora de desarrollo — nfshs-dc

Registro del trabajo **efectivo**: lo que está construido, las cifras medidas y las
decisiones vigentes. Solo hechos comprobados; los caminos que se probaron y se
descartaron no se guardan aquí.

Se actualiza al cerrar cada paso. El detalle largo vive en `docs/hitos/`; esto es el
resumen que permite retomar el proyecto sin releerlo todo.

**Última entrada: 2026-10-28** (cierre de M5 en emulador).

---

## Estado de un vistazo

| Hito | Estado |
|---|---|
| **M0** Viabilidad | Cerrado (2026-10-18) |
| **M1a** Backend C + `ps1rt` en C, en x86 | Cerrado (2026-10-18) |
| **M1b** Cross-compilar a SH-4 y arrancar | Cerrado en emulador (2026-10-23); la prueba en consola es el M7 |
| **M2a** El front end en pantalla | Cerrado en emulador; leerlo en una tele en 480i pasa a M7 |
| **M2b** La pista y el cielo en el PVR | Cerrado en emulador; **la carrera se ve a 60 fps** |
| **M2c** Coches, objetos y efectos | **Criterio cumplido en emulador** (2026-10-26): una carrera completa idéntica a la referencia en 40 000 frames; showroom, pausa, policía y noche; 30–43 fps |
| **M2** (a, b y c) | **Cerrado en emulador (2026-10-26)** |
| **M3** FMV | **Cerrado en emulador (2026-10-26)**: películas con sonido a sus 15 fps y video wall idéntico a la referencia; menús a ~40 fps |
| **M4** Audio | **Cerrado en emulador (2026-10-27)**: música, motor y efectos por el AICA; reverb en su DSP; reloj real en Dreamcast |
| **M5** Controles y 2 jugadores | **Cerrado en emulador (2026-10-28)**: el mando de Dreamcast como DualShock analógico, con vibración; pantalla partida idéntica a la referencia. Falta que Jorge lo pruebe a mano con dos mandos |
| M6 Guardado y VMU | Siguiente |
| M7 Consola real · M8 Optimización · M9 Enhancements · M10 Polishing | Sin empezar (plan de Jorge del 2026-10-28 en CLAUDE.md §4) |

**Lo que hay hoy (M5 cerrado en emulador):** lo de M2 y M3, más el **audio** (M4: música,
motor y efectos por el AICA, con la reverb en su DSP, y el reloj del juego siguiendo al
tiempo real) y los **controles** (M5: el mando de Dreamcast hace de DualShock analógico,
con dirección proporcional, pedales en los gatillos y vibración en el Jump Pack), también
**a dos jugadores**: selección de coche y carrera a pantalla partida.

**Lo de M2:** un CDI que arranca en Dreamcast con la BIOS
real, lee sus datos del disco y dibuja con el PVR tanto el **front end** (~60 fps, completo
y legible, con el coche del showroom, sus pinturas y su reflejo) como una **carrera completa**
—de la salida a la meta, las estadísticas y la repetición, con pausa—: pista, cielo, objetos del escenario, **coches** y **HUD** —cronómetro,
velocidad, marcha, tacómetro con aguja y mapa—, con las texturas y el color de vértice del
original. En carrera da **30–43 fps** en Flycast; el coste no está en dibujar sino en
**grabar los coches** (código real del juego cada frame), que es trabajo de M8. Del paso 6
de M2c (los efectos) ya se dibujan las **sombras** de los coches, las **líneas pintadas** de
la carretera, las **marcas de neumático**, los **destellos** de las farolas, los **halos** de
faros y frenos, el **humo**, el **flare del sol**, el **clima** (lluvia de día y de noche) y los
**reflejos** de los coches; y de noche, el **haz de los faros**.

---

## M0 — Viabilidad (cerrado 2026-10-18)

Medido sobre el disco original de Jorge (NTSC-U, SLUS-00826).

| | |
|---|---|
| `NFS4.EXE` / `FRONT.BIN` | 1 245 184 y 281 048 bytes |
| Funciones recompiladas | **3 674** |
| Instrucciones MIPS únicas | **245 518** (959 KB) |
| Direcciones HLE / hooks | 119 / 53 |
| RAM en uso en carrera | ~**1,84 MB** de 2 MB |
| Pila del juego | **12 KB** |

**Hallazgo:** el juego **no** se marca el ritmo con `VSync(N)`; la cadencia la impone el
host. Su lógica apunta a **60 Hz, un flip por campo de vídeo**, con la física atada al
root counter de 128 Hz y no al frame.

---

## M1a — El port en C, validado en x86 (cerrado 2026-10-18)

`recomp.py --lang c` genera las 3 674 funciones; `ps1rt` reescrito en C; host mínimo en
x86.

**Test diferencial contra la referencia Rust:** **120 frames idénticos** en el front end
y **231** en la carrera, comparando 512 hashes de página de 4 KB, los 32 registros, hi,
lo, cop0, los 64 del GTE, `cont` y ticks. Pruebas unitarias de `ps1rt`: **227/227**.

**Hallazgo que marcó el método:** los **cinco bugs** que encontró el test estaban todos
en la **integración host/juego**, ninguno en la traducción MIPS→C. Es el patrón que se
ha repetido en todos los hitos posteriores.

---

## M1b — En SH-4 y arrancando (cerrado en emulador 2026-10-23)

### Toolchain

`sh-elf-gcc` **15.2.0**, binutils 2.45.1, newlib 4.6.0, KallistiOS `8e704086`,
`-m4-single`. Instalado con dc-chain dentro del MSYS2 existente (`docs/instalar-sdk.md`).

El toolchain del **AICA (ARM7) no hace falta**: KOS trae
`kernel/arch/dreamcast/sound/arm/stream.drv.prebuilt` y su `Makefile` lo copia cuando no
encuentra compilador ARM. Lo único que se pierde es recompilar el firmware del AICA, que
este port no necesita.

### Tamaño del código: el riesgo número uno, cerrado

| | `.text` en SH-4 | B/instr MIPS |
|---|---|---|
| `-Os` | **3 595 380** (3,43 MB) | 14,6 |
| `-O2` | 8 233 000 (7,85 MB) | 33,5 |

Con 2 MB de RAM de PS1 y 1 MB de espejo de VRAM van 6,43 MB de 16: **no hacen falta
overlays**. Elegir `-Os` o `-O2` es cuestión de velocidad (I-cache de 8 KB), no de
espacio. Hoy se compila con `-Os`.

ELF del juego: `text 3,8 MB`, `bss 4,85 MB`. `ps1rt` pasa **227/227** en SH-4.

### Rendimiento medido (Flycast, con dynarec)

| | |
|---|---|
| Front end | 69–78 fps |
| Carrera | **98–113 fps** |
| Carga inicial desde el disco | ~20–35 s |

Son fps de **lógica**: no se dibuja nada. Como presupuesto, a 30 fps (33 ms) la lógica
se come ~15 ms. **Proyección, no medida:** la grabación de geometría que M2b tiene que
portar es el 34 % del trabajo de la carrera en la referencia (34,0 M de ticks frente a
nuestros 22,4 M), así que al portarla el coste de la lógica subirá en esa proporción.

### El disco

`dc/tools/mkdisc/mkdisc.sh` genera **CDI** (por omisión) o GDI.

- **CDI, disposición MIL-CD: es la única que arranca con la BIOS real.** Pista de audio
  muda y pista de datos desde el LBA **11702**, `1ST_READ.BIN` **cifrado** con
  `scramble`, IP.BIN diciendo `CD-ROM1/1`, ISO construido con `-C 0,11702`. Es también
  lo que se graba en un CD-R para hardware.
- **GDI:** datos en el área de alta densidad desde el LBA 45000, sin cifrar, IP.BIN de
  `GD-ROM1/1`. Arranca solo con REIOS. Se conserva porque es la disposición de un
  GD-ROM real y la que usaría un ODE.

Imagen actual: **558 MB** (552 MB de datos de juego, sin los FMV ni la imagen original
del CD).

---

## M2a — El front end en pantalla (cerrado en emulador 2026-10-23)

Primer hito **con imagen**. Criterio: front end navegable y legible en 480p y 480i.

| # | Paso | Estado |
|---|---|---|
| 1 | Grabación del dibujo 2D en C | **Hecho**, validado contra la referencia |
| 2 | Modos de vídeo 480p/480i | **Hecho**, probado con los dos cables |
| 3 | VRAM y paletas → texturas del PVR | **Hecho**, medido |
| 4 | Dibujo 2D en el PVR | **Hecho**, el front end se ve en Dreamcast |
| 5 | Ayudantes del HUD | **No hacen falta para el front end** (ver abajo) |

Cerrado en emulador; verlo en una tele en 480i (el resto del criterio) pasa a M7, la prueba en
consola real.

### Paso 1: la grabación

`dc/src/host/overlay.c` convierte el dibujo del front end en una lista de comandos
(cuadros en coordenadas de pantalla y recortes), por sus **cuatro fuentes**: el emisor de
formas `0x8004E74C`, `PSXDrawSquare` y `PSXDrawTransSquare`, las rutinas que construyen
sus propias primitivas (se corren anidadas y se graba lo que escriben), y `Font_TextXY`.

**Validado con oráculo:** los dos lados vuelcan su lista por frame en el mismo formato y
`dc/test/diff/cmpovl.py` las compara comando a comando, dentro de `make diff`.

```
comandos (cuadros y recortes): C 34525, referencia 34525
comandos de la referencia que el port no produce todavia: 0
```

| Medido en 120 frames de front end | |
|---|---|
| Comandos grabados | **34 525** |
| Máximo en un frame | **300** |
| Combinaciones `(página, paleta)` en total | 63 |
| **Vivas a la vez en un frame** | **54** |

**Las 54 condicionan el paso 3.** Si la caché convirtiera una página de textura entera
por combinación (256×256 a 16 bits = 128 KB) serían **6,9 MB de los 8 MB de VRAM** del
PVR. Hay que convertir solo el rectángulo que los UV usan, no la página.

En **carrera** la lista no coincide (68 361 contra 210 157) y es lo esperado: la
referencia graba además la tabla de ordenación de las vistas de carrera y la geometría
del mundo, que son M2b y M2c.

### Paso 2: modos de vídeo

`dc/src/platform/dc/video.c`. VGA → **480p**; RGB/SCART o compuesto → **480i** NTSC o PAL
según la región de la consola. Se fuerza con `/cd/VIDEO.TXT` (`vga`, `ntsc`, `pal`), que
`mkdisc.sh --video` escribe. Forzar VGA sin cable VGA no se obedece: la consola solo saca
31 kHz por ese cable.

### Paso 3: la caché de texturas

`dc/src/render/texcache.c`, portable. Convierte de la VRAM de PS1 (páginas de 4, 8 o 16
bits con paleta) a ARGB1555 del PVR. Tres decisiones, cada una salida de una medida:

1. **Se convierte el rectángulo que los UV usan, no la página.** Guardar la página entera
   por combinación serían 6,75 MB; el rectángulo son 0,66 MB.
2. **La caché persiste entre frames.** La VRAM apenas cambia (6,6 escrituras por frame),
   así que reconvertir cada frame sería tirar el SH-4 a la basura.
3. **Se invalida por solape**, no en bloque: una escritura a la VRAM solo tumba las
   entradas cuyo rectángulo —o cuya paleta— toca.

Los texeles se reparten con un **asignador por tope**: una entrada convertida no se
mueve, porque compactar cambiaría todos los desplazamientos y obligaría a reconvertirlo
todo. Cuando el buffer se llena se vacía entero y se empieza de cero.

**Y hace falta expulsar.** La tabla tiene 128 entradas y al principio no se expulsaba
nada. Sobre 120 frames no se nota (63 combinaciones), pero en 3 000 frames la tabla se
llena de combinaciones viejas y **13 654 peticiones se quedaban sin textura**: en pantalla
eran cuadros negros en la foto del coche. Expulsando la entrada que lleva más tiempo sin
pedirse —nunca una del frame en curso— bajan a **0**.

| Medido, 3 000 frames de front end | |
|---|---|
| Conversiones | 380 (0,13 por frame) |
| Pico de texeles vivos | 523 968 de 524 288 (1,0 MB) |
| Invalidaciones por escritura a la VRAM | 101 |
| Expulsiones por tabla llena | 39 |
| Vaciados del buffer | 4 |
| **Peticiones sin textura** | **0** |

### Paso 4: el dibujo en el PVR

`dc/src/platform/dc/render.c`. Cada cuadro va como una tira de dos triángulos a la lista
**translúcida** con el **autosort desactivado**, para que el PVR respete el orden de
envío: esto es un algoritmo del pintor, no una escena con profundidad. Sin test ni
escritura de Z, sin descarte de caras.

- **Coordenadas:** el front end dibuja en 512×240 y la salida es 640×480, así que el
  escalado **no es uniforme**: ×1,25 en horizontal y ×2 en vertical. (En carrera la
  pantalla de PS1 es 320×240, o sea ×2 en ambos ejes.)
- **Color:** en PS1 el 0x80 del color de vértice es el 1,0 y modula la textura; aquí se
  duplica por canal y se satura.
- **Recorte:** en software sobre los cuadros alineados a los ejes, ajustando también los
  UV. El recorte por hardware del PVR va por baldosas de 32×32 y no da el grano fino que
  el texto desplazado del front end necesita.
- **Subidas a la VRAM del PVR:** solo cuando la caché ha convertido algo nuevo.

**El encuadre está verificado, y no a ojo.** Dibujando cuatro marcadores de 32×32 en las
esquinas de 640×480 se midió que el marcador de (0,0) cae exactamente en el primer píxel
de la zona dibujada y mide exactamente 32×32 en coordenadas de pantalla. Lo que parecía
un desplazamiento era la captura: PowerShell pide el rectángulo del cliente en píxeles
lógicos y `PrintWindow` dibuja en físicos, así que con el escalado de Windows al 125 %
la imagen salía recortada por la derecha y por abajo. Capturando a ×1,25 aparece la
pantalla entera.

Por serie, el modo confirmado es `640x480, bitmap en 172,40`.

**Rendimiento:** **~60 fps** en el front end dibujado (Flycast con dynarec), contra 69–78
sin dibujar.

### Paso 5: los ayudantes del HUD

Medido antes de escribir nada: en 3 000 frames de front end **no se llama ni una vez** a
los cuatro ayudantes del HUD, y no queda ninguna primitiva sin grabar ni ningún cuadro
sin dibujar. El front end ya se lee entero —menú, opciones, foto y texto de ayuda—, que
es el criterio del paso.

`Rect`, `ThinBox`, `PauseBox`, `Shape` y `Bitmap` son del **HUD de carrera**, y en
carrera la lista 2D sale hoy vacía (0 cuadros en el frame 900) porque la vista de
carrera entera todavía no se dibuja. Portarlos ahora sería escribir a ciegas contra un
escenario que no existe: van con M2c, cuando haya carrera que mirar. El port los deja
correr y los **cuenta**, así que el primer guion que los toque lo dirá en el informe.

---

## M2b — La pista y el cielo en el PVR (cerrado en emulador 2026-10-23)

El hito que convierte el negro de la carrera en algo mirable.

| # | Paso | Estado |
|---|---|---|
| 1 | Captura de la vista de carrera | **Hecho**, validado contra la referencia |
| 2 | Horneado de la pista en el PC | **Hecho**, las 40 pistas |
| 3 | Transformación, recorte y dibujo en el PVR | **Hecho**, la pista se ve |
| 4 | El cielo | **Hecho**, 104 triángulos |

### El dimensionado, que cambió el planteamiento

Medido antes de escribir nada, sobre 5 000 frames de carrera y el GRP de una pista:

| | media | pico |
|---|---|---|
| Bloques de pista por vista | **8,4** | **18** |
| Cuadros a pintar | ~1 180 | ~2 520 |
| **Triángulos a pintar** | **~2 360** | **~5 040** |

Un Dreamcast mueve del orden de 50 000 triángulos por frame a 60 Hz: el pico de la pista
es el **10 %**. **El PVR no es el cuello**; lo será el SH-4, que tiene que transformar
5 000–10 000 vértices por frame *además* de correr la lógica recompilada.

Y la pista entera (18 046 cuadros, ~200 KB de caras) **cabe en RAM**: se carga al entrar y
se queda. **No hace falta streaming**, que era el problema más caro que podía tener el
hito.

### Paso 1: la captura de la vista

El juego **no construye la malla del mundo en su RAM**: solo deja preparada la cámara y
dice qué bloques se ven y con qué nivel de detalle. Eso es todo lo que un renderer recibe
de él, y ahora el port se lo queda (`RaceView` en `host.h`, llenada en `race_view_logic`).

**Validado con oráculo**, igual que la grabación 2D: los dos lados vuelcan la vista por
frame en el mismo formato (`NFSV`) y `dc/test/diff/cmpview.py` las compara campo a campo
dentro de `make diff`.

```
vista de carrera:
  C: 900 frames, referencia: 900 frames (900 en comun)
OK: 669 vistas identicas en 900 frames
```

### Paso 2: el horneado de la pista

`dc/tools/bake/bake.rs` (`make -C dc bake`) convierte GRP + PSH en un `.TRK` que el SH-4
dibuja sin parsear nada. Vive bajo `dc/` porque es código del port y se compila dentro de
la referencia Rust porque reutiliza `src/formats`; de upstream solo toca dos líneas.

Se resuelve en el PC todo lo que se puede: las **esquinas de UV** con sus indicadores ya
aplicados, las **texturas** en un atlas ARGB1555, la **doble cara** y la
**semitransparencia** (que en PS1 sale de que la paleta de la forma tenga un color con
STP, no de lo que diga el material).

**El color de vértice no se hornea, a propósito.** Sale de `Chunk_lightTable`, que vive en
la RAM del juego y cambia con la pista, la variante y el clima: cada vértice guarda su
*índice de sombra* y el color se busca al dibujar. Es lo que mantiene noche y lluvia sin
rehornear.

El horneado **se verifica a sí mismo**: relee lo escrito y comprueba que cada índice de
vértice cae en su propio charco y que todo material existe.

| Medido, las 40 pistas | |
|---|---|
| Horneadas sin error | **40 / 40** |
| Cuadros por pista | ~18 000 a 24 000 |
| Geometría | 380–435 KB |
| Atlas | **24 pistas en 1 MB**, 16 en 2 MB |
| Archivo entero | 1 375 – 2 497 KB |

En el peor caso van ~4,2 MB de los 8 del PVR entre atlas, caché de M2a y los dos buffers
de pantalla. Entra, pero M2c todavía tiene que meter coches: si aprieta, el atlas se puede
partir en varios, porque la pista es opaca con Z-buffer y el orden no importa.

### Paso 3: la pista en pantalla

`dc/src/render/track.c` (portable) lleva el `.TRK` a vértices de pantalla y
`platform/dc/render.c` los manda a la lista **opaca con Z-buffer** —al revés que el
overlay 2D, que es algoritmo del pintor en la translúcida—. El arnés de PC los pinta por
software (`--track`), que es lo único parecido a un oráculo que tiene este paso.

Qué pista cargar lo dice el propio juego: el host se queda con el nombre del **`.GRP` que
abre** y la plataforma carga `/cd/BAKED/<NOMBRE>.TRK`.

| Medido en Flycast con BIOS real | |
|---|---|
| **Triángulos por frame** | **1 038** |
| **fps en carrera, dibujando** | **53–60** (16,7–18,7 ms) |
| fps en carrera sin dibujar (M1b) | 98–113 |

Dibujar la pista cuesta del orden de medio frame. Sigue por encima del mínimo de 30 fps
sostenidos, y **todavía sin FTRV**: la transformación va en C con `float` normal. Esa
optimización es de M8 y ahora hay con qué medirla.

### Paso 4: el cielo

**No es un asset.** Sus vértices y colores viven en la RAM del juego; del PSH solo salen
las texturas `HZ01..HZ12`, que el horneado mete al atlas a propósito porque ningún
material de la pista las referencia. Son la **cúpula** (4×16 cuadros sobre una malla de
5×17 puntos, con color por vértice y a veces textura) y la **banda de horizonte** (16
segmentos de un anillo de 17 puntos), las dos centradas en la cámara. El host lo relee
cada frame, así que el cielo sigue los cambios de luz sin adivinar cuándo pasan.

**104 triángulos**, menos del 10 % del frame, y el ritmo no se mueve de 60 fps.

Dos fallos que solo se ven con el mundo delante:

- **A la pista le faltaba el ×2.** El `0x80` de PS1 es el 1,0 y el color de vértice iba
  al PVR sin doblar: todo salía a media luz.
- **El color clave en la lista opaca sale negro.** Las copas de los árboles son cuadros
  con texeles transparentes y la lista opaca del PVR no hace prueba de alfa: cada
  silueta llevaba un fleco negro. **No era el filtro bilineal** —con muestreo puntual
  seguía igual—. El mundo va ahora a la lista **punch-through**.

---

## M2c — Coches, objetos y efectos (cerrado en emulador 2026-10-26)

El hito que convierte un paseo por un escenario en una carrera. **Criterio cumplido**: una
carrera completa de un jugador (`carrera_completa.txt`, 40 000 frames idénticos a la
referencia en RAM, 2D y vista). Con él queda **cerrado M2** (a, b y c) en emulador; lo que
falta probar en una Dreamcast es el M7.

| # | Paso | Estado |
|---|---|---|
| 1 | Grabación de los objetos del escenario | **Hecho**, validado contra la referencia |
| 2 | Horneado y dibujo de los objetos | **Hecho**, el escenario se puebla |
| 3 | Grabación de los coches | **Hecho**, validado palabra a palabra |
| 4 | Horneado y dibujo de los coches | **Hecho**; el coste no es dibujarlos |
| 5 | El HUD | **Hecho**, 4 769 frames idénticos |
| 6 | Efectos: sombras, destellos, humo, marcas, clima, líneas | **Hecho en emulador** (2026-10-25); la lluvia, comparada con el original en DuckStation |
| 7 | El showroom del front end | **Hecho en emulador**: grabado con oráculo, dibujado con su mapa de reflejo, con las 64 pinturas y los 39 coches horneados; y la policía con la librea de su región (las sombras de los pasos de rueda no van: el juego no las pide en el showroom) |
| 8 | La pausa en carrera | **Hecho en emulador** (2026-10-26): con oráculo de RAM, 2D y vista |
| 9 | Una carrera completa, y la noche | **Hecho en emulador** (2026-10-26): 40 000 frames idénticos, de los menús a la meta y de vuelta |

El HUD va **antes** que los efectos a propósito: sin velocímetro, vueltas ni posición una
carrera no se puede jugar, y los efectos son adorno al lado de eso.

### El patrón que se repite: el juego no deja nada escrito, lo dibuja

Ni los objetos ni los coches están listos en la RAM del juego: los va **dibujando**, y por
el camino decide cuáles entran. Así que el port corre su rutina **anidada** con un **hook
interno** que la sustituye y **anota** la decisión en vez de construir primitivas. Es el
mismo mecanismo que ya usaba `showroom.c` desde M1a.

### Pasos 1 y 3: las grabaciones, con oráculo

```
OK: 4769 vistas, 220002 objetos y 9494 coches identicos en 5000 frames
```

Los coches se comparan **en crudo**, palabra a palabra (99 por coche), que es menos código
y más estricto que comparar campos interpretados.

| Medido, 5 000 frames de carrera | |
|---|---|
| **Objetos por vista** | media **46,1**, máximo 92 |
| **Coches por vista** | media **1,99**, máximo 4 |

La estimación estática de objetos hecha antes de escribir código (707 colocaciones entre
136 bloques, por 8,4 bloques por vista → ~47) acertó en el primer decimal.

### Paso 2: los objetos en pantalla

Los 353 modelos del GRP van al `.TRK` (versión 3), cada uno con su propio charco de
vértices. Lo delicado es la colocación: los **fijos** van por la **transpuesta** de su
matriz —el original carga el GTE con `(M·vista)ᵀ`—, y los **billboard** se giran hacia la
cámara con la escala sacada de las dos primeras columnas.

**51 objetos por 110 triángulos**, contra ~1 150 de la pista: menos del 10 %. La
estimación previa (~240) se quedó larga — los modelos que de verdad se colocan son más
pequeños que la media de los 353.

### Una deuda de M1a que este hito tuvo que resolver

El **ancho del cono de visibilidad de los coches**. La referencia lo ensancha
(`CAR_CONE_WIDTH = 2`) porque su renderer es más ancho que el 4:3 de PS1; es **mejora
suya, no del original**. M1a lo dejó anotado como «si se porta, detrás de una opción
apagada por omisión», y así queda: `car_cone_width` vale **1** en el port y el test
diferencial lo sube a 2, porque si no los dos lados no son comparables. Mismo caso que
`--unlock`.

### Paso 4: los coches en pantalla

`bake-car` reutiliza `assets::load_car`. Las partes van indexadas igual que los 57 bytes
de `R3DCar_ObjectVisible`, así que la captura del paso 3 sirve de índice directo. Un
fallo silencioso cerrado: dos páginas de 256×256 más el texel blanco **no caben en 512
por una fila** y el cargador tiraba una página sin fallar; se recorta a 512×256 (0,25 MB
de VRAM por coche).

**Dibujar los coches no cuesta casi nada; lo que cuesta es grabarlos.** Con el dibujo
apagado el ritmo es el mismo, así que la caída de ~60 a 30–40 fps la produce el paso 3,
que corre código real del juego cada frame. Es trabajo de M8.

### Paso 9: una carrera completa, y la noche (hecho en emulador, 2026-10-26)

**El criterio de M2c se cumple en emulador.** Ningún guion fijo llega a la meta (sin girar,
el coche se estrella), así que la entrada de carrera la pone el **piloto automático de
pruebas de la referencia** (`src/game/ai.rs`), metido en su modo diferencial detrás de
`NFSHS_DIFF_AUTOPILOT`: conduce siguiendo la línea central de la pista y anota cada frame
sus botones como guion del port. Los dos lados reciben así exactamente la misma entrada
(`carrera_completa.txt`). Single race en solitario por Alemania, 2 vueltas:

| | |
|---|---|
| salida | frame ~1 300 |
| meta y «RACE STATISTICS» | ~34 050: vuelta 1 4:16.00, vuelta 2 4:42.78, total 8:58.78 |
| repetición | ~34 100 a ~37 950 (el Cross del piloto la pide y la corta) |
| menús | desde ~37 950 |

**Resultado: 40 000 frames idénticos** en RAM, 3 345 733 cuadros 2D, 36 789 vistas,
6 034 coches, marcas y destellos. Sacó **dos fallos de grabación** que ningún guion
anterior tocaba:
- **Las marcas de neumático** se leían al preparar la vista y quedaban las del frame
  anterior cuando el juego las borra (al recolocar el coche y al acabar). Ahora se leen en
  la frontera de frame, como la referencia.
- **El último frame de la carrera**, ya en los menús, llevaba las tablas del HUD grabadas
  antes del cambio de modo. La referencia las guarda aparte y solo las usa al componer en
  carrera; aquí se quitan.

**La noche estaba sin hacer.** Faltaba el **haz de los faros** (§3 de CLAUDE.md: por vértice
en el SH-4): ahora el host lee el de Night (mapa 64 × 64, tabla 256 × 16 y los ejes del coche)
y `track.c` cambia el índice de luz de los vértices de la pista que caen dentro, como el
shader de la referencia. Además, **las pistas N y S estaban horneadas con el formato
antiguo** (del 23 de octubre) y no cargaban: la noche salía en negro. Rehorneadas (`make
bake`, 9 s). Y **los coches salían sin reflejo con cualquier variante** (noche, lluvia): se
buscaba `ZTR06WR.RFL` y los mapas son de la pista, `ZTR06R.RFL`.

**La «nieve» no es nieve en Alemania.** La variante S de una pista es clima con noche
(`GameSetup` +0x48 y +0x54). `docs/FORMATS.md` (del upstream) la llama «snow», pero el
original, por el mismo camino de menús en DuckStation, pinta **lluvia de noche**. El port
graba lo que pinta el juego (272 991 cuadros de clima idénticos en `noche_lluvia.txt`) y
en Flycast se ve como en DuckStation: la escena azul, la farola, la carretera iluminada,
la lluvia y las gotas en la lente.

### Paso 8: la pausa en carrera (hecho en emulador, 2026-10-26)

En pausa el original no vuelve a pintar el mundo: copia la última imagen y dibuja encima el
menú con `Font_TextXY` y cuatro ayudantes del HUD (`Hud_BuildString`, `Hud_BlackThinBox`,
`Hud_RenderPauseBox` y `Hud_FBuildF4`). La referencia los **atiende** (el original no corre)
y graba comandos. El port los dejaba correr, así que **la RAM divergía al pausar**; era la
deuda de «los cuatro ayudantes» que venía de M1a.

- **Los ayudantes, como la referencia:** `Hud_BuildString` calcula el ancho con
  `HudPmx_gShapes` y graba un comando por carácter; los demás graban el suyo y devuelven 0;
  los que llama el HUD que la referencia dibuja con su propio port no se graban.
- **Las cámaras quietas** (`hold_cameras`): en cada frame de pausa `Camera_gInfo` vuelve a
  como lo encontró la pausa. Escribe en la RAM, así que también hacía falta para el oráculo.
- **El flare del sol** se conserva en pausa, y sin vistas no se vuelcan objetos, como la
  referencia.
- **Cada comando es un marcador** en la lista 2D (para el volcado) **seguido de sus cuadros**,
  con las texturas que el juego tiene en la VRAM: la fuente y las formas del HUD (cada entrada
  de `HudPmx_gShapes` lleva su página, como un `POLY_GT4`).
- **Lo que se ve:** las vistas y el HUD del último frame jugado y el último menú, porque el
  juego solo lo redibuja cuando cambia. El frame que no pinta nada al reanudar repite el
  último, como `held` en la referencia.

**Oráculo:** volcado de la lista 2D **versión 3**, con los comandos de la pausa y sus
parámetros en los dos lados. Con `pausa.txt` (pausa del frame 700 al 1000): **1 200 frames
de RAM idénticos**, 211 227 cuadros y **28 comandos de pausa idénticos**, y la vista idéntica.
`race_long` sigue idéntico en todo. **Visto en Flycast:** «Game Paused» con Continue,
Restart, Options y Quit Race sobre la carretera y el HUD.

### Paso 7: el showroom del front end (hecho en emulador)

El coche de la selección, del concesionario y del garaje no se pintaba: del showroom solo
estaba portada la **mecánica** (lo que el original escribe en memoria y en el GTE). Ahora se
graba como la referencia —el coche y su reflejo con pose, partes, cámara y proyección del
GTE; los halos; la sombra; y el suelo, con el anillo, sus luces que giran y el foco— y se
dibuja con el PVR.

**Con oráculo:** el volcado de la lista 2D (versión 2) incluye el showroom: **92 coches y 46
suelos idénticos** en `race_long`, **1 000 y 646** en `lluvia`. Eran los «138 comandos que el
port no produce todavía».

**La deuda de «dos cuadros de menos» de los menús quedó resuelta**, y no era del showroom:
eran los dos cuadros del vídeo de la pista que gira en la pantalla de pista
(`tScreenTrackSelect::DrawVideoWall`), que la referencia graba y el port no. Portado: la lista
2D sale **idéntica entera** (370 703 cuadros en `race_long`, 935 968 en `lluvia`).

**Dibujo.** Los coches del showroom son 3D con profundidad y van en la lista punch-through,
antes que la translúcida; el orden de la referencia (2D de debajo, reflejo, suelo, coche, 2D
de encima) se consigue con la **profundidad**: el reflejo comprimido a una franja lejana, el
2D de debajo a la profundidad del fondo con `GEQUAL` (solo donde no hay coche), el suelo por
encima del reflejo y por debajo del coche, y el resto sin prueba. Proyecta con la distancia
y el centro del GTE del juego. Visto en Flycast: coche, reflejo, plataforma y halos, como en
el original en DuckStation.

**Dos fallos de brillo que venían de antes, encontrados al compararlo con DuckStation:**
- **El 2D sin textura salía al doble de brillo.** El «0x80 es el 1,0» vale solo con textura;
  sin ella la PS1 pinta el color del vértice tal cual. Afectaba a los cuadros planos del
  front end, a los destellos, al foco y a la lluvia. Las «manchas blancas» de la cuenta atrás
  eran esto, no su aspecto original.
- **Los coches salían a la mitad de brillo**, en carrera desde el paso 4: el color del
  vértice horneado es 0x80 en lo neutro y el PVR modula por color/255, así que hay que
  doblarlo. Además, la luz lleva el mínimo de 24 de la referencia.

**La pintura: paleta en el disco, textura normal en la VRAM (decisión de Jorge).** Medido
antes de decidir: las texturas de coche de PS1 son de 4 bits con paletas de 16 colores, así que
una versión paletizada se vería **igual**; y la memoria de paletas del PVR (1 024 entradas)
solo da para un coche con texturas de 4 bits o cuatro con 8 bits, y en carrera hay hasta ocho.
Así que el `.CAR` (versión 3) guarda, por téxel, el índice de su **serie de colores** en las 64
pinturas (el color 0–15 y dos banderas de paleta de cada coche), y la tabla de esas series; al
cargar el coche, el SH-4 expande la textura con la pintura que toca (preparar una textura, no
dibujar) y la sube al PVR. Por construcción es exactamente lo que da el cargador de la
referencia para esa pintura. 431–575 series por coche de carrera, ~70 por coche de tráfico:
índices de 16 bits.

| | Disco por coche | 39 coches |
|---|---|---|
| un `.CAR` por pintura (64) | 64 × ~280 KB | ~700 MB |
| índices + series (versión 3) | ~410 KB (carrera), ~284 KB (tráfico) | **14 MB** |

Cada modelo guarda hasta 4 texturas pintadas en VRAM (dos coches iguales con distinta pintura
no se pisan) y la caché de modelos expulsa el menos usado. **Visto en Flycast:** el menú «COLOR»
cambia la pintura del coche (rojo, amarillo, blanco, plata, gris…) y «CAR» recorre los coches
(SLK, Z3…). `make bake-coches` hornea los 18 de carrera (versión H) y los 21 de tráfico en 20 s.
Antes todos los coches salían con la pintura 0; ahora cada uno con la suya (`CarDraw.paint`).

**El mapa de reflejo sobre la carrocería** (2026-10-26). En el showroom el mapa A del juego no
está en la página de la pista sino en la **VRAM de PS1** (página `0x3E`, 4 bits, 128 × 65
téxeles desde u 0, v 128 en todo `race_long`): lo sirve la caché de texturas del 2D y se suma
sobre la carrocería, con las coordenadas recortadas al mapa como la referencia. **Una
diferencia a propósito:** la referencia acerca el reflejo 1 cm a la cámara y lo prueba con
«más cerca»; donde dos paneles se solapan (el capó bajo la aleta) el reflejo del de debajo
también pasa y se suma dos veces, y en Flycast salía una **línea de puntos claros** en las
aristas. En la PS1 no pasa porque el panel de encima se pinta después y lo tapa. Aquí el
reflejo usa los mismos vértices que la carrocería y la prueba es de **profundidad igual**:
solo suma sobre el panel que se ve. Visto en Flycast: brillos en el capó y el parabrisas que
cambian al girar el coche, y sin la línea de puntos.

**Las sombras de los pasos de rueda no van en el showroom (medido, 2026-10-26).** Estaban en
la lista de pendientes por un comentario de la referencia, que las pinta en negro si el juego
las pide. Pero el juego **no las pide**: en todos los coches del showroom grabados la parte 5
(la única que resta, en los 16 modelos que la tienen) tiene visibilidad 0. Son 2 432 registros
de 10 modelos distintos (ADB7, BMWM, BMWZ, CMRO, CORV, F550, JXKR, MSLK, P993, TRAM), coche y
reflejo, más los 1 092 del SLK en `race_long` y `lluvia`. Solo se encienden en carrera, donde
ya se restan desde el paso 6. No hay nada que dibujar.

**La policía, con la librea de su región (2026-10-26).** Los códigos de policía (`CBMW`,
`CDBL`, `CCMR`, `CCOV`, `CCPR`) no son archivos: el juego cambia la primera letra por la de la
región de la pista y abre esa librea (`zzzgBMWs.viv` en Alemania). El host anota la letra de
la última que abre (`host_car_file`) y el renderer carga ese `.CAR`. `make bake-coches`
hornea además las **14 libreas** del disco (A/B/F/G para el BMW, A/B/F/G/U para el Diablo,
B/F/G para el Camaro, y el Corvette y el Porsche de la C), unos 5,4 MB. Comprobado en una
**persecución** por los menús (`hot pursuit` → solo → beginner → Alemania): el juego abre
`zzzgCMRs.viv` y `zzzgBMWs.viv`, el port resuelve `CBMW` a `GBMW` y el rasterizador de PC
dibuja la patrulla blanca con la franja verde. En Flycast la persecución corre, pero en las
capturas no salió ninguna patrulla de cerca.

**Paso 7 completo en emulador** salvo mirar de cerca una patrulla en Flycast.

### Paso 6: los efectos (hecho en emulador)

**Lo sustractivo, con la mezcla del PVR.** La PS1 tiene un modo que resta (`B − F`); el PVR
suma siempre sus dos factores. La técnica conocida sale de una identidad,
`B − F = 1 − ((1 − B) + F)`: **invertir, sumar F e invertir otra vez**. Las tres cosas son
mezclas del PVR en la lista translúcida:

| Paso | Mezcla | Resultado |
|---|---|---|
| invertir | origen blanco, factor de origen `INVERSE-DEST-COLOUR`, de destino `ZERO` | `1 − B` |
| sumar | `ONE` / `ONE` | `(1 − B) + F`, saturado a 1 |
| invertir | igual que la primera | `B − F`, saturado a 0 |

Es el modo 2 de PS1 tal cual, no una aproximación. KOS llama a los factores
`PVR_BLEND_DESTCOLOR` / `INVDESTCOLOR`; en el factor de **origen** actúan sobre lo que ya
hay en pantalla y en el de **destino** sobre el color del polígono. Las inversiones solo se
hacen sobre la **caja que ocupan los efectos** (se calcula antes con un recorrido sin
dibujar), no sobre la pantalla entera: invertir dos veces deja cada píxel como estaba.

**Verificado dibujando, no supuesto:** un gris de 26/255 sobre media pantalla en Flycast
dejó esa mitad **26 / 24 / 23** niveles por debajo de la otra en la carretera y 28 / 23 / 23
más abajo, con un cociente que varía de 0,55 a 0,78 según la zona: resta **absoluta**, no
un porcentaje.

Va **antes del overlay 2D**, con la profundidad probada contra lo opaco (`GEQUAL`, sin
escribir) y los cuadros acercados un 0,3 % a la cámara para que no se hundan en lo que
cubren.

**Hechas**
- **Sombra de cada coche en el suelo** (`SHAD` de `ZSFX.PSH`, sobre el cuadro de
  `Newton_CalcRealShadowCoordinates`, elevada sobre la carretera como hace la referencia) y
  **sombra de los pasos de rueda** (las partes `subtract` del modelo).
- **Líneas pintadas** de la carretera: cuadros del GRP texturados con `LIN0..LIN9`, que se
  hornean enteros —incluido el vector derecho de la sección de carretera más cercana— y se
  dibujan solo cuando el juego pone el indicador 4 en el trozo. ~28 cuadros por bloque;
  3 786 en la pista de prueba.
- **Marcas de neumático**: el host las lee de la RAM del juego cada frame (hasta 64
  segmentos de 23 entradas), y se dibujan con `SKD0`/`SKD1` restando, solo las de las
  secciones a la vista y a menos de 50 m.

Las formas de `ZSFX.PSH` (`SHAD`, `SKD0`, `SKD1`, `LIN0..LIN9`) van en el atlas del `.TRK`
(versión 5).

**Con oráculo:** la lectura de las marcas coincide con la referencia —1 069 983 marcas
idénticas en 5 000 frames de `race_long`, y 457 416 en 2 800 de `derrape`—. El dibujo no
tiene oráculo: se mide. Marcas, con y sin el efecto, en el arnés de PC: **13,3 niveles de
oscurecimiento de media**, que es lo que resta la PS1 (`26/255 × 0x40/0x80 = 13`).

**Destellos, halos, humo y el flare del sol: 2D del propio juego.** El original no los
construye como geometría sino como cuadros de pantalla alrededor de un punto del mundo:
`BWorld_BuildGlareEffects` (farolas), `DrawC_PrimHalo` (faros y frenos), `Souffle_InsertFacet`
(humo y polvo) y `Flare_Sun`. El host los corre anidados, como la referencia, y graba lo que
escriben en el buffer de primitivas junto con el punto del que sale cada uno. **Como la
cámara del port es la del juego, caen exactamente donde los puso el original**: la
referencia tiene que moverlos porque su cámara está suavizada, el port no. Lo único que se
les añade es una profundidad, para que el PVR los pruebe contra lo opaco (`GEQUAL`, sin
escribir), con la regla de la referencia:

| Qué | Profundidad |
|---|---|
| farola | un poco por delante de la luz (el doble de su tamaño más 1 m, como mucho la mitad de la distancia) |
| halo de coche | por delante del coche entero (el radio de su caja más 0,2 m) |
| bocanada | 0,5 m por delante, y por debajo del ancla, la de la carretera que tapa |
| flare del sol | la del plano de fondo (0,0001): pasa sobre el cielo y lo tapa el mundo |

Casi todo **suma** (modo 1): 10 062 cuadros de farola, 12 136 de halo y 602 de humo en
`race_long`, contra **5** cuadros de humo que restan, que van con su propia caja de
invertir, sumar e invertir.

**El orden de las llamadas importa.** Grabar paquetes mueve el modo de dibujo vigente, y el
HUD que se graba después depende de él. Por eso el port corre también `Weather_Render` y
graba el clima en su sitio, aunque dibujarlo sea otro paso. Con eso el HUD sigue idéntico.

**Con oráculo**, que se amplió (volcado de vista versión 5): **2 071 destellos idénticos**
en `race_long` (586 halos, 607 bocanadas) y 825 en `derrape`, y **29 038 cuadros del flare
del sol** idénticos. El clima sale con 0 cuadros en las dos: esta pista no tiene, así que
**su grabación no tiene oráculo todavía**.

**Visto, no supuesto:** en Flycast la farola brilla con su estrella sobre el farol, y en el
arnés de PC (que ahora también los dibuja) los halos de freno caen sobre los pilotos del
coche de delante y el polvo que levanta aclara **24 niveles de media** sin que la carretera
le corte la base.

De paso se corrigió el **modo 3** de PS1 (`B + F/4`) en el 2D: se dibujaba como `B + F/2`.

**Los reflejos de los coches.** Son el camino de entorno de `DrawC_Prim`: cada polígono
de las partes que reflejan (capó, techo y demás: 0, 8, 10, 41, 42, 45 y 46), solo los que
miran a la cámara, otra vez con un **mapa de reflejo de la pista** (`ZTR<nn>R.PSH`, horneado
a `.RFL` con la misma disposición que en VRAM). Las coordenadas del mapa salen de la normal
de cada vértice; las del mapa A, girada por la guiñada de la cámara alrededor del coche.
En las carreras medidas **el mapa A siempre suma y el B siempre resta**, así que el B va en
la caja de la resta exacta.

**Las normales son las de la RAM, no las del GEO.** Medido: de 1 176 esquinas, **0**
coinciden y la diferencia llega a 98 (otra escala, unos 21 de longitud contra 64). El host
las lee cada frame de los objetos de parte que el juego tiene cargados, como la referencia,
y el `.CAR` (versión 2) trae para cada esquina su vértice del GEO para poder usarlas, más la
normal del GEO de respaldo. Solo los coches de carrera tienen modelo en una ranura; el
tráfico se queda sin reflejo, igual que en la referencia. Los datos de reflejo de cada coche
(mapas, desplazamiento, color, guiñada) ya estaban en la captura de 99 palabras que compara
el oráculo.

**El clima, por el camino del jugador.** En «test drive» el front end no ofrece clima. En
«single race» sí (cuadrado en la pantalla de pista, «weather» en ON), pero exige tener un
coche, así que el guion `lluvia.txt` compra antes el Mercedes SLK 230 con los 20 000 de una
partida nueva. Con eso el juego carga la **variante mojada** de la pista (`zTr06W.grp`:
cielo de tormenta, asfalto mojado) y construye su lluvia: trazos que suman y gotas en la
lente con textura, a medias. Se dibuja encima del 3D y de los destellos y debajo del HUD, en
orden del pintor.

**El oráculo encontró un fallo de verdad.** El port convertía las líneas en cuadros con una
regla de enteros que daba lo mismo que la referencia **solo en pantalla**; con una punta en
x = 8 205, en `f32` el `dy²` se pierde, la normal sale justo −0,5 y redondea a −1. Ahora se
reproduce cada operación `f32` (en `double`, redondeando a `float` tras cada paso, para no
depender de que el compilador fusione operaciones). En la carrera con lluvia: **694 736
cuadros de clima idénticos** en 6 000 frames, con la RAM idéntica en todos.

**Comparado con el juego original en DuckStation** (renderer por software, el mismo camino
de menús, con el disco de Jorge y la BIOS SCPH-1001): la misma pista mojada, el cielo de
tormenta, trazos cortos y gotas como anillos pequeños. En el port, en el arnés y en Flycast,
se ve lo mismo.

**Una lección: forzar el clima engañaba.** Antes de tener ese camino probé a forzarlo
escribiendo el clima en `GameSetup_tData`. El juego activaba la lluvia pero **cargaba la
pista seca**, sin las texturas de la lluvia, y salían dos rarezas que parecían preguntas
sobre la GPU de PS1: trazos fuera de los 11 bits que cruzaban la pantalla y gotas como
cuadros oscuros. Con el camino real hay **0** trazos fuera de rango (de 665 175) y las
gotas leen su textura (página 0x21). El forzado se retiró. Que el oráculo coincidiera en
los dos casos no decía nada de esto: los dos lados forzaban igual.

### Paso 5: el HUD

En carrera **no se sustituyen los ayudantes**: corre el código original y el host graba
sus tablas de ordenación en cada `DrawOTag`, salvo la del mundo. Tenía oráculo desde
M2a —la misma lista 2D— y ahora da **4 769 frames de carrera idénticos byte a byte**.

Por el camino apareció que **la referencia no era determinista**: buscaba el entorno de
dibujo por desplazamiento en un `HashMap` de semilla aleatoria. Se arregló en los dos
lados buscando por la dirección del paquete.

## M3 — FMV (cerrado en emulador 2026-10-26)

Detalle en `docs/hitos/M3.md`.

- **Dos caminos:** `Movie_Play` (5 películas `.XA`, 256×240 a 15 fps con audio XA estéreo a
  37 800 Hz, 3 min 11 s) y los `VIDEO_*` del front end (31 clips `.DCT`, 160×128 y 80×80, sin
  audio, que el juego dibuja como texturas desde la VRAM).
- **Decisiones de Jorge:** el bitstream original se decodifica en el SH-4 (sin recodificar)
  y el color lo convierte el **conversor YUV del PVR**; el audio de las películas suena ya en
  M3; durante una película el rendimiento no es criterio, solo conservar sus fps; las
  películas se leen **de los datos del juego**, sin hornear (abre la puerta al disco
  original de M9; en M8 se puede volver a un formato horneado).
- **Decodificador en C** (`dc/src/host/mdec.c`), traducido de la referencia: con la IDCT
  literal, las 5 películas y los 31 clips **idénticos byte a byte**, y el audio muestra a
  muestra; con la IDCT rápida (la que se usa), diferencia máxima de 1 en el 0,0004 %.
  En SH-4 (Flycast) un frame de la intro cuesta 28 ms de los 66,7.
- **Películas** (`dc/src/platform/dc/movie.c`): lectura por delante, MDEC → YUV420 →
  conversor YUV → textura a pantalla completa, audio por `snd_stream`. Intro: 1 358 frames en
  90,64 s (14,98 fps), 0 tarde, 90,13 s de audio sin huecos. Jorge lo oye bien y sincronizado.
- **Video wall** (`dc/src/host/video.c`), traducido de la referencia; deja de neutralizarse
  en el test diferencial: `carrera_completa` sigue idéntica en 40 000 frames con él.
- **Rendimiento del video wall:** de ~25 a **~40 fps** en el menú y ~43 en la selección de
  pista, quitando el intérprete (el backend C genera dos funciones HLE que el host no
  siempre atiende y una a la que solo se llega por puntero) y arreglando la caché de
  texturas (reconvertir en el sitio y subir al PVR solo lo convertido).

Trampas que costaron y conviene recordar:

- **`gamedata/MOVIES` estaba incompleto:** el extractor copiaba 2 048 bytes por sector y el
  audio XA (forma 2) no cabe. `extract_iso.py` copia ahora los `.XA` con los sectores enteros.
  Y el test diferencial pasaba sin saltar las películas solo porque la referencia no podía
  leer esas copias.
- **El driver ARM del AICA de nuestro KOS no arranca en Flycast** (commit `8690c6dd`, junio
  de 2026): no sonaba nada. Se carga el de justo antes, sacado del historial de KOS.
- **Flycast deja `PVR_YUV_STAT` en 0:** esperarlo tiraba 25-30 ms por frame.
- **Al cortar una película**, la pantalla a negro: si no, su último frame se queda quieto
  durante la carga y parece colgado.

---

## M4 — Audio (cerrado en emulador 2026-10-27)

- **El middleware atrapa las escrituras del driver de EA en el SPU** (`SpuSink`) y las hace
  sonar con el AICA: muestras a PCM16 en un espejo en su RAM, una voz por canal. La música en
  stream (voces de más de 65 534 muestras) va a un canal en bucle sobre un anillo que un hilo
  rellena: **la CPU no mezcla**. Coste: +1,8 % en carrera (antes +8,0 % con la mezcla en CPU).
- **Reloj real en Dreamcast** (decisión de Jorge, 2026-10-27): el juego iba a cámara lenta
  (36,9 s de juego en 69,3 s reales) porque el reloj avanzaba un campo por frame. Ahora sigue
  al tiempo real con un tope de 4 campos por frame, un tick por interrupción y esperas reales.
  PC y test diferencial, con el reloj virtual: idénticos (120 y 900 frames).
- **EA sincroniza su stream con la IRQ por dirección** (medido en la referencia): el
  middleware la levanta cuando el canal del AICA pasa por ella, y corrige el canal con los
  rellenos del propio driver (espera si va adelantado, salta si va retrasado). Bloques
  pisados (deben ser 0): 0 en una carrera entera con su carga, 1 en 80 s de front end.
- **Envolventes y barridos, medidos:** el juego solo usa una puerta (ataque inmediato, 1,5 ms
  de release) y ningún barrido; el AICA ya lo hace.
- **La reverb va en el DSP del AICA:** microprograma generado y probado contra la reverb de PS1
  (`dc/tools/dsp/reverb_aica.py`, dentro de ~0,5 dB). En Flycast hay que activar el DSP
  (`aica.DSPEnabled`, apagado por omisión).
- Detalle en `docs/hitos/M4.md` (secciones 8 a 10).

## M5 — Controles y dos jugadores (cerrado en emulador 2026-10-28)

Detalle en `docs/hitos/M5.md`.

- **El mando** (`dc/src/platform/dc/mando.c`): el juego ve un **DualShock analógico** (tipo
  `0x73`, decisión de Jorge) con la dirección proporcional en el stick y la vibración en el
  Jump Pack (477 cambios de motor en una carrera de 2 000 frames). Botones **según el
  contexto**: en menús, A/B/X/Y son ✕/○/□/△; conduciendo (`0x80113A04` del jugador a 1),
  **gatillos R/L = acelerar/frenar** (decisión de Jorge), B freno de mano, Y cámaras, A/X
  marchas. Es la configuración 1 de serie del juego (tabla en `0x801220EC`), la estándar de
  PS1 según Jorge.
- **Dos jugadores:** los guiones aceptan una columna para el mando 2 (`<frame> <b1> | <b2>`).
  `dos_jugadores.txt` sale **idéntico a la referencia en 2 500 frames** (RAM, 2D, vistas y
  lo que se dibuja en cada una). Lo que faltaba era todo del port:
  - la grabación era por frame y no por **vista** (`ViewCap`, como el `ViewCapture` de la
    referencia) y el renderer solo dibujaba la primera;
  - cada vista va a su franja, y el 3D se **recorta en geometría** contra sus bordes, como
    el plano cercano, porque el PVR recorta a tiles de 32 y la frontera cae en y = 240;
  - los entornos de `SetDrawEnv` se guardan todos (tabla hash), como el `HashMap` de la
    referencia;
  - en la selección de coche a dos, una franja de profundidad por coche.
- **Rendimiento (Flycast):** carrera a dos ~18 fps (56 ms, 24,7 de dibujo) frente a ~21 de un
  jugador con tráfico (47 ms, 14,1 de dibujo). Subirlos va al **M10** (decisión de Jorge).

---

## Decisiones vigentes

Las de `CLAUDE.md` §3 siguen en pie. Lo que ha cambiado o se ha precisado:

- **Optimización:** se compila con **`-Os`**. Cabe de sobra y falla menos en la I-cache
  de 8 KB; la elección definitiva se decide midiendo ms/frame con el juego corriendo.
- **Formato de disco: CDI** por omisión, porque es el único que arranca con la BIOS real
  y el que se graba en CD-R. El GDI queda como alternativa para ODE.
- **Escalado 2D:** la regla es **llevar el área de pantalla de PS1 a 640×480**. El juego
  dibuja a **512×240** (no a 320×240), así que el factor vertical es ×2 y el horizontal
  ×1,25.
- **Toolchain del AICA:** no se instala. KOS usa su driver precompilado.
- **Lo que el PVR no hace, se resuelve con técnicas conocidas del PVR (decisión de
  Jorge, 2026-10-25).** Nada de renderizado por software en el SH-4, ni una parte pequeña,
  porque rompe la arquitectura. Por eso lo sustractivo (modo 2 de PS1, `B − F`) se hace con
  invertir, sumar F e invertir otra vez (`B − F = 1 − ((1 − B) + F)`), tres mezclas del PVR.
  Descartado: renderizar a textura y restar con el SH-4, porque es renderizado por
  software. **Aclaración:** que
  ahora no haya Dreamcast solo impide hacer profiling en hardware real para decidir estos
  efectos; no es una regla general contra medir en consola cuando se tenga.
- **Objetivo de arquitectura: todo lo gráfico pasa por el middleware del PVR (Jorge,
  2026-10-25).** Salvo que por rendimiento no sea posible. La razón y lo que implica en la
  práctica están en `docs/estrategia.md`, sección «Objetivo de arquitectura». Hasta hoy no
  hay ninguna excepción.
- **`Render_RenderPlayerView`:** se porta su parte de **lógica** (cámara, trozos de
  pista, HUD) y no su grabación de geometría, que es M2b y M2c.

---

## Cómo se trabaja aquí

**El método que ha funcionado en los tres hitos: tener un oráculo.** La referencia Rust
produce lo mismo que el port, así que se compara en vez de opinar.

- `make -C dc diff` corre los dos lados con el mismo guion y compara **la RAM** (hashes
  por página, registros, GTE, ticks) **y la lista de dibujo 2D**.
- `make -C dc test` son las 227 pruebas unitarias de `ps1rt`.
- `make -C dc dc` construye el ELF de Dreamcast; `mkdisc.sh` la imagen.
- Guiones de entrada en `dc/test/diff/scripts/`: el disco puede llevar uno como
  `/cd/SCRIPT.TXT` y el juego se conduce solo hasta donde diga. `<frame> <b1> | <b2>` da
  también los botones del mando 2. A `make -C dc diff` el guion se le pasa con **ruta
  absoluta**.
- El test compara también **cada vista de carrera** (volcado de vista, versión 6):
  cámara, trozos, objetos, destellos, sol y clima de cada una, más los coches y las marcas.
- **Compilar para Dreamcast** desde el entorno UCRT64 de MSYS2:
  `MSYSTEM=UCRT64 bash -lc 'cd <repo> && source /opt/toolchains/dc/kos/environ.sh && make -C dc dc'`.
  Sin `MSYSTEM=UCRT64`, `sh-elf-gcc` no encuentra `libwinpthread-1.dll`.

Los dos problemas más caros del proyecto —la carrera congelada y una lista de dibujo
incompleta— los encontró el oráculo, no la lectura del código.

---

## Hechos del sistema que conviene no volver a descubrir

- **`host_frame_top` tiene que avanzar el reloj al siguiente campo de vídeo.** En el
  front end la frontera de frame es el flip, que mueve el tiempo por otro camino; en
  carrera es `systemtask`, que no. Sin ese avance el root counter de 128 Hz no late y la
  física se congela.
- **KOS solo busca el sistema de archivos en el área de baja densidad**
  (`fs_iso9660.c`). Para leer un GD-ROM hace falta el parche de
  `dc/tools/kos-patches/`, que prueba primero la alta.
- **Los archivos del juego se leen a demanda.** `zzzFE.viv` son 28,7 MB y en una máquina
  de 16 no caben: del montaje solo queda residente el directorio (31 KB).
- **`Font_TextXY` no produce texto en el front end: produce un cuadro por carácter.**
- **`SetDrawArea` es un hook, no una HLE.** El front end lo usa 11 veces por frame.
- **El SPU de PS1 choca de nombre con el AICA de KOS** (`spu_init`, `SPU_RAM_SIZE`): el
  stub del port usa `ps1_spu_*`.
- **En SH-4, `uint32_t` es `long unsigned int`**, así que un `%u` sin cast avisa.
- **El Makefile de Dreamcast necesita `-MMD -MP`.** Sin dependencias de cabeceras, los
  módulos del host ven layouts distintos del mismo struct sin dar ningún error. El host
  de PC no lo sufre porque compila todo en una sola invocación.
- **`xorriso` ignora los `-m` si van detrás de las rutas.**
- **Los campos del descriptor de pista de un CDI se miden desde el final del nombre del
  archivo**, que va dentro del propio descriptor.
- **El intérprete de respaldo no es código muerto:** 21 378 entradas en 900 frames.
- **Mandar geometría a una lista del PVR con `PVR_BINSIZE_0` no da error:** deja la
  pantalla negra y hunde el ritmo a 4 fps. M2a solo usaba la translúcida, así que la
  opaca se quedó a cero sin que se notara.
- **El dithering del PVR no es una opción aparte: va pegado al modo de píxel RGB.** En
  `video.c` de KOS, `PM_RGB555` y `PM_RGB565` llevan `PVR_PM_DITHER` en la propia tabla,
  así que se enciende solo al fijar el modo de vídeo. Se apaga con
  `vid_set_dithering(false)` **después** de `vid_set_mode_ex`, o el PVR tapiza la imagen
  con su patrón de puntos.
- **En la lista opaca del PVR, un texel con color clave se pinta NEGRO.** No hay prueba
  de alfa. Lo que lleve color clave va a *punch-through*.
- **Forzar el clima no es elegirlo.** Escribir el clima en `GameSetup_tData` activa la
  lluvia pero deja la pista seca y sin sus texturas. Para probar algo del juego, mejor
  llegar por los menús como un jugador; DuckStation con el disco original sirve de
  referencia visual cuando el oráculo no basta.
- **El juego arranca la carrera en vista de capó y las marcas de neumático no se ven en
  ella.** La cámara queda a ~1 m de las marcas y a su altura, y las ruedas las dejan debajo
  y detrás. Ni `race_long.txt` (recta) ni un derrape las ponen a la vista: `SELECT` no
  cambia de cámara en el guion. Para verlas, el arnés de PC tiene `PS1_CAMBACK=<m>,<alto>`,
  que retrasa y sube la cámara; **no es del juego**.
- **Un oscurecimiento proporcional (`B·(1 − F)`) no sirve para marcas de neumático.** Su F
  es ~0,05 y sobre carretera oscura deja ~2,5 niveles contra los ~13 de la resta absoluta
  de PS1: no se ven. Por eso la resta es exacta (invertir, sumar, invertir).
- **El `0x80` de PS1 es el 1,0 en todas partes**, también en el color de vértice del
  mundo: hay que doblar y saturar, o la escena sale a media luz.
- **El renderer calcula en espacio de PS1 y espeja la X al final.** A un giro no se le
  puede pasar `R(θ)` sino `S·R·S·D`, y **`S·R·S = R(−θ)`**. Pasarle `R(θ)` deja los
  *billboard* girados al revés.
- **La referencia no es infalible.** Su búsqueda de entornos de dibujo dependía del orden
  aleatorio de un `HashMap` y daba listas distintas en cada ejecución. Si el oráculo
  parece cambiar solo, correr la referencia varias veces y comparar.
- **Un comparador que se para a las N diferencias puede esconder todo lo demás.** Una
  deuda vieja en los menús tapaba los 669 frames de carrera.
- **El orden en que el host corre las rutinas del juego importa.** Los coches van después
  del HUD porque es donde los pone el original: ese orden decide qué ve cada rutina.
- **Los cuatro vértices de un cuadro de pista van en orden cíclico**, no en zigzag: se
  parte en (0,1,2) y (0,2,3). Como tira deja huecos triangulares por toda la pista.
- **El juego abre sus archivos en minúsculas** (`zTr06.grp`) y el ISO del disco los
  guarda en mayúsculas.
- **Un registro de archivo con pares `(u32, u16)` no se puede superponer a un struct de
  C:** el compilador rellena cada par a ocho bytes. Hay que decodificarlo, que además
  evita lecturas desalineadas, que en SH-4 son una excepción.
- **Las capturas de la ventana de Flycast salen recortadas por el DPI.** PowerShell pide
  el rectángulo del cliente en píxeles lógicos y `PrintWindow` dibuja en físicos: con
  Windows al 125 % hay que crear el mapa de bits a ×1,25 o se pierde la derecha y el
  fondo. No es un desplazamiento del render.
- **`snd_mem_available()` de nuestro KOS da el bloque más grande de la lista, libre o no.**
  Repartir la RAM del AICA fiándose de él se llevó el hueco del stream de las películas, y
  `snd_stream_alloc` no avisa: escribe en la dirección 0 del AICA (el driver del ARM) y
  Flycast se cierra en la intro.
- **Un guion se salta la película de introducción.** El fallo anterior solo se veía con el
  CDI sin guion: hay que probar también ese.
- **En el formato de coma flotante del DSP del AICA, el cero es `0x6000`**, no `0`. Y en
  Flycast el DSP viene **apagado** por omisión (`aica.DSPEnabled`): sin él no hay reverb.
- **El PVR solo recorta a tiles de 32 píxeles** (user clip). Una frontera que no cae en
  múltiplo de 32 (la pantalla partida, en y = 240) se recorta en geometría, en espacio de
  cámara, como el plano cercano.
- **La lista 2D se dibuja del final al principio** (como la referencia, que la recorre con
  `rev()`). En ese orden el suelo del showroom va **antes** de su coche; leída en el otro
  sentido, parece al revés. Dos supuestos falsos sobre esto se resolvieron leyendo la
  referencia, no mirando capturas.
- **La consola serie de Flycast es una ventana propia** («Flycast Serial Output»), no la
  salida estándar: en Windows el proceso es de interfaz gráfica y `stdout` no va a
  ninguna parte.

---

## Abierto

- **Probar el mando a mano (Jorge):** menús, carrera, pausa, fin de carrera y vibración, y a
  dos jugadores con dos mandos (`dc/build/dc/disc/nfshs.cdi`, sin guion).
- **M10:** la herramienta de espectrogramas, la **reverb en estéreo** (hoy entra mono al DSP)
  y **subir los fps a dos jugadores**.
- **Un bloque de la música pisado** al cambiar de tema en el front end (1 en 80 s; en carrera,
  0). Sin oído que lo note; se mide con la herramienta de M10.
- **Los fps de un jugador con el reloj real** (M4) quedan por debajo de los 30–43 de M2c,
  que se midieron con el reloj virtual: un frame lento simula ahora dos campos. Llegar a 30
  sostenidos es M8.
- **Probar en una consola real (M7).** El CDI es grabable y arranca con la BIOS real en
  emulador, pero nadie lo ha quemado. Hasta entonces los fps son de Flycast.
- **El CDI no cabe en un CD-R de 80 minutos** desde que lleva las películas (M3): 725 MB
  frente a ~703. Es la **primera tarea de M10** (plan de Jorge, 2026-10-28); hasta
  entonces no se dedica esfuerzo al tamaño. En Flycast no importa.
- **El bit STP de PS1 no cabe en ARGB1555.** PS1 marca por texel los que se mezclan en
  las primitivas semitransparentes; ARGB1555 tiene **un solo bit** de alfa. Hoy se
  resuelve por modo de mezcla del cuadro entero, no por texel.
- **Leer el front end en 480i (M7).** Está probado el modo, no la legibilidad en una tele.
- **M8:** factor de escala de las texturas, método y formato (solo si el escalado mejora el
  rendimiento).
- **Hitos renumerados (2026-10-26, decisiones de Jorge):** M3 FMV, M4 audio, M5 controles
  y 2 jugadores, M6 guardado y VMU, **M7 prueba en consola real (480i y 480p)**, M8
  optimización, M9 enhancements con los packs de texturas; y desde el 2026-10-27, **M10
  "Polishing"**, el pulido con medidas. En los `docs/hitos/` anteriores,
  «M3» como optimización es el M8 y como audio el M4, y «M4» como escalado de texturas es el
  M9. El pulido final va en M8. **M9 admite objetivos descartables** (se desechan si la
  plataforma no lo permite o el rendimiento se resiente mucho): salida en 720p, resolución
  interna 3×, widescreen y un **disco de arranque** que lee los datos del disco original de
  PS1, para no empaquetar el programa con el juego. Puntos de partida en `docs/resolucion.md`
  y, para 720p, dos referencias de homebrew que aporta Jorge (en §4 de CLAUDE.md), sin
  investigar.
- **Parche local a KallistiOS:** habría que proponerlo aguas arriba.
- **Driver ARM del AICA (M3):** si el de KOS actual suena en una consola, y avisar aguas
  arriba (M7). Igual con `PVR_YUV_STAT` en hardware.
- **Autor de nfshs-rs:** contacto sobre licencia y posible colaboración.
- **Resolución: 640×480 y FSAA horizontal** (investigado el 2026-10-25, `docs/resolucion.md`).
  El Dreamcast ya usa la temporización de 720×480 (858 relojes × 525 líneas) con 640 píxeles
  activos, y el 3D del port ya se rasteriza a 640×480 desde la geometría (el «2×» de un
  emulador de PS1). Por encima, solo el FSAA horizontal del PVR (1 280×480 promediado):
  probado en Flycast sin commit, coste sin medir. Pendiente de decisión de Jorge: se mide en
  M7 con una consola y se decide en M8.
- **RAM de los coches.** Cada `.CAR` de carrera ocupa ~410 KB en RAM mientras está en la caché
  (6 modelos: ~2,5 MB) porque guarda los índices para poder cambiar de pintura. Sin medir con
  una carrera de 8 coches distintos más tráfico; si aprieta, los índices se pueden releer del CD
  solo al cambiar de pintura.
- **El coste en fps del paso 6 está sin medir.** Destellos, humo, clima y reflejos
  añaden rutinas del juego por frame y pases del PVR; no se ha separado su coste.
- **Coste de las inversiones del PVR sin medir en consola.** Cada frame con efectos añade
  dos rectángulos translúcidos sobre la caja de los efectos. En pantalla completa serían
  ~0,6 M de píxeles por frame; la caja es mucho menor, pero el coste real en un Dreamcast
  está sin medir (no hay consola).
- **Modelos de coche de mayor calidad:** entran en **M9** (plan de Jorge, 2026-10-28; antes
  estaban fuera del plan). El formato `.CAR` ya se carga por código de coche, pero
  habría que resolver los 57 huecos de partes, los faros y abolladuras por triángulo, el
  presupuesto de triángulos en el SH-4 y el de VRAM.
