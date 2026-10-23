# M2b: la pista y el cielo en el PVR

**Criterio de salida (CLAUDE.md §4):** una pista recorrible; ms/frame y poligonos medidos.

Es el hito que convierte el negro de la carrera en algo mirable. Hasta M2a el port dibuja
el 2D del front end; en carrera la lista de dibujo sale **vacia** (0 cuadros en el frame
900) porque la geometria del mundo no pasa por el OT: `Render_RenderPlayerView`
(`0x800B4024`) es HLE y el juego nunca llega a construirla.

## 1. El dimensionado, medido antes de disenar

### Lo que el juego pide por frame

`dc/test/diff/scripts/race_long.txt`, 5 000 frames de carrera, host de PC:

| | |
|---|---|
| Vistas de carrera preparadas | 4 769 |
| **Trozos de pista por vista** | **media 8,4, maximo 18** |
| Llamadas a `LoadImage` | 4 071 |
| Area de pantalla en carrera | **320x240** (el front end es 512x240) |

### Lo que hay en una pista

`ZTR00.GRP` (448 064 bytes), con `nfshs dump-track` y `NFSHS_BLOCKS=all`:

| | |
|---|---|
| Nodos / bloques | 129 / 129 |
| Texturas | 374 |
| Muestras de la carretera virtual | 1 029 |
| Cuadros de suelo (`ground`) | **16 610** (media 129 por bloque) |
| Cuadros de suelo lejano (`far`, el LOD) | 5 679 (media 44) |
| Cuadros de detalle (`detail`) | 1 436 (media 11) |
| Colocaciones de objetos (`placements`) | 1 089 (media 8,4) |

`far` **no se suma**: es la version de bajo detalle de `ground` para los bloques lejanos,
y el juego dice cual quiere en la propia lista de trozos.

### Y de ahi, el presupuesto

| | media | pico |
|---|---|---|
| Bloques por vista | 8,4 | 18 |
| Cuadros a pintar | ~1 180 | ~2 520 |
| **Triangulos a pintar** | **~2 360** | **~5 040** |

**El PVR no es el problema.** Un Dreamcast mueve del orden de 3 millones de poligonos por
segundo, o sea unos 50 000 por frame a 60 Hz: el pico de la pista es el **10 %** de eso.
El coste real va a estar en el **SH-4**, que tiene que transformar unos 5 000-10 000
vertices por frame *ademas* de correr la logica recompilada, y en los fallos de I-cache
que ya pesan con 3,43 MB de codigo.

**Y la pista entera cabe en RAM.** 18 046 cuadros son del orden de 200 KB de caras mas su
charco de vertices: **no hace falta streaming**, se carga la pista al entrar y se queda.
Eso quita de encima el problema mas caro que tendria el hito.

## 2. Las dos decisiones que no son obvias

### De donde sale la geometria: del asset, no de la RAM del juego

El juego **no** construye la malla del mundo en su RAM: solo dice **que bloques** se ven y
con **que nivel de detalle** (hasta 64 entradas en `0x801102B0`, pares de `i16` id y `u16`
detalle). La malla esta en el GRP. La referencia la parsea con `src/formats/grp.rs` y el
port la **hornea en el PC** (CLAUDE.md §3), que es la decision vigente.

### De donde salen las texturas: del PSH horneado

Medido: el juego llama a `LoadImage` 4 071 veces en una carrera, asi que **sus texturas si
pasan por la VRAM de PS1** y la cache de M2a las vera. Pero los cuadros del GRP referencian
su textura por **indice en la tabla del GRP** (`Trk_Material`), que apunta a una forma del
PSH; para resolverlo por la VRAM haria falta la tabla forma -> pagina que el juego arma al
cargar, y eso es ingenieria inversa que no esta hecha.

Por el PSH esta resuelto: es el camino que la referencia ya demuestra y la decision de §3
ya lo fija. **Las texturas de pista se hornean**; la cache de VRAM de M2a se queda para el
2D, que es lo que graba.

## 3. Orden de trabajo

| # | Paso | Cierra con |
|---|---|---|
| 1 | **Captura de la vista** en el host de C: camara, matriz, proyeccion, rectangulo y lista de trozos | **Hecho (2026-10-23)**: la captura del port y la de la referencia **coinciden** en el guion de carrera |
| 2 | **Horneado de la pista** (PC, Rust, reutilizando `src/formats`): GRP + PSH -> malla y texturas listas para el PVR | tamano del horneado y de su atlas, medidos |
| 3 | **Transformacion en el SH-4** (FTRV) y **recorte por plano cercano** | una pista en pantalla; ms/frame y triangulos medidos |
| 4 | **El cielo** | el horizonte se ve |

El paso 1 va primero por la misma razon que en M2a: es **codigo portable y tiene
oraculo**. La referencia graba exactamente lo mismo en su `ViewCapture`, asi que se puede
comparar campo a campo dentro de `make diff`, sin tocar un Dreamcast y sin mirar una
pantalla. Del paso 2 en adelante ya hay que mirar.

### Que es la captura de la vista (paso 1)

Es poco codigo y esta todo localizado. La referencia lo lee asi (`src/ps1/host/views.rs`):

| Campo | De donde |
|---|---|
| `camera_pos[3]` | `RENDER_VIEW + 8`, tres `i32` |
| `camera_rot[9]` | `RENDER_VIEW + 0x14`, nueve `i32` |
| `projection` | `CAMERA_ZOOM` |
| `rect[4]` | `Draw_gViews[v]`: el recorte relativo al de la vista 0, y su tamano |
| `chunks` | `0x801102B0`, pares (`i16` bloque, `u16` detalle) |

La **cuenta** de trozos ya la calculaba el port (`race_view_logic` en `host/game.c`);
lo que faltaba era quedarse con la lista y con la camara.

**Cerrado el 2026-10-23 con oraculo:**

```
vista de carrera:
  C: 900 frames, referencia: 900 frames (900 en comun)
OK: 669 vistas identicas en 900 frames
```

Los dos lados vuelcan la vista por frame en el mismo formato (`NFSV`) y
`dc/test/diff/cmpview.py` las compara campo a campo dentro de `make diff`: jugador,
recorte, posicion, los nueve enteros de la matriz, proyeccion y la lista entera de
trozos con su nivel de detalle. En un guion de front end no hay ninguna vista, asi que
los dos lados salen vacios y el paso no estorba.

**Deuda que este guion destapo, anotada y no olvidada:** en los frames **126 a 130**, que
son los menus justo antes de entrar a la carrera, la lista 2D del port tiene **dos cuadros
menos** que la de la referencia (239 contra 241). No lo veia nadie porque
`front_end_120.txt` no llega hasta ahi. No bloquea M2b —es 2D de menus, no mundo—, pero
hay que mirarlo.

## 4. Riesgos propios de M2b

- **El SH-4 y los 64 bits.** La camara viene en enteros de PS1 y la transformacion natural
  del PVR es en `float` con FTRV. Hay que fijar el convenio una sola vez y medirlo, no
  mezclar.
- **I-cache.** El bucle de transformacion compite con 3,43 MB de codigo recompilado. Es el
  riesgo de CLAUDE.md §11 y aqui es donde se va a notar por primera vez.
- **Recorte por plano cercano.** El PVR no recorta: hay que partir los triangulos que
  cruzan el plano, en el SH-4, o se cuelan vertices detras de la camara.
- **El orden.** La pista es opaca y va a la lista `OP` con Z-buffer, al reves que el 2D de
  M2a, que es el algoritmo del pintor en la lista `TR`. Son dos regimenes distintos en el
  mismo frame.
