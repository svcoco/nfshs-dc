# M1b: cross-compilar a SH-4 (KallistiOS) y arrancar en Dreamcast

**Objetivo:** que el C generado + `ps1rt` + el host minimo compilen con `sh-elf-gcc` y
corran en Dreamcast (Flycast y hardware), con el front end y la logica de carrera
avanzando sin cuelgues, contadores y estado del coche en pantalla, y el **tamano del ELF y
la RAM medidos**.

**Entrada:** M1a cerrado el 2026-10-18 (ver `M1a.md`). El front end es identico a la
referencia durante 120 frames y la carrera durante 231, asi que de aqui en adelante
**cualquier divergencia nueva es del cambio de arquitectura**, no de la recompilacion. Eso
es exactamente el valor que tiene haber hecho M1a primero.

**Estado: no empezado. Bloqueado por el toolchain** (no hay KallistiOS ni `sh-elf-gcc` en
la maquina; comprobado el 2026-10-18).

---

## 1. El riesgo numero uno, ahora con una referencia medida

CLAUDE.md §11 pone la CPU y la I-cache como riesgos principales, pero el que decide si el
port es viable es el **tamano del codigo**: en Dreamcast el `1ST_READ.BIN` se carga
**entero en RAM**, asi que cada byte de codigo es un byte menos de los 16 MB.

Medido en x86-64 (mismo arbol generado, gcc 16.2 UCRT64, 71 unidades de traduccion):

| | `.text` total | bytes por instruccion MIPS | tiempo de compilacion (8 hilos) |
|---|---|---|---|
| `-O2` | 7 954 452 (**7,59 MB**) | 32,4 | — |
| `-Os` | 4 122 768 (**3,93 MB**) | 16,8 | **27 s** |

Sobre las 245 518 instrucciones MIPS unicas de M0. **`-Os` deja el codigo en la mitad**
(-48 %), y en la parte mas grande (`part_21.c`, 12 337 lineas) la proporcion es la misma:
123 908 -> 66 484 bytes. `-O1` **no** ayuda (126 816, peor que `-O2`).

Esto no es el numero de SH-4, que es lo que hay que medir, pero sitia el problema:

- La estimacion de M0 era **2,9-5,9 MB** para SH-4 (12-24 bytes por instruccion MIPS). La
  medida de x86-64 a `-Os` (16,8 B/instr) cae **dentro** de ese rango, lo que da algo de
  confianza a la estimacion.
- SH-4 tiene instrucciones de **2 bytes** de ancho fijo, pero necesita mas instrucciones
  por operacion (menos registros utiles, direccionamiento mas pobre, constantes por tabla
  de literales). Puede salir por debajo o por encima; **no se puede saber sin compilar**.
- Presupuesto: 16 MB - 2 MB de RAM de PS1 - 1 MB de espejo de VRAM - KOS, pila, heap y
  assets en vuelo. Quedan del orden de **12 MB para codigo**. A `-Os` sobra; a `-O2` entra
  pero sin holgura para M2.

### La medicion real de SH-4 (2026-10-19): **riesgo cerrado**

Mismo arbol generado, mismos 71 ficheros, `sh-elf-gcc 15.2.0 -ml -m4-single`, suma de
`.text` de los 71 objetos:

| | `.text` SH-4 | B/instr MIPS | x86-64 | SH-4 vs x86-64 |
|---|---|---|---|---|
| `-Os` | **3 595 380 (3,43 MB)** | **14,6** | 3,93 MB | **-8,5 %** |
| `-O2` | 8 233 000 (7,85 MB) | 33,5 | 7,59 MB | +3,4 % |

Tiempo de compilacion con 8 hilos: **38 s** a `-Os`, 96 s a `-O2`. Compila **sin avisos**.

Tres conclusiones:

1. **No hacen falta overlays.** A `-Os` el codigo son 3,43 MB de los 16 MB. Con los 2 MB
   de RAM de PS1 y el 1 MB de espejo de VRAM van 6,43 MB, y quedan ~9,5 MB para KOS,
   pila, heap y assets en vuelo. Hay holgura de sobra. A `-O2` (7,85 MB) tambien entra,
   pero deja ~5 MB y eso ya aprieta para M2.
2. **La estimacion de M0 acerto.** Decia 2,9-5,9 MB (12-24 B/instr) y han salido 14,6
   B/instr. Queda dentro, en la mitad baja.
3. **El SH-4 sale mas compacto que x86-64 a `-Os`**, que no era obvio: sus instrucciones
   de 2 bytes de ancho fijo compensan necesitar mas instrucciones por operacion. A `-O2`
   se da la vuelta, porque el desenrollado y la alineacion pesan mas que el ancho.

**Queda por decidir `-Os` vs `-O2`, y no se decide por tamano sino por velocidad**, que es
el otro riesgo (CPU e I-cache). Con 8 KB de I-cache, los 3,43 MB de `-Os` fallan menos que
los 7,85 MB de `-O2`, asi que es posible que `-Os` sea ademas **mas rapido** aqui. Se mide
en el paso 5, con el juego corriendo, no antes.

Aviso de lectura: esto es la suma de `.text` de los objetos, no el binario enlazado. El
ELF final suma `ps1rt`, el host, KOS y newlib, y **no se puede descartar nada**: todas las
funciones recompiladas son alcanzables por la tabla de despacho. La cifra del ELF es el
paso 5.

## 2. Auditoria del codigo portable contra los peligros del SH-4

Hecha el 2026-10-18 sobre `dc/src/ps1rt/` y `dc/src/host/` (4 818 lineas). Los contadores
salen de correr el escenario de carrera completo (`into_race.txt`, 900 frames).

| Peligro | Medida | Veredicto |
|---|---|---|
| **Accesos desalineados** (en SH-4 un `mov.l` desalineado levanta excepcion) | `accesos desalineados: 0` en 900 frames de carrera | **Riesgo cerrado para este escenario.** El respaldo byte a byte hay que escribirlo igual (correccion), pero **no se paga nunca** en el camino caliente. El contador se queda puesto |
| **Borde superior de la RAM** (`p & 0x1FFFFF` + 4 bytes) | `en el borde de la RAM: 0` | Igual: el relleno esta, el caso no ocurre |
| **Casts puntero <-> entero** (ILP32 en SH-4, LP64 en x86-64) | 0 sitios | Nada que arreglar |
| **`double` / `float`** (`-m4-single` pone el FPU en simple precision) | 1 sitio, y es un `printf` del informe | Trivial |
| **`long`** (4 bytes en SH-4 y en Windows, 8 en x86-64 Linux) | 4 usos reales (`files.c`, `host.c`, `pad.c`: resultados de `strtoul` y `ftell`) | Revisar uno a uno; ninguno guarda estado de la maquina |
| **Aritmetica de 64 bits** (en SH-4 es software) | 76 sitios en `gte.c`; **28 540 comandos del GTE** en 900 frames (~32/frame) | **No afecta a la correccion; es el candidato numero uno de M3.** El GTE hace todas sus cuentas en `int64_t` por exigencia del hardware de PS1 |
| **Interprete de respaldo** | **21 378 entradas** en 900 frames (~24/frame), el 93 % en `0x80107358` | `interp.c` tiene que estar **completo y correcto** en SH-4: no es codigo muerto. Y conviene mirar que es `0x80107358` (pegado a `EnterCriticalSection`), porque 19 911 entradas al interprete son caras |
| **`setjmp`/`longjmp`** (la parada del host) | 1 sitio | Funciona en KOS (newlib) |

Dependencias de libc del codigo portable: `stdio.h` (11), `string.h` (5), `stdlib.h` (4),
`stdint.h`, `stddef.h`, `setjmp.h`, `limits.h`. **Todas existen en la newlib de KOS**, asi
que la separacion de CLAUDE.md §8 (nada de cabeceras de KOS en `ps1rt/`, `host/` y
`render/`) se sostiene sin tocar esos modulos.

### Correccion (2026-10-19): no habia nada que abstraer

Una version anterior de esta seccion decia que habia **que abstraer `FILE *` y `fopen`**,
porque aparecen en `host/files.c`, `host/dump.c`, `host/pad.c` y `host/host.c`. Era una
suposicion escrita antes de tener el SDK, y es **falsa**: KOS implementa los syscalls de
newlib (`newlib_open.c`, `newlib_read.c`, `newlib_lseek.c`, `newlib_close.c`...) sobre su
VFS, con montajes `/cd` (GD-ROM), `/rd` (romdisk), `/pc` (dcload), `/ram` y `/vmu`.
Comprobado enlazando un `fopen("/cd/NFS4.EXE", "rb")` con `kos-cc`.

Detras de esos cuatro sitios habia **tres problemas distintos**, y solo dos eran trabajo:

| Sitio | Que era de verdad | Que se hizo |
|---|---|---|
| `files.c` | No era `FILE`, era **donde viven los datos**. Y resulta que ya tomaba el prefijo de `h->gamedata`, que se pasa en `host_init` | **Nada.** En Dreamcast se le pasa `/rd` o `/cd` y ya esta |
| `host.c` leyendo `hle_funcs.txt` | **Dato de compilacion**, no de usuario: 130 lineas y 119 direcciones unicas que no cambian, parseadas con `sscanf` al arrancar | Se **hornea** a `gen/hle_table.c` con `tools/recomp/bake_hle.py`. `host_init(h, NULL, ...)` usa la tabla horneada; con una ruta, lee el `.txt` (la via de PC) |
| `dump.c`, `pad_load_script`, `host_call_counts` | Solo existen para el **test diferencial**, y **escriben** archivos. El GD-ROM es de solo lectura | Detras de `PS1_DIAG` (1 por omision, `-DPS1_DIAG=0` en la build de DC). No por portabilidad: por no llevar peso muerto |

**El arbol portable compila entero para SH-4 sin un solo aviso** (`ps1rt` + los 12 archivos
del host con `PS1_DIAG=0` + la tabla horneada), con `-Os -Wall -Wextra`. El unico aviso que
salio fue real y se arreglo: en SH-4 (newlib) **`uint32_t` es `long unsigned int`**, no
`unsigned int`, asi que un `%u` sin cast avisa. Dos sitios, los dos en `host.c:284`.

Sin regresion en PC: el test diferencial sigue dando **120 frames identicos** en el front
end y **231** en la carrera, con la divergencia del frame 232 en el mismo sitio de siempre
(los registros del GTE, `Render_RenderPlayerView`, que va a M2b).

## 3. El toolchain: instalado y verificado (2026-10-19)

**Instalado el 2026-10-19**, siguiendo [`docs/instalar-sdk.md`](../instalar-sdk.md).
Versiones exactas de esta maquina:

| Pieza | Version |
|---|---|
| entorno | MSYS2 **UCRT64** sobre Windows 11 |
| gcc del host | 16.2.0 (Rev6, MSYS2) |
| `sh-elf-gcc` | **15.2.0** |
| `sh-elf-ld` (binutils) | **2.45.1** |
| newlib | **4.6.0** |
| KallistiOS | `8e704086`, 2026-10-16 |
| `arm-eabi-gcc` (AICA) | **no instalado**, a proposito: ver abajo |
| `KOS_SH4_PRECISION` | `-m4-single` |

Artefactos: `lib/dreamcast/libkallisti.a` (9,28 MB), `libkallisti_exports.a`,
`libromdiskbase.a` y los addons (`libpthread.a`, `libkosfat.a`, ...).

Prueba de humo real (no solo `--version`): un `hello.c` con `#include <kos.h>` compila y
**enlaza**, y `sh-elf-objdump -f` dice `elf32-shl`, `architecture: sh4a`, `EXEC_P`.
`text 189500  data 5792  bss 18596`.

**El toolchain del AICA (ARM7) se dejo sin instalar.** Su perfil usa gcc 8.5.0, que no
compila en hosts Windows modernos (`libcpp/files.c:1696`, conversion `const char*` ->
`char*` en el bloque de rutas DOS; `files.c` se compila como C++). Da igual: KOS trae
`kernel/arch/dreamcast/sound/arm/stream.drv.prebuilt` y su `Makefile` lo copia solo cuando
`wildcard $(DC_ARM_CC)` sale vacio. Verificado: `stream.drv` se genero con los mismos 3 360
bytes del precompilado. Lo unico que se pierde es poder **recompilar el firmware del
AICA**, que este port no necesita: el audio de M3 va por la API de sonido de KOS, que
corre sobre ese mismo driver.

### Como se decidio y como se documento


**Decision de Jorge (2026-10-18): dc-chain dentro del MSYS2 que ya existe.** La
instalacion la lanza el **por separado**, de dos formas que llevan al mismo sitio:

- [`dc/tools/install-kos.sh`](../../dc/tools/install-kos.sh) — automatico, de una pasada.
- [`docs/instalar-sdk.md`](../instalar-sdk.md) — **la misma cosa paso a paso**, con una
  comprobacion despues de cada paso y una tabla de sintomas. Es el respaldo si el script
  falla: cada paso es independiente y se retoma donde se rompio.

Las dos cosas son **solo para Windows con MSYS2** (comprueban `$MSYSTEM` y usan `pacman`).

El script esta escrito contra el **procedimiento real del repositorio de KallistiOS**
(consultado el 2026-10-18), no contra el wiki, que esta desactualizado en tres puntos que
habrian hecho fallar una instalacion a ciegas:

| El wiki dice | La realidad |
|---|---|
| el constructor esta en `utils/dc-chain` | se llama **`utils/kos-chain`** |
| se clona con `git://git.code.sf.net/p/cadcdev/...` | el protocolo `git://` esta **retirado**; el desarrollo vive en **GitHub** |
| hay que editar y correr `packages/fixup-sh4-newlib.sh` | ese script **ya no existe**: el arreglo es automatico (`auto_fixup_newlib` vale 1 por omision) |

Lo que el script instala, con las rutas de fabrica de KallistiOS (no son una eleccion
nuestra: salen de `Makefile.dreamcast.cfg`, `Makefile.aica.cfg` y `doc/environ.sh.sample`):

```
/opt/toolchains/dc/kos/        KallistiOS            (C:\msys64\opt\toolchains\dc\...)
/opt/toolchains/dc/kos-ports/  librerias portadas (se clona, no se compila)
/opt/toolchains/dc/sh-elf/     toolchain SH-4, la CPU
/opt/toolchains/dc/arm-eabi/   toolchain ARM7, el AICA (OPCIONAL: KOS usa stream.drv.prebuilt si falta)
```

Perfil **estable** para Dreamcast: binutils 2.45.1, **gcc 15.2.0**, newlib 4.6.0, con
`--with-endian=little`, multilib `m4-single,m4-single-only` y `thread_model=kos`. El
`-m4-single` que pide CLAUDE.md §3 es el valor por omision, asi que no hay nada que
ajustar.

Tres cosas que el script hace a proposito distinto de la documentacion, y por que:

1. **No hace `pacman -Syuu` por omision** (queda detras de `--upgrade`). En esta maquina
   el gcc 16.2 de UCRT64 es con el que esta **validado M1a**; una actualizacion completa
   puede moverlo y entonces un fallo nuevo no se sabria atribuir al SH-4 o al x86.
2. **Pasa `makejobs=N`** (el numero solo, sin `-j`: `init.mk` hace
   `jobs_arg = -j$(makejobs)`), para poder bajarlo a 1 al diagnosticar. No hace falta
   para ir en paralelo: con `makejobs` sin definir, `init.mk` ya usa todos los nucleos.
3. **No crea `Makefile.cfg`.** Usa `make platform=dreamcast` y `make platform=aica`, que
   el Makefile de `kos-chain` soporta, para que las dos arquitecturas salgan de sus
   ficheros de fabrica y no quede un archivo local que se quede viejo.

Modo seguro para mirar antes de instalar: `bash dc/tools/install-kos.sh --check`, que no
descarga ni instala nada. Detecta tambien el error facil de lanzarlo desde el **Git Bash**
de Git for Windows, que pone el mismo `MSYSTEM=MINGW64` que MSYS2 pero no tiene `pacman`.

**El script no se ha ejecutado en la sesion en que se escribio**, asi que esta verificado
en sintaxis y en su modo `--check`, no de punta a punta.

### Los tres caminos que se consideraron

Para dejar constancia de por que se eligio el primero:

| Camino | Coste | A favor | En contra |
|---|---|---|---|
| **dc-chain en el MSYS2 que ya hay** | compilacion larga y desatendida | KOS de upstream; misma familia de gcc que ya se usa (que es la razon por la que el plan eligio gcc y no MSVC); un solo arbol de toolchain | construir `sh-elf-gcc` desde fuente tarda |
| **DreamSDK** (instalador de Windows) | minutos | rapido, trae `sh-elf-gcc` ya construido | monta su propio entorno MSYS en paralelo al que ya hay, y su KOS puede ir por detras de upstream |
| **Docker** | descarga de imagen | reproducible y aislado | hace falta Docker Desktop; el ciclo de edicion-compilacion se vuelve mas incomodo |

Se eligio el primero: el proyecto va a vivir mucho tiempo y conviene estar sobre KOS de
upstream, con un solo entorno que mantener. El coste es una compilacion larga que no
necesita vigilancia.

## 4. Orden de trabajo propuesto

| # | Paso | Cierra con |
|---|---|---|
| 0 | Instalar KOS + `sh-elf-gcc` | **Hecho (2026-10-19).** Versiones abajo |
| 1 | **Medir el tamano**: compilar `dc/gen/` con `-ml -m4-single -Os` | **Hecho (2026-10-19): 3,43 MB.** Sin overlays (seccion 1) |
| 2 | `ps1rt` en SH-4: compilar y pasar las **227 pruebas unitarias** en Dreamcast | **Hecho (2026-10-19): 227/227 en Flycast 2.6** |
| 3 | `platform/dc/`: archivos (GD-ROM o romdisk), pad, timers, VSync, salida de texto | el juego arranca y avanza frames, con contadores en pantalla |
| 4 | Front end y logica de carrera avanzando sin cuelgues | N frames sin panico, en Flycast **y en hardware** |
| 5 | Medir: tamano del ELF, RAM en uso, ms/frame | cifras en este archivo; comparacion con el presupuesto de la seccion 1 |

El paso 2 es el que aprovecha M1a de verdad: las 227 comprobaciones de `ps1rt` son
independientes de la plataforma, asi que separan limpiamente "el runtime esta mal en SH-4"
de "el host de DC esta mal".

## 4bis. `ps1rt` en SH-4 (paso 2, parte compilada)

`make -C dc dc-test` produce `build/dc/run_tests.elf`:

- Los 7 modulos de `ps1rt` (`alu`, `bios`, `cpu`, `gte`, `interp`, `mem`, `panic`) mas el
  runner compilan con `kos-cc -std=c11 -Os -Wall -Wextra` **sin un solo aviso**.
- Enlaza: `text 229 108 · data 5 792 · bss 2 124 456`. El bss son los 2 MB de RAM de PS1
  mas el scratchpad y el relleno del borde, o sea lo esperado.
- Las cuatro `_Static_assert` de `ps1rt.h` pasan en SH-4: `int` de 32 bits, `>>` con signo
  aritmetico, conversion `u32 -> i32` y **`__ORDER_LITTLE_ENDIAN__`**. Esta ultima no es
  trivial: el SH-4 existe en las dos endianness y confirma que `-ml` esta puesto.
- Esto no contradice la auditoria de la seccion 2, la confirma: los peligros que quedaban
  (`long` de 4 bytes, `int64_t` del GTE) son de **host y de velocidad**, no de compilacion.

La envoltura de arranque es [`dc/src/platform/dc/test_main.c`](../../dc/src/platform/dc/test_main.c):
el runner portable se compila con `-Dmain=ps1rt_tests_main` y KOS llama al `main` de
verdad. La salida va a dcload o al cable serie si los hay y, si no, al framebuffer, que
existe siempre; al terminar espera a START para que el resultado se pueda leer.

### Ejecutado: **227/227 en SH-4** (Flycast 2.6, 2026-10-19)

Mismo resultado que en x86, comprobacion por comprobacion. Cubre los 22 bloques del
runner: mapa de memoria, accesos desalineados, `div`/`mult`, `lwl`/`lwr`/`swl`/`swr`,
`load_exe`, orden de despacho de `ps1_call`, ticks, parada, continuaciones, llamadas
anidadas, interrupciones, diferidas, `syscall`, los seis de GTE (incluidos `divide`, el
RTPS de identidad, las rarezas del banco de registros, saturaciones y banderas, y **el
error de MVMVA con `cv == 2`**) y los tres de BIOS A0.

Esto importa mas de lo que parece: el GTE hace toda su aritmetica en `int64_t`, que en
SH-4 es **software**, y las saturaciones y banderas dependen de que esas cuentas de 64
bits den exactamente lo mismo que en x86-64. Dan lo mismo.

La linea `ps1rt: llamada a BIOS 0xa0:0x1 sin implementar (devuelve 0)` que sale en
pantalla es el aviso esperado de B0/C0, que estan sin implementar a proposito, no un
fallo.

Como arrancarlo: `make -C dc dc-test` y `flycast.exe dc/build/dc/run_tests.elf`. Flycast
carga el `.elf` directamente, sin GDI ni CDI. Arranca por REIOS.

**Trampa que costo un rato:** KOS manda la salida al dispositivo que detecta, y
`scif_detected()` devuelve 1 **siempre**, asi que gana el puerto serie y **la pantalla del
Dreamcast se queda negra**. La salida esta ahi, pero en la ventana "Flycast Serial Output"
(hace falta `Debug.SerialConsoleEnabled = yes` en `emu.cfg`), no en la del emulador. La
envoltura fuerza `fb` por eso: en hardware sin cable serie ni dcload esa ventana no
existe.

## 4ter. La capa de archivos: los datos no caben (2026-10-19)

Al ir a escribir `platform/dc/` salio un problema que no es de plataforma sino de
**tamano de los datos**, y cambia el plan del paso 3.

**Lo medido:**

| | |
|---|---|
| `gamedata/` completo | **1,3 GB** (de los cuales 700 MB son la imagen `.bin` del disco original y 56 MB los FMV; datos de juego, ~600 MB) |
| Lo que monta el front end | **zzzFE.viv, 28,7 MB**, y `files.c` lo leia **entero a memoria** |
| Lo que lee de verdad una carrera entera | **3,87 MB** en 266 lecturas, 102 aperturas, 3 BIGF montados |
| Con solo NFS4.EXE + FRONT.BIN | el juego carga FRONT.BIN, **falla al montar zzzFE.viv y se queda girando**: watchdog a los 10 000 001 saltos hacia atras, **0 frames** |

Dos consecuencias:

1. **El romdisk no sirve.** Vive en RAM, asi que meter zzzFE.viv en el ELF es meter 28,7
   MB en una maquina de 16. Y con los dos archivos sueltos el juego no avanza ni un frame.
   La recomendacion anterior de «`/rd` para arrancar y `/cd` para la build real» **era
   mia y estaba mal**: la di sin haber mirado el tamano de los datos.
2. **Leer archivos enteros no se sostiene**, sea cual sea el medio.

**Arreglado (lo segundo):** el montaje de BIGF ahora **abre el archivo y lee a demanda**.
Solo queda residente el directorio, que esta al principio y es contiguo. El formato lo
dice en la cabecera: `"BIGF"`, +4 tamano, +8 numero de entradas, +12 tamano del
directorio; comprobado en ZZZFE.VIV (435 entradas, directorio de 8 559 bytes, primera
entrada en 0x2170, justo detras).

| Archivo | En disco | Residente antes | Residente ahora |
|---|---|---|---|
| ZZZFE.VIV | 28 733 748 | 28 733 748 | **31 320** |
| ZZZZZENG.VIV | 53 698 916 | 53 698 916 | **43 848** |

Sin cambio de comportamiento: el test diferencial sigue dando **120 frames identicos** y
la misma divergencia en el 232, las **227** pruebas unitarias pasan, y el arbol portable
sigue compilando para SH-4 **sin un solo aviso**.

**Lo que queda, y es decision de Jorge:** hace falta un **disco**. KOS ya trae compilados
`makeip.exe`, `scramble.exe`, `elf2bin` y `genromfs` en `utils/`; lo unico que falta es un
generador de ISO, y `xorriso` esta en el repo `msys` de MSYS2 (`pacman -S xorriso`). Eso
adelanta a M1b el `mkdisc` que estaba planeado para M2.

## 5. Lo que no se mide aqui

El **fps** no es criterio de M1b. El objetivo de 30 fps sostenidos (CLAUDE.md §3) se mide
en M3, cuando exista el renderer del PVR; en M1b el renderer no dibuja y el numero no
significaria nada.
