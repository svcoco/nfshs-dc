# Bitácora de desarrollo — nfshs-dc

Registro del trabajo **efectivo**: lo que está construido, las cifras medidas y las
decisiones vigentes. Solo hechos comprobados; los caminos que se probaron y se
descartaron no se guardan aquí.

Se actualiza al cerrar cada paso. El detalle largo vive en `docs/hitos/`; esto es el
resumen que permite retomar el proyecto sin releerlo todo.

**Última entrada: 2026-10-25.**

---

## Estado de un vistazo

| Hito | Estado |
|---|---|
| **M0** Viabilidad | Cerrado (2026-10-18) |
| **M1a** Backend C + `ps1rt` en C, en x86 | Cerrado (2026-10-18) |
| **M1b** Cross-compilar a SH-4 y arrancar | Cerrado en emulador (2026-10-23); falta probar en consola |
| **M2a** El front end en pantalla | Pasos 1 a 5 hechos; falta leerlo en 480i |
| **M2b** La pista y el cielo en el PVR | Cerrado en emulador; **la carrera se ve a 60 fps** |
| **M2c** Coches, objetos y efectos | Pasos 1 a 5 de 6; **la carrera se dibuja entera con HUD, a 30–43 fps**; faltan los efectos |
| M3, M4, M5 | Sin empezar |

**Lo que hay hoy:** un CDI de 558 MB que arranca en Dreamcast con la BIOS real, lee sus
datos del disco y dibuja con el PVR tanto el **front end** (~60 fps, completo y legible)
como la **carrera**: pista, cielo, objetos del escenario, **coches** y **HUD** —cronómetro,
velocidad, marcha, tacómetro con aguja y mapa—, con las texturas y el color de vértice del
original. En carrera da **30–43 fps** en Flycast; el coste no está en dibujar sino en
**grabar los coches** (código real del juego cada frame), que es trabajo de M3. Faltan los
**efectos** (paso 6 de M2c): sombras, destellos, humo, marcas, clima y líneas pintadas.

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

Cerrado en emulador; el criterio de salida pide verlo en una tele en 480i.

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
optimización es de M3 y ahora hay con qué medirla.

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

## M2c — Coches, objetos y efectos (en curso)

El hito que convierte un paseo por un escenario en una carrera.

| # | Paso | Estado |
|---|---|---|
| 1 | Grabación de los objetos del escenario | **Hecho**, validado contra la referencia |
| 2 | Horneado y dibujo de los objetos | **Hecho**, el escenario se puebla |
| 3 | Grabación de los coches | **Hecho**, validado palabra a palabra |
| 4 | Horneado y dibujo de los coches | **Hecho**; el coste no es dibujarlos |
| 5 | El HUD | **Hecho**, 4 769 frames idénticos |
| 6 | Efectos: sombras, destellos, humo, marcas, clima, líneas | Siguiente |

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
que corre código real del juego cada frame. Es trabajo de M3.

### Paso 5: el HUD

En carrera **no se sustituyen los ayudantes**: corre el código original y el host graba
sus tablas de ordenación en cada `DrawOTag`, salvo la del mundo. Tenía oráculo desde
M2a —la misma lista 2D— y ahora da **4 769 frames de carrera idénticos byte a byte**.

Por el camino apareció que **la referencia no era determinista**: buscaba el entorno de
dibujo por desplazamiento en un `HashMap` de semilla aleatoria. Se arregló en los dos
lados buscando por la dirección del paquete.

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
  `/cd/SCRIPT.TXT` y el juego se conduce solo hasta donde diga.

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
- **La consola serie de Flycast es una ventana propia** («Flycast Serial Output»), no la
  salida estándar: en Windows el proceso es de interfaz gráfica y `stdout` no va a
  ninguna parte.

---

## Abierto

- **Probar en una consola real.** El CDI es grabable y arranca con la BIOS real en
  emulador, pero nadie lo ha quemado. Hasta entonces los fps son de Flycast.
- **El bit STP de PS1 no cabe en ARGB1555.** PS1 marca por texel los que se mezclan en
  las primitivas semitransparentes; ARGB1555 tiene **un solo bit** de alfa. Hoy se
  resuelve por modo de mezcla del cuadro entero, no por texel.
- **Leer el front end en 480i.** Está probado el modo, no la legibilidad en una tele.
- **Dos cuadros de menos** en los frames 126–130 del guion de carrera, los menús justo
  antes de entrar: 239 contra 241 de la referencia. `front_end_120.txt` no llega ahí.
- **M4:** factor de escala de las texturas, método y formato.
- **Alcance:** si la primera versión incluye 2P y FMV.
- **Parche local a KallistiOS:** habría que proponerlo aguas arriba.
- **Autor de nfshs-rs:** contacto sobre licencia y posible colaboración.
- **Reflejos y sombras sustractivos (decisión de Jorge pendiente).** El PVR suma siempre
  sus dos factores de mezcla: no existe `B − F`, que es el modo 2 de la semitransparencia
  de PS1. Lo usan los reflejos de los coches y la sombra de los pasos de rueda; hoy esas
  piezas **no se dibujan**. Alternativas: oscurecer con alfa sobre negro (proporcional,
  no exacto), volúmenes modificadores (solo sombras), no dibujarlos, o **renderizar a
  textura y restar con el SH-4** (el más exacto).
- **Medir el coste de leer VRAM desde el SH-4 en consola real.** Decide si la resta por
  SH-4 es viable. Era una estimación mía («es caro») y no está medida; Flycast no modela
  ese coste. KOS ofrece `pvr_scene_begin_rtt()` y los contadores de `perfctr.h`. Se haría
  con un programa de prueba que lea 640×480 de VRAM y cronometre el ciclo de render a
  textura, y necesita el CDI quemado.
- **Modelos y texturas sustituibles** (p. ej. un coche de mayor resolución): **fuera del
  plan** por decisión de Jorge. El formato `.CAR` ya se carga por código de coche, pero
  habría que resolver los 57 huecos de partes, los faros y abolladuras por triángulo, el
  presupuesto de triángulos en el SH-4 y el de VRAM.
