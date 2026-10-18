/* Aritmetica del R3000: mult/div con sus rarezas, y los accesos no alineados
 * lwl/lwr/swl/swr. Portado de Cpu::{mult,multu,div,divu,lwl,lwr,swl,swr} de
 * crates/ps1rt/src/lib.rs.
 */
#include "ps1rt.h"

void ps1_mult(Cpu *c, uint32_t a, uint32_t b)
{
    int64_t p = (int64_t)(int32_t)a * (int64_t)(int32_t)b;
    c->lo = (uint32_t)p;
    c->hi = (uint32_t)((uint64_t)p >> 32);
}

void ps1_multu(Cpu *c, uint32_t a, uint32_t b)
{
    uint64_t p = (uint64_t)a * (uint64_t)b;
    c->lo = (uint32_t)p;
    c->hi = (uint32_t)(p >> 32);
}

/* Las tres ramas se comprueban ANTES de dividir: en C, INT32_MIN / -1 es
 * comportamiento indefinido, y la division por cero tambien. */
void ps1_div(Cpu *c, uint32_t ua, uint32_t ub)
{
    int32_t a = (int32_t)ua, b = (int32_t)ub;
    if (b == 0) {
        c->lo = (a < 0) ? 1u : 0xFFFFFFFFu;
        c->hi = (uint32_t)a;
    } else if (a == INT32_MIN && b == -1) {
        c->lo = (uint32_t)a;          /* 0x80000000 */
        c->hi = 0;
    } else {
        c->lo = (uint32_t)(a / b);
        c->hi = (uint32_t)(a % b);
    }
}

void ps1_divu(Cpu *c, uint32_t a, uint32_t b)
{
    if (b == 0) {
        c->lo = 0xFFFFFFFFu;
        c->hi = a;
    } else {
        c->lo = a / b;
        c->hi = a % b;
    }
}

/* lwl/lwr/swl/swr leen la palabra ALINEADA y la mezclan con el valor previo.
 * El caso sh == 0 va aparte porque `32 - sh` seria un desplazamiento de 32, que
 * en C es indefinido (en Rust entra en panico). */
uint32_t ps1_lwl(Cpu *c, uint32_t addr, uint32_t cur)
{
    uint32_t word = ps1_r32(c, addr & ~3u);
    uint32_t sh = (3u - (addr & 3u)) * 8u;
    uint32_t mask = (sh == 0u) ? 0u : (0xFFFFFFFFu >> (32u - sh));
    return (cur & mask) | (word << sh);
}

uint32_t ps1_lwr(Cpu *c, uint32_t addr, uint32_t cur)
{
    uint32_t word = ps1_r32(c, addr & ~3u);
    uint32_t sh = (addr & 3u) * 8u;
    uint32_t mask = (sh == 0u) ? 0u : (0xFFFFFFFFu << (32u - sh));
    return (cur & mask) | (word >> sh);
}

void ps1_swl(Cpu *c, uint32_t addr, uint32_t v)
{
    uint32_t a = addr & ~3u;
    uint32_t word = ps1_r32(c, a);
    uint32_t sh = (3u - (addr & 3u)) * 8u;
    uint32_t keep = (sh == 0u) ? 0u : (0xFFFFFFFFu << (32u - sh));
    ps1_w32(c, a, (word & keep) | (v >> sh));
}

void ps1_swr(Cpu *c, uint32_t addr, uint32_t v)
{
    uint32_t a = addr & ~3u;
    uint32_t word = ps1_r32(c, a);
    uint32_t sh = (addr & 3u) * 8u;
    uint32_t keep = (sh == 0u) ? 0u : (0xFFFFFFFFu >> (32u - sh));
    ps1_w32(c, a, (word & keep) | (v << sh));
}
