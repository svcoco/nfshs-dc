/* Arranque de Dreamcast (M1b, paso 3): enciende la maquina de PS1, corre el codigo
 * recompilado sobre el SH-4 y reporta.
 *
 * Es el hermano de src/platform/pc/main.c, con la misma secuencia de encendido
 * (power_on()/boot() de src/ps1/race.rs). Las diferencias son tres y solo tres:
 *
 *   - los archivos salen del GD-ROM (/cd) en vez de gamedata/;
 *   - la lista HLE sale de la tabla horneada (gen/hle_table.c) en vez de un .txt;
 *   - la entrada sale del mando por maple en vez de un guion.
 *
 * Nada de esto toca src/ps1rt, src/host ni gen/: ahi no entra ninguna cabecera de KOS.
 */
#include <kos.h>

#include "addr.h"
#include "funcs.h"          /* gen: ps1_dispatch, ps1_dispatch_at */
#include "host.h"
#include "ps1rt.h"
#include "spu.h"
#include "aica_spu.h"
#include "mando.h"
#include "render.h"
#include "video.h"
#include "movie.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

/* Los 2 MB de la PS1 mas el relleno del borde. En BSS: el ELF no engorda por esto. */
static uint8_t  ram[PS1_RAM_SIZE + PS1_RAM_PAD];
static Cpu      cpu;
static GameHost host;
static SpuStub  spu;

#define GAMEDATA   "/cd"
/* El juego hace esperas activas sobre el timer; tras tantas ramas hacia atras sin pasar
 * por el host se deja correr el tiempo, como harian las interrupciones. Igual que PC. */
#define SPIN_LIMIT 20000u
#define TICK_LIMIT 2000000000ull

static uint64_t t_start;
static uint64_t last_report_frame;
static uint64_t last_report_ms;
/* Cuando y en que frame empezo la carrera, para poder dar sus fps por separado: el
   front end y la carrera cuestan cosas muy distintas y una media conjunta no dice
   nada util. */
static uint64_t race_ms0, race_frame0;
static uint64_t race600_ms;                 /* los primeros 600 frames de carrera */


/* Se llama una vez por frame del juego, desde host_frame_top. Es ademas el unico sitio
 * donde el host recupera el control mientras el juego corre, asi que aqui va tambien el
 * latido en pantalla: sin el no hay forma de saber si avanza o se colgo. */
static void on_frame(GameHost *h)
{
    /* Con un guion cargado manda el guion: si el mando se leyera aqui, entre paso y
     * paso del guion dejaria los botones a 0 y se perderia el "mantener hasta la
     * linea siguiente" (pad_write solo escribe en los frames que tienen paso). */
    if (h->mode == HOST_RACE && race_frame0 == 0u) {
        race_frame0 = h->frames;
        race_ms0 = timer_ms_gettime64() - t_start;
        printf("--- CARRERA desde el frame %lu ---\n", (unsigned long)h->frames);
    }
    /* Medida fija para comparar versiones: lo que tardan los primeros 600 frames de
       carrera (las ventanas de 60 frames bailan con las cargas del stream de musica). */
    if (race_frame0 != 0u && h->frames == race_frame0 + 600u) {
        uint64_t ms = timer_ms_gettime64() - t_start - race_ms0;
        race600_ms = ms;
        printf("--- CARRERA: 600 frames en %lu ms = %lu.%02lu fps ---\n", (unsigned long)ms,
               (unsigned long)(600000u / ms), (unsigned long)(60000000u / ms % 100u));
    }

    if (h->pad_steps_n == 0u)
        mando_lee(h, &cpu);

    /* Los primeros frames se anuncian de uno en uno: lo que hay que saber antes que
     * el rendimiento es si el juego ARRANCA. Despues, cada 60 frames. */
    if (h->frames <= 3u || h->frames - last_report_frame >= 60u) {
        uint64_t ms = timer_ms_gettime64() - t_start;
        uint64_t dms = ms - last_report_ms;
        uint64_t df = h->frames - last_report_frame;
        last_report_frame = h->frames;
        /* fps del TRAMO, no la media desde el arranque: la media arrastra para siempre
           los ~30 s de carga inicial del GD-ROM y no dice como va el juego ahora. */
        printf("frame %6lu   %lu.%02lu fps   media %lu.%02lu   interp %lu\n",
               (unsigned long)h->frames,
               (unsigned long)(dms ? df * 1000u / dms : 0u),
               (unsigned long)(dms ? (df * 100000u / dms) % 100u : 0u),
               (unsigned long)(ms ? h->frames * 1000u / ms : 0u),
               (unsigned long)(ms ? (h->frames * 100000u / ms) % 100u : 0u),
               (unsigned long)cpu.interpreted);
        last_report_ms = ms;
    }
}

/* Hilo de diagnostico. El juego corre dentro de ps1_call() y no suelta el control
   hasta la frontera de frame, asi que si no llega a la primera no hay forma de saber
   si avanza o esta girando. Esto lo cuenta desde fuera, cada 3 segundos. */
static int diag_corriendo = 1;

/* Cuantas veces ha visto el host esa direccion (host.seen[] la lleva por direccion). */
static uint64_t veces(uint32_t addr)
{
    uint32_t i;
    for (i = 0; i < host.seen_addrs; i++)
        if (host.seen[i].addr == addr)
            return host.seen[i].n;
    return 0;
}

/* La ultima direccion HLE que aparecio por primera vez: dice por donde va el juego. */
static uint32_t ultima_hle(void)
{
    uint32_t i, mejor = 0;
    uint64_t cuando = 0;
    for (i = 0; i < host.seen_addrs; i++)
        if (host.seen[i].first > cuando) {
            cuando = host.seen[i].first;
            mejor = host.seen[i].addr;
        }
    return mejor;
}

static void *diag_thread(void *arg)
{
    uint64_t prev_calls = 0;
    uint64_t prev_frames = 0;

    (void)arg;
    while (diag_corriendo) {
        thd_sleep(3000);
        printf("[%lus] ticks %lu  llamadas %lu (+%lu)  interp %lu  frames %lu\n",
               (unsigned long)((timer_ms_gettime64() - t_start) / 1000u),
               (unsigned long)cpu.ticks, (unsigned long)cpu.calls,
               (unsigned long)(cpu.calls - prev_calls),
               (unsigned long)cpu.interpreted, (unsigned long)host.frames);
        if (cpu.calls == prev_calls || host.frames == prev_frames) {
            /* el juego gira sin llamar a nada: donde */
            uint32_t d = cpu.stack_depth, k;
            printf("        GIRA: irq %d  ticks pendientes %lu  pila (%lu):", cpu.in_irq,
                   (unsigned long)cpu.pending_ticks, (unsigned long)d);
            for (k = 0; k < 8u && k < d && d <= PS1_STACK_MAX; k++)
                printf(" %08lx", (unsigned long)cpu.stack[d - 1u - k]);
            printf("\n        ultimas al host:");
            for (k = 0; k < 4u; k++) {
                uint32_t i = (cpu.recent_at + 15u - k) % 16u;
                printf(" %08lx<-%08lx", (unsigned long)cpu.recent[i][0], (unsigned long)cpu.recent[i][1]);
            }
            printf("\n");
        }
        prev_calls = cpu.calls;
        prev_frames = host.frames;
        printf("        acumulado: video wall %lu ms (%lu frames)  texturas %lu ms  dibujo %lu ms\n",
               (unsigned long)(host.video_us / 1000u), (unsigned long)host.video_frames,
               (unsigned long)(host.tex_us / 1000u), (unsigned long)(host.draw_us / 1000u));
        aica_spu_report();
        if (race600_ms)
            printf("        carrera: 600 frames en %lu ms\n", (unsigned long)race600_ms);
        /* el reloj del juego (unidades de 1/1920 s) frente al real desde la primera frontera
           de frame: con reloj real, la diferencia no debe crecer */
        if (host.real_us0 != 0u)
            printf("        reloj del juego %lu ms frente al real %lu ms (desde el ancla); descartado %lu ms\n",
                   (unsigned long)((host.clock - host.real_clock0) * 1000u / 1920u),
                   (unsigned long)((timer_us_gettime64() - host.real_us0) / 1000u),
                   (unsigned long)(host.real_dropped * 1000u / 1920u));
        printf("        archivos %lu ok / %lu fallidos  BIGF %lu ok / %lu fallidos\n",
               (unsigned long)host.files.opens, (unsigned long)host.files.opens_failed,
               (unsigned long)host.files.mounts_ok,
               (unsigned long)host.files.mounts_failed);
        if (host.files.last_missing[0])
            printf("        ultimo que falto: %s\n", host.files.last_missing);
        /* Las tres llamadas que deciden si el front end arranca, por direccion:
           opensync, addbigsync y readsync. Si addbigsync sale 0, el juego ni siquiera
           esta intentando montar zzzFE.viv y la divergencia es anterior. */
        printf("        opensync %lu  addbigsync %lu  readsync %lu  ultima HLE %#lx\n",
               (unsigned long)veces(0x800EB204u), (unsigned long)veces(0x800EB354u),
               (unsigned long)veces(0x800EB27Cu), (unsigned long)ultima_hle());
        printf("        montaje[0]: handle %#lx  entradas %lu   next_handle %#lx\n",
               (unsigned long)host.files.mounts[0].handle,
               (unsigned long)host.files.mounts[0].n,
               (unsigned long)host.files.next_handle);
        /* host.mode pasa a carrera en SIM_MAINGAMELOOP (views.c) y player_views
           cuenta las vistas 3D pedidas: juntos dicen si la carrera arranco. */
        printf("        modo: %s   vistas 3D %lu   guion %lu/%lu\n",
               host.mode == HOST_RACE ? "CARRERA" : "front end",
               (unsigned long)host.player_views,
               (unsigned long)host.pad_at, (unsigned long)host.pad_steps_n);
        /* La pista: si no se ve nada, esto dice si es que no cargo o si es que se
           dibuja y no se ve. */
        printf("        pista: %s   %llu triangulos en %llu frames\n",
               host.track_name[0] != '\0' ? host.track_name : "(ninguna)",
               (unsigned long long)render_tris(),
               (unsigned long long)render_tri_frames());
        printf("        coches: %lu vistos, %llu triangulos   objetos %lu\n",
               (unsigned long)host.cars_n, (unsigned long long)render_car_tris(),
               (unsigned long)host.objects_n);
        printf("        efectos restados: %llu triangulos\n",
               (unsigned long long)render_fx_tris());
        printf("        destellos: %llu cuadros dibujados en total; reflejos %llu triangulos\n",
               (unsigned long long)render_glow_quads(),
               (unsigned long long)render_refl_tris());
        prev_calls = cpu.calls;
    }
    return NULL;
}

static uint8_t *read_whole(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (uint8_t *)malloc((size_t)n);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void esperar_start(void)
{
    printf("\n[START para salir]\n");
    for (;;) {
        maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *st = dev ? (cont_state_t *)maple_dev_status(dev) : NULL;

        if (st && (st->buttons & CONT_START))
            return;
        thd_sleep(50);
    }
}

int main(int argc, char **argv)
{
    uint8_t *exe;
    size_t exe_len;
    uint32_t a;
    uint64_t ms;

    (void)argc;
    (void)argv;

    /* El modo de video va ANTES que la consola de pantalla, que se monta sobre el
     * framebuffer del modo vigente. Si el disco trae /cd/VIDEO.TXT con "vga", "ntsc"
     * o "pal", manda eso; si no, lo decide el cable. */
    {
        char forzado[16];
        FILE *vf = fopen(GAMEDATA "/VIDEO.TXT", "r");
        size_t i;

        forzado[0] = '\0';
        if (vf != NULL) {
            if (fgets(forzado, (int)sizeof(forzado), vf) == NULL)
                forzado[0] = '\0';
            fclose(vf);
            /* fuera el salto de linea, y a minusculas */
            for (i = 0; forzado[i] != '\0'; i++) {
                if (forzado[i] == '\r' || forzado[i] == '\n') {
                    forzado[i] = '\0';
                    break;
                }
                if (forzado[i] >= 'A' && forzado[i] <= 'Z')
                    forzado[i] = (char)(forzado[i] - 'A' + 'a');
            }
        }
        video_init(forzado[0] ? forzado : NULL);
    }

    /* A pantalla: sin cable serie ni dcload es la unica salida que hay. */
    dbgio_dev_select("fb");
    dbgio_enable();

    printf("NFS High Stakes -> Dreamcast\n");
    printf("%lu funciones recompiladas\n", (unsigned long)ps1_range_n);
    printf("video: %s   (cable: %s)%s\n\n", video_mode_name(),
           video_cable_name(), video_forced() ? "  [forzado]" : "");

    /* Donde esta el sistema de archivos, segun el disco. KOS lo busca SIEMPRE en el
     * area de baja densidad (fs_iso9660.c: cdrom_read_toc(&toc, false)), asi que la
     * imagen tiene que llevarlo ahi y no en la de alta, que es la de un GD-ROM. Esto
     * se imprime para poder construir el ISO con el offset exacto en vez de adivinarlo. */
    {
        cd_toc_t toc;
        int r = cdrom_read_toc(&toc, false);
        printf("TOC baja densidad: r=%d", r);
        if (r == 0)
            printf("  pista de datos en LBA %lu",
                   (unsigned long)cdrom_locate_data_track(&toc));
        printf("\n");
    }

    /* ---- encendido: NFS4.EXE en una RAM de 2 MB limpia ---- */
    ps1_init(&cpu, ram);
    exe = read_whole(GAMEDATA "/NFS4.EXE", &exe_len);
    if (exe == NULL) {
        printf("ERROR: no pude leer " GAMEDATA "/NFS4.EXE\n");
        printf("(el disco tiene que llevar los datos del juego)\n");
        esperar_start();
        return 1;
    }
    printf("NFS4.EXE: %lu bytes\n", (unsigned long)exe_len);
    ps1_load_exe(&cpu, exe, exe_len);
    free(exe);
    /* el BSS lo pone a cero el arranque de la PS1, no el ejecutable */
    for (a = ADDR_BSS_START; a < ADDR_BSS_END; a += 4u)
        ps1_w32(&cpu, a, 0);

    cpu.dispatch = ps1_dispatch;
    cpu.dispatch_at = ps1_dispatch_at;
    /* IEc e IM2 del SR de COP0, como los deja la BIOS. Sin esto no se entrega ninguna
     * interrupcion de bus y el juego se queda esperando. */
    cpu.cop0[12] = 0x401u;
    ps1_spu_init(&spu);
    ps1_spu_attach(&spu, &cpu);
    /* M4: lo que el driver de sonido pide al SPU, al AICA */
#ifndef NFSHS_SIN_AUDIO
    aica_spu_init(&spu);
#endif
    cpu.tick_limit = TICK_LIMIT;
    cpu.spin_limit = SPIN_LIMIT;
#ifndef NFSHS_RELOJ_VIRTUAL
    cpu.ticks_per_service = 1;                   /* uno por interrupcion, como la PS1 */
#endif

    /* NULL = tabla HLE horneada (gen/hle_table.c), no hay .txt que leer */
    if (!host_init(&host, NULL, GAMEDATA)) {
        printf("ERROR: la lista HLE salio vacia\n");
        esperar_start();
        return 1;
    }
    /* Como la referencia por omision: marca pistas y coches como disponibles. Sin esto
     * no hay acceso a carreras (ver docs/hitos/M1a.md). */
    host.unlock = 1;
    /* El cono de visibilidad de los coches: 1 es el del original (4:3 de PS1). La
     * referencia usa 2, pero eso es una mejora suya y en el port va apagada
     * (docs/hitos/M1a.md). */
    host.car_cone_width = 1u;
    host.poll_input = on_frame;
    host.now_us = timer_us_gettime64;
#ifndef NFSHS_RELOJ_VIRTUAL
    host.real_clock = 1;                         /* el juego a velocidad real, como en la PS1 */
#endif
    /* El PVR: a partir de aqui el juego se DIBUJA, no solo se cuenta. */
    if (render_init()) {
        host.tex_buf = render_texbuf();
        host.tex_cap = render_texcap();
        host.on_draw = render_frame;
        /* las peliculas necesitan el PVR (el conversor YUV): solo con el render */
        host.play_movie = movie_play;
        printf("PVR listo: %lu texeles de cache de texturas\n",
               (unsigned long)render_texcap());
        /* A partir de aqui la pantalla es del juego, asi que los diagnosticos se van
         * al puerto serie: en Flycast salen en la ventana "Serial Output" y en
         * hardware por el cable serie o dcload. */
        printf("(los contadores siguen por el puerto serie)\n");
        dbgio_dev_select("scif");
    } else {
        printf("AVISO: el PVR no arranco; se sigue sin dibujar\n");
    }

    host_attach(&host, &cpu);

    /* Guion de entrada opcional. Si el disco trae uno, el juego se conduce solo hasta
     * donde diga; si no, manda el mando. Es como se prueba la logica de carrera sin
     * depender de que alguien navegue los menus a mano. */
    if (pad_load_script(&host, GAMEDATA "/SCRIPT.TXT"))
        printf("guion de entrada: %lu pasos, fin en el frame %lu\n",
               (unsigned long)host.pad_steps_n, (unsigned long)host.pad_end);
    else
        printf("sin guion en el disco: manda el mando\n");

    printf("%lu funciones en la lista HLE\n", (unsigned long)host.hle_n);
    printf("entrando por %#lx\n\n", (unsigned long)ADDR_MAIN);

    /* ---- marco de pila como el que monta la referencia antes de llamar ---- */
    cpu.r[29] = ADDR_STACK_TOP - 0x100u;
    cpu.r[30] = cpu.r[29];
    cpu.r[28] = ADDR_GP;
    cpu.r[31] = 0;
    cpu.stack_depth = 0;

    t_start = timer_ms_gettime64();
    thd_create(1, diag_thread, NULL);
    if (setjmp(cpu.stop_jmp) == 0) {
        cpu.stop_armed = 1;
        ps1_call(&cpu, ADDR_MAIN);
        printf("\nla funcion de entrada volvio (v0 = %#lx)\n", (unsigned long)cpu.r[2]);
    } else {
        printf("\nparada pedida por el host\n");
    }
    diag_corriendo = 0;
    ms = timer_ms_gettime64() - t_start;

    host_report(&host, &cpu);

    printf("\n=== %lu frames en %lu ms ===\n",
           (unsigned long)host.frames, (unsigned long)ms);
    if (ms != 0u && host.frames != 0u)
        printf("    %lu.%02lu fps de media, %lu ms por frame\n",
               (unsigned long)(host.frames * 1000u / ms),
               (unsigned long)((host.frames * 100000u / ms) % 100u),
               (unsigned long)(ms / host.frames));
    /* Lo que de verdad pide M1b: los fps de la CARRERA, sin la carga inicial del
       GD-ROM ni los menus, que cuestan algo muy distinto. */
    if (race_frame0 != 0u && ms > race_ms0) {
        uint64_t rf = host.frames - race_frame0;
        uint64_t rms = ms - race_ms0;
        printf("    carrera: %lu frames en %lu ms = %lu.%02lu fps\n",
               (unsigned long)rf, (unsigned long)rms,
               (unsigned long)(rf * 1000u / rms),
               (unsigned long)((rf * 100000u / rms) % 100u));
    } else {
        printf("    no se llego a la carrera\n");
    }
    esperar_start();
    return 0;
}
