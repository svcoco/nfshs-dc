# M2c: coches, objetos y efectos

**Criterio de salida (CLAUDE.md §4):** una carrera completa de un jugador.

**Estado: cerrado en emulador (2026-10-26)**, con `carrera_completa.txt` identica a la
referencia en 40 000 frames (seccion 13). La prueba en una consola real es el M7.

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
| 5 | **El HUD** | **Hecho (2026-10-24)**: 4 769 frames de carrera con la lista 2D identica a la referencia, HUD incluido |
| 6 | **Efectos**: sombras, destellos, humo, marcas, clima, lineas | **Hecho en emulador (2026-10-25)**: todos los efectos con oraculo, salvo la imagen; la lluvia, comparada con el original en DuckStation |
| 7 | **El showroom del front end**: el coche de seleccion, concesionario y garaje | **Hecho en emulador (2026-10-26)**: grabado con oraculo, dibujado con el PVR, pinturas y mapa de reflejo; y la policia con la librea de su region (las sombras de los pasos de rueda no van: el juego no las pide en el showroom) |
| 8 | **La pausa en carrera**: el menu y lo que se ve detras | **Hecho en emulador (2026-10-26)**: RAM, 2D y vista identicas con `pausa.txt` |
| 9 | **Una carrera completa, y la noche** | **Hecho en emulador (2026-10-26)**: 40 000 frames identicos con `carrera_completa.txt` |

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
recompilado: justo lo que M8 ataca («codegen, SH-4 a mano en hooks»). Un candidato
concreto ya identificado es la **segunda pasada de halos**, cuyo resultado hoy se tira
porque los halos son el paso 6.

## 9. Paso 5 hecho: el HUD (2026-10-24)

### Lo que M2a creia y no era asi

M2a dejo aplazados «los cuatro ayudantes del HUD» (`Hud_BuildString`, `ThinBox`,
`PauseBox`, `FBuildF4`) pensando que habia que sustituirlos. **En carrera no se
sustituyen**: la referencia deja correr el codigo original, que construye el HUD
—velocimetro, tacometro, marcha, cronometro, posicion, mapa— en **tablas de ordenacion**,
una por vista. Lo que hace el host es **grabar esas tablas** en cada `DrawOTag`, salvo la
del mundo (la primera, con miles de paquetes), y al cerrar el frame quedarse con las que
van detras de las vistas de jugador. Los ayudantes solo se sustituyen con la partida en
**pausa**, que es el paso 8 (seccion 12).

Y tenia oraculo desde M2a: es la misma lista 2D. En carrera daba 68 361 comandos contra
210 157; ahora, en el guion largo:

```
carrera (frame >= 232): 4769 de 4769 identicos byte a byte
```

### Cuatro fallos, por orden de aparicion

1. **El comparador se paraba a las 5 diferencias.** Las de los menus previos a la carrera
   (la deuda de los dos cuadros, ya anotada) tapaban todos los frames de carrera. Se mide
   ahora por tramos, con la carrera aparte.
2. **La linea engordada hacia el lado equivocado.** La referencia redondea la normal de
   media unidad, y eso solo da +-1 en lineas horizontales o verticales; en las diagonales
   cae a (0, 1). El port engordaba las diagonales por su eje dominante y movia **la aguja
   del tacometro** un pixel. Se resuelve en enteros, exacto.
3. **La tabla de entornos de dibujo se llenaba en el front end.** `SetDrawEnv` se llama
   con decenas de paquetes distintos antes de la carrera; una tabla fija de 64 se llenaba
   de ellos y los de carrera nunca entraban, asi que el recorte de la esfera del
   tacometro salia a pantalla completa. Ahora es un anillo.
4. **La referencia no era determinista.** Buscaba el entorno por desplazamiento en un
   `HashMap` de semilla aleatoria, y hay varios con el mismo: tres ejecuciones de la
   referencia daban listas distintas (14 y 177 frames). Lo correcto es buscar por la
   **direccion del paquete**, que es justo la que se le dio a `SetDrawEnv`, sin
   ambiguedad. **Se arreglo en los dos lados** —en la referencia, en
   `src/ps1/host/overlay.rs`, como arreglo de determinismo del oraculo—, con el
   desplazamiento de respaldo. Tres ejecuciones dan ahora el mismo hash.

### En el PVR: recortar tambien lo inclinado

El render 2D solo recortaba los cuadros alineados a los ejes, y la aguja del tacometro
—una linea inclinada que el HUD recorta a su esfera de 28x28— se salia por debajo. Ahora
los no alineados se recortan con **Sutherland-Hodgman** contra los cuatro bordes,
interpolando UV y color (hasta 8 vertices), y se mandan como tira en zigzag.

### Medido

| | |
|---|---|
| Comandos del HUD por frame | ~58 |
| fps en carrera con HUD, coches, objetos, pista y cielo | **30-43** |

El HUD no mueve el ritmo: el coste sigue siendo el de grabar los coches (paso 4).

## 10. Paso 6: los efectos (hecho en emulador, 2026-10-25)

Hechos las **sombras**, las **líneas pintadas**, las **marcas de neumático**, los
**destellos** de las farolas, los **halos** de los coches, el **humo**, el **flare del sol**,
el **clima** y los **reflejos** de los coches.

### Lo sustractivo: resta exacta con tres mezclas del PVR

El PVR suma siempre sus dos factores de mezcla, y el modo 2 de PS1 resta. La identidad
`B − F = 1 − ((1 − B) + F)` lo resuelve sin tocar un píxel desde el SH-4: **invertir, sumar
F e invertir otra vez**, las tres en la lista translúcida y antes del 2D. Las inversiones
solo cubren la caja de los efectos (se calcula con un recorrido previo sin dibujar).

**Primera versión, y por qué se cambió.** Empecé con `ZERO` / `INVERSE-OTHER-COLOUR`, que da
`B·(1 − F)`. La verifiqué dibujando un gris del 50 % sobre media pantalla (cociente 0,50:
la técnica estaba bien implementada). Pero oscurece en **proporción**, y las marcas de
neumático tienen un F de ~0,05: sobre carretera oscura dejaban **2,5 niveles** contra los
**13** que resta la PS1, y no se veían. Esa medida la dio un frame real, no el rectángulo.

La versión exacta se verificó con un gris de 26/255 sobre media pantalla en Flycast:
diferencia **absoluta de 26 / 24 / 23** niveles con un cociente que varía de 0,55 a 0,78,
que es lo que distingue una resta de un porcentaje. Sobre marcas reales: **13,3 niveles**.

### Las líneas pintadas

Cada bloque del GRP trae registros `(vértice, sección, tipo, cuadro)`; cada par consecutivo
forma un cuadro texturado con `LIN<tipo>`, ensanchado a ambos lados por el vector derecho
de la sección de carretera **más cercana** (sus bytes 6..8, con signo, `>> 3`). Todo cae en
unidades de 1/64 m, así que se calcula entero en el horneado. Se añaden como **diez
materiales más**, tras los de la pista, para que sus cuadros usen el mismo formato que los
demás. Formato del `.TRK`: **versión 5**, bloques de 56 bytes con siete tramos.

### Las marcas de neumático

El host las lee de la RAM del juego (segmentos de 688 bytes, hasta 64, con hasta 23
entradas) y se dibujan con `SKD0`/`SKD1`, solo las de las secciones a la vista y a menos de
50 m. **Con oráculo**: la lectura coincide con la referencia en 1 069 983 marcas
(`race_long`, 5 000 frames) y 457 416 (`derrape`, 2 800 frames).

### Un guion nuevo, y por qué hizo falta

Ningún guion existente ponía las marcas a la vista: `race_long.txt` va en recta, y la cámara
por omisión es la de **capó**, con las marcas a ~1 m, debajo y detrás. Se añadió
`dc/test/diff/scripts/derrape.txt` (acelerar en recta y luego derrapar) como segundo
escenario del oráculo, y el arnés de PC una opción `PS1_CAMBACK=<m>,<alto>` para ver desde
fuera. **No es del juego.** Un primer derrape, lanzado desde el frame 900, acabó contra la
pared de la salida sin avanzar.

| Escenario del oráculo | Resultado |
|---|---|
| `race_long` (5 000 frames) | 4 769 vistas, 220 002 objetos, 9 494 coches y 1 069 983 marcas idénticos |
| `derrape` (2 800 frames) | 2 569 vistas, 117 260 objetos, 2 320 coches y 457 416 marcas idénticos |

### Destellos, halos, humo y el flare del sol

Son **2D del propio juego**: rutinas que proyectan un punto del mundo y escriben cuadros de
pantalla. El host las corre anidadas en el mismo punto y orden que la referencia
(`host/glows.c`):

| Rutina | Qué | Ancla |
|---|---|---|
| `BWorld_BuildGlareEffects` (0x8007E068), por trozo a la vista | farolas | el `coorddef` que reciben `Flare_Halo`/`Flare_Halo2` en a3 |
| `Souffle_InsertFacet` (0x800B8ABC) | humo y polvo | `Souffle_tISouffle + 12`, en a1 de `Sfx_BuildSouffleFacet` |
| `Weather_Render` (0x800E493C) | clima | — |
| `Flare_Sun` (0x800CFF30), con la rotación del horizonte en el GTE | flare del sol | — |
| `R3DCar_InsertCarFacetII` con la carrocería saltada | halos de faros y frenos | la posición del coche |

Cada destello lleva sus cuadros y el punto del que sale. **La cámara del port es la del
juego**, así que los cuadros caen donde los puso el original, y solo se les da una
profundidad (`glow_depth`, en `render/fx.c`, portable) para probarlos contra lo opaco. Hay
un detalle de fidelidad que no es cosmético: grabar paquetes mueve el modo de dibujo
vigente (E1), y el HUD se graba después; por eso también se corre y se graba el clima en
su sitio.

| Oráculo (vista v5) | `race_long` | `derrape` |
|---|---|---|
| destellos idénticos | 2 071 (586 halos, 607 bocanadas) | 825 (4 halos, 30 bocanadas) |
| cuadros del flare del sol | 29 038 | 13 608 |
| cuadros de clima | 0 | 0 |

Por modo de mezcla, en `race_long`: farolas 10 062 cuadros y halos 12 136, todos sin
textura y sumando; humo 602 sumando y **5 restando**.

Medido en el arnés de PC con un frame real: el polvo del frame 1853 aclara 542 píxeles,
**24,1 niveles de media**; el halo del frame 4500 cae sobre los pilotos traseros del coche.
En Flycast, la farola y el flare del sol se ven en su sitio.

**Sin medir:** el coste en fps de correr esas rutinas cada frame, y el del dibujo, que en
este paso no se separó.

### Los reflejos de los coches

`DrawC_Prim`, camino de entorno: los polígonos de las partes 0, 8, 10, 41, 42, 45 y 46 que
miran a la cámara se dibujan otra vez con un mapa de reflejo de la pista.

| Mapa | Coordenadas | Polígonos | Mezcla medida |
|---|---|---|---|
| A | normal girada por la guiñada de la cámara: u = u0 + 64 + x, v = v0 + \|z\| | todos | suma (modo 1) |
| B | normal tal cual: u = u0 + 64 + y, v = v0 + desplazamiento + z, en la ventana 128 × 64 | los marcados 0x3F3 | resta (modo 2) |

- **Mapas:** `bake-refl NN OUT.RFL` hornea `ZTR<nn>R.PSH` a una página de 256 × 256 con cada
  mapa de 128 × 64 en u 128, v 64·n, como el juego en VRAM (8 542 texeles con color en
  ZTR06).
- **`.CAR` versión 2:** por cada triángulo de las partes que reflejan, sus indicadores y,
  por esquina, su vértice del GEO y la normal del GEO entre 64. El coche del jugador crece
  11 KB (392 triángulos); los demás, 2 KB.
- **Normales de la RAM.** Las del GEO no sirven: **0 de 1 176** esquinas coinciden con las
  que el juego tiene en memoria (hasta 98 de diferencia, otra escala). El host las lee cada
  frame (`car_env_normals`, unas 300 por coche) y el renderer las usa si el número de
  vértices cuadra con el GEO. Solo los coches de carrera tienen ranura de modelo; el
  tráfico no refleja, como en la referencia.
- **Visto:** en el arnés de PC, 195 triángulos de reflejo en el coche del jugador en el
  frame 420; en Flycast, el mismo brillo y moteado en el capó durante la presentación.

### El clima

El guion `dc/test/diff/scripts/lluvia.txt` llega a una carrera con lluvia **por los menús**:
«single race», «weather» en ON en las opciones de pista y la compra del Mercedes SLK 230 (una
carrera normal exige tener coche). El juego carga entonces `zTr06W.grp`, la variante mojada,
que se hornea a `ZTR06W.TRK` como cualquier otra.

| Oráculo (`lluvia.txt`, 6 000 frames) | Resultado |
|---|---|
| RAM y registros | idénticos en los 6 000 |
| cuadros de clima | **694 736 idénticos** |
| trazos fuera de los 11 bits de la GPU | 0 de 665 175 |
| lista 2D | idéntica en carrera; los menús 296–1 160, con la deuda conocida de dos cuadros |

El oráculo destapó que la conversión de líneas a cuadros del port solo coincidía con la
referencia en pantalla: con una punta en x = 8 205 la normal en `f32` redondea distinto.
Ahora se reproduce la aritmética `f32` exacta.

**Referencia visual: el juego original en DuckStation** (renderer por software, BIOS
SCPH-1001, el mismo camino de menús). Pista mojada, cielo de tormenta, trazos cortos y gotas
como anillos pequeños; el port lo dibuja igual en el arnés y en Flycast.

Un primer intento forzaba el clima escribiendo `GameSetup_tData`. El juego activaba la
lluvia sobre la pista **seca** y sin sus texturas, y salían trazos que cruzaban la pantalla y
gotas como cuadros oscuros, que parecían dudas sobre la GPU de PS1. Eran artefactos del
forzado, que se retiró.

## 11. Paso 7 (hecho en emulador): el showroom del front end (2026-10-25)

Del showroom solo estaba portada la mecanica: el coche no se pintaba. Plan acordado con Jorge:
(1) grabarlo como la referencia, (2) dibujarlo con el PVR, (3) hornear todos los coches que
puede ensenar (la decision de la pintura, pendiente).

### 1. Grabacion

| Que | Rutina | Como se graba |
|---|---|---|
| coche y reflejo | `R3DCar_InsertCarFacetMenuII` (0x800B3428), dos veces | `ShowroomCar`: pose, partes, camara, H/OFX/OFY del GTE, nivel de luz, reflejo; el modelo, el de `DrawCar` (0x8003AC64) |
| halos | la misma, con la carroceria saltada | sus paquetes, detras del coche |
| sombra | `DrawC_ShadowPrimClip` | aparte, hasta que la recoge el suelo |
| suelo | `R3DCar_Showroom` (0x800B3850) | `ShowroomFloor`: la sombra, el anillo (32 cuadros, con las luces que giran segun el contador de ticks) y el foco (abanico desde el nivel de luz) |

En la lista 2D van como **marcadores** que apuntan a tablas del host, asi la lista de
cuadros no cambia. Volcado de la lista 2D **version 2**, en los dos lados:

| Oraculo | `race_long` | `lluvia` |
|---|---|---|
| coches del showroom | 92 identicos | 1 000 identicos |
| suelos | 46 identicos | 646 identicos |
| lista 2D completa | 370 703 cuadros identicos | 935 968 identicos |

La lista 2D sale identica entera por primera vez: la "deuda de dos cuadros" era
`tScreenTrackSelect::DrawVideoWall` (los dos cuadros del video de la pista que gira, que la
referencia graba al llamarla desde `DrawBackground`), portada ahora.

### 2. Dibujo

- Los coches van en la lista **punch-through**, con su profundidad; el orden de la referencia
  (2D de debajo, reflejo, suelo, coche, 2D de encima) se hace con la profundidad: reflejo en
  una franja lejana (`Z_FONDO + 1/w x 0,0007`), 2D de debajo a `Z_FONDO` con `GEQUAL`, suelo a
  `0,0005` con `GEQUAL`, y el resto sin prueba.
- Proyeccion con la distancia y el centro del GTE (`RaceView.centre`); el reflejo con la
  altura negada y la y espejada, como la referencia (`CarDraw.mirror`).
- Visto en Flycast: coche, reflejo, plataforma con su foco y halos de los pilotos.

Al compararlo con DuckStation aparecieron **dos fallos de brillo anteriores**: el 2D sin
textura salia al doble (el 0x80 = 1,0 solo vale con textura) y los coches a la mitad (el color
neutro del horneado es 0x80 y el PVR modula por color/255). Corregidos; tambien el minimo de
luz 24 de la referencia.

### 3. La pintura y todos los coches

Decision de Jorge: **paleta en el disco, textura normal en la VRAM**. Medidas que la
justifican (`car-pal-stats`, `car-pal-tuples`):

| | MSLK | F550 | DIAB | MCF1 |
|---|---|---|---|---|
| paletas en la pagina de carroceria | 27 | 32 | 31 | — |
| series de color en las 64 pinturas | 431 | 497 | 503 | 575 |

- Una version paletizada en el PVR se veria igual (las texturas ya son de 4 bits con colores de
  15 bits) y la memoria de paletas (1 024 entradas) no da para los 8 coches de una carrera.
- **`.CAR` version 3:** por texel el indice (16 bits) de su serie de colores en las 64
  pinturas, y la tabla de series. El SH-4 expande la textura de la pintura pedida al cargar
  (`car_paint`) y la sube; es exactamente lo que da `load_car` con esa pintura. El horneado
  comprueba que la geometria y las UV no cambian entre pinturas.
- Caché de Dreamcast: modelo por codigo (6, expulsa el menos usado) y hasta 4 texturas
  pintadas por modelo.
- `make bake-coches`: 18 de carrera (H) y 21 de trafico, 14 MB, 20 s.
- Visto en Flycast: «COLOR» cambia la pintura y «CAR» recorre los coches. En el arnes, el
  guion de prueba pasa por 10 coches, todos horneados.

### 4. El mapa de reflejo de la carroceria (2026-10-26)

- El mapa A del showroom esta en la **VRAM de PS1**, no en la pagina de reflejos de la pista:
  en `race_long`, siempre pagina `0x3E` (4 bits, mezcla por suma), paleta `0x7A28`, desde
  u 0, v 128. `host_build_textures` pide sus 128 x 65 texeles a la cache del 2D.
- `CAR_PASS_ENV_SR` en `car_draw`: las coordenadas del mapa A recortadas al mapa
  (`u = 64 + x` en 0..127, `v = |z|` hasta 64), como `Showroom::geometry` de la referencia;
  el sumidero las lleva de la pagina de 256 x 256 a la entrada de la cache.
- Se dibuja en la lista translucida, sumando: el del reflejo en el suelo **antes** del
  suelo (que lo tapa) y el del coche despues.
- **Diferencia a proposito con la referencia:** alli el reflejo se acerca 1 cm a la camara y
  se prueba con "mas cerca". Donde dos paneles se solapan, el reflejo del de debajo tambien
  pasa y se suma dos veces: en Flycast salia una linea de puntos claros en las aristas del
  capo y del lateral. Aqui los vertices son exactamente los de la carroceria (el mismo
  sesgo por parte, sin acercarlos) y la prueba es de **profundidad igual**. Tambien se
  descartan los poligonos que no miran a la camara, como `DrawC_Prim` (con el sentido
  invertido en el coche espejado).
- Visto en Flycast: brillos en el capo y el parabrisas que se mueven al girar el coche.

### 5. Las sombras de los pasos de rueda: no van en el showroom (medido, 2026-10-26)

La referencia pinta en negro las partes que restan si el juego las pide (`dark` en
`Showroom::geometry`), y eso se habia leido como "faltan en el showroom". Medido en el arnes,
con la visibilidad de partes grabada en cada `ShowroomCar`:

| Guion | Coches | Registros | Parte 5 visible |
|---|---|---|---|
| recorrer coches (10 modelos) | ADB7, BMWM, BMWZ, CMRO, CORV, F550, JXKR, MSLK, P993, TRAM | 2 432 | 0 |
| `race_long` | MSLK | 92 | 0 |
| `lluvia` | MSLK | 1 000 | 0 |

La parte 5 es la unica que resta en los 16 modelos que tienen alguna (23 no tienen). El juego
solo la enciende en carrera, donde ya se dibuja restando (paso 6). No hay nada que portar.

### 6. La policia, con la librea de su region (2026-10-26)

- `CBMW`, `CDBL`, `CCMR`, `CCOV` y `CCPR` no son archivos: el juego cambia la primera letra
  por la de la region de la pista y abre esa librea (`CBMW` -> `zzzgBMWs.viv` en Alemania).
- `host/files.c` anota, al abrir un `zzz?xxx[hs].viv` de policia, su letra
  (`GameHost.police`), y `host_car_file` traduce el codigo al `.CAR` de esa librea, como
  `opened_variant` de la referencia. Lo usan los dos renderers. El estado del juego no
  cambia: el test diferencial sigue identico.
- `make bake-coches` hornea las 14 libreas del disco (`CARS_POLICE`): ABMW BBMW FBMW GBMW,
  ADBL BDBL FDBL GDBL UDBL, BCMR FCMR GCMR, CCOV, CCPR. De 380 a 403 KB cada una.
- Prueba: una persecucion por los menus (`hot pursuit`, solo, beginner, Alemania). En el
  arnes, el juego abre `zzzgCMRs.viv` y `zzzgBMWs.viv`; las patrullas entran en vista desde
  el frame 4119 y `CBMW` se resuelve a `GBMW`. El rasterizador de PC (mismo `car.c`) dibuja
  la patrulla blanca con la franja verde. En Flycast la persecucion corre; el coche oscuro
  que sale en las capturas es el Z3 del jugador, con la camara exterior tras salirse.

**Pendiente de mirar:** una patrulla de cerca en Flycast.

## 12. Paso 8 (hecho en emulador): la pausa en carrera (2026-10-26)

El original, en pausa, no pinta el mundo: muestra la imagen que copio y encima el menu, con
`Font_TextXY` y cuatro ayudantes del HUD. La referencia (`views.rs`, `frame.rs`, `race.rs`)
los atiende sin dejar correr el original. El port los dejaba correr y la RAM divergia en el
primer frame de pausa (frame 701 de `pausa.txt`: tres paginas, `r14`/`r15` y 8 ticks).

| Que | Como (igual que la referencia) |
|---|---|
| `Font_TextXY` (0x800CC7F0) | comando `Text`: x, y, cual de las 16 paletas de la fuente, espacio, texto |
| `Hud_BuildString` (0x800D4E9C) | el ancho con `HudPmx_gShapes`; un `Shape` por caracter salvo si solo mide |
| `Hud_BlackThinBox` (0x800D8690) | `ThinBox`, devuelve 0 |
| `Hud_RenderPauseBox` (0x800D9ABC) | `PauseBox`, devuelve 0 |
| `Hud_FBuildF4` (0x800D3558) | `Rect` (semi, x, y, w, h, color, dx0, dx1), devuelve 0 |
| llamadas desde el HUD nativo de la referencia | no se graban (`NATIVE_HUD`) |
| `hold_cameras` | cada frame de pausa devuelve `Camera_gInfo` (0x80110570, 2 x 0x110) a como estaba al pausar, salvo cambio de modo |
| el flare del sol | si en pausa no se pinta, el del ultimo frame |
| objetos en el volcado de vista | sin vistas, ninguno |

**Representacion.** Cada comando es un marcador `OVL_HUD_CMD` (el volcado lo escribe con sus
parametros) seguido de sus cuadros `OVL_HUD_QUAD`, que el volcado se salta y el PVR pinta
como cualquier 2D. Las texturas son las de la VRAM de PS1: el texto, con el mismo blit de la
fuente que el front end; las formas, de `HudPmx_gShapes`, que guarda por entrada u, v, paleta y
**pagina** como un `POLY_GT4`. Las cajas siguen `ps1hud.rs` (marco negro de 2 pixeles con las
esquinas 14-17; la caja de pausa, `0x461414` semitransparente, que en BBGGRR es azul oscuro).

**Lo que se ve (`pausa_presenta`).** Las vistas y el HUD del ultimo frame jugado (el HUD se
copia, hasta 768 cuadros) y el ultimo menu: el juego solo lo pinta cuando cambia, y un frame
con solo rectangulos no lo cambia. Al reanudar hay un frame que no pinta nada (el 1003) y se
ve el ultimo dibujado, como `held`.

**Oraculo.** Volcado de la lista 2D **version 3** en los dos lados: los comandos de la pausa
con su posicion y parametros (120 bytes). Con `pausa.txt`:

| | Resultado |
|---|---|
| RAM | 1 200 frames identicos |
| lista 2D | 211 227 cuadros y 28 comandos de pausa identicos; 0 comandos sin producir |
| vista | 667 vistas, 31 604 objetos, 130 coches, 9 732 marcas identicos |

`race_long` sigue identico en todo. Visto en Flycast: el menu "Game Paused" (Continue,
Restart, Options, Quit Race) sobre la carretera y el HUD.

**Sin probar:** las opciones del menu (Restart, Options, Quit Race) y una pausa en pantalla
partida.

## 13. Paso 9 (hecho en emulador): una carrera completa, y la noche (2026-10-26)

**Criterio de salida de M2c cumplido en emulador:** una carrera completa de un jugador,
identica a la referencia.

### El piloto de pruebas como entrada

Un guion fijo no llega a la meta. La referencia tiene un piloto automatico de pruebas
(`src/game/ai.rs`: sigue la linea central `vroad` de la pista, frena antes de las curvas y
se desatasca marcha atras), pero vive en el camino de la aplicacion. Se ha metido en el
**modo diferencial** de la referencia (`boot-main`), detras de `NFSHS_DIFF_AUTOPILOT=FILE`:
en carrera lee el coche del jugador de la RAM, decide, anade sus botones a los del guion y
los anota en FILE como guion del port. Para eso los dos lectores de guiones aceptan
`0x1234` (el de la referencia no lo hacia) y `PAD_SCRIPT_MAX` sube de 256 a 4 096.

`fe-play` no servia: su arranque (`RaceThread`) deja el juego en otro estado y, con los
mismos botones, los menus no siguen el mismo camino que en el port.

### Resultado (`carrera_completa.txt`)

| Frame | Que pasa |
|---|---|
| ~1 300 | salida (single race en solitario, Alemania, 2 vueltas, el SLK comprado) |
| ~34 050 | meta: "RACE STATISTICS", vuelta 1 4:16.00, vuelta 2 4:42.78, total 8:58.78 |
| ~34 100-37 950 | la repeticion (camaras de television y la barra de video) |
| ~37 950 | de vuelta a "game setup" |

| Oraculo | 40 000 frames |
|---|---|
| RAM | identica |
| lista 2D | 3 345 733 cuadros identicos |
| vista | 36 789 vistas, 1 359 742 objetos, 6 034 coches, 5 931 116 marcas, 225 772 destellos identicos |

Dos fallos de grabacion que salieron aqui:
- **Marcas de neumatico:** se leian en `Render_RenderPlayerView` y quedaban las del frame
  anterior cuando el juego las borra (frames 34 051, 35 998 y desde el 37 950). Se leen
  ahora en la frontera de frame (`host_frame_top`), donde las lee la referencia.
- **El ultimo frame de carrera** (37 950) ya cerraba en los menus con las tablas del HUD
  grabadas antes: 312 cuadros contra 299. La referencia las guarda en `frame_lists` y solo
  las compone en carrera; aqui se quitan (`hud_end`).

### La noche

- **El haz de los faros** (`NightBeam`, `night_beam_read` en `game.c`, `sombra_haz` en
  `track.c`): el host lee el mapa de 64 x 64 niveles (0x8013ED20), la tabla de 256 x 16
  indices (0x8013ED28), su forma (z0, xs, zs) y los ejes del coche del jugador; cada
  vertice de la pista dentro del haz cambia su indice de luz `s` por `table[s][nivel]`.
  Por vertice en el SH-4, como decide la seccion 3 de CLAUDE.md y como el shader de la
  referencia.
- **Pistas N y S rehorneadas:** eran del formato anterior y `track_load` las rechazaba; la
  noche salia en negro.
- **Mapas de reflejo de las variantes:** el renderer pedia `ZTR06WR.RFL`; los mapas son de
  la pista (`ZTR06R.RFL`). Con lluvia o de noche los coches salian sin reflejo.

### La "nieve"

La variante S es clima con noche (`GameSetup` +0x48 y +0x54 a 1). `docs/FORMATS.md` la
llama "snow", pero en Alemania el original pinta **lluvia de noche**: comprobado en
DuckStation por el mismo camino de menus. El guion es `noche_lluvia.txt`: 3 000 frames
identicos y 272 991 cuadros de clima. En Flycast se ve como en DuckStation.

**Sin probar:** la noche sin clima (variante N) en Flycast, otras pistas con clima por si
alguna tiene nieve de verdad, y las opciones de la pantalla de estadisticas (el piloto solo
pulsa Cross).
