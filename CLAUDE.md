# CLAUDE.md: NFS High Stakes → Dreamcast (nfshs-dc)

Port de *Need for Speed: High Stakes* de PS1 (NTSC-U, SLUS-00826) a Sega Dreamcast. La base es
[nfshs-rs](https://github.com/bsimovic93/nfshs-rs) (bsimovic93): una recompilación estática de
NFS4.EXE + FRONT.BIN a Rust, con un runtime mínimo de PS1 y un renderer wgpu nuevo.

El port **regenera la recompilación en C** para KallistiOS (sh-elf-gcc, SH-4) y **dibuja el 3D
de forma nativa con el PowerVR**.

Responsable: Jorge. Idioma de trabajo: **español**, tanto en la documentación como en los
mensajes. Los identificadores de código van en inglés.

Última actualización: 2026-10-27. Estado: **M0, M1a, M2, M3 y M4 cerrados (M1b, M2, M3 y
M4, en emulador); el siguiente es M5 (controles y 2 jugadores).** M1b cumple su criterio
funcional: el juego **arranca con la BIOS real** desde un CDI (`mkdisc.sh`, formato por
omisión), el front end corre a 69–78 fps y la **carrera simula a 98–113 fps**, `ps1rt`
pasa **227/227** en SH-4 y el código generado ocupa **3,43 MB** con `-Os` (sin overlays).
La prueba en una consola real (quemar el CDI en CD-R y verlo en 480i y 480p) es el
**M7**. **M2a tiene sus cinco pasos hechos**: el front end **se dibuja con el PVR en
Dreamcast a ~60 fps**, completo y legible; leerlo en una tele en 480i pasa a M7.
**M2b también está hecho en emulador**: la carrera se dibuja —pista, cielo y objetos del
escenario— a **60 fps**. **M2c cumple su criterio en emulador (2026-10-26)**: una carrera completa de un jugador,
de los menús a la meta, las estadísticas, la repetición y de vuelta a los menús, sale
**idéntica a la referencia en 40 000 frames** (RAM, 2D y vista). Coches, objetos, HUD,
efectos, showroom, pausa y noche se dibujan en Dreamcast a 30-43 fps; la lluvia y la
lluvia de noche se compararon además con el original en DuckStation.** **M3 cierra en emulador
(2026-10-26)**: las películas (intro, logo y demos) se ven y suenan a sus 15 fps, leídas de
los datos del juego, y el video wall de los menús es idéntico a la referencia en 40 000
frames, con los menús a ~40 fps.

El repo es un **fork** de nfshs-rs: `origin` = `svcoco/nfshs-dc`, `upstream` =
`bsimovic93/nfshs-rs`, commit base `27d903e`. `master` es el espejo limpio del upstream y
todo el trabajo del port va en la rama **`dc`**. El C vive en el árbol **`dc/`** (sección 8).

---

## 1. Cómo retomar (léeme primero)

0. **`docs/bitacora.md`** es el resumen vivo del proyecto: lo construido, las cifras
   medidas y las decisiones vigentes. Empieza por ahí y se actualiza al cerrar cada
   paso; el detalle largo está en `docs/hitos/`.
1. Lee las secciones 3 (decisiones) y 6 (M0) de este archivo. No reabras decisiones ya
   tomadas salvo que Jorge lo pida.
2. Comprueba que estás en la rama `dc` y verifica los hashes de `gamedata/` (sección 6).
3. Corre `python -I dc/tools/recomp/m0_stats.py`. Si los números no coinciden con la
   sección 6, para y avisa antes de seguir. (`codegen_metrics.py` da las cifras del
   dimensionado del backend C.)
4. El siguiente hito es **M5** (controles y 2 jugadores). M4 está cerrado en emulador (`docs/hitos/M4.md`). Mantén la lista de tareas y deja
   las cifras nuevas en `docs/hitos/`; cada hito tiene su archivo.

## 2. Qué es la recompilación estática (resumen)

Antes de compilar, `tools/recomp.py` traduce cada función MIPS a una función en el lenguaje
de destino, `f_XXXXXXXX(c)`. Esa función opera sobre un estado de CPU (`c.r[32]`, hi, lo,
cop0, registros del GTE) y sobre la RAM de PS1, que es un arreglo de 2 MB más 1 KB de
scratchpad.

El traductor trata cada caso así:
- **Slots de retardo:** se reordenan al traducir, con semántica exacta.
- **Llamadas directas (`jal`):** se convierten en una llamada directa en el lenguaje de
  destino.
- **Funciones de la lista HLE (`hle_funcs.txt`):** las sustituye el host (libgpu, libcd,
  libpad, libmcrd, SPU, archivos).
- **Hooks (`hook_funcs.txt`):** siempre se llaman a través de `Cpu::call`, para que el host
  pueda observarlas.
- **Saltos indirectos o código no cubierto:** pasan por `dispatch` y `dispatch_at` y, si no
  hay función recompilada, por un intérprete MIPS de respaldo (`interp.rs`).
- **Continuaciones (`c.cont`):** cubren las funciones que siguen de largo hasta la
  siguiente.

No es un emulador: no se emula la BIOS, la GPU, el CD ni el DMA. El juego corre en su
propio hilo, un paso de 60 Hz a la vez. El host graba lo que el juego decidió dibujar
(cámara, chunks de pista, coches, props, overlay 2D) y el renderer lo dibuja.

## 3. Decisiones tomadas

- **Lenguaje:** se regenera en **C** (`recomp.py --lang c`). Rust en Dreamcast solo existe
  de forma experimental (gccrs o rustc_codegen_gcc), así que no se usa.
- **Runtime:** `ps1rt` se porta a C. El host va en C sobre KOS.
- **Render:** el PVR renderiza de forma nativa (no se reproduce el OT con un GTE por
  software). `Render_RenderPlayerView` (0x800B4024) sigue siendo HLE: el juego decide qué
  se ve y el host lo dibuja desde los assets.
- **Limitaciones del PVR:** no tiene shaders ni blending sustractivo. El ×2 de color de PS1,
  la niebla y el haz nocturno de los faros se calculan por vértice en el SH-4.
  **Decisión de Jorge (2026-10-25): todo lo que el PVR no hace se resuelve con técnicas
  conocidas del propio PVR; no se escribe renderizado por software en el SH-4, ni siquiera
  una parte pequeña, porque rompe la arquitectura (todo el middleware de render está en el
  PVR).** Lo sustractivo (`B − F`) se hace **invirtiendo, sumando F e invirtiendo otra
  vez** (`B − F = 1 − ((1 − B) + F)`), tres mezclas del PVR en la lista translúcida: es el
  modo 2 de PS1 exacto, no una aproximación. **Aclaración de
  Jorge: no hay Dreamcast disponible por ahora, así que no se hace profiling en hardware
  real para decidir estos efectos; se eligen técnicas conocidas.** Esto no prohíbe medir en
  consola cuando la haya.
- **Assets:** se hornean en el PC con una herramienta de build que reutiliza `src/formats`
  (Rust) de nfshs-rs. Las texturas quedan twiddled y paletizadas cuando conviene (el PVR
  tiene 1024 entradas de paleta), y las mallas quedan listas para dibujar.
- **Audio (decisiones de Jorge, 2026-10-26, M4):** **el middleware de audio atrapa las
  peticiones de audio del driver de EA en su capa de hardware** (unas 15 funciones pequeñas
  del fondo del driver: tocar y callar voces, volumen, tono, dirección y ADSR, subir muestras,
  leer el estado) **y las procesa con el AICA**: cada voz de EA a un canal del AICA, y las
  muestras a PCM al subirlas. Toda la lógica de EA sigue corriendo (intérprete de parches,
  streams, curvas del motor). El SPU no se programa ni se emula: es el mismo principio que el
  render (el juego decide qué suena; el middleware lo hace sonar con el hardware). Medido antes de
  decidir (`docs/hitos/M4.md`): un SPU de PS1 fiel por software cuesta el 196 % de la CPU del
  SH-4. Para el test diferencial, esas funciones se sustituyen en los dos lados, como hasta
  ahora el SPU. (Sustituye a «se convierte antes de compilar».) Las voces de más de 65 534 muestras
  (la música en stream) también van a un canal del AICA, en bucle sobre un anillo que un hilo
  rellena; la CPU no mezcla nada (`docs/hitos/M4.md`, sección 9). La reverb del SPU la hace
  el **DSP del AICA** con un microprograma generado (sección 10); en Flycast el DSP viene
  apagado por omisión (`aica.DSPEnabled`).
- **Vídeo (en M2a):** 480p a 31 kHz (VGA) y 480i a 15 kHz. Se renderiza a 640×480 en
  ambos y el modo se elige con `vid_check_cable` (se puede forzar con `/cd/VIDEO.TXT`).
  **Hecho el 2026-10-23** (`dc/src/platform/dc/video.c`), comprobado con los dos cables.
  Matiz medido: el juego dibuja a **512×240**, no a 320×240, así que el «×2» vale para
  la vertical pero la horizontal es ×1,25. La regla es **llevar el área de pantalla de
  PS1 a 640×480** (detalle en `docs/hitos/M2a.md`).
- **FPS (decisión de Jorge, 2026-10-18): sin tope artificial.**
  - **Mínimo conservador: 30 fps sostenidos.** El original en PS1 tiene el tope en 30 y
    baja hasta 8 en los momentos malos, así que 30 sostenidos ya es una mejora sobre él.
  - **Ideal: 60 fps**, si la lógica del juego lo permite. Y hay evidencia medida de que lo
    permite: en la referencia corre a **un flip por campo (60 Hz)** en front end y en
    carrera, con el contador de simulación avanzando de forma constante, porque la física
    va atada al root counter de 128 Hz y no al frame. Llegar ahí sería un «NFS HS Plus».
  - **No se pone cap en el host de Dreamcast:** corre lo que el hardware dé. El techo son
    los 60 Hz del campo de vídeo, que es un límite de la pantalla, no nuestro.
  - Sigue sin haber **interpolación** ni frames inventados: los frames que se dibujen son
    pasos reales de la lógica del juego.
  - **Reloj real en Dreamcast (decisión de Jorge, 2026-10-27):** el reloj del juego sigue al
    tiempo real, como los temporizadores de la PS1: a 30 fps el juego avanza dos campos por
    frame y va a su velocidad (antes avanzaba uno por frame y a 28 fps iba a la mitad). Con
    un tope de 4 campos por frame: por debajo de 15 fps va más lento. El PC y el test
    diferencial siguen con el reloj virtual (`docs/hitos/M4.md`, sección 9).
  - No se mide el fps del original: es un hecho conocido y medirlo no aporta (ver
    `docs/hitos/M0.md`).
- **Datos:** el usuario siempre aporta su disco original. El repo no contiene datos de EA
  ni C generado. El pack de texturas nuevas es arte propio, va en un repo aparte y
  reemplaza texturas por nombre de 4 caracteres en PSH, manteniendo los UV.
- **Licencia:** nfshs-rs **no tiene LICENSE** (comprobado 2026-10-17), pero su repositorio
  es público y admite forks, y los Términos de Servicio de GitHub conceden por ello licencia
  para reproducir el contenido *en GitHub a través del fork*. Este repo es ese fork, así que
  su historia de git es la atribución al autor. **No habrá distribución fuera de GitHub**
  (ni imágenes GDI/CDI ni binarios en otros sitios), que es justo lo que esa cláusula no
  cubriría. Con eso, derivar código suyo dentro del fork —`ps1rt` en C incluido— queda
  cubierto y no hay nada pendiente.

## 4. Hitos

| Hito | Objetivo | Criterio de salida |
|---|---|---|
| M0 | Cifras de viabilidad | **Hecho.** Funciones, tamaño de código y RAM medidos (`docs/hitos/M0.md`). El «fps original (VSync)» quedó resuelto como mal planteado: el juego no se marca el ritmo con `VSync(N)` y la cadencia la impone el host; su lógica apunta a 60 Hz con un flip por campo, y el fps real del original es un hecho de rendimiento del hardware que necesitaría DuckStation |
| M1a | Backend C + ps1rt en C, en PC (x86) | **Hecho (2026-10-18).** Test diferencial contra la versión Rust: **120 frames idénticos** en el front end y **231** en la carrera (incluida la transición), comparando 512 hashes de página, los 32 registros, hi, lo, cop0, los 64 del GTE, `cont` y ticks. **Cinco bugs reales** encontrados, todos de integración host/juego. Pruebas unitarias 227/227. El manejador de `Render_RenderPlayerView` queda para M2b (`docs/hitos/M1a.md`) |
| M1b | Cross-compilar a SH-4 (KOS, `-m4-single`) + host mínimo (archivos, pad, timers, VSync, BIOS) | **Hecho en Flycast con BIOS real (2026-10-23).** Arranca desde CDI; front end a 69–78 fps y carrera simulando a **98–113 fps**; `.text` de 3,43 MB con `-Os`, ELF `text 3,8 MB / bss 4,85 MB`; 227/227 unitarias en SH-4. La prueba en consola es el M7 |
| M2a | Grabación en C + modos 480p/480i + 2D en PVR + espejo de VRAM y texturas | **Hecho en emulador (2026-10-23).** Front end navegable y legible, con los dos modos probados en Flycast; la lectura en una tele en 480i pasa a M7 (`docs/hitos/M2a.md`) |
| M2b | Pista y cielo en PVR (transformación en SH-4, recorte por plano cercano) | **Hecho en emulador (2026-10-23).** La carrera se dibuja a **60 fps** con ~1 150 triángulos de pista y 104 de cielo. La transformación va en C con `float`, **sin FTRV**: esa optimización queda para M8 y ahora hay con qué medirla (`docs/hitos/M2b.md`) |
| M2c | Coches, props y efectos | **Criterio cumplido en emulador (2026-10-26).** Carrera completa de un jugador idéntica a la referencia en 40 000 frames (`carrera_completa.txt`, con el piloto de pruebas de la referencia); showroom, pausa, policía y noche. 30-43 fps en Flycast (`docs/hitos/M2c.md`) |
| M3 | FMV: los vídeos del juego (STR/MDEC de PS1) | **Hecho en emulador (2026-10-26).** Las 5 películas se ven y suenan en Flycast a sus 15 fps (MDEC en SH-4, conversor YUV del PVR, audio XA por el AICA), leídas de los datos del juego; Start las salta. El video wall de los menús, idéntico a la referencia en 40 000 frames; menús a ~40 fps (`docs/hitos/M3.md`) |
| M4 | Middleware de audio: SPU → AICA | **Hecho en emulador (2026-10-27).** Música, motor y efectos en el front end y en carrera por el AICA: cada voz de EA a un canal, la música en stream en anillos que sigue al driver (0 bloques pisados en front end y carrera), la reverb en el DSP del AICA (dentro de ~0,5 dB de la de PS1) y el reloj real. Audio ~1,4 % de CPU (`docs/hitos/M4.md`) |
| M5 | Middleware de IO: controles (mando de Dreamcast, analógicos, gatillos, vibración) y 2 jugadores | Front end y carrera jugables con el mando de Dreamcast, también a pantalla partida con dos mandos |
| M6 | Middleware de IO: guardado y VMU (memory card de PS1 → VMU) | Guardar y cargar la partida en la VMU |
| M7 | Prueba en consola real: el CDI quemado en CD-R, vídeo en 480i (tele) y 480p (VGA). **Primera tarea: que el CDI quepa en un CD-R de 80 min** (725 MB con las películas de M3, el límite ronda 703; decisión de Jorge, 2026-10-26: no se optimiza antes) | Arranca en una Dreamcast real; front end legible y carrera jugable en 480i y 480p; fps y costes del PVR medidos en consola |
| M8 | Revisión, optimización (codegen, SH-4 a mano en hooks, FTRV, LOD) y pulido final | **30 fps sostenidos** en carrera como mínimo **en consola**, sin tope por arriba (60 es el objetivo ideal); el port pulido de punta a punta |
| M9 "Enhancements" | Mejoras del código y de los assets actuales (escalado de texturas horneado en el PC, mejoras de la referencia como opción) y packs de texturas externos. Más **objetivos descartables** (ver abajo) | Texturas de pista, coches y HUD escaladas dentro del presupuesto de VRAM, con respaldo a las originales; un pack de texturas distribuido aparte se carga por nombre |
| M10 "Polishing" | Pulido con medidas (decisión de Jorge, 2026-10-27). Primera tarea: **la herramienta de espectrogramas** de Jorge: grabar el audio del juego en DuckStation (el original) y en Dreamcast, alinear tramos y medir el % de desviación, con el SPU de la referencia como base exacta. También **la reverb en estéreo**: en el AICA cada canal tiene un solo envío al DSP, así que hoy entra mono; con tres buses (izquierda, derecha y centro, según el panorama de cada canal) sería estéreo (medido: el ~87 % de los sonidos con reverb van panoramizados, `docs/hitos/M4.md` sección 10). Y **subir los fps a dos jugadores** (decisión de Jorge, 2026-10-28): la pantalla partida va a ~18 fps en Flycast frente a ~21 de un jugador con tráfico (`docs/hitos/M5.md` sección 5) | El audio comparado con cifras contra el original, tramo a tramo |

Numeración vigente desde el **2026-10-26 (decisiones de Jorge)**. En los `docs/hitos/` de
antes de esa fecha: «M3» como optimización es el **M8** de ahora, y como audio el **M4**;
«M4» como escalado de texturas es el **M9**; la prueba en consola que M1b y M2a dejaron
pendiente es el **M7**. El pulido final va en **M8**.

**Objetivos descartables de M9 (decisión de Jorge, 2026-10-26).** M9 admite objetivos que se
**desechan** si la arquitectura o la plataforma no lo permiten, o si el rendimiento se resiente
considerablemente; se estudian, se miden y decide Jorge. Hoy son:
- **Salida en 720p.** Punto de partida medido (`docs/resolucion.md`): los modos de KOS van
  con la temporización de 480 líneas (858 relojes × 525 líneas) y 640 píxeles activos. Pero
  **hay homebrew que llega a 720p** (referencias de Jorge, sin investigar todavía):
  [Dreamcast: Video Output Notes](https://consolemods.org/wiki/Dreamcast:Video_Output_Notes#yellow)
  y los modos de vídeo de [DreamHAL](https://github.com/sega-dreamcast/dreamhal#supported-dreamcast-video-modes).
- **Widescreen** (16:9).
- **Resolución interna 3×.** Punto de partida medido (`docs/resolucion.md`): el port ya
  rasteriza el 3D a 640×480 (el «2×» de un emulador de PS1); por encima, el PVR solo
  supermuestrea 2× en horizontal (FSAA), no en vertical, y sus texturas llegan a 1 024
  téxeles.
- **Disco de arranque (boot disc).** Un disco que lleva solo el programa de Dreamcast:
  arranca, pide cambiar al **disco original de PS1** del jugador y lee los datos de él. Así
  el programa no se empaqueta nunca con el juego (hoy el CDI lleva los 552 MB del disco
  junto al programa). Precedente conocido, no medido: Bleem!cast. Sin investigar todavía;
  lo que habría que medir: que la unidad GD-ROM lea con KOS los sectores del CD de PS1
  (modo 2, XA) y dónde se hornean los assets, que hoy se hornean en el PC (§3) y con el
  disco original tendrían que hornearse en la consola o leerse sin hornear. Cambiar eso
  es decisión de Jorge.

## 5. Arquitectura objetivo

```
Disco del usuario ──► herramienta de build (PC)
                        ├─ recomp.py --lang c ──► gen/*.c (no se versiona)
                        ├─ bake: PSH/GRP/GEO/VIV ──► baked/ (PVR twiddled, mallas, audio en M4)
                        └─ mkdisc ──► 1ST_READ.BIN + IP.BIN ──► GDI/CDI
Dreamcast:
  gen/*.c (lógica del juego, sin cambios)
    └─ ps1rt (CPU, RAM de 2 MB, GTE, BIOS HLE, intérprete de respaldo)
         └─ host (reemplazos de libgpu/libcd/libpad/libmcrd/SPU + grabación del frame)
              └─ render PVR (pista, coches, props, overlay 2D) + platform/dc (KOS)
```

La interfaz entre la grabación y el renderer debe ser estable (equivalente a `FrameInfo`),
para poder comparar contra el renderer wgpu de referencia en PC.

## 6. Datos verificados de M0 (estático)

Archivos del disco de Jorge:

| Archivo | Bytes | MD5 | SHA-1 |
|---|---|---|---|
| NFS4.EXE (PS-X EXE) | 1 245 184 | `befb56d758c0ed24921ac89dd81bb1e8` | `a5e2f0c5c4a630dc627509ed541907e9cd97c3a9` |
| FRONT.BIN (imagen cruda) | 281 048 | `eb5dc78e6da024a4835496293bafc412` | `db9f8467d12000b49197a37cf6df7df68e0eff52` |

- **NFS4.EXE:** texto en 0x80010000–0x8013F800, entry 0x800E4C38, sp 0x801FFFF0. Los
  primeros 277 KB (0x80010000–0x80055560) están en **ceros**: ahí se carga FRONT.BIN.
- **FRONT.BIN:** ocupa 0x80010000–0x800549D8 y cabe en ese hueco con ≈2,9 KB de sobra.
  Empieza con datos ("SimpleMem", "FE Data Stream"), y el 91 % es código.
- **Compatibilidad:** las 119 direcciones de `hle_funcs.txt` y las 53 de `hook_funcs.txt`
  coinciden con inicios de función. Es la misma versión que usa nfshs-rs.

| | FRONT.BIN | NFS4.EXE | Total |
|---|---|---|---|
| Funciones recompiladas | 994 | 2 680 | **3 674** |
| Instrucciones MIPS únicas | 63 757 (249 KB) | 181 761 (710 KB) | **245 518 (959 KB)** |
| Mediana / máx. de función | 30 / 3 207 | 34 / 1 957 | 33 / 3 207 (0x8002E504) |
| `jal` | 3 827 | 7 746 | 11 573 |
| `jr` (no `ra`) / `jalr` | 18 / 160 | 81 / 426 | 99 / 586 |
| Loads + stores | 31,6 % | 32,3 % | 32,1 % |
| Instrucciones GTE (cop2) | 0 | 809 | 809 |
| Llamadas cruzadas | 931 hacia NFS4.EXE | 22 hacia FRONT.BIN | todas resueltas |

- El backend Rust actual genera 28,8 MB de fuente (≈514 000 líneas, 48 partes) en unos 2 s.
- **Estimación (sin validar):** el código en SH-4 ocuparía entre 2,9 y 5,9 MB con -O2
  (12–24 bytes por instrucción MIPS, máscara de memoria incluida). A eso se suman 2 MB de
  RAM de PS1 y 1 MB de espejo de VRAM, de un total de 16 MB. El punto a vigilar es la
  I-cache de 8 KB.
  - **Medido en x86-64 (2026-10-18, gcc 16.2):** `.text` = **7,59 MB con `-O2` y 3,93 MB
    con `-Os`** (−48 %; `-O1` sale peor que `-O2`), o sea 32,4 y 16,8 bytes por
    instrucción MIPS.
  - **Medido en SH-4 (2026-10-19, sh-elf-gcc 15.2.0, `-ml -m4-single`):** `.text` =
    **3 595 380 bytes (3,43 MB) con `-Os`** y 8 233 000 (7,85 MB) con `-O2`; 14,6 y 33,5
    bytes por instrucción MIPS. Compila en 38 s y 96 s con 8 hilos, sin avisos. La
    estimación de 2,9–5,9 MB acertó, y a `-Os` el SH-4 sale un 8,5 % **más compacto** que
    x86-64. **No hacen falta overlays** (detalle en `docs/hitos/M1b.md`).
- **M0 cerrado (2026-10-18).** Las dos cifras que faltaban están en `docs/hitos/M0.md`:
  - **RAM:** en carrera el juego tiene en uso ~**1,84 MB de los 2 MB** (1 214 KB de texto,
    616 KB de heap y pila, 12 KB de BSS), y escribe 265 de 512 páginas de 4 KB. La **pila
    usa solo 12 KB**, dato útil para M1b.
  - **Framerate:** la pregunta estaba mal planteada. El juego **no** se marca el ritmo con
    `VSync(N)` (0 call sites con argumento 0) y la cadencia la impone el host. Lo medido:
    su lógica apunta a **60 Hz, un flip por campo de vídeo**. El fps real del original es
    un hecho de rendimiento del hardware y necesitaría DuckStation; la pregunta que importa
    para el port es la equivalente en Dreamcast, y se mide en M1b, M7 (consola) y M8.

### Trampas conocidas del recompilador

- `recomp.py` lee `../gamedata/NFS4.EXE` y `../gamedata/FRONT.BIN` relativos a `tools/`,
  así que hay que ejecutarlo desde `tools/`. El directorio `re/` es opcional (solo aporta
  nombres de símbolos).
- **Sin el FRONT.BIN real, las cifras salen mal.** Con un FRONT.BIN vacío, el recompilador
  crea 34 funciones espurias en el hueco de ceros, y `func_end` las extiende hasta su tope
  de 0x8000 bytes (8 192 instrucciones de ceros).
- Al sumar `func_end - start` por función se cuenta de más, porque las extensiones se
  solapan (colas compartidas, tablas de salto extendidas hasta el siguiente inicio).
  Cuenta siempre las **instrucciones únicas**.
- `hle_funcs.txt` tiene 119 direcciones únicas, no tantas como líneas.
- En `ps1rt/src/lib.rs`, el comentario de `gte_regs` dice que los comandos del GTE no se
  ejecutan, pero `Cpu::gte` sí los ejecuta (`gte_command`). El GTE en C es necesario.

## 7. Repo base: archivos clave de nfshs-rs

| Ruta | Qué es |
|---|---|
| `tools/recomp.py` (≈480 líneas) | Recompilador. Funciones clave: `all_starts`, `collect`, `func_end`, `calls_of`, `gen_function`, `call_expr`, `collect_entry_points`. Escribe `part_XX.rs` y `lib.rs` con `RANGES`, `dispatch`, `dispatch_at` y la lista HLE |
| `tools/hle_funcs.txt`, `tools/hook_funcs.txt` | Listas de HLE y de hooks; se reutilizan tal cual |
| `crates/ps1rt/src/lib.rs` (512) | `Cpu`, memoria, `call` (vectores BIOS A0/B0/C0, luego el host, luego código recompilado), `cont`, timers e IRQ |
| `crates/ps1rt/src/interp.rs` (260) | Intérprete MIPS de respaldo |
| `crates/ps1rt/src/gte.rs` (477) | GTE completo según psx-spx |
| `crates/ps1rt/src/bios.rs` (230) | BIOS HLE |
| `src/ps1/host/*` | Reemplazos de librerías y grabación del frame (`frame.rs`, `views.rs`, `overlay.rs`, `gpu.rs`, `files.rs`, `system.rs`, `game.rs`) |
| `src/ps1/race.rs`, `src/game/original.rs` | Hilo del juego y paso de 60 Hz |
| `src/ps1/spu.rs`, `src/ps1/memcard.rs` | Modelo del SPU y memory card de 128 KB |
| `src/render/mod.rs` + `shader.wgsl` | Renderer de referencia: atlas, reverse-Z, ×2, color key, STP, niebla/depth cue, haz nocturno, pases sustractivos |
| `src/formats/*`, `docs/FORMATS.md` | Formatos PSH/GRP/GEO/VIV y otros |

Ejecutar la referencia en PC: `cargo build --release`, después
`./target/release/nfshs extract <imagen.bin> gamedata`, después
`cd tools && python recomp.py ../crates/nfs4rc/src --all` y otra vez `cargo build --release`.

## 8. Entorno y estructura del repo

El repo **no es nuevo**: es el fork de nfshs-rs. El árbol Rust se deja intacto (sigue
siendo la referencia ejecutable y el oráculo del test diferencial) y el port en C vive en
`dc/`, para que `cargo` siga funcionando y los merges desde `upstream` no choquen.

```
nfshs-dc/                        # fork; rama dc
├── CLAUDE.md
├── Cargo.toml, Cargo.lock, README.md, TODO.txt   # del upstream
├── crates/ps1rt/, crates/nfs4rc/ # referencia Rust (nfs4rc/src/ es generado, ignorado)
├── src/, tools/, bugs/           # del upstream: host, renderer, formats, recomp.py
├── docs/
│   ├── hitos/M0.md               # cifras medidas
│   └── arquitectura/             # vistas por capas, As-Is y To-Be separados
├── dc/                           # EL PORT EN C
│   ├── Makefile                  # targets: m0, metrics, gen, pc, dc, bake, disc, diff
│   ├── src/ps1rt/                # PORTABLE: cpu, mem, gte, bios, interp
│   ├── src/host/                 # PORTABLE: HLE de librerías, SPU stub, grabación
│   ├── src/render/               # PORTABLE: pista, coches, props; visibilidad
│   ├── src/platform/dc/          # KOS: pvr, vídeo, pad, VMU, AICA, cd, timers
│   ├── src/platform/pc/          # x86: harness para M1a
│   ├── tools/recomp/             # m0_stats.py, codegen_metrics.py, backend C
│   ├── tools/bake/, tools/mkdisc/
│   ├── test/diff/, test/unit/
│   └── gen/  build/  baked/      # en .gitignore
└── gamedata/                     # en .gitignore; lo aporta Jorge
```

- `dc/src/ps1rt/`, `dc/src/host/` y `dc/src/render/` **no incluyen cabeceras de KOS**. Así
  M1a corre en x86 y M1b solo cambia `dc/src/platform/`.
- Archivos del juego: `gamedata/NFS4.EXE` y `gamedata/FRONT.BIN` (verifica los hashes de la
  sección 6). Jorge los aporta y **nunca se versionan**. Como `gamedata/` es hermano de
  `tools/`, el `../gamedata` que usa `recomp.py` resuelve sin tocar nada.
- Toolchain de DC: KallistiOS con sh-elf-gcc. Para probar: Flycast y hardware real (el
  medio de carga, SD/ODE o GDI, está por decidir).
- Disco: todo en C: (49 GB libres el 2026-10-17). El externo `D:\dreamcast-dev` queda como
  copia de seguridad.

## 9. M1a (hecho) y próximo trabajo: M1b

**Lo que sigue describe M1a, que está cerrado**, porque documenta lo que se construyó y
cómo. El trabajo actual es **M1b** y vive en `docs/hitos/M1b.md`; el resumen está al final
de esta sección.

### M1a: backend C + ps1rt en C, en x86

1. **Backend C en `recomp.py`** (opción `--lang c`, sin romper la salida Rust):
   - Una función C por función MIPS: `void f_XXXXXXXX(Cpu *c)`. Los bloques básicos
     pasan a etiquetas con `goto`; los `jr reg` locales (tablas de salto), a un `switch`
     sobre la dirección.
   - Mismas semánticas que la versión Rust: slots de retardo, `call_expr` (llamada directa
     si la función es conocida y no es hook; si no, `ps1_call(c, addr)`), continuaciones
     con `c->cont`, `collect_entry_points`.
   - Acceso a memoria con macros o funciones `inline` `r8/r16/r32/w8/w16/w32`: ruta
     rápida con máscara `& 0x1FFFFF` sobre RAM plana; ruta lenta solo para scratchpad
     (0x1F800000) e IO.
   - Emitir `dispatch.c` con la tabla dirección→función (ordenada, búsqueda binaria) y
     `RANGES` para `dispatch_at`.
   - Partir en N archivos (`part_XX.c`), como en Rust, para no castigar al compilador.
2. **ps1rt en C** (`src/ps1rt/`), traducido a partir de lib.rs, interp.rs, gte.rs y
   bios.rs, **después de confirmar la licencia con el autor**. Si no hay permiso,
   reescribir desde psx-spx usando nfshs-rs solo como oráculo de pruebas.
3. **Host mínimo en x86** (`platform/pc`): archivos desde `gamedata/`, VSync y timers,
   pad con entrada guionizada, GPU y SPU como stubs que aceptan escrituras.
4. **Test diferencial** (`test/diff/`): ejecutar la versión Rust y la C con la misma
   secuencia de entrada y comparar la RAM de 2 MB (o un hash por región) en cada paso de
   60 Hz. El primer frame que diverja indica la función a revisar.
5. Registrar en `docs/hitos/M1a.md` el tamaño de `gen/*.c`, el tiempo de compilación y el
   tamaño del binario x86.

**Criterio de salida:** front end y una carrera de prueba, N frames idénticos en RAM entre
C y Rust.

**Estado: cerrado el 2026-10-18.** Cumple el criterio con **120 frames idénticos** en el
front end y **231 en la carrera** (incluida la transición desde los menús), comparando 512
hashes de página de 4 KB, los 32 registros, hi, lo, cop0, los 64 del GTE, `cont` y ticks,
con los dos lados llegando al mismo frame en la misma llamada. Pruebas unitarias de
`ps1rt`: **227/227**. Detalle, los cinco bugs y las cifras en `docs/hitos/M1a.md`.

El frame 232 —el primer frame de carrera— diverge **solo en los registros del GTE**, por el
manejador de `Render_RenderPlayerView` (`0x800B4024`), que no está portado: 183 líneas y
**18 de las 33 llamadas anidadas** de la capa de observación de la referencia. **Decisión
de Jorge: se corta ahí**, porque esas 18 llamadas son la grabación de geometría que M2b y
M2c escriben de todas formas con el PVR delante, y portarlas solo para el test sería
andamio. Los cinco bugs que encontró el test estaban todos en la integración host/juego, y
esa clase ya está barrida por los 231 frames.

Deuda anotada, no olvidada (está desarrollada en `docs/hitos/M1a.md`): el manejador de
`0x800B4024` (va a M2b), los **cuatro ayudantes del HUD** (resueltos en M2c, la pausa), `CAR_CONE_WIDTH = 2` (mejora de
la referencia que escribe en RAM; si se porta, detrás de una opción apagada por omisión,
como `--unlock`) y la **pila de diagnóstico** del port clavada en 0.

**Para comparar, se neutralizan en los dos lados** las conveniencias del host de la
referencia, no solo en el port: el SPU (`src/ps1/diffspu.rs`) y las películas
(`Movie_Play`). Los `VIDEO_*` se neutralizaban hasta M3; desde entonces corren en los dos lados
y se comparan (`docs/hitos/M3.md`).
**`unlock_everything` sí se porta** (`--unlock` en el port, que es el comportamiento por
omisión de la referencia): parecía cosmético, pero marcar pistas y coches como disponibles
**da acceso a carreras**, y sin ello los dos lados no son comparables. No marca la
propiedad del coche, así que el guion entra por *test drive*. Si el Dreamcast lo ofrece
como opción al jugador, es una decisión de producto de M9.

### Próximo trabajo: M1b (cross-compilar a SH-4)

Detalle en `docs/hitos/M1b.md`. Tres cosas que conviene saber antes de empezar:

1. **El toolchain ya está (2026-10-19).** dc-chain dentro del MSYS2 que ya existía:
   `sh-elf-gcc` 15.2.0, binutils 2.45.1, newlib 4.6.0, KOS `8e704086` (2026-10-16),
   `-m4-single`. Verificado enlazando un `hello.c` con `<kos.h>` a un ELF `sh4a` real.
   El toolchain del **AICA (ARM7) se dejó sin instalar a propósito**: su gcc 8.5.0 no
   compila en hosts Windows modernos y **no hace falta**, porque KOS trae
   `stream.drv.prebuilt` y lo usa solo cuando no encuentra compilador ARM.
2. **El tamaño del código ya está medido (2026-10-19) y el riesgo está cerrado.** En
   SH-4, la suma de `.text` de las 71 unidades es **3 595 380 bytes (3,43 MB) con `-Os`**
   y 8 233 000 (7,85 MB) con `-O2`; 14,6 bytes por instrucción MIPS, dentro de la
   estimación de M0 (2,9–5,9 MB) y un 8,5 % **por debajo** de x86-64 a `-Os`. Con 2 MB de
   RAM de PS1 y 1 MB de espejo de VRAM van 6,43 MB de 16: **sin overlays**. Elegir `-Os`
   o `-O2` es cuestión de velocidad (I-cache de 8 KB), no de espacio, y se decide
   midiendo con el juego corriendo.
3. **La carrera necesitó dos arreglos (2026-10-23), y el segundo era el de verdad.**
   `Render_RenderPlayerView` era un contador vacío y le faltaba su parte de **lógica**
   (cámara, trozos de pista, y el HUD: velocidad, marcha, tiempos, sentido contrario);
   pero aun con eso seguía congelada, porque `host_frame_top` **no avanzaba el reloj al
   siguiente campo de vídeo**. En el front end no se notaba —allí la frontera es el
   flip, que sí mueve el tiempo—, pero en carrera la frontera es `systemtask` y sin ese
   avance el root counter de 128 Hz no late y la física no corre. Detalle en
   `docs/hitos/M1b.md`.
4. **La auditoría del código portable contra los peligros del SH-4 ya está hecha** y
   despeja dos riesgos con datos: **0 accesos desalineados** y **0 en el borde de la RAM**
   en 900 frames de carrera, sin casts puntero↔entero y sin `double` salvo un `printf`.
   Lo de «abstraer `FILE`/`fopen`» que decía aquí **era falso** y queda retirado: KOS
   implementa los syscalls de newlib sobre su VFS (`/cd`, `/rd`, `/pc`, `/vmu`), así que
   `fopen` funciona. Lo que sí se hizo (2026-10-19) fue hornear `hle_funcs.txt` a
   `gen/hle_table.c` y dejar el volcado y los guiones del test diferencial detrás de
   `PS1_DIAG`. **El árbol portable compila entero para SH-4 sin un solo aviso.** Queda
   presente que el **intérprete de respaldo no es código muerto**: 21 378 entradas en 900
   frames.

## 10. Reglas para Claude Code en este proyecto

- **Mide, no supongas.** Toda cifra que vaya a la documentación sale de ejecutar algo, y
  las estimaciones se marcan como tales. Si una cifra parece redonda o rara (como 8 192),
  investígala antes de reportarla.
- No subas al repo `gamedata/`, `gen/`, `baked/` ni nada derivado del disco.
- Derivar código de nfshs-rs dentro de este fork está cubierto (ver §3, Licencia). Lo que
  no se hace es **publicar nada derivado fuera de GitHub**.
- No cambies las decisiones de la sección 3 por tu cuenta. Proponlas a Jorge.
- Escribe la documentación en español, de forma concisa. En los diagramas de
  arquitectura, Jorge prefiere vistas por capas y separar el As-Is del To-Be.
- Al cerrar un hito, actualiza la tabla de la sección 4, la sección 6 o 9 y
  `docs/estrategia.md`.

## 11. Riesgos y decisiones abiertas

- **Riesgos:**
  - **CPU del SH-4:** tiene que correr la lógica recompilada más la transformación de la
    pista.
  - **I-cache:** con 3–6 MB de código, los fallos de caché pesarán.
  - **VRAM del PVR:** hay riesgo de thrash, porque PS1 sube texturas y CLUTs de forma
    dinámica.
  - **Audio:** hay que mapear las subidas DMA del driver EA a las muestras convertidas.
- **Abiertas:**
  - **M9:** factor de escala de las texturas (2× como base), método y formato.
  - **Objetivos descartables de M9:** salida en 720p, resolución interna 3×, widescreen y
    disco de arranque para usar el disco original de PS1 (ver §4).
    *(Todo tiene hito desde el 2026-10-26: FMV es el M3, los dos jugadores van con los
    controles en M5, el pulido final en M8 y los packs de texturas en M9.)*
  - *(cerrada 2026-10-23: el arranque con BIOS real funciona desde un **CDI** con
    disposición MIL-CD, que genera `dc/tools/mkdisc/mkdisc.sh` por omisión. El GDI solo
    arrancaba con REIOS.)*
  - **Pruebas en hardware:** el CDI es grabable en CD-R, pero nadie lo ha quemado ni
    probado en una consola. Es el M7.
  - **Parche local a KallistiOS:** `fs_iso9660.c` solo lee el área de baja densidad y el
    port necesita la de alta (552 MB de datos no caben en los 92 MB de la baja). El
    parche está en `dc/tools/kos-patches/`; habría que proponerlo aguas arriba.
  - **Driver ARM del AICA de KOS (M3, 2026-10-26):** con el de nuestro KOS (`8e704086`) no
    suena nada en Flycast 2.6: el timer del ARM no arranca desde el commit `8690c6dd`
    (2026-06-10). El port carga el driver de justo antes (`dc/src/platform/dc/aica.c`, sacado
    del historial de KOS por el Makefile). Falta saber si es fallo de Flycast o de KOS y si
    en consola hace falta (M7); habría que avisar aguas arriba.
  - *(cerrada 2026-10-18: el objetivo de fps es 30 sostenidos, ver §3. No se mide el
    fps del original.)*
  - **Autor de nfshs-rs:** contacto sobre la licencia y una posible colaboración.

---

## Apéndice A: `tools/recomp/m0_stats.py`

Reproduce las cifras de la sección 6. Ejecútalo desde el directorio de `recomp.py`, con
`../gamedata/NFS4.EXE` y `../gamedata/FRONT.BIN` presentes. Salida esperada: `TOTAL: 3674
funciones, 245518 instr (959 KB)` y `HLE ... 119/119; hooks: 53/53`.

```python
"""M0: cifras de la imagen combinada NFS4.EXE + FRONT.BIN (ejecutar desde tools/)."""
import sys
sys.argv = ['m0_stats']
import recomp

e = recomp.e
OV0, OV1 = 0x80010000, 0x80010000 + len(recomp._front)
hle = {int(l.split()[0], 16) for l in open('hle_funcs.txt') if l.strip() and not l.startswith('#')}
recomp.HLE_SET |= hle
funcs = recomp.collect(recomp.all_starts())

# regiones de ceros >= 1 KB (hueco del overlay si falta FRONT.BIN, BSS, relleno)
runs, a = [], e.base
while a < e.end:
    if e.word(a) == 0:
        b = a
        while b < e.end and e.word(b) == 0:
            b += 4
        if (b - a) // 4 >= 256:
            runs.append((a, b))
        a = b
    else:
        a += 4
in_zero = lambda p: any(x <= p < y for x, y in runs)
real = [f for f in funcs if not in_zero(f)]
print(f'recompilador: {len(funcs)} funciones; espurias en ceros: {len(funcs) - len(real)}')


def stats(fs, name):
    cov = set()
    for f in fs:
        cov.update(p for p in range(f, recomp.func_end(f), 4) if not in_zero(p))
    c = dict(jal=0, jr=0, jalr=0, ld=0, st=0, cop2=0)
    for pc in cov:
        w = e.word(pc); op = w >> 26
        if op == 0:
            c['jr'] += (w & 63) == 8 and ((w >> 21) & 31) != 31
            c['jalr'] += (w & 63) == 9
        elif op == 3: c['jal'] += 1
        elif op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x32): c['ld'] += 1
        elif op in (0x28, 0x29, 0x2a, 0x2b, 0x2e, 0x3a): c['st'] += 1
        elif op == 0x12: c['cop2'] += 1
    sz = sorted((recomp.func_end(f) - f) // 4 for f in fs) or [0]
    print(f'{name}: {len(fs)} funciones, {len(cov)} instr ({len(cov) * 4 / 1024:.0f} KB), '
          f'mediana {sz[len(sz) // 2]}, max {sz[-1]}, {c}')


stats([f for f in real if OV0 <= f < OV1], 'FRONT.BIN')
stats([f for f in real if f >= OV1], 'NFS4.EXE')
stats(real, 'TOTAL')
starts = set(recomp.all_starts())
hooks = [int(l.split()[0], 16) for l in open('hook_funcs.txt') if l.strip() and not l.startswith('#')]
print(f'HLE que son inicio de función: {len(hle & starts)}/{len(hle)}; '
      f'hooks: {sum(h in starts for h in hooks)}/{len(hooks)}')
```
