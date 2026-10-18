/* Pruebas unitarias de ps1rt. Sin dependencias: se compila y se ejecuta.
 *
 * Los valores esperados salen de la semantica de la referencia Rust
 * (crates/ps1rt/src/lib.rs) y del R3000 documentado en psx-spx; los de lwl/lwr/swl/swr
 * estan calculados a mano para las cuatro alineaciones.
 */
#include "ps1rt.h"

#include <stdio.h>
#include <string.h>

static int fallos, total;

#define CHECK(expr)                                                                   \
    do {                                                                              \
        total++;                                                                      \
        if (!(expr)) {                                                                \
            fallos++;                                                                 \
            printf("  FALLO %s:%d  %s\n", __FILE__, __LINE__, #expr);                 \
        }                                                                             \
    } while (0)

#define CHECK_EQ(got, want)                                                           \
    do {                                                                              \
        uint64_t g_ = (uint64_t)(got), w_ = (uint64_t)(want);                          \
        total++;                                                                      \
        if (g_ != w_) {                                                               \
            fallos++;                                                                 \
            printf("  FALLO %s:%d  %s: 0x%llx, esperado 0x%llx\n", __FILE__,          \
                   __LINE__, #got, (unsigned long long)g_, (unsigned long long)w_);   \
        }                                                                             \
    } while (0)

static uint8_t ram_buf[PS1_RAM_SIZE + PS1_RAM_PAD];
static Cpu cpu;

static Cpu *fresh(void)
{
    ps1_init(&cpu, ram_buf);
    return &cpu;
}

/* ---------------------------------------------------------------- mapa de memoria */
static void test_mem_map(void)
{
    Cpu *c = fresh();
    puts("mapa de memoria");

    /* RAM basica, los tres tamanos, little-endian */
    ps1_w32(c, 0x80001000u, 0x44332211u);
    CHECK_EQ(ps1_r32(c, 0x80001000u), 0x44332211u);
    CHECK_EQ(ps1_r8(c, 0x80001000u), 0x11u);
    CHECK_EQ(ps1_r8(c, 0x80001003u), 0x44u);
    CHECK_EQ(ps1_r16(c, 0x80001002u), 0x4433u);

    /* Espejos KUSEG / KSEG0 / KSEG1: la misma palabra */
    CHECK_EQ(ps1_r32(c, 0x00001000u), 0x44332211u);
    CHECK_EQ(ps1_r32(c, 0xA0001000u), 0x44332211u);

    /* El umbral de IO es 0x00800000, NO 0x200000: el rango 0x200000..0x7FFFFF se
     * refleja dentro de la RAM a traves de la mascara de 2 MB. Esto es lo que
     * CLAUDE.md seccion 9 describia mal. */
    CHECK_EQ(ps1_r32(c, 0x80201000u), 0x44332211u);   /* 0x201000 & 0x1FFFFF = 0x1000 */
    CHECK_EQ(ps1_r32(c, 0x80601000u), 0x44332211u);   /* 0x601000 & 0x1FFFFF = 0x1000 */

    /* la frontera exacta: 0x7FFFFF es el ultimo byte que llega a la RAM (y cae en su
     * tope, 0x1FFFFF), y 0x800000 ya es el primero que se va a IO */
    ps1_w8(c, 0x801FFFFFu, 0x7Eu);
    CHECK_EQ(ps1_r8(c, 0x807FFFFFu), 0x7Eu);
    CHECK_EQ(ps1_r8(c, 0x80800000u), 0u);

    /* 0x00800000 en adelante ya es IO: lee 0 y las escrituras se tiran */
    {
        uint64_t io0 = c->io_accesses;
        ps1_w32(c, 0x80800000u, 0xDEADBEEFu);
        CHECK_EQ(ps1_r32(c, 0x80800000u), 0u);
        CHECK(c->io_accesses > io0);
        /* y no ha tocado la RAM reflejada en 0x000000 */
        CHECK_EQ(ps1_r32(c, 0x80000000u), 0u);
    }

    /* Scratchpad: 1 KB propio, no es la RAM */
    ps1_w32(c, 0x1F800000u, 0xCAFEF00Du);
    CHECK_EQ(ps1_r32(c, 0x1F800000u), 0xCAFEF00Du);
    CHECK_EQ(ps1_r32(c, 0x80000000u), 0u);            /* la RAM sigue intacta */
    ps1_w8(c, 0x1F8003FFu, 0x5Au);                    /* ultimo byte */
    CHECK_EQ(ps1_r8(c, 0x1F8003FFu), 0x5Au);
    CHECK_EQ(ps1_r8(c, 0x1F800400u), 0u);             /* justo despues ya es IO */

    /* Volumen de envolvente del SPU: lo unico que el modelo sin IoBus no devuelve 0 */
    c->spu_volume[1] = 0x1234u;
    CHECK_EQ(ps1_r16(c, 0x1F801C1Cu), 0x1234u);       /* voz 1, desplazamiento 0xC */
    CHECK_EQ(ps1_r16(c, 0x1F801C1Au), 0u);            /* otro desplazamiento: 0 */
    CHECK_EQ(ps1_r32(c, 0x1F801C1Cu), 0x1234u);       /* mitad alta a 0 */
}

/* --------------------------------------------------------- accesos desalineados */
static void test_mem_unaligned(void)
{
    Cpu *c = fresh();
    puts("accesos desalineados y borde de la RAM");

    /* bytes 11 22 33 44 55 en 0x1000.. */
    ps1_w32(c, 0x80001000u, 0x44332211u);
    ps1_w8(c, 0x80001004u, 0x55u);

    /* una lectura desalineada es una lectura desalineada DE VERDAD, no enmascarada:
     * r32 en +1 toma los bytes 22 33 44 55, no la palabra alineada */
    CHECK_EQ(ps1_r32(c, 0x80001001u), 0x55443322u);
    CHECK_EQ(ps1_r16(c, 0x80001001u), 0x3322u);
    CHECK(c->misaligned >= 2);

    /* escritura desalineada */
    ps1_w32(c, 0x80001001u, 0xAABBCCDDu);
    CHECK_EQ(ps1_r8(c, 0x80001000u), 0x11u);          /* no toca el byte anterior */
    CHECK_EQ(ps1_r8(c, 0x80001001u), 0xDDu);
    CHECK_EQ(ps1_r8(c, 0x80001004u), 0xAAu);

    /* Borde superior de la RAM: la referencia Rust entra en panico por indice fuera
     * de rango; aqui hay relleno explicito y un contador, para que se vea en vez de
     * leer basura en silencio. */
    {
        uint64_t edge0 = c->ram_edge;
        (void)ps1_r32(c, 0x801FFFFFu);
        CHECK(c->ram_edge > edge0);
    }
}

/* --------------------------------------------------------------------- aritmetica */
static void test_alu_divmul(void)
{
    Cpu *c = fresh();
    puts("mult / div y sus rarezas");

    ps1_div(c, 7u, 2u);
    CHECK_EQ(c->lo, 3u);
    CHECK_EQ(c->hi, 1u);

    ps1_div(c, (uint32_t)-7, 2u);
    CHECK_EQ((int32_t)c->lo, -3);
    CHECK_EQ((int32_t)c->hi, -1);

    /* division por cero: lo depende del signo del dividendo, hi es el dividendo */
    ps1_div(c, 5u, 0u);
    CHECK_EQ(c->lo, 0xFFFFFFFFu);
    CHECK_EQ(c->hi, 5u);

    ps1_div(c, (uint32_t)-5, 0u);
    CHECK_EQ(c->lo, 1u);
    CHECK_EQ(c->hi, (uint32_t)-5);

    /* INT32_MIN / -1 no desborda: el R3000 deja el dividendo y resto 0 */
    ps1_div(c, 0x80000000u, 0xFFFFFFFFu);
    CHECK_EQ(c->lo, 0x80000000u);
    CHECK_EQ(c->hi, 0u);

    ps1_divu(c, 7u, 2u);
    CHECK_EQ(c->lo, 3u);
    CHECK_EQ(c->hi, 1u);

    ps1_divu(c, 0x12345678u, 0u);
    CHECK_EQ(c->lo, 0xFFFFFFFFu);
    CHECK_EQ(c->hi, 0x12345678u);

    /* 0xFFFFFFFF como unsigned es grande; como signed es -1 */
    ps1_divu(c, 0xFFFFFFFFu, 2u);
    CHECK_EQ(c->lo, 0x7FFFFFFFu);

    ps1_mult(c, (uint32_t)-3, 5u);
    CHECK_EQ(c->lo, (uint32_t)-15);
    CHECK_EQ(c->hi, 0xFFFFFFFFu);                     /* signo extendido en hi */

    ps1_mult(c, 0x10000u, 0x10000u);
    CHECK_EQ(c->lo, 0u);
    CHECK_EQ(c->hi, 1u);

    ps1_multu(c, 0xFFFFFFFFu, 0xFFFFFFFFu);
    CHECK_EQ(c->lo, 1u);
    CHECK_EQ(c->hi, 0xFFFFFFFEu);
}

/* --------------------------------------------------- lwl / lwr / swl / swr */
static void test_alu_unaligned_ops(void)
{
    Cpu *c = fresh();
    const uint32_t A = 0x80001000u;
    const uint32_t CUR = 0xAABBCCDDu;
    const uint32_t V = 0xAABBCCDDu;
    /* valores esperados calculados a mano, alineacion 0..3 */
    static const uint32_t lwl_exp[4] = { 0x11BBCCDDu, 0x2211CCDDu, 0x332211DDu, 0x44332211u };
    static const uint32_t lwr_exp[4] = { 0x44332211u, 0xAA443322u, 0xAABB4433u, 0xAABBCC44u };
    static const uint32_t swl_exp[4] = { 0x443322AAu, 0x4433AABBu, 0x44AABBCCu, 0xAABBCCDDu };
    static const uint32_t swr_exp[4] = { 0xAABBCCDDu, 0xBBCCDD11u, 0xCCDD2211u, 0xDD332211u };
    int k;
    puts("lwl / lwr / swl / swr en las cuatro alineaciones");

    for (k = 0; k < 4; k++) {
        ps1_w32(c, A, 0x44332211u);
        CHECK_EQ(ps1_lwl(c, A + (uint32_t)k, CUR), lwl_exp[k]);
        CHECK_EQ(ps1_lwr(c, A + (uint32_t)k, CUR), lwr_exp[k]);
    }
    for (k = 0; k < 4; k++) {
        ps1_w32(c, A, 0x44332211u);
        ps1_swl(c, A + (uint32_t)k, V);
        CHECK_EQ(ps1_r32(c, A), swl_exp[k]);

        ps1_w32(c, A, 0x44332211u);
        ps1_swr(c, A + (uint32_t)k, V);
        CHECK_EQ(ps1_r32(c, A), swr_exp[k]);
    }
    /* estos cuatro siempre leen y escriben la palabra ALINEADA, asi que no deben
     * contar como accesos desalineados */
    CHECK_EQ(c->misaligned, 0u);
}

/* ------------------------------------------------------------------ carga del EXE */
static void test_load_exe(void)
{
    Cpu *c = fresh();
    uint8_t exe[0x800 + 16];
    puts("carga de PS-X EXE");
    memset(exe, 0, sizeof(exe));
    memcpy(exe, "PS-X EXE", 8);
    /* t_addr = 0x80001000, t_size = 8 */
    exe[0x18] = 0x00; exe[0x19] = 0x10; exe[0x1A] = 0x00; exe[0x1B] = 0x80;
    exe[0x1C] = 8;
    memcpy(exe + 0x800, "ABCDEFGH", 8);
    ps1_load_exe(c, exe, sizeof(exe));
    CHECK_EQ(ps1_r8(c, 0x80001000u), 'A');
    CHECK_EQ(ps1_r8(c, 0x80001007u), 'H');
    CHECK_EQ(ps1_r8(c, 0x80001008u), 0u);
}

/* ------------------------------------------------- cpu.c: orden de despacho */

static int  dispatch_calls, dispatch_ret;
static uint32_t dispatch_last;
static int  host_calls, host_handled, host_saw_null_host, inner_calls;

static int mock_dispatch(Cpu *c, uint32_t addr)
{
    (void)c;
    dispatch_calls++;
    dispatch_last = addr;
    return dispatch_ret;
}

static int mock_host_call(Cpu *c, void *self, uint32_t addr)
{
    (void)self;
    (void)addr;
    host_calls++;
    /* ps1_call retira el host mientras corre, para que sus llamadas anidadas no
     * puedan reentrar en el */
    if (c->host == NULL)
        host_saw_null_host++;
    return host_handled;
}

static int mock_inner(Cpu *c, uint32_t addr)
{
    (void)c;
    (void)addr;
    inner_calls++;
    return 1;
}

static void test_call_order(void)
{
    Cpu *c = fresh();
    Ps1Host host;
    puts("orden de despacho de ps1_call");
    host.call = mock_host_call;
    host.idle = NULL;
    host.self = NULL;

    c->dispatch = mock_dispatch;
    dispatch_ret = 1;

    /* 1. los vectores de la BIOS ganan: no se consulta ni al host ni al codigo */
    c->host = &host;
    host_handled = 1;
    host_calls = 0;
    dispatch_calls = 0;
    ps1_w8(c, 0x80002000u, (uint32_t)'h');
    ps1_w8(c, 0x80002001u, 0u);
    c->r[9] = 0x19u;                                  /* strcpy */
    c->r[4] = 0x80002100u;
    c->r[5] = 0x80002000u;
    ps1_call(c, 0xA0u);
    CHECK_EQ(ps1_r8(c, 0x80002100u), (uint32_t)'h'); /* lo atendio el BIOS */
    CHECK_EQ(dispatch_calls, 0);
    CHECK_EQ(host_calls, 0);

    /* y tambien con la direccion en KSEG0, que es como la llama el juego: la
     * comprobacion es sobre addr & 0x1FFFFFFF. B0 no esta implementada, asi que
     * cuenta como no atendida, pero sigue ganando al host y al despacho */
    ps1_call(c, 0x800000B0u);
    CHECK_EQ(c->bios_unhandled, 1u);
    CHECK_EQ(dispatch_calls, 0);
    CHECK_EQ(host_calls, 0);

    /* 2. el host va antes que el codigo recompilado */
    host_handled = 1;
    host_calls = 0;
    dispatch_calls = 0;
    ps1_call(c, 0x80012340u);
    CHECK_EQ(host_calls, 1);
    CHECK_EQ(dispatch_calls, 0);

    /* 3. si el host no la atiende, cae en el codigo recompilado */
    host_handled = 0;
    host_calls = 0;
    dispatch_calls = 0;
    ps1_call(c, 0x80012340u);
    CHECK_EQ(host_calls, 1);
    CHECK_EQ(dispatch_calls, 1);
    CHECK_EQ(dispatch_last, 0x80012340u);

    /* 4. inner_hook solo se consulta cuando NO hay host */
    c->host = NULL;
    c->inner_hook = mock_inner;
    inner_calls = 0;
    dispatch_calls = 0;
    ps1_call(c, 0x80012340u);
    CHECK_EQ(inner_calls, 1);
    CHECK_EQ(dispatch_calls, 0);

    /* 5. el host se retira mientras corre */
    c->host = &host;
    host_handled = 1;
    host_calls = 0;
    host_saw_null_host = 0;
    ps1_call(c, 0x80012340u);
    CHECK_EQ(host_calls, 1);
    CHECK_EQ(host_saw_null_host, 1);
}

/* ------------------------------------------------------------- tick y parada */
static void test_tick(void)
{
    Cpu *c = fresh();
    puts("tick, since_host y parada del programa");
    c->dispatch = mock_dispatch;
    dispatch_ret = 1;

    CHECK_EQ(c->ticks, 0u);
    ps1_tick(c);
    CHECK_EQ(c->ticks, 1u);
    CHECK_EQ(c->since_host, 1u);

    /* ps1_call tiquea una vez (las llamadas directas f_X(c) que emite el
     * recompilador NO tiquean, igual que en la referencia) */
    ps1_call(c, 0x80012340u);
    CHECK_EQ(c->ticks, 2u);

    /* sin host, since_host sigue creciendo; con host, se pone a 0 y queda la marca */
    CHECK_EQ(c->since_host, 2u);
    {
        Ps1Host host;
        host.call = mock_host_call;
        host.idle = NULL;
        host.self = NULL;
        c->host = &host;
        host_handled = 1;
        ps1_call(c, 0x80012340u);
        CHECK_EQ(c->since_host, 0u);
        CHECK_EQ(c->max_since_host, 3u);
        /* y queda anotada en recent para diagnosticar cuelgues */
        CHECK_EQ(c->recent[0][0], 0x80012340u);
        CHECK_EQ(c->recent_at, 1u);
    }
}

static void test_stop(void)
{
    Cpu *c = fresh();
    puts("tick_limit 0 sale del programa por longjmp");
    c->tick_limit = 0;
    if (setjmp(c->stop_jmp) == 0) {
        c->stop_armed = 1;
        ps1_tick(c);
        CHECK(0);                      /* no deberia volver de ahi */
    } else {
        CHECK(1);                      /* salio por el longjmp */
        CHECK_EQ(c->stop_armed, 0);
    }
}

/* ------------------------------------------------------- continuaciones */
static int cont_steps;

static int cont_dispatch(Cpu *c, uint32_t addr)
{
    (void)addr;
    cont_steps++;
    if (cont_steps < 3)
        c->cont = 0x80000000u + (uint32_t)cont_steps * 4u;
    return 1;
}

static void test_cont(void)
{
    Cpu *c = fresh();
    puts("continuaciones encadenadas");
    c->dispatch = cont_dispatch;
    cont_steps = 0;
    ps1_call(c, 0x80010000u);
    /* la primera deja una continuacion; el trampolin sigue hasta que nadie deja
     * ninguna, sin hacer crecer la pila del anfitrion */
    CHECK_EQ(cont_steps, 3);
    CHECK_EQ(c->cont, 0u);
}

/* --------------------------------------------------------- ps1_call_nested */
static uint32_t nested_a0, nested_a1, nested_sp, nested_ra;

static int nested_dispatch(Cpu *c, uint32_t addr)
{
    (void)addr;
    nested_a0 = c->r[4];
    nested_a1 = c->r[5];
    nested_sp = c->r[29];
    nested_ra = c->r[31];
    c->r[2] = 0x1234u;                 /* valor de retorno */
    c->r[8] = 0xDEADu;                 /* ensucia un registro: debe restaurarse */
    return 1;
}

static void test_call_nested(void)
{
    Cpu *c = fresh();
    uint32_t args[2];
    uint32_t ret;
    puts("ps1_call_nested: marco por debajo y estado restaurado");
    args[0] = 0x11u;
    args[1] = 0x22u;
    c->dispatch = nested_dispatch;
    c->r[29] = 0x801FFF04u;            /* sp sin alinear a 8, a proposito */
    c->r[8] = 0x55u;
    c->r[4] = 0x99u;

    ret = ps1_call_nested(c, 0x80012340u, args, 2);
    CHECK_EQ(ret, 0x1234u);
    CHECK_EQ(nested_a0, 0x11u);
    CHECK_EQ(nested_a1, 0x22u);
    CHECK_EQ(nested_sp, (0x801FFF04u - 0x100u) & ~7u);
    CHECK_EQ(nested_ra, 0u);
    /* al volver, los registros son los de antes */
    CHECK_EQ(c->r[29], 0x801FFF04u);
    CHECK_EQ(c->r[8], 0x55u);
    CHECK_EQ(c->r[4], 0x99u);
    CHECK_EQ(c->r[2], 0u);
}

/* --------------------------------------------------------- interrupciones */
static int irq_calls, irq_saw_in_irq;
static uint32_t irq_sp[8], irq_ra[8], irq_addr[8];

static int irq_dispatch(Cpu *c, uint32_t addr)
{
    if (irq_calls >= 0 && irq_calls < 8) {
        irq_sp[irq_calls] = c->r[29];
        irq_ra[irq_calls] = c->r[31];
        irq_addr[irq_calls] = addr;
    }
    irq_calls++;
    if (c->in_irq)
        irq_saw_in_irq++;
    /* reentrar desde dentro de un manejador no debe entregar nada: in_irq corta.
     * Sin tocar pending_ticks, que lo esta consumiendo el bucle de fuera. */
    ps1_service_interrupts(c);
    c->r[16] = 0xBADu;                 /* ensucia: debe restaurarse */
    return 1;
}

static void test_interrupts(void)
{
    Cpu *c = fresh();
    puts("entrega de interrupciones de timer");
    c->dispatch = irq_dispatch;
    c->r[29] = 0x801FFF00u;
    c->r[16] = 0x77u;
    c->timer_handlers[0] = 0x80020000u;
    c->timer_handlers[1] = 0x80030000u;
    c->timer_handler_n = 2;
    c->pending_ticks = 2;
    irq_calls = 0;
    irq_saw_in_irq = 0;

    ps1_service_interrupts(c);

    /* 2 ticks pendientes x 2 manejadores, y ni uno mas: in_irq corta la reentrada */
    CHECK_EQ(irq_calls, 4);
    CHECK_EQ(irq_saw_in_irq, 4);
    CHECK_EQ(c->pending_ticks, 0u);
    CHECK_EQ(irq_addr[0], 0x80020000u);
    CHECK_EQ(irq_addr[1], 0x80030000u);
    /* pila de interrupcion por debajo del marco actual, alineada a 8, y ra a 0 */
    CHECK_EQ(irq_sp[0], (0x801FFF00u - 0x200u) & ~7u);
    CHECK_EQ(irq_ra[0], 0u);
    /* al salir, el estado del invitado es el de antes */
    CHECK_EQ(c->r[29], 0x801FFF00u);
    CHECK_EQ(c->r[16], 0x77u);
    CHECK_EQ(c->in_irq, 0);

    /* sin manejadores registrados no hace nada */
    c->timer_handler_n = 0;
    c->pending_ticks = 3;
    irq_calls = 0;
    ps1_service_interrupts(c);
    CHECK_EQ(irq_calls, 0);
    CHECK_EQ(c->pending_ticks, 3u);
}

/* ------------------------------------------------- llamadas diferidas del host */
static int deferred_runs;
static uint32_t deferred_arg;
static int deferred_host_available;

static int defer_pending;

static int defer_host_call(Cpu *c, void *self, uint32_t addr)
{
    uint32_t a = 0x77u;
    (void)self;
    /* la propia diferida tiene que llegar al codigo recompilado, asi que el host
     * no la atiende */
    if (addr == 0x80040000u)
        return 0;
    /* Solo una vez: las diferidas se ejecutan con ps1_call_nested, que vuelve a
     * pasar por ps1_call con el host ya restaurado, asi que un host que encolara
     * en cada llamada se recursaria hasta desbordar la pila. Es una trampa real
     * para el host de paso 3, no un problema de cpu.c. */
    if (!defer_pending)
        return 1;
    defer_pending = 0;
    ps1_defer(c, 0x80040000u, &a, 1);
    return 1;
}

static int defer_dispatch(Cpu *c, uint32_t addr)
{
    if (addr == 0x80040000u) {
        deferred_runs++;
        deferred_arg = c->r[4];
        deferred_host_available = (c->host != NULL);
    }
    return 1;
}

static void test_deferred(void)
{
    Cpu *c = fresh();
    Ps1Host host;
    puts("llamadas diferidas: corren al volver el hook, ya con el host disponible");
    host.call = defer_host_call;
    host.idle = NULL;
    host.self = NULL;
    c->dispatch = defer_dispatch;
    c->host = &host;
    deferred_runs = 0;
    deferred_host_available = 0;
    defer_pending = 1;

    ps1_call(c, 0x80012340u);
    CHECK_EQ(deferred_runs, 1);
    CHECK_EQ(deferred_arg, 0x77u);
    CHECK_EQ(deferred_host_available, 1);
    CHECK_EQ(c->deferred_n, 0u);
}

static void test_syscall(void)
{
    Cpu *c = fresh();
    puts("syscall");
    c->r[2] = 0x99u;
    ps1_syscall(c);
    /* Enter/ExitCriticalSection: las interrupciones no se emulan, devuelve 1 */
    CHECK_EQ(c->r[2], 1u);
}

/* ------------------------------------------------------------------ gte.c */

/* Los dos primeros bloques son los tests que ya trae crates/ps1rt/src/gte.rs,
 * portados tal cual: son valores de referencia, no inventados aqui. */
static void test_gte_divide(void)
{
    Cpu *c = fresh();
    int ok;
    puts("GTE: division Newton-Raphson (puntos de referencia de gte.rs)");

    CHECK_EQ(ps1_gte_divide(c, 0x100u, 0x100u, &ok), 0x10000u);
    CHECK_EQ(ok, 1);
    CHECK_EQ(ps1_gte_divide(c, 0x100u, 0x200u, &ok), 0x8000u);
    CHECK_EQ(ok, 1);
    CHECK_EQ(ps1_gte_divide(c, 0x1FFu, 0x100u, &ok), 0x1FF00u);
    CHECK_EQ(ok, 1);
    /* h >= sz3 * 2: no hay resultado (el llamante pone la bandera 17 y usa 0x1FFFF) */
    (void)ps1_gte_divide(c, 0x200u, 0x100u, &ok);
    CHECK_EQ(ok, 0);
    /* sz3 == 0 tampoco tiene resultado */
    (void)ps1_gte_divide(c, 1u, 0u, &ok);
    CHECK_EQ(ok, 0);
}

static void test_gte_rtps(void)
{
    Cpu *c = fresh();
    uint32_t sxy;
    puts("GTE: RTPS proyecta un punto (test de gte.rs)");

    /* rotacion identidad (1.0 = 0x1000), traslacion 0, H = 256,
     * desplazamiento de pantalla (160, 120) en 16.16 */
    ps1_gte_write(c, 32u, 0x1000u);
    ps1_gte_write(c, 34u, 0x1000u);
    ps1_gte_write(c, 36u, 0x1000u);
    ps1_gte_write(c, 32u + 24u, 160u << 16);
    ps1_gte_write(c, 32u + 25u, 120u << 16);
    ps1_gte_write(c, 32u + 26u, 256u);
    /* V0 = (100, 50, 512) */
    ps1_gte_write(c, 0u, 100u | (50u << 16));
    ps1_gte_write(c, 1u, 512u);

    ps1_gte_command(c, 0x00180001u);          /* RTPS, sf = 1 */

    sxy = ps1_gte_read(c, 14u);
    CHECK_EQ((int16_t)(uint16_t)sxy, 160 + 50);
    CHECK_EQ((int16_t)(uint16_t)(sxy >> 16), 120 + 25);
    CHECK_EQ(ps1_gte_read(c, 19u), 512u);
}

static void test_gte_regs(void)
{
    Cpu *c = fresh();
    puts("GTE: rarezas del banco de registros");

    /* los de 16 bits se leen con el signo extendido */
    c->gte_regs[9] = 0xFFFFu;                 /* IR1 = -1 */
    CHECK_EQ(ps1_gte_read(c, 9u), 0xFFFFFFFFu);
    c->gte_regs[36] = 0x8000u;                /* control de 16 bits */
    CHECK_EQ(ps1_gte_read(c, 36u), 0xFFFF8000u);

    /* estos se leen truncados a 16 bits, sin signo */
    c->gte_regs[19] = 0x1234FFFFu;
    CHECK_EQ(ps1_gte_read(c, 19u), 0xFFFFu);

    /* 15 (SXYP) espeja el 14 al leer, y al escribir empuja la cola de vertices */
    c->gte_regs[12] = 0xAAAAu;
    c->gte_regs[13] = 0xBBBBu;
    c->gte_regs[14] = 0xCCCCu;
    CHECK_EQ(ps1_gte_read(c, 15u), 0xCCCCu);
    ps1_gte_write(c, 15u, 0xDDDDu);
    CHECK_EQ(c->gte_regs[12], 0xBBBBu);
    CHECK_EQ(c->gte_regs[13], 0xCCCCu);
    CHECK_EQ(c->gte_regs[14], 0xDDDDu);

    /* 28 (IRGB): al escribir expande a IR1..3 por 5 bits; al leer, los comprime */
    ps1_gte_write(c, 28u, 0x1Fu | (0x10u << 5) | (0x01u << 10));
    CHECK_EQ(c->gte_regs[9], 0x1Fu << 7);
    CHECK_EQ(c->gte_regs[10], 0x10u << 7);
    CHECK_EQ(c->gte_regs[11], 0x01u << 7);
    CHECK_EQ(ps1_gte_read(c, 28u), 0x1Fu | (0x10u << 5) | (0x01u << 10));

    /* 29 y 31 son de solo lectura: escribir no hace nada */
    c->gte_regs[29] = 0x1111u;
    ps1_gte_write(c, 29u, 0x2222u);
    CHECK_EQ(c->gte_regs[29], 0x1111u);

    /* 31 (LZCR) cuenta los ceros a la izquierda de 30, o los unos si es negativo */
    ps1_gte_write(c, 30u, 0x00000FFFu);
    CHECK_EQ(ps1_gte_read(c, 31u), 20u);
    ps1_gte_write(c, 30u, 0xFFFFF000u);
    CHECK_EQ(ps1_gte_read(c, 31u), 20u);
    ps1_gte_write(c, 30u, 0u);
    CHECK_EQ(ps1_gte_read(c, 31u), 32u);
    ps1_gte_write(c, 30u, 0xFFFFFFFFu);
    CHECK_EQ(ps1_gte_read(c, 31u), 32u);

    /* FLAG: al escribirlo, el bit 31 es el OR de los bits de error */
    ps1_gte_write(c, 63u, 0x00002000u);       /* un bit de error */
    CHECK_EQ(c->gte_regs[63] & 0x80000000u, 0x80000000u);
    ps1_gte_write(c, 63u, 0x00001000u);       /* bit sin error (IR0 saturado) */
    CHECK_EQ(c->gte_regs[63] & 0x80000000u, 0u);
}

static void test_gte_flags(void)
{
    Cpu *c = fresh();
    puts("GTE: saturaciones y banderas");

    /* NCLIP con un triangulo degenerado da area 0 y ninguna bandera */
    c->gte_regs[12] = 0u;
    c->gte_regs[13] = 0u;
    c->gte_regs[14] = 0u;
    ps1_gte_command(c, 0x00000006u);
    CHECK_EQ(c->gte_regs[24], 0u);
    CHECK_EQ(c->gte_regs[63], 0u);

    /* SQR eleva IR1..3 al cuadrado; con sf = 0 no desplaza */
    c->gte_regs[9] = 0x10u;
    c->gte_regs[10] = 0x20u;
    c->gte_regs[11] = (uint32_t)(int32_t)-0x30;    /* el cuadrado sale positivo */
    ps1_gte_command(c, 0x00000028u);
    CHECK_EQ(c->gte_regs[25], 0x100u);
    CHECK_EQ(c->gte_regs[26], 0x400u);
    CHECK_EQ(c->gte_regs[27], 0x900u);
    CHECK_EQ(c->gte_regs[9], 0x100u);              /* y pasa a IR1..3 */

    /* IR saturado por arriba enciende su bandera y, con ella, el bit 31 */
    c->gte_regs[9] = 0x4000u;                      /* IR1 = 0x4000 */
    c->gte_regs[10] = 0u;
    c->gte_regs[11] = 0u;
    ps1_gte_command(c, 0x00000028u);               /* 0x4000^2 = 0x10000000 */
    CHECK(c->gte_regs[63] & (1u << 24));           /* IR1 saturado */
    CHECK(c->gte_regs[63] & 0x80000000u);
    CHECK_EQ(c->gte_regs[9], 0x7FFFu);             /* clavado al tope */

    /* AVSZ3: media de SZ1..SZ3 con el factor ZSF3 */
    c->gte_regs[17] = 100u;
    c->gte_regs[18] = 200u;
    c->gte_regs[19] = 300u;
    ps1_gte_write(c, 32u + 29u, 0x1000u);          /* ZSF3 = 1.0 en 1.12 */
    ps1_gte_command(c, 0x0000002Du);
    CHECK_EQ(c->gte_regs[24], (100u + 200u + 300u) * 0x1000u);
    CHECK_EQ(c->gte_regs[7], 600u);                /* OTZ */
}

static void test_gte_mvmva_bug(void)
{
    Cpu *c = fresh();
    puts("GTE: el error de MVMVA con cv == 2");

    /* matriz identidad en RT, vector V0 = (1, 2, 3), traslacion de fondo (BK) */
    ps1_gte_write(c, 32u, 0x1000u);
    ps1_gte_write(c, 34u, 0x1000u);
    ps1_gte_write(c, 36u, 0x1000u);
    ps1_gte_write(c, 0u, 1u | (2u << 16));
    ps1_gte_write(c, 1u, 3u);
    c->gte_regs[53] = 0x11111111u;                 /* BK, el que usa cv == 2 */
    c->gte_regs[54] = 0x22222222u;
    c->gte_regs[55] = 0x33333333u;

    /* cv = 0 (TR): el camino normal suma traslacion y las tres columnas */
    c->gte_regs[37] = 0;
    c->gte_regs[38] = 0;
    c->gte_regs[39] = 0;
    ps1_gte_command(c, 0x00080012u);               /* MVMVA sf=1, mx=0, v=0, cv=0 */
    CHECK_EQ((int32_t)c->gte_regs[25], 1);         /* identidad: MAC = V */
    CHECK_EQ((int32_t)c->gte_regs[26], 2);
    CHECK_EQ((int32_t)c->gte_regs[27], 3);

    /* cv = 2 (BK): el hardware solo lleva al MAC las dos ultimas columnas, asi que
     * con la identidad MAC1 sale 0 (su unica columna no nula es la primera) */
    ps1_gte_command(c, 0x00084012u);               /* ...cv = 2 */
    CHECK_EQ((int32_t)c->gte_regs[25], 0);
    CHECK_EQ((int32_t)c->gte_regs[26], 2);
    CHECK_EQ((int32_t)c->gte_regs[27], 3);
}

static void test_gte_cop2(void)
{
    Cpu *c = fresh();
    puts("GTE: instrucciones COP2 (mfc2 / mtc2 / cfc2 / ctc2 / lwc2 / swc2)");

    /* mtc2 r5 -> gte 9, y mfc2 gte 9 -> r6 (con el signo extendido) */
    c->r[5] = 0xFFFFu;
    ps1_gte(c, 0x48850000u | (9u << 11));          /* mtc2: op 18, rs 4, rt 5, rd 9 */
    CHECK_EQ(c->gte_regs[9], 0xFFFFu);
    ps1_gte(c, 0x48060000u | (9u << 11));          /* mfc2: op 18, rs 0, rt 6, rd 9 */
    CHECK_EQ(c->r[6], 0xFFFFFFFFu);

    /* escribir en r0 se descarta */
    ps1_gte(c, 0x48000000u | (9u << 11));          /* mfc2 a r0 */
    CHECK_EQ(c->r[0], 0u);

    /* ctc2 / cfc2 sobre un registro de control */
    c->r[5] = 0x1234u;
    ps1_gte(c, 0x48C50000u | (26u << 11));         /* ctc2: rs 6, rt 5, rd 26 -> H */
    CHECK_EQ(c->gte_regs[32u + 26u], 0x1234u);
    ps1_gte(c, 0x48460000u | (26u << 11));         /* cfc2: rs 2, rt 6, rd 26 */
    CHECK_EQ(c->r[6], 0x1234u);

    /* lwc2 / swc2 mueven una palabra entre la RAM y un registro del GTE */
    c->r[4] = 0x80001000u;
    ps1_w32(c, 0x80001010u, 0x0000ABCDu);
    ps1_gte(c, 0xC8000010u | (4u << 21) | (16u << 16));  /* lwc2 gte16, 0x10(r4) */
    CHECK_EQ(c->gte_regs[16], 0x0000ABCDu);
    c->gte_regs[17] = 0x00005678u;
    ps1_gte(c, 0xE8000014u | (4u << 21) | (17u << 16));  /* swc2 gte17, 0x14(r4) */
    CHECK_EQ(ps1_r32(c, 0x80001014u), 0x5678u);

    /* un comando llega por el bit 25 y se cuenta */
    CHECK_EQ(c->gte_commands, 0u);
    ps1_gte(c, 0x4A000006u);                       /* op 18, bit 25, NCLIP */
    CHECK_EQ(c->gte_commands, 1u);
}

/* --------------------------------------------------------------- bios.c (tabla A0) */

static void put_str(Cpu *c, uint32_t a, const char *s)
{
    uint32_t i = 0;
    for (;;) {
        ps1_w8(c, a + i, (uint32_t)(uint8_t)s[i]);
        if (s[i] == '\0')
            break;
        i++;
    }
}

static int guest_streq(Cpu *c, uint32_t a, const char *s)
{
    uint32_t i = 0;
    for (;;) {
        uint32_t ch = ps1_r8(c, a + i);
        if (ch != (uint32_t)(uint8_t)s[i])
            return 0;
        if (s[i] == '\0')
            return 1;
        i++;
    }
}

/* Llama a la tabla A0 como lo hace el juego: numero de funcion en t1 (r9). */
static uint32_t bios_a0(Cpu *c, uint32_t f, uint32_t a0, uint32_t a1, uint32_t a2)
{
    c->r[9] = f;
    c->r[4] = a0;
    c->r[5] = a1;
    c->r[6] = a2;
    ps1_bios_call(c, 0xA0u);
    return c->r[2];
}

static void test_bios_strings(void)
{
    Cpu *c = fresh();
    const uint32_t S = 0x80002000u, D = 0x80002100u;
    puts("BIOS A0: cadenas");

    put_str(c, S, "hola mundo");

    CHECK_EQ(bios_a0(c, 0x1Bu, S, 0, 0), 10u);              /* strlen */

    CHECK_EQ(bios_a0(c, 0x19u, D, S, 0), D);                /* strcpy */
    CHECK(guest_streq(c, D, "hola mundo"));

    put_str(c, D, "hola");
    CHECK_EQ(bios_a0(c, 0x15u, D, S, 0), D);                /* strcat */
    CHECK(guest_streq(c, D, "holahola mundo"));

    /* strcmp y strncmp devuelven la diferencia del primer byte distinto */
    put_str(c, D, "hola");
    /* "hola" vs "hola mundo": difieren en el byte 4, ' ' contra espacio */
    CHECK_EQ((int32_t)bios_a0(c, 0x17u, D, S, 0), -(int32_t)' ');
    CHECK_EQ((int32_t)bios_a0(c, 0x17u, S, D, 0), (int32_t)' ');
    CHECK_EQ(bios_a0(c, 0x18u, S, D, 4u), 0u);              /* strncmp, 4 bytes iguales */
    CHECK_EQ(bios_a0(c, 0x18u, S, D, 0u), 0u);              /* n = 0: siempre iguales */

    /* strncpy escribe exactamente n bytes, rellenando con 0 */
    ps1_w8(c, D + 6u, 0x5Au);
    CHECK_EQ(bios_a0(c, 0x1Au, D, S, 6u), D);
    CHECK_EQ(ps1_r8(c, D + 5u), (uint32_t)'m');
    CHECK_EQ(ps1_r8(c, D + 6u), 0x5Au);                     /* no toca el septimo */
    put_str(c, S, "ab");
    CHECK_EQ(bios_a0(c, 0x1Au, D, S, 5u), D);
    CHECK_EQ(ps1_r8(c, D + 2u), 0u);                        /* rellenado */
    CHECK_EQ(ps1_r8(c, D + 4u), 0u);

    /* index / strchr y rindex / strrchr */
    put_str(c, S, "abcabc");
    CHECK_EQ(bios_a0(c, 0x1Cu, S, (uint32_t)'c', 0), S + 2u);
    CHECK_EQ(bios_a0(c, 0x1Du, S, (uint32_t)'c', 0), S + 5u);
    CHECK_EQ(bios_a0(c, 0x1Cu, S, (uint32_t)'z', 0), 0u);
    CHECK_EQ(bios_a0(c, 0x1Du, S, (uint32_t)'z', 0), 0u);

    /* strstr */
    CHECK_EQ(bios_a0(c, 0x24u, S, S + 3u, 0), S);           /* "abc" dentro de "abcabc" */
    put_str(c, D, "bca");
    CHECK_EQ(bios_a0(c, 0x24u, S, D, 0), S + 1u);
    put_str(c, D, "zz");
    CHECK_EQ(bios_a0(c, 0x24u, S, D, 0), 0u);
    put_str(c, D, "");
    CHECK_EQ(bios_a0(c, 0x24u, S, D, 0), S);                /* aguja vacia */

    CHECK_EQ(bios_a0(c, 0x25u, (uint32_t)'q', 0, 0), (uint32_t)'Q');   /* toupper */
    CHECK_EQ(bios_a0(c, 0x26u, (uint32_t)'Q', 0, 0), (uint32_t)'q');   /* tolower */
    CHECK_EQ(bios_a0(c, 0x25u, (uint32_t)'7', 0, 0), (uint32_t)'7');   /* sin cambio */
}

static void test_bios_mem(void)
{
    Cpu *c = fresh();
    const uint32_t A = 0x80003000u, B = 0x80003100u;
    uint32_t i;
    puts("BIOS A0: memoria");

    for (i = 0; i < 16u; i++)
        ps1_w8(c, A + i, i);

    CHECK_EQ(bios_a0(c, 0x2Au, B, A, 16u), B);              /* memcpy */
    CHECK_EQ(ps1_r8(c, B + 15u), 15u);

    /* memmove con solape: la referencia lee todo el origen antes de escribir, asi que
     * memcpy y memmove se comportan igual */
    CHECK_EQ(bios_a0(c, 0x2Cu, A + 4u, A, 8u), A + 4u);
    CHECK_EQ(ps1_r8(c, A + 4u), 0u);
    CHECK_EQ(ps1_r8(c, A + 11u), 7u);

    CHECK_EQ(bios_a0(c, 0x2Bu, A, 0xABu, 4u), A);           /* memset */
    CHECK_EQ(ps1_r8(c, A), 0xABu);
    CHECK_EQ(ps1_r8(c, A + 3u), 0xABu);

    /* memcmp compara los n bytes, sin parar en el 0 */
    ps1_w8(c, B, 0xABu);
    ps1_w8(c, B + 1u, 0xABu);
    ps1_w8(c, B + 2u, 0x00u);
    ps1_w8(c, B + 3u, 0xABu);
    CHECK_EQ((int32_t)bios_a0(c, 0x2Du, A, B, 2u), 0);
    CHECK_EQ((int32_t)bios_a0(c, 0x2Du, A, B, 4u), (int32_t)0xAB - 0x00);

    CHECK_EQ(bios_a0(c, 0x2Eu, A, 0xABu, 4u), A);           /* memchr */
    CHECK_EQ(bios_a0(c, 0x2Eu, A, 0x11u, 4u), 0u);
}

static void test_bios_num(void)
{
    Cpu *c = fresh();
    const uint32_t S = 0x80004000u, E = 0x80004100u;
    puts("BIOS A0: numeros y rand");

    put_str(c, S, "  -42abc");
    CHECK_EQ((int32_t)bios_a0(c, 0x10u, S, 0, 0), -42);     /* atoi */

    put_str(c, S, "0x1f");
    CHECK_EQ(bios_a0(c, 0x0Cu, S, E, 0u), 0x1Fu);           /* strtol base 0 -> hex */
    CHECK_EQ(ps1_r32(c, E), S + 4u);                        /* endp */
    CHECK_EQ(bios_a0(c, 0x0Cu, S, 0u, 10u), 0u);            /* base 10: para en la x */

    put_str(c, S, "017");
    CHECK_EQ(bios_a0(c, 0x0Cu, S, 0u, 0u), 15u);            /* prefijo 0 -> octal */

    CHECK_EQ(bios_a0(c, 0x0Eu, (uint32_t)-5, 0, 0), 5u);    /* abs */
    CHECK_EQ(bios_a0(c, 0x0Eu, 0x80000000u, 0, 0), 0x80000000u);

    /* rand: generador congruencial del BIOS */
    bios_a0(c, 0x30u, 1u, 0, 0);                            /* srand(1) */
    CHECK_EQ(c->rand_seed, 1u);
    {
        uint32_t r1 = bios_a0(c, 0x2Fu, 0, 0, 0);
        uint32_t seed = 1u * 0x41C64E6Du + 0x3039u;
        CHECK_EQ(r1, (seed >> 16) & 0x7FFFu);
        CHECK(r1 <= 0x7FFFu);
    }

    /* una funcion que no existe avisa y devuelve 0, sin matar la ejecucion */
    {
        uint64_t before = c->bios_unhandled;
        CHECK_EQ(bios_a0(c, 0x01u, 0, 0, 0), 0u);
        CHECK_EQ(c->bios_unhandled, before + 1u);
    }
}

int main(void)
{
    /* sin buffer: si una prueba se lleva el proceso por delante, queremos saber
     * exactamente en cual estabamos */
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("== ps1rt: pruebas unitarias ==\n");
    test_mem_map();
    test_mem_unaligned();
    test_alu_divmul();
    test_alu_unaligned_ops();
    test_load_exe();
    test_call_order();
    test_tick();
    test_stop();
    test_cont();
    test_call_nested();
    test_interrupts();
    test_deferred();
    test_syscall();
    test_gte_divide();
    test_gte_rtps();
    test_gte_regs();
    test_gte_flags();
    test_gte_mvmva_bug();
    test_gte_cop2();
    test_bios_strings();
    test_bios_mem();
    test_bios_num();
    printf("\n%d/%d comprobaciones correctas\n", total - fallos, total);
    if (fallos) {
        printf("%d FALLOS\n", fallos);
        return 1;
    }
    puts("OK");
    return 0;
}
