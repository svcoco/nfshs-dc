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

**Primera medicion de M1b, antes de escribir una linea de plataforma:** compilar
`dc/gen/` con `sh-elf-gcc -ml -m4-single -Os` y dar el `.text` real. Es la cifra que decide
si hay que trocear el codigo (overlays desde el GD-ROM) o no, y es una medicion de una
tarde, no de un hito.

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

**Lo que si hay que abstraer:** `FILE *` y `fopen` aparecen en `host/files.c`,
`host/dump.c`, `host/pad.c` y `host/host.c`. En Dreamcast los archivos del juego salen del
GD-ROM (`/cd`) o de un romdisk, no del sistema de archivos del PC. De los cuatro:

- `files.c` es el que importa: es la capa de archivos del juego.
- `dump.c` y `pad.c` son del **test diferencial** (volcado por frame y guion de entrada):
  en DC no hacen falta, y conviene que queden detras de una compilacion condicional en vez
  de arrastrar `stdio` al binario de consola.
- `host.c` solo los usa para el informe.

## 3. El toolchain: decidido, y el script esta escrito

**Decision de Jorge (2026-10-18): dc-chain dentro del MSYS2 que ya existe.** La
instalacion la lanza el **por separado**, con
[`dc/tools/install-kos.sh`](../../dc/tools/install-kos.sh).

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
/opt/toolchains/dc/arm-eabi/   toolchain ARM7, el AICA (KOS no enlaza sin el)
```

Perfil **estable** para Dreamcast: binutils 2.45.1, **gcc 15.2.0**, newlib 4.6.0, con
`--with-endian=little`, multilib `m4-single,m4-single-only` y `thread_model=kos`. El
`-m4-single` que pide CLAUDE.md §3 es el valor por omision, asi que no hay nada que
ajustar.

Tres cosas que el script hace a proposito distinto de la documentacion, y por que:

1. **No hace `pacman -Syuu` por omision** (queda detras de `--upgrade`). En esta maquina
   el gcc 16.2 de UCRT64 es con el que esta **validado M1a**; una actualizacion completa
   puede moverlo y entonces un fallo nuevo no se sabria atribuir al SH-4 o al x86.
2. **Pasa `makejobs=-jN`.** Los `.cfg` de fabrica lo dejan vacio, o sea compilacion en
   serie.
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
| 0 | Instalar KOS + `sh-elf-gcc` con `dc/tools/install-kos.sh` (lo lanza Jorge aparte) | `sh-elf-gcc --version` responde; el script deja las versiones en `/opt/toolchains/dc/versiones-sdk.txt`, que se pegan **aqui** |
| 1 | **Medir el tamano**: compilar `dc/gen/` con `-ml -m4-single -Os` | `.text` real de SH-4, comparado con los 3,93 MB de x86-64 |
| 2 | `ps1rt` en SH-4: compilar y pasar las **227 pruebas unitarias** en Dreamcast | pruebas en verde sobre Flycast |
| 3 | `platform/dc/`: archivos (GD-ROM o romdisk), pad, timers, VSync, salida de texto | el juego arranca y avanza frames, con contadores en pantalla |
| 4 | Front end y logica de carrera avanzando sin cuelgues | N frames sin panico, en Flycast **y en hardware** |
| 5 | Medir: tamano del ELF, RAM en uso, ms/frame | cifras en este archivo; comparacion con el presupuesto de la seccion 1 |

El paso 2 es el que aprovecha M1a de verdad: las 227 comprobaciones de `ps1rt` son
independientes de la plataforma, asi que separan limpiamente "el runtime esta mal en SH-4"
de "el host de DC esta mal".

## 5. Lo que no se mide aqui

El **fps** no es criterio de M1b. El objetivo de 30 fps sostenidos (CLAUDE.md §3) se mide
en M3, cuando exista el renderer del PVR; en M1b el renderer no dibuja y el numero no
significaria nada.
