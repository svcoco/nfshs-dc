# Build CDI para Dreamcast real con GDEMU

## Estado confirmado

**El 2026-10-30 Jorge confirmó que funcionó el CDI entregado con este método.**
La confirmación no especificó si probó 480p, 720p o ambas: no dar por validadas
ambas modalidades de vídeo ni afirmar que se probó un CD-R físico.

Esta es la receta que debe seguir el agente para los próximos builds. El método
validado usa **el escritor Audio/Data de cdi4dc con el controlador local**, no
el generador Python `mkcdi.py`, aunque este último también recibió correcciones.

Repositorio de trabajo en Windows:

```text
C:\Users\jafvi\Documents\Dreamcast-dev\nfshs-dc
```

Datos originales: `gamedata/`. Código del port: `dc/`. Assets horneados:
`dc/build/baked/`. No modificar los datos ni recortar contenido para reproducir
este build. Leer `CLAUDE.md` y comprobar el estado de Git antes de trabajar;
conservar los cambios locales. La rama durante la prueba fue `exp/720p-pvr`.

## 1. Preparar el entorno

Ejecutar los comandos siguientes en **MSYS2 Bash**, no directamente en PowerShell:

```sh
set -eu
export PATH=/ucrt64/bin:/usr/bin:$PATH
cd /c/Users/jafvi/Documents/Dreamcast-dev/nfshs-dc
source /opt/toolchains/dc/kos/environ.sh

test -d gamedata
test -d dc/build/baked
test -x dc/build/disc-audit/cdi4dc.exe
command -v xorriso
command -v sh-elf-objcopy
command -v python
git status --short
python -I dc/tools/recomp/m0_stats.py
```

La comprobación M0 debe dar 3674 funciones, 245518 instrucciones, HLE 119/119 y
hooks 53/53 para estos datos. Si no coincide, investigar antes de empaquetar.

En esta máquina Bash está en `C:\msys64\usr\bin\bash.exe`. Si se invoca con
`-lc`, establecer el PATH **dentro** de Bash, antes de cargar KOS. Omitir
`/ucrt64/bin` produjo un error por `libwinpthread-1.dll` y una detección incorrecta
del ABI. No reinstalar el SDK para resolver ese error de PATH.

Si falta `cdi4dc.exe`, reconstruirlo con la sección 5. No sustituirlo
silenciosamente por otra herramienta o por el generador Python.

## 2. Compilar y empaquetar

Usar un directorio nuevo por ejecución para conservar las imágenes que ya
funcionan. Los siguientes comandos compilan las dos variantes:

```sh
make -C dc dc -j8
make -C dc dc VIDEO720=1 -j8

BUILD_RUN="dc/build/gdemu/run-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BUILD_RUN"

sh dc/tools/mkdisc/mkdisc.sh \
  --elf dc/build/dc/nfshs.elf \
  --data gamedata \
  --baked dc/build/baked \
  --out "$BUILD_RUN/480p" \
  --cdi-tool dc/build/disc-audit/cdi4dc.exe

sh dc/tools/mkdisc/mkdisc.sh \
  --elf dc/build/dc720/nfshs.elf \
  --data gamedata \
  --baked dc/build/baked \
  --out "$BUILD_RUN/720p" \
  --cdi-tool dc/build/disc-audit/cdi4dc.exe

mv "$BUILD_RUN"/480p/nfshs_*.cdi "$BUILD_RUN/480p/disc.cdi"
mv "$BUILD_RUN"/720p/nfshs_*.cdi "$BUILD_RUN/720p/disc.cdi"
```

No agregar `--minimal`, `--script`, `--video` ni `--format gdi` para reproducir
la configuración entregada. La versión normal detecta el cable; 480p requiere
VGA. El modo experimental 720p también depende del cable VGA.
Si cambian assets o sus herramientas, regenerarlos siguiendo el proceso del
proyecto antes de empaquetar: estos comandos reutilizan los assets horneados.

`mkdisc.sh` realiza esta cadena:

1. ELF → binario con `sh-elf-objcopy -R .stack -O binary`.
2. Scramble del binario → `1ST_READ.BIN`.
3. IP.BIN mediante makeip, dispositivo `CD-ROM1/1` y nombre de arranque
   `1ST_READ.BIN`.
4. ISO con xorriso, bootstrap `-G IP.BIN` y multisesión **`-C 0,11702`**.
5. ISO → CDI con el cdi4dc local: audio + datos XA Mode 2 Form 1 de **2336 bytes**,
   pregap y sectores finales de cdi4dc.
6. Auditoría del contenedor. Un error de xorriso o del auditor detiene el proceso.

## 3. Verificar y entregar

Tras renombrar, actualizar los informes para que indiquen el nombre final:

```sh
for variant in 480p 720p; do
  python -I dc/tools/mkdisc/audit_cdi.py \
    "$BUILD_RUN/$variant/disc.cdi" \
    --report "$BUILD_RUN/$variant/audit.json"
  sha256sum "$BUILD_RUN/$variant/disc.cdi" > "$BUILD_RUN/$variant/SHA256SUMS.txt"
done
python -I dc/test/mkdisc/test_cdi.py
```

Comprobar en ambos informes:

- `sessions`: 2; pista 1 audio (`mode: 0`), pista 2 datos (`mode: 2`).
- Pista de datos: `sector_bytes: 2336`, `lba: 11702`, `pregap: 150`.
- `payload_matches_descriptors: true` y `disc_end_matches: true`.
- IP.BIN identifica CD-ROM1/1, JUE y 1ST_READ.BIN; ese archivo existe en la raíz.
- El final global se calcula desde la pista; **no fijarlo a un número constante**.
  En los builds entregados fue 382494, pero cambiará si cambia el contenido.

El auditor comprueba estructura y extents, no todos los códigos EDC/ECC. En la
auditoría inicial se compararon todos los sectores de datos con sus ISO y se
verificaron EDC/ECC de 146 sectores por imagen con el verificador independiente
de `dc/test/mkdisc/verifica_xa.py`. El script adicional que realizó esa comprobación
es `dc/build/disc-audit/verify_delivery.py`; usa las rutas originales de entrega,
por lo que hay que adaptar sus rutas para verificar otra ejecución.

Entregar al usuario las rutas absolutas de los dos CDI y sus informes.
Para GDEMU, copiar **solo `disc.cdi`** de cada variante a una carpeta numérica
distinta, libre dentro de la organización existente de la SD. No copiar `tmp/`,
`datos.iso`, IP.BIN ni 1ST_READ.BIN junto al CDI. No sobrescribir otras imágenes.

La compilación y la auditoría estructural no prueban automáticamente el
funcionamiento de una versión nueva en hardware. Registrar aparte la prueba
física del usuario. Para CD-R se necesita además reducir el tamaño: el build
completo validado supera los 80 minutos, lo cual no impidió usarlo en GDEMU.

## 4. Qué no se debe volver a introducir

Los CDI anteriores conservaban en el descriptor global el final **18707** y la
etiqueta **240pSuiteDC**, aunque sus pistas terminaban aproximadamente en 382492.
No reutilizar una cabecera de otro disco sin actualizar todos sus campos.
El método que funcionó cambió tanto el empaquetado como los sectores a 2336:
no afirmar que se aisló cuál de esos cambios resolvió por sí solo el fallo.

No aceptar «funciona en Flycast» como sustituto de arranque en GDEMU.
No cambiar el LBA 11702 sin cambiar toda la disposición y reconstruir el ISO.
No volver a ocultar errores de xorriso con una tubería terminada en `|| true`.

## 5. Recuperar la herramienta local si se borró `dc/build/`

El directorio `dc/build/` está ignorado por Git. El ejecutable y sus fuentes de
referencia pueden desaparecer al limpiar builds. Esta sección permite recuperarlos.

Fuentes: [sizious/img4dc](https://github.com/sizious/img4dc). El árbol de fuentes
usado en la prueba tiene SHA Git **`20d1c1489102bef780a4e9bbd888c258acd6d1e4`**.
Es el identificador del árbol, no se debe asumir que sea un commit.

Si no se conserva `dc/build/disc-audit/reference/img4dc`, clonar el repositorio en
esa ruta. Comprobar `git rev-parse 'HEAD^{tree}'` desde el clon: debe coincidir
con el SHA anterior. Si no coincide, localizar en el historial el commit cuyo
árbol sí coincida y usar ese commit. No actualizar silenciosamente las fuentes.
Los directorios necesarios son `cdi4dc/`, `common/` y `edc/`.

Guardar el siguiente controlador como
`dc/build/disc-audit/reference/driver.c` (es el utilizado en la prueba):

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned long fsize_stream(FILE *f) {
    long at=ftell(f), n; fseek(f,0,SEEK_END); n=ftell(f); fseek(f,at,SEEK_SET); return n;
}
void writing_data_track_event(unsigned long a,unsigned long b) {(void)a;(void)b;}
void padding_event(int n) {(void)n;}
void get_full_filename(char *a,char *b) {(void)a;(void)b;}
int write_cdi_audio_track(FILE *f);
int write_gap_tracks(FILE *f);
int write_data_track(FILE *f,FILE *i);
int get_total_cdi_space_used(unsigned long n);
int write_audio_cdi_header(FILE *f,char *name,char *volume,long n,long total);
int main(int argc,char **argv) {
    FILE *i,*o; int n; char volume[33];
    if(argc!=3) return 2;
    i=fopen(argv[1],"rb"); o=fopen(argv[2],"wb"); if(!i||!o)return 1;
    fseek(i,0x8028,SEEK_SET); if(fread(volume,1,32,i)!=32)return 1;
    volume[32]=0; rewind(i);
    write_cdi_audio_track(o); write_gap_tracks(o); n=write_data_track(o,i);
    write_audio_cdi_header(o,"disc.cdi",volume,n,get_total_cdi_space_used(n));
    fclose(i); if(fclose(o))return 1; printf("cdi4dc: %d sectors\n",n); return 0;
}
```

Desde la raíz del proyecto, compilar con el **GCC de Windows UCRT64** (no con
sh-elf-gcc ni con un GCC de Linux, donde los tamaños de `long` pueden diferir):

```sh
(
  cd dc/build/disc-audit/reference/img4dc
  /ucrt64/bin/gcc -std=gnu89 -O2 -DWIN32 -DHAVE_MEMMOVE \
    -include stdlib.h -include string.h -include cdi4dc/inc/tools.h \
    -Icdi4dc/inc -Icommon/inc -Iedc/inc \
    ../driver.c cdi4dc/src/common.c cdi4dc/src/cdiaudio.c cdi4dc/src/tools.c \
    edc/src/edc_ecc.c edc/src/libedc.c edc/src/patch.c \
    -o ../../cdi4dc.exe
)
test -x dc/build/disc-audit/cdi4dc.exe
```

El controlador sustituye únicamente el main y funciones auxiliares: proporciona
`fsize_stream`, suprime el progreso por sector y evita el buffer inseguro de la
etiqueta del main original. Los escritores de sectores y descriptores son los
de cdi4dc. No compilar `cdi4dc/src/main.c` junto a este controlador.

Para el detalle de las mediciones iniciales, consultar
[auditoria_CDI_GDEMU.md](auditoria_CDI_GDEMU.md).
