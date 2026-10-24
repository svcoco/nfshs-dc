# Estrategia del port: por que los hitos van en este orden

CLAUDE.md §4 dice **qué** hace cada hito y con qué criterio se cierra. Este archivo dice
**por qué en ese orden**, y recoge lo que se ha aprendido al cerrar cada uno, para no
repetir discusiones ya tenidas. Se actualiza al cerrar un hito (regla de CLAUDE.md §10).

## La idea que sostiene todo el plan

Un port así puede fallar por dos razones muy distintas: porque **la traducción del código
es incorrecta**, o porque **el hardware de destino no da**. Si se atacan las dos a la vez,
cada fallo es ambiguo y se investiga a ciegas.

Por eso el plan las separa en el tiempo:

- **M0** responde «¿cabe?» sin escribir nada: cuenta funciones, instrucciones y RAM.
- **M1a** responde «¿la traducción es correcta?» **en x86**, donde hay depurador, memoria
  de sobra y un oráculo ejecutable (la referencia Rust). No hay Dreamcast por ningún lado.
- **M1b** responde «¿compila y arranca en SH-4?» con la traducción **ya demostrada
  correcta**, así que cualquier divergencia nueva es del cambio de arquitectura.
- **M2** añade el renderer, que es trabajo nuevo y no una traducción.
- **M3** es el único hito donde el rendimiento es el criterio.

El coste de este orden es que se tarda más en ver algo en pantalla. El beneficio quedó
demostrado en M1a, y está documentado abajo.

## Lo que se aprendió en M0 (cerrado 2026-10-18)

Dos cifras cambiaron decisiones, y una pregunta estaba mal planteada:

- **La RAM entra, pero justa.** En carrera el juego usa ~1,84 MB de los 2 MB. Y la **pila
  usa solo 12 KB**, que es un dato que abarata M1b.
- **La I-cache de 8 KB es el riesgo real**, no la RAM.
- **«¿A cuántos fps va el original?» era la pregunta equivocada.** El juego no se marca el
  ritmo con `VSync(N)`: la cadencia la impone el host, y su lógica apunta a 60 Hz con un
  flip por campo. El fps del original es un hecho del hardware de PS1 y no dice nada sobre
  lo que puede dar el Dreamcast. De ahí la decisión de §3: 30 fps sostenidos como mínimo,
  sin tope artificial por arriba.

## Lo que se aprendió en M1a (cerrado 2026-10-18)

**El test diferencial se pagó solo, y por una razón concreta: los cinco bugs que encontró
eran invisibles ejecutando el juego.** El juego funcionaba con todos ellos puestos. Habrían
aparecido en M2 o M3 como síntomas raros, a mucha distancia de su causa.

Y hay un patrón en los cinco: **ninguno estaba en la recompilación ni en el GTE**. Todos
estaban en la **integración host/juego** — numeración de handles de archivo, el concepto de
modo (front end contra carrera), el marco anidado de captura, el orden del volcado y la
frontera de frame según el modo. Eso dice algo útil para el resto del port: la traducción
de MIPS a C es la parte **mecánica y fiable**; el riesgo vive en la frontera entre el juego
y lo que le ponemos debajo. M2 es toda esa frontera.

**El método que funcionó: contar y bisecar, no mirar.** Cada herramienta nueva descartó un
espacio entero de hipótesis con una medición, en vez de con una lectura de código:
contadores por frame, tabla de llamadas por dirección, orden de primera aparición, ventana
de la secuencia de llamadas, y al final bisección de la RAM por número de llamada, que
aisló **cuatro bytes**. Tres hipótesis mías (el IRQ del timer, la paridad de los backends y
una sobre el orden de volcado) murieron por medición; dos resultaron ser defectos del
propio instrumento, no del port.

**Dónde se cortó y por qué.** M1a cierra con 120 frames idénticos en el front end y 231 en
la carrera, no con la carrera completa. Lo que falta es el manejador de
`Render_RenderPlayerView`, que concentra **18 de las 33 llamadas anidadas** de la capa de
observación: es la grabación de geometría, y es exactamente lo que M2b y M2c escriben de
todas formas con el PVR delante. Portarla solo para el test sería andamio que M2a
reemplaza. La decisión se tomó **con el coste medido**, no a priori: el tramo anterior (el
showroom) costó ~170 líneas y 3 ciclos de medición para avanzar 47 frames, lo que dejó ver
que el siguiente paso era de otro tamaño.

**Lo que sobrevive a M1a:** el arnés diferencial no se tira. Queda como oráculo reutilizable
para comparar cualquier cambio futuro del runtime o del backend contra la referencia Rust,
con escenarios reproducibles (`front_end_120.txt`, `into_race.txt`) y herramientas de
diagnóstico (`cmp.py`, `bisect.py`, `screens.py`).

**Una lección sobre las conveniencias del host de la referencia.** nfshs-rs trae mejoras
que no son del juego original (desbloquear todo, un cono de visibilidad de coches más
ancho). Para comparar hay que neutralizarlas **en los dos lados**. Pero `unlock_everything`
enseñó que no se puede juzgar una de ellas sin medirla: parecía cosmética y resultó que
**da acceso a carreras**. La regla que queda: cada mejora de la referencia se porta detrás
de una opción apagada por omisión, y si va a estar en el Dreamcast es una decisión de
producto de M5, no de implementación.

## Lo que se aprendió en M1b (cerrado en Flycast, 2026-10-23)

Tres cosas, y ninguna era la que se esperaba.

**El riesgo que daba miedo no existía.** El tamaño del código era el riesgo número uno
—en Dreamcast el `1ST_READ.BIN` se carga entero en RAM— y salió en **3,43 MB con `-Os`**,
un 8,5 % por debajo de x86-64 y dentro de la estimación de M0. No hacen falta overlays.
Elegir `-Os` o `-O2` pasó a ser cuestión de velocidad, no de espacio.

**Los problemas de verdad fueron de integración, otra vez.** Igual que en M1a, donde los
cinco bugs estaban en el host y no en la recompilación. En M1b: una regla de Makefile sin
dependencias de cabeceras que dejaba a los módulos del host viendo **layouts distintos
del mismo struct** sin dar un error; `host_frame_top` que no avanzaba el reloj y
congelaba la carrera; y un descriptor de CDI medido desde el sitio equivocado que hacía
que la consola tomara el disco por un CD de audio. Ninguno era del SH-4.

**Y una lección sobre el método.** El bug de la carrera congelada se encontró porque hay
un oráculo: comparar los hashes de página contra la referencia Rust dijo «0 páginas
cambian, ellos 242» y eso convirtió una sospecha en un hecho. El mismo patrón que en M1a.
La inversión en el test diferencial se ha pagado dos veces.

Lo que **no** está cerrado: nadie ha quemado el CDI ni lo ha probado en una consola. El
formato es el correcto (MIL-CD, arranca con la BIOS real en Flycast) y la imagen cabe en
un CD-R, pero hasta que no corra en hardware el número de rendimiento sigue siendo de un
emulador.

## El siguiente corte: M2a

M2a es el primer hito con **imagen**: hasta ahora el port cuenta lo que el juego quiere
dibujar, pero no dibuja nada. Su criterio es un front end navegable y legible.

El orden no es caprichoso. Primero la **grabación en C**, que es código portable y —esto
es lo que la hace primera— **se puede comparar contra la referencia**, igual que la RAM:
la referencia produce la misma lista de comandos de dibujo, así que el oráculo sigue
sirviendo. Después el PVR, que ya no tiene oráculo y hay que mirarlo con los ojos.

**Al 2026-10-23 los cinco pasos están hechos** y el front end se dibuja en Dreamcast a
~60 fps. La parte sin oráculo dejó dos lecciones que valen para M2b y M2c:

- **120 frames no bastan para medir una caché.** La de texturas parecía perfecta en 120
  frames y a los 3 000 dejaba 13 654 peticiones sin textura, porque la tabla se llenaba
  de combinaciones viejas y no expulsaba ninguna. El escenario corto esconde el
  envejecimiento.
- **Mirar una captura no es medir.** Lo que parecía un desplazamiento del encuadre del
  PVR era el escalado de DPI de la captura de pantalla. Lo resolvieron cuatro marcadores
  de posición conocida y una línea por el puerto serie, no más capturas.

Detalle y cifras en `docs/hitos/M2a.md`.

## Despues: M2b y M2c

**M2b esta cerrado en emulador**: la carrera se dibuja —pista, cielo y objetos— a 60 fps,
y todavia **sin FTRV** (la transformacion va en C con `float`), asi que esa optimizacion
sigue entera disponible para M3.

**M2c va por el paso 3 de 6.** Su patron es el mismo que ya funciono en M2a y M2b, y
merece quedar escrito porque es la forma de trabajar del proyecto: **lo que tiene oraculo
va primero**. El juego no deja los objetos ni los coches escritos en su RAM —los dibuja—,
asi que el port corre su rutina anidada con un hook interno que la sustituye y anota la
decision. Esa anotacion **se compara contra la referencia** campo a campo (o en crudo,
palabra a palabra, para los coches) dentro de `make diff`, sin encender un Dreamcast.
Solo despues se dibuja, que es la parte que hay que mirar con los ojos.

Detalle y cifras en `docs/hitos/M2b.md` y `docs/hitos/M2c.md`.
