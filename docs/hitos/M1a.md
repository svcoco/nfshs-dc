# M1a — Backend C + ps1rt en C, en x86

Fecha de inicio: 2026-10-17. Estado: **paso 1 (backend C) hecho y con paridad demostrada;
pasos 2 a 4 pendientes** (ps1rt en C, host x86, test diferencial).

## Paso 1: backend C del recompilador — hecho

`dc/tools/recomp/recomp.py` es la copia del recompilador de upstream con la opción
`--lang c` añadida. Las dos rutas comparten todo el análisis; solo cambia la generación,
que es precisamente lo que hace comparables las dos salidas.

### Medido

| Métrica | Valor |
|---|---|
| Funciones generadas | **3 674** |
| Salida | **26 MB**, **546 371 líneas**, 74 archivos |
| Partes | **70** (reparto por instrucciones, ~4 000 cada una) |
| Tamaño de parte, mín / máx | 186 KB / 500 KB |
| Funciones stub | **0** |
| Tiempo de generación | **2,4 s** (el backend Rust: 2,4 s / 48 partes) |
| Paridad con Rust | **3 674/3 674** funciones, **140 569** operaciones comparadas |
| Regresión del backend Rust | salida **byte a byte idéntica** a la de upstream |

Reproducir: `make -C dc gen`, `make -C dc parity`.

### Paridad: qué se verifica sin compilador

`recomp.py --lang rust` genera el Rust también con solo Python, así que la equivalencia se
demuestra antes de tener un compilador. `dc/tools/recomp/check_parity.py` extrae de cada
salida, función por función:

- el **conjunto ordenado de leaders** (los brazos del `match` en Rust ↔ las etiquetas en C);
- la **secuencia canónica de operaciones observables**: accesos a memoria
  (`r8/r16/r32/w8/w16/w32`, `lwl/lwr/swl/swr`), comandos del GTE con su palabra de
  instrucción, `mult/multu/div/divu`, `tick`, `syscall`, llamadas (directas, por dirección
  e indirectas), `push`/`pop`, `return` y la fijación de `cont`.

Si las dos secuencias coinciden elemento a elemento, las dos traducciones hacen lo mismo en
el mismo orden: slots de retardo en su sitio, ticks solo en saltos hacia atrás, y las mismas
aristas de control. Lo que **no** cubre es la aritmética dentro de cada expresión; eso lo
cubrirán los tests unitarios de `ps1rt` contra el oráculo Rust (paso 2).

### Decisiones de generación, con su motivo medido

- **Forma estática** (3 576 funciones): `switch (pc)` de entrada que hace `goto` a la
  etiqueta, y `goto` directo en todas las aristas internas. Con mediana de **6 leaders** por
  función, el `switch` es un árbol de 3–4 comparaciones.
- **Forma indirecta** (98 funciones con `jr reg`): `for (;;)` más `switch` con **índices
  densos** `(pc - base) >> 2`, porque con `switch (pc)` los casos van de 4 en 4 y gcc
  construye una tabla 4× más grande. Guarda `if (off & 3u) goto L_out;` obligatoria: sin
  ella `pc`, `pc+1`, `pc+2` y `pc+3` colapsan en el mismo caso y un `jr` desalineado iría al
  bloque equivocado en vez de salir, como hace el brazo `_` de Rust.
- **Sin computed goto** (`&&label`): con cientos de destinos infla el grafo de flujo y gcc
  tiene problemas conocidos de tiempo de compilación. El peor caso aquí son 1 464 casos
  (`0x800644b4`), y las tablas de salto suman **73 KB** como cota superior.
- **Reparto por instrucciones**, no por número de funciones: el backend Rust usa 48 partes
  por conteo de funciones y quedan desbalanceadas porque una carga con la función de 3 207
  instrucciones.
- **`dispatch_at` replica el `.take(8)` de Rust, fallos incluidos.** Es una heurística que
  puede no encontrar el rango; si en C buscásemos más atrás acertaríamos donde la referencia
  falla, y el test diferencial divergiría.
- **`dispatch` reutiliza la tabla de rangos** con búsqueda binaria exacta en vez de traducir
  el `match` de 3 674 casos: los inicios son únicos y `f_X(c)` hace `f_X_at(c, X)` por
  construcción. Tablas en SoA (`ps1_range_start/end/at`), para que la búsqueda binaria
  recorra solo 4 bytes por entrada.
- **`r0` no se pliega a 0** (`c->r[0]`, igual que Rust): mantiene la paridad trivial. Se
  podrá plegar después de que el diff pase, midiendo el delta de tamaño.

### Trampas del original que hubo que replicar

1. **Escribir en `r0` descarta el valor pero evalúa la expresión** (el backend Rust emite
   `let _ = expr;`). Así que `lb $zero, off(rs)` **sí ejecuta el acceso**: en C se emite
   `(void)(...)` y los accesores **no pueden** ser `pure`/`const`. Hay **149** loads con
   destino `$zero`.
2. **`nor` es `!(rs | rt)` en Rust, que es NOT de bits.** En C tiene que ser `~`; con `!`
   daría `0`/`1` en silencio.
3. **`break` se omite por completo.** Hay **383** en el juego: 206 con código `0x1C00`
   (división por cero) y 168 con `0x1800` (desbordamiento), las trampas que genera el
   compilador, precedidas de un `nop` y protegidas por un branch, así que solo se alcanzan
   si el divisor es cero. Los dos backends las omiten igual, luego la paridad se mantiene;
   queda como **desviación respecto al hardware real** heredada de upstream.
4. **Un branch en slot de retardo se descartaría en silencio** (`gen_simple(...) or ''`
   convierte el `None` en cadena vacía y el `raise` nunca se dispara). Medido: hay **0** en
   el juego, así que no afecta.
5. **Sin `syscall`** en todo el conjunto (0 instrucciones).

### Interfaz que `ps1rt` en C tiene que ofrecer

El C generado solo usa esto, así que define el contrato del paso 2:

```
Cpu        c->r[32], c->hi, c->lo, c->cop0[32], c->cont
pila       ps1_push(c, addr)   ps1_pop(c)
memoria    ps1_r8/r16/r32(c, a)        ps1_w8/w16/w32(c, a, v)
           ps1_lwl/lwr(c, a, prev)     ps1_swl/swr(c, a, v)
aritmetica ps1_mult/multu/div/divu(c, a, b)
GTE        ps1_gte(c, instr_word)
control    ps1_call(c, addr)   ps1_run_cont(c)   ps1_tick(c)
           ps1_syscall(c)      ps1_panic(c, msg)
despacho   ps1_dispatch(c, addr)   ps1_dispatch_at(c, pc)   (los emite dispatch.c)
```

**Corrección sobre CLAUDE.md §9:** la máscara `& 0x1FFFFF` que propone para la ruta rápida
de memoria es incorrecta, porque mapearía el scratchpad `0x1F800000` sobre la RAM. Hay que
normalizar con `a & 0x1FFFFFFF` (cubre KUSEG/KSEG0/KSEG1) y **comprobar el rango primero**
(`< 0x200000` → RAM plana; si no, ruta lenta `noinline` para scratchpad e IO).

## Paso 0: toolchain y pruebas de carga — hecho

### Entorno

| | |
|---|---|
| MSYS2 | instalado en `C:\msys64` (installer 20260611, vía winget) |
| gcc | **16.2.0** (Rev6, MSYS2), destino `x86_64-w64-mingw32`, entorno UCRT64 |
| make | **4.4.1** |
| Disco | 49 GB libres en C: antes y después; MSYS2 + toolchain apenas se notan |

gcc y no MSVC a propósito: es el mismo compilador que `sh-elf-gcc` de M1b, así que los
builtins, el `>>` con signo, las extensiones GNU y el tratamiento de los `switch` grandes
se comportan igual en PC y en Dreamcast.

### Supuestos del backend C: validados, no supuestos

Compilado y ejecutado con `_Static_assert` reales. Los cinco se cumplen:
`sizeof(int) == 4 && UINT_MAX == 0xFFFFFFFF`; `(-1 >> 1) == -1` y
`(int32_t)0x80000000 >> 31 == -1` (el `>>` con signo es aritmético, que es
*implementation-defined* en C y de lo que dependen cada `sra`, `srav`, `slt`, `slti`,
`blez` y `bgtz` generados); `(int32_t)0x80000000u == INT32_MIN` (la conversión
`uint32_t`→`int32_t` reinterpreta en módulo 2³², como el `as i32` de Rust); el unsigned
envuelve sin UB; y little-endian, igual que PS1 y que el SH-4 del Dreamcast, así que no
hay intercambio de bytes en todo el proyecto.

Esas comprobaciones viven ya en `dc/src/ps1rt/ps1rt.h`, de modo que cualquier compilador
que no las cumpla falla al compilar en vez de generar código incorrecto en silencio.

### El C generado compila entero

| | -O0 | -O2 |
|---|---|---|
| Unidades compiladas | 71 | 71 |
| Fallos | **0** | **0** |
| Líneas de aviso con `-Wall -Wextra` | **0** | **0** |
| Tiempo de pared con `-j8` | **14,8 s** | **55,6 s** |
| Suma de `.o` | 9,85 MB | 10,7 MB |
| Unidad más lenta | `part_21`, 1,5 s | `part_59`, 6,8 s |

Tamaños por sección a -O2, en x86-64:

| Sección | Tamaño |
|---|---|
| `.text` | **6,80 MB** |
| `.rodata` (tablas de salto) | **0,65 MB** |
| `.data` | 0 |
| `dispatch.o`, solo sus tablas | 58 KB (3 674 × 3 entradas en SoA) |

El `switch` denso del peor caso no se dispara: `part_21`, que contiene la función de
1 464 casos, ocupa 107,8 KB de `.text` y 14,5 KB de tablas, y **-O2 sale más pequeño que
-O0**. La decisión de los índices densos queda validada con números.

### Dos correcciones a cifras anteriores

1. **La cota de 73 KB para las tablas de salto era incompleta.** Solo contaba los `switch`
   densos de las 98 funciones indirectas. El `.rodata` real es de **0,65 MB**, porque los
   `switch` de *entrada* de las funciones normales también generan tablas: hay **65 713
   leaders** en total, de los que solo 18 691 son de funciones indirectas. A 4 bytes por
   entrada eso son ya 0,25 MB, y el resto se va en anchura de entrada y alineación.
   **Candidato claro de optimización para M3:** el `switch` de entrada solo lo usa
   `dispatch_at`, que es la ruta rara, porque el flujo interno va por `goto`; cambiarlo por
   una búsqueda binaria recuperaría la mayor parte de esos 0,65 MB.
2. **La estimación de 2,9–5,9 MB de código SH-4 parece optimista en su extremo alto.** El
   mismo código da **6,80 MB de `.text` en x86-64**. No es extrapolable directamente (SH-4
   usa instrucciones de 16 bits, pero necesita más por operación), así que la cifra real
   hay que **medirla en M1b**; pero si queda cerca, con 2 MB de RAM de PS1 y 1 MB de espejo
   de VRAM sobre 16 MB totales, el presupuesto se pone tenso. Es el riesgo que hay que
   vigilar antes de M2.

### `dc/src/ps1rt/ps1rt.h`: el contrato, ya escrito

Define `struct Cpu`, los accesores de memoria como `static inline` con la ruta rápida, la
pila de diagnóstico, y los prototipos de los seis módulos que faltan. Incluye el mapa de
memoria correcto (umbral de IO en `0x00800000`, no `0x200000`) y contadores de accesos
desalineados y de borde de RAM, que son los que dirán en M1b si hace falta el respaldo
byte a byte para el SH-4.

## Paso 2: `ps1rt` en C — en curso

| Modulo | Estado | Pruebas |
|---|---|---|
| `ps1rt.h` | hecho: `struct Cpu`, mapa de memoria, accesores inline, `_Static_assert` | — |
| `mem.c` | hecho: scratchpad, IO, volumen del SPU, bus, `load_exe` | fronteras del mapa, espejos, desalineados, borde de RAM |
| `alu.c` | hecho: `mult`/`multu`/`div`/`divu`, `lwl`/`lwr`/`swl`/`swr` | casos limite de division, 4 alineaciones |
| `panic.c` | hecho (lo unico dependiente de plataforma) | — |
| `cpu.c` | hecho: `ps1_call`, `tick`, `run_cont`, `jump`, `call_nested`, interrupciones, diferidas | orden de despacho, `cont`, parada, IRQ, diferidas |
| `bios.c` | hecho: las 25 rutinas de la tabla A0; B0 y C0 avisan y devuelven 0 | cadenas, memoria, numeros, rand |
| `interp.c` | hecho: el juego llama por puntero a `0x80107358`, que el recompilador no cubre | lo ejercita el front end, 906 k entradas |
| `gte.c` | hecho: registros, saturaciones, `FLAG`, division UNR y los 24 comandos | vectores de `gte.rs`, rarezas de registros, banderas, el bug de MVMVA, COP2 |

**227/227 comprobaciones correctas** con `make -C dc test`.

Las pruebas no son de humo: fijan el orden exacto de `ps1_call` (vectores de BIOS con
`addr & 0x1FFFFFFF`, luego el host, luego el código recompilado), que el host se
**retira** mientras corre para que sus llamadas anidadas no reentren en él, que
`inner_hook` solo se consulta cuando no hay host, el encadenado de continuaciones, el
marco por debajo y la restauración de estado de `call_nested`, la entrega de
interrupciones de timer con `in_irq` cortando la reentrada, y `tick_limit == 0` saliendo
por `longjmp`.

### Dos trampas que encontraron las pruebas

Las tres veces que falló algo, el fallo estaba en la prueba, no en el port — pero dos de
ellas dejan avisos útiles para más adelante:

1. **Una llamada diferida vuelve a pasar por `ps1_call` con el host ya restaurado.** Un
   host que encolara una diferida en *cada* llamada se recursaría hasta desbordar la pila.
   No es un problema de `cpu.c` (la referencia hace lo mismo), pero sí una regla para el
   host del paso 3: encolar solo lo que toca, y nunca de forma incondicional.
2. **Un manejador de interrupción no debe reponer `pending_ticks`**, porque el bucle que
   lo consume está por encima: eso convierte la entrega en un bucle infinito.

También quedó claro que el arnés de pruebas necesita `stdout` sin buffer: con buffer, un
fallo que se lleva el proceso por delante se traga justo la línea que dice dónde estabas.

### El GTE, verificado por partida doble

Los dos tests que ya traía `gte.rs` están portados tal cual, porque son valores de
referencia y no suposiciones mías: los cuatro puntos de la división
(`divide(0x100,0x100) = 0x10000`, `(0x100,0x200) = 0x8000`, `(0x1FF,0x100) = 0x1FF00`,
`(0x200,0x100)` sin resultado) y el RTPS de identidad con H = 256 y desplazamiento
(160, 120), que da **(210, 145)** y **SZ3 = 512**.

Como el módulo pasó a la primera, añadí una comprobación más amplia en lugar de
fiarme: un modelo independiente del algoritmo de la división Newton-Raphson, y la
comparación de **608 595 combinaciones** de `h` y `sz3` contra la implementación en C.
Coinciden todas, así que la transcripción de la parte más retorcida (la tabla UNR de
257 entradas, los ceros a la izquierda de `sz3` *truncado a 16 bits*, y las dos
iteraciones de refinamiento) está bien.

Las pruebas cubren además lo que de verdad rompe un port del GTE: la extensión de signo
registro por registro, el 15 espejando el 14 y empujando la cola de vértices al
escribirse, la compresión a 5 bits del 28, el 29 y el 31 de solo lectura, el 31 contando
ceros (o unos, si es negativo), el bit 31 de `FLAG` como OR de los bits de error, la
saturación de IR encendiendo su bandera, y **el error de hardware de MVMVA con
`cv == 2`**, donde solo las dos últimas columnas llegan al MAC.

La tabla UNR va horneada como `static const` en vez de calcularse al arrancar, y el
índice lleva guarda: donde la referencia entraría en pánico por índice fuera de rango,
aquí para con un mensaje en vez de leer fuera del array.

**Nota para M1b:** toda la aritmética del GTE va en `int64_t`, que en SH-4 es software.
El juego tiene 809 instrucciones cop2 y están en el código de dibujo, así que no afecta
a M1a; se mide en M1b y, si pesa, se optimiza en M3.

### Contrato que falta cubrir

Quedan el cuerpo de `bios.c` y el de `interp.c`. Los dos se benefician de esperar al
host (paso 3): `bios_unhandled` dirá exactamente qué funciones de la tabla A0 usa el
juego, y el contador de entradas al intérprete dirá si hace falta portarlo entero o
basta con recompilar unos pocos puntos de entrada. Implementar las 25 rutinas de BIOS y
las 60 instrucciones del intérprete a ciegas es justo lo que la regla de «mide, no
supongas» aconseja no hacer.

## Paso 3: host x86 — el juego arranca

`make -C dc pc` enlaza el C generado (71 objetos) con `ps1rt`, el host y el arnés de
x86, y produce `dc/build/nfshs-dc`. **El código recompilado ejecuta código real del
juego**: se entra por `main` (`0x800A52A4`) con la misma secuencia de arranque que
`power_on()` de la referencia (cargar NFS4.EXE, poner a cero el BSS `0x8013F084`–`0x80149E40`,
`sp = 0x801FFFF0 - 0x100`, `gp = 0x8013D83C`, `spin_limit = 20000`).

El host de esta primera versión **no implementa nada a propósito**: lee
`tools/hle_funcs.txt` en ejecución (así no puede desincronizarse del C generado) y
registra qué pide el juego, en qué orden y cuántas veces. Eso es lo que decide por
dónde seguir, en vez de escribir 119 reemplazos a ciegas. Tiene dos modos: por omisión
para en la primera HLE que falta con su nombre y sus argumentos; con `--survey` devuelve
0 y sigue, para ver el máximo en una sola ejecución (a partir de ahí el estado del juego
ya no es fiable, y se dice).

### Lo que el juego pide al arrancar, en orden

| # | Dirección | Función | Llamadas |
|---|---|---|---|
| 1 | `0x800f4d30` | FlushCache | 2 |
| 2 | `0x800f31a8` | ResetCallback | 1 |
| 3 | `0x800edfcc` | ResetGraph | 1 |
| 4 | `0x800fe6d4` | PadInitDirect | 1 |
| 5 | `0x800f07bc` | PadStartCom | 1 |
| 6 | `0x800fad44` | CD_Init | 1 |
| 7 | `0x800f24b8` | GetVideoMode | 1 |
| 8 | `0x800ee680` | PutDrawEnv | 1 |
| 9 | `0x800ee564` | ClearOTagR | 3 |
| 10 | `0x80107338` | EnterCriticalSection | 2 |
| 11 | `0x801077cc` | OpenEvent | 1 |
| 12 | `0x801077bc` | EnableEvent | 1 |
| 13 | `0x800ea7cc` | SetRCnt | 1 |
| 14 | `0x800ea8a0` | StartRCnt | 1 |
| 15 | `0x80107318` | ExitCriticalSection | 2 |
| 16 | `0x800f3208` | DMACallback | 1 |

Son **16 de las 119**, y la secuencia dice exactamente qué falta: el juego registra un
evento, programa el root counter, lo arranca, registra la devolución de DMA… y se queda
girando. **El bloqueo es el timer de 128 Hz:** `ps1_service_interrupts` ya sabe
entregarlo, pero nadie alimenta `pending_ticks`. Eso es lo siguiente a implementar, y
no era evidente antes de medirlo.

### Contadores de la ejecución

| | |
|---|---|
| Ramas hacia atrás | 20 000 001 (tope del watchdog) |
| Llamadas por dirección | 29 |
| **Entradas al intérprete** | **0** |
| **Accesos desalineados** | **0** |
| **Accesos en el borde de la RAM** | **0** |
| Comandos del GTE | 0 (todavía no se dibuja) |
| Accesos a IO | 242 (el último en `0x1f801da2`, registros del SPU) |
| BIOS sin implementar | 1 (`B0:0x56`, que la referencia tampoco implementa) |
| Idles del host | 999 |

Tres de esos ceros son los que importaban:

- **0 entradas al intérprete**: hasta aquí, la recompilación cubre todo lo que el juego
  ejecuta. Refuerza la decisión de no portar `interp.rs` todavía.
- **0 accesos desalineados y 0 en el borde de la RAM**: la ruta rápida alineada vale, y
  en M1b el `mov.l` desalineado del SH-4 no será un problema — al menos en el arranque.
  Hay que volver a medirlo con una carrera.

### `bios.c` completo, y por qué ahora

La ejecución pidió `strlen`, `strcpy`, `strncmp` y `memset` en los primeros cientos de
instrucciones, así que la tabla A0 pasó de "esperar a medir" a "hace falta ya". Están
las 25 rutinas portadas y probadas (cadenas, memoria, `strtol`/`atoi`, `abs`, y el
generador congruencial de `rand`), con el detalle de que `memcpy` y `memmove` se
comportan igual porque la referencia lee todo el origen antes de escribir. B0 y C0
siguen avisando y devolviendo 0, como alli.

**227/227 comprobaciones** con `make -C dc test`.

### El timer de 128 Hz y las 16 HLE del arranque: hechas

`dc/src/host/system.c` y `dc/src/host/gpu.c` implementan las 16 funciones que el
arranque pide, más el reloj virtual. **Las 16 salen `ok` en el informe.**

El reloj va en unidades de 1/1920 s, como la referencia: cada frontera de 15 unidades
encola una interrupción del root counter (128 Hz) y cada 32, la devolución de VSync
(60 Hz, limitada a 4 por salto para no acumular avalanchas). `host_advance_to` es quien
alimenta `pending_ticks`, que es lo que `ps1_service_interrupts` ya sabía entregar, y el
`idle` del host deja pasar un tick cuando el juego lleva 20 000 ramas hacia atrás sin
pasar por el host.

Hay un detalle de diseño que importó: `systemtask` (`0x800E77F0`) **no** es HLE sino un
*hook*, o sea que está recompilado. El host produce su efecto lateral (dejar pasar el
tiempo, que es lo que saca al juego de sus esperas activas) y después deja correr el
código original. El `host_call` distingue los dos casos: lo que está en `hle_funcs.txt`
lo atiende y devuelve "atendida"; lo que no, pasa por `host_hook` y cae al despacho.

### Resultado: el juego avanza, y el siguiente bloqueo está localizado

| | antes | ahora |
|---|---|---|
| HLE implementadas de las que pide | 0 de 16 | **16 de 16** |
| Llamadas por dirección | 29 | **1 529** |
| Manejadores de timer registrados | 0 | **1** |
| Tiempo de juego simulado | 0 | **11,7 s** |

Sigue sin pasar del arranque, pero ya no es un misterio. El juego gira en este bucle:

```
0x80100ee0  jal 0x8010bac8
0x80100ee4  move a0, s0
0x80100ee8  beq v0, zero, 0x80100ee0    <-- repite mientras devuelva 0
```

Y `0x8010bac8` llama a `0x8010b87c` y después consulta un byte de estado en
`0x80148C60`. Rastreando el ejecutable completo, ese byte tiene **tres lecturas y
ninguna escritura**: nadie lo pone a nada. La cadena de llamadas sube hasta `main` sin
pasar por ninguna función HLE, así que en la referencia este código también corre.

Matiz importante sobre ese rastreo: solo descarta escrituras cuya dirección se forme
con `lui`+`addiu` en el propio código. Una escritura **a través de un puntero** guardado
en una global no la vería, así que no está probado que nadie escriba el byte; lo que
está probado es que no hay escritura directa.

Las direcciones del bucle (`0x80100bb8`, `0x8010b4bc`, `0x8010b87c`, `0x8010bac8`) caen
entre `CdRead` (`0x80109788`) y `DeliverEvent` (`0x8010d354`), o sea en las tripas del
driver de CD y eventos de la BIOS. La estructura en `0x80148C54` tiene campos de estado
en +12 y +14, que es la forma típica de una máquina de estados que avanza una
interrupción.

**Diferencia encontrada con la referencia, y verificada.** Su `new_cpu()` hace dos cosas
que el arnés no hacía:

1. `cop0[12] = 0x401` — interrupciones habilitadas (IEc e IM2 del SR), «como las deja la
   BIOS». **Ya corregido.** Sin eso, `service_bus_interrupts` descarta todo de entrada.
2. `c.m.bus = Spu::default()` — conecta un `IoBus`. **Esto es lo que falta.** Sin bus,
   `take_irqs` no se consulta nunca, así que ninguna interrupción de hardware llega, y
   arreglar solo el punto 1 no cambió el resultado (comprobado: mismos 1 529 llamadas y
   mismo giro).

El `IoBus` de la referencia modela el SPU y su canal 4 de DMA (`0x1F8010C0`–`C8`), y es
quien reporta el fin de las transferencias. Construir uno mínimo es el siguiente paso.

Para diagnosticar esto añadí al arnés la opción `--idlelog` (dónde gira el juego cuando
se detecta espera activa), el anillo de las últimas continuaciones y la pila del
invitado en el informe. De paso quedó claro que **la pila es solo diagnóstico y no es
fiable**: un *tail call* a una función HLE hace `pop` y luego llama al host, que no
apila, así que la profundidad se va a 0. La referencia hace exactamente lo mismo, así
que se conserva; el dato útil en esas situaciones es `ra`.

### El `IoBus`: hecho, y la hipótesis era correcta

`dc/src/host/spu.c` conecta el bus de hardware. **No sintetiza audio** —la decisión de
§3 es que en M1 y M2 el SPU sea un stub que acepta escrituras, y la conversión a AICA es
M3— pero sí modela lo que el arranque necesita:

- el bloque de registros del SPU (`0x1F801C00`–`0x1F801FFF`), con `SPUSTAT` derivado de
  `SPUCNT` como hace el hardware, y la dirección de transferencia en unidades de 8;
- el **canal 4 de DMA** (`MADR`/`BCR`/`CHCR`, más `DPCR` y `DICR`): al escribir el bit 24
  de `CHCR` la transferencia se hace de verdad entre la RAM del invitado y 512 KB de RAM
  de sonido, y **al terminar levanta la interrupción de fin** (bit 16 + canal 4);
- el reconocimiento de banderas de `DICR` escribiendo 1, como el hardware.

El volumen de envolvente de las voces se lee como 0: el driver de sonido lo consulta para
saber cuando una voz ha terminado, y así las da todas por terminadas. Es consistente con
no sintetizar.

**Resultado medido: el juego pasa el giro.**

| | sin bus | con bus |
|---|---|---|
| HLE distintas que pide | 16 | **17** |
| Giro infinito | sí (30 M de ramas, 1 499 idles) | **no** |
| Llamadas por dirección | 1 529 | 160 |
| Idles | 1 499 | **35** |
| Transferencias de DMA4 | — | **35** (8 192 bytes) |
| Interrupciones levantadas | 0 | **35** |
| Entradas al intérprete | 0 | **0** |

La hipótesis de la sección anterior queda confirmada: lo que faltaba eran las
interrupciones de fin de DMA, y hacían falta las dos cosas (el `cop0[12] = 0x401` y el
bus; ninguna de las dos por separado basta).

### Siguiente bloqueo: la capa de archivos

El juego llega ahora a la función **17: `FILE_open`** (`0x800eccc8`), y el nombre que pide
es `"front.bin"` — el overlay del front end, **que ya está en `gamedata/`**. Después pide
`FILE_opensync`.

Eso cambia el orden del plan: la capa de archivos se puede empezar **sin extraer la
imagen del disco**, porque el primer archivo ya lo tenemos. La extracción hará falta en
cuanto pida datos de pistas o coches.

### Extraccion del disco y capa de archivos: hechas

**`dc/tools/extract_iso.py`** saca los archivos de la imagen. Es MODE2/2352: sectores de
2352 bytes con 16 de sincronismo y cabecera, 8 de subcabecera (Mode 2 Form 1) y 2048 de
datos utiles, con un ISO 9660 normal encima. En Python a proposito, porque el
`nfshs extract` de la referencia necesita cargo y esto hacia falta antes de tener Rust;
en M2 la herramienta de bake volvera a leer el disco y reutilizara este codigo.

| | |
|---|---|
| Archivos en el disco | **652**, 607,7 MB |
| Extraidos | 648 (4 ya estaban) en **3,7 s** |
| Verificacion | los hashes de `NFS4.EXE` y `FRONT.BIN` siguen siendo los de §6 |

Un contraste util: el `FRONT.BIN` del disco mide 281 048 bytes, exactamente los del que
Jorge ya tenia.

**`dc/src/host/files.c`** implementa las 17 funciones `FILE_*`. Las `*sync` son
sincronas y las asincronas (`FILE_open`, `FILE_read`, `FILE_size`, `FILE_close`)
devuelven un identificador de operacion, pero **todas terminan en el acto**: no hay
disco lento que esperar, asi que una operacion nace ya completada, igual que en la
referencia. Incluye el montaje de archivos **BIGF** (`FILE_addbigsync`) y la busqueda
por nombre base sin distinguir mayusculas dentro de ellos.

Dos detalles que importaron: el sexto argumento de `FILE_read` va **en la pila**, en
`sp+20`, no en un registro; y las entradas de un BIGF se reservan segun las que declare
el archivo, no con un tope fijo — mi primer intento tenia un limite de 256 y
`ZZZFE.VIV`, el archivo del front end, tiene **435**, asi que fallaba el montaje.

### El front end carga y su codigo corre

Con la capa de archivos, el espejo de VRAM, los videos y los stubs de memory card y
streaming, el juego pasa de **16 a 37 funciones HLE, todas `ok`**, y ninguna falta.

| | |
|---|---|
| HLE distintas pedidas | **37**, 37 `ok` |
| Archivos abiertos | 9, **623 897 bytes leidos** |
| BIGF montados | 1 (`zzzFE.viv`, 435 entradas) |
| Area de pantalla | **512 x 240** (la resolucion NTSC de PS1) |
| Flips (`PutDispEnv`) | 2 |
| Entradas al interprete | **0** |
| Accesos desalineados | **0** |

Los archivos que carga, en orden: `front.bin` (el overlay del front end, 281 048 bytes),
`zfecars.car`, `ztourn.trn`, `zfetrk.trk` y el archivo `zzzFE.viv`. Es decir: **el front
end se carga y su codigo se ejecuta**.

### Siguiente bloqueo: el front end espera sobre VSync

No llega a dibujar (0 `DrawOTag`). Se queda en una espera activa:
**278 337 llamadas a `VSync` en 20 M de ramas**, y quien la llama es `0x800353a8`, que
esta dentro del rango del overlay de FRONT.BIN. O sea, código del front end esperando
algo que no pasa.

**Hipotesis con respaldo:** los mandos. `PadInitDirect` escribe `0xFF` en los dos buffers
del pad, que significa "sin mando conectado", y el comentario de la propia referencia lo
dice: *"no controller until the host writes one"*. Su driver de frames escribe el estado
del pad en esos buffers en cada frame; nosotros no escribimos nunca. Un front end que
espera a que aparezca un mando encaja exactamente con lo que se ve.

Eso es lo siguiente: escribir en los buffers el estado de un mando digital conectado, que
ademas es justo lo que M1a necesita para la **entrada guionizada** del test diferencial.

### El front end corre su bucle de frames

Con los mandos, la memory card, los videos y el dibujo contado, el juego pasa de 37 a
**58 funciones HLE, todas `ok`**, y el front end entra en su bucle de frames de verdad.

Progresa de forma **lineal** con el presupuesto de ticks, que es la prueba de que no
esta girando:

| Presupuesto | 20 M ramas | 60 M ramas |
|---|---|---|
| Flips (`PutDispEnv`) | 3 042 | **12 267** |
| Campos de video | 3 474 | 12 996 |
| Tiempo de juego | 57,9 s | **216,6 s** |
| Formas 2D del front end | 408 217 | **1 645 963** |
| Cuadros (`PSXDrawSquare`) | 51 616 | 208 122 |
| Listas de dibujo (`DrawOTag`) | — | 12 265 |

Hace **un flip por campo de video**, o sea que el front end apunta a 60 Hz. Es una
observacion mediada por nuestro reloj (el host decide cuando avanza el tiempo), no una
medida del ritmo original, pero dice que el bucle esta sano.

### Los cuatro bloqueos que hubo que levantar, y lo que ensenaron

**1. Los mandos.** `PadInitDirect` deja `0xFF` en los buffers, que significa "sin mando
conectado". El host escribe ahora, en cada campo, estado correcto (`0x00`), tipo de mando
digital (`0x41`) y los botones **con logica invertida**. Nota para el test diferencial:
la referencia lo escribe en su `frame_top`, atado a la estructura de frames del juego, y
aqui va atado al reloj; **habra que atarlo al mismo punto**, porque la entrada tiene que
aplicarse en el mismo sitio o el estado divergira.

**2. La memory card, y un error mio que merece la pena contar.** Copie el stub de
`MemCardSync` de `system.rs` de la referencia... y resulta que **esa rama es codigo
muerto**: su modulo de memory card se consulta antes y la atiende con estado real. El
modelo correcto es de **comando pendiente**: las funciones de la tarjeta no devuelven el
resultado, lo dejan pendiente, y `MemCardSync` devuelve 1 y escribe cual y su resultado,
o **-1 si no hay nada pendiente**. Devolver 1 siempre, como hacia mi stub, dejaba al
front end releyendo un comando fantasma para siempre. La leccion: al copiar de la
referencia hay que mirar **el orden de despacho**, no solo la rama.

**3. Los videos.** Los siete `VIDEO_*` informan de que no hay video. El front end lo
acepta y sigue, que era justo lo que habia que medir en vez de suponer.

**4. El interprete de respaldo, que ahora si hacia falta.** El juego llama **por puntero**
a `0x80107358`, una funcion de cinco instrucciones (lee un byte y avanza el puntero, un
`getc`) que las heuristicas del recompilador no reconocen como inicio de funcion: nadie
la llama con `jal` y no monta marco de pila. La referencia tiene el mismo agujero y lo
tapa con `interp.rs`. Portado `interp.c`, y con eso el front end arranca.

Esto cierra el **paso 2 de M1a**: `ps1rt` esta completo, con sus siete modulos. Y valida
la decision de haber esperado: el modulo se porto cuando una medicion dijo que hacia
falta y para que direccion, no por si acaso.

**Dato para M3:** son **906 234 entradas al interprete** en 60 M de ramas, unas 74 por
frame, todas para esa misma funcion de cinco instrucciones. En SH-4 eso costaria caro.
La via mas simple sera pasarle esa direccion al recompilador como raiz extra para que la
compile; hay que comprobar antes que no rompe la paridad de 3 674 funciones con la
referencia.

### El juego llega a una carrera

`dc/src/host/pad.c` acepta un **guion de entrada**: un archivo de texto con pulsaciones
por numero de campo de 60 Hz, con los botones por nombre (`START`, `CROSS`, `DOWN`...) o
en hexadecimal, y un `#end` para terminar. Es lo que hace reproducible el test
diferencial, y de paso lo que lleva al juego donde queramos.

Con `dc/test/diff/scripts/into_race.txt` (30 s de juego, 1 800 campos):

| | |
|---|---|
| HLE distintas, todas `ok` | **65** |
| **Vistas 3D pedidas** (`Render_RenderPlayerView`) | **884** |
| Flips | 1 121 |
| Listas de dibujo | 5 537 |
| Archivos abiertos | 102 (4,8 MB leidos), 3 BIGF montados |
| Transferencias de DMA del SPU | 702 (1,37 MB de sonido) |
| Area de pantalla | 320,256 320x240 |

Las 884 vistas 3D son la senal que buscabamos: **el juego esta en una carrera**. Las 3
aperturas fallidas son sondeos legitimos (`zTr06.trf` no existe en el disco: hay 0
archivos `.trf`), no un agujero del host.

### El arnes del test diferencial, y el determinismo medido

**Toolchain:** instalado `rustup` 1.29.1 con `cargo` 1.99.1, y generada la fuente Rust
de referencia (29 MB, 49 archivos, en 2,3 s).

**`dc/src/host/dump.c`** vuelca el estado por frame. Volcar 2 MB por frame seria
inviable (1 800 frames x 2 MB x 2 lados = 7 GB), asi que son dos pasadas: la primera
guarda por frame los registros mas un **hash FNV-1a de 64 bits por pagina de 4 KB** de
la RAM (4 636 bytes por frame, 8,3 MB en 1 800 frames); la segunda, ya con el frame
divergente localizado, vuelca esos 2 MB en crudo. FNV-1a a proposito: trivial de
implementar igual en los dos lados, sin dependencias.

El volcado se escribe **en la frontera de campo y despues de aplicar la entrada**, que
es el punto donde los dos lados tienen que coincidir.

**`dc/test/diff/cmp.py`** compara dos volcados y responde en orden: en que frame
divergen, en que parte del estado (que registro, o cuantas paginas de RAM) y que paginas,
para poder pedir el volcado crudo.

Dos resultados medidos:

1. **El lado C es determinista.** Dos ejecuciones de 1 800 frames con el mismo guion dan
   volcados **identicos byte a byte** (mismo MD5). Es condicion necesaria para que el
   test diferencial signifique algo, y ya esta comprobada de nuestro lado.
2. **El comparador funciona.** Validado con un caso positivo y uno negativo: moviendo una
   pulsacion de `START` del campo 120 al 60, localiza la divergencia **exactamente en el
   frame 60**, en la pagina `0x13f000`.

Lo que queda del paso 4 es el otro lado: inyectar el mismo volcado en la referencia Rust,
comprobar que **ella** tambien es determinista (el riesgo que sigue sin medir) y comparar.
Ahi aparecera la diferencia ya identificada de **donde se aplica la entrada**: la
referencia la escribe en su `frame_top`, atado a la estructura de frames del juego, y
aqui va atada al reloj.

### El test diferencial funciona de los dos lados, y el riesgo mayor queda resuelto

**La referencia es determinista.** Era el mayor riesgo de M1a y ya esta medido: dos
ejecuciones de `boot-main` con el mismo guion dan volcados **identicos byte a byte**
(mismo MD5) y contadores exactamente iguales (408 661 llamadas, 97 974 interpretadas,
11 389 092 ramas hacia atras). El test diferencial tiene suelo firme.

Como se inyecto: la referencia ya trae un comando de desarrollo **sin ventana**,
`boot-main`, que arranca `main` y tiene un callback `on_frame` — justo el punto donde
aplica la entrada. Se le anadieron dos variables de entorno, sin cambiar su
comportamiento por omision:

- `NFSHS_DIFF_SCRIPT`: lee **el mismo archivo de guion** que el host en C, para que los
  dos lados reciban la misma secuencia.
- `NFSHS_DIFF_OUT`: escribe el volcado en el formato de `dc/src/host/dump.c`.

Un detalle que confundia al principio: `boot-main` termina con
`main returned: Err(...)` y un mensaje vacio. **No es un fallo**: es la parada normal al
acabar el guion (`tick_limit = 0` levanta un `Stop` que no es un `String`, de ahi el
mensaje vacio).

### La frontera de frame, alineada

La primera comparacion divergio en el frame 1, y la causa no era el codigo recompilado
sino **donde vuelca cada lado**: C lo hacia en cada campo de video y la referencia en su
`frame_top`, que es un punto del juego. Su regla, ahora replicada en C:

- front end: `PutDispEnv` del flip del motor, con `ra == 0x800BF164`
- carrera: `systemtask` en el tope del bucle principal, con `ra == 0x800B7AA4`

Ahi se cuenta el frame, se aplica la entrada y se vuelca, en los dos lados. Y la clave
del volcado pasa a ser el **numero de frame**, no el campo, para que los registros se
alineen por posicion.

### Donde esta ahora, y que falta

Los dos lados producen 1 800 frames y volcados del mismo tamano, pero el estado sigue
divergiendo desde el frame 1, con una causa concreta todavia por explicar:

| | lado C | referencia |
|---|---|---|
| Primer `frame_top` | ticks **8 653 103**, campo 376, flip nº 3 | ticks **736 510** |
| Entradas al interprete | 122 898 | 97 974 |
| Ramas hacia atras (total) | ~15 M | 11 389 092 |

El interprete **no** es la causa: las dos entran un numero parecido de veces y, medido
con un histograma por direccion, en el lado C **las 122 898 son a la misma funcion**,
`0x80107358`. Lo que falta por entender es por que el lado C tarda 12 veces mas en
llegar a su primera frontera de frame. Candidatos a medir: que disparo el primer
`frame_top` de la referencia (puede no ser el flip del front end, porque tiene una
tercera via en `mod.rs`), y si el modo (front end / carrera) que la referencia consulta
antes de aceptar el flip cambia cual es el primer frame valido.

Efecto lateral a rehacer: el guion `into_race.txt` estaba afinado por **campos** y ahora
el indice son **frames**, asi que con la alineacion nueva ya no llega a la carrera
(0 vistas 3D). Hay que reafinarlo contra la cadencia de frames.

### El desfase del primer frame: explicado, y el diff ya compara de verdad

La causa no estaba en el codigo recompilado. La referencia tiene **cuatro** sitios que
emiten un frame, no tres, y el cuarto no pasa por `frame_top`: `loading_frames`
(`src/ps1/host/frame.rs`) emite **un frame por campo mientras el juego carga**, con el
estado del invitado congelado (ticks y llamadas identicos entre ellos, 736 510 y 1 256).
El host solo mantiene la pantalla viva mientras el juego lee del CD.

Esos frames no aportan nada al test diferencial — el estado no cambia — y desalinean las
secuencias, porque el port en C no los genera. Ahora se saltan cuando `NFSHS_DIFF_OUT`
esta puesto, o sea en modo diff.

Con eso, los dos lados arrancan en el mismo punto:

| Primer `frame_top` | lado C | referencia |
|---|---|---|
| Ticks | 8 653 103 | 8 652 495 |
| Via | flip del front end, `ra = 0x800bf164` | la misma |

**608 ticks de diferencia sobre 8,65 millones: el 0,007 %.**

### Primera comparacion real

Los dos lados producen 1 800 frames con el mismo guion, y en el frame 1:

| | |
|---|---|
| Ticks | +608 |
| Registros distintos | 8 (`r1`, `r5`–`r7`, `r14`, `r15`, `hi`, `lo`) |
| **Paginas de RAM distintas** | **15 de 512** (497 ya coinciden) |
| Profundidad de pila | 0 vs 6 (esperado: es diagnostico, ver mas arriba) |

Las paginas que divergen caen casi todas en el BSS (`0x13f000`, `0x148000`, `0x149000`) y
en los datos (`0x115000`), o sea en globales del juego. A partir de aqui el trabajo es el
bucle normal de convergencia: volcar esas paginas en crudo en los dos lados, ver que bytes
difieren y rastrear que funcion los escribe.

### Tres diagnosticos equivocados por el camino, y la leccion

Vale la pena anotarlo porque el patron se repitio:

1. Culpe al **reproductor de video**: parecia que sus frames explicaban el desfase. Lo
   desactive en los dos lados y la salida no cambio en absoluto — no era eso.
2. Concluí que sin video la referencia producia **cero frames**. Era falso: en esa prueba
   me falto `BOOT_FRAMES`, asi que no se imprimian, no es que no existieran.
3. Mi instrumentacion de `frame_top` **no imprimia nada** y lo interpreté como "no se
   llama". En realidad la condicion que puse (`frames_run < 8`) no contaba lo que creia, y
   luego un `head` en la tuberia mataba el proceso por SIGPIPE antes de que vaciara el
   buffer — lo que ademas trunco un volcado a 4 frames y me dio una comparacion falsa.

La leccion, concreta: **comprobar que la instrumentacion dispara antes de sacar
conclusiones de su silencio**, y no mezclar `head` ni tuberias con procesos que escriben
archivos. Las tres veces el codigo del port estaba bien y el error era de medicion.

Herramientas que quedan, utiles: `NFSHS_FRAMESRC` imprime los primeros `frame_top` de la
referencia con su via y su estado, y `NFSHS_DIFF_NO_MOVIE` desactiva el reproductor (no
hizo falta para esto, pero sirve cuando lleguemos al FMV).

### Acotando la primera divergencia: de 15 paginas a una funcion

La temporizacion de los dos lados es **identica** en el frame 1, lo que descarta el
reloj, los idles y el interprete de golpe:

| En el frame 1 | lado C | referencia |
|---|---|---|
| Reloj virtual | 12 032 | **12 032** |
| Idles del host | 413 | **413** |
| Entradas al interprete | 76 | **76** |
| Llamadas | 16 389 | 16 322 (**+67**, 0,4 %) |
| Ticks | 8 653 103 | 8 652 495 (**+608**, 0,007 %) |

Asi que la diferencia es que el codigo del juego ejecuta un poco mas en nuestro lado.
Para localizarlo se cuentan **todas** las llamadas que ve el host, por direccion y con el
numero de llamada en que aparece cada una por primera vez, en los dos lados
(`--callcount` en el arnes de C, `NFSHS_CALLCOUNT` en la referencia). De ~120
direcciones, **solo 4 difieren**:

| Direccion | Que es | C (veces / 1a) | Rust |
|---|---|---|---|
| `0x800f31d8` | HLE `InterruptCallback` | 1 / **14 276** | 2 / **14 276** |
| `0x80104338` | recompilada | 0 | 1 / 15 675 |
| `0x800cc00c` | recompilada | 182 / 15 940 | 0 |
| `0x80022158` | hook `DrawTVLines` | 32 / 16 333 | 0 |

Los dos lados llegan a `InterruptCallback` en **la misma llamada, la 14 276**: es la
raiz, y las otras tres son consecuencia (acabamos en otra pantalla del front end, por eso
dibujamos lineas de TV y ellos no).

Con `--idlelog` se ve con que argumentos: `InterruptCallback(linea=0x9,
manejador=0x80104338)`. **La linea 9 es la IRQ del SPU**, y el manejador es exactamente
`0x80104338`, la funcion que la referencia ejecuta una vez y nosotros nunca. O sea: el
juego registra un manejador de interrupcion del SPU, la referencia se la entrega y
nosotros no.

### Un hueco real del stub del SPU, tapado

Nuestro stub no tenia la **IRQ por direccion** del SPU: el hardware levanta la linea 9
cuando la RAM de sonido se toca en la direccion que el juego dejo en su registro de IRQ.
Portado de `check_irq` de la referencia, con todo lo que lleva: la bandera y su
reconocimiento al escribir `SPUCNT` sin el bit 6, el bit 6 de `SPUSTAT`, la comprobacion
en cada transferencia de DMA y las escrituras manuales por la FIFO de datos (registro
`0x1A8`), que tampoco estaban.

Es un hueco real y corregido, pero **no cambio el resultado**: el manejador sigue sin
correr. Y la traza de sonido de la propia referencia (`PS1_SNDLOG`) no muestra ninguna
`spu irq` en este escenario, asi que su segunda llamada a `InterruptCallback` no viene de
ahi.

### Lo que queda por explicar, con el hilo ya en la mano

La referencia **cuenta dos llamadas** a `0x800f31d8`, pero su propio manejador de
`InterruptCallback` (en `src/ps1/host/system.rs`) **no se ejecuta** en ese escenario —
comprobado: el binario tiene la traza compilada y no imprime. Es decir, hay otra via en
su host que atiende esa direccion antes de llegar al modulo de sistema, y es la que no
he portado. Encontrarla es el siguiente paso, y es un paso acotado.

### Herramienta nueva, y una leccion de metodo

`dc/test/diff/run.py` lanza los dos lados con el entorno pasado de forma explicita y
**comprueba que cada volcado se acaba de escribir** antes de comparar. Existe porque
invocarlos a mano me dio varios resultados falsos: un volcado de una ejecucion anterior
que parecia nuevo, un `head` en la tuberia que mato un proceso a mitad de escritura
(dejando un volcado de 4 frames que compare como si fueran 1 800), y variables de entorno
que no llegaban al proceso.

La leccion, y me costo tres rondas de aprenderla: **el diff sistematico de contadores por
direccion** dio la respuesta en una sola medicion, mientras que tres rondas de trazas con
`printf` me dieron tres diagnosticos equivocados. Cuando algo divergia, lo que hacia falta
era contar, no mirar.

### La primera divergencia, rastreada hasta su causa

Encadenando mediciones se llega al punto exacto. Las secuencias de llamadas de los dos
lados son **identicas hasta la llamada 15 674**, y en la **15 675**:

| | llamada 15 675 |
|---|---|
| lado C | `0x800f4c84` con `ra = 0` — el manejador del **timer** |
| referencia | `0x80104338` con `ra = 0` — el manejador de la **IRQ del SPU** |

Las dos con `ra = 0`, o sea entregas de interrupcion. La referencia tiene una IRQ del SPU
pendiente en ese instante y nosotros no. Su propia traza de sonido lo confirma con el
valor:

```
spu irq at 0x1048
InterruptCallback(linea=0x9, manejador=0x80104338) -> 0x0        (llamada 14276)
InterruptCallback(linea=0x9, manejador=0x80104338) -> 0x80104338 (llamada 15676)
```

El juego registra un manejador para la linea 9 (la del SPU), el SPU levanta la IRQ al
tocar la direccion `0x1048` de la RAM de sonido, y el manejador **se re-registra a si
mismo** — de ahi la segunda llamada, que devuelve el anterior.

**Por que no se dispara en nuestro lado.** La traza de nuestras transferencias dice:

```
dma4: spu[0x1000] 64 bytes  SPUCNT=0xc020 (irq off)  dir_irq=0
```

El juego **nunca habilita la IRQ** (bit 6 de `SPUCNT`) ni escribe su registro de
direccion. En la referencia si, porque su `check_irq` exige ese bit y se disparo. Y como
las secuencias de llamadas coinciden hasta ese punto, la diferencia no esta en el flujo
de control sino en **los valores que el driver de sonido lee de los registros del SPU**
— lecturas de memoria, invisibles en una traza de llamadas — con los que decide que
escribir.

Nuestro stub devuelve 0 en el volumen de envolvente de las voces y no modela ENDX ni la
reproduccion; la referencia si. El driver de EA lee esos valores y programa el SPU en
consecuencia, asi que termina escribiendo cosas distintas.

De paso se tapo un hueco real del stub, aunque no fuera la causa final: la **IRQ por
direccion** del SPU (la bandera, su reconocimiento al escribir `SPUCNT` sin el bit 6, el
bit 6 de `SPUSTAT`, la comprobacion en cada DMA y las escrituras manuales por la FIFO de
datos del registro `0x1A8`).

### La decision que esto plantea

Para que el test diferencial converja hace falta que el SPU sea **fiel**, no un stub: el
driver de sonido del juego lee sus registros y cambia de comportamiento segun lo que
encuentre. Dos caminos:

1. **Portar el modelo del SPU de la referencia** (voces, ADSR, ENDX, IRQ por direccion).
   Es trabajo real, pero hace falta igual en M3 para el audio, asi que no se tira.
2. **Neutralizar el SPU en los dos lados** para M1a: que la referencia use el mismo stub
   minimo. El test valida entonces la logica recompilada sin exigir fidelidad de audio,
   que es el objetivo declarado del hito; el SPU fiel se aborda en M3 con su propio
   criterio.

Es una decision de alcance, no tecnica, y queda para Jorge.

### Como se encontro: contar, no mirar

El camino fue: 15 paginas de RAM distintas -> contadores por frame identicos (reloj,
idles, interprete) -> **tabla de llamadas por direccion** (4 de ~120 difieren) -> orden
de primera aparicion (la raiz es `InterruptCallback`, en la misma llamada en los dos
lados) -> ventana de la secuencia de llamadas (identicas hasta la 15 674) -> traza del
SPU.

Cada paso fue una medicion que descartaba un espacio entero de hipotesis. Lo que no
funciono fue adivinar: en esta sesion tuve cuatro diagnosticos equivocados, y **todos**
salieron de instrumentacion mal verificada, no del codigo del port. El ultimo fue
instructivo: una ejecucion de la referencia terminaba en la llamada 1 256 porque sin
`NFSHS_DIFF_OUT` no se saltan los frames de carga y el guion acababa en el frame 2, asi
que ninguna traza llegaba a la llamada 14 276 y parecia que el codigo no se ejecutaba.

Herramientas que quedan: `--callcount` y `NFSHS_CALLCOUNT` (tabla por direccion con el
orden de aparicion), `--callwin` y `NFSHS_CALLWIN` (secuencia en una ventana), `--spulog`,
`--idlelog`, y `run.py`, que comprueba que los volcados se acaban de escribir antes de
comparar.

### Convergencia: el primer bug real encontrado por el test diferencial

Decision de Jorge (2026-10-18): **opcion 2**, neutralizar el SPU en los dos lados para
M1a y dejar el SPU fiel para M3. Se implemento por construccion, no parcheando casos:
`src/ps1/diffspu.rs` es un `IoBus` en la referencia que replica exactamente el stub del
port (`dc/src/host/spu.c`), y `new_cpu` lo usa cuando `NFSHS_DIFF_OUT` esta puesto. Por
el mismo criterio se neutralizaron los `VIDEO_*`, porque el modulo de video de la
referencia reserva un bloque en el heap del juego con una llamada anidada, y eso solo ya
desplazaba toda la secuencia de llamadas.

Con eso, el diff empezo a converger, y la herramienta nueva lo remato:
`dc/test/diff/bisect.py` vuelca los 2 MB en la **misma llamada** en los dos lados y
biseca hasta la primera donde la RAM difiere. La secuencia de llamadas no basta, porque
una diferencia de datos puede tardar miles de llamadas en cambiar el flujo.

Bisecando: identicas en la llamada 100, distintas en la 400, y la diferencia eran
**cuatro bytes**:

```
0x8013e558:  C = 05 00    Rust = 04 01
0x8014e284:  C = 01 00    Rust = 00 01
```

Son **handles de archivo**. La referencia empieza a numerarlos en `0x100` y el port
empezaba en 1, asi que el primer handle era `0x100` contra `1`, y el quinto `0x104`
contra `5`. El juego **guarda el handle en su RAM**, de modo que numerar distinto hace
divergir el estado desde la primera apertura de archivo. Una linea en `files.c`.

### El test diferencial pasa: 120 frames identicos

**Resultado (2026-10-18):** con el guion `front_end_120.txt` (front end, 120 frames),
`python -I dc/test/diff/run.py` da:

```
OK: 120 frames identicos (registros y hashes de RAM)
aviso: profundidad de la pila de diagnostico: 0 vs 6 (no es estado de la maquina)
```

Identico quiere decir: los **512 hashes de pagina** de los 2 MB de RAM, los 32 registros,
hi, lo, los 32 de cop0, los 64 del GTE, `cont` y el contador de ticks, en cada uno de los
120 frames. Y la alineacion es exacta: los dos lados llegan al frame 1 en la llamada
16 319 con 8 652 492 ticks, y al frame 2 en la 16 745 con 8 654 432.

Cifras de la corrida (lado C): 66 019 llamadas al host, 20 877 fuera de la lista HLE,
9 120 entradas al interprete, 499 campos de video para 120 frames, 1 200 `Font_TextXY`
atendidas y 2 520 rutinas de dibujado corridas en marco anidado.

### Los cuatro bugs que encontro el diff, y por que ninguno se veia en ejecucion normal

La convergencia fue 15 -> 13 -> 9 -> 6 -> 3 -> 2 -> 1 -> **0** paginas divergentes en el
frame 1. Los dos primeros pasos fueron neutralizar el SPU y el video en los dos lados
(decision de Jorge: opcion 2). Los cuatro siguientes fueron bugs de verdad:

**1. Numeracion de handles de archivo.** La referencia los numera desde `0x100` y el port
desde `1`. El juego **guarda el handle en su RAM**, asi que divergia desde la primera
apertura. Lo encontro `bisect.py` volcando los 2 MB en la misma llamada en los dos lados:
identicas en la 100, distintas en la 400, y la diferencia eran **cuatro bytes**
(`0x8013e558`, `0x8014e284`). Una linea en `files.c`.

**2. El modo de las vistas.** `Font_TextXY` (`0x800CC7F0`) no es un hook puro: la
referencia la trata **segun el modo** (`src/ps1/host/views.rs`). En carrera sin pausa deja
correr el original; en el front end y en carrera en pausa **la atiende el host y el
original no corre**. El port la tenia como hook puro. Se vio en la secuencia de llamadas:
en la 15 940 el port llamaba cuatro veces a `0x800CC00C` y la referencia ya estaba en otra
cosa. Arreglado con `dc/src/host/views.c`, que ademas introduce el modo
(`HOST_FRONTEND`/`HOST_RACE`) y sus tres transiciones.

**3. El marco anidado de la captura.** La mas instructiva, porque una sola causa explicaba
tres sintomas sin relacion aparente. La referencia no deja correr en su sitio las diez
rutinas del front end que construyen sus propias primitivas (`CAPTURED_FE_DRAWING`): las
corre en un **marco anidado** (`capture_prims`) para leer el buffer de primitivas antes y
despues. Eso copia 16 palabras de argumentos a `(sp - 0x100) & ~7`, mueve `sp`, y es **una
llamada y un tick mas** por invocacion. De ahi salian: el desfase de **21 ticks y 21
llamadas** por frame, **60 bytes de pila** distintos, y que el port ejecutara
`DrawTVLines` **32 veces y la referencia ninguna** (con otro `sp`, la rama `bne v0, zero`
de `0x800226A4` se decidia al contrario). `ps1_call_nested` ya existia en el runtime en C,
asi que fueron ~50 lineas.

**4. El orden del volcado (fallo del instrumento, no del port).** El port escribia los
buffers del pad **antes** de volcar el estado, y la referencia **despues** (su volcado
ocurre dentro del callback `on_frame`, y los pads se escriben luego en `frame.rs`). El
volcado del port llevaba los botones de este frame y el de la referencia los del anterior:
dos paginas distintas con la RAM identica en la frontera de la llamada. Invertido en
`pad.c`.

Ninguno de los cuatro se veia en ejecucion normal: el juego funcionaba igual. Habrian sido
un dolor de cabeza en M2 o M3, cuando algo dependiera del valor.

### `unlock_everything`: una funcion de la referencia, no un bug

Los ultimos 26 bytes de los 2 MB eran todos `0x00` contra `0x01`, dentro de `CAR_MANAGER`
(`0x8011552C`) y `TRACK_MANAGER` (`0x80115D08`). La causa es `unlock_everything`
(`src/ps1/host/mod.rs`): la referencia **desbloquea las once pistas y todos los coches en
cada frame del front end**, que es una conveniencia suya y trae su propio interruptor.

**Decision:** se apaga en el lado de la referencia con `NFSHS_LOCKED=1` (en `run.py` y
`bisect.py`), igual que el SPU y el video, en vez de copiar la trampa al port. Si el
Dreamcast acaba ofreciendo un «desbloquear todo», es una decision de producto de M5, no
algo que deba colarse por la puerta de atras de un test.

### La segunda pasada, ya implementada

El formato de `dump.c` anticipaba una segunda pasada que no existia: los hashes por pagina
dicen *que* pagina cambia, pero no *que bytes*. Ahora `--framedump N FILE` (lado C) y
`NFSHS_FRAMEDUMP=N:FILE` (referencia) vuelcan los 2 MB en crudo **en la frontera del
frame**, que es el punto que hace falta: el volcado de un frame ocurre *dentro* de una
llamada al host, asi que un disparador por numero de llamada no sirve. Fue lo que redujo
el ultimo caso a «26 bytes en 2 MB».

### El escenario de carrera: alcanzable, y el siguiente bloqueo localizado

**La navegacion ya no se adivina.** El guion va en numeros de frame y probar pulsaciones
a ciegas costaba una corrida por intento. Ahora el juego lo dice: el host atiende
`Font_TextXY`, asi que `--textlog` saca el texto en pantalla con sus coordenadas, y
`dc/test/diff/screens.py` lo resume en «que pantalla es y que opcion esta resaltada». El
truco que lo hace posible: **la opcion resaltada se dibuja en una x distinta** de las
demas (el front end pinta cada entrada alineada, menos la seleccionada). Con eso, el
camino salio en cuatro corridas:

```
[game setup]   arranca en "game options"  -> UP x2 -> "one player"
[race type]    arranca en "test drive"    -> aceptar
[track select] germany / landstrasse      -> aceptar
[car select]                              -> aceptar -> carrera
```

Y una correccion de rumbo por el camino: **`unlock_everything` no era cosmetico.** Lo
habia apagado en la referencia (`NFSHS_LOCKED=1`) pensando que era solo una conveniencia,
pero con todo bloqueado el juego no deja entrar a una carrera («you must buy a car from
the car dealer before you can enter this race»), asi que **sin eso no existe escenario de
carrera**. Esta portado en `dc/src/host/unlock.c`, **apagado por defecto** y encendido con
`--unlock`, y el test lo enciende en los dos lados (que es el comportamiento por defecto
de la referencia). El front end sigue dando **120 frames identicos** con los dos
desbloqueados, lo que de paso **valida el port byte a byte**: la referencia escribe esas
banderas en cada frame, asi que escribirlas distinto habria divergido.

Nota: `unlock_everything` marca *disponible* y *desbloqueado*, no **propiedad**. Por eso el
guion va por `test drive` y no por `single race`: con la tarjeta vacia no se tiene coche.

**Donde se rompe la carrera.** Con `into_race.txt` (900 frames), los frames 1-184 son
identicos y el **185** diverge: ticks +2779, `r5`-`r7` desplazados `0x73F0`, y por primera
vez **registros del GTE** (el front end no ejecuta ni una instruccion cop2). La segunda
pasada da 32 752 bytes en 7 910 rachas, y la primera por direccion es `0x8122306`, a 58
bytes de `SHOWROOM_TEX` (`0x801222CC`).

Es el **showroom**, o sea la pantalla de seleccion de coche. La referencia atiende
`R3DCar_InsertCarFacetMenuII` (`0x800B3428`) y `R3DCar_Showroom` (`0x800B3850`) en el front
end, y su manejador **escribe RAM** por varias vias (`src/ps1/host/game.rs`):

1. `w32(0x1F800000 + 140, nivel * 0x010101)` en el scratchpad;
2. `call_nested(DRAWC_PRIM_START, [R3DCAR_CENTER, a0, nivel, CAR_CACHE])`, que construye
   los mapas de reflejo en `CAR_CACHE`;
3. si procede, `capture_prims(DRAWC_SHADOW_PRIM_CLIP)` para la sombra del suelo;
4. una **segunda llamada anidada a `0x800B3428` sobre si misma**, con `inner_hook =
   record_car_facet` y `HALO_PASS`, que corre el original **saltandose el dibujado de la
   carroceria** (`CAR_BODY_DRAWING`) para quedarse solo con los halos de los faros.

`inner_hook` y `ps1_call_nested` ya existen en el runtime en C, asi que la mecanica es
portable; lo que no esta es el manejador.

**Y hay una decision de alcance detras.** `record_car_facet` no solo observa: en el paso de
visibilidad **ensancha a proposito el cono de los coches** (`CAR_CONE_WIDTH`, escalando la
fila x de la vista antes de una llamada anidada y restaurandola despues). Eso es una
*mejora* deliberada del renderer de la referencia, no fidelidad al original, y escribe RAM.
Portarla al Dreamcast para que cuadre un test seria heredar sin querer una decision de
producto, igual que pasaba con `unlock_everything`.

### Opcion A, primer tramo: el showroom (hecho, con su coste medido)

Jorge eligio la opcion A (portar la capa de observacion) **empezando por el showroom**,
justo para medir el coste por manejador antes de comprometerse con el resto. El resultado:

| Pieza | Codigo | Ciclos de medicion | Efecto |
|---|---|---|---|
| `R3DCar_InsertCarFacetMenuII` (`0x800B3428`) | ~100 lineas | 1 | 32 752 -> **6 815 bytes**; los `gte[]` dejan de divergir; ticks +2779 -> +168 |
| `R3DCar_Showroom` (`0x800B3850`) | ~70 lineas | 1 | primera divergencia del frame **185 al 232** |
| frontera de frame del flip | **1 linea** | 1 | `r31` vuelve a cuadrar |

`dc/src/host/showroom.c`. Lo que hace cada uno:

- **El coche**: nivel de luz al cache, `DrawC_PrimStart` anidado (mapas de reflejo), la
  sombra del suelo con `capture_prims`, y una **segunda llamada anidada a si misma** con un
  `inner_hook` que salta las seis rutinas de `CAR_BODY_DRAWING` para quedarse solo con los
  halos de los faros. `inner_hook` y `ps1_call_nested` ya estaban en el runtime.
- **La plataforma**: `showroom_gte` (la camara de la vista en los registros de control 32-39,
  matriz transpuesta con la y volteada) y **193 proyecciones RTPS** en orden exacto: 128 del
  anillo (32 cuadros de 4 puntos, solo si `0x8005199C` no es cero), 1 del centro y 64 del
  borde. Las primitivas no se emiten (eso es M2a), pero **las proyecciones si**, porque los
  64 registros del GTE entran en la comparacion por frame. Esto es el ejemplo claro de
  «la grabacion se mete en el estado de la maquina».

### El quinto bug: dos fronteras de frame por frame en carrera

El port trataba el flip del motor como frontera de frame **siempre**, y la referencia solo
en el front end (`gpu.rs` lo condiciona a `Mode::FrontEnd`). En carrera la frontera es el
systemtask del tope del bucle principal, asi que el port contaba **dos frames por cada
uno**: alternaba flip y systemtask desde el frame 232 mientras la referencia se quedaba en
systemtask.

Se vio analizando `r[31]` de los dos volcados por frame, que ya estaba grabado. No se
podia ver antes porque el port **no tenia concepto de modo** hasta que lo trajo `views.c`
hoy mismo. Es un bug con consecuencias reales para M1b y M2 (el ritmo de frames en
carrera), y una linea de arreglo.

### Donde esta la carrera ahora

Con `into_race.txt` (900 frames), los frames **1-231 son identicos** y el 232 —el primer
frame de carrera— diverge en los registros del GTE. El siguiente manejador es
`Render_RenderPlayerView` (`0x800B4024`), y es el grande:

| | lineas | escrituras a RAM | llamadas anidadas |
|---|---|---|---|
| Manejador de `0x800B4024` | 183 | 3 | **18** |

O sea que **18 de las 33 llamadas anidadas de toda la capa estan en ese solo manejador**:
el montaje de la vista del mundo (`Render_StartRenderingWorldView`), las matrices
(`Setup_BuildMatrices`, `DrawW_WorldSetupMatrix`), la lista de chunks
(`Setup_ChunkBuildList`), los objetos y los destellos de las luces de pista. Es
exactamente la capa de observacion de M2b y M2c.

### Lo que queda abierto

- **La carrera.** El criterio de M1a pide front end **y una carrera de prueba**. El front
  end esta cerrado con 120 frames; el guion de carrera hay que rehacerlo. Dato medido que
  lo abarata: **campo de video ~= 375 + frame** (499 campos para 120 frames; el frame 1
  cae en el campo 376). El `into_race.txt` viejo esta en unidades de campo y sus
  pulsaciones de los campos 120-330 caen *antes* del primer frame registrado, porque en
  modo diff los frames de carga se saltan; por eso ya no llega a una carrera.
- **Los cuatro ayudantes del HUD** (`Hud_BuildString` y tres mas) los atiende la
  referencia devolviendo **anchos calculados**. En el escenario del front end no se llaman
  ni una vez (medido con `--callcount`), asi que el port los deja correr, los cuenta, y
  avisa por `stderr` y en el informe si un guion de carrera los toca. Portar ese calculo
  sin un escenario que lo ejercite seria escribir a ciegas.
- **La pila de diagnostico del port** se queda clavada en 0 mientras la de la referencia
  sigue el anidamiento (5-6). Los dos arboles generados emiten exactamente los mismos
  **3 674 `push` y 7 687 `pop`**, asi que la causa esta en el runtime, no en el backend. No
  es estado de la maquina (el `sp` real vive en la RAM, que coincide byte a byte), pero
  degrada los mensajes de panico, asi que hay que arreglarlo. `cmp.py` lo pasa a **aviso**
  con el razonamiento escrito, para que no tumbe el test pero siga a la vista.

### La leccion de metodo, confirmada

El camino que funciono fue siempre el mismo: **contar y bisecar, no mirar**. Cada
herramienta nueva descarto un espacio entero de hipotesis en una medicion:

- contadores por frame (reloj, idles, interprete) -> descarto la temporizacion
- tabla de llamadas por direccion -> 4 divergencias de ~120
- orden de primera aparicion -> cual es la raiz y cuales la consecuencia
- ventana de la secuencia de llamadas -> la llamada exacta
- biseccion de la RAM por numero de llamada -> **cuatro bytes**

Y de paso: el test diferencial **ya esta pagando por si mismo**. El bug de los handles
era invisible en la ejecucion normal (el juego funcionaba), y habria sido un dolor de
cabeza en M2 o M3 cuando algo dependiera del valor.

## Pasos pendientes

| Paso | Bloqueado por |
|---|---|
| 2. `ps1rt` en C (`dc/src/ps1rt/`) | en curso: falta el cuerpo de `bios.c` e `interp.c`, que conviene medir antes |
| 3. Host x86 (`dc/src/platform/pc/`, `dc/src/host/`) | **hecho para M1a**: 65 HLE, front end y carrera corriendo con entrada guionizada |
| 4. Test diferencial (`dc/test/diff/`) | rustup, para construir el oráculo |

Lo primero del paso 4, antes de escribir host: correr la referencia Rust **dos veces** con
el mismo guion de entrada y comprobar que el volcado es idéntico byte a byte. Si no lo es,
forzar el determinismo es trabajo previo, y es el mayor riesgo de M1a.

---

## M1a cerrado (2026-10-18): la decision y donde se corta

El criterio de salida pedia "front end y una carrera de prueba, N frames identicos". Se
cumple asi:

| Escenario | Frames identicos | Que cubre |
|---|---|---|
| `front_end_120.txt` | **120 / 120** | front end completo |
| `into_race.txt` | **231** de 900 | front end, navegacion de menus y la **transicion a carrera** |

En los dos casos la comparacion es la completa: 512 hashes de pagina de 4 KB, los 32
registros, hi, lo, los 32 de cop0, los 64 del GTE, `cont` y los ticks. Pruebas unitarias
de `ps1rt`: **227/227**.

El frame 232 —el primer frame de carrera— diverge **solo en los registros del GTE**, y la
causa esta identificada y acotada: el manejador de `Render_RenderPlayerView`
(`0x800B4024`) no esta portado, y con el 18 de las 33 llamadas anidadas de la capa de
observacion de la referencia.

**Decision de Jorge: se corta aqui y se pasa a M1b.** El razonamiento, por si hay que
revisarlo:

- Los **cinco bugs** que encontro el test estaban todos en la **integracion host/juego**
  (numeracion de handles, concepto de modo, marco anidado de captura, orden del volcado,
  frontera de frame segun el modo), no en la recompilacion ni en el GTE. Esa clase de bug
  ya esta barrida por los 231 frames, que incluyen la transicion a carrera, que era donde
  vivian.
- Lo que queda por validar tras `0x800B4024` es la **grabacion de geometria**: matrices,
  lista de chunks, objetos y destellos. Eso se escribe de todas formas en **M2b y M2c**,
  con el renderer del PVR delante, y entonces el oraculo sale casi gratis.
- Portar `0x800B4024` solo para el test seria **escribir andamio**: la mecanica sin la
  grabacion es codigo que M2a reemplaza. Lo que si se conserva es el **conocimiento** de a
  donde va cada llamada anidada, y eso ya esta medido y escrito aqui.

Coste medido del tramo que si se hizo (la opcion A, primer tramo), para calibrar el resto:

| Pieza | Codigo | Ciclos de medicion | Efecto |
|---|---|---|---|
| `R3DCar_InsertCarFacetMenuII` | ~100 lineas | 1 | 32 752 -> **6 815 bytes** divergentes; los `gte[]` dejan de divergir; ticks +2 779 -> **+168** |
| `R3DCar_Showroom` (plataforma) | ~70 lineas | 1 | primera divergencia del frame **185 -> 232** |
| Frontera de frame del flip | **1 linea** | 1 | `r[31]` vuelve a cuadrar |

O sea ~170 lineas y 3 ciclos para avanzar 47 frames. El siguiente paso no era otro tramo
parecido: era **la mitad de la capa en un solo manejador**.

### Lo que queda anotado como deuda, no como olvido

1. **`Render_RenderPlayerView` (`0x800B4024`)**: 183 lineas, 3 escrituras a RAM, 18
   llamadas anidadas. Se retoma en **M2b**, cuando el renderer le de sentido.
2. **Los cuatro ayudantes del HUD** (`Hud_BuildString` 0x800D4E9C, 0x800D8690, 0x800D9ABC,
   0x800D3558): la referencia devuelve anchos calculados. El port los deja correr, los
   cuenta y **avisa**. En el front end no se llaman ni una vez.
3. **`CAR_CONE_WIDTH = 2`**: mejora deliberada de la referencia (cono de visibilidad de
   coches mas ancho) que **escribe en la RAM**. Si se porta, va detras de una opcion
   apagada por omision, igual que `--unlock`.
4. **La pila de diagnostico del port** clavada en 0 (la referencia marca 5-6). Los dos
   arboles emiten los mismos 3 674 `push` y 7 687 `pop`, asi que la causa esta en el
   runtime. No es estado de la maquina; `cmp.py` lo trata como aviso.

### Lo que el test diferencial deja hecho para siempre

El arnes no se tira al cerrar M1a: queda como **oraculo reutilizable** para M2. Sigue
sirviendo para comparar cualquier cambio futuro del runtime o del backend C contra la
referencia Rust, y los guiones (`front_end_120.txt`, `into_race.txt`) son escenarios
reproducibles. Herramientas que quedan: `run.py`, `cmp.py`, `bisect.py` (biseccion de la
RAM por numero de llamada) y `screens.py` (que pantalla hay y que opcion esta resaltada,
derivado de `--textlog`).
