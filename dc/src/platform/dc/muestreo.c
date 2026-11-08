/* M8: de donde sale el tiempo en una consola real, sin cable serie.
 *
 * El TMU1 interrumpe unas 1 000 veces por segundo y anota donde estaba el PC y en que seccion del
 * frame estaba el host (HOST_SEC en host.h). PRFC1 va midiendo por turnos instrucciones, paradas
 * de cache y demas. Cuando se llena, el juego se para y el informe sale por la TELE, por paginas,
 * para fotografiarlo: en una Dreamcast real no hay otra salida con sitio para esto.
 *
 * Solo en `make dc-perfil`, que enlaza dos veces: la segunda con la tabla de simbolos del ELF de la
 * primera (datos, no codigo: no mueve .text, y tools/recomp/perfil_syms.py lo comprueba).
 *
 * Lo que no ve: el tiempo DENTRO de otras interrupciones, que tienen las interrupciones enmascaradas.
 */
#ifdef NFSHS_PERFIL
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos.h>
#include <arch/timer.h>
#include <dc/biosfont.h>
#include <dc/perfctr.h>
#include <dc/spu.h>
#include <dc/video.h>

#include "muestreo.h"

typedef struct PerfilSim {
    uint32_t addr;
    char     name[20];
} PerfilSim;

extern const PerfilSim perfil_tab[] __attribute__((weak));
extern const uint32_t  perfil_tab_n __attribute__((weak));

#define HZ_MUESTREO  997u            /* primo: no va en fase con los 60 Hz ni con el reloj de KOS */
#define MUESTRAS     40000u          /* unos 40 s */
#if defined(PERFIL_ESPERA_MS)
#define ESPERA_MS    PERFIL_ESPERA_MS
#elif defined(NFSHS_SERIE)
#define ESPERA_MS    5000u           /* con guion, en Flycast, para el volcado de perfil_orden.py */
#else
#define ESPERA_MS    35000u          /* tras empezar la carrera: salta la carga y da tiempo a llegar al trafico */
#endif
#define TURNO_MS     400u            /* 10 contadores: cada uno se mide 10 veces repartidas por los 40 s, no en un solo trozo */
#define HILO_AJENO   0x80u

static uint32_t s_pc[MUESTRAS];
static uint8_t  s_sec[MUESTRAS];
static volatile uint32_t s_n;
static volatile int s_activo;
static kthread_t *s_principal;

static const char *const SECCION[HSEC_N] = {
    "juego (simulacion)", "vista: camara y trozos", "grabar objetos", "grabar destellos",
    "cielo, vista y HUD", "grabar coches", "frontera de frame", "texturas PS1",
    "espera al PVR", "dibujo: cielo", "dibujo: pista", "dibujo: objetos", "dibujo: coches",
    "dibujo: translucido", "dibujo: HUD 2D", "dibujo: resto", "espera al reloj",
};

typedef struct Evento {
    perf_cntr_event_t ev;
    const char *nombre;
    uint64_t cuenta, ms, frames;
} Evento;

static Evento s_ev[] = {
    { PMCR_INSTRUCTION_ISSUED_MODE,             "instrucciones",      0, 0, 0 },
    { PMCR_PARALLEL_INSTRUCTION_ISSUED_MODE,    "en paralelo",        0, 0, 0 },
    { PMCR_PIPELINE_FREEZE_BY_ICACHE_MISS_MODE, "paro cache instr",   0, 0, 0 },
    { PMCR_PIPELINE_FREEZE_BY_DCACHE_MISS_MODE, "paro cache datos",   0, 0, 0 },
    { PMCR_PIPELINE_FREEZE_BY_BRANCH_MODE,      "paro por saltos",    0, 0, 0 },
    { PMCR_INSTRUCTION_CACHE_MISS_MODE,         "fallos cache instr", 0, 0, 0 },
    { PMCR_OPERAND_CACHE_READ_MISS_MODE,        "fallos lect datos",  0, 0, 0 },
    { PMCR_OPERAND_CACHE_WRITE_MISS_MODE,       "fallos escr datos",  0, 0, 0 },
    { PMCR_SUBROUTINE_ISSUED_MODE,              "llamadas",           0, 0, 0 },
    { PMCR_INTERRUPT_COUNTER_MODE,              "interrupciones",     0, 0, 0 },
};
#define EVENTOS (sizeof s_ev / sizeof s_ev[0])

/* Lo que cuenta PRFC1, repartido por secciones: en cada cambio de seccion (HOST_SEC) se lee el
 * contador y la diferencia va a la seccion que termina. Exacto, sin depender del muestreo; lo de
 * otros hilos e interrupciones que caiga en medio cuenta para la seccion en curso. */
static uint64_t s_por_sec[EVENTOS][HSEC_N];
static uint64_t s_ult;
static unsigned s_e;

void host_sec_cambia(unsigned n)
{
    if (s_activo) {
        uint64_t v = perf_cntr_count(PRFC1);
        unsigned s = host_seccion;

        if (s < HSEC_N)
            s_por_sec[s_e][s] += v - s_ult;
        s_ult = v;
    }
    host_seccion = (unsigned char)n;
}

static void tick(irq_t src, irq_context_t *cxt, void *data)
{
    (void)src;
    (void)data;
    timer_clear(TMU1);
    if (s_activo && s_n < MUESTRAS) {
        s_pc[s_n] = cxt->pc;
        s_sec[s_n] = (uint8_t)(host_seccion | (thd_current != s_principal ? HILO_AJENO : 0u));
        s_n++;
    }
}

static void evento_empieza(unsigned e)
{
    perf_cntr_stop(PRFC1);
    perf_cntr_clear(PRFC1);
    s_ult = 0;
    s_e = e;
    perf_cntr_start(PRFC1, s_ev[e].ev, PMCR_COUNT_CPU_CYCLES);
}

/* ---- el informe ---- */

static int simbolo(uint32_t pc)
{
    int lo = 0, hi = (int)perfil_tab_n - 1, r = -1;

    while (lo <= hi) {
        int m = (lo + hi) / 2;

        if (perfil_tab[m].addr <= pc) {
            r = m;
            lo = m + 1;
        } else {
            hi = m - 1;
        }
    }
    return r;
}

static unsigned s_fila;

static void pagina(unsigned k, unsigned total)
{
    vid_clear(0, 0, 0);
    s_fila = 0;
    bfont_draw_str_vram_fmt(24, 4, true, "PERFIL %u/%u", k, total);
}

static void fila(unsigned col, const char *txt)
{
    bfont_draw_str_vram_fmt(24u + col * 300u, 30u + s_fila * 24u, true, "%s", txt);
}

#ifdef NFSHS_SERIE
/* Todo el perfil por el puerto serie, para tools/recomp/perfil_orden.py: "PF <addr> <seccion> <n>";
 * la seccion HSEC_N son los otros hilos. */
static void volcado(uint32_t n, uint32_t nsim)
{
    uint32_t *t = calloc((size_t)nsim * (HSEC_N + 1u), sizeof *t);
    uint32_t i;

    if (t == NULL) {
        printf("PF-SIN-MEMORIA\n");
        return;
    }
    for (i = 0; i < n; i++) {
        int k = simbolo(s_pc[i]);
        uint32_t sec = (s_sec[i] & HILO_AJENO) ? HSEC_N : (s_sec[i] & 0x7Fu);

        if (k >= 0 && sec <= HSEC_N)
            t[(uint32_t)k * (HSEC_N + 1u) + sec]++;
    }
    for (i = 0; i < nsim; i++) {
        uint32_t j;

        for (j = 0; j <= HSEC_N; j++)
            if (t[i * (HSEC_N + 1u) + j] != 0u)
                printf("PF %08lx %lu %lu\n", (unsigned long)perfil_tab[i].addr, (unsigned long)j,
                       (unsigned long)t[i * (HSEC_N + 1u) + j]);
    }
    printf("PF-FIN\n");
    free(t);
}
#endif

static uint32_t *s_cuenta;

static int cmp_cuenta(const void *a, const void *b)
{
    uint32_t x = s_cuenta[*(const uint32_t *)a], y = s_cuenta[*(const uint32_t *)b];

    return x < y ? 1 : (x > y ? -1 : 0);
}

/* Decimas de `parte` sobre `todo`, como "12.3" */
static void pct(char *o, size_t n, uint64_t parte, uint64_t todo)
{
    unsigned d = todo ? (unsigned)(parte * 1000u / todo) : 0u;

    snprintf(o, n, "%2u.%u", d / 10u, d % 10u);
}

static void informe(uint64_t frames, uint64_t campos, uint64_t ms)
{
    enum { POR_PAGINA = 17, PAGINAS_TOP = 3, PAGINAS_FIJAS = 3 };
    static uint32_t sec[HSEC_N];
    uint32_t i, ajenos = 0, juego = 0, sonido = 0, runtime = 0, nosim = 0;
    uint32_t nsim = &perfil_tab_n != NULL ? perfil_tab_n : 0u;
    uint32_t *orden = NULL;
    uint32_t n = s_n;
    char l[64], p[16];
    unsigned msf10 = frames ? (unsigned)(ms * 10u / frames) : 0u;
    unsigned pag = 0;

    s_activo = 0;
    timer_stop(TMU1);
    perf_cntr_stop(PRFC1);
    spu_disable();
    pvr_shutdown();
    vid_set_start(0);

    if (nsim != 0u) {
        s_cuenta = calloc(nsim, sizeof *s_cuenta);
        orden = malloc(nsim * sizeof *orden);
    }
    for (i = 0; i < n; i++) {
        int k;

        if (s_sec[i] & HILO_AJENO)
            ajenos++;
        else if ((s_sec[i] & 0x7Fu) < HSEC_N)
            sec[s_sec[i] & 0x7Fu]++;
        k = (s_cuenta != NULL) ? simbolo(s_pc[i]) : -1;
        if (k < 0) {
            nosim++;
            continue;
        }
        s_cuenta[k]++;
        if (strncmp(perfil_tab[k].name, "f_", 2) == 0) {
            uint32_t a = (uint32_t)strtoul(perfil_tab[k].name + 2, NULL, 16);

            if (a >= 0x800E7000u && a < 0x8010D000u)
                sonido++;
            else
                juego++;
        } else if (strncmp(perfil_tab[k].name, "ps1_", 4) == 0) {
            runtime++;
        }
    }
    if (orden != NULL) {
        for (i = 0; i < nsim; i++)
            orden[i] = i;
        qsort(orden, nsim, sizeof *orden, cmp_cuenta);
    }

#ifdef NFSHS_SERIE
    if (nsim != 0u)
        volcado(n, nsim);
#endif

    for (;;) {
        unsigned total = PAGINAS_FIJAS + PAGINAS_TOP;
        uint64_t turnos = 0;

        pag = pag % total;
        pagina(pag + 1u, total);
        if (pag == 0u) {
            snprintf(l, sizeof l, "%lu ms %lu fr  %u.%u ms/fr  campos/fr %u.%u",
                     (unsigned long)ms, (unsigned long)frames, msf10 / 10u, msf10 % 10u,
                     frames ? (unsigned)(campos / frames) : 0u,
                     frames ? (unsigned)(campos * 10u / frames % 10u) : 0u);
            fila(0, l); s_fila++;
            for (i = 0; i < HSEC_N; i++) {
                unsigned d = n ? (unsigned)((uint64_t)sec[i] * msf10 / n) : 0u;

                pct(p, sizeof p, sec[i], n);
                snprintf(l, sizeof l, "%s %3u.%u ms  %s", p, d / 10u, d % 10u, SECCION[i]);
                fila(0, l); s_fila++;
            }
        } else if (pag == 1u) {
            pct(p, sizeof p, ajenos, n);
            snprintf(l, sizeof l, "%s otros hilos", p);
            fila(0, l); s_fila++;
            pct(p, sizeof p, juego, n);
            snprintf(l, sizeof l, "%s codigo del juego", p);
            fila(0, l); s_fila++;
            pct(p, sizeof p, sonido, n);
            snprintf(l, sizeof l, "%s driver de sonido EA", p);
            fila(0, l); s_fila++;
            pct(p, sizeof p, runtime, n);
            snprintf(l, sizeof l, "%s ps1_* (runtime)", p);
            fila(0, l); s_fila++;
            pct(p, sizeof p, n - juego - sonido - runtime - nosim, n);
            snprintf(l, sizeof l, "%s host, render, KOS", p);
            fila(0, l); s_fila += 2u;
            for (i = 0; i < EVENTOS; i++) {
                const Evento *e = &s_ev[i];

                /* por frame, en miles; y por ms (200 000 ciclos) */
                snprintf(l, sizeof l, "%6lu K/f %6lu/ms %s",
                         (unsigned long)(e->frames ? e->cuenta / e->frames / 1000u : 0u),
                         (unsigned long)(e->ms ? e->cuenta / e->ms : 0u), e->nombre);
                fila(0, l); s_fila++;
                turnos += e->ms;
            }
            /* si esto no es 0 y lo de arriba si, el contador no cuenta (Flycast no los modela) */
            snprintf(l, sizeof l, "contadores medidos %lu ms", (unsigned long)turnos);
            fila(0, l); s_fila++;
        } else if (pag == 2u) {
            /* por seccion y por frame: instrucciones (miles), ms de paro por cache de
             * instrucciones, fallos de cache de instrucciones, ms de paro por cache de datos */
            static const char *const CORTO[HSEC_N] = {
                "juego", "vista", "g objetos", "g destellos", "cielo,vis,HUD", "g coches",
                "frontera", "texturas PS1", "espera PVR", "d cielo", "d pista", "d objetos",
                "d coches", "d translucido", "d HUD 2D", "d resto", "espera reloj",
            };
            enum { E_INS = 0, E_PARO_I = 2, E_PARO_D = 3, E_FALLO_I = 5 };
            uint64_t fi = s_ev[E_INS].frames, fpi = s_ev[E_PARO_I].frames;
            uint64_t fpd = s_ev[E_PARO_D].frames, ffi = s_ev[E_FALLO_I].frames;

            fila(0, "seccion      Kinst/f  paroI  fallosI/f  paroD"); s_fila++;
            for (i = 0; i < HSEC_N; i++) {
                /* paro en decimas de ms por frame: ciclos / 20 000 (200 MHz) */
                unsigned pi = fpi ? (unsigned)(s_por_sec[E_PARO_I][i] / 20000u / fpi) : 0u;
                unsigned pd = fpd ? (unsigned)(s_por_sec[E_PARO_D][i] / 20000u / fpd) : 0u;

                snprintf(l, sizeof l, "%-13s%7lu %3u.%u %10lu %3u.%u", CORTO[i],
                         (unsigned long)(fi ? s_por_sec[E_INS][i] / 1000u / fi : 0u),
                         pi / 10u, pi % 10u,
                         (unsigned long)(ffi ? s_por_sec[E_FALLO_I][i] / ffi : 0u),
                         pd / 10u, pd % 10u);
                fila(0, l); s_fila++;
            }
        } else if (orden != NULL) {
            uint32_t ini = (pag - PAGINAS_FIJAS) * 2u * POR_PAGINA, r;

            for (r = 0; r < 2u * POR_PAGINA; r++) {
                uint32_t k;

                if (ini + r >= nsim || s_cuenta[orden[ini + r]] == 0u)
                    break;
                k = orden[ini + r];
                pct(p, sizeof p, s_cuenta[k], n);
                snprintf(l, sizeof l, "%s %.19s", p, perfil_tab[k].name);
                s_fila = r % POR_PAGINA;
                fila(r / POR_PAGINA, l);
            }
        } else {
            fila(0, "sin tabla de simbolos");
        }
        /* otra pagina cada 12 s: tiempo de sobra para una foto */
        thd_sleep(12000);
        pag++;
    }
}

/* ---- la maquina de estados, una vez por frame ---- */

void muestreo_frame(GameHost *h)
{
    static uint64_t t_carrera, t0, f0, c0, t_ev, f_ev;
    static unsigned e;
    static int estado;
    uint64_t ahora;

    if (h->mode != HOST_RACE)
        return;
    ahora = timer_ms_gettime64();
    if (estado == 0) {
        t_carrera = ahora;
        estado = 1;
        return;
    }
    if (estado == 1) {
        if (ahora - t_carrera < ESPERA_MS)
            return;
        s_principal = thd_current;
        irq_set_handler(EXC_TMU1_TUNI1, tick, NULL);
        timer_prime(TMU1, HZ_MUESTREO, 1);
        timer_clear(TMU1);
        timer_start(TMU1);
        t0 = t_ev = ahora;
        f0 = f_ev = h->frames;
        c0 = h->fields;
        evento_empieza(0);
        s_activo = 1;
        estado = 2;
        return;
    }
    if (ahora - t_ev >= TURNO_MS) {
        host_sec_cambia(host_seccion);   /* lo que va de la seccion en curso, a este turno */
        s_ev[e].cuenta += perf_cntr_count(PRFC1);
        s_ev[e].ms += ahora - t_ev;
        s_ev[e].frames += h->frames - f_ev;
        e = (e + 1u) % EVENTOS;
        evento_empieza(e);
        t_ev = ahora;
        f_ev = h->frames;
    }
    if (s_n >= MUESTRAS)
        informe(h->frames - f0, h->fields - c0, ahora - t0);
}
#endif /* NFSHS_PERFIL */
