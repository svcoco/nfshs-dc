/* Interprete MIPS de respaldo para el codigo que el recompilador no cubrio: puntos de
 * entrada a los que solo se llega de forma indirecta, y colas compartidas.
 *
 * Portado de crates/ps1rt/src/interp.rs. Siempre que puede, salta al codigo
 * recompilado en vez de seguir interpretando.
 *
 * Por que hace falta: el juego llama por puntero a 0x80107358, una funcion de cinco
 * instrucciones (lee un byte y avanza el puntero) que las heuristicas del recompilador
 * no reconocen como inicio de funcion, porque nadie la llama con `jal` y no monta marco
 * de pila. La referencia tiene el mismo agujero y lo tapa aqui.
 */
#include "ps1rt.h"

#include <stdio.h>

static uint32_t sx16(uint32_t v)
{
    return (uint32_t)(int32_t)(int16_t)(uint16_t)(v & 0xFFFFu);
}

/* Escribe en un registro, descartando r0 como hace el hardware. */
static void setr(Cpu *c, uint32_t r, uint32_t v)
{
    if (r != 0u)
        c->r[r] = v;
}

/* Ejecuta una instruccion que no es de control de flujo. Las de control (op 1..7 y
 * los jr/jalr) no llegan aqui; syscall y break se tratan en el bucle. */
static void exec_simple(Cpu *c, uint32_t w, uint32_t pc)
{
    uint32_t op = w >> 26;
    uint32_t rs = (w >> 21) & 31u, rt = (w >> 16) & 31u, rd = (w >> 11) & 31u;
    uint32_t sh = (w >> 6) & 31u, fn = w & 63u;
    uint32_t imm = w & 0xFFFFu;

    switch (op) {
    case 0:
        switch (fn) {
        case 0:  setr(c, rd, c->r[rt] << sh); return;
        case 2:  setr(c, rd, c->r[rt] >> sh); return;
        case 3:  setr(c, rd, (uint32_t)((int32_t)c->r[rt] >> sh)); return;
        case 4:  setr(c, rd, c->r[rt] << (c->r[rs] & 31u)); return;
        case 6:  setr(c, rd, c->r[rt] >> (c->r[rs] & 31u)); return;
        case 7:  setr(c, rd, (uint32_t)((int32_t)c->r[rt] >> (c->r[rs] & 31u))); return;
        case 16: setr(c, rd, c->hi); return;
        case 17: c->hi = c->r[rs]; return;
        case 18: setr(c, rd, c->lo); return;
        case 19: c->lo = c->r[rs]; return;
        case 24: ps1_mult(c, c->r[rs], c->r[rt]); return;
        case 25: ps1_multu(c, c->r[rs], c->r[rt]); return;
        case 26: ps1_div(c, c->r[rs], c->r[rt]); return;
        case 27: ps1_divu(c, c->r[rs], c->r[rt]); return;
        case 32: case 33: setr(c, rd, c->r[rs] + c->r[rt]); return;
        case 34: case 35: setr(c, rd, c->r[rs] - c->r[rt]); return;
        case 36: setr(c, rd, c->r[rs] & c->r[rt]); return;
        case 37: setr(c, rd, c->r[rs] | c->r[rt]); return;
        case 38: setr(c, rd, c->r[rs] ^ c->r[rt]); return;
        case 39: setr(c, rd, ~(c->r[rs] | c->r[rt])); return;
        case 42: setr(c, rd, (uint32_t)((int32_t)c->r[rs] < (int32_t)c->r[rt])); return;
        case 43: setr(c, rd, (uint32_t)(c->r[rs] < c->r[rt])); return;
        case 8: case 9: case 12: case 13:
            return;                      /* control de flujo, o syscall/break */
        default:
            fprintf(stderr, "interp: special %u en %#x\n", (unsigned)fn, (unsigned)pc);
            ps1_panic(c, "interprete: instruccion special no soportada");
        }
        return;
    case 8: case 9: setr(c, rt, c->r[rs] + sx16(imm)); return;
    case 10: setr(c, rt, (uint32_t)((int32_t)c->r[rs] < (int32_t)sx16(imm))); return;
    case 11: setr(c, rt, (uint32_t)(c->r[rs] < sx16(imm))); return;
    case 12: setr(c, rt, c->r[rs] & imm); return;
    case 13: setr(c, rt, c->r[rs] | imm); return;
    case 14: setr(c, rt, c->r[rs] ^ imm); return;
    case 15: setr(c, rt, imm << 16); return;
    case 16:
        if (rs == 0u) setr(c, rt, c->cop0[rd]);
        else if (rs == 4u) c->cop0[rd] = c->r[rt];
        return;
    case 18: case 50: case 58:
        ps1_gte(c, w);
        return;
    default:
        break;
    }
    if (op >= 32u && op <= 46u) {
        uint32_t a = c->r[rs] + sx16(imm);
        switch (op) {
        case 32: setr(c, rt, (uint32_t)(int32_t)(int8_t)ps1_r8(c, a)); return;
        case 33: setr(c, rt, (uint32_t)(int32_t)(int16_t)ps1_r16(c, a)); return;
        case 34: setr(c, rt, ps1_lwl(c, a, c->r[rt])); return;
        case 35: setr(c, rt, ps1_r32(c, a)); return;
        case 36: setr(c, rt, (uint32_t)ps1_r8(c, a)); return;
        case 37: setr(c, rt, (uint32_t)ps1_r16(c, a)); return;
        case 38: setr(c, rt, ps1_lwr(c, a, c->r[rt])); return;
        case 40: ps1_w8(c, a, c->r[rt]); return;
        case 41: ps1_w16(c, a, c->r[rt]); return;
        case 42: ps1_swl(c, a, c->r[rt]); return;
        case 43: ps1_w32(c, a, c->r[rt]); return;
        case 46: ps1_swr(c, a, c->r[rt]); return;
        default:
            fprintf(stderr, "interp: acceso a memoria op %u en %#x\n",
                    (unsigned)op, (unsigned)pc);
            ps1_panic(c, "interprete: acceso a memoria no soportado");
        }
        return;
    }
    fprintf(stderr, "interp: op %u en %#x (%#010x)\n", (unsigned)op, (unsigned)pc,
            (unsigned)w);
    ps1_panic(c, "interprete: opcode no soportado");
}

/* Ejecuta exactamente una instruccion que no sea de control de flujo: los slots de
 * retardo. Un branch en un slot de retardo se ignora, igual que en la referencia. */
static void step_one(Cpu *c, uint32_t pc)
{
    uint32_t w = ps1_r32(c, pc);
    uint32_t op = w >> 26;
    int is_cf = (op >= 1u && op <= 7u) ||
                (op == 0u && ((w & 63u) == 8u || (w & 63u) == 9u));
    if (w == 0u || is_cf)
        return;
    exec_simple(c, w, pc);
}

/* Interpreta desde pc hasta que la funcion actual retorne (un `jr ra` a profundidad 0). */
/* Histograma de direcciones de entrada al interprete: dice que funciones le faltan a
 * la recompilacion y cuanto cuestan. Lo imprime el host al final. */
uint32_t ps1_interp_addr[PS1_INTERP_TOP];
uint64_t ps1_interp_hits[PS1_INTERP_TOP];

static void note_entry(uint32_t pc)
{
    uint32_t i;
    for (i = 0; i < PS1_INTERP_TOP; i++) {
        if (ps1_interp_addr[i] == pc) {
            ps1_interp_hits[i]++;
            return;
        }
        if (ps1_interp_addr[i] == 0u) {
            ps1_interp_addr[i] = pc;
            ps1_interp_hits[i] = 1u;
            return;
        }
    }
}

void ps1_interpret(Cpu *c, uint32_t pc)
{
    uint32_t depth = 0u;
    uint32_t npc = pc + 4u;
    note_entry(pc);

    for (;;) {
        uint32_t w = ps1_r32(c, pc);
        uint32_t op = w >> 26;
        uint32_t rs = (w >> 21) & 31u, rt = (w >> 16) & 31u, rd = (w >> 11) & 31u;
        uint32_t imm = w & 0xFFFFu;
        uint32_t nnpc = npc + 4u;
        uint32_t call = 0u;        /* destino pendiente, tras el slot de retardo */
        int has_call = 0, ret = 0;

        if (op == 0u && (w & 63u) == 8u) {               /* jr */
            if (rs == 31u)
                ret = 1;
            nnpc = c->r[rs];
        } else if (op == 0u && (w & 63u) == 9u) {        /* jalr */
            uint32_t t = c->r[rs];
            setr(c, rd, npc + 4u);
            call = t;
            has_call = 1;
        } else if (op == 0u && (w & 63u) == 12u) {       /* syscall */
            ps1_syscall(c);
        } else if (op == 0u && (w & 63u) == 13u) {       /* break */
            fprintf(stderr, "interp: break en %#x\n", (unsigned)pc);
            ps1_panic(c, "interprete: break");
        } else if (op == 1u) {                           /* bltz/bgez/bltzal/bgezal */
            int32_t v = (int32_t)c->r[rs];
            int taken = (rt & 1u) ? (v >= 0) : (v < 0);
            if (rt & 16u)
                c->r[31] = npc + 4u;
            if (taken)
                nnpc = npc + (sx16(imm) << 2);
        } else if (op == 2u) {                           /* j */
            nnpc = (npc & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2);
        } else if (op == 3u) {                           /* jal */
            c->r[31] = npc + 4u;
            call = (npc & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2);
            has_call = 1;
        } else if (op >= 4u && op <= 7u) {               /* beq/bne/blez/bgtz */
            uint32_t a = c->r[rs], b = c->r[rt];
            int taken;
            if (op == 4u) taken = (a == b);
            else if (op == 5u) taken = (a != b);
            else if (op == 6u) taken = ((int32_t)a <= 0);
            else taken = ((int32_t)a > 0);
            if (taken)
                nnpc = npc + (sx16(imm) << 2);
        } else {
            exec_simple(c, w, pc);
        }

        if (nnpc != npc + 4u || has_call || ret) {
            uint32_t delay_pc = npc;
            uint32_t target = nnpc;
            step_one(c, delay_pc);                       /* el slot de retardo */
            if (has_call) {
                /* el retorno se lee DESPUES del slot, que pudo cambiarlo */
                uint32_t back = c->r[31];
                if (c->dispatch != NULL && c->dispatch(c, call)) {
                    if (c->cont != 0u)
                        ps1_run_cont(c);
                    pc = back;
                    npc = back + 4u;
                } else {
                    depth++;
                    pc = call;
                    npc = call + 4u;
                }
                continue;
            }
            if (ret) {
                if (depth == 0u)
                    return;
                depth--;
            }
            if (target <= delay_pc)
                ps1_tick(c);
            pc = target;
            npc = pc + 4u;
            continue;
        }
        pc = npc;
        npc = nnpc;
    }
}
