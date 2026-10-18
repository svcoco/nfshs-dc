/* Salida por error irrecuperable. Va aparte de cpu.c porque es lo unico de ps1rt
 * que depende de la plataforma: en x86 escribe el diagnostico y aborta; en M1b el
 * host de Dreamcast lo sustituira por algo que se pueda ver en pantalla.
 */
#include "ps1rt.h"

#include <stdio.h>
#include <stdlib.h>

void ps1_panic(Cpu *c, const char *msg)
{
    uint32_t i, n;
    fprintf(stderr, "\nps1rt: %s\n", msg);
    if (c != NULL) {
        fprintf(stderr, "  ticks=%llu  llamadas=%llu  interpretadas=%llu\n",
                (unsigned long long)c->ticks, (unsigned long long)c->calls,
                (unsigned long long)c->interpreted);
        fprintf(stderr, "  io=%llu (ultimo %#x)  desalineados=%llu  borde=%llu\n",
                (unsigned long long)c->io_accesses, (unsigned)c->last_io,
                (unsigned long long)c->misaligned, (unsigned long long)c->ram_edge);
        n = c->stack_depth < PS1_STACK_MAX ? c->stack_depth : PS1_STACK_MAX;
        fprintf(stderr, "  pila (%u):", (unsigned)c->stack_depth);
        for (i = n; i > 0 && i + 12u > n; i--)
            fprintf(stderr, " %#x", (unsigned)c->stack[i - 1]);
        fprintf(stderr, "\n");
    }
    fflush(stderr);
    abort();
}
