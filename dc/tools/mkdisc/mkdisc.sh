#!/bin/sh
# Construye la imagen de Dreamcast: ELF -> 1ST_READ.BIN -> IP.BIN -> ISO -> GDI.
#
# Hace falta el entorno de KallistiOS (source /opt/toolchains/dc/kos/environ.sh) y
# xorriso (pacman -S xorriso). Lo demas (makeip, sh-elf-objcopy) lo trae KOS.
#
# El disco lleva el binario y los datos del juego. NO lleva la imagen original del CD
# (es de donde se extrajo todo) ni los FMV (el reproductor de video es M5).
#
#   mkdisc.sh --elf dc/build/dc/nfshs.elf --data gamedata --out dc/build/dc/disc
#   mkdisc.sh ... --minimal      solo lo que la ejecucion medida toca (~90 MB)
#
# ---- por que la estructura es esta (ver docs/hitos/M1b.md) ----
#
# Un GD-ROM tiene dos areas. La de ALTA densidad empieza en el LBA 45000 y es la unica
# que da para 552 MB, asi que ahi va todo: IP.BIN en el area de sistema (los primeros
# 32 KB), 1ST_READ.BIN y los datos.
#
# El ISO se construye con -C 0,45000 porque KOS lee los extents tal cual y su biread()
# traduce a FAD sumando 150: los extents tienen que valer (FAD de la pista) - 150, que
# para una pista que empieza en el FAD 45150 es justo 45000.
#
# Las pistas 1 y 2 son relleno de la baja densidad: nadie las lee, pero el indice del
# GDI las exige.
set -eu

ELF=""; DATA="gamedata"; OUT=""; MINIMAL=0
KOS_BASE="${KOS_BASE:-/opt/toolchains/dc/kos}"
TRACK3_LBA=45000

while [ $# -gt 0 ]; do
    case "$1" in
        --elf)     ELF=$2; shift 2 ;;
        --data)    DATA=$2; shift 2 ;;
        --out)     OUT=$2; shift 2 ;;
        --minimal) MINIMAL=1; shift ;;
        *) echo "opcion desconocida: $1" >&2; exit 2 ;;
    esac
done

[ -n "$ELF" ] || { echo "falta --elf" >&2; exit 2; }
[ -n "$OUT" ] || { echo "falta --out (un directorio)" >&2; exit 2; }
[ -f "$ELF" ] || { echo "no existe $ELF" >&2; exit 1; }
[ -d "$DATA" ] || { echo "no existe el directorio de datos $DATA" >&2; exit 1; }

MAKEIP="$KOS_BASE/utils/makeip/makeip.exe"
[ -x "$MAKEIP" ] || MAKEIP="$KOS_BASE/utils/makeip/makeip"
[ -x "$MAKEIP" ] || { echo "no encuentro makeip en $KOS_BASE/utils/makeip" >&2; exit 1; }
command -v xorriso >/dev/null || { echo "falta xorriso (pacman -S xorriso)" >&2; exit 1; }

mkdir -p "$OUT"
TMP="$OUT/.tmp"; rm -rf "$TMP"; mkdir -p "$TMP"

echo "+ 1ST_READ.BIN"
sh-elf-objcopy -R .stack -O binary "$ELF" "$TMP/1ST_READ.BIN"
ls -l "$TMP/1ST_READ.BIN" | awk '{printf "  %s bytes\n", $5}'

# -i GD-ROM1/1 y no el CD-ROM1/1 por omision: el disco es un GD-ROM.
echo "+ IP.BIN"
"$MAKEIP" -g "NFS HIGH STAKES" -c "nfshs-dc" -i "GD-ROM1/1" -f "$TMP/IP.BIN" >/dev/null

echo "+ pista 3 (datos)"
set -- -as mkisofs -C "0,$TRACK3_LBA" -G "$TMP/IP.BIN" -R -l -m "Need for Speed*" -m "MOVIES" -o "$OUT/track03.iso" \
       -graft-points "/1ST_READ.BIN=$TMP/1ST_READ.BIN"

if [ "$MINIMAL" -eq 1 ]; then
    for f in NFS4.EXE FRONT.BIN ZZZFE.VIV ZZENGL.VIV ZZZZAMUS.VIV ZZZMSLKH.VIV \
             LICENSEA.DAT; do
        [ -f "$DATA/$f" ] && set -- "$@" "/$f=$DATA/$f"
    done
else
    set -- "$@" "/=$DATA"
fi
xorriso "$@" 2>&1 | grep -iE "^xorriso.*(FAILURE|SORRY)|produced" || true

echo "+ pistas 1 y 2 (relleno de baja densidad) y el indice"
: > "$OUT/track01.bin"; : > "$OUT/track02.raw"
i=0; while [ $i -lt 300 ]; do printf '%2352s' '' | tr ' ' '\000' >> "$OUT/track01.bin"; i=$((i+1)); done
cp "$OUT/track01.bin" "$OUT/track02.raw"
printf '3\n1 0 4 2352 track01.bin 0\n2 1798 0 2352 track02.raw 0\n3 %d 4 2048 track03.iso 0\n' \
    "$TRACK3_LBA" > "$OUT/nfshs.gdi"

rm -rf "$TMP"
echo "listo: $OUT/nfshs.gdi  ($(du -m "$OUT/track03.iso" | cut -f1) MB de datos)"
