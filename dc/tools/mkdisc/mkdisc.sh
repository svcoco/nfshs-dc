#!/bin/sh
# Construye la imagen de Dreamcast: ELF -> 1ST_READ.BIN -> IP.BIN -> ISO -> CDI (o GDI).
#
# Hace falta el entorno de KallistiOS (source /opt/toolchains/dc/kos/environ.sh) y
# xorriso (pacman -S xorriso). Lo demas (makeip, scramble, sh-elf-objcopy) lo trae KOS.
#
# El disco lleva el binario y los datos del juego. NO lleva la imagen original del CD
# (es de donde se extrajo todo) ni los FMV (el reproductor de video es M5).
#
#   mkdisc.sh --elf dc/build/dc/nfshs.elf --data gamedata --out dc/build/dc/disc
#   mkdisc.sh ... --format gdi    la otra disposicion (ver abajo)
#   mkdisc.sh ... --minimal       solo lo que la ejecucion medida toca (~90 MB)
#   mkdisc.sh ... --script dc/test/diff/scripts/into_race.txt   entrada guionizada
#   mkdisc.sh ... --video ntsc   fuerza el modo en vez de detectar el cable
#
# ---- las dos disposiciones, y por que CDI por omision ----
#
# CDI (MIL-CD, lo que se graba en un CD-R): una pista de audio muda y una de datos que
# empieza en el LBA 11702, con el 1ST_READ.BIN **cifrado** (scramble) y la IP.BIN
# diciendo CD-ROM. **Es la unica que arranca con la BIOS real**, comprobado en Flycast.
#
# GDI (GD-ROM): los datos en el area de alta densidad, desde el LBA 45000, sin cifrar y
# con la IP.BIN diciendo GD-ROM. Arranca solo con REIOS; con la BIOS real la consola
# reconoce el disco y se va al menu. Se conserva porque es la disposicion de un GD-ROM
# de verdad y la que usaria un ODE.
#
# En las dos, el ISO se construye con `-C 0,<LBA de la pista>` porque los extents de
# ISO9660 son absolutos y KOS los lee tal cual (ver docs/hitos/M1b.md).
set -eu

ELF=""; DATA="gamedata"; OUT=""; MINIMAL=0; SCRIPT=""; FORMAT="cdi"; VIDEO=""
KOS_BASE="${KOS_BASE:-/opt/toolchains/dc/kos}"
AQUI=$(dirname "$0")

while [ $# -gt 0 ]; do
    case "$1" in
        --elf)     ELF=$2; shift 2 ;;
        --data)    DATA=$2; shift 2 ;;
        --out)     OUT=$2; shift 2 ;;
        --script)  SCRIPT=$2; shift 2 ;;
        --format)  FORMAT=$2; shift 2 ;;
        --video)   VIDEO=$2; shift 2 ;;
        --minimal) MINIMAL=1; shift ;;
        *) echo "opcion desconocida: $1" >&2; exit 2 ;;
    esac
done

[ -n "$ELF" ] || { echo "falta --elf" >&2; exit 2; }
[ -n "$OUT" ] || { echo "falta --out (un directorio)" >&2; exit 2; }
[ -f "$ELF" ] || { echo "no existe $ELF" >&2; exit 1; }
[ -d "$DATA" ] || { echo "no existe el directorio de datos $DATA" >&2; exit 1; }

case "$FORMAT" in
    cdi) LBA=11702; DEVICE="CD-ROM1/1" ;;
    gdi) LBA=45000; DEVICE="GD-ROM1/1" ;;
    *) echo "--format tiene que ser cdi o gdi" >&2; exit 2 ;;
esac

MAKEIP="$KOS_BASE/utils/makeip/makeip.exe"
[ -x "$MAKEIP" ] || MAKEIP="$KOS_BASE/utils/makeip/makeip"
SCRAMBLE="$KOS_BASE/utils/scramble/scramble.exe"
[ -x "$SCRAMBLE" ] || SCRAMBLE="$KOS_BASE/utils/scramble/scramble"
[ -x "$MAKEIP" ]   || { echo "no encuentro makeip en $KOS_BASE/utils/makeip" >&2; exit 1; }
command -v xorriso >/dev/null || { echo "falta xorriso (pacman -S xorriso)" >&2; exit 1; }

mkdir -p "$OUT" "$OUT/tmp"
TMP="$OUT/tmp"

echo "+ 1ST_READ.BIN"
sh-elf-objcopy -R .stack -O binary "$ELF" "$TMP/plano.bin"
if [ "$FORMAT" = cdi ]; then
    # Arrancar desde CD exige el binario cifrado; desde GD-ROM, en claro.
    [ -x "$SCRAMBLE" ] || { echo "no encuentro scramble en $KOS_BASE/utils/scramble" >&2; exit 1; }
    "$SCRAMBLE" "$TMP/plano.bin" "$TMP/1ST_READ.BIN" >/dev/null
else
    cp "$TMP/plano.bin" "$TMP/1ST_READ.BIN"
fi
ls -l "$TMP/1ST_READ.BIN" | awk '{printf "  %s bytes\n", $5}'

echo "+ IP.BIN ($DEVICE)"
"$MAKEIP" -g "NFS HIGH STAKES" -c "nfshs-dc" -i "$DEVICE" -f "$TMP/IP.BIN" >/dev/null

echo "+ ISO (extents desde el LBA $LBA)"
# OJO: en xorriso los -m van ANTES de las rutas; detras no filtran nada y el disco se
# lleva la imagen original del CD entera (lo aprendimos generando 1,3 GB en vez de 552).
set -- -as mkisofs -C "0,$LBA" -G "$TMP/IP.BIN" -R -l \
       -m "Need for Speed*" -m "MOVIES" -o "$TMP/datos.iso" \
       -graft-points "/1ST_READ.BIN=$TMP/1ST_READ.BIN"
[ -n "$SCRIPT" ] && set -- "$@" "/SCRIPT.TXT=$SCRIPT"
# --video vga|ntsc|pal: fuerza el modo en vez de dejarlo a la deteccion del cable.
if [ -n "$VIDEO" ]; then
    echo "$VIDEO" > "$TMP/VIDEO.TXT"
    set -- "$@" "/VIDEO.TXT=$TMP/VIDEO.TXT"
fi
if [ "$MINIMAL" -eq 1 ]; then
    for f in NFS4.EXE FRONT.BIN ZZZFE.VIV ZZENGL.VIV ZZZZAMUS.VIV ZZZMSLKH.VIV \
             LICENSEA.DAT; do
        [ -f "$DATA/$f" ] && set -- "$@" "/$f=$DATA/$f"
    done
else
    set -- "$@" "/=$DATA"
fi
xorriso "$@" 2>&1 | grep -iE "^xorriso.*(FAILURE|SORRY)|produced" || true

if [ "$FORMAT" = cdi ]; then
    echo "+ CDI"
    python -I "$AQUI/mkcdi.py" "$TMP/datos.iso" "$OUT/nfshs.cdi"
    echo "listo: $OUT/nfshs.cdi   (arranca con la BIOS real)"
else
    echo "+ GDI (pistas de relleno de baja densidad y el indice)"
    cp "$TMP/datos.iso" "$OUT/track03.iso"
    i=0; : > "$OUT/track01.bin"
    while [ $i -lt 300 ]; do printf '%2352s' '' | tr ' ' '\000' >> "$OUT/track01.bin"; i=$((i+1)); done
    cp "$OUT/track01.bin" "$OUT/track02.raw"
    printf '3\n1 0 4 2352 track01.bin 0\n2 1798 0 2352 track02.raw 0\n3 %d 4 2048 track03.iso 0\n' \
        "$LBA" > "$OUT/nfshs.gdi"
    echo "listo: $OUT/nfshs.gdi   (solo arranca con REIOS)"
fi
