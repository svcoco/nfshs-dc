/* ps1rt: modelo minimo de la maquina PS1 para el codigo recompilado estaticamente.
 *
 * Portado de crates/ps1rt (Rust) de este mismo fork. La semantica tiene que coincidir
 * exactamente con la de la referencia: el test diferencial de M1a compara la RAM del
 * invitado frame a frame entre las dos implementaciones.
 *
 * No incluye cabeceras de KOS: M1a corre en x86 y M1b solo cambia dc/src/platform/.
 */
#ifndef PS1RT_H
#define PS1RT_H

#include <stdint.h>
#include <limits.h>
#include <setjmp.h>
#include <stddef.h>

/* ---- supuestos del codigo generado (validados con gcc 16.2 x86_64) ---- */
_Static_assert(sizeof(int) == 4 && UINT_MAX == 0xFFFFFFFFu, "int no es de 32 bits");
_Static_assert((-1 >> 1) == -1, ">> con signo no es aritmetico");
_Static_assert((int32_t)0x80000000u == INT32_MIN, "conversion u32->i32 inesperada");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "no es little-endian");

#if defined(__GNUC__)
#define PS1_LIKELY(x)   __builtin_expect(!!(x), 1)
#define PS1_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define PS1_NOINLINE    __attribute__((noinline))
#define PS1_NORETURN    __attribute__((noreturn))
typedef uint32_t ps1_u32a __attribute__((may_alias));
typedef uint16_t ps1_u16a __attribute__((may_alias));
#else
#define PS1_LIKELY(x)   (x)
#define PS1_UNLIKELY(x) (x)
#define PS1_NOINLINE
#define PS1_NORETURN
typedef uint32_t ps1_u32a;
typedef uint16_t ps1_u16a;
#endif

/* ---- mapa de memoria -----------------------------------------------------------
 * Replica Mem::slot() de lib.rs, cuyo orden es:
 *
 *   p = a & 0x1FFFFFFF                       normaliza KUSEG / KSEG0 / KSEG1
 *   scratchpad   0x1F800000 .. 0x1F8003FF  -> scratch[p - 0x1F800000]
 *   p >= 0x00800000                        -> IO (lee 0, las escrituras se tiran)
 *   resto                                  -> ram[p & 0x1FFFFF]
 *
 * Dos detalles que NO son los que describe CLAUDE.md seccion 9:
 *  - el umbral de IO es 0x00800000, no 0x200000: el rango 0x200000..0x7FFFFF se
 *    refleja dentro de la RAM a traves de la mascara.
 *  - los accesos desalineados son lecturas desalineadas de verdad, sin enmascarar
 *    los bits bajos. En SH-4 eso serian excepciones de direccion, de ahi el
 *    respaldo byte a byte y el contador.
 */
#define PS1_RAM_SIZE   0x200000u
#define PS1_RAM_MASK   0x1FFFFFu
#define PS1_RAM_PAD    4u            /* holgura para un acceso de 4 bytes en el borde */
#define PS1_IO_BASE    0x00800000u
#define PS1_SCRATCH_BASE 0x1F800000u
#define PS1_SCRATCH_SIZE 0x400u
#define PS1_STACK_MAX  1024u         /* profundidad de la pila de diagnostico */
#define PS1_DEFERRED_MAX  32u        /* llamadas que un hook del host puede encolar */
#define PS1_DEFERRED_ARGS 8u

/* Contadores de diagnostico en la ruta rapida. Valen para M1a (hay que medir si el
 * juego hace accesos desalineados); se apagan con -DPS1_COUNT_ACCESS=0. */
#ifndef PS1_COUNT_ACCESS
#define PS1_COUNT_ACCESS 1
#endif

typedef struct Cpu Cpu;

/* Registros de hardware que modela el host: el driver de sonido del juego programa
 * el SPU y su canal de DMA directamente. Equivale al trait IoBus de Rust. */
typedef struct Ps1IoBus {
    uint32_t (*read)(Cpu *c, void *self, uint32_t a, uint32_t n);
    void     (*write)(Cpu *c, void *self, uint32_t a, uint32_t v, uint32_t n);
    uint32_t (*take_irqs)(Cpu *c, void *self);
    void     *self;
} Ps1IoBus;

/* Reemplazos de las funciones que tocan hardware (archivos, GPU, timers...). */
typedef struct Ps1Host {
    /* Atiende una llamada a addr; devuelve 0 si no la gestiona. */
    int  (*call)(Cpu *c, void *self, uint32_t addr);
    /* El codigo lleva mucho girando sin llamar al host (ver spin_limit). */
    void (*idle)(Cpu *c, void *self);
    void *self;
} Ps1Host;

struct Cpu {
    /* ---- estado del invitado: esto es lo que compara el test diferencial ---- */
    uint32_t r[32];
    uint32_t hi, lo;
    uint32_t cop0[32];
    /* Datos del GTE (0..31) y control (32..63). Los comandos SI se ejecutan: el
     * comentario de gte_regs en lib.rs dice lo contrario, pero Cpu::gte llama a
     * gte_command (ya anotado en CLAUDE.md seccion 6). */
    uint32_t gte_regs[64];

    /* ---- memoria ---- */
    uint8_t *ram;                        /* PS1_RAM_SIZE + PS1_RAM_PAD bytes */
    uint8_t  scratch[PS1_SCRATCH_SIZE];
    uint16_t spu_volume[24];             /* sin IoBus: volumen ADSR que leen las voces */
    Ps1IoBus *bus;

    /* ---- control de flujo ---- */
    uint32_t cont;                            /* continuacion pendiente */
    int (*dispatch)(Cpu *c, uint32_t addr);   /* los define dc/gen/dispatch.c */
    int (*dispatch_at)(Cpu *c, uint32_t pc);
    int (*inner_hook)(Cpu *c, uint32_t addr); /* consultado mientras corre el host */
    Ps1Host *host;

    /* Llamadas que un hook del host pide: se ejecutan en cuanto el hook vuelve, ya
     * con el host disponible otra vez (un hook no alcanza al host desde una llamada
     * anidada, porque ps1_call lo retira mientras corre). */
    struct {
        uint32_t f;
        uint32_t n;
        uint32_t args[PS1_DEFERRED_ARGS];
    } deferred[PS1_DEFERRED_MAX];
    uint32_t deferred_n;

    /* ---- interrupciones ---- */
    uint32_t timer_handlers[16];         /* root counter de 128 Hz (via OpenEvent) */
    uint32_t timer_handler_n;
    uint32_t pending_ticks;
    uint32_t irq_callbacks[11];          /* InterruptCallback, por linea de I_STAT */
    uint32_t dma_callbacks[7];           /* DMACallback, por canal */
    int      in_irq;

    /* ---- watchdog y diagnostico ---- */
    uint64_t ticks;                      /* ramas hacia atras */
    uint64_t tick_limit;
    uint64_t since_host, max_since_host, spin_limit;
    uint64_t calls, interpreted, gte_commands;
    uint64_t bios_unhandled;             /* llamadas a BIOS sin implementar */
    uint64_t io_accesses;
    uint32_t last_io;
    uint64_t misaligned;                 /* accesos desalineados a la RAM */
    uint64_t ram_edge;                   /* accesos que cruzan el tope de la RAM */
    uint32_t stack[PS1_STACK_MAX];       /* funciones recompiladas en curso */
    uint32_t stack_depth;
    uint32_t recent[16][2];              /* (direccion, retorno) de llamadas al host */
    uint32_t cont_recent[32];            /* ultimas continuaciones (diagnostico) */
    uint32_t cont_at;
    uint64_t cont_runs;
    uint32_t recent_at;
    int      trace;

    /* ---- estado del BIOS HLE ---- */
    uint32_t rand_seed;

    /* Salida del programa: tick_limit == 0 salta aqui. En Rust es resume_unwind(Stop). */
    jmp_buf  stop_jmp;
    int      stop_armed;
};

/* ---- ciclo de vida ---- */
void ps1_init(Cpu *c, uint8_t *ram_buf);   /* ram_buf: PS1_RAM_SIZE + PS1_RAM_PAD */
void ps1_load_exe(Cpu *c, const uint8_t *exe, size_t len);

/* ---- rutas lentas de memoria (mem.c): scratchpad, IO y bus ---- */
uint32_t ps1_r8_slow(Cpu *c, uint32_t a);
uint32_t ps1_r16_slow(Cpu *c, uint32_t a);
uint32_t ps1_r32_slow(Cpu *c, uint32_t a);
void     ps1_w8_slow(Cpu *c, uint32_t a, uint32_t v);
void     ps1_w16_slow(Cpu *c, uint32_t a, uint32_t v);
void     ps1_w32_slow(Cpu *c, uint32_t a, uint32_t v);
void     ps1_read_bytes(Cpu *c, uint32_t a, uint8_t *out, uint32_t n);
void     ps1_write_bytes(Cpu *c, uint32_t a, const uint8_t *data, uint32_t n);

/* ---- aritmetica (alu.c) ---- */
void     ps1_mult(Cpu *c, uint32_t a, uint32_t b);
void     ps1_multu(Cpu *c, uint32_t a, uint32_t b);
void     ps1_div(Cpu *c, uint32_t a, uint32_t b);
void     ps1_divu(Cpu *c, uint32_t a, uint32_t b);
uint32_t ps1_lwl(Cpu *c, uint32_t a, uint32_t cur);
uint32_t ps1_lwr(Cpu *c, uint32_t a, uint32_t cur);
void     ps1_swl(Cpu *c, uint32_t a, uint32_t v);
void     ps1_swr(Cpu *c, uint32_t a, uint32_t v);

/* ---- control (cpu.c) ---- */
void     ps1_call(Cpu *c, uint32_t addr);
uint32_t ps1_call_nested(Cpu *c, uint32_t f, const uint32_t *args, uint32_t n);
void     ps1_run_cont(Cpu *c);
void     ps1_jump(Cpu *c, uint32_t pc);
void     ps1_tick(Cpu *c);
void     ps1_syscall(Cpu *c);
void     ps1_service_interrupts(Cpu *c);
void     ps1_defer(Cpu *c, uint32_t f, const uint32_t *args, uint32_t n);
PS1_NORETURN void ps1_panic(Cpu *c, const char *msg);

/* ---- GTE (gte.c) ---- */
void     ps1_gte(Cpu *c, uint32_t w);
void     ps1_gte_command(Cpu *c, uint32_t w);
uint32_t ps1_gte_read(const Cpu *c, uint32_t r);
/* Division Newton-Raphson del GTE; *ok a 0 cuando no hay resultado (h >= sz3 * 2). */
uint32_t ps1_gte_divide(Cpu *c, uint32_t h, uint32_t sz3, int *ok);
void     ps1_gte_write(Cpu *c, uint32_t r, uint32_t v);

/* ---- BIOS HLE (bios.c) e interprete (interp.c) ---- */
void     ps1_bios_call(Cpu *c, uint32_t table);
void     ps1_interpret(Cpu *c, uint32_t pc);
/* Las entradas al interprete, por direccion: dice que le falta a la recompilacion. */
#define PS1_INTERP_TOP 32u
extern uint32_t ps1_interp_addr[PS1_INTERP_TOP];
extern uint64_t ps1_interp_hits[PS1_INTERP_TOP];

/* ---- pila de diagnostico ------------------------------------------------------- */
static inline void ps1_push(Cpu *c, uint32_t f)
{
    if (PS1_LIKELY(c->stack_depth < PS1_STACK_MAX))
        c->stack[c->stack_depth] = f;
    c->stack_depth++;
}

static inline void ps1_pop(Cpu *c)
{
    if (PS1_LIKELY(c->stack_depth != 0))
        c->stack_depth--;
}

/* ---- cargas y almacenamientos sin alinear -------------------------------------- */
#if PS1_COUNT_ACCESS
#define PS1_NOTE(c, o, n)                                                  \
    do {                                                                   \
        if (PS1_UNLIKELY(((o) & ((n) - 1u)) != 0u)) (c)->misaligned++;      \
        if (PS1_UNLIKELY((o) > PS1_RAM_MASK - ((n) - 1u))) (c)->ram_edge++; \
    } while (0)
#else
#define PS1_NOTE(c, o, n) ((void)0)
#endif

static inline uint32_t ps1_ld16(const uint8_t *p)
{
    if (PS1_UNLIKELY((uintptr_t)p & 1u))
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    return *(const ps1_u16a *)p;
}

static inline uint32_t ps1_ld32(const uint8_t *p)
{
    if (PS1_UNLIKELY((uintptr_t)p & 3u))
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return *(const ps1_u32a *)p;
}

static inline void ps1_st16(uint8_t *p, uint32_t v)
{
    if (PS1_UNLIKELY((uintptr_t)p & 1u)) {
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
        return;
    }
    *(ps1_u16a *)p = (uint16_t)v;
}

static inline void ps1_st32(uint8_t *p, uint32_t v)
{
    if (PS1_UNLIKELY((uintptr_t)p & 3u)) {
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
        return;
    }
    *(ps1_u32a *)p = v;
}

/* ---- accesores a memoria: ruta rapida ------------------------------------------
 * Nunca pueden ser pure ni const: escribir en r0 descarta el valor pero el acceso
 * se ejecuta igual (el generador emite (void)(ps1_r8(...)) en 149 sitios), y una
 * lectura de IO tiene efecto lateral.
 */
static inline uint32_t ps1_r8(Cpu *c, uint32_t a)
{
    uint32_t p = a & 0x1FFFFFFFu;
    if (PS1_LIKELY(p < PS1_IO_BASE))
        return c->ram[p & PS1_RAM_MASK];
    return ps1_r8_slow(c, a);
}

static inline uint32_t ps1_r16(Cpu *c, uint32_t a)
{
    uint32_t p = a & 0x1FFFFFFFu;
    if (PS1_LIKELY(p < PS1_IO_BASE)) {
        uint32_t o = p & PS1_RAM_MASK;
        PS1_NOTE(c, o, 2u);
        return ps1_ld16(c->ram + o);
    }
    return ps1_r16_slow(c, a);
}

static inline uint32_t ps1_r32(Cpu *c, uint32_t a)
{
    uint32_t p = a & 0x1FFFFFFFu;
    if (PS1_LIKELY(p < PS1_IO_BASE)) {
        uint32_t o = p & PS1_RAM_MASK;
        PS1_NOTE(c, o, 4u);
        return ps1_ld32(c->ram + o);
    }
    return ps1_r32_slow(c, a);
}

static inline void ps1_w8(Cpu *c, uint32_t a, uint32_t v)
{
    uint32_t p = a & 0x1FFFFFFFu;
    if (PS1_LIKELY(p < PS1_IO_BASE)) {
        c->ram[p & PS1_RAM_MASK] = (uint8_t)v;
        return;
    }
    ps1_w8_slow(c, a, v);
}

static inline void ps1_w16(Cpu *c, uint32_t a, uint32_t v)
{
    uint32_t p = a & 0x1FFFFFFFu;
    if (PS1_LIKELY(p < PS1_IO_BASE)) {
        uint32_t o = p & PS1_RAM_MASK;
        PS1_NOTE(c, o, 2u);
        ps1_st16(c->ram + o, v);
        return;
    }
    ps1_w16_slow(c, a, v);
}

static inline void ps1_w32(Cpu *c, uint32_t a, uint32_t v)
{
    uint32_t p = a & 0x1FFFFFFFu;
    if (PS1_LIKELY(p < PS1_IO_BASE)) {
        uint32_t o = p & PS1_RAM_MASK;
        PS1_NOTE(c, o, 4u);
        ps1_st32(c->ram + o, v);
        return;
    }
    ps1_w32_slow(c, a, v);
}

#endif /* PS1RT_H */
