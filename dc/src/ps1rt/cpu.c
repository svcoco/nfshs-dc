/* Control de flujo del invitado: despacho de llamadas, watchdog, continuaciones e
 * interrupciones. Portado de Cpu::{call, call_nested, service_interrupts,
 * service_bus_interrupts, tick, run_cont, jump, syscall} de crates/ps1rt/src/lib.rs.
 *
 * El orden de cada rutina es parte del contrato: el test diferencial de M1a compara
 * el estado frame a frame contra la referencia, asi que mover un paso de sitio
 * (por ejemplo tiquear despues de mirar la BIOS) ya divergiria.
 */
#include "ps1rt.h"

#include <stdio.h>

/* ---------------------------------------------------------------- watchdog y tiempo */

/* Se llama en cada rama hacia atras, y tambien desde ps1_call. El codigo generado
 * NO tiquea en las llamadas directas f_X(c), igual que la referencia. */
void ps1_tick(Cpu *c)
{
    c->ticks++;
    c->since_host++;

    /* El codigo lleva spin_limit ramas hacia atras sin pasar por el host: se da por
     * hecho que esta girando sobre memoria que solo cambiaria una interrupcion, y se
     * deja correr el tiempo del host. */
    if (c->spin_limit != 0 && c->since_host >= c->spin_limit && !c->in_irq) {
        c->since_host = 0;
        if (c->host != NULL) {
            Ps1Host *h = c->host;
            c->host = NULL;              /* mientras corre, no se puede reentrar */
            if (h->idle != NULL)
                h->idle(c, h->self);
            c->host = h;
            ps1_service_interrupts(c);
        }
    }

    /* El host ha pedido parar el programa: salir de el. En Rust esto es
     * resume_unwind(Stop); aqui, el longjmp que el host dejo armado. */
    if (c->tick_limit == 0) {
        if (c->stop_armed) {
            c->stop_armed = 0;
            longjmp(c->stop_jmp, 1);
        }
        ps1_panic(c, "parada pedida sin punto de salto armado");
    }
    if (PS1_UNLIKELY(c->ticks > c->tick_limit)) {
        uint32_t i;
        fprintf(stderr, "\nwatchdog: %llu ramas hacia atras; ultimas llamadas al host:",
                (unsigned long long)c->ticks);
        for (i = 0; i < 16u; i++) {
            uint32_t k = (c->recent_at + i) % 16u;
            if (c->recent[k][0] != 0)
                fprintf(stderr, " %#x<-%#x", (unsigned)c->recent[k][0],
                        (unsigned)c->recent[k][1]);
        }
        fprintf(stderr, "\n");
        /* Si el host dejo armado un punto de salida, se sale por ahi para que pueda
         * imprimir su informe: el watchdog es una red de seguridad del host, no
         * semantica del juego, y un informe vale mas que un abort. */
        if (c->stop_armed) {
            c->stop_armed = 0;
            c->tick_limit = UINT64_MAX;
            longjmp(c->stop_jmp, 2);
        }
        ps1_panic(c, "watchdog");
    }
}

/* --------------------------------------------------------------------- despacho */

void ps1_defer(Cpu *c, uint32_t f, const uint32_t *args, uint32_t n)
{
    uint32_t i;
    if (c->deferred_n >= PS1_DEFERRED_MAX)
        ps1_panic(c, "cola de llamadas diferidas llena");
    if (n > PS1_DEFERRED_ARGS)
        ps1_panic(c, "demasiados argumentos en una llamada diferida");
    c->deferred[c->deferred_n].f = f;
    c->deferred[c->deferred_n].n = n;
    for (i = 0; i < n; i++)
        c->deferred[c->deferred_n].args[i] = args[i];
    c->deferred_n++;
}

/* Saca la primera de la cola (FIFO, como el deferred.remove(0) de Rust) y la corre.
 * Se vuelve a comprobar la cola en cada vuelta porque un manejador puede encolar mas. */
static void drain_deferred(Cpu *c)
{
    while (c->deferred_n > 0) {
        uint32_t f = c->deferred[0].f;
        uint32_t n = c->deferred[0].n;
        uint32_t args[PS1_DEFERRED_ARGS];
        uint32_t i;
        for (i = 0; i < n; i++)
            args[i] = c->deferred[0].args[i];
        for (i = 1; i < c->deferred_n; i++)
            c->deferred[i - 1] = c->deferred[i];
        c->deferred_n--;
        ps1_call_nested(c, f, args, n);
    }
}

/* Llamada por direccion: vectores de BIOS, reemplazos del host y, por ultimo, codigo
 * recompilado. El orden es el de Cpu::call y no se puede tocar. */
void ps1_call(Cpu *c, uint32_t addr)
{
    uint32_t phys = addr & 0x1FFFFFFFu;

    c->calls++;
    ps1_tick(c);
    if (c->trace)
        fprintf(stderr, "call %#x a0=%#x a1=%#x a2=%#x ra=%#x\n", (unsigned)addr,
                (unsigned)c->r[4], (unsigned)c->r[5], (unsigned)c->r[6],
                (unsigned)c->r[31]);

    /* 1. vectores de la BIOS: el numero de funcion va en t1 (r9) */
    if (phys == 0xA0u || phys == 0xB0u || phys == 0xC0u) {
        ps1_bios_call(c, phys);
        return;
    }

    /* 2. el host. Se retira mientras corre, para que sus llamadas anidadas no puedan
     *    reentrar en el: ahi es donde entra inner_hook. */
    if (c->host != NULL) {
        Ps1Host *h = c->host;
        int handled;
        if (c->since_host > c->max_since_host)
            c->max_since_host = c->since_host;
        c->since_host = 0;
        c->recent[c->recent_at % 16u][0] = addr;
        c->recent[c->recent_at % 16u][1] = c->r[31];
        c->recent_at++;

        c->host = NULL;
        handled = (h->call != NULL) ? h->call(c, h->self, addr) : 0;
        c->host = h;

        drain_deferred(c);
        ps1_service_interrupts(c);
        if (handled)
            return;
    } else if (c->inner_hook != NULL) {
        if (c->inner_hook(c, addr))
            return;
    }

    /* 3. codigo recompilado y, si no esta, el interprete de respaldo */
    if (!(c->dispatch != NULL && c->dispatch(c, addr))) {
        c->interpreted++;
        ps1_interpret(c, addr);
    }

    if (c->cont != 0)
        ps1_run_cont(c);
}

/* Llama a f desde codigo del host en medio del juego (por ejemplo dentro de una
 * funcion reemplazada): los registros se preservan y la llamada recibe un marco de
 * pila por debajo del actual. */
uint32_t ps1_call_nested(Cpu *c, uint32_t f, const uint32_t *args, uint32_t n)
{
    uint32_t saved_r[32], saved_hi, saved_lo, ret, i;
    for (i = 0; i < 32u; i++)
        saved_r[i] = c->r[i];
    saved_hi = c->hi;
    saved_lo = c->lo;

    for (i = 0; i < n; i++)
        c->r[4 + i] = args[i];
    c->r[29] = (c->r[29] - 0x100u) & ~7u;
    c->r[31] = 0;
    ps1_call(c, f);
    ret = c->r[2];

    for (i = 0; i < 32u; i++)
        c->r[i] = saved_r[i];
    c->hi = saved_hi;
    c->lo = saved_lo;
    return ret;
}

/* ------------------------------------------------------------- continuaciones */

/* El control se fue mas alla del final de una funcion recompilada (cola compartida o
 * segunda entrada). */
void ps1_jump(Cpu *c, uint32_t pc)
{
    if (c->dispatch != NULL && c->dispatch(c, pc))
        return;
    if (!(c->dispatch_at != NULL && c->dispatch_at(c, pc))) {
        c->interpreted++;
        ps1_interpret(c, pc);
    }
}

/* Trampolin: sigue reanudando mientras alguna funcion deje una continuacion, sin
 * hacer crecer la pila del anfitrion. */
void ps1_run_cont(Cpu *c)
{
    while (c->cont != 0) {
        uint32_t pc = c->cont;
        c->cont = 0;
        c->cont_recent[c->cont_at % 32u] = pc;
        c->cont_at++;
        c->cont_runs++;
        ps1_tick(c);
        ps1_jump(c, pc);
    }
}

void ps1_syscall(Cpu *c)
{
    /* SYSCALL 1/2 = Enter/ExitCriticalSection: las interrupciones no se emulan. */
    c->r[2] = 1;
}

/* ------------------------------------------------------------- interrupciones */

/* Entrega las interrupciones de hardware que levanto el IoBus (fin de DMA, IRQ del
 * SPU) a las devoluciones que registro el juego, salvo que el juego las tenga
 * deshabilitadas (IEc o IM2 del SR de COP0, como en sus secciones criticas). */
static void service_bus_interrupts(Cpu *c)
{
    uint32_t handlers[18];
    uint32_t n = 0, irqs, i;
    uint32_t saved_r[32], saved_hi, saved_lo, saved_depth;

    if ((c->cop0[12] & 0x401u) != 0x401u)
        return;
    if (c->bus == NULL || c->bus->take_irqs == NULL)
        return;
    irqs = c->bus->take_irqs(c, c->bus->self);
    if (irqs == 0)
        return;

    /* La lista se toma como foto antes de ejecutar nada, igual que la referencia. */
    for (i = 0; i < 11u; i++)
        if ((irqs >> i) & 1u) {
            if (c->irq_callbacks[i] != 0)
                handlers[n++] = c->irq_callbacks[i];
        }
    for (i = 0; i < 7u; i++)
        if ((irqs >> (16u + i)) & 1u) {
            if (c->dma_callbacks[i] != 0)
                handlers[n++] = c->dma_callbacks[i];
        }
    if (n == 0)
        return;

    c->in_irq = 1;
    for (i = 0; i < 32u; i++)
        saved_r[i] = c->r[i];
    saved_hi = c->hi;
    saved_lo = c->lo;
    saved_depth = c->stack_depth;

    for (i = 0; i < n; i++) {
        c->r[29] = (saved_r[29] - 0x200u) & ~7u;
        c->r[31] = 0;
        ps1_call(c, handlers[i]);
    }

    for (i = 0; i < 32u; i++)
        c->r[i] = saved_r[i];
    c->hi = saved_hi;
    c->lo = saved_lo;
    c->stack_depth = saved_depth;
    c->in_irq = 0;
}

/* Entrega las interrupciones de timer pendientes: corre cada manejador registrado
 * con el estado de CPU salvado y restaurado alrededor, como hace el despachador de
 * interrupciones de la PS1. */
void ps1_service_interrupts(Cpu *c)
{
    uint32_t saved_r[32], saved_hi, saved_lo, saved_depth, i;

    if (!c->in_irq && c->bus != NULL)
        service_bus_interrupts(c);
    if (c->in_irq || c->pending_ticks == 0 || c->timer_handler_n == 0)
        return;

    c->in_irq = 1;
    for (i = 0; i < 32u; i++)
        saved_r[i] = c->r[i];
    saved_hi = c->hi;
    saved_lo = c->lo;
    saved_depth = c->stack_depth;

    while (c->pending_ticks > 0) {
        c->pending_ticks--;
        for (i = 0; i < c->timer_handler_n; i++) {
            /* pila de interrupcion por debajo del marco actual */
            c->r[29] = (saved_r[29] - 0x200u) & ~7u;
            c->r[31] = 0;
            ps1_call(c, c->timer_handlers[i]);
        }
    }

    for (i = 0; i < 32u; i++)
        c->r[i] = saved_r[i];
    c->hi = saved_hi;
    c->lo = saved_lo;
    c->stack_depth = saved_depth;
    c->in_irq = 0;
}
