# Instalar el SDK de Dreamcast a mano (Windows + MSYS2)

Guia paso a paso para dejar `sh-elf-gcc` y KallistiOS funcionando. Es la alternativa
manual a [`dc/tools/install-kos.sh`](../dc/tools/install-kos.sh), que hace esto mismo
seguido: **si el script falla, sigue esta guia desde el paso donde se rompio**, porque
cada paso es independiente y se puede repetir.

**Esto es solo para Windows con MSYS2.** El script y esta guia comprueban `$MSYSTEM` y
usan `pacman`, asi que no valen en Linux ni en macOS. Para esos, KallistiOS trae sus
propias guias en `utils/kos-chain/doc/` (`debian.md`, `alpine.md`, `macos.md`, `bsd.md`,
`cygwin.md`).

**Cuanto tarda:** entre 1 y 3 horas, casi todo en los pasos 5 y 6. Construye binutils,
gcc (dos pasadas) y newlib desde fuente, para dos arquitecturas.

**Que vas a tener al acabar:**

| Ruta en MSYS2 | En Windows | Que es |
|---|---|---|
| `/opt/toolchains/dc/kos/` | `C:\msys64\opt\toolchains\dc\kos\` | KallistiOS |
| `/opt/toolchains/dc/kos-ports/` | `...\kos-ports\` | librerias portadas (se clona, no se compila) |
| `/opt/toolchains/dc/sh-elf/` | `...\sh-elf\` | toolchain **SH-4**, la CPU del Dreamcast |
| `/opt/toolchains/dc/arm-eabi/` | `...\arm-eabi\` | toolchain **ARM7**, el AICA (el sonido) |

Esas rutas **no son una eleccion nuestra**: son las de fabrica de KallistiOS, y vienen de
`utils/kos-chain/Makefile.dreamcast.cfg` (`toolchain_path=/opt/toolchains/dc/sh-elf`),
`Makefile.aica.cfg` y `doc/environ.sh.sample`. Cambiarlas obliga a editar los tres sitios.

Hacen falta **los dos** toolchains: KallistiOS no enlaza sin el del AICA.

> **Aviso:** la documentacion del wiki de KallistiOS esta desactualizada en tres puntos
> que hacen fallar una instalacion a ciegas. Esta guia esta escrita contra el repositorio
> real, consultado el 2026-10-18. Las diferencias estan en el apartado
> [«Donde el wiki se equivoca»](#donde-el-wiki-se-equivoca).

---

## 1. Requisitos previos

- Windows 10 u 11.
- **MSYS2 instalado** en `C:\msys64` (ya lo esta en esta maquina).
- **~15 GB libres** en C:. La construccion descarga y compila gcc dos veces por
  arquitectura; con el paso 9 se recupera casi todo.

## 2. Abre el shell correcto

Ejecuta **`C:\msys64\ucrt64.exe`**. Comprueba que estas donde crees:

```sh
echo $MSYSTEM          # tiene que decir UCRT64
command -v pacman      # tiene que decir /usr/bin/pacman
```

Dos trampas aqui:

- **El Git Bash de Git for Windows tambien pone `MSYSTEM=MINGW64`**, pero no es MSYS2: no
  tiene `pacman` ni `/opt`. Si `command -v pacman` no dice nada, estas en el shell
  equivocado. (El script detecta justo esto y aborta con un mensaje claro.)
- No vale el shell **MSYS** pelado ni **CLANG64**.

**UCRT64 o MINGW64?** Esta guia usa **UCRT64** porque es el entorno que ya usa el proyecto
para el build de x86, y asi hay uno solo que mantener. KallistiOS documenta MINGW64. El
compilador que construye el cruzado no cambia lo que el cruzado genera, asi que da igual
para el resultado; si la construccion falla en UCRT64, **reintentar en MINGW64 es la via
documentada**. Lo unico que cambia entre los dos es el prefijo de los paquetes del paso 3.

## 3. Instala los paquetes

Fija el prefijo segun tu entorno y luego instala:

```sh
PKG=mingw-w64-ucrt-x86_64        # en UCRT64
# PKG=mingw-w64-x86_64           # en MINGW64

pacman -Sy --needed --noconfirm \
    base-devel patch diffutils git python \
    $PKG-toolchain $PKG-autotools $PKG-texinfo \
    $PKG-libpng $PKG-libjpeg-turbo $PKG-libelf \
    $PKG-freetype $PKG-freeglut
```

`--noconfirm` hace falta porque `base-devel` es un grupo y si no pregunta por cada
paquete. `libpng`, `libjpeg-turbo`, `freetype` y `freeglut` no los necesita el toolchain:
los piden los addons y los ejemplos de KOS.

**Comprueba:**

```sh
gcc --version && git --version && python --version && make --version
```

### Sobre `pacman -Syuu`

La documentacion de KallistiOS dice de actualizar MSYS2 entero antes de empezar, y MSYS2
desaconseja las actualizaciones parciales. **Aqui no se hace por omision**, a proposito: en
esta maquina el gcc 16.2 de UCRT64 es con el que esta **validado M1a** (120 frames
identicos a la referencia, 227/227 pruebas). Si una actualizacion completa lo mueve, un
fallo nuevo ya no se sabe atribuir al SH-4 o al x86.

Si el paso 3 falla por dependencias incompatibles, entonces si:

```sh
pacman -Syuu
# si pide cerrar la terminal: cierrala, abrela de nuevo, y repite el comando
```

y despues **revalida el lado x86** antes de seguir, para no arrastrar dudas:

```sh
cd /c/Users/jafvi/Documents/Dreamcast-dev/nfshs-dc
make -C dc test     # tiene que dar 227/227
make -C dc diff     # tiene que dar "OK: 120 frames identicos"
```

## 4. Crea los directorios

```sh
mkdir -p /opt/toolchains/dc
cd /opt/toolchains/dc
pwd                              # /opt/toolchains/dc
```

## 5. Clona KallistiOS

```sh
cd /opt/toolchains/dc
git clone https://github.com/KallistiOS/KallistiOS.git kos
git clone https://github.com/KallistiOS/kos-ports.git  kos-ports
```

Fijate en que el directorio se llama **`kos`**, no `KallistiOS`: `environ.sh.sample` y los
makefiles esperan `/opt/toolchains/dc/kos`.

Si ya estaban clonados, actualizalos en vez de volver a clonar:

```sh
git -C /opt/toolchains/dc/kos       pull --ff-only
git -C /opt/toolchains/dc/kos-ports pull --ff-only
```

**Comprueba** que el constructor esta donde esta guia espera:

```sh
ls /opt/toolchains/dc/kos/utils/kos-chain/Makefile.dreamcast.cfg
```

Si no existe, KallistiOS ha movido el constructor de sitio: mira que hay en
`/opt/toolchains/dc/kos/utils/` y avisa, porque habria que ajustar esto y el script.

## 6. Construye el toolchain del SH-4 — **el paso largo**

```sh
cd /opt/toolchains/dc/kos/utils/kos-chain
make platform=dreamcast makejobs=$(nproc)
```

Dos cosas que conviene entender de ese comando:

- **`platform=dreamcast`** evita crear un `Makefile.cfg`. El Makefile de `kos-chain` lee
  `Makefile.cfg` si existe y, si no, `Makefile.$(platform).cfg` cuando le pasas `platform=`
  por linea de comandos. Asi las dos arquitecturas salen de sus ficheros de fabrica y no
  queda un archivo local que se quede viejo.
- **`makejobs=N` es solo el numero**, sin `-j`. `scripts/init.mk` hace
  `jobs_arg = -j$(makejobs)`, asi que si le pasas `-j8` acaba construyendo `-j-j8`, make
  no reconoce la opcion y **vuelca su ayuda en pantalla** en vez de compilar. Es un fallo
  facil de cometer y el mensaje no dice que ha pasado.
  Dejarlo sin poner tampoco compila en serie: `init.mk` cae en
  `-j$(getconf _NPROCESSORS_ONLN)`, o sea todos los nucleos. O sea que **`make
  platform=dreamcast` a secas ya va en paralelo**; se pasa explicito solo para poder
  bajarlo a `makejobs=1` si hay que diagnosticar un fallo.

Lo que construye es el perfil **estable**: binutils 2.45.1, **gcc 15.2.0**, newlib 4.6.0,
con `--with-endian=little`, multilib `m4-single,m4-single-only` y `thread_model=kos`. El
`-m4-single` que pide CLAUDE.md §3 es el valor por omision, no hay nada que ajustar.

**Comprueba:**

```sh
/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc --version
```

Si falla, los registros de cada componente estan en
`/opt/toolchains/dc/kos/utils/kos-chain/logs/`. Mira el del ultimo que corrio.

## 7. Construye el toolchain del AICA (ARM7)

```sh
cd /opt/toolchains/dc/kos/utils/kos-chain
make platform=aica makejobs=$(nproc)
```

**Comprueba:**

```sh
/opt/toolchains/dc/arm-eabi/bin/arm-eabi-gcc --version
```

## 8. Configura el entorno de KallistiOS

```sh
cd /opt/toolchains/dc/kos
cp doc/environ.sh.sample environ.sh
```

**No hay que editar nada.** El ejemplo ya trae exactamente las rutas de esta guia
(`KOS_BASE=/opt/toolchains/dc/kos`, `KOS_CC_BASE=.../sh-elf`,
`DC_ARM_BASE=.../arm-eabi`) y `KOS_SH4_PRECISION="-m4-single"`.

Si ya tenias un `environ.sh`, **no lo sobrescribas** sin mirarlo.

Carga el entorno (hay que hacerlo en cada sesion nueva de terminal):

```sh
source /opt/toolchains/dc/kos/environ.sh
echo $KOS_BASE                   # /opt/toolchains/dc/kos
command -v sh-elf-gcc            # ahora esta en el PATH
```

Cargar `environ.sh` mete `$KOS_CC_BASE/bin` en el `PATH`, que es por lo que a partir de
aqui puedes llamar a `sh-elf-gcc` sin la ruta completa.

## 9. Compila KallistiOS

```sh
source /opt/toolchains/dc/kos/environ.sh
cd /opt/toolchains/dc/kos
make -j$(nproc)
```

Unos minutos, no horas.

**Comprueba:**

```sh
ls -l /opt/toolchains/dc/kos/lib/dreamcast/libkallisti.a
```

`kos-ports` no hay que compilarlo para M1b: no usamos ninguna de sus librerias.

## 10. Comprueba que compila de verdad para SH-4

La prueba que de verdad cierra la instalacion:

```sh
source /opt/toolchains/dc/kos/environ.sh
echo 'int main(void){return 0;}' > /tmp/t.c
sh-elf-gcc -ml -m4-single -Os -c /tmp/t.c -o /tmp/t.o && sh-elf-size /tmp/t.o
```

Si eso da un tamano, el toolchain esta listo.

## 11. Anota las versiones

El paso 0 de [`hitos/M1b.md`](hitos/M1b.md) pide dejarlas registradas. Saca el bloque y
pegalo alli:

```sh
{
  echo "SDK de Dreamcast, $(date -u '+%Y-%m-%d %H:%M UTC')"
  echo "entorno MSYS2:  $MSYSTEM"
  echo "gcc del host:   $(gcc --version | head -1)"
  echo "sh-elf-gcc:     $(/opt/toolchains/dc/sh-elf/bin/sh-elf-gcc --version | head -1)"
  echo "arm-eabi-gcc:   $(/opt/toolchains/dc/arm-eabi/bin/arm-eabi-gcc --version | head -1)"
  echo "KallistiOS:     $(git -C /opt/toolchains/dc/kos log -1 --format='%h %cd' --date=short)"
} | tee /opt/toolchains/dc/versiones-sdk.txt
```

## 12. Libera espacio (opcional)

Borra los arboles de fuentes del toolchain, que ya no hacen falta:

```sh
cd /opt/toolchains/dc/kos/utils/kos-chain
make platform=dreamcast distclean
make platform=aica      distclean
```

---

## Si algo falla

| Sintoma | Causa y arreglo |
|---|---|
| `pacman: command not found` con `$MSYSTEM` puesto | Estas en el **Git Bash** de Git for Windows, no en MSYS2. Abre `C:\msys64\ucrt64.exe` |
| `Cannot create temporary file in C:\WINDOWS\: Permission denied` | gcc busca su temporal en `TMP`/`TEMP` y no los tiene. Pasa al invocar `make` desde un shell que no los exporta. Arreglo: `export TMPDIR=/tmp TMP=/tmp TEMP=/tmp` (ojo: en **comandos separados**, porque `export A=/tmp B=$A` deja `B` con el valor **viejo** de `A`) |
| **make vuelca su pagina de ayuda** y luego `Error 1` en `binutils.mk` (o en cualquier otro componente) | Le has pasado una opcion que no entiende. Casi siempre es `makejobs` con el `-j` incluido: `init.mk` hace `jobs_arg = -j$(makejobs)`, asi que `makejobs=-j8` produce `-j-j8`. Usa **solo el numero**: `makejobs=8`, o no lo pongas |
| El paso 6 o 7 se para a mitad | Mira `kos-chain/logs/`, que tiene un registro por componente. Los pasos se pueden **relanzar**: `make platform=... ` retoma lo que falte |
| `configure: error: no acceptable C compiler` | Falta `$PKG-toolchain` del paso 3, o estas en el shell MSYS pelado en vez de UCRT64/MINGW64 |
| Errores raros de dependencias al instalar paquetes | Actualizacion parcial. Haz `pacman -Syuu`, reabre la terminal, repite, y **revalida el lado x86** (ver el apartado del paso 3) |
| Se queda sin espacio | Haz el paso 12 y relanza |
| `sh-elf-gcc: command not found` tras instalar | Te falta `source /opt/toolchains/dc/kos/environ.sh` en esa terminal |
| Falla el enlazado de KOS por algo de ARM | Te falta el paso 7. KOS no enlaza sin el toolchain del AICA |
| Un script `.sh` del repo da errores raros de sintaxis | Finales de linea CRLF. El repo tiene `.gitattributes` que lo evita; si vienes de un clon viejo, `git add --renormalize .` |

## Donde el wiki se equivoca

Si acabas buscando en la documentacion oficial, cuidado con estos tres, que estan en el
wiki renderizado y harian fallar la instalacion:

| El wiki dice | La realidad (2026-10-18) |
|---|---|
| el constructor esta en `utils/dc-chain` | se llama **`utils/kos-chain`** |
| `git clone git://git.code.sf.net/p/cadcdev/kallistios kos` | el protocolo **`git://` esta retirado**; el desarrollo vive en GitHub |
| hay que editar y correr `packages/fixup-sh4-newlib.sh` | **ya no existe**: el arreglo de newlib es automatico (`auto_fixup_newlib` vale 1 por omision) |

## Que viene despues

El paso 1 de [`hitos/M1b.md`](hitos/M1b.md), y no es escribir codigo de plataforma: es
**medir el tamano** del codigo generado compilado para SH-4, porque en Dreamcast el
`1ST_READ.BIN` se carga entero en RAM y cada byte de codigo es un byte menos de los 16 MB.
La referencia medida en x86-64 es **7,59 MB con `-O2` y 3,93 MB con `-Os`**.

Nota para cuando llegue ese momento: `environ.sh` pone `KOS_CFLAGS` con `-O2`. Para el
codigo generado nos interesa `-Os`, pero eso se decide en el Makefile del proyecto, no
tocando `environ.sh`.
