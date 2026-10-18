"""Paridad entre el backend C y el de referencia en Rust, sin compilar nada.

Los dos backends comparten el analisis, asi que lo que hay que demostrar es que la
GENERACION coincide. Para cada una de las funciones se extrae de cada salida una
secuencia canonica de operaciones observables (accesos a memoria, comandos del GTE,
llamadas, ticks, push/pop, retornos y continuaciones) mas el conjunto de leaders, y se
comparan. Si las dos secuencias son iguales instruccion a instruccion, las dos
traducciones hacen lo mismo en el mismo orden.

No cubre la aritmetica dentro de cada expresion: eso lo cubren los tests unitarios de
ps1rt contra el oraculo Rust. Aqui se verifica la estructura y el orden, que es donde
estan los errores de un backend nuevo (slots de retardo, ticks, aristas de control).

    uso: python -I check_parity.py [--limit N] [-v]
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.argv = ['check_parity'] + [a for a in sys.argv[1:]]
_args = sys.argv[1:]
import recomp  # noqa: E402

VERBOSE = '-v' in _args
LIMIT = None
if '--limit' in _args:
    LIMIT = int(_args[_args.index('--limit') + 1])

for line in open(os.path.join(recomp.UPSTREAM_TOOLS, 'hle_funcs.txt')):
    if line.strip() and not line.startswith('#'):
        recomp.HLE_SET.add(int(line.split()[0], 16))

RUST_COMMENT = re.compile(r'//.*')
C_COMMENT = re.compile(r'/\*.*?\*/', re.S)

# (patron, token) en el orden en que aparecen en el texto ya sin comentarios.
RUST_TOKENS = [
    (r'c\.stack\.push\((0x[0-9a-f]+)\)', 'push:{}'),
    (r'c\.m\.(r8|r16|r32|w8|w16|w32)\(', '{}'),
    (r'c\.(lwl|lwr|swl|swr)\(', '{}'),
    (r'c\.(mult|multu|div|divu)\(', '{}'),
    (r'c\.gte\((0x[0-9a-f]+)\)', 'gte:{}'),
    (r'c\.tick\(\)', 'tick'),
    (r'c\.syscall\(\)', 'syscall'),
    (r'f_([0-9a-f]{8})\(c\)', 'call:{}'),
    (r'c\.call\((0x[0-9a-f]+)\)', 'calladdr:{}'),
    (r'c\.call\(target\)', 'callind'),
    (r'c\.run_cont\(\)', 'cont_run'),
    (r'c\.stack\.pop\(\)', 'pop'),
    (r'c\.cont = pc', 'setcont'),
    (r'\breturn;', 'ret'),
]
C_TOKENS = [
    (r'ps1_push\(c, (0x[0-9a-f]+)u\)', 'push:{}'),
    (r'ps1_(r8|r16|r32|w8|w16|w32)\(c,', '{}'),
    (r'ps1_(lwl|lwr|swl|swr)\(c,', '{}'),
    (r'ps1_(mult|multu|div|divu)\(c,', '{}'),
    (r'ps1_gte\(c, (0x[0-9a-f]+)u\)', 'gte:{}'),
    (r'ps1_tick\(c\)', 'tick'),
    (r'ps1_syscall\(c\)', 'syscall'),
    (r'f_([0-9a-f]{8})\(c\)', 'call:{}'),
    (r'ps1_call\(c, (0x[0-9a-f]+)u\)', 'calladdr:{}'),
    (r'ps1_call\(c, t\)', 'callind'),
    (r'ps1_run_cont\(c\)', 'cont_run'),
    (r'ps1_pop\(c\)', 'pop'),
    (r'c->cont = pc', 'setcont'),
    (r'\breturn;', 'ret'),
]


def tokenize(text, table):
    rx = re.compile('|'.join(f'(?P<t{i}>{p})' for i, (p, _) in enumerate(table)))
    out = []
    for m in rx.finditer(text):
        i = int(m.lastgroup[1:]) if m.lastgroup and m.lastgroup.startswith('t') else None
        if i is None:
            # grupo interno: localizar el patron por el indice del grupo nombrado
            for j in range(len(table)):
                if m.group(f't{j}') is not None:
                    i = j
                    break
        pat, tok = table[i]
        inner = [g for g in m.groups() if g is not None and g != m.group(0)]
        out.append(tok.format(inner[0]) if '{}' in tok and inner else tok)
    return out


RUST_LEADER = re.compile(r'^\s{12}(0x[0-9a-f]+) => \{$', re.M)
C_LEADER = re.compile(r'^L_([0-9a-f]{8}):$', re.M)


def main():
    funcs = recomp.collect(recomp.all_starts())
    known = set(funcs)
    recomp.collect_entry_points(funcs)
    if LIMIT:
        funcs = funcs[:LIMIT]
    print(f'comparando {len(funcs)} funciones (C contra Rust)...')
    bad = []
    tok_total = 0
    for f in funcs:
        rs = RUST_COMMENT.sub('', recomp.safe_gen(f, known))
        cs = C_COMMENT.sub('', recomp.safe_gen_c(f, known))
        rl = [int(x, 16) for x in RUST_LEADER.findall(rs)]
        cl = [int(x, 16) for x in C_LEADER.findall(cs)]
        rt = tokenize(rs, RUST_TOKENS)
        ct = tokenize(cs, C_TOKENS)
        tok_total += len(rt)
        why = None
        if rl != cl:
            why = f'leaders: rust {len(rl)} vs c {len(cl)}'
            d = next((i for i, (a, b) in enumerate(zip(rl, cl)) if a != b), min(len(rl), len(cl)))
            why += f'; primera diferencia en el indice {d}'
        elif rt != ct:
            d = next((i for i, (a, b) in enumerate(zip(rt, ct)) if a != b),
                     min(len(rt), len(ct)))
            why = (f'operaciones en el indice {d}: rust {rt[d:d + 4]} vs c {ct[d:d + 4]}')
        if why:
            bad.append((f, why))
            if VERBOSE or len(bad) <= 10:
                print(f'  DIFIERE f_{f:08x}: {why}')
    print(f'\n{len(funcs) - len(bad)}/{len(funcs)} funciones con estructura y orden '
          f'identicos ({tok_total} operaciones comparadas)')
    if bad:
        sys.exit(f'ERROR: {len(bad)} funciones difieren')
    print('OK: paridad estructural completa entre el backend C y la referencia Rust.')


main()
