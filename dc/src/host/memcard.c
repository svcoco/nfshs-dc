/* libmcrd: la memory card (M6).
 *
 * Portado de src/ps1/memcard.rs de la referencia: cada ranura es una imagen cruda de 128 KB,
 * el formato `.mcd` de DuckStation (16 bloques de 8 KB; el bloque 0 es el directorio: el
 * marco 0 lleva la cabecera "MC" y los marcos 1..15 una entrada de 128 bytes por bloque de
 * datos con su estado, tamano, siguiente bloque, nombre y un XOR de comprobacion en el byte
 * 127). Crear, escribir, leer, borrar, buscar y formatear trabajan sobre esa imagen, y cada
 * cambio se guarda por `mc_save`, que pone la plataforma: el `.mcd` entero en PC, el archivo
 * de la VMU en Dreamcast (docs/hitos/M6.md).
 *
 * Lo esencial del protocolo es el **comando pendiente**: Exist, Accept, ReadFile y
 * WriteFile no devuelven el resultado, lo dejan pendiente, y el juego lo recoge con
 * `MemCardSync`, que devuelve 1 (hay un comando terminado: escribe cual y su resultado) o
 * -1 (nada pendiente). Create, Delete, GetDirentry y Format devuelven el suyo directamente.
 *
 * Las ranuras: con `mc_two_slots` a 0 (PC y test diferencial) cualquier canal con el nibble
 * bajo a 0 es la ranura 1, que es lo que hace la referencia. Con 1 (Dreamcast), el canal
 * 0x00 es la ranura 1 y el 0x10 la 2, cada una presente o no segun haya VMU (decision de
 * Jorge: puertos A y B).
 */
#include "host.h"

#include <stdio.h>
#include <string.h>

/* codigos de error y de comando de libmcrd (McErr* y McFunc*) */
#define ERR_NONE             0u
#define ERR_CARD_NOT_EXIST   1u
#define ERR_FILE_NOT_EXIST   5u
#define ERR_ALREADY_EXIST    6u
#define ERR_BLOCK_FULL       7u
#define FUNC_EXIST           1u
#define FUNC_ACCEPT          2u
#define FUNC_READ_FILE       3u
#define FUNC_WRITE_FILE      4u

#define BLOCK   8192u
#define FRAME   128u
/* estados de una entrada del directorio: usada (primer, medio y ultimo bloque) y libre */
#define FIRST   0x51u
#define MIDDLE  0x52u
#define LAST    0x53u
#define FREE    0xA0u
#define NO_NEXT 0xFFFFu
#define NOMBRE_MAX 20u

static uint8_t *entrada(uint8_t *card, uint32_t b)
{
    return card + b * FRAME;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void suma(uint8_t *f)
{
    uint8_t x = 0;
    uint32_t i;

    for (i = 0; i < FRAME - 1u; i++)
        x ^= f[i];
    f[FRAME - 1u] = x;
}

void memcard_format(uint8_t *card)
{
    uint32_t b;

    memset(card, 0, MC_CARD);
    card[0] = 'M';
    card[1] = 'C';
    suma(card);
    for (b = 1; b < 16u; b++) {
        uint8_t *f = entrada(card, b);

        wr32(f, FREE);
        wr16(f + 8, NO_NEXT);
        suma(f);
    }
    /* la lista de sectores rotos, vacia */
    for (b = 16; b < 36u; b++) {
        uint8_t *f = entrada(card, b);

        wr32(f, 0xFFFFFFFFu);
        wr16(f + 8, NO_NEXT);
        suma(f);
    }
    /* el marco de prueba de escritura repite la cabecera */
    memcpy(entrada(card, 63), card, FRAME);
}

/* El nombre de la entrada del bloque b (hasta 20 caracteres, sin el 0). */
static void nombre_de(uint8_t *card, uint32_t b, char *out)
{
    const uint8_t *e = entrada(card, b) + 10;
    uint32_t i;

    for (i = 0; i < 21u && e[i] != 0u; i++)
        out[i] = (char)e[i];
    out[i] = '\0';
}

int memcard_find(uint8_t *card, const char *name)
{
    char n[22];
    uint32_t b;

    for (b = 1; b < 16u; b++) {
        if (rd32(entrada(card, b)) != FIRST)
            continue;
        nombre_de(card, b, n);
        if (strcmp(n, name) == 0)
            return (int)b;
    }
    return -1;
}

/* Los bloques de un archivo en orden; devuelve cuantos. */
static uint32_t cadena(uint8_t *card, uint32_t first, uint32_t *out)
{
    uint32_t n = 0, b = first;

    out[n++] = first;
    while (n < 15u) {
        uint16_t nx = rd16(entrada(card, b) + 8);

        if (nx == NO_NEXT || nx >= 15u)
            break;
        b = nx + 1u;
        out[n++] = b;
    }
    return n;
}

/* Copia entre los bytes [off, off + len) del archivo y la RAM del juego en `addr`. */
static int acceso(Cpu *c, uint8_t *card, uint32_t first, uint32_t off, uint32_t len,
                  uint32_t addr, int leer)
{
    uint32_t bl[15], n = cadena(card, first, bl), i;

    if ((uint64_t)off + len > (uint64_t)n * BLOCK)
        return 0;
    for (i = 0; i < len; i++) {
        uint32_t p = off + i;
        uint8_t *at = card + bl[p / BLOCK] * BLOCK + p % BLOCK;

        if (leer)
            ps1_w8(c, addr + i, *at);
        else
            *at = (uint8_t)ps1_r8(c, addr + i);
    }
    return 1;
}

/* Bloques de datos usados de la tarjeta. */
static uint32_t usados(uint8_t *card)
{
    uint32_t b, n = 0;

    for (b = 1; b < 16u; b++)
        if ((rd32(entrada(card, b)) & 0xF0u) != FREE)
            n++;
    return n;
}

int memcard_put(uint8_t *card, const char *name, uint32_t bloques, const uint8_t *datos)
{
    uint32_t libres[15], n = 0, b, i;

    if (bloques == 0u || bloques > 15u)
        return 0;
    for (b = 1; b < 16u && n < bloques; b++)
        if ((rd32(entrada(card, b)) & 0xF0u) == FREE)
            libres[n++] = b;
    if (n < bloques)
        return 0;
    for (i = 0; i < bloques; i++) {
        uint8_t *e = entrada(card, libres[i]);

        memset(e, 0, FRAME);
        wr32(e, i == 0u ? FIRST : i + 1u == bloques ? LAST : MIDDLE);
        wr32(e + 4, i == 0u ? bloques * BLOCK : 0u);
        wr16(e + 8, i + 1u < bloques ? (uint16_t)(libres[i + 1u] - 1u) : (uint16_t)NO_NEXT);
        if (i == 0u)
            memcpy(e + 10, name, strlen(name) < NOMBRE_MAX ? strlen(name) : NOMBRE_MAX);
        suma(e);
        if (datos != NULL)
            memcpy(card + libres[i] * BLOCK, datos + i * BLOCK, BLOCK);
        else
            memset(card + libres[i] * BLOCK, 0, BLOCK);
    }
    return 1;
}

uint32_t memcard_get(uint8_t *card, const char *name, uint8_t *out, uint32_t max_bloques)
{
    uint32_t bl[15], n, i;
    int first = memcard_find(card, name);

    if (first <= 0)
        return 0;
    n = cadena(card, (uint32_t)first, bl);
    if (n > max_bloques)
        return 0;
    for (i = 0; i < n; i++)
        memcpy(out + i * BLOCK, card + bl[i] * BLOCK, BLOCK);
    return n;
}

static void cadena_ps1(Cpu *c, uint32_t a, char *out, uint32_t max)
{
    uint32_t i;

    for (i = 0; i + 1u < max; i++) {
        out[i] = (char)ps1_r8(c, a + i);
        if (out[i] == '\0')
            return;
    }
    out[i] = '\0';
}

/* `*` y `?` como la busqueda del directorio de la tarjeta, sin distinguir mayusculas. */
static int comodin(const char *p, const char *n)
{
    if (*p == '\0')
        return *n == '\0';
    if (*p == '*')
        return comodin(p + 1, n) || (*n != '\0' && comodin(p, n + 1));
    if (*n == '\0')
        return 0;
    if (*p == '?')
        return comodin(p + 1, n + 1);
    {
        char a = *p, b = *n;

        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        return a == b && comodin(p + 1, n + 1);
    }
}

static void pendiente(GameHost *h, uint32_t cmd, uint32_t result)
{
    h->mc_pending = 1;
    h->mc_cmd = cmd;
    h->mc_result = result;
}

/* Guarda el cambio por la plataforma; sin ella (arnes sin --memcard) la tarjeta vive solo en
 * la memoria, y eso cuenta como guardado. */
static int guarda(GameHost *h, uint32_t ranura, const char *name)
{
    h->mc_saves++;
    if (h->mc_save == NULL)
        return 1;
    if (h->mc_save(h, ranura, name))
        return 1;
    h->mc_save_errors++;
    return 0;
}

int host_memcard_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    uint32_t a0 = c->r[4], a1 = c->r[5], a2 = c->r[6], a3 = c->r[7];
    uint32_t ranura = h->mc_two_slots ? (a0 >> 4) & 1u : 0u;
    int hay;
    uint8_t *card = h->mc_card[ranura];

    /* al preguntar si hay tarjeta, la plataforma mira si se puso o se quito */
    if ((addr == 0x800FB534u || addr == 0x800FB770u) && h->mc_probe != NULL)
        h->mc_probe(h, ranura);
    hay = (a0 & 0xFu) == 0u && h->mc_present[ranura];
    char name[64];
    int first;

    switch (addr) {
    case 0x800FB458u:                       /* MemCardInit */
    case 0x800FB484u:                       /* MemCardEnd */
    case 0x800FB4A4u:                       /* MemCardStart */
    case 0x800FB4F4u:                       /* MemCardStop */
    case 0x800FC494u:                       /* MemCardCallback */
        *ret = 0;
        return 1;

    case 0x800FB534u:                       /* MemCardExist(canal) */
    case 0x800FB770u:                       /* MemCardAccept(canal) */
        pendiente(h, addr == 0x800FB534u ? FUNC_EXIST : FUNC_ACCEPT,
                  hay ? ERR_NONE : ERR_CARD_NOT_EXIST);
        *ret = 1;
        return 1;

    case 0x800FBDF4u: {                     /* MemCardReadFile(canal, nombre, dir, desde, bytes) */
        uint32_t bytes = ps1_r32(c, c->r[29] + 16u);

        cadena_ps1(c, a1, name, sizeof name);
        first = hay ? memcard_find(card, name) : -1;
        pendiente(h, FUNC_READ_FILE,
                  first > 0 && acceso(c, card, (uint32_t)first, a3, bytes, a2, 1)
                      ? ERR_NONE : ERR_FILE_NOT_EXIST);
        *ret = 1;
        return 1;
    }

    case 0x800FC014u: {                     /* MemCardWriteFile(canal, nombre, dir, desde, bytes) */
        uint32_t bytes = ps1_r32(c, c->r[29] + 16u);

        cadena_ps1(c, a1, name, sizeof name);
        first = hay ? memcard_find(card, name) : -1;
        pendiente(h, FUNC_WRITE_FILE,
                  first > 0 && acceso(c, card, (uint32_t)first, a3, bytes, a2, 0) &&
                  guarda(h, ranura, name) ? ERR_NONE : ERR_FILE_NOT_EXIST);
        h->mc_writes++;
        *ret = 1;
        return 1;
    }

    case 0x800FC234u: {                     /* MemCardGetDirentry(canal, patron, entradas, &n, primera, max) */
        /* DIRENTRY: name[20], attr, size, next, head, system[4] (40 bytes) */
        uint32_t desde = ps1_r32(c, c->r[29] + 16u), max = ps1_r32(c, c->r[29] + 20u);
        uint32_t b, vistos = 0, n = 0, k;
        char pat[64], nom[22];

        cadena_ps1(c, a1, pat, sizeof pat);
        for (b = 1; hay && b < 16u; b++) {
            uint32_t e;

            if (rd32(entrada(card, b)) != FIRST)
                continue;
            nombre_de(card, b, nom);
            if (!comodin(pat, nom))
                continue;
            if (vistos++ < desde || n >= max)
                continue;
            e = a2 + 40u * n++;
            for (k = 0; k < 20u; k++)
                ps1_w8(c, e + k, k < 19u && k < strlen(nom) ? (uint8_t)nom[k] : 0u);
            ps1_w32(c, e + 20u, FIRST);
            ps1_w32(c, e + 24u, rd32(entrada(card, b) + 4));
            ps1_w32(c, e + 28u, 0u);
            ps1_w32(c, e + 32u, b - 1u);
            ps1_w32(c, e + 36u, 0u);
        }
        ps1_w32(c, a3, n);
        *ret = 0;
        return 1;
    }

    case 0x800FC4A8u:                       /* MemCardSync(modo, &comando, &resultado) */
        /* una escritura que la plataforma hace en segundo plano no ha terminado todavia: el
         * juego sigue preguntando, como con una tarjeta de verdad */
        if (h->mc_pending && h->mc_busy != NULL && h->mc_busy(h)) {
            *ret = 0xFFFFFFFFu;
            return 1;
        }
        if (h->mc_pending && h->mc_async_error) {
            /* y si fallo, el comando que esperaba lo cuenta */
            h->mc_async_error = 0;
            if (h->mc_cmd == FUNC_WRITE_FILE)
                h->mc_result = ERR_FILE_NOT_EXIST;
        }
        if (h->mc_pending) {
            h->mc_pending = 0;
            if (a1 != 0u)
                ps1_w32(c, a1, h->mc_cmd);
            if (a2 != 0u)
                ps1_w32(c, a2, h->mc_result);
            h->mc_syncs++;
            *ret = 1;
        } else {
            *ret = 0xFFFFFFFFu;            /* nada pendiente */
        }
        return 1;

    case 0x800FC5C4u: {                     /* MemCardCreateFile(canal, nombre, bloques) */
        uint32_t bloques = a2 > 0u ? a2 : 1u;

        cadena_ps1(c, a1, name, sizeof name);
        if (!hay) {
            *ret = ERR_CARD_NOT_EXIST;
            return 1;
        }
        if (memcard_find(card, name) > 0) {
            *ret = ERR_ALREADY_EXIST;
            return 1;
        }
        /* lo que de verdad cabe: los 15 bloques de la tarjeta, o lo que diga la plataforma
         * (en Dreamcast, el sitio libre de la VMU) */
        if (usados(card) + bloques > h->mc_limit[ranura] || !memcard_put(card, name, bloques, NULL)) {
            *ret = ERR_BLOCK_FULL;
            return 1;
        }
        *ret = guarda(h, ranura, name) ? ERR_NONE : ERR_CARD_NOT_EXIST;
        return 1;
    }

    case 0x800FC7CCu: {                     /* MemCardDeleteFile(canal, nombre) */
        uint32_t bl[15], n, i;

        cadena_ps1(c, a1, name, sizeof name);
        first = hay ? memcard_find(card, name) : -1;
        if (first <= 0) {
            *ret = ERR_FILE_NOT_EXIST;
            return 1;
        }
        n = cadena(card, (uint32_t)first, bl);
        for (i = 0; i < n; i++) {
            uint8_t *e = entrada(card, bl[i]);

            e[0] = (uint8_t)((e[0] & 0x0Fu) | FREE);
            suma(e);
        }
        *ret = guarda(h, ranura, name) ? ERR_NONE : ERR_FILE_NOT_EXIST;
        return 1;
    }

    case 0x800FC988u:                       /* MemCardFormat(canal) */
        if (!hay) {
            *ret = ERR_CARD_NOT_EXIST;
            return 1;
        }
        memcard_format(card);
        *ret = guarda(h, ranura, NULL) ? ERR_NONE : ERR_CARD_NOT_EXIST;
        return 1;

    default:
        return 0;
    }
}
