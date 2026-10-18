#!/usr/bin/env bash
#
# Instala el toolchain de Dreamcast (sh-elf-gcc + arm-eabi-gcc) y KallistiOS, para M1b.
#
#   SOLO PARA WINDOWS CON MSYS2. Es bash, pero comprueba $MSYSTEM y usa pacman, asi que
#   no vale en Linux ni en macOS: para esos, KallistiOS trae sus propias guias en
#   utils/kos-chain/doc/ (debian.md, alpine.md, macos.md, bsd.md, cygwin.md).
#
#   SI ESTE SCRIPT FALLA, hay una guia manual paso a paso en docs/instalar-sdk.md, con
#   los mismos pasos por separado, una comprobacion despues de cada uno y una tabla de
#   sintomas. Cada paso se puede repetir, asi que se retoma donde se rompio.
#
#   COMO SE EJECUTA
#
#     1. Abre el shell de MSYS2 **UCRT64** (C:\msys64\ucrt64.exe) o **MINGW64**
#        (C:\msys64\mingw64.exe). No vale el shell "MSYS" pelado ni CLANG64.
#     2. bash /c/Users/jafvi/Documents/Dreamcast-dev/nfshs-dc/dc/tools/install-kos.sh
#
#   Primero conviene ver que falta sin instalar nada:
#
#     bash .../install-kos.sh --check
#
#   Opciones:
#     --check      solo comprueba requisitos y sale (no instala, no compila, no descarga)
#     --upgrade    hace `pacman -Syuu` antes de instalar (ver la nota de abajo)
#     --jobs N     hilos de compilacion (por omision: todos los nucleos)
#     --skip-pacman   no toca paquetes de MSYS2 (si ya los tienes)
#     --distclean  al terminar, borra los arboles de fuentes del toolchain (libera GBs)
#
#   TARDA MUCHO. Construye binutils, gcc (dos pasadas) y newlib desde fuente, para dos
#   arquitecturas. Cuenta entre 1 y 3 horas segun la maquina. Se puede relanzar: salta
#   lo que ya este hecho.
#
#   QUE INSTALA Y DONDE (rutas de MSYS2; /opt es C:\msys64\opt)
#
#     /opt/toolchains/dc/              raiz del SDK
#     /opt/toolchains/dc/kos/          KallistiOS (git, GitHub)
#     /opt/toolchains/dc/kos-ports/    librerias portadas (se clona, no se compila)
#     /opt/toolchains/dc/sh-elf/       toolchain SH-4 (la CPU del Dreamcast)
#     /opt/toolchains/dc/arm-eabi/     toolchain ARM7 (el AICA, el procesador de sonido)
#
#   Esas rutas no son una eleccion nuestra: son las que traen por omision
#   `utils/kos-chain/Makefile.dreamcast.cfg` (toolchain_path=/opt/toolchains/dc/sh-elf),
#   `Makefile.aica.cfg` y `doc/environ.sh.sample` de KallistiOS. Cambiarlas obliga a
#   editar los tres sitios, asi que el script usa las de fabrica.
#
#   POR QUE `pacman -Syuu` NO SE HACE POR OMISION
#
#   La documentacion de KallistiOS dice de actualizar MSYS2 entero antes de empezar, y
#   MSYS2 desaconseja las actualizaciones parciales. Pero en esta maquina hay un gcc de
#   UCRT64 (16.2) con el que esta **validado M1a**: 120 frames identicos a la referencia
#   y 227/227 pruebas. Una actualizacion completa puede moverlo, y entonces habria que
#   revalidar antes de saber si un fallo nuevo es del SH-4 o del x86. Por eso el script
#   instala solo lo que falta (`--needed`) y deja la actualizacion completa detras de
#   `--upgrade`, por si hace falta.
#
#   Si `--upgrade` pide cerrar la terminal, cierrala, abrela de nuevo y relanza.
#
#   QUE *NO* HACE
#
#   No compila nada de este proyecto. Al acabar, el paso 1 de M1b es medir el tamano del
#   codigo generado en SH-4 (ver docs/hitos/M1b.md).
#
#   AVISO HONESTO: este script esta escrito contra el procedimiento real del repositorio
#   de KallistiOS (utils/kos-chain, consultado el 2026-10-18), no contra el wiki, que
#   esta desactualizado: alli el directorio se llama todavia `dc-chain`, se clona por
#   `git://` de SourceForge (protocolo ya retirado) y se menciona un
#   `packages/fixup-sh4-newlib.sh` que ya no existe, porque el arreglo de newlib ahora es
#   automatico (`auto_fixup_newlib` vale 1 por omision). Aun asi, **no se ha ejecutado**,
#   asi que si algo falla el log dira donde.

set -u
set -o pipefail

CHECK_ONLY=0
DO_UPGRADE=0
SKIP_PACMAN=0
DO_DISTCLEAN=0
JOBS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --check)       CHECK_ONLY=1 ;;
        --upgrade)     DO_UPGRADE=1 ;;
        --skip-pacman) SKIP_PACMAN=1 ;;
        --distclean)   DO_DISTCLEAN=1 ;;
        --jobs)        JOBS="$2"; shift ;;
        # imprime la cabecera de comentarios, sin el '#', hasta la primera linea de
        # codigo: asi no hay que mantener un rango de lineas a mano
        -h|--help)     awk 'NR>1 && /^#/ {sub(/^# ?/, ""); print; next}
                            NR>1 {exit}' "$0"
                       exit 0 ;;
        *)             echo "opcion desconocida: $1 (usa --help)" >&2; exit 2 ;;
    esac
    shift
done

DC_ROOT=/opt/toolchains/dc
KOS=$DC_ROOT/kos
KOS_PORTS=$DC_ROOT/kos-ports
CHAIN=$KOS/utils/kos-chain
LOG="${HOME}/nfshs-kos-install-$(date +%Y%m%d-%H%M%S).log"
T0=$(date +%s)

# ---------------------------------------------------------------- utilidades

paso()  { echo; echo "=============== $* ==============="; }
info()  { echo "  $*"; }
error() {
    echo; echo "ERROR: $*" >&2
    [ -f "$LOG" ] && echo "El log completo esta en $LOG" >&2
    exit 1
}

transcurrido() {
    local s=$(( $(date +%s) - T0 ))
    printf '%dh %02dm %02ds' $((s/3600)) $(((s%3600)/60)) $((s%60))
}

# Corre un comando dejando su salida en el log y en pantalla, y aborta si falla.
run() {
    echo "+ $*" | tee -a "$LOG"
    "$@" 2>&1 | tee -a "$LOG"
    local rc=${PIPESTATUS[0]}
    [ "$rc" -eq 0 ] || error "fallo: $* (codigo $rc)"
}

# ---------------------------------------------------------- 0. comprobaciones

paso "0. Comprobando el entorno"

[ -n "${MSYSTEM:-}" ] || error "esto no parece un shell de MSYS2 (no hay \$MSYSTEM).
       Abre C:\\msys64\\ucrt64.exe y vuelve a lanzarlo."

# OJO: Git for Windows (Git Bash) tambien pone MSYSTEM=MINGW64, pero no es MSYS2: no
# tiene pacman ni /opt, y la instalacion fallaria a mitad con un error que no dice por
# que. Se distingue por pacman.
if ! command -v pacman >/dev/null || [ ! -f /etc/pacman.conf ]; then
    error "esto tiene \$MSYSTEM=$MSYSTEM pero no es MSYS2: no hay pacman.
       Casi seguro que es el Git Bash de Git for Windows, que no sirve para esto.
       Abre C:\\msys64\\ucrt64.exe (o mingw64.exe) y vuelve a lanzarlo desde ahi."
fi

case "$MSYSTEM" in
    UCRT64)  PKG=mingw-w64-ucrt-x86_64 ;;
    MINGW64) PKG=mingw-w64-x86_64 ;;
    *) error "estas en el entorno '$MSYSTEM', que no sirve.
       Usa UCRT64 (C:\\msys64\\ucrt64.exe) o MINGW64 (C:\\msys64\\mingw64.exe).
       UCRT64 es el que ya usa este proyecto para el build de x86; MINGW64 es el que
       documenta KallistiOS. Si la construccion falla en UCRT64, reintentar en MINGW64
       es la via documentada." ;;
esac
info "entorno MSYS2:    $MSYSTEM  (paquetes '$PKG-*')"
info "gcc del host:     $(gcc --version 2>/dev/null | head -1 || echo 'no hay todavia')"
info "raiz del SDK:     $DC_ROOT  (en Windows: C:\\msys64${DC_ROOT//\//\\})"

if [ -z "$JOBS" ]; then
    JOBS=$(nproc 2>/dev/null || echo 2)
fi
info "hilos:            $JOBS"

# Espacio libre: el arbol de fuentes mas los dos toolchains pide holgura.
AVAIL_KB=$(df -Pk "$([ -d /opt ] && echo /opt || echo /)" 2>/dev/null | awk 'NR==2 {print $4}')
if [ -n "${AVAIL_KB:-}" ]; then
    info "espacio libre:    $((AVAIL_KB/1024/1024)) GB en /opt"
    if [ "$AVAIL_KB" -lt 15000000 ]; then
        echo "  AVISO: menos de 15 GB libres. La construccion descarga y compila gcc dos"
        echo "         veces por arquitectura; con --distclean se recupera casi todo."
    fi
fi

# Existe ya algo?
if [ -x "$DC_ROOT/sh-elf/bin/sh-elf-gcc" ]; then
    info "ya instalado:     $("$DC_ROOT/sh-elf/bin/sh-elf-gcc" --version | head -1)"
fi

if [ "$CHECK_ONLY" -eq 1 ]; then
    paso "Solo comprobacion (--check). No se ha instalado ni descargado nada."
    echo "Si todo lo de arriba tiene sentido, relanza sin --check."
    exit 0
fi

mkdir -p "$(dirname "$LOG")" || true
echo "nfshs-dc: instalacion de KallistiOS, $(date)" > "$LOG"
echo "MSYSTEM=$MSYSTEM PKG=$PKG JOBS=$JOBS" >> "$LOG"
info "log:              $LOG"

# ------------------------------------------------- 1. paquetes de MSYS2

if [ "$SKIP_PACMAN" -eq 0 ]; then
    if [ "$DO_UPGRADE" -eq 1 ]; then
        paso "1a. Actualizando MSYS2 entero (--upgrade)"
        echo "  Si pide cerrar la terminal: cierrala, abrela de nuevo y relanza el script."
        run pacman -Syuu --noconfirm
    fi

    paso "1. Instalando los paquetes de compilacion"
    # La lista sale de utils/kos-chain/doc/mingw/mingw-w64.md de KallistiOS, con el
    # prefijo adaptado al entorno. base-devel, patch y diffutils son del lado MSYS;
    # el resto son del lado mingw. libpng/jpeg/freetype/freeglut no los necesita el
    # toolchain, los piden los addons y los ejemplos de KOS.
    # --noconfirm es necesario: `base-devel` es un grupo y si no pregunta por cada
    # paquete, y este script se lanza desatendido.
    run pacman -Sy --needed --noconfirm \
        base-devel patch diffutils git python \
        "$PKG-toolchain" "$PKG-autotools" "$PKG-texinfo" \
        "$PKG-libpng" "$PKG-libjpeg-turbo" "$PKG-libelf" \
        "$PKG-freetype" "$PKG-freeglut"
else
    paso "1. Paquetes de MSYS2: omitido (--skip-pacman)"
fi

for t in git python make gcc curl; do
    command -v "$t" >/dev/null || error "falta '$t' en el PATH tras instalar los paquetes."
done
info "git:    $(git --version)"
info "python: $(python --version 2>&1)"

# -------------------------------------------- 2. directorios y repositorios

paso "2. Creando los directorios y clonando KallistiOS"

run mkdir -p "$DC_ROOT"

# Se clona de GitHub por HTTPS. El wiki todavia dice `git://git.code.sf.net/...`, pero
# el protocolo git:// esta retirado y el desarrollo vive en GitHub.
clonar() {
    local url="$1" dir="$2"
    if [ -d "$dir/.git" ]; then
        info "ya existe $dir, actualizando"
        run git -C "$dir" pull --ff-only
    else
        run git clone "$url" "$dir"
    fi
}

clonar https://github.com/KallistiOS/KallistiOS.git "$KOS"
clonar https://github.com/KallistiOS/kos-ports.git  "$KOS_PORTS"

[ -d "$CHAIN" ] || error "no existe $CHAIN.
       KallistiOS ha movido el constructor de sitio. Mira que hay en $KOS/utils/
       y avisa, porque este script hay que ajustarlo."

info "constructor en:   $CHAIN"
info "perfil estable:   $(grep -hE '^(binutils|gcc|newlib)_ver=' \
                          "$CHAIN/profiles/dreamcast/stable.mk" | tr '\n' ' ')"

# ------------------------------------------------------ 3. los dos toolchains

# El Makefile de kos-chain lee Makefile.cfg si existe y, si no, Makefile.$(platform).cfg
# cuando se le pasa `platform=` por linea de comandos. Aprovechamos eso para no crear
# ningun Makefile.cfg: asi las dos arquitecturas salen de sus ficheros de fabrica y no
# hay un archivo local que se quede viejo. `makejobs` por linea de comandos pisa el
# valor vacio del .cfg, que compilaria en serie.
#
# Hacen falta las DOS: sh-elf para la CPU SH-4 y arm-eabi para el ARM7 del AICA (el
# procesador de sonido). KallistiOS no enlaza sin la segunda.

paso "3. Construyendo el toolchain SH-4 (sh-elf) - esto es lo que tarda"
info "binutils + gcc (dos pasadas) + newlib, con $JOBS hilos"
( cd "$CHAIN" && run make platform=dreamcast makejobs="-j$JOBS" )

paso "4. Construyendo el toolchain ARM del AICA (arm-eabi)"
( cd "$CHAIN" && run make platform=aica makejobs="-j$JOBS" )

for gcc_bin in "$DC_ROOT/sh-elf/bin/sh-elf-gcc" "$DC_ROOT/arm-eabi/bin/arm-eabi-gcc"; do
    [ -x "$gcc_bin" ] || error "no se genero $gcc_bin. Mira los logs en $CHAIN/logs/"
done

# ---------------------------------------------------------- 5. environ.sh

paso "5. Configurando el entorno de KallistiOS"

# doc/environ.sh.sample ya trae exactamente las rutas que usa este script
# (/opt/toolchains/dc/{kos,sh-elf,arm-eabi}) y KOS_SH4_PRECISION="-m4-single", que es
# lo que pide CLAUDE.md seccion 3. No hay nada que editar.
if [ -f "$KOS/environ.sh" ]; then
    info "ya existe $KOS/environ.sh, no se toca"
else
    run cp "$KOS/doc/environ.sh.sample" "$KOS/environ.sh"
    info "creado $KOS/environ.sh a partir del ejemplo (rutas por omision, -m4-single)"
fi

# ------------------------------------------------------ 6. compilar KallistiOS

paso "6. Compilando KallistiOS"
# shellcheck disable=SC1090
set +u                        # environ_base.sh lee variables no definidas
. "$KOS/environ.sh"
set -u
info "KOS_BASE=$KOS_BASE"
info "KOS_CC_BASE=$KOS_CC_BASE"
( cd "$KOS" && run make -j"$JOBS" )

[ -f "$KOS/lib/dreamcast/libkallisti.a" ] \
    || error "no se genero libkallisti.a. Mira el final de $LOG"

# ------------------------------------------------------------ 7. limpieza

if [ "$DO_DISTCLEAN" -eq 1 ]; then
    paso "7. Borrando los arboles de fuentes del toolchain (--distclean)"
    ( cd "$CHAIN" && make platform=dreamcast distclean 2>&1 | tee -a "$LOG" ) || true
    ( cd "$CHAIN" && make platform=aica      distclean 2>&1 | tee -a "$LOG" ) || true
fi

# ------------------------------------------------------------- 8. resumen

paso "LISTO en $(transcurrido)"

RESUMEN="$DC_ROOT/versiones-sdk.txt"
{
    echo "SDK de Dreamcast instalado el $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo "entorno MSYS2:  $MSYSTEM"
    echo "gcc del host:   $(gcc --version | head -1)"
    echo "sh-elf-gcc:     $("$DC_ROOT/sh-elf/bin/sh-elf-gcc" --version | head -1)"
    echo "sh-elf-ld:      $("$DC_ROOT/sh-elf/bin/sh-elf-ld" --version | head -1)"
    echo "arm-eabi-gcc:   $("$DC_ROOT/arm-eabi/bin/arm-eabi-gcc" --version | head -1)"
    echo "KallistiOS:     $(git -C "$KOS" describe --always --dirty 2>/dev/null) \
($(git -C "$KOS" log -1 --format=%cd --date=short))"
    echo "kos-ports:      $(git -C "$KOS_PORTS" describe --always 2>/dev/null) \
($(git -C "$KOS_PORTS" log -1 --format=%cd --date=short))"
    echo "perfil:         $(grep -hE '^(binutils|gcc|newlib)_ver=' \
                            "$CHAIN/profiles/dreamcast/stable.mk" | tr '\n' ' ')"
    echo "multilib SH-4:  $(grep -E '^precision_modes=' "$CHAIN/Makefile.dreamcast.cfg")"
} | tee "$RESUMEN"

cat <<EOF

Las versiones estan en $RESUMEN; pegalas en docs/hitos/M1b.md (el paso 0 pide anotarlas).

Para usar el toolchain en una sesion nueva, desde el shell $MSYSTEM:

    source /opt/toolchains/dc/kos/environ.sh

Comprobacion rapida de que compila para SH-4:

    source /opt/toolchains/dc/kos/environ.sh
    echo 'int main(void){return 0;}' > /tmp/t.c
    sh-elf-gcc -ml -m4-single -Os -c /tmp/t.c -o /tmp/t.o && sh-elf-size /tmp/t.o

Siguiente paso de M1b (ver docs/hitos/M1b.md): medir el tamano real del codigo
generado en SH-4, que es la cifra que decide si hace falta trocearlo en overlays.
La referencia medida en x86-64 es 7,59 MB con -O2 y 3,93 MB con -Os.
EOF
