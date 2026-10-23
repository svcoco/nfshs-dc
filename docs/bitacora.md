# Bitácora de desarrollo — nfshs-dc

Registro del trabajo **efectivo**: lo que está construido, las cifras medidas y las
decisiones vigentes. Solo hechos comprobados; los caminos que se probaron y se
descartaron no se guardan aquí.

Se actualiza al cerrar cada paso. El detalle largo vive en `docs/hitos/`; esto es el
resumen que permite retomar el proyecto sin releerlo todo.

**Última entrada: 2026-10-23.**

---

## Estado de un vistazo

| Hito | Estado |
|---|---|
| **M0** Viabilidad | Cerrado (2026-10-18) |
| **M1a** Backend C + `ps1rt` en C, en x86 | Cerrado (2026-10-18) |
| **M1b** Cross-compilar a SH-4 y arrancar | Cerrado en emulador (2026-10-23); falta probar en consola |
| **M2a** El front end en pantalla | Pasos 1 y 2 de 5 |
| M2b, M2c, M3, M4, M5 | Sin empezar |

**Lo que hay hoy:** un CDI de 558 MB que arranca en Dreamcast con la BIOS real, lee sus
datos del disco y ejecuta la lógica del juego —front end y carrera— a 98–113 fps. **No
dibuja nada todavía**: muestra contadores en pantalla. La imagen empieza en M2a.

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

## M2a — El front end en pantalla (en curso)

Primer hito **con imagen**. Criterio: front end navegable y legible en 480p y 480i.

| # | Paso | Estado |
|---|---|---|
| 1 | Grabación del dibujo 2D en C | **Hecho**, validado contra la referencia |
| 2 | Modos de vídeo 480p/480i | **Hecho**, probado con los dos cables |
| 3 | VRAM y paletas → texturas del PVR | Siguiente |
| 4 | Dibujo 2D en el PVR | |
| 5 | Ayudantes del HUD | |

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

---

## Abierto

- **Probar en una consola real.** El CDI es grabable y arranca con la BIOS real en
  emulador, pero nadie lo ha quemado. Hasta entonces los fps son de Flycast.
- **Caché de texturas del PVR** (paso 3 de M2a): no puede cachear páginas enteras.
- **M4:** factor de escala de las texturas, método y formato.
- **Alcance:** si la primera versión incluye 2P y FMV.
- **Parche local a KallistiOS:** habría que proponerlo aguas arriba.
- **Autor de nfshs-rs:** contacto sobre licencia y posible colaboración.
