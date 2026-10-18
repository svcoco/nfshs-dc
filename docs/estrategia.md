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

## El siguiente corte: M1b

M1b está **bloqueado por el toolchain** (no hay KallistiOS en la máquina, 2026-10-18), y su
primera medición no es escribir plataforma, es **el tamaño del código**: en Dreamcast el
`1ST_READ.BIN` se carga entero en RAM, así que cada byte de código quita RAM. La referencia
medida en x86-64 es 7,59 MB con `-O2` y **3,93 MB con `-Os`**, y el número de SH-4 decide
si hace falta trocear el código en overlays. Detalle en `docs/hitos/M1b.md`.
