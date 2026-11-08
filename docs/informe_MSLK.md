# MSLK de NFS: High Stakes PC: formato y extracción

## Resultado y alcance

**Leí directamente tu archivo** `E:\Juegos\pc\EA-NeedForSpeedHighStakes\DATA\CARS\MSLK\CAR.VIV`, sin modificarlo. Contiene 27 entradas y ocupa 4 547 788 bytes. SHA-256 del VIV: `15a1f2ddd22b26a2125f42545720f46093a65ad8c4a54d613e436b462018ea41`.

El `car.fce` original tiene **1 295 vértices, 1 588 registros triangulares, 21 partes con geometría y 14 dummies**. El total incluye LOD alternativos. La selección de alta resolución tiene **1 054 registros triangulares y 14 partes**, antes de resolver techo y cristales rotos. El PC no ofrece necesariamente más triángulos visibles que los 1 222 que indicas para PS1.

Etiquetas usadas en este informe:

- **V**: comprobado leyendo los bytes de tu MSLK, contando elementos o comparándolos.
- **D**: significado documentado por quienes desarrollan herramientas; no comprobado ejecutando el juego.
- **I**: inferencia a partir de esos datos.

No he inspeccionado el modelo PS1 ni ejecutado el juego o el port Dreamcast. Sus 57 partes, 46 con geometría, 1 222 triángulos y atlas 512×256 son datos que tú aportaste.

## Identificación y fuentes

**V:** los cinco FCE, incluido `dash.fce`, empiezan por `14 10 10 00`: entero little-endian `0x00101014`. La cabecera de 8 248 bytes, sus tablas y los índices locales encajan en FCE4 de NFS4. No es FCE3 ni el FCE4M de Motor City Online. La documentación primaria de [fcecodec](https://github.com/bfut/fcecodec) remite a la especificación de Addict de 1999 y a OpenNFS; su [definición de estructuras](https://github.com/bfut/fcecodec/blob/main/src/fcelib/fcelib_fcetypes.h) permite contrastar la identificación. No usé resúmenes que asignan erróneamente esta firma a FCE3.

El script adjunto es una implementación independiente en Python, sin dependencias de fcecodec. Sus fuentes se consultaron como referencia, sin incorporar código de la biblioteca.

## BIGF / CAR.VIV

**V:** offsets absolutos desde el inicio del VIV; enteros del directorio big-endian.

| Offset | Tipo/tamaño | Contenido medido |
|---|---|---|
| `0x00` | 4 bytes | `BIGF` |
| `0x04` | u32 BE | tamaño total: 4 547 788 |
| `0x08` | u32 BE | 27 entradas |
| `0x0C` | 4 bytes | `00 00 02 00`; leído BE da 512, exactamente el final del directorio de este archivo |
| `0x10` | registros variables | offset u32 BE, tamaño u32 BE, nombre ASCII terminado en NUL |

El campo `0x0C` no es necesario para extraer. Su coincidencia con el final del directorio en este VIV no demuestra que tenga ese significado en todas las variantes BIGF. El formato general puede contrastarse con el [parser y especificación de TailsLab](https://github.com/AndyGura/nfs-resources-converter/blob/main/resources/NFS4.md).

## Cabecera FCE4: offsets exactos

**V:** todos los enteros que usa el extractor son u32 LE; coordenadas y UV son IEEE-754 float32 LE. Los contadores e índices del MSLK son no negativos. `N=1295`, `T=1588` para `car.fce`. Cada puntero a tabla de datos es **relativo a `B=0x2038`**, no al inicio del archivo ni a la entrada VIV. Las tablas fijas de la cabecera sí usan offsets desde el inicio del FCE.

| Offset FCE | Tamaño | Campo / lectura |
|---|---:|---|
| `0x0000` | 4 | firma `0x00101014` |
| `0x0004` | 4 | desconocido; no asumir identificador ni contador |
| `0x0008` | 4 | número total de triángulos, `T` |
| `0x000C` | 4 | número total de vértices, `N` |
| `0x0010` | 4 | `NumArts=1`; todas las caras del MSLK usan página 0 |
| `0x0014` | 4 | offset posiciones, longitud `12N` |
| `0x0018` | 4 | offset normales, longitud `12N` |
| `0x001C` | 4 | offset triángulos, longitud `56T` |
| `0x0020` | 4 | offset área auxiliar/reservada 1, `32N` |
| `0x0024` | 4 | offset área auxiliar/reservada 2, `12N` |
| `0x0028` | 4 | offset área auxiliar/reservada 3, `12N` |
| `0x002C` | 4 | offset posiciones sin daño duplicadas, `12N` |
| `0x0030` | 4 | offset normales sin daño duplicadas, `12N` |
| `0x0034` | 4 | offset posiciones dañadas, `12N` |
| `0x0038` | 4 | offset normales dañadas, `12N` |
| `0x003C` | 4 | offset área auxiliar/reservada 4, `4N` |
| `0x0040` | 4 | offset banderas de animación por vértice, `4N` |
| `0x0044` | 4 | offset área auxiliar/reservada 5, `4N` |
| `0x0048` | 4 | offset área auxiliar/reservada 6, `12T` |
| `0x004C` | 12 | tres floats `HalfSize` |
| `0x0058` | 4 | número de dummies: 14 |
| `0x005C` | `16×12` | posiciones xyz de dummies; usar solo las primeras 14 |
| `0x011C` | 4 | número de partes: 21 |
| `0x0120` | `64×12` | posición/pivote xyz de cada parte |
| `0x0420` | `64×4` | primer vértice global de cada parte |
| `0x0520` | `64×4` | cantidad de vértices de cada parte |
| `0x0620` | `64×4` | primer triángulo global de cada parte |
| `0x0720` | `64×4` | cantidad de triángulos de cada parte |
| `0x0820` | 4 | cantidad de combinaciones de colores: 7 |
| `0x0824` | `16×4` | colores primarios |
| `0x0864` | `16×4` | colores interiores |
| `0x08A4` | `16×4` | colores secundarios |
| `0x08E4` | `16×4` | colores de pelo del conductor |
| `0x0924` | 4 | desconocido |
| `0x0928` | 256 | desconocido |
| `0x0A28` | `16×64` | nombres de dummies, campos ASCII con NUL |
| `0x0E28` | `64×64` | nombres de partes, campos ASCII con NUL |
| `0x1E28` | 528 | desconocido |
| `0x2038` | — | comienzo de las tablas apuntadas por la cabecera |

**D:** los cuatro bytes de un color son tono, saturación, brillo y un cuarto componente denominado transparencia en las herramientas. Tono/saturación/brillo usan escala 0–255. No se debe trasladar automáticamente ese cuarto componente a opacidad OBJ.

Estas son las posiciones absolutas **medidas** de las tablas de `car.fce`; eliminan la ambigüedad de la base de los punteros:

| Tabla | Inicio decimal | Bytes |
|---|---:|---:|
| Posiciones | 8 248 | 15 540 |
| Normales | 23 788 | 15 540 |
| Triángulos | 39 328 | 88 928 |
| Auxiliar 1 | 128 256 | 41 440 |
| Auxiliar 2 | 169 696 | 15 540 |
| Auxiliar 3 | 185 236 | 15 540 |
| Posiciones sin daño duplicadas | 200 776 | 15 540 |
| Normales sin daño duplicadas | 216 316 | 15 540 |
| Posiciones dañadas | 231 856 | 15 540 |
| Normales dañadas | 247 396 | 15 540 |
| Auxiliar 4 | 262 936 | 5 180 |
| Animación | 268 116 | 5 180 |
| Auxiliar 5 | 273 296 | 5 180 |
| Auxiliar 6 | 278 476 | 19 056 |
| Fin del FCE | 297 532 | — |

**V:** no hay huecos ni solapamientos entre estas tablas. Las copias sin daño coinciden byte por byte con posiciones y normales iniciales en los cinco FCE. `car.fce` tiene 277 posiciones diferentes en la tabla dañada; la tabla de animación contiene 1 268 ceros y 27 valores 4. **D:** 4 indica vértice inmóvil y 0 permite animación del conductor; el efecto depende de la parte. El [tutorial del autor de fcecodec](https://github.com/bfut/fcecodec/blob/main/scripts/doc_Obj2Fce.md) explica pivotes, animación y daño.

### Vértices y coordenadas

Un vértice ocupa 12 bytes: `x` en `+0`, `y` en `+4`, `z` en `+8`. Su normal está en el mismo índice de la tabla de normales, también tres floats. La posición almacenada es local a la parte:

```text
índice_global = primer_vértice_de_parte + índice_local
posición_del_coche = posición_local + pivote_de_parte
normal_del_coche = normal_almacenada
```

No hay matrices de huesos en las estructuras aquí identificadas. Los centros de rueda están en los pivotes de sus partes, no en una lista de dummies de ruedas.

**D:** `+X` derecha, `+Y` arriba y `+Z` adelante; unidad documentada: metro. **V:** las ruedas delanteras están en Z≈+1,174 y las traseras en Z≈−1,193; las izquierdas tienen X negativo. La caja global es aproximadamente `[-0,860,-0,625,-1,998073]` a `[+0,860,+0,625,+1,998073]`. La dimensión de unos cuatro metros es compatible con la interpretación métrica (**I**); no hay un campo que diga «metros». El origen no está en el suelo.

### Registro triangular: 56 bytes (`0x38`)

**V:** tabla contrastada con índices, UV y rangos de todas las caras del MSLK.

| Offset dentro de cara | Tipo | Contenido |
|---|---|---|
| `0x00` | u32 | página de textura; siempre 0 en los cinco FCE |
| `0x04` | 3 u32 | índices de los tres vértices, **locales a la parte** |
| `0x10` | 3 u32 | datos no interpretados; se conservan en JSON |
| `0x1C` | u32 | banderas completas, sin enmascarar |
| `0x20` | 3 float32 | U de las tres esquinas |
| `0x2C` | 3 float32 | V de las tres esquinas |

Las UV pertenecen a las esquinas, no al vértice geométrico. El script escribe tres `vt` por triángulo para conservar las costuras. En `car.fce`, U y V están aproximadamente entre 0,001953 y 0,998047.

**D:** bits conocidos: `0x1` mate, `0x2` reflejo intenso, `0x4` doble cara, `0x8` semitransparente; `0x20` ventana, `0x40/0x80/0x100/0x200` ventana frontal/izquierda/trasera/derecha, `0x400` cristal roto. Otros bits requieren cautela. **V:** hay valores altos como `0x42000`, `0x40000` y `0x8000`; por tanto, **no truncar a 12 bits** al portar. Se conservan en el material y en JSON. `0x10`, `0x800` y los bits superiores quedan sin interpretación confirmada aquí.

Para OBJ se conservan los ejes FCE, se usa `vt=(U,1−V)` y se escriben las esquinas en orden `0,2,1`. Es equivalente en orientación al [exportador de referencia](https://github.com/bfut/fcecodec/blob/main/src/fcelib/fcelib_io.h), que invierte Z y mantiene el orden de índices. No se debe invertir Z una segunda vez sin ajustar también el orden. No todas las normales del archivo coinciden con la normal geométrica de la cara: no las «corrijas» automáticamente.

## Qué representa cada archivo

La asignación documentada es serie/mejoras 1/2/3, con una textura y un fichero de prestaciones por variante; `dash` es el habitáculo. Véase la guía de quien mantiene [NFS Modder's Corner](https://nfsmodderscorner.blogspot.com/p/need-for-speed-high-stakes.html). Estos son los datos leídos que la respaldan, sin atribuir a los nombres una prueba de ejecución del juego:

| Archivo | Bytes | Vértices | Triángulos de todos los LOD | Partes | Dummies | Alta resolución según selección del script |
|---|---:|---:|---:|---:|---:|---:|
| `car.fce` | 297 532 | 1 295 | 1 588 | 21 | 14 | 1 054 |
| `car1.fce` | 290 624 | 1 268 | 1 542 | 21 | 14 | 1 008 |
| `car2.fce` | 299 368 | 1 312 | 1 580 | 21 | 14 | 1 022 |
| `car3.fce` | 299 368 | 1 312 | 1 580 | 21 | 14 | 1 022 |
| `dash.fce` | 292 164 | 1 279 | 1 542 | 18 | 1 | no corresponde a carrocería exterior |

**V:** cada `car*.fce` contiene `:HB`, `:MB`, `:LB`, `:TB`, ruedas H/M y tablas de daño propias. No son cuatro LOD separados ni cuatro estados de daño. Entre serie y mejora 1, `:OH` pasa de 46 vértices/74 triángulos a 17/28 y `:OD` de 39/58 a 41/58. Las ruedas delanteras también cambian ligeramente de posición. No atribuyo el cambio de cabeza a un casco sin comprobarlo visualmente en 3D.

En mejoras 2 y 3, `:HB` pasa de 384/508 a 400/522, `:MB` de 204/250 a 218/262 y `:LB` de 164/170 a 178/182. `car2.fce` y `car3.fce` **no son idénticos**: difieren 39 722 bytes. Sus cabeceras son iguales; posiciones, triángulos, posiciones dañadas, normales dañadas y animación coinciden. Difieren las normales iniciales, su copia y las áreas auxiliares 1–3. No prueba un cambio visible de silueta entre niveles 2 y 3.

**V:** `carp1.txt` cambia, por ejemplo, desaceleración máxima de frenado de 8,25 a 9,75 y multiplicador de agarre lateral de 3,0 a 3,2. `carp2.txt` eleva la velocidad máxima declarada de 64,373802 a 66,966667 m/s. `carp3.txt` la eleva a 67,966667, cambia demora de cambio de 7 a 5 ticks y límite de motor de 5 800 a 6 000 rpm. Esto respalda la lectura como mejoras (**I**), junto con la documentación (**D**).

### TGA y materiales

**V:** las cinco TGA tienen exactamente la misma estructura:

| Offset | Campo | Valor |
|---|---|---|
| `0` | longitud ID | 0 |
| `1` | mapa de color | 0, sin paleta |
| `2` | tipo | 2, true-color sin compresión |
| `12` | anchura u16 LE | 256 |
| `14` | altura u16 LE | 256 |
| `16` | profundidad | 32 bits |
| `17` | descriptor | `0x08`: 8 bits de atributo, origen inferior izquierdo |
| `18` | píxeles | 262 144 bytes, BGRA |
| `262162` | pie | 26 bytes; offsets de extensión/desarrollador nulos y firma `TRUEVISION-XFILE.\0` |

Se confirma `18 + 256×256×4 + 26 = 262188`. El significado del descriptor y el pie procede de la [especificación original Truevision TGA 2.0](https://www.ludorg.net/amnesia/TGA_File_Format_Spec.html).

**D:** parejas: `car.fce → car00.tga`, `car1.fce → car100.tga`, `car2.fce → car200.tga`, `car3.fce → car300.tga`, `dash.fce → dash00.tga`. No son cuatro páginas simultáneas del coche de serie: su campo de página es siempre 0.

**V:** las cuatro texturas exteriores son distintas. Frente a `car00.tga`, cambian 17 063 / 17 599 / 17 610 píxeles RGB en `car100/200/300.tga`, respectivamente. La inspección del atlas RGB muestra cambios de llantas y detalles; no son solo una recoloración plana. Los siete colores elegibles ya están representados en tablas de cada FCE.

**D:** EA reutiliza el alfa para controlar regiones recoloreables; la documentación da referencias cercanas a 224 para pintura primaria, 164 interior, 96 secundaria y 32 pelo. **V:** el MSLK real usa mucho **223**, no exactamente 224: `car00.tga` contiene 32 499 píxeles con alfa 223, 3 056 con 164, 702 con 32, 23 303 con 255 y 5 872 con 0, además de otros valores. No implemento una regla de recoloreado basada solo en igualdad con 224. Falta validar la lógica de rangos y mezcla del motor.

El script genera PNG RGB opaco para `map_Kd` y PNG RGBA con todos los bytes alfa preservados (`*_rgba.png`). No usa `map_d`, ni simula cromo, cristales, luces o pintura elegida. Es una exportación geométrica con textura base, no una reproducción exacta del material EA. Todos los datos originales quedan extraídos localmente.

### Habitáculo

**V:** `dash.fce` incluye nombres explícitos como `:F_DASH unlit`, `:F_LDASH lit`, `:F_RPM (0.0 to 0.55)`, `:F_MPH (0.0 to 0.7)` y `:F_W steering wheel`; la última parte tiene 321 vértices y 470 triángulos. También contiene puertas, asientos y espejos. Eso identifica el habitáculo para la vista interior, separado del interior simplificado `:OC` del coche exterior. Su único dummy se llama `Omni01`. No hay diferencias entre posiciones normales y dañadas en este FCE.

`dash.fsh` tiene firma `SHPI` y 66 616 bytes (**V**). Su uso como texturas de salpicadero del renderizador software está documentado (**D**, guía de Modder's Corner); aquí no se ha decodificado ni comprobado en ejecución.

## Dummies, ruedas y luces

**V:** estas son las 14 entradas de `car.fce`, en orden. Coordenadas globales redondeadas a seis decimales:

| Índice | Nombre literal | xyz |
|---:|---|---|
| 0 | `PWYN350` | −0.548683, −0.326039, +1.841380 |
| 1 | `HWYN650` | −0.548683, −0.027962, +1.745227 |
| 2 | `PWYN350` | +0.541015, −0.316434, +1.841380 |
| 3 | `HWYN650` | +0.541015, −0.018357, +1.745226 |
| 4 | `TRYN550` | −0.646293, +0.105694, −1.889334 |
| 5 | `RWYN550` | −0.565538, +0.078587, −1.919990 |
| 6 | `BRYN250` | −0.108020, +0.287660, −1.867200 |
| 7 | `BRYN350` | −0.000949, +0.287660, −1.867200 |
| 8 | `BRYN250` | +0.111873, +0.287660, −1.867200 |
| 9 | `RWYN550` | +0.564987, +0.078587, −1.919990 |
| 10 | `TRYN550` | +0.624249, +0.105694, −1.889334 |
| 11 | `:LICENSE_EURO` | +0.006626, +0.095562, −1.944038 |
| 12 | `:LICMED` | +0.006626, +0.095562, −1.972884 |
| 13 | `:LICLOW` | +0.006626, +0.095562, −1.972884 |

**D:** los nombres luminosos codifican tipo, color y comportamiento: H faro, T piloto trasero, B freno, R marcha atrás, P intermitente; W blanco, R rojo; Y indica rompible y N sin destello. Los dígitos especifican intensidad y tiempos. Es la interpretación de [TailsLab, sección FCE4](https://github.com/AndyGura/nfs-resources-converter/blob/main/resources/NFS4.md), no una observación de luces en juego. La ubicación delantera/trasera de estas entradas sí concuerda con sus coordenadas (**V**). Los nombres duplicados son válidos: no usar el nombre de dummy como clave única.

**V:** los pivotes de rueda de alta resolución son:

| Parte | Pivote xyz |
|---|---|
| `:HLFW` | −0.681968, −0.317077, +1.173971 |
| `:HRFW` | +0.673471, −0.317077, +1.173971 |
| `:HLRW` | −0.663472, −0.305893, −1.192945 |
| `:HRRW` | +0.651967, −0.305893, −1.192945 |

**I:** el giro de rodadura es alrededor de un eje paralelo a X; la dirección alrededor de Y. No hay un vector de eje explícito en la cabecera. Hay que casar estos pivotes con los de PS1, no sustituirlos por el centro del coche.

Atención: `:OLB`, llamado freno izquierdo por convención, tiene X≈+0,680; `:ORB` está en X≈−0,686. **V:** hay una discrepancia entre esos nombres convencionales y su lado geométrico en este MSLK. Para mapear piezas se deben comprobar coordenadas, no confiar solo en las letras.

## Partes y cajas del coche de serie

**D:** H/M/L/T identifican niveles alto/medio/bajo/mínimo; B carrocería; `:OT` techo; `:OLM/:ORM` espejos; `:OD` conductor/asiento/volante, `:OH` cabeza, `:OC` interior y `:ODL` iluminación de instrumentos. Las funciones se contrastaron con la lista de partes y pivotes del [tutorial de conversión](https://github.com/bfut/fcecodec/blob/main/scripts/doc_Obj2Fce.md).

**V:** la tabla que sigue usa cajas **globales**, sumando pivote. Sus 21 partes tienen geometría; se conserva el orden original. Coordenadas xyz en unidades FCE, redondeadas a seis decimales.

| Parte | Vértices | Triángulos | min xyz | max xyz |
|---|---:|---:|---|---|
| :HB | 384 | 508 | (-0.808876, -0.498794, -1.997813) | (0.808876, 0.590782, 1.967822) |
| :MB | 204 | 250 | (-0.804894, -0.493215, -1.997813) | (0.804894, 0.590782, 1.967822) |
| :LB | 164 | 170 | (-0.782389, -0.623166, -1.997813) | (0.786272, 0.602719, 1.967822) |
| :TB | 33 | 34 | (-0.816723, -0.623120, -1.998073) | (0.816723, 0.625000, 1.998073) |
| :OT | 81 | 62 | (-0.700146, 0.234308, -1.358724) | (0.700146, 0.606356, 0.142392) |
| :OLM | 8 | 11 | (-0.860000, 0.231917, 0.129217) | (-0.652308, 0.379738, 0.238902) |
| :OD | 39 | 58 | (-0.610904, -0.382900, -0.787405) | (-0.125103, 0.445210, 0.367088) |
| :OC | 124 | 156 | (-0.708409, -0.328395, -1.358724) | (0.708409, 0.510850, 0.894421) |
| :HLRW | 22 | 40 | (-0.777286, -0.623053, -1.526427) | (-0.549658, 0.011268, -0.859463) |
| :HRFW | 22 | 40 | (0.559657, -0.625000, 0.850203) | (0.787285, -0.009155, 1.497741) |
| :HLFW | 22 | 40 | (-0.795783, -0.625000, 0.850203) | (-0.568154, -0.009155, 1.497741) |
| :HRRW | 22 | 40 | (0.538153, -0.623053, -1.526427) | (0.765781, 0.011268, -0.859463) |
| :MRRW | 24 | 20 | (0.523348, -0.623583, -1.523778) | (0.790598, 0.038082, -0.862113) |
| :MRFW | 24 | 20 | (0.547986, -0.623583, 0.858164) | (0.815236, 0.038082, 1.519829) |
| :MLFW | 24 | 20 | (-0.808175, -0.623583, 0.858164) | (-0.540925, 0.038082, 1.519829) |
| :MLRW | 24 | 20 | (-0.792308, -0.623583, -1.523777) | (-0.516287, 0.038082, -0.862113) |
| :ODL | 12 | 10 | (-0.640158, -0.098917, 0.067006) | (0.098086, 0.352984, 0.231842) |
| :ORM | 8 | 11 | (0.652308, 0.231917, 0.129216) | (0.860000, 0.379737, 0.238901) |
| :OLB | 4 | 2 | (0.680128, -0.489579, 1.034250) | (0.680128, -0.129145, 1.427296) |
| :ORB | 4 | 2 | (-0.685977, -0.489579, 1.034251) | (-0.685977, -0.129145, 1.427296) |
| :OH | 46 | 74 | (-0.454604, 0.283063, -0.629091) | (-0.276523, 0.571157, -0.392195) |

## Comparación con PS1 y adaptación al port

| Medida | PS1, dato aportado | PC MSLK, leído |
|---|---:|---:|
| Partes totales | 57 | 21 en `car.fce` |
| Partes con geometría | 46 | 21, sumando alternativas LOD |
| Triángulos almacenados | 1 222 | 1 588, sumando alternativas LOD |
| Selección alta PC | no determinado | 1 054 en 14 partes |
| Textura exterior de serie | dos páginas 256×256 con paleta | una TGA 256×256 BGRA |

La selección alta excluye `:MB`, `:LB`, `:TB` y las cuatro ruedas M. Son `250+170+34+80=534` caras alternativas; `1588−534=1054`. Incluso esa selección alta **no equivale al número de caras visibles simultáneamente**: contiene techo opcional y caras de cristal entero y roto. En alta hay 31 caras con `0x400` (12 en `:HB`, 19 en `:OT`). El script no elimina ninguna porque eliminar estados sin criterio perdería información. `--lod high` es una selección práctica por nombres, no una réplica completa de las reglas del motor.

No hay correspondencia una a una: solo 14 grupos candidatos en alta frente a las 46 partes geométricas de PS1. Las cuatro ruedas sí están separadas, con sus propios centros. Hay espejos y frenos separados; los efectos de faros, freno y marcha atrás se sitúan mediante dummies, mientras parte de su aspecto está en la textura o en la carrocería. **No existen partes `:OL` ni grupos independientes llamados faro/freno trasero en el MSLK leído.** No se puede asignar una pieza de PS1 a cada dummy PC: un dummy es un punto sin triángulos.

Para casarlas hacen falta:

1. Una tabla de las 57 partes PS1 con ID, padre, función, pivote, ejes, escala y estados de visibilidad. Confirmar si sus 1 222 caras incluyen variantes o estados.
2. Elegir serie/mejora, LOD, techo y daño antes de presupuestar geometría. Mantener el habitáculo `dash` aparte salvo que se quiera implementar cámara interior.
3. Alinear ambos coches por centros de rueda y dimensiones. Establecer escala, transformación de ejes y orden de caras del motor del port; al reflejar un eje, ajustar normales y winding.
4. Mapear las ruedas H a los nodos animados PS1 y conservar los pivotes locales. Los frenos delanteros deben acompañar a la dirección sin girar necesariamente con el neumático.
5. Repartir o cortar triángulos de `:HB` para los nodos PS1 que requieran faros, luces u otras superficies independientes. Las UV, regiones de textura y coordenadas ayudan, pero no constituyen una segmentación semántica automática.
6. Trasladar dummies PC a los emisores de efectos y a las piezas PS1 correspondientes. Crear las asociaciones de encendido, frenado, intermitencia y daño en el motor.
7. Adaptar materiales, máscara de pintura, doble cara y cristales. Rehacer el atlas/UV si el cargador conserva el esquema PS1. Si admite texturas Dreamcast nuevas, decidir el formato de carga y tratamiento de color antes de convertir la imagen.
8. Conservar o convertir la tabla de deformación dañada. OBJ no representa los estados, pivotes ni lógica de animación por sí solo; usar `car_metadata.json`.

## Script, uso y salidas

Archivo completo: `extraer_fce.py`, Python **3.9 o posterior**, solo biblioteca estándar. No requiere Pillow, fcecodec, NumPy ni herramientas externas.

```powershell
python extraer_fce.py "E:\Juegos\pc\EA-NeedForSpeedHighStakes\DATA\CARS\MSLK\CAR.VIV" salida
```

Para una selección de alta resolución, escribirla en otra carpeta:

```powershell
python extraer_fce.py "E:\Juegos\pc\EA-NeedForSpeedHighStakes\DATA\CARS\MSLK\CAR.VIV" salida_alta --lod high
```

Extrae las 27 entradas, convierte las cinco TGA, exporta **`car.fce`** a `car.obj` y `car.mtl`, e imprime y guarda el informe de partes. Genera además `informe.json` con resumen de los cinco FCE y las imágenes, y `car_metadata.json` con posiciones originales/dañadas, normales, caras, UV, banderas, pivotes y dummies. Los grupos del MSLK conservan sus nombres literales. Para nombres con espacios, OBJ exige varios tokens de grupo; por ello el script los escapa en `g` y conserva el nombre original en `o`, comentario y JSON.

La salida por defecto incluye todos los LOD y los estados de cristal alternativos: aparecerán superficies superpuestas al abrirla. La opción de alta evita los LOD inferiores, pero conserva las caras de estados alternativos. Las opciones de selección son específicas de los nombres estándar de coches FCE4, no de los nombres de `dash.fce`.

Validaciones: firma y tamaños BIGF, directorio, rutas de extracción, duplicados y solapamientos; firma FCE4, límites de tablas, cobertura de partes, índices locales y floats finitos. Un formato distinto se rechaza explícitamente. TGA admite true-color no comprimido de 24/32 bits, con origen superior/inferior e izquierdo/derecho; otros tipos requieren ampliar el lector. La carpeta de salida debe ser separada de la del archivo fuente y los resultados con igual nombre se sobrescriben.

**Verificación realizada:** ejecución completa sobre tu VIV; lectura de los cinco FCE; sumas y rangos; extracción comparada byte por byte; recuentos/referencias OBJ; correspondencia de UV tras cambiar el orden de las esquinas; PNG contrastados con los píxeles TGA y pruebas de origen de imagen; rechazo de entradas corruptas y rutas fuera de la carpeta. No se comprobó el resultado en Dreamcast ni se simularon materiales EA.

## Dudas sin resolver

- Significado de las áreas auxiliares y campos desconocidos; no asumir que sean ceros o prescindibles al reconstruir FCE.
- Función de banderas altas y algunos bits bajos que las herramientas no documentan claramente.
- Algoritmo exacto de recoloreado EA, incluidos alfa 223 y valores intermedios, y combinación con ventanas/reflejos.
- Regla exacta de selección y orden de dibujo de LOD, techo y cristales en el ejecutable original.
- Motivo artístico del cambio de cabeza/conductor entre serie y mejoras, y efecto visual de las diferencias de normales de mejora 2/3.
- Razón de la aparente inversión de nombres de frenos `:OLB/:ORB` en este coche.
- Correspondencia de los 57 nodos PS1, unidades y convenciones del cargador Dreamcast; todavía no se inspeccionaron sus datos.
- `Omni01` del habitáculo y uso efectivo de `dash.fsh`, sin confirmar en el motor.

## Fuentes consultadas

- [fcecodec: proyecto y procedencia de las especificaciones](https://github.com/bfut/fcecodec).
- [fcelib_fcetypes.h: estructuras y convenciones](https://github.com/bfut/fcecodec/blob/main/src/fcelib/fcelib_fcetypes.h).
- [fcelib_io.h: lectura y exportación OBJ](https://github.com/bfut/fcecodec/blob/main/src/fcelib/fcelib_io.h).
- [Obj2Fce: nombres, pivotes, daño y animación](https://github.com/bfut/fcecodec/blob/main/scripts/doc_Obj2Fce.md).
- [TailsLab: especificación generada de NFS4](https://github.com/AndyGura/nfs-resources-converter/blob/main/resources/NFS4.md).
- [NFS Modder's Corner: archivos y mejoras de coches](https://nfsmodderscorner.blogspot.com/p/need-for-speed-high-stakes.html).
- [Truevision TGA: especificación original 2.0](https://www.ludorg.net/amnesia/TGA_File_Format_Spec.html).

Las cifras y tablas del MSLK proceden de tu copia local, no de modelos descargados. Los únicos entregables enlazados son este informe y el código; los modelos y texturas de verificación permanecen en la carpeta local temporal.
