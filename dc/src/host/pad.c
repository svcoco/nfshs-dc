/* Los mandos, con entrada guionizada.
 *
 * libpad deja en RAM del invitado dos buffers (uno por puerto) que el host rellena en
 * cada frame; el juego los lee directamente. Formato de cada buffer, tomado del driver
 * de frames de la referencia:
 *
 *   +0  estado: 0x00 = correcto, 0xFF = sin mando
 *   +1  tipo:   0x41 = mando digital, 0x73 = DualShock en modo analogico
 *   +2  botones, 16 bits, **con logica invertida** (un bit a 0 = pulsado)
 *   +4  los cuatro ejes analogicos, solo en modo analogico
 *
 * Mientras el host no escriba nada, `PadInitDirect` deja 0xFF ahi, o sea "sin mando
 * conectado", y el front end se queda esperando a que aparezca uno.
 *
 * El guion es lo que hara reproducible el test diferencial de M1a: la misma secuencia
 * de pulsaciones por numero de campo, en los dos lados.
 */
#include "addr.h"
#include "host.h"

#include <stdio.h>
#include <string.h>

/* Lectura del guion de entrada. NO va bajo PS1_DIAG: solo LEE un archivo, y en
 * Dreamcast es lo que permite llevar al juego hasta una carrera sin manos, que es
 * como se prueba que la logica de carrera avanza. PS1_DIAG cubre lo que ESCRIBE
 * archivos (el volcado por frame y la tabla de llamadas), que en un GD-ROM no cabe. */

/* Bits del mando digital de PS1, en el orden del hardware. */
static const struct {
    const char *name;
    uint16_t bit;
} PAD_NAMES[] = {
    { "SELECT",   1u << 0 },  { "L3",       1u << 1 },  { "R3",     1u << 2 },
    { "START",    1u << 3 },  { "UP",       1u << 4 },  { "RIGHT",  1u << 5 },
    { "DOWN",     1u << 6 },  { "LEFT",     1u << 7 },  { "L2",     1u << 8 },
    { "R2",       1u << 9 },  { "L1",       1u << 10 }, { "R1",     1u << 11 },
    { "TRIANGLE", 1u << 12 }, { "CIRCLE",   1u << 13 }, { "CROSS",  1u << 14 },
    { "SQUARE",   1u << 15 },
};

static int name_bit(const char *tok, uint16_t *bit)
{
    size_t i;
    for (i = 0; i < sizeof(PAD_NAMES) / sizeof(PAD_NAMES[0]); i++) {
        const char *a = PAD_NAMES[i].name, *b = tok;
        for (; *a != '\0' && *b != '\0'; a++, b++) {
            char y = *b;
            if (y >= 'a' && y <= 'z')
                y = (char)(y - 'a' + 'A');
            if (*a != y)
                break;
        }
        if (*a == '\0' && (*b == '\0')) {
            *bit = PAD_NAMES[i].bit;
            return 1;
        }
    }
    return 0;
}

/* Una linea de botones: "-" o "NONE" para ninguno, "0x1234" en hexadecimal, o nombres
 * separados por comas o '+'. */
static int parse_buttons(const char *s, uint16_t *out)
{
    char tok[24];
    size_t n = 0;
    uint16_t acc = 0;

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-' || *s == '\0' || *s == '#')
        { *out = 0; return 1; }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        unsigned v = 0;
        if (sscanf(s, "%x", &v) != 1)
            return 0;
        *out = (uint16_t)v;
        return 1;
    }
    for (;;) {
        char ch = *s;
        if (ch == ',' || ch == '+' || ch == ' ' || ch == '\t' || ch == '\0'
            || ch == '\r' || ch == '\n' || ch == '#') {
            if (n > 0u) {
                uint16_t bit;
                tok[n] = '\0';
                if (!name_bit(tok, &bit))
                    return 0;
                acc |= bit;
                n = 0;
            }
            if (ch == '\0' || ch == '\r' || ch == '\n' || ch == '#')
                break;
        } else if (n + 1u < sizeof(tok)) {
            tok[n++] = ch;
        }
        s++;
    }
    *out = acc;
    return 1;
}

/* Formato del guion: lineas "<campo> <botones>", y "#end <campo>" para parar.
 * Los botones se mantienen hasta la linea siguiente. */
int pad_load_script(GameHost *h, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    if (f == NULL)
        return 0;
    h->pad_steps_n = 0;
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        unsigned long frame;
        char rest[200];
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, "#end", 4) == 0) {
            unsigned long e = 0;
            if (sscanf(p + 4, "%lu", &e) == 1)
                h->pad_end = (uint64_t)e;
            continue;
        }
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;
        rest[0] = '\0';
        if (sscanf(p, "%lu %199[^\r\n]", &frame, rest) < 1)
            continue;
        if (h->pad_steps_n >= PAD_SCRIPT_MAX) {
            fclose(f);
            return 0;
        }
        h->pad_steps[h->pad_steps_n].frame = (uint64_t)frame;
        if (!parse_buttons(rest, &h->pad_steps[h->pad_steps_n].buttons)) {
            fprintf(stderr, "guion del mando: no entiendo \"%s\"\n", rest);
            fclose(f);
            return 0;
        }
        h->pad_steps_n++;
    }
    fclose(f);
    return 1;
}
/* En pausa las camaras se quedan quietas. Camera_Update del original corre en cada vuelta
 * del bucle, en pausa o no: sus camaras de persecucion seguirian acercandose a su sitio
 * detras de los coches parados, sin verse bajo la imagen copiada, y saltarian alli al
 * reanudar. Cada frame en pausa devuelve Camera_gInfo a como lo encontro la pausa (salvo
 * que cambie el modo de una camara). Es hold_cameras de la referencia, y escribe en la RAM:
 * sin esto el test diferencial diverge al pausar. */
static void hold_cameras(GameHost *h, Cpu *c)
{
    uint32_t n = (uint32_t)sizeof h->cams_held, i;
    uint16_t m0, m1;

    if (!h->paused) {
        h->cams_held_ok = 0;
        return;
    }
    if (!h->cams_held_ok) {
        for (i = 0; i < n; i++)
            h->cams_held[i] = (uint8_t)ps1_r8(c, ADDR_CAMERA_INFO + i);
        h->cams_held_ok = 1;
        return;
    }
    m0 = (uint16_t)(h->cams_held[0x70] | (h->cams_held[0x71] << 8));
    m1 = (uint16_t)(h->cams_held[0x110 + 0x70] | (h->cams_held[0x110 + 0x71] << 8));
    if (m0 != (uint16_t)ps1_r16(c, ADDR_CAMERA_INFO + 0x70u) ||
        m1 != (uint16_t)ps1_r16(c, ADDR_CAMERA_INFO + 0x110u + 0x70u)) {
        for (i = 0; i < n; i++)
            h->cams_held[i] = (uint8_t)ps1_r8(c, ADDR_CAMERA_INFO + i);
        return;
    }
    for (i = 0; i < n; i++)
        ps1_w8(c, ADDR_CAMERA_INFO + i, h->cams_held[i]);
}

/* Lo que se ve en pausa (solo para dibujar; los volcados ya estan escritos). El original
 * muestra la imagen que copio al pausar y encima el menu, que solo redibuja cuando cambia:
 * aqui son las vistas y el HUD del ultimo frame jugado, y el ultimo menu. Como la
 * referencia (race.rs), un frame cuyo 2D no tiene mas que rectangulos no cambia el menu. */
static void pausa_presenta(GameHost *h)
{
    Overlay *o = &h->overlay;
    uint32_t i, k;
    int menu_nuevo = 0;

    if (!h->paused) {
        if (h->mode == HOST_RACE && h->views_n > 0u) {
            /* el ultimo frame jugado: sus vistas y su HUD */
            memcpy(h->views_kept, h->view, h->views_n * sizeof h->view[0]);
            h->views_kept_n = h->views_n;
            k = o->n < HUD_KEPT_MAX ? o->n : HUD_KEPT_MAX;
            h->hud_kept_lost += o->n - k;
            memcpy(h->hud_kept, o->quad, k * sizeof o->quad[0]);
            h->hud_kept_n = k;
        } else if (h->mode == HOST_RACE && h->views_n == 0u && o->n == 0u &&
                   h->views_kept_n > 0u) {
            /* un frame que no pinta nada (los primeros al cerrar el menu de pausa): se ve el
             * ultimo dibujado, como `held` en la referencia; si no, saldria en negro */
            memcpy(h->view, h->views_kept, h->views_kept_n * sizeof h->view[0]);
            h->views_n = h->views_kept_n;
            memcpy(o->quad, h->hud_kept, h->hud_kept_n * sizeof o->quad[0]);
            o->n = h->hud_kept_n;
        }
        h->menu_kept_n = 0;
        return;
    }
    for (i = 0; i < h->hud_cmds_n; i++)
        if (h->hud_cmd[i].kind != HUD_RECT)
            menu_nuevo = 1;
    if (menu_nuevo) {
        k = o->n < MENU_KEPT_MAX ? o->n : MENU_KEPT_MAX;
        h->hud_kept_lost += o->n - k;
        memcpy(h->menu_kept, o->quad, k * sizeof o->quad[0]);
        h->menu_kept_n = k;
    }
    /* la lista se dibuja del final al principio: primero el HUD conservado, encima el menu */
    memcpy(o->quad, h->menu_kept, h->menu_kept_n * sizeof o->quad[0]);
    k = h->hud_kept_n;
    if (h->menu_kept_n + k > OVERLAY_MAX)
        k = OVERLAY_MAX - h->menu_kept_n;
    memcpy(&o->quad[h->menu_kept_n], h->hud_kept, k * sizeof o->quad[0]);
    o->n = h->menu_kept_n + k;
    /* y el mundo tal como se quedo */
    if (h->views_n == 0u && h->views_kept_n > 0u) {
        memcpy(h->view, h->views_kept, h->views_kept_n * sizeof h->view[0]);
        h->views_n = h->views_kept_n;
    }
}

/* Frontera de frame del juego: se cuenta, se aplica la entrada y se vuelca el estado.
 * La referencia hace estas tres cosas en su frame_top, y por eso aqui tambien. */
void host_frame_top(GameHost *h, Cpu *c)
{
    h->frames++;
    /* El HUD de carrera, en el mismo punto que la referencia (al empezar frame_top): de
     * las tablas grabadas en el frame se tira la del mundo y la lista se da la vuelta. */
    h->otags_frame = 0;
    h->paused = h->mode == HOST_RACE && ps1_r32(c, SIM_PAUSED) != 0u;
    if (h->mode == HOST_RACE && !h->paused) {
        overlay_race_compose(h);
    } else if (h->hud_lists > 0u) {
        /* Tablas de carrera grabadas en un frame que se cierra ya en los menus (el ultimo de
         * la carrera): la referencia las guarda aparte y solo las pasa a la lista al componer
         * en carrera, asi que no entran. Aqui ya estaban en la lista: se quitan. */
        Overlay *o = &h->overlay;
        uint32_t d = h->hud_start[0], e = h->hud_end;

        if (d < e && e <= o->n) {
            memmove(&o->quad[d], &o->quad[e], (size_t)(o->n - e) * sizeof o->quad[0]);
            o->n -= e - d;
        }
        h->hud_lists = 0;
    }
    hold_cameras(h, c);
    /* el flare del sol: en pausa, si el juego no lo pinta, el del ultimo frame (como la
     * referencia, que lo vuelca asi) */
    if (h->paused && h->sky_quads_n == 0u) {
        memcpy(h->sky_quad, h->sky_kept, h->sky_kept_n * sizeof h->sky_quad[0]);
        h->sky_quads_n = h->sky_kept_n;
    } else {
        memcpy(h->sky_kept, h->sky_quad, h->sky_quads_n * sizeof h->sky_quad[0]);
        h->sky_kept_n = h->sky_quads_n;
    }
    /* en el mismo punto que la referencia (frame.rs, antes de su volcado): cargar una
     * partida vuelve a fijar las banderas, asi que se ponen en cada frame */
    if (h->mode == HOST_FRONTEND)
        host_unlock_everything(h, c);
    if (h->idlelog && h->frames <= 5u)
        fprintf(stderr, "frame_top %llu: ticks=%llu llamadas=%llu interp=%llu reloj=%llu idles=%llu campo=%llu\n",
                (unsigned long long)h->frames, (unsigned long long)c->ticks,
                (unsigned long long)c->calls, (unsigned long long)c->interpreted,
                (unsigned long long)h->clock, (unsigned long long)h->idles,
                (unsigned long long)h->fields);
    /* El orden importa y es este: la referencia vuelca el estado **antes** de escribir
     * los buffers del pad (el volcado ocurre dentro de su callback on_frame, y los pads
     * se escriben despues, en frame.rs). Al hacerlo al reves, el volcado del port
     * incluia los botones de este frame y el de la referencia los del anterior, y en el
     * frame 1 salian dos paginas distintas (los dos buffers del pad) aunque la RAM fuera
     * identica en la frontera de la llamada. Era un fallo del instrumento, no del port. */
    /* Los frames del juego van atados a los campos de video de 60 Hz: si el reloj no
     * esta justo en un campo, o no se ha movido desde la frontera anterior, se avanza
     * al siguiente campo. Es lo que hace frame_top en frame.rs de la referencia.
     *
     * Sin esto la CARRERA SE CONGELA, y costo encontrarlo: en el front end la frontera
     * de frame es el flip (PutDispEnv), que mueve el tiempo por otro camino, asi que
     * alli no se nota. En carrera la frontera es systemtask en el tope del bucle
     * principal, que no mueve el reloj; los campos se quedaban clavados, el root
     * counter de 128 Hz no latia y la fisica no avanzaba: 0 paginas de RAM cambiaban
     * y el juego gastaba 2 ticks por frame contando frames para siempre. */
    {
        uint64_t t = h->clock;
        if (h->clock % HOST_FIELD != 0u || h->clock == h->last_top)
            t = (h->clock / HOST_FIELD + 1u) * HOST_FIELD;
        /* Con reloj real, ademas, hasta el ultimo campo que ya paso de verdad: si el frame
         * tardo dos campos, el juego ve dos (asi va en la PS1 a 30 fps).
         *
         * Con un TOPE de HOST_REAL_MAX_FIELDS por frame. En la PS1 las interrupciones siguen
         * llegando durante una carga del CD; aqui una carga (o una pelicula) es una llamada al
         * host que no deja correr nada, y el tiempo que dura llegaria de golpe a la siguiente
         * frontera: medido, 626 ticks tras cargar la carrera, y el driver de sonido se queda
         * dentro de su interrupcion esperando 5 s de musica que nadie ha leido del CD. Lo que
         * pasa del tope se descarta (el ancla se mueve): por debajo de 15 fps el juego va
         * mas lento, no a saltos mayores. */
        if (h->real_clock && h->now_us != NULL) {
            uint64_t real = host_real_clock(h) / HOST_FIELD * HOST_FIELD, tope;
            tope = (h->clock / HOST_FIELD + HOST_REAL_MAX_FIELDS) * HOST_FIELD;
            if (real > tope) {
                h->real_dropped += real - tope;
                real = tope;
                h->real_us0 = h->now_us();
                h->real_clock0 = tope;
            }
            if (real > t)
                t = real;
        }
        host_advance_to(h, c, t);
    }
    h->last_top = h->clock;
    /* Las marcas de neumatico, de la RAM en este punto, como las lee la referencia al volcar
     * (read_skids en cada frame). Leerlas al preparar la vista dejaba las del frame anterior
     * cuando el juego las borra: al recolocar el coche y al terminar la carrera. */
    host_read_skids(h, c);

#if PS1_DIAG
    dump_frame(h, c);
#endif
    if (h->poll_input != NULL)
        h->poll_input(h);
    pad_write(h, c);
    /* La lista de dibujo es de UN frame: aqui se cierra la del frame que termina (de
     * momento solo se mide; dibujarla es el paso 4 de M2a) y se empieza la siguiente. */
    if (h->overlay.n > h->overlay_max)
        h->overlay_max = h->overlay.n;
#if PS1_DIAG
    overlay_dump_frame(h);
    view_dump_frame(h);
#endif
    pausa_presenta(h);
    {
        uint64_t t0 = h->now_us ? h->now_us() : 0u, t1;

        host_build_textures(h);
        t1 = h->now_us ? h->now_us() : 0u;
        if (h->on_draw != NULL)
            h->on_draw(h);
        if (h->now_us) {
            h->tex_us += t1 - t0;
            h->draw_us += h->now_us() - t1;
        }
    }
    overlay_reset(&h->overlay);
    h->hud_cmds_n = 0;
    h->sroom_n = 0;
    h->sroom_floor_n = 0;
    /* Las vistas de carrera tambien son de UN frame: el renderer ya las ha consumido
     * en on_draw. */
    h->views_n = 0;
    host_glows_reset(h);
    /* fin del guion: parada ordenada, para que el arnes imprima su informe */
    if (h->pad_end != 0u && h->frames >= h->pad_end)
        c->tick_limit = 0;
}

void pad_write(GameHost *h, Cpu *c)
{
    uint32_t port;

    /* avanza por el guion hasta el paso que corresponde a este frame */
    while (h->pad_at < h->pad_steps_n && h->pad_steps[h->pad_at].frame <= h->frames) {
        h->pad_buttons[0] = h->pad_steps[h->pad_at].buttons;
        h->pad_at++;
    }

    for (port = 0; port < 2u; port++) {
        uint32_t b = h->pad_buffers[port];
        if (b == 0u)
            continue;
        ps1_w8(c, b, 0x00u);                       /* estado correcto */
        ps1_w8(c, b + 1u, 0x41u);                  /* mando digital */
        /* los botones van invertidos: 0xFFFF = ninguno pulsado */
        ps1_w16(c, b + 2u, (uint32_t)(uint16_t)~h->pad_buttons[port]);
    }
    h->pad_writes++;
}
