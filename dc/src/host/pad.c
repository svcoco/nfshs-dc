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
#include "host.h"

#include <stdio.h>
#include <string.h>

/* Lectura del guion de entrada: solo para el test diferencial. PAD_NAMES, name_bit
 * y parse_buttons no los usa nadie mas, asi que entran en el mismo guardado. */
#if PS1_DIAG

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
#endif /* PS1_DIAG */

/* Frontera de frame del juego: se cuenta, se aplica la entrada y se vuelca el estado.
 * La referencia hace estas tres cosas en su frame_top, y por eso aqui tambien. */
void host_frame_top(GameHost *h, Cpu *c)
{
    h->frames++;
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
#if PS1_DIAG
    dump_frame(h, c);
#endif
    pad_write(h, c);
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
