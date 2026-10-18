/* Volcado del estado por frame, para el test diferencial de M1a.
 *
 * La idea es comparar la ejecucion en C contra la referencia en Rust con la misma
 * entrada guionizada, y encontrar el primer frame y la primera region de RAM que
 * difieren. Volcar los 2 MB de RAM en cada frame seria inviable (1 800 frames x 2 MB x
 * 2 lados = 7 GB), asi que se hace en dos pasadas:
 *
 *   pasada 1 (esta): por frame, los registros mas un hash por pagina de 4 KB de la RAM.
 *                    Unos 4,2 KB por frame, o sea ~7,5 MB en 1 800 frames.
 *   pasada 2:        con el frame divergente ya localizado, se relanza volcando esos
 *                    2 MB en crudo, solo de ese frame y el anterior.
 *
 * FORMATO (little-endian, sin relleno). El lado Rust tiene que escribir exactamente
 * esto:
 *
 *   cabecera: "NFSD" u32 version=1  u32 pagina=4096  u32 paginas=512  u32 reservado
 *   por frame: u32 idx       numero de frame (campo de video)
 *              u64 ticks     ramas hacia atras acumuladas
 *              u32 r[32], hi, lo
 *              u32 cop0[32]
 *              u32 gte[64]
 *              u32 cont
 *              u32 profundidad de pila
 *              u64 hash[512]  FNV-1a de 64 bits de cada pagina de 4 KB
 *
 * FNV-1a a proposito: es trivial de implementar igual en los dos lados, sin
 * dependencias, y basta para localizar la pagina que cambia.
 */
#include "host.h"

#include <stdio.h>

#define DUMP_PAGE  4096u
#define DUMP_PAGES (PS1_RAM_SIZE / DUMP_PAGE)   /* 512 */

#define FNV64_OFFSET 0xCBF29CE484222325ull
#define FNV64_PRIME  0x00000100000001B3ull

static uint64_t fnv1a(const uint8_t *p, uint32_t n)
{
    uint64_t h = FNV64_OFFSET;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= (uint64_t)p[i];
        h *= FNV64_PRIME;
    }
    return h;
}

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)v;         b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
    fwrite(b, 1, 4, f);
}

static void put64(FILE *f, uint64_t v)
{
    put32(f, (uint32_t)v);
    put32(f, (uint32_t)(v >> 32));
}

int dump_open(GameHost *h, const char *path)
{
    h->dump = fopen(path, "wb");
    if (h->dump == NULL)
        return 0;
    fwrite("NFSD", 1, 4, h->dump);
    put32(h->dump, 1u);            /* version */
    put32(h->dump, DUMP_PAGE);
    put32(h->dump, DUMP_PAGES);
    put32(h->dump, 0u);            /* reservado */
    return 1;
}

void dump_frame(GameHost *h, const Cpu *c)
{
    uint32_t i;
    FILE *f = h->dump;
    /* Antes de la salida temprana a proposito: la segunda pasada tiene que poder
     * volcar la RAM sin pedir tambien la tabla de hashes. */
    if (h->frame_dump_at != 0u && h->frames == h->frame_dump_at) {
        if (dump_ram(c, h->frame_dump_path))
            printf("RAM volcada en la frontera del frame %llu -> %s\n",
                   (unsigned long long)h->frames, h->frame_dump_path);
    }
    if (f == NULL)
        return;
    put32(f, (uint32_t)h->frames);
    put64(f, c->ticks);
    for (i = 0; i < 32u; i++)
        put32(f, c->r[i]);
    put32(f, c->hi);
    put32(f, c->lo);
    for (i = 0; i < 32u; i++)
        put32(f, c->cop0[i]);
    for (i = 0; i < 64u; i++)
        put32(f, c->gte_regs[i]);
    put32(f, c->cont);
    put32(f, c->stack_depth);
    for (i = 0; i < DUMP_PAGES; i++)
        put64(f, fnv1a(c->ram + i * DUMP_PAGE, DUMP_PAGE));
    h->dump_frames++;
}

/* Volcado crudo de la RAM, para la segunda pasada. */
int dump_ram(const Cpu *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return 0;
    fwrite(c->ram, 1, PS1_RAM_SIZE, f);
    fclose(f);
    return 1;
}

void dump_close(GameHost *h)
{
    if (h->dump != NULL) {
        fclose(h->dump);
        h->dump = NULL;
    }
}
