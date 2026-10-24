# M2c: coches, objetos y efectos

**Criterio de salida (CLAUDE.md §4):** una carrera completa de un jugador.

M2b dejo el mundo en pantalla: pista y cielo a 60 fps. Falta **todo lo que se mueve** y el
HUD. Es el hito que convierte un paseo por un escenario en una carrera.

## 1. Lo que falta, por piezas

| Pieza | De donde sale | Estado |
|---|---|---|
| **Objetos del escenario** | modelos del GRP (`0x21/0x08`) colocados por bloque | la lista la decide el juego (`DrawW_DoObjects`) |
| **Coches** | modelos GEO dentro de los VIV `ZZZ*` | el juego dice **que partes** dibuja de cada uno |
| **Sombras de los coches** | el juego las construye por coche | — |
| **Reflejos** | mapas de reflexion de la pista, por poligono | el PVR **no tiene blending sustractivo** (CLAUDE.md §3) |
| **Destellos de farolas y halos** | `BWorld_BuildGlareEffects`, como primitivas 2D | se graban como la lista 2D de M2a |
| **Humo, marcas de neumatico, clima** | el juego los construye por frame | — |
| **Lineas pintadas de la carretera** | `0x09` de cada bloque (`Trk_Line`) | el juego las pide con el indicador 4 del trozo |
| **HUD** | los **cuatro ayudantes** aplazados en M2a paso 5 | cuentan pero no se atienden |

## 2. Dimensionado, medido

### Los objetos del escenario (`ZTR06`, con `nfshs bake-track`)

| | |
|---|---|
| Modelos de objeto | **353** |
| Vertices / cuadros de todos ellos | 2 244 / **903** |
| Colocaciones en bloques | **707** (5,2 por bloque) |
| Colocaciones globales (islas, cabos) | 11 |
| Modelos con animacion por fotogramas | 2 |

Con los **9 bloques por vista** que midio M2b salen del orden de **47 objetos por frame**,
de 2,6 cuadros de media: unos **240 triangulos**. Al lado de los ~1 150 de la pista, nada.
Y los 353 modelos enteros son 903 cuadros: **caben en RAM sin pensarlo**, igual que la
pista.

### Los coches

Los modelos van en GEO (`src/formats/geo.rs`) dentro de los VIV `ZZZ*`: cada uno son
**partes** con sus vertices, normales y poligonos de **tres** vertices (no cuatro, al
contrario que la pista). El juego decide por frame **que partes** se dibujan
(`R3DCar_ObjectVisible`), con que codigos de luz y abolladura, y con que mapa de
reflexion. *(El recuento de partes y poligonos se mide al hornearlos, en el paso 2.)*

## 3. Orden de trabajo, y por que ese

| # | Paso | Cierra con |
|---|---|---|
| 1 | **Grabacion de los objetos** en el host de C | **Hecho (2026-10-24)**: 31 686 objetos identicos a la referencia en 900 frames |
| 2 | **Horneado y dibujo de los objetos** | **Hecho (2026-10-24)**: el escenario se puebla, 110 triangulos, 60 fps |
| 3 | **Grabacion de los coches** | **Hecho (2026-10-24)**: 9 494 coches identicos a la referencia en 5 000 frames |
| 4 | **Horneado y dibujo de los coches** | **Hecho (2026-10-24)**: los coches se ven; 30-40 fps, y el coste **no es dibujarlos** |
| 5 | **El HUD**: los cuatro ayudantes de M2a | la carrera se **lee**: velocidad, marcha, tiempos, posicion |
| 6 | **Efectos**: sombras, destellos, humo, marcas, clima, lineas | la carrera se parece al original |

Los pasos 1 y 3 van primero **porque tienen oraculo**, igual que la lista 2D de M2a y la
vista de M2b: la referencia graba exactamente lo mismo, asi que se comparan campo a campo
dentro de `make diff` sin encender un Dreamcast. Del 2 en adelante hay que mirar.

El HUD va **antes** que los efectos a proposito: sin el, una carrera no se puede jugar
—no hay velocimetro, ni vueltas, ni posicion—, y los efectos son adorno comparados con
eso.

## 4. Riesgos propios de M2c

- **Los reflejos sustractivos.** El PVR no los tiene (CLAUDE.md §3). Hay que decidir una
  alternativa, y es una decision de producto: no se elige por cuenta propia.
- **El atlas del PVR.** La pista ya ocupa 1 o 2 MB de los 8. Los coches traen su propio
  atlas de 256x256 cada uno, y en una carrera hay varios. Si aprieta, la pista se puede
  partir en varios atlas, porque es opaca y el orden no importa.
- **Los triangulos de los coches.** Son poligonos de tres vertices y hay hasta ocho
  coches: es la primera vez que el conteo puede acercarse al del mundo.
- **El HUD escribe en la RAM del juego.** Los cuatro ayudantes **devuelven anchos
  calculados**, asi que atenderlos mal no se ve: diverge la simulacion. Tienen oraculo,
  y hay que usarlo.

## 5. Paso 1 hecho: la grabacion de los objetos (2026-10-24)

`dc/src/host/objects.c`. El juego **no deja los objetos listos en su RAM**: los va
dibujando, y por el camino decide cuales entran (los que siguen en pie, los tumbados, los
animados). Asi que el port corre `DrawW_DoObjects` **anidada** con un **hook interno** que
intercepta sus dos rutinas de dibujo —`DrawObjectTransform` y `DrawObjectSimple`— y, en
vez de dejarlas construir primitivas, anota el modelo, su matriz y su posicion. El mismo
mecanismo que ya usaba `showroom.c`.

El hook se come tres llamadas **a proposito**:

- `ObjectClipped`, porque recorta con el cono 4:3 mas estrecho de PS1 y el port dibuja a
  640x480;
- las dos de destello (`FlareHalo`), que son efectos 2D y se graban por otro lado.

**Validado con oraculo**, en la version 2 del volcado `NFSV` que ya llevaba la vista:

```
vista de carrera:
  C: 900 frames, referencia: 900 frames (900 en comun)
OK: 669 vistas y 31686 objetos identicos en 900 frames
```

| Medido, 5 000 frames de carrera | |
|---|---|
| **Objetos por vista** | **media 46,1, maximo 92** |
| Estimado antes de medir, desde el GRP | ~47 |

La estimacion estatica (707 colocaciones entre 136 bloques, por los 8,4 bloques que pide
una vista) acerto en el primer decimal.

## 6. Paso 2 hecho: hornear y dibujar los objetos (2026-10-24)

Los 353 modelos del GRP van al `.TRK` (version 3) con el mismo formato que las mallas de
un bloque, pero **cada uno con su propio charco de vertices**: se colocan por separado,
uno por cada objeto que el juego decide dibujar. Sus vertices estan en 1/64 m alrededor
del punto de colocacion.

### Como se coloca cada objeto, que es lo unico delicado

- **Los fijos, por la TRANSPUESTA de su matriz.** El original carga el GTE con
  `(M * vista)^T = vista^T * M^T`, asi que al modelo le toca `M^T`: las **columnas** de la
  matriz tal como viene.
- **Los billboard, girados hacia la camara** alrededor de Y, con la escala sacada de la
  longitud de las dos primeras columnas (la primera da el ancho, la segunda el alto).

Y aqui hay una trampa que conviene dejar escrita. El renderer del port calcula todo en
espacio de PS1 y **espeja la X al final**. Para un giro eso no es inocente: la matriz que
hay que darle no es el giro `R(theta)` del mundo sino `S*R*S*D`, y **`S*R*S = R(-theta)`**.
Pasarle `R(theta)` deja los billboard girados al reves, que es de las cosas que se ven mal
sin saber por que.

`malla()` se generalizo para aceptar una matriz de colocacion: la pista le pasa NULL y los
objetos se la pasan siempre.

### Medido

| | |
|---|---|
| Objetos dibujados en el frame mirado | **51** |
| Triangulos que cuestan | **110** |
| Triangulos de la pista, para comparar | ~1 150 |
| **fps en carrera, con todo** | **60** |

Los objetos cuestan **menos del 10 %** de lo que cuesta la pista. La estimacion previa
(2,6 cuadros por objeto, o sea ~240 triangulos para 46 objetos) se quedo **larga**: los
modelos que de verdad se colocan son mas pequenos que la media de los 353.

## 7. Paso 3 hecho: la grabacion de los coches (2026-10-24)

`dc/src/host/cars.c`. Mismo patron que los objetos: el juego **dibuja** los coches y por
el camino decide que partes de cada modelo entran, con que luz, con que sombra y con que
mapas de reflejo. El port corre `R3DCar_InsertAllListFacet` **anidada** con un hook
interno que sustituye `R3DCar_InsertCarFacetII` y anota la decision en vez de construir
primitivas.

Por coche salen **99 palabras**: el `Car_tObj`, la luz de la pista en el, si hay sombra y
sus cuatro esquinas (relativas a la posicion del coche en x y z, porque se dibuja en una
pose interpolada), su color, los **57 bytes de `R3DCar_ObjectVisible`** que dicen que
partes se dibujan, el resultado de `DrawC_PrimStart`, los mapas de reflejo del cache del
coche y los **26 codigos de luz y abolladura** por textura.

### El oraculo compara el crudo

Los dos lados vuelcan **la lista de palabras tal cual**, no campos interpretados. Es
menos codigo y mas estricto: cualquier diferencia en cualquiera de las 99 salta.

```
OK: 4769 vistas, 220002 objetos y 9494 coches identicos en 5000 frames
```

### Dos cosas que hubo que respetar al pie de la letra

1. **El orden.** Los coches van **despues** del HUD, que es donde los pone el original.
   El port los tenia antes y hubo que moverlos: ese orden decide que ve cada rutina.
2. **El ancho del cono de visibilidad.** La referencia divide la fila x de la vista
   (`CAR_CONE_WIDTH = 2`) para ensanchar el cono, porque su renderer es mas ancho que el
   4:3 de PS1. Es una **mejora suya**, no del original, y M1a la dejo anotada como deuda
   «si se porta, detras de una opcion apagada por omision». Asi queda: `car_cone_width`
   vale **1** en el port (el comportamiento del original) y el test diferencial lo sube a
   2 con `--carcone 2`, porque si no los dos lados no son comparables. Es el mismo caso
   que `--unlock`.

### Medido, 5 000 frames de carrera

| | |
|---|---|
| **Coches por vista** | **media 1,99, maximo 4** |

Con hasta 4 coches a la vez, y sus modelos siendo triangulos y no cuadros, el paso 4 es
la primera vez en el hito que el conteo de poligonos puede acercarse al del mundo.

## 8. Paso 4 hecho: los coches en pantalla (2026-10-24)

### El horneado

`dc/tools/bake/bakecar.rs` (`nfshs bake-car CODE OUT.CAR [--rival]`) reutiliza
`assets::load_car`, que ya sabe lo dificil: el nombre del VIV (`ZZZ<code>H.VIV` para el
del jugador, `S` para los demas), las dos paginas de textura, la pintura y las partes con
su rueda y su buje.

Un modelo son **partes indexadas igual que los 57 bytes de `R3DCar_ObjectVisible`** que el
paso 3 ya graba, asi que el port usa esa captura como indice directo: 1 a 16 se dibuja,
de 17 en adelante es un halo de luz (2D, va en el paso 6). Sus poligonos son **triangulos**,
no cuadros como la pista.

**Un fallo silencioso que hubo que cerrar.** `load_car` empaqueta en un atlas cuadrado, y
las dos paginas de 256x256 mas el texel blanco del cargador **no caben en 512 por una
fila**: avisa y **tira una pagina entera sin fallar**. El horneado carga en uno holgado y
despues **recorta a 512x256** —solo las dos paginas, una al lado de otra, recolocando las
UV—, que ademas baja la VRAM del PVR de 2 MB a **0,25 MB por coche**.

| Modelo | Partes | Triangulos | Archivo |
|---|---|---|---|
| MSLK (el del jugador) | 57 (10 de rueda) | 1 222 | 343 KB |
| WOLW, MERC, ARMY (trafico) | 57 | 218-224 | ~272 KB |

### La colocacion

El origen del modelo esta **a ras de suelo** y la posicion que simula el juego es el
centro de la caja del cuerpo, asi que el coche se baja por la media extension en Y de esa
caja. Las filas de `orient` son derecha, arriba y adelante en espacio de PS1, y el modelo
tiene **+X a la izquierda** del coche, asi que su eje x es `-derecha`; las tres se
ortonormalizan como hace la referencia. Las **ruedas** giran alrededor de su buje: las
delanteras por `wheel_rot[0]` y ademas viran, las traseras por `wheel_rot[1]`.

### Lo medido, y donde esta de verdad el coste

En Flycast con BIOS real, `race_long`:

| | |
|---|---|
| Coches dibujados a la vez | 2 a 4 |
| Triangulos que cuestan | ~110 a 360 |
| Triangulos de la pista | ~1 370 |
| **fps con los coches dibujados** | **29-38** (media 28,0) |
| **fps con el dibujo de coches APAGADO** | **31-40** (media 28,6) |

**Dibujar los coches no cuesta casi nada: lo que cuesta es GRABARLOS.** Apagar el dibujo
deja el ritmo practicamente igual, asi que la caida de ~60 a ~30-40 fps la produce el
**paso 3**, que corre codigo real del juego en cada frame: `R3DCar_InsertAllListFacet`
anidada y, por coche, `BWorldSm_QuadLight`, `DrawC_PrimStart` y **una segunda pasada
entera** de `R3DCar_InsertCarFacetII` para los halos.

Sigue **por encima del minimo de 30 fps sostenidos** de CLAUDE.md seccion 3, y es codigo
recompilado: justo lo que M3 ataca («codegen, SH-4 a mano en hooks»). Un candidato
concreto ya identificado es la **segunda pasada de halos**, cuyo resultado hoy se tira
porque los halos son el paso 6.
