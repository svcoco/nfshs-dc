/* Arnes x86 de M1a: enciende la maquina, corre el codigo recompilado y reporta.
 *
 * El objetivo de esta primera version no es que el juego funcione, es MEDIR: cuanto
 * avanza, que funciones del host pide y en que orden, cuantas veces cae en el
 * interprete, si hace accesos desalineados. Con eso se decide que implementar, en vez
 * de escribir 119 reemplazos a ciegas.
 *
 * Secuencia de arranque tomada de power_on()/boot() en src/ps1/race.rs.
 */
#include "addr.h"
#include "funcs.h"          /* dc/gen: ps1_dispatch, ps1_dispatch_at */
#include "host.h"
#include "raster.h"
#include "spu.h"
#include "ps1rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t ram[PS1_RAM_SIZE + PS1_RAM_PAD];
/* Buffer de texeles de la cache. Medido: el peor frame del front end son ~345 000
 * texeles (0,66 MB); con medio millon sobra. En Dreamcast esto sera memoria del
 * PVR y lo pondra la plataforma. */
static uint16_t texbuf[512u * 1024u];

/* --shot: el frame que hay que dibujar y donde dejarlo. Se hace desde el gancho por
 * frame porque es el unico punto en que la lista de dibujo esta completa: justo
 * despues se vacia para el frame siguiente. */
static uint64_t shot_frame;
static const char *shot_file;
static uint64_t track_frame;
static const char *track_file;    /* el PPM de salida */
static const char *track_trk;     /* el .TRK horneado */
static Track track;

static void pc_on_frame(GameHost *h)
{
    if (track_file != NULL && h->frames == track_frame) {
        if (track.raw == NULL && !track_load(&track, track_trk))
            fprintf(stderr, "no pude cargar la pista horneada %s\n", track_trk);
        else if (host_build_textures(h), raster_track(h, &track, texbuf, track_file, 640, 480))
            printf("frame %llu: pista dibujada en %s\n",
                   (unsigned long long)h->frames, track_file);
    }
    if (shot_file == NULL || h->frames != shot_frame)
        return;
    host_build_textures(h);
    if (raster_dump(h, texbuf, shot_file, h->display[2], h->display[3]))
        printf("frame %llu dibujado: %s (%ux%u)\n",
               (unsigned long long)h->frames, shot_file,
               (unsigned)h->display[2], (unsigned)h->display[3]);
    else
        fprintf(stderr, "no pude dibujar el frame %llu\n",
                (unsigned long long)h->frames);
}
static Cpu cpu;
static GameHost host;
static SpuStub spu;

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;
    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (uint8_t *)malloc((size_t)n);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "uso: %s [opciones]\n"
            "  --gamedata DIR   directorio con NFS4.EXE y FRONT.BIN (por omision: gamedata)\n"
            "  --hle FILE       lista de funciones HLE (por omision: tools/hle_funcs.txt)\n"
            "  --ticks N        limite de ramas hacia atras (por omision: 10000000)\n"
            "  --entry ADDR     funcion por la que entrar (por omision: main, %#x)\n"
            "  --survey         sigue tras la primera HLE sin implementar, para ver\n"
            "                   cuantas mas pide (el estado del juego deja de ser fiable)\n"
            "  --trace          traza cada llamada por direccion\n"
            "  --idlelog        donde gira el juego al detectar espera activa\n"
            "  --textlog        el texto en pantalla por frame (que pantalla es)\n"
            "  --unlock         pistas y coches disponibles, como la referencia\n"
            "  --clima W|S|N    fuerza mojado, nieve o noche (prueba; ver unlock.c)\n"
            "  --script FILE    entrada guionizada (ver dc/test/diff/scripts/)\n"
            "  --dump FILE      volcado del estado por frame (test diferencial)\n",
            argv0, (unsigned)ADDR_MAIN);
}

int main(int argc, char **argv)
{
    const char *gamedata = "gamedata";
    const char *hle_path = "tools/hle_funcs.txt";
    const char *script = NULL;
    const char *dump_path = NULL;
    const char *ovl_path = NULL;
    const char *view_path = NULL;
    uint32_t cone = 1u;   /* el cono de visibilidad del original */
    const char *tex_path = NULL;
    const char *counts_path = NULL;
    uint64_t win_lo = 0, win_hi = 0, ram_at_call = 0, frame_dump_at = 0;
    const char *frame_dump_path = NULL;
    const char *ram_at_path = NULL;
    uint64_t tick_limit = 10000000u;
    /* volatile: una local modificada entre setjmp y longjmp queda indeterminada,
     * y gcc avisa con razon aunque aqui se lea antes del salto */
    volatile uint32_t entry = ADDR_MAIN;
    int survey = 0, trace = 0, idlelog = 0, filelog = 0, spulog = 0, i;
    int textlog = 0, unlock = 0;
    char clima = 0;
    char path[1024];
    uint8_t *exe;
    size_t exe_len;
    uint32_t a;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gamedata") && i + 1 < argc)
            gamedata = argv[++i];
        else if (!strcmp(argv[i], "--hle") && i + 1 < argc)
            hle_path = argv[++i];
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc)
            tick_limit = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--entry") && i + 1 < argc)
            entry = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--survey"))
            survey = 1;
        else if (!strcmp(argv[i], "--idlelog"))
            idlelog = 1;
        else if (!strcmp(argv[i], "--textlog"))
            textlog = 1;
        else if (!strcmp(argv[i], "--carcone") && i + 1 < argc)
            cone = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--unlock"))
            unlock = 1;
        else if (!strcmp(argv[i], "--clima") && i + 1 < argc)
            clima = argv[++i][0];
        else if (!strcmp(argv[i], "--filelog"))
            filelog = 1;
        else if (!strcmp(argv[i], "--spulog"))
            spulog = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc)
            script = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dump_path = argv[++i];
        else if (!strcmp(argv[i], "--overlaydump") && i + 1 < argc)
            ovl_path = argv[++i];
        else if (!strcmp(argv[i], "--viewdump") && i + 1 < argc)
            view_path = argv[++i];
        else if (!strcmp(argv[i], "--texdump") && i + 1 < argc)
            tex_path = argv[++i];
        else if (!strcmp(argv[i], "--track") && i + 3 < argc) {
            track_frame = strtoull(argv[++i], NULL, 0);
            track_trk = argv[++i];
            track_file = argv[++i];
        }
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) {
            shot_frame = strtoull(argv[++i], NULL, 0);
            shot_file = argv[++i];
        }
        else if (!strcmp(argv[i], "--callcount") && i + 1 < argc)
            counts_path = argv[++i];
        else if (!strcmp(argv[i], "--ramdump") && i + 2 < argc) {
            ram_at_call = strtoull(argv[++i], NULL, 0);
            ram_at_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--framedump") && i + 2 < argc) {
            frame_dump_at = strtoull(argv[++i], NULL, 0);
            frame_dump_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--callwin") && i + 2 < argc) {
            win_lo = strtoull(argv[++i], NULL, 0);
            win_hi = strtoull(argv[++i], NULL, 0);
        }
        else if (!strcmp(argv[i], "--trace"))
            trace = 1;
        else {
            usage(argv[0]);
            return 2;
        }
    }

    /* ---- encendido: NFS4.EXE en una RAM de 2 MB limpia ---- */
    ps1_init(&cpu, ram);
    snprintf(path, sizeof(path), "%s/NFS4.EXE", gamedata);
    exe = read_file(path, &exe_len);
    if (exe == NULL) {
        fprintf(stderr, "no pude leer %s\n", path);
        return 1;
    }
    ps1_load_exe(&cpu, exe, exe_len);
    free(exe);
    /* el BSS lo pone a cero el arranque de la PS1, no el ejecutable */
    for (a = ADDR_BSS_START; a < ADDR_BSS_END; a += 4u)
        ps1_w32(&cpu, a, 0);

    cpu.dispatch = ps1_dispatch;
    cpu.dispatch_at = ps1_dispatch_at;
    /* Interrupciones habilitadas (IEc e IM2 del SR de COP0), que es como las deja la
     * BIOS al arrancar. Sin esto, service_bus_interrupts no entrega nada: lo hace
     * new_cpu() de la referencia y es facil de pasar por alto. */
    cpu.cop0[12] = 0x401u;
    /* El bus de hardware: sin el, take_irqs no se consulta nunca y ninguna
     * interrupcion de DMA llega. Es lo que hace new_cpu() de la referencia. */
    ps1_spu_init(&spu);
    spu.log = spulog;
    ps1_spu_attach(&spu, &cpu);
    cpu.trace = trace;
    cpu.tick_limit = tick_limit;
    /* el juego hace esperas activas sobre el timer: tras tantas ramas hacia atras sin
     * pasar por el host, se deja correr el tiempo como harian las interrupciones */
    cpu.spin_limit = 20000u;

    if (!host_init(&host, hle_path, gamedata)) {
        fprintf(stderr, "no pude leer la lista HLE %s\n", hle_path);
        return 1;
    }
    host.survey = survey;
    host.idlelog = idlelog;
    host.textlog = textlog;
    host.unlock = unlock;
    host.clima = clima;
    host.car_cone_width = cone != 0u ? cone : 1u;
    host.files.log = filelog;
    host.tex_buf = texbuf;
    host.poll_input = pc_on_frame;
    host.tex_cap = (uint32_t)(sizeof texbuf / sizeof texbuf[0]);
    host.win_lo = win_lo;
    host.win_hi = win_hi;
    host.ram_at_call = ram_at_call;
    host.frame_dump_at = frame_dump_at;
    host.frame_dump_path = frame_dump_path;
    host.ram_at_path = ram_at_path;
    if (script != NULL && !pad_load_script(&host, script)) {
        fprintf(stderr, "no pude cargar el guion de entrada %s\n", script);
        return 1;
    }
    if (ovl_path != NULL && !overlay_dump_open(&host, ovl_path)) {
        fprintf(stderr, "no pude abrir el volcado de la lista de dibujo %s\n", ovl_path);
        return 1;
    }
    if (view_path != NULL && !view_dump_open(&host, view_path)) {
        fprintf(stderr, "no pude abrir el volcado de la vista %s\n", view_path);
        return 1;
    }
    if (dump_path != NULL && !dump_open(&host, dump_path)) {
        fprintf(stderr, "no pude abrir el volcado %s\n", dump_path);
        return 1;
    }
    host_attach(&host, &cpu);

    printf("ps1rt: %u funciones recompiladas, %u HLE en la lista\n",
           (unsigned)ps1_range_n, host.hle_n);
    printf("entrando por %#x con un limite de %llu ticks%s\n\n", (unsigned)entry,
           (unsigned long long)tick_limit, survey ? " (modo survey)" : "");

    /* ---- marco de pila como el que monta la referencia antes de llamar ---- */
    cpu.r[29] = ADDR_STACK_TOP - 0x100u;
    cpu.r[30] = cpu.r[29];
    cpu.r[28] = ADDR_GP;
    cpu.r[31] = 0;
    cpu.stack_depth = 0;

    if (setjmp(cpu.stop_jmp) == 0) {
        cpu.stop_armed = 1;
        ps1_call(&cpu, entry);
        printf("\nla funcion de entrada volvio (v0 = %#x)\n", (unsigned)cpu.r[2]);
    } else {
        printf("\nparada pedida por el host\n");
    }

    dump_close(&host);
    overlay_dump_close(&host);
    view_dump_close(&host);
    if (counts_path != NULL && !host_call_counts(&host, counts_path))
        fprintf(stderr, "no pude escribir la tabla de llamadas %s\n", counts_path);
    if (tex_path != NULL) {
        /* Las texturas convertidas del ultimo frame, para mirarlas: este paso no tiene
         * oraculo y la unica validacion posible es ver si se parecen a lo que deberian.
         * Formato: "NFST" u32 n, y por entrada tpage, clut, u0, v0, w, h y w*h texeles
         * en ARGB1555. dc/tools/texview.py las compone en una imagen. */
        FILE *tf = fopen(tex_path, "wb");

        if (tf == NULL) {
            fprintf(stderr, "no pude abrir %s\n", tex_path);
        } else {
            uint32_t k, vivas = 0;

            for (k = 0; k < host.texcache.n; k++)
                if (host.texcache.e[k].lista)
                    vivas++;
            fwrite("NFST", 1, 4, tf);
            fwrite(&vivas, 4, 1, tf);
            for (k = 0; k < host.texcache.n; k++) {
                const TexEntry *e = &host.texcache.e[k];
                uint16_t cab[6];

                if (!e->lista)
                    continue;
                cab[0] = e->tpage; cab[1] = e->clut;
                cab[2] = e->u0;    cab[3] = e->v0;
                cab[4] = e->w;     cab[5] = e->h;
                fwrite(cab, 2, 6, tf);
                fwrite(texbuf + e->offset, 2, (size_t)e->w * e->h, tf);
            }
            fclose(tf);
            printf("texturas volcadas: %u en %s\n", (unsigned)vivas, tex_path);
        }
    }
    host_report(&host, &cpu);
    printf("\n=== bus de hardware (stub del SPU) ===\n");
    printf("  lecturas / escrituras:     %llu / %llu\n",
           (unsigned long long)spu.reads, (unsigned long long)spu.writes);
    printf("  transferencias de DMA4:    %llu (%llu bytes)\n",
           (unsigned long long)spu.dma_transfers, (unsigned long long)spu.dma_bytes);
    printf("  interrupciones levantadas: %llu\n",
           (unsigned long long)spu.irqs_raised);
    return 0;
}
