#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_addr(const void *a, const void *b)
{
    uint32_t x = ((const HleFunc *)a)->addr;
    uint32_t y = ((const HleFunc *)b)->addr;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/* Mete una entrada si la direccion no estaba ya. La lista tiene direcciones repetidas
 * (130 lineas, 119 unicas) y se queda la PRIMERA, en las dos vias por igual: si una se
 * quedara con la ultima, los nombres del informe no coincidirian entre PC y DC. */
static int hle_add(GameHost *h, uint32_t addr, const char *name)
{
    uint32_t i;

    for (i = 0; i < h->hle_n; i++)
        if (h->hle[i].addr == addr)
            return 1;                      /* duplicada: no es un error */
    if (h->hle_n >= HOST_HLE_MAX)
        return 0;
    h->hle[h->hle_n].addr = addr;
    snprintf(h->hle[h->hle_n].name, sizeof(h->hle[h->hle_n].name), "%s",
             (name != NULL && name[0] != '\0') ? name : "(sin nombre)");
    h->hle_n++;
    return 1;
}

/* Lee tools/hle_funcs.txt. Es la via del host de PC, que tiene el archivo al lado. */
static int hle_load_file(GameHost *h, const char *path)
{
    FILE *fh = fopen(path, "r");
    char line[256];

    if (fh == NULL)
        return 0;
    while (fgets(line, (int)sizeof(line), fh) != NULL) {
        unsigned long addr;
        char name[48];
        char *p = line;

        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;
        name[0] = '\0';
        if (sscanf(p, "%lx %47[^\r\n]", &addr, name) < 1)
            continue;
        if (!hle_add(h, (uint32_t)addr, name)) {
            fclose(fh);
            return 0;
        }
    }
    fclose(fh);
    return 1;
}

/* La tabla horneada en dc/gen/hle_table.c. Es la via de Dreamcast: mismo archivo de
 * origen, pero resuelto al compilar, sin llevar un .txt en el disco ni parsearlo. */
static int hle_load_baked(GameHost *h)
{
    uint32_t i;

    for (i = 0; i < host_hle_baked_n; i++)
        if (!hle_add(h, host_hle_baked[i].addr, host_hle_baked[i].name))
            return 0;
    return 1;
}

int host_init(GameHost *h, const char *hle_funcs_path, const char *gamedata)
{
    memset(h, 0, sizeof(*h));
    h->gamedata = gamedata;
    h->next_event = 0xF0000001u;   /* como en la referencia */
    files_init(&h->files);

    if (hle_funcs_path != NULL) {
        if (!hle_load_file(h, hle_funcs_path))
            return 0;
    } else if (!hle_load_baked(h)) {
        return 0;
    }

    qsort(h->hle, h->hle_n, sizeof(h->hle[0]), cmp_addr);
    return h->hle_n > 0;
}

static HleFunc *find_hle(GameHost *h, uint32_t addr)
{
    uint32_t lo = 0, hi = h->hle_n;
    while (lo < hi) {
        uint32_t m = lo + ((hi - lo) >> 1);
        if (h->hle[m].addr < addr)
            lo = m + 1u;
        else if (h->hle[m].addr > addr)
            hi = m;
        else
            return &h->hle[m];
    }
    return NULL;
}

/* Las funciones HLE no estan recompiladas, asi que el codigo generado las llama a
 * traves de ps1_call y aterrizan aqui. Si devolvemos 0 ("no atendida"), ps1_call
 * seguiria hasta el interprete y pararia: por eso una HLE sin implementar se trata
 * aqui mismo, con un diagnostico que dice por su nombre que falta. */
/* Cuenta la llamada por direccion. Busqueda lineal: son unos cientos de direcciones
 * distintas y esto solo corre una vez por llamada al host. */
static void note_seen(GameHost *h, const Cpu *c, uint32_t addr)
{
    uint32_t i;
    for (i = 0; i < h->seen_addrs; i++)
        if (h->seen[i].addr == addr) {
            h->seen[i].n++;
            return;
        }
    if (h->seen_addrs < HOST_SEEN_MAX) {
        h->seen[h->seen_addrs].addr = addr;
        h->seen[h->seen_addrs].n = 1u;
        h->seen[h->seen_addrs].first = c->calls;   /* orden de aparicion */
        h->seen_addrs++;
    }
}

static int host_call(Cpu *c, void *self, uint32_t addr)
{
    GameHost *h = (GameHost *)self;
    HleFunc *f = find_hle(h, addr);
    uint32_t ret = 0;

    note_seen(h, c, addr);
#if PS1_DIAG
    if (h->ram_at_call != 0u && !h->ram_dumped && c->calls >= h->ram_at_call) {
        h->ram_dumped = 1;
        if (dump_ram(c, h->ram_at_path))
            printf("RAM volcada en la llamada %llu -> %s\n",
                   (unsigned long long)c->calls, h->ram_at_path);
    }
#endif
    if (h->win_lo != 0u && c->calls >= h->win_lo && c->calls <= h->win_hi)
        printf("CALL %llu %#x ra %#x sp %#x ticks %llu prof %u\n",
               (unsigned long long)c->calls,
               (unsigned)addr, (unsigned)c->r[31], (unsigned)c->r[29],
               (unsigned long long)c->ticks, (unsigned)c->stack_depth);
    int handled;

    if (f == NULL) {
        /* No esta en la lista HLE: es una funcion recompilada. Puede ser un hook, y
         * entonces el host produce su efecto lateral y deja correr el original.
         *
         * Las vistas se consultan primero porque algunas **si** cortan el original: en
         * el front end el host dibuja el texto y Font_TextXY no debe ejecutarse. */
        if (host_showroom_call(h, c, addr, &ret)) {
            c->r[2] = ret;
            return 1;
        }
        if (host_views_call(h, c, addr, &ret)) {
            c->r[2] = ret;
            return 1;
        }
        host_hook(h, c, addr);
        h->unknown_calls++;
        h->unknown_last = addr;
        return 0;
    }

    /* el orden importa: el modulo de memory card gana al stub generico de
     * system.c, igual que en la referencia (donde ese stub es codigo muerto) */
    handled = host_memcard_call(h, c, addr, &ret);
    if (!handled)
        handled = host_system_call(h, c, addr, &ret);
    if (!handled)
        handled = host_gpu_call(h, c, addr, &ret);
    if (!handled)
        handled = host_file_call(h, c, addr, &ret);
    if (!handled)
        handled = host_game_call(h, c, addr, &ret);
    if (!handled)
        handled = host_video_call(h, c, addr, &ret);

    if (f->calls == 0) {
        h->seen_n++;
        f->first = h->seen_n;
        f->implemented = handled;
        if (!handled) {
            fprintf(stderr, "\n--- primera llamada a una HLE sin implementar ---\n");
            fprintf(stderr, "  %s  (%#x)\n", f->name, (unsigned)addr);
            fprintf(stderr, "  a0=%#x a1=%#x a2=%#x a3=%#x  ra=%#x\n",
                    (unsigned)c->r[4], (unsigned)c->r[5], (unsigned)c->r[6],
                    (unsigned)c->r[7], (unsigned)c->r[31]);
            if (!h->survey) {
                /* Parada ordenada: el siguiente tick sale por el longjmp del arnes,
                 * y asi se imprime el informe en vez de morir aqui. */
                c->tick_limit = 0;
            }
        }
    }
    f->calls++;

    /* Sin implementar: v0 = 0, igual que el caso por defecto de la referencia. */
    c->r[2] = ret;
    return 1;
}

static void host_idle(Cpu *c, void *self)
{
    GameHost *h = (GameHost *)self;
    /* espera activa sobre el timer: se deja pasar un tick */
    host_advance_to(h, c, (h->clock / HOST_TICK + 1u) * HOST_TICK);
    h->idles++;
    /* Diagnostico: en los primeros idles, donde esta girando el juego. */
    if (h->idlelog && (h->idles <= 3u || h->idles == 50u)) {
        uint32_t d = c->stack_depth;
        uint32_t shown = d < 8u ? d : 8u;
        uint32_t k;
        fprintf(stderr, "idle %llu: pila (%u)", (unsigned long long)h->idles,
                (unsigned)d);
        for (k = 0; k < shown; k++)
            fprintf(stderr, " %#x", (unsigned)c->stack[d - 1u - k]);
        fprintf(stderr, "  ra=%#x\n", (unsigned)c->r[31]);
    }
}

void host_attach(GameHost *h, Cpu *c)
{
    h->iface.call = host_call;
    h->iface.idle = host_idle;
    h->iface.self = h;
    c->host = &h->iface;
}

static int cmp_first(const void *a, const void *b)
{
    uint32_t x = ((const HleFunc *)a)->first;
    uint32_t y = ((const HleFunc *)b)->first;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

#if PS1_DIAG
int host_call_counts(GameHost *h, const char *path)
{
    FILE *f = fopen(path, "w");
    uint32_t i;
    if (f == NULL)
        return 0;
    /* TODAS las direcciones que vio el host, ordenadas por direccion, igual que la
     * tabla de la referencia: asi se pueden diferenciar linea a linea. */
    {
        uint32_t j, best;
        for (i = 0; i < h->seen_addrs; i++) {
            best = i;
            for (j = i + 1u; j < h->seen_addrs; j++)
                if (h->seen[j].addr < h->seen[best].addr)
                    best = j;
            if (best != i) {
                uint32_t ta = h->seen[i].addr;
                uint64_t tn = h->seen[i].n, tf = h->seen[i].first;
                h->seen[i] = h->seen[best];
                h->seen[best].addr = ta;
                h->seen[best].n = tn;
                h->seen[best].first = tf;
            }
        }
        for (i = 0; i < h->seen_addrs; i++)
            fprintf(f, "%#010x %llu %llu\n", (unsigned)h->seen[i].addr,
                    (unsigned long long)h->seen[i].n,
                    (unsigned long long)h->seen[i].first);
    }
    fclose(f);
    return 1;
}
#endif /* PS1_DIAG */

void host_report(const GameHost *h, const Cpu *c)
{
    HleFunc *sorted;
    uint32_t i, n = 0;

    printf("\n=== lo que el juego le pidio al host ===\n");
    /* los casts no son decorado: en SH-4 (newlib) uint32_t es `long unsigned int`,
     * no `unsigned int`, asi que %u sin cast avisa al cruzar a Dreamcast */
    printf("HLE en la lista: %u    distintas llamadas: %u\n",
           (unsigned)h->hle_n, (unsigned)h->seen_n);

    sorted = (HleFunc *)malloc(sizeof(HleFunc) * (h->hle_n ? h->hle_n : 1u));
    if (sorted == NULL)
        return;
    for (i = 0; i < h->hle_n; i++)
        if (h->hle[i].first != 0)
            sorted[n++] = h->hle[i];
    qsort(sorted, n, sizeof(sorted[0]), cmp_first);

    printf("\n  %-4s %-10s %-34s %-10s %s\n", "n", "direccion", "funcion",
           "llamadas", "estado");
    for (i = 0; i < n; i++)
        printf("  %-4u %#010x %-34s %-10llu %s\n", (unsigned)sorted[i].first,
               (unsigned)sorted[i].addr, sorted[i].name,
               (unsigned long long)sorted[i].calls,
               sorted[i].implemented ? "ok" : "FALTA");
    free(sorted);

    printf("\n=== contadores de ps1rt ===\n");
    printf("  ramas hacia atras (ticks): %llu\n", (unsigned long long)c->ticks);
    printf("  llamadas por direccion:    %llu\n", (unsigned long long)c->calls);
    printf("  al interprete:             %llu\n", (unsigned long long)c->interpreted);
    {
        uint32_t k;
        for (k = 0; k < PS1_INTERP_TOP && ps1_interp_addr[k] != 0u; k++)
            printf("      %#010x  %llu veces\n", (unsigned)ps1_interp_addr[k],
                   (unsigned long long)ps1_interp_hits[k]);
    }
    printf("  comandos del GTE:          %llu\n", (unsigned long long)c->gte_commands);
    printf("  BIOS sin implementar:      %llu\n", (unsigned long long)c->bios_unhandled);
    printf("  accesos a IO:              %llu (ultimo %#x)\n",
           (unsigned long long)c->io_accesses, (unsigned)c->last_io);
    printf("  accesos desalineados:      %llu\n", (unsigned long long)c->misaligned);
    printf("  en el borde de la RAM:     %llu\n", (unsigned long long)c->ram_edge);
    printf("  idles del host:            %llu\n", (unsigned long long)h->idles);
    printf("  llamadas fuera de la HLE:  %llu (ultima %#x)\n",
           (unsigned long long)h->unknown_calls, (unsigned)h->unknown_last);
    printf("  reloj virtual:             %llu (%.2f s de juego)\n",
           (unsigned long long)h->clock, (double)h->clock / (double)HOST_HZ);
    printf("  manejadores de timer:      %u\n", (unsigned)c->timer_handler_n);
    printf("  campos de video / frames:  %llu / %llu\n",
           (unsigned long long)h->fields, (unsigned long long)h->frames);
    printf("  modo:                      %s\n",
           h->mode == HOST_RACE ? "carrera" : "front end");
    if (h->race_views != 0u)
        printf("  coches por vista:          media %.2f, maximo %u%s\n",
               (double)h->cars_sum / (double)h->race_views,
               (unsigned)h->cars_max,
               h->cars_lost != 0u ? " (EL BUFFER SE LLENO)" : "");
    if (h->race_views != 0u)
        printf("  objetos por vista:         media %.1f, maximo %u%s\n",
               (double)h->objects_sum / (double)h->race_views,
               (unsigned)h->objects_max,
               h->objects_lost != 0u ? " (LA TABLA SE LLENO)" : "");
    if (h->race_views != 0u)
        printf("  trozos de pista por vista: media %.1f, maximo %u (%llu vistas)\n",
               (double)h->race_chunks_sum / (double)h->race_views,
               (unsigned)h->race_chunks_max,
               (unsigned long long)h->race_views);
    printf("  Font_TextXY atendidas:     %llu\n",
           (unsigned long long)h->views_fonttext);
    printf("  dibujado capturado:        %llu (en marco anidado)\n",
           (unsigned long long)h->views_captured);
    printf("  showroom coche / suelo:    %llu / %llu\n",
           (unsigned long long)h->showroom_cars,
           (unsigned long long)h->showroom_floors);
    printf("  desbloqueos:               %llu%s\n",
           (unsigned long long)h->unlocks, h->unlock ? "" : " (--unlock apagado)");
    if (h->views_hud_unported != 0u)
        printf("  AVISO ayudantes del HUD sin portar: %llu (la RAM divergira)\n",
               (unsigned long long)h->views_hud_unported);
    printf("  escrituras de mando:       %llu\n", (unsigned long long)h->pad_writes);
    printf("  MemCardSync entregados:    %llu (escrituras rechazadas: %llu)\n",
           (unsigned long long)h->mc_syncs, (unsigned long long)h->mc_writes_refused);
    printf("\n=== capa de archivos ===\n");
    printf("  aperturas ok / fallidas:   %llu / %llu\n",
           (unsigned long long)h->files.opens, (unsigned long long)h->files.opens_failed);
    printf("  lecturas:                  %llu (%llu bytes)\n",
           (unsigned long long)h->files.reads, (unsigned long long)h->files.bytes_read);
    printf("  BIGF montados / fallidos:  %llu / %llu\n",
           (unsigned long long)h->files.mounts_ok, (unsigned long long)h->files.mounts_failed);
    if (h->files.last_missing[0] != '\0')
        printf("  ultimo que no estaba:      %s\n", h->files.last_missing);
    printf("\n=== graficos ===\n");
    printf("  escrituras / lecturas VRAM: %llu / %llu\n",
           (unsigned long long)h->vram_writes, (unsigned long long)h->vram_reads);
    printf("  listas de dibujo (DrawOTag): %llu\n", (unsigned long long)h->otags);
    printf("  formas 2D del front end:     %llu  cuadros: %llu\n",
           (unsigned long long)h->fe_shapes, (unsigned long long)h->fe_squares);
    printf("  vistas 3D pedidas:           %llu\n", (unsigned long long)h->player_views);
    /* M2a: lo que el juego quiere PINTAR, ya grabado. Las combinaciones distintas
     * de pagina de textura y paleta son lo que dimensiona la cache del PVR. */
    printf("  cuadros grabados:            %llu (maximo por frame: %u)\n",
           (unsigned long long)h->overlay.total, (unsigned)h->overlay_max);
    printf("  combinaciones pagina+paleta: %u en total, %u a la vez en un frame%s\n",
           (unsigned)h->overlay.combos_n, (unsigned)h->overlay.frame_combos_max,
           h->overlay.combos_lost ? " (la tabla se lleno)" : "");
    printf("  texturas: %u entradas, %u texeles vivos (pico %u de %u)\n",
           (unsigned)h->texcache.n, (unsigned)h->texcache.texels,
           (unsigned)h->texcache.pico_texels, (unsigned)h->tex_cap);
    printf("  conversiones: %llu (%llu texeles), invalidadas %llu, expulsadas %llu,\n"
           "                vaciados %llu, sin sitio %llu\n",
           (unsigned long long)h->texcache.conversiones,
           (unsigned long long)h->texcache.texels_conv,
           (unsigned long long)h->texcache.invalidadas,
           (unsigned long long)h->texcache.expulsadas,
           (unsigned long long)h->texcache.vaciados,
           (unsigned long long)h->texcache.sin_sitio);
    if (h->overlay.dropped || h->overlay_unknown)
        printf("  AVISO: %llu cuadros no cupieron, %llu primitivas sin grabar\n",
               (unsigned long long)h->overlay.dropped,
               (unsigned long long)h->overlay_unknown);
    printf("  llamadas de video:           %llu\n", (unsigned long long)h->video_calls);
    printf("  flips (PutDispEnv):          %llu\n", (unsigned long long)h->flips);
    printf("  area de pantalla:            %u,%u %ux%u\n",
           (unsigned)h->display[0], (unsigned)h->display[1],
           (unsigned)h->display[2], (unsigned)h->display[3]);
    printf("  videos pedidos:              %llu (ultimo %u)\n",
           (unsigned long long)h->movies_played, (unsigned)h->last_movie);

    /* La pila de funciones recompiladas en curso: cuando el juego se queda girando,
     * esto dice en que funcion esta, que es el dato que hace falta para saber que le
     * falta al host. */
    {
        uint32_t depth = c->stack_depth;
        uint32_t shown = depth < PS1_STACK_MAX ? depth : PS1_STACK_MAX;
        uint32_t k;
        /* Las ultimas continuaciones: si el juego gira cayendo por el final de unas
     * funciones, aqui se ve el ciclo exacto. */
    if (c->cont_runs != 0u) {
        uint32_t shown = c->cont_at < 32u ? c->cont_at : 32u;
        uint32_t k;
        printf("\n=== continuaciones (%llu en total, ultimas %u) ===\n",
               (unsigned long long)c->cont_runs, (unsigned)shown);
        printf("  ");
        for (k = 0; k < shown; k++)
            printf("%#x ", (unsigned)c->cont_recent[(c->cont_at - shown + k) % 32u]);
        printf("\n");
    }

    printf("\n=== pila del juego (profundidad %u) ===\n", (unsigned)depth);
        if (shown == 0u) {
            printf("  (vacia: no estaba dentro de ninguna funcion recompilada)\n");
        } else {
            printf("  ");
            for (k = shown; k > 0u && k + 20u > shown; k--)
                printf("%#x%s", (unsigned)c->stack[k - 1u], (k > 1u) ? " <- " : "");
            printf("\n");
        }
    }
}
