# Auditoría de arranque CDI / GDEMU — 2026-10-30

**Actualización posterior:** Jorge confirmó el 2026-10-30 que funcionó la imagen
entregada en su Dreamcast con GDEMU. No especificó cuál de las dos variantes
probó. La receta para próximos agentes está en [BUILD_CDI_GDEMU.md](BUILD_CDI_GDEMU.md).
Las observaciones de «pendiente de prueba» que siguen describen el estado al
cerrar la auditoría inicial, antes de esa confirmación.

## Resultado y alcance

El usuario confirmó que ejecuta los CDI mediante **GDEMU**, sin grabar un CD-R:
aparece el logo Dreamcast y después el reproductor de audio con dos pistas.
Esto cambia el diagnóstico: exceder 80 minutos impide usar un CD-R estándar,
pero no demuestra la causa del fallo en GDEMU.

**Se encontró y corrigió una incoherencia real en el descriptor global del CDI.**
Su impacto exacto sobre el firmware de GDEMU queda pendiente de prueba física.
No se ha verificado el arranque en consola ni se atribuye el fallo definitivamente
al tamaño del sector. Ver dos pistas en el reproductor tampoco prueba por sí solo
que ambas estén marcadas como audio.

## Medidas de las imágenes originales

| Campo | 480p | 720p |
|---|---:|---:|
| Sectores de datos | 370640 | 370641 |
| Tamaño almacenado del sector | 2048 | 2048 |
| LBA de datos | 11702 | 11702 |
| Pregap de datos | 150 | 150 |
| Final global declarado (FAD) | 18707 | 18707 |
| Final global correspondiente a las pistas | 382492 | 382493 |
| Archivos ISO | 756 | 757 |

Ambos contenedores declaran dos sesiones: audio de 302 sectores y datos Mode 2.
Las longitudes de pista coinciden con los bytes del archivo. El ISO, sus directorios
y los extents de todos los archivos están dentro de la pista de datos.
IP.BIN identifica CD-ROM1/1, regiones JUE y 1ST_READ.BIN. El descriptor global aún
tenía el nombre **240pSuiteDC**, además del final incorrecto. La etiqueta por sí sola
no se considera un impedimento de arranque.

La fórmula del final se contrastó con `get_total_cdi_space_used()` de cdi4dc y con
una imagen producida por su código: LBA + longitud de pista + 150.
Los informes originales están en `dc/build/disc-audit/original480.json` y
`original720.json`.

## Requisitos contrastados

- Homebrew MIL-CD: binario SH-4 en formato binario, scramble de 1ST_READ.BIN,
  bootstrap IP.BIN en los primeros 16 sectores de la pista de datos y nombre de
  arranque coincidente con un archivo de la raíz del ISO.
- En esta disposición Audio/Data, el ISO debe construirse con `-C 0,11702`.
  Ese valor depende de la disposición; no es una constante universal de Dreamcast.
- cdi4dc Audio/Data usa sectores XA Mode 2 Form 1 de 2336 bytes, pregap de 150,
  dos sectores finales adicionales y descriptores coherentes.
  No se encontró evidencia primaria suficiente para afirmar que GDEMU rechaza
  universalmente los CDI Mode 2/2048.
- GDEMU admite CDI y exige una carpeta numérica por imagen y nombre `disc.cdi`
  (DOS 8.3). Usar una carpeta libre en la secuencia existente de la tarjeta.
- Un CD-R de 80 minutos tiene un presupuesto aproximado de 360000 sectores.
  Estas imágenes necesitan aproximadamente 84.97 minutos: para grabarlas habría
  que reducir contenido. Convertir 2048 a 2336 no reduce esa duración.
- El modo 720p del proyecto es experimental y se activa con cable VGA; la prueba
  inicial debe hacerse con la versión normal para separar vídeo de arranque.

Fuentes primarias consultadas:

- [cdi4dc / IMG4DC, código y documentación](https://github.com/sizious/img4dc)
- [makeip, formato y opciones de bootstrap](https://github.com/Dreamcast-Projects/makeip)
- [GDEMU: formatos y nombres de archivos](https://gdemu.wordpress.com/details/gdemu-details/)
- [Autor de GDEMU: CDI para homebrew/MIL-CD](https://gdemu.wordpress.com/2020/04/16/stayin-alive-contd/)
- [Flycast: lectura de pistas CDI](https://github.com/flyinghead/flycast/blob/master/core/imgread/cdi.cpp)

## Entregables

- **`dc/build/gdemu/480p/disc.cdi`**: compilación normal actual, detección de cable.
- **`dc/build/gdemu/720p/disc.cdi`**: compilación actual con VIDEO720=1.
- `dc/build/disc-audit/reference480.cdi`: prueba de control, empaquetada con cdi4dc
  a partir del ISO original 480p; conserva exactamente su ejecutable y datos.
  Si se usa en GDEMU, copiar como `disc.cdi` en otra carpeta numérica.

Los originales no se reemplazaron. Los nuevos contienen todos los datos del juego,
películas y assets horneados; no se recortó contenido. Copiar solamente el CDI a
la carpeta de GDEMU, no el directorio de temporales ni sus BIN/ISO.

Los dos CDI nuevos usan cdi4dc, con final global 382494 y sectores de 2336 bytes.
El aumento del tamaño del archivo corresponde a subcabeceras y EDC/ECC.

## Validación realizada

- Cifras M0: 3674 funciones, 245518 instrucciones, HLE 119/119 y hooks 53/53.
- SHA-1 de NFS4.EXE y FRONT.BIN coincide con CLAUDE.md.
- Compilación y enlace de ambas variantes con KallistiOS: correctos.
  Se conservó la rama `exp/720p-pvr` y los cambios locales existentes.
- Comparación de **todos los 370640 sectores de datos** de cada CDI contra su ISO:
  idénticos. Bootstrap y ejecutable coinciden con los usados al empaquetar.
- En cada imagen: **146 sectores muestreados**, incluidos los primeros 18,
  con CRC EDC calculado bit a bit y síndromes Reed-Solomon P/Q independientes: correctos.
- Longitudes de pistas, final global, raíz ISO, extents y archivo de arranque: correctos.
- Dos pruebas de regresión del generador Python: correctas; comprueban el descriptor
  global con tres longitudes de nombre y el rechazo de tamaño de sector no admitido.
- No se ejecutaron estas nuevas imágenes en Flycast ni en una Dreamcast durante
  esta auditoría. Las pruebas estructurales no sustituyen la prueba física.

Informes y hashes: `dc/build/gdemu/{480p,720p}/audit.json` y
`dc/build/disc-audit/verification.json`.

## Reproducción

Se descargó IMG4DC a `dc/build/disc-audit/reference/img4dc`, fijado al SHA del
archivo `reference/img4dc-tree.json`. Se compiló su escritor Audio/Data con un
controlador pequeño (`reference/driver.c`) que proporciona la función ausente
`fsize_stream`, suprime el progreso por sector y evita el buffer inseguro para
la etiqueta del main original. No cambia el codificador de sectores ni descriptores.
El ejecutable local es `dc/build/disc-audit/cdi4dc.exe`.

Desde MSYS2, con `/ucrt64/bin` en PATH y el entorno KOS cargado:

```sh
make -C dc dc -j8
make -C dc dc VIDEO720=1 -j8
sh dc/tools/mkdisc/mkdisc.sh --elf dc/build/dc/nfshs.elf --data gamedata \
  --out dc/build/gdemu/480p --cdi-tool dc/build/disc-audit/cdi4dc.exe
sh dc/tools/mkdisc/mkdisc.sh --elf dc/build/dc720/nfshs.elf --data gamedata \
  --out dc/build/gdemu/720p --cdi-tool dc/build/disc-audit/cdi4dc.exe
```

El script produce `nfshs_<yyyyMMddHHmm>.cdi`; renombrar a `disc.cdi` para GDEMU.
También se corrigió el final global y la etiqueta en `mkcdi.py`, se añadió el
auditor `audit_cdi.py`, y `mkdisc.sh` ahora se detiene si falla xorriso (antes
ocultaba el error y podía empaquetar un ISO viejo). Sin `--cdi-tool`, continúa
disponible el generador Python; no es el utilizado para los CDI entregados.

**Siguiente comprobación:** cargar primero el CDI nuevo de 480p. Si sigue apareciendo
el reproductor de audio, probar el CDI de control y registrar modelo/firmware de GDEMU,
BIOS de la consola y si otro homebrew MIL-CD conocido arranca en la misma instalación.
