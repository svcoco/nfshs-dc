/* Reemplazos de alto nivel de las llamadas al BIOS que usa el juego.
 *
 * Portado de crates/ps1rt/src/bios.rs. Las llamadas se hacen como `jal 0xA0` con el
 * numero de funcion en t1 (r9), y el resultado va a v0 (r2).
 *
 * Solo esta la tabla A0 (ayudantes de cadenas y memoria), que es lo que usa el juego y
 * lo unico que implementa la referencia. B0 y C0 avisan una vez y devuelven 0, igual
 * que alli; `bios_unhandled` los cuenta para que el host lo reporte.
 *
 * Todo opera sobre la memoria del invitado byte a byte: no se puede usar la libreria
 * de C del anfitrion porque las direcciones son del mapa de la PS1.
 */
#include "ps1rt.h"

#include <stdio.h>
#include <stdlib.h>

#define REPORTED_MAX 64

static uint32_t reported[REPORTED_MAX][2];
static uint32_t reported_n;

static int already_reported(uint32_t table, uint32_t f)
{
    uint32_t i;
    for (i = 0; i < reported_n; i++)
        if (reported[i][0] == table && reported[i][1] == f)
            return 1;
    if (reported_n < REPORTED_MAX) {
        reported[reported_n][0] = table;
        reported[reported_n][1] = f;
        reported_n++;
    }
    return 0;
}

static uint32_t guest_strlen(Cpu *c, uint32_t a)
{
    uint32_t n = 0;
    while (ps1_r8(c, a + n) != 0u)
        n++;
    return n;
}

/* Lectura de enteros estilo C: espacios, signo, digitos en `base` (0 = segun el
 * prefijo). Devuelve el valor y, por `end`, la direccion siguiente al ultimo caracter
 * usado. */
static int32_t parse_int(Cpu *c, uint32_t a, uint32_t base, uint32_t *end)
{
    int neg = 0;
    int64_t v = 0;
    uint32_t ch;

    for (;;) {
        ch = ps1_r8(c, a);
        if (ch == (uint32_t)' ' || (ch >= 9u && ch <= 13u))
            a++;
        else
            break;
    }
    ch = ps1_r8(c, a);
    if (ch == (uint32_t)'-') {
        a++;
        neg = 1;
    } else if (ch == (uint32_t)'+') {
        a++;
    }
    if ((base == 0u || base == 16u) && ps1_r8(c, a) == (uint32_t)'0') {
        uint32_t nx = ps1_r8(c, a + 1u);
        if (nx == (uint32_t)'x' || nx == (uint32_t)'X') {
            a += 2u;
            base = 16u;
        } else if (base == 0u) {
            base = 8u;
        }
    } else if (base == 0u) {
        base = 10u;
    }
    for (;;) {
        uint32_t d;
        ch = ps1_r8(c, a);
        if (ch >= (uint32_t)'0' && ch <= (uint32_t)'9')
            d = ch - (uint32_t)'0';
        else if (ch >= (uint32_t)'a' && ch <= (uint32_t)'z')
            d = ch - (uint32_t)'a' + 10u;
        else if (ch >= (uint32_t)'A' && ch <= (uint32_t)'Z')
            d = ch - (uint32_t)'A' + 10u;
        else
            break;
        if (d >= base)
            break;
        v = v * (int64_t)base + (int64_t)d;
        a++;
    }
    *end = a;
    return (int32_t)(neg ? -v : v);
}

void ps1_bios_call(Cpu *c, uint32_t table)
{
    uint32_t a0 = c->r[4], a1 = c->r[5], a2 = c->r[6];
    uint32_t f = c->r[9];
    uint32_t ret = 0;

    if (table != 0xA0u) {
        /* B0 y C0 no estan implementadas, igual que en la referencia */
        c->bios_unhandled++;
        if (!already_reported(table, f))
            fprintf(stderr, "ps1rt: llamada a BIOS %#x:%#x sin implementar (devuelve 0)\n",
                    (unsigned)table, (unsigned)f);
        c->r[2] = 0;
        return;
    }

    switch (f) {
    case 0x15: {                                        /* strcat */
        uint32_t d = a0 + guest_strlen(c, a0);
        uint32_t i = 0;
        for (;;) {
            uint32_t ch = ps1_r8(c, a1 + i);
            ps1_w8(c, d + i, ch);
            i++;
            if (ch == 0u)
                break;
        }
        ret = a0;
        break;
    }
    case 0x16: {                                        /* strncat */
        uint32_t d = a0 + guest_strlen(c, a0);
        uint32_t i = 0;
        while (i < a2) {
            uint32_t ch = ps1_r8(c, a1 + i);
            if (ch == 0u)
                break;
            ps1_w8(c, d + i, ch);
            i++;
        }
        ps1_w8(c, d + i, 0u);
        ret = a0;
        break;
    }
    case 0x17:                                          /* strcmp */
    case 0x18: {                                        /* strncmp */
        uint32_t i = 0;
        int32_t res = 0;
        while (f == 0x17u || i < a2) {
            uint32_t x = ps1_r8(c, a0 + i), y = ps1_r8(c, a1 + i);
            if (x != y) {
                res = (int32_t)x - (int32_t)y;
                break;
            }
            if (x == 0u)
                break;
            i++;
        }
        ret = (uint32_t)res;
        break;
    }
    case 0x19: {                                        /* strcpy */
        uint32_t i = 0;
        for (;;) {
            uint32_t ch = ps1_r8(c, a1 + i);
            ps1_w8(c, a0 + i, ch);
            i++;
            if (ch == 0u)
                break;
        }
        ret = a0;
        break;
    }
    case 0x1A: {                                        /* strncpy */
        uint32_t i;
        int end = 0;
        for (i = 0; i < a2; i++) {
            uint32_t ch = end ? 0u : ps1_r8(c, a1 + i);
            if (ch == 0u)
                end = 1;
            ps1_w8(c, a0 + i, ch);
        }
        ret = a0;
        break;
    }
    case 0x1B:                                          /* strlen */
        ret = guest_strlen(c, a0);
        break;
    case 0x1C:                                          /* index */
    case 0x1E: {                                        /* strchr */
        uint32_t i = 0;
        ret = 0;
        for (;;) {
            uint32_t ch = ps1_r8(c, a0 + i);
            if (ch == (a1 & 0xFFu)) {
                ret = a0 + i;
                break;
            }
            if (ch == 0u)
                break;
            i++;
        }
        break;
    }
    case 0x1D:                                          /* rindex */
    case 0x1F: {                                        /* strrchr */
        uint32_t i = 0;
        ret = 0;
        for (;;) {
            uint32_t ch = ps1_r8(c, a0 + i);
            if (ch == (a1 & 0xFFu))
                ret = a0 + i;
            if (ch == 0u)
                break;
            i++;
        }
        break;
    }
    case 0x24: {                                        /* strstr */
        uint32_t n = guest_strlen(c, a1);
        uint32_t len = guest_strlen(c, a0);
        ret = 0;
        if (n <= len) {
            uint32_t i;
            /* i recorre 0..=(len - n); con n == 0 el bucle interior no corre y
               devuelve a0, igual que el `all` sobre un rango vacio de la referencia */
            for (i = 0; i + n <= len; i++) {
                uint32_t k;
                int all = 1;
                for (k = 0; k < n; k++)
                    if (ps1_r8(c, a0 + i + k) != ps1_r8(c, a1 + k)) {
                        all = 0;
                        break;
                    }
                if (all) {
                    ret = a0 + i;
                    break;
                }
            }
        }
        break;
    }
    case 0x25: {                                        /* toupper */
        uint32_t ch = a0 & 0xFFu;
        ret = (ch >= (uint32_t)'a' && ch <= (uint32_t)'z') ? ch - 32u : ch;
        break;
    }
    case 0x26: {                                        /* tolower */
        uint32_t ch = a0 & 0xFFu;
        ret = (ch >= (uint32_t)'A' && ch <= (uint32_t)'Z') ? ch + 32u : ch;
        break;
    }
    case 0x2A:                                          /* memcpy */
    case 0x2C: {                                        /* memmove */
        /* La referencia lee todo el origen antes de escribir, asi que las dos se
         * comportan como memmove. Se replica con un buffer intermedio. */
        if (a2 != 0u) {
            uint8_t *buf = (uint8_t *)malloc(a2);
            if (buf == NULL)
                ps1_panic(c, "sin memoria para el memcpy del BIOS");
            ps1_read_bytes(c, a1, buf, a2);
            ps1_write_bytes(c, a0, buf, a2);
            free(buf);
        }
        ret = a0;
        break;
    }
    case 0x2B: {                                        /* memset */
        uint32_t i;
        for (i = 0; i < a2; i++)
            ps1_w8(c, a0 + i, a1 & 0xFFu);
        ret = a0;
        break;
    }
    case 0x2D: {                                        /* memcmp */
        uint32_t i;
        int32_t res = 0;
        for (i = 0; i < a2; i++) {
            uint32_t x = ps1_r8(c, a0 + i), y = ps1_r8(c, a1 + i);
            if (x != y) {
                res = (int32_t)x - (int32_t)y;
                break;
            }
        }
        ret = (uint32_t)res;
        break;
    }
    case 0x2E: {                                        /* memchr */
        uint32_t i;
        ret = 0;
        for (i = 0; i < a2; i++)
            if (ps1_r8(c, a0 + i) == (a1 & 0xFFu)) {
                ret = a0 + i;
                break;
            }
        break;
    }
    case 0x0C:                                          /* strtol */
    case 0x0D:                                          /* strtoul */
    case 0x10:                                          /* atoi */
    case 0x11: {                                        /* atol */
        uint32_t base = (f == 0x0Cu || f == 0x0Du) ? a2 : 10u;
        uint32_t end = 0;
        int32_t value = parse_int(c, a0, base, &end);
        if ((f == 0x0Cu || f == 0x0Du) && a1 != 0u)
            ps1_w32(c, a1, end);
        ret = (uint32_t)value;
        break;
    }
    case 0x0E:                                          /* abs */
    case 0x0F: {                                        /* labs */
        int32_t v = (int32_t)a0;
        ret = (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
        break;
    }
    case 0x2F:                                          /* rand */
        c->rand_seed = c->rand_seed * 0x41C64E6Du + 0x3039u;
        ret = (c->rand_seed >> 16) & 0x7FFFu;
        break;
    case 0x30:                                          /* srand */
        c->rand_seed = a0;
        ret = 0;
        break;
    default:
        c->bios_unhandled++;
        if (!already_reported(table, f))
            fprintf(stderr, "ps1rt: llamada a BIOS %#x:%#x sin implementar (devuelve 0)\n",
                    (unsigned)table, (unsigned)f);
        ret = 0;
        break;
    }

    c->r[2] = ret;
}
