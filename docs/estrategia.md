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
- **M3 a M5** completan el middleware que falta: audio, controles, y guardado con la VMU.
- **M6** lleva el port a una **consola real** (CD-R, 480i y 480p): hasta ahí todo se ha
  validado en emulador, y es la primera vez que hay cifras del hardware de verdad.
- **M7** es el único hito donde el rendimiento es el criterio. Va después del middleware y
  de la consola (decisiones de Jorge, 2026-10-26): se optimiza el port completo, midiendo
  en el hardware real y no en Flycast.
- **M8** mejora el código y los assets actuales (texturas escaladas).

El coste de este orden es que se tarda más en ver algo en pantalla. El beneficio quedó
demostrado en M1a, y está documentado abajo.

## Objetivo de arquitectura: todo lo gráfico pasa por el middleware del PVR

**Objetivo (decisión de Jorge, 2026-10-25):** todo lo gráfico lo maneja el middleware de
renderizado del PVR. La única salvedad admisible es que **por problemas de rendimiento no
sea posible**: entonces se mide, se documenta como excepción en «Abierto» de la bitácora y
decide Jorge. No se hace por cuenta propia.

**Por qué, dos razones:**

1. **Poder cambiar texturas y modelos.** En una fase posterior, todavía sin definir en
   detalle ni incluida en el plan, el port deberá admitir texturas y modelos sustituidos.
   Eso solo es posible si **todo** lo que se ve sale del mismo camino y de los mismos
   formatos. Si un trozo se dibujara por otra vía, aunque fuera pequeño, un pack de texturas
   o un modelo nuevo tendría que tocarse también ahí, y se rompería la promesa. Hoy no se
   construye esa fase; lo que se exige es **no cerrarle la puerta**.
2. **Un marco reutilizable para otros proyectos de recompilación estática de juegos de
   PS1.** La separación que ya tiene el port es la que permite reutilizarlo: el juego decide
   qué se dibuja → el host lo **graba como datos** (la vista, los objetos, los coches, la
   lista 2D) → un renderer lo dibuja con el PVR desde assets horneados. Cambiar de juego
   cambia las grabaciones y los formatos de assets; el camino hasta el PVR es el mismo.
   **Esto es un objetivo, no un hecho:** hoy es el port de un solo juego, y lo reutilizable
   no está separado ni extraído todavía.

**Qué significa en la práctica**

- **Lo que el PVR no hace se resuelve con técnicas conocidas del propio PVR**, no con
  renderizado por software. Ejemplo ya hecho: la resta `B − F` de PS1 (que el PVR no tiene)
  se logra invirtiendo, sumando F e invirtiendo otra vez, tres mezclas del PVR; es exacta.
- **Hay una frontera que conviene tener clara.** El SH-4 **transforma y recorta geometría**
  (el PVR no tiene unidad de geometría, así que es lo normal en Dreamcast), pero **no
  rasteriza ni compone píxeles**. Eso último es lo que no se hace.
- **Los assets se cargan por nombre o código desde formatos horneados propios** (`.TRK`,
  `.CAR`, atlas), con las partes indexadas como las indexa el juego. Sustituir uno es
  sustituir un archivo, no tocar el renderer.
- **La interfaz entre la grabación y el renderer se mantiene estable** (CLAUDE.md §5), para
  poder comparar contra la referencia y para que el renderer sea intercambiable.

**Estado a 2026-10-25:** el front end, la pista, el cielo, los objetos, los coches, el HUD,
las sombras, las líneas pintadas, las marcas de neumático, los destellos y halos, el humo y
el flare del sol, el clima y los reflejos de los coches van por el PVR. **No hay ninguna
excepción.**

**Una cautela honesta.** Todas las cifras de rendimiento son de Flycast: no hay Dreamcast
disponible. Si alguna técnica resultara demasiado cara en consola real, esa sería la
excepción por rendimiento de la que habla el objetivo, y se decidiría con una medida real.

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
producto de M8, no de implementación.

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

**M2c va por el paso 5 de 6** (objetos, coches y HUD, todo validado). Su patron es el mismo que ya funciono en M2a y M2b, y
merece quedar escrito porque es la forma de trabajar del proyecto: **lo que tiene oraculo
va primero**. El juego no deja los objetos ni los coches escritos en su RAM —los dibuja—,
asi que el port corre su rutina anidada con un hook interno que la sustituye y anota la
decision. Esa anotacion **se compara contra la referencia** campo a campo (o en crudo,
palabra a palabra, para los coches) dentro de `make diff`, sin encender un Dreamcast.
Solo despues se dibuja, que es la parte que hay que mirar con los ojos.

Dos cosas que M2c ensenaron y que valen para M3:

- **El coste de la carrera esta en grabar, no en dibujar.** Con el dibujo de coches
  apagado el ritmo es el mismo (media 28,6 contra 28,0 fps): la caida de ~60 a 30-40 fps
  la produce correr codigo real del juego cada frame (`R3DCar_InsertAllListFacet` y, por
  coche, una segunda pasada entera para los halos, cuyo resultado hoy se tira).
- **Un oraculo hay que desconfiar tambien de el.** La referencia buscaba el entorno de
  dibujo en un `HashMap` de semilla aleatoria y daba listas distintas en cada ejecucion;
  y un comparador que se para a las N diferencias escondio 669 frames de carrera detras de
  una deuda vieja de los menus. Se arreglaron los dos.

**Decision de Jorge (2026-10-25):** lo que el PVR no puede se resuelve con tecnicas
conocidas del propio PVR, sin renderizado por software en el SH-4 —ni una parte pequena,
porque rompe la arquitectura—. No hay Dreamcast disponible por ahora, asi que no se hace
profiling en hardware real para decidir estos efectos (aclaracion de Jorge: no es una
regla general contra medir en consola cuando se tenga). Lo
sustractivo (`B - F`) se hace invirtiendo, sumando F e invirtiendo otra vez
(`B - F = 1 - ((1 - B) + F)`), tres mezclas del PVR: es el modo 2 de PS1 exacto.

**Una leccion de ese paso: verificar la tecnica no basta, hay que verificar el RESULTADO.**
La primera version usaba `B * (1 - F)`, que se comprobo dibujando un gris del 50 % y dio
exactamente lo esperado (cociente 0,50). La tecnica estaba bien implementada y aun asi era
la equivocada: oscurece en proporcion, y las marcas de neumatico, con F ~0,05, dejaban ~2,5
niveles contra los ~13 que resta la PS1. Solo se vio al medir el efecto real sobre un
frame real. La version exacta se verifico igual y dio una diferencia ABSOLUTA de 26 niveles
con un cociente que variaba de 0,55 a 0,78, que es justo lo que distingue una resta de un
porcentaje.
Ver `docs/bitacora.md`.

Detalle y cifras en `docs/hitos/M2b.md` y `docs/hitos/M2c.md`.

## Lo que se aprendió en M2 (cerrado en emulador, 2026-10-26)

- **Un guion que llega a un estado nuevo encuentra fallos nuevos.** La pausa sacó la
  deuda de los ayudantes del HUD (la RAM divergía al pausar); la carrera completa, dos
  fallos de grabación en el último frame y al recolocar el coche; la noche, que el haz de
  los faros no existía y que las pistas N y S estaban horneadas con el formato antiguo.
  Ninguno se veía con los guiones de siempre.
- **Para llegar lejos hace falta conducir, y el oráculo lo permite.** Ningún guion fijo
  termina una carrera. El piloto de pruebas de la referencia, metido en su modo diferencial,
  anota su entrada y los dos lados la reciben igual: 40 000 frames idénticos.
- **Los nombres heredados se comprueban.** La variante «S» (snow) es lluvia de noche en
  Alemania; lo dijo el original en DuckStation, no el nombre.
- **Mirar el original antes de decidir que algo falta.** Las sombras de los pasos de rueda
  del showroom estaban en la lista de pendientes; medido, el juego no las pide ahí.

Lo que no se ha hecho en ningún momento es medir en una Dreamcast: todas las cifras de
fps son de Flycast. Por eso la consola real (M6) va antes de optimizar (M7).
