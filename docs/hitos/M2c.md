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
| 2 | **Horneado y dibujo de los objetos** (modelos del GRP al `.TRK`) | el escenario se puebla; triangulos medidos |
| 3 | **Grabacion de los coches** (`CarFacet`: partes, luz, sombra, codigos) | la captura del port y la de la referencia coinciden |
| 4 | **Horneado y dibujo de los coches** (GEO + su atlas) | se ven los coches corriendo |
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
