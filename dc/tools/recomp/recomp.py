"""Static recompiler: MIPS R3000 functions from NFS4.EXE -> Rust or C source.

Each MIPS function becomes one function in the target language, a state machine over basic
blocks with exact delay-slot semantics. Calls between recompiled functions are direct;
others go through the runtime's dispatch (`c.call(addr)` / `ps1_call(c, addr)`), which the
host must provide.

Rust is the upstream backend (nfshs-rs) and stays byte-for-byte as it was. C is the
Dreamcast port's backend; both share the whole analysis, which is what makes the two
outputs comparable in the differential test.

    usage: recomp.py OUT_DIR [--lang rust|c] [--all | ROOTS...] [--hle ADDRS...]
                             [--parts N | --instrs-per-part N] [--no-comments]

Unlike upstream's copy, paths resolve from the repository root instead of the current
directory, so it runs from anywhere.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
UPSTREAM_TOOLS = os.path.join(ROOT, 'tools')
GAMEDATA = os.path.join(ROOT, 'gamedata')
sys.path.insert(0, UPSTREAM_TOOLS)
import mips

e = mips.Exe(os.path.join(GAMEDATA, 'NFS4.EXE'))
# The front-end overlay (FRONT.BIN) stays resident at 0x80010000 during races and race code
# calls into it. NFS4.EXE has no code in that range (start-up rdata only), so recompile the
# combined image: executable with the overlay laid over its low region.
_front = open(os.path.join(GAMEDATA, 'FRONT.BIN'), 'rb').read()
_img = bytearray(e.data)
_img[0:len(_front)] = _front
e.data = bytes(_img)

# all direct call targets in the executable -> function starts
ALL_CALLS = set()
for i in range(len(e.data) // 4):
    pc = e.base + 4 * i
    w = e.word(pc)
    if w >> 26 == 3:
        ALL_CALLS.add((pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
SORTED_STARTS = sorted(ALL_CALLS)


def is_prologue(w):
    return (w >> 16) == 0x27BD and (w & 0x8000)


_END_CACHE = {}


def func_end(start):
    """Function extent by control-flow tracing from the entry: follow branches and jumps,
    stop paths at `jr ra`. A `jr reg` (jump table) extends linearly to the next known start."""
    if start in _END_CACHE:
        return _END_CACHE[start]
    import bisect
    k = bisect.bisect_right(SORTED_STARTS, start)
    limit = min(SORTED_STARTS[k] if k < len(SORTED_STARTS) else e.end, e.end, start + 0x8000)
    seen = set()
    work = [start]
    hi = start
    jump_table = False
    while work:
        pc = work.pop()
        while start <= pc < limit and pc not in seen:
            seen.add(pc)
            hi = max(hi, pc)
            w = e.word(pc)
            op = w >> 26
            fn = w & 63
            if op == 0 and fn == 8:  # jr
                seen.add(pc + 4); hi = max(hi, pc + 4)
                if ((w >> 21) & 31) != 31:
                    jump_table = True
                break
            if op == 2:  # j
                t = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                seen.add(pc + 4); hi = max(hi, pc + 4)
                if start <= t < limit:
                    work.append(t)
                break
            if op in (4, 5, 6, 7) or op == 1:
                s16 = w & 0xFFFF
                s16 = s16 - 0x10000 if s16 & 0x8000 else s16
                t = (pc + 4 + s16 * 4) & 0xFFFFFFFF
                if start <= t < limit:
                    work.append(t)
                # unconditional "b" (beq zero, zero) ends the path
                if op == 4 and ((w >> 21) & 31) == 0 and ((w >> 16) & 31) == 0:
                    seen.add(pc + 4); hi = max(hi, pc + 4)
                    break
            pc += 4
    end = hi + 4
    if jump_table:
        end = limit
    _END_CACHE[start] = end
    return end


def looks_like_function(a):
    """Address-taken code constant that plausibly starts a function."""
    if not (e.valid(a) and a & 3 == 0):
        return False
    if a in ALL_CALLS:
        return True
    # a stack frame set up within the first instructions (the compiler schedules loads
    # before it), or the previous function's return just before (with alignment padding)
    if any(e.valid(a + 4 * k) and is_prologue(e.word(a + 4 * k)) for k in range(4)):
        return True
    return any(e.valid(a - 4 * k) and e.word(a - 4 * k) == 0x03E00008 for k in (2, 3))


def calls_of(start):
    end = func_end(start)
    out = set()
    hi = {}
    for q in range(start, end, 4):
        w = e.word(q)
        op = w >> 26
        if op == 3:
            out.add((q & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
        rt, rs = (w >> 16) & 31, (w >> 21) & 31
        if op == 15:
            hi[rt] = (w & 0xFFFF) << 16
        elif op in (9, 13) and rs in hi:
            imm = w & 0xFFFF
            v = (hi[rs] + (imm - 0x10000 if (op == 9 and imm & 0x8000) else imm)) & 0xFFFFFFFF
            if 0x80010000 <= v < e.end and looks_like_function(v):
                out.add(v)
    return out


HLE_SET = set()
# Solo en el backend C (M3): codigo que el analisis no genera y que en el juego acaba en el
# interprete de respaldo, medido con NFSHS_INTERPLOG en el menu del video wall.
#  - C_HLE_BODIES: funciones HLE que el host a veces NO atiende (el front end "captura" su
#    propio dibujo y corre el original). Se genera su cuerpo, pero las llamadas a ellas
#    siguen yendo por ps1_call (no entran en `known`): primero el host, y si no las atiende,
#    el codigo recompilado en vez del interprete.
#  - C_EXTRA_STARTS: inicios de funcion a los que solo se llega por puntero y que las
#    heuristicas no reconocen (0x80107358: cinco instrucciones, sin marco, nadie la llama
#    con jal; ver dc/src/ps1rt/interp.c).
C_HLE_BODIES = {0x8004F12C, 0x8004F3B8}      # PSXDrawSquare, PSXDrawTransSquare
C_EXTRA_STARTS = {0x80107358}

# Optional names transferred from the 1999 development build (tools/symmatch.py output).
NAMES = {}
SRC = {}
try:
    for line in open(os.path.join(ROOT, 're', 'retail_syms.tsv')):
        r, n, d = line.rstrip(chr(10)).split(chr(9))
        NAMES[int(r, 16)] = (n, int(d, 16))
    for line in open(os.path.join(ROOT, 're', 'nfs4_funcs.tsv'), encoding='latin1'):
        a, loc, n = line.split(chr(9))[:3]
        SRC[int(a, 16)] = loc.split('nfs4' + chr(92))[-1]
except FileNotFoundError:
    pass



def collect(roots):
    seen = set()
    stack = list(roots)
    while stack:
        f = stack.pop()
        if f in seen or f in HLE_SET or not e.valid(f):
            continue
        seen.add(f)
        stack.extend(calls_of(f))
    return sorted(seen)


R = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
     's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']


def reg(i):
    return f'c.r[{i}]'


def set_reg(i, expr):
    if i == 0:
        return f'let _ = {expr};'
    return f'c.r[{i}] = {expr};'


def sx(imm):
    return imm - 0x10000 if imm & 0x8000 else imm


def gen_simple(w, pc):
    """Rust for a non-control-flow instruction (or None if control flow)."""
    op = w >> 26
    rs, rt, rd = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    sh, fn = (w >> 6) & 31, w & 63
    imm = w & 0xFFFF
    s = sx(imm)
    if w == 0:
        return ''
    if op == 0:
        if fn == 0: return set_reg(rd, f'{reg(rt)} << {sh}')
        if fn == 2: return set_reg(rd, f'{reg(rt)} >> {sh}')
        if fn == 3: return set_reg(rd, f'(({reg(rt)} as i32) >> {sh}) as u32')
        if fn == 4: return set_reg(rd, f'{reg(rt)}.wrapping_shl({reg(rs)} & 31)')
        if fn == 6: return set_reg(rd, f'{reg(rt)}.wrapping_shr({reg(rs)} & 31)')
        if fn == 7: return set_reg(rd, f'(({reg(rt)} as i32).wrapping_shr({reg(rs)} & 31)) as u32')
        if fn == 16: return set_reg(rd, 'c.hi')
        if fn == 18: return set_reg(rd, 'c.lo')
        if fn == 17: return f'c.hi = {reg(rs)};'
        if fn == 19: return f'c.lo = {reg(rs)};'
        if fn == 24: return f'c.mult({reg(rs)}, {reg(rt)});'
        if fn == 25: return f'c.multu({reg(rs)}, {reg(rt)});'
        if fn == 26: return f'c.div({reg(rs)}, {reg(rt)});'
        if fn == 27: return f'c.divu({reg(rs)}, {reg(rt)});'
        if fn in (32, 33): return set_reg(rd, f'{reg(rs)}.wrapping_add({reg(rt)})')
        if fn in (34, 35): return set_reg(rd, f'{reg(rs)}.wrapping_sub({reg(rt)})')
        if fn == 36: return set_reg(rd, f'{reg(rs)} & {reg(rt)}')
        if fn == 37: return set_reg(rd, f'{reg(rs)} | {reg(rt)}')
        if fn == 38: return set_reg(rd, f'{reg(rs)} ^ {reg(rt)}')
        if fn == 39: return set_reg(rd, f'!({reg(rs)} | {reg(rt)})')
        if fn == 42: return set_reg(rd, f'(({reg(rs)} as i32) < ({reg(rt)} as i32)) as u32')
        if fn == 43: return set_reg(rd, f'({reg(rs)} < {reg(rt)}) as u32')
        if fn in (8, 9, 12, 13):
            return None
        raise ValueError(f'special {fn} at {pc:#x}')
    if op == 16:
        if rs == 0: return set_reg(rt, f'c.cop0[{rd}]')
        if rs == 4: return f'c.cop0[{rd}] = {reg(rt)};'
        if rs == 16 and fn == 16: return '/* rfe */'
        raise ValueError(f'cop0 {w:#010x} at {pc:#x}')
    if op in (18, 50, 58):
        return f'c.gte({w:#010x});'
    if op in (8, 9): return set_reg(rt, f'{reg(rs)}.wrapping_add({s & 0xFFFFFFFF:#x})')
    if op == 10: return set_reg(rt, f'(({reg(rs)} as i32) < {s}) as u32')
    if op == 11: return set_reg(rt, f'({reg(rs)} < {s & 0xFFFFFFFF:#x}) as u32')
    if op == 12: return set_reg(rt, f'{reg(rs)} & {imm:#x}')
    if op == 13: return set_reg(rt, f'{reg(rs)} | {imm:#x}')
    if op == 14: return set_reg(rt, f'{reg(rs)} ^ {imm:#x}')
    if op == 15: return set_reg(rt, f'{imm << 16:#x}')
    addr = f'{reg(rs)}.wrapping_add({s & 0xFFFFFFFF:#x})'
    if op == 32: return set_reg(rt, f'c.m.r8({addr}) as i8 as i32 as u32')
    if op == 33: return set_reg(rt, f'c.m.r16({addr}) as i16 as i32 as u32')
    if op == 35: return set_reg(rt, f'c.m.r32({addr})')
    if op == 36: return set_reg(rt, f'c.m.r8({addr}) as u32')
    if op == 37: return set_reg(rt, f'c.m.r16({addr}) as u32')
    if op == 34: return set_reg(rt, f'c.lwl({addr}, {reg(rt)})')
    if op == 38: return set_reg(rt, f'c.lwr({addr}, {reg(rt)})')
    if op == 40: return f'c.m.w8({addr}, {reg(rt)} as u8);'
    if op == 41: return f'c.m.w16({addr}, {reg(rt)} as u16);'
    if op == 43: return f'c.m.w32({addr}, {reg(rt)});'
    if op == 42: return f'c.swl({addr}, {reg(rt)});'
    if op == 46: return f'c.swr({addr}, {reg(rt)});'
    if op in (1, 2, 3, 4, 5, 6, 7):
        return None
    raise ValueError(f'op {op} at {pc:#x} ({w:#010x})')


def branch_info(w, pc):
    """(kind, cond_expr, target) for control flow instructions."""
    op = w >> 26
    rs, rt = (w >> 21) & 31, (w >> 16) & 31
    s = sx(w & 0xFFFF)
    tgt = (pc + 4 + s * 4) & 0xFFFFFFFF
    if op == 0 and (w & 63) == 8:
        return ('jr', rs, None)
    if op == 0 and (w & 63) == 9:
        return ('jalr', rs, (w >> 11) & 31)
    if op == 0 and (w & 63) == 12:
        return ('syscall', None, None)
    if op == 2:
        return ('j', None, (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    if op == 3:
        return ('jal', None, (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    if op == 4:
        return ('b', f'{reg(rs)} == {reg(rt)}', tgt)
    if op == 5:
        return ('b', f'{reg(rs)} != {reg(rt)}', tgt)
    if op == 6:
        return ('b', f'({reg(rs)} as i32) <= 0', tgt)
    if op == 7:
        return ('b', f'({reg(rs)} as i32) > 0', tgt)
    if op == 1:
        cond = f'({reg(rs)} as i32) >= 0' if rt & 1 else f'({reg(rs)} as i32) < 0'
        if rt & 16:
            return ('bal', cond, tgt)
        return ('b', cond, tgt)
    return None


# Addresses other functions continue at inside a function's range (their branch targets past
# their own end, and the fall-through after a branch in their last slot): each needs its own
# block in the function holding it, or the continuation would find no entry.
ENTRY_POINTS = set()


def collect_entry_points(funcs):
    for g in funcs:
        end = func_end(g)
        for pc in range(g, end, 4):
            bi = branch_info(e.word(pc), pc)
            if not bi:
                continue
            if bi[0] in ('b', 'bal', 'j') and not g <= bi[2] < end:
                ENTRY_POINTS.add(bi[2])
            if bi[0] in ('b', 'bal') and pc + 8 >= end:
                ENTRY_POINTS.add(pc + 8)


def gen_function(f, known):
    end = func_end(f)
    words = {pc: e.word(pc) for pc in range(f, end, 4)}
    has_indirect = any((w >> 26) == 0 and (w & 63) == 8 and ((w >> 21) & 31) != 31 for w in words.values())
    leaders = {f}
    for pc, w in words.items():
        bi = branch_info(w, pc)
        if bi:
            kind = bi[0]
            if kind in ('b', 'bal') and f <= bi[2] < end:
                leaders.add(bi[2])
            if kind == 'j' and f <= bi[2] < end:
                leaders.add(bi[2])
            leaders.add(pc + 8)
    leaders |= {x for x in ENTRY_POINTS if f <= x < end}
    if has_indirect:
        leaders |= set(words)
    leaders = sorted(x for x in leaders if f <= x < end)
    doc = []
    if f in NAMES:
        n, dev = NAMES[f]
        doc.append(f'/// `{n}`' + (f' ({SRC[dev]})' if dev in SRC else ''))
    out = doc + [f'pub fn f_{f:08x}(c: &mut Cpu) {{ f_{f:08x}_at(c, {f:#x}) }}',
                 f'pub fn f_{f:08x}_at(c: &mut Cpu, mut pc: u32) {{', f'    c.stack.push({f:#x});', '    loop {', '        match pc {']
    for li, start in enumerate(leaders):
        stop = leaders[li + 1] if li + 1 < len(leaders) else end
        body = []
        pc = start
        terminated = False
        while pc < stop:
            w = words[pc]
            bi = branch_info(w, pc)
            if bi is None:
                code = gen_simple(w, pc)
                if code:
                    body.append(f'                {code} // {pc:#x}: {mips.dis1(w, pc)}')
                pc += 4
                continue
            kind = bi[0]
            # (a branch at the end of the range still has its delay slot, even where the next
            # function starts: it runs before the jump either way)
            delay_w = words.get(pc + 4, e.word(pc + 4))
            delay = gen_simple(delay_w, pc + 4) or ''
            if delay is None:
                raise ValueError(f'branch in delay slot at {pc:#x}')
            dl = f'                {delay} // delay {pc + 4:#x}' if delay else ''
            comment = f' // {pc:#x}: {mips.dis1(w, pc)}'
            if kind == 'b':
                body.append(f'                let cond = {bi[1]};{comment}')
                if dl: body.append(dl)
                tick = 'c.tick(); ' if bi[2] <= pc else ''
                body.append(f'                if cond {{ {tick}pc = {bi[2]:#x}; continue; }}')
                body.append(f'                pc = {pc + 8:#x}; continue;')
            elif kind == 'bal':
                raise ValueError(f'bltzal/bgezal at {pc:#x}')
            elif kind == 'j':
                if dl: body.append(dl)
                if f <= bi[2] < end:
                    tick = 'c.tick(); ' if bi[2] <= pc else ''
                    body.append(f'                {tick}pc = {bi[2]:#x}; continue;{comment}')
                else:  # tail call
                    body.append(f'                c.stack.pop(); {call_expr(bi[2], known)} return;{comment}')
            elif kind == 'jal':
                if dl: body.append(dl)
                body.append(f'                c.r[31] = {pc + 8:#x};{comment}')
                body.append(f'                {call_expr(bi[2], known)} if c.cont != 0 {{ c.run_cont(); }}')
                body.append(f'                pc = {pc + 8:#x}; continue;')
            elif kind == 'jr':
                rs = bi[1]
                body.append(f'                let target = {reg(rs)};{comment}')
                if dl: body.append(dl)
                if rs == 31:
                    body.append('                let _ = target; c.stack.pop(); return;')
                else:
                    body.append(f'                if ({f:#x}..{end:#x}).contains(&target) {{ pc = target; continue; }}')
                    body.append('                c.stack.pop(); c.call(target); return;')
            elif kind == 'jalr':
                rs, rd = bi[1], bi[2]
                body.append(f'                let target = {reg(rs)};{comment}')
                if dl: body.append(dl)
                if rd: body.append(f'                c.r[{rd}] = {pc + 8:#x};')
                body.append('                c.call(target); if c.cont != 0 { c.run_cont(); }')
                body.append(f'                pc = {pc + 8:#x}; continue;')
            elif kind == 'syscall':
                body.append(f'                c.syscall();{comment}')
                pc += 4
                continue
            terminated = True
            pc += 8
            break
        if not terminated:
            body.append(f'                pc = {stop:#x}; continue;')
        out.append(f'            {start:#x} => {{')
        out.extend(body)
        out.append('            }')
    # control left the traced extent (fall-through into a neighbouring function): tail call
    out.append('            _ => { c.stack.pop(); c.cont = pc; return; }')
    out += ['        }', '    }', '}', '']
    return '\n'.join(out)


HOOK_SET = set()
try:
    for _l in open(os.path.join(UPSTREAM_TOOLS, 'hook_funcs.txt')):
        if _l.strip() and not _l.startswith('#'):
            HOOK_SET.add(int(_l.split()[0], 16))
except FileNotFoundError:
    pass


def call_expr(t, known):
    return f'f_{t:08x}(c);' if t in known and t not in HOOK_SET else f'c.call({t:#x});'


def all_starts():
    starts = set(ALL_CALLS)
    for a in range(e.base, e.end, 4):
        if is_prologue(e.word(a)):
            starts.add(a)
    for a in range(e.base, e.end, 4):
        v = e.word(a)
        if 0x80010000 <= v < e.end and looks_like_function(v):
            starts.add(v)
    return sorted(x for x in starts if e.valid(x))


def safe_gen(f, known):
    try:
        return gen_function(f, known)
    except ValueError as ex:
        return f'pub fn f_{f:08x}(c: &mut Cpu) {{ let _ = c; panic!("f_{f:08x}: not code"); }}' + chr(10)


# ---------------------------------------------------------------------------
# Backend C (port a Dreamcast). Comparte todo el analisis con el backend Rust;
# solo cambia la generacion, que es lo que hace comparables las dos salidas.
# ---------------------------------------------------------------------------

def cr(i):
    """Registro del invitado. Igual que el backend Rust, no se pliega r0 a 0: eso
    mantiene la paridad trivial con la version de referencia (ver --zero-reg)."""
    return f'c->r[{i}]'


def cu(v):
    return f'{v & 0xFFFFFFFF:#010x}u'


def cset(i, expr):
    """Escritura a registro. OJO: escribir a r0 descarta el valor pero la expresion
    SI se evalua (el backend Rust emite `let _ = expr;`), asi que un `lb $zero, off(rs)`
    ejecuta el acceso. De ahi el (void) y de ahi que los accesores no puedan ser pure."""
    if i == 0:
        return f'(void)({expr});'
    return f'c->r[{i}] = {expr};'


def gen_simple_c(w, pc):
    """C para una instruccion que no es de control de flujo (None si lo es)."""
    op = w >> 26
    rs, rt, rd = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    sh, fn = (w >> 6) & 31, w & 63
    imm = w & 0xFFFF
    s = sx(imm)
    if w == 0:
        return ''
    if op == 0:
        if fn == 0: return cset(rd, f'{cr(rt)} << {sh}')
        if fn == 2: return cset(rd, f'{cr(rt)} >> {sh}')
        if fn == 3: return cset(rd, f'(uint32_t)((int32_t){cr(rt)} >> {sh})')
        if fn == 4: return cset(rd, f'{cr(rt)} << ({cr(rs)} & 31u)')
        if fn == 6: return cset(rd, f'{cr(rt)} >> ({cr(rs)} & 31u)')
        if fn == 7: return cset(rd, f'(uint32_t)((int32_t){cr(rt)} >> ({cr(rs)} & 31u))')
        if fn == 16: return cset(rd, 'c->hi')
        if fn == 18: return cset(rd, 'c->lo')
        if fn == 17: return f'c->hi = {cr(rs)};'
        if fn == 19: return f'c->lo = {cr(rs)};'
        if fn == 24: return f'ps1_mult(c, {cr(rs)}, {cr(rt)});'
        if fn == 25: return f'ps1_multu(c, {cr(rs)}, {cr(rt)});'
        if fn == 26: return f'ps1_div(c, {cr(rs)}, {cr(rt)});'
        if fn == 27: return f'ps1_divu(c, {cr(rs)}, {cr(rt)});'
        if fn in (32, 33): return cset(rd, f'{cr(rs)} + {cr(rt)}')
        if fn in (34, 35): return cset(rd, f'{cr(rs)} - {cr(rt)}')
        if fn == 36: return cset(rd, f'{cr(rs)} & {cr(rt)}')
        if fn == 37: return cset(rd, f'{cr(rs)} | {cr(rt)}')
        if fn == 38: return cset(rd, f'{cr(rs)} ^ {cr(rt)}')
        # nor: en Rust `!` es NOT de bits; en C tiene que ser `~`, no `!`
        if fn == 39: return cset(rd, f'~({cr(rs)} | {cr(rt)})')
        if fn == 42: return cset(rd, f'(uint32_t)((int32_t){cr(rs)} < (int32_t){cr(rt)})')
        if fn == 43: return cset(rd, f'(uint32_t)({cr(rs)} < {cr(rt)})')
        if fn in (8, 9, 12, 13):
            return None
        raise ValueError(f'special {fn} at {pc:#x}')
    if op == 16:
        if rs == 0: return cset(rt, f'c->cop0[{rd}]')
        if rs == 4: return f'c->cop0[{rd}] = {cr(rt)};'
        if rs == 16 and fn == 16: return '/* rfe */'
        raise ValueError(f'cop0 {w:#010x} at {pc:#x}')
    if op in (18, 50, 58):
        return f'ps1_gte(c, {w:#010x}u);'
    if op in (8, 9): return cset(rt, f'{cr(rs)} + {cu(s)}')
    if op == 10: return cset(rt, f'(uint32_t)((int32_t){cr(rs)} < {s})')
    if op == 11: return cset(rt, f'(uint32_t)({cr(rs)} < {cu(s)})')
    if op == 12: return cset(rt, f'{cr(rs)} & {imm:#06x}u')
    if op == 13: return cset(rt, f'{cr(rs)} | {imm:#06x}u')
    if op == 14: return cset(rt, f'{cr(rs)} ^ {imm:#06x}u')
    if op == 15: return cset(rt, cu(imm << 16))
    addr = f'{cr(rs)} + {cu(s)}'
    if op == 32: return cset(rt, f'(uint32_t)(int32_t)(int8_t)ps1_r8(c, {addr})')
    if op == 33: return cset(rt, f'(uint32_t)(int32_t)(int16_t)ps1_r16(c, {addr})')
    if op == 35: return cset(rt, f'ps1_r32(c, {addr})')
    if op == 36: return cset(rt, f'(uint32_t)ps1_r8(c, {addr})')
    if op == 37: return cset(rt, f'(uint32_t)ps1_r16(c, {addr})')
    if op == 34: return cset(rt, f'ps1_lwl(c, {addr}, {cr(rt)})')
    if op == 38: return cset(rt, f'ps1_lwr(c, {addr}, {cr(rt)})')
    if op == 40: return f'ps1_w8(c, {addr}, (uint8_t){cr(rt)});'
    if op == 41: return f'ps1_w16(c, {addr}, (uint16_t){cr(rt)});'
    if op == 43: return f'ps1_w32(c, {addr}, {cr(rt)});'
    if op == 42: return f'ps1_swl(c, {addr}, {cr(rt)});'
    if op == 46: return f'ps1_swr(c, {addr}, {cr(rt)});'
    if op in (1, 2, 3, 4, 5, 6, 7):
        return None
    raise ValueError(f'op {op} at {pc:#x} ({w:#010x})')


def branch_info_c(w, pc):
    """Igual que branch_info, con la condicion formateada en C. Los `kind` y los
    destinos son identicos, asi que los leaders salen iguales en los dos backends."""
    op = w >> 26
    rs, rt = (w >> 21) & 31, (w >> 16) & 31
    s = sx(w & 0xFFFF)
    tgt = (pc + 4 + s * 4) & 0xFFFFFFFF
    if op == 0 and (w & 63) == 8:
        return ('jr', rs, None)
    if op == 0 and (w & 63) == 9:
        return ('jalr', rs, (w >> 11) & 31)
    if op == 0 and (w & 63) == 12:
        return ('syscall', None, None)
    if op == 2:
        return ('j', None, (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    if op == 3:
        return ('jal', None, (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    if op == 4:
        return ('b', f'{cr(rs)} == {cr(rt)}', tgt)
    if op == 5:
        return ('b', f'{cr(rs)} != {cr(rt)}', tgt)
    if op == 6:
        return ('b', f'(int32_t){cr(rs)} <= 0', tgt)
    if op == 7:
        return ('b', f'(int32_t){cr(rs)} > 0', tgt)
    if op == 1:
        cond = f'(int32_t){cr(rs)} >= 0' if rt & 1 else f'(int32_t){cr(rs)} < 0'
        if rt & 16:
            return ('bal', cond, tgt)
        return ('b', cond, tgt)
    return None


def call_expr_c(t, known):
    return f'f_{t:08x}(c);' if t in known and t not in HOOK_SET else f'ps1_call(c, {cu(t)});'


def gen_function_c(f, known, comments=True):
    end = func_end(f)
    words = {pc: e.word(pc) for pc in range(f, end, 4)}
    has_indirect = any((w >> 26) == 0 and (w & 63) == 8 and ((w >> 21) & 31) != 31
                       for w in words.values())
    leaders = {f}
    for pc, w in words.items():
        bi = branch_info_c(w, pc)
        if bi:
            kind = bi[0]
            if kind in ('b', 'bal') and f <= bi[2] < end:
                leaders.add(bi[2])
            if kind == 'j' and f <= bi[2] < end:
                leaders.add(bi[2])
            leaders.add(pc + 8)
    leaders |= {x for x in ENTRY_POINTS if f <= x < end}
    if has_indirect:
        leaders |= set(words)
    leaders = sorted(x for x in leaders if f <= x < end)
    lset = set(leaders)

    def jump(target):
        """Arista con destino constante: goto si el destino es leader; si no, el
        control sale de la extension trazada, igual que el brazo `_` de Rust."""
        if target in lset:
            return f'goto L_{target:08x};'
        return f'pc = {cu(target)}; goto L_out;'

    ind = '    ' if not has_indirect else '        '
    out = []
    if f in NAMES:
        n, dev = NAMES[f]
        out.append(f'/* `{n}`' + (f' ({SRC[dev]})' if dev in SRC else '') + ' */')
    out.append(f'void f_{f:08x}(Cpu *c) {{ f_{f:08x}_at(c, {cu(f)}); }}')
    out.append('')
    out.append(f'void f_{f:08x}_at(Cpu *c, uint32_t pc)')
    out.append('{')
    out.append('    uint32_t t, cnd, off;')
    out.append('    (void)t; (void)cnd; (void)off;')
    out.append(f'    ps1_push(c, {cu(f)});')
    if has_indirect:
        # Todas las instrucciones son leaders: indices densos (pc - base) >> 2 para que
        # gcc emita una tabla de N entradas en vez de una de 4N. La guarda `off & 3` es
        # obligatoria: sin ella, pc, pc+1, pc+2 y pc+3 colapsan en el mismo caso y un
        # `jr` desalineado iria al bloque equivocado en vez de salir.
        out.append('    for (;;) {')
        out.append(f'        off = pc - {cu(f)};')
        out.append('        if (off & 3u) goto L_out;')
        out.append('        switch (off >> 2) {')
        for x in leaders:
            out.append(f'        case {(x - f) // 4}: goto L_{x:08x};')
        out.append('        default: goto L_out;')
        out.append('        }')
    else:
        out.append('    switch (pc) {')
        for x in leaders:
            out.append(f'    case {cu(x)}: goto L_{x:08x};')
        out.append('    default: goto L_out;')
        out.append('    }')

    for li, start in enumerate(leaders):
        stop = leaders[li + 1] if li + 1 < len(leaders) else end
        out.append(f'L_{start:08x}:')
        pc = start
        terminated = False
        while pc < stop:
            w = words[pc]
            bi = branch_info_c(w, pc)
            cm = f'  /* {pc:#x}: {mips.dis1(w, pc)} */' if comments else ''
            if bi is None:
                code = gen_simple_c(w, pc)
                if code:
                    out.append(f'{ind}{code}{cm}')
                pc += 4
                continue
            kind = bi[0]
            # (un branch al final del rango conserva su slot de retardo, aunque ahi
            # empiece la funcion siguiente: se ejecuta antes del salto de todas formas)
            delay_w = words.get(pc + 4, e.word(pc + 4))
            delay = gen_simple_c(delay_w, pc + 4) or ''
            dl = f'{ind}{delay}  /* delay {pc + 4:#x} */' if delay else ''
            if kind == 'b':
                out.append(f'{ind}cnd = ({bi[1]});{cm}')
                if dl: out.append(dl)
                tick = 'ps1_tick(c); ' if bi[2] <= pc else ''
                out.append(f'{ind}if (cnd) {{ {tick}{jump(bi[2])} }}')
                out.append(f'{ind}{jump(pc + 8)}')
            elif kind == 'bal':
                raise ValueError(f'bltzal/bgezal at {pc:#x}')
            elif kind == 'j':
                if dl: out.append(dl)
                if f <= bi[2] < end:
                    tick = 'ps1_tick(c); ' if bi[2] <= pc else ''
                    out.append(f'{ind}{tick}{jump(bi[2])}{cm}')
                else:  # tail call
                    out.append(f'{ind}ps1_pop(c); {call_expr_c(bi[2], known)} return;{cm}')
            elif kind == 'jal':
                if dl: out.append(dl)
                out.append(f'{ind}c->r[31] = {cu(pc + 8)};{cm}')
                out.append(f'{ind}{call_expr_c(bi[2], known)}')
                out.append(f'{ind}if (c->cont) ps1_run_cont(c);')
                out.append(f'{ind}{jump(pc + 8)}')
            elif kind == 'jr':
                rs = bi[1]
                out.append(f'{ind}t = {cr(rs)};{cm}')
                if dl: out.append(dl)
                if rs == 31:
                    out.append(f'{ind}ps1_pop(c); return;')
                else:
                    out.append(f'{ind}if (t >= {cu(f)} && t < {cu(end)}) {{ pc = t; continue; }}')
                    out.append(f'{ind}ps1_pop(c); ps1_call(c, t); return;')
            elif kind == 'jalr':
                rs, rd = bi[1], bi[2]
                out.append(f'{ind}t = {cr(rs)};{cm}')
                if dl: out.append(dl)
                if rd: out.append(f'{ind}c->r[{rd}] = {cu(pc + 8)};')
                out.append(f'{ind}ps1_call(c, t); if (c->cont) ps1_run_cont(c);')
                out.append(f'{ind}{jump(pc + 8)}')
            elif kind == 'syscall':
                out.append(f'{ind}ps1_syscall(c);{cm}')
                pc += 4
                continue
            terminated = True
            pc += 8
            break
        if not terminated:
            out.append(f'{ind}{jump(stop)}')
    # el control salio de la extension trazada (cae en la funcion vecina): continuacion
    out.append('L_out:')
    out.append(f'{ind}ps1_pop(c); c->cont = pc; return;')
    if has_indirect:
        out.append('    }')
    out.append('}')
    out.append('')
    return '\n'.join(out)


def safe_gen_c(f, known, comments=True):
    try:
        return gen_function_c(f, known, comments)
    except ValueError:
        return (f'void f_{f:08x}(Cpu *c) {{ ps1_panic(c, "f_{f:08x}: not code"); }}\n'
                f'void f_{f:08x}_at(Cpu *c, uint32_t pc) '
                f'{{ (void)pc; ps1_panic(c, "f_{f:08x}: not code"); }}\n')


def emit_c(funcs, known, out_dir, instrs_per_part=4000, comments=True):
    """Escribe funcs.h, part_XX.c, dispatch.c, parts.mk y manifest.json.

    El reparto va por instrucciones, no por numero de funciones como en el backend
    Rust: ahi las 48 partes quedan muy desbalanceadas porque una carga con la funcion
    de 3207 instrucciones."""
    os.makedirs(out_dir, exist_ok=True)
    banner = ('/* Generado por dc/tools/recomp/recomp.py desde NFS4.EXE '
              '(NTSC-U SLUS-00826). No editar a mano. */')

    # --- reparto por instrucciones -----------------------------------------
    parts, cur, cur_instrs = [], [], 0
    for f in funcs:
        n = (func_end(f) - f) // 4
        if cur and cur_instrs + n > instrs_per_part:
            parts.append(cur); cur, cur_instrs = [], 0
        cur.append(f); cur_instrs += n
    if cur:
        parts.append(cur)

    # --- funcs.h -----------------------------------------------------------
    h = [banner, '#ifndef NFS4RC_FUNCS_H', '#define NFS4RC_FUNCS_H', '',
         '#include "ps1rt.h"', '',
         'typedef void (*ps1_at_fn)(Cpu *c, uint32_t pc);', '',
         '/* Extensiones de las funciones recompiladas, en SoA: la busqueda binaria',
         '   recorre solo ps1_range_start, que son 4 bytes por entrada. */',
         'extern const uint32_t  ps1_range_start[];',
         'extern const uint32_t  ps1_range_end[];',
         'extern const ps1_at_fn ps1_range_at[];',
         'extern const uint32_t  ps1_range_n;',
         '/* Funciones que reemplaza el host (excluidas de la recompilacion). */',
         'extern const uint32_t  ps1_hle[];',
         'extern const uint32_t  ps1_hle_n;', '',
         'int ps1_dispatch(Cpu *c, uint32_t addr);',
         'int ps1_dispatch_at(Cpu *c, uint32_t pc);', '']
    for f in funcs:
        h.append(f'void f_{f:08x}(Cpu *c);')
        h.append(f'void f_{f:08x}_at(Cpu *c, uint32_t pc);')
    h += ['', '#endif /* NFS4RC_FUNCS_H */', '']
    open(os.path.join(out_dir, 'funcs.h'), 'w', encoding='utf-8').write('\n'.join(h))

    # --- part_XX.c ---------------------------------------------------------
    manifest, stubs = {}, 0
    for k, sub in enumerate(parts):
        body = [banner, '', '#include "funcs.h"', '']
        for f in sub:
            src = safe_gen_c(f, known, comments)
            is_stub = src.startswith(f'void f_{f:08x}(Cpu *c) {{ ps1_panic')
            stubs += is_stub
            manifest[f'{f:#010x}'] = {
                'start': f, 'end': func_end(f), 'instrs': (func_end(f) - f) // 4,
                'part': k, 'stub': is_stub,
            }
            body.append(src)
        open(os.path.join(out_dir, f'part_{k:02}.c'), 'w', encoding='utf-8').write('\n'.join(body))

    # --- dispatch.c --------------------------------------------------------
    d = [banner, '', '#include "funcs.h"', '']
    d.append('const uint32_t ps1_hle[] = {')
    d += ['    ' + ', '.join(f'{x:#010x}u' for x in sorted(HLE_SET)[i:i + 6]) + ','
          for i in range(0, len(HLE_SET), 6)]
    d += ['};', f'const uint32_t ps1_hle_n = {len(HLE_SET)}u;', '']
    d.append('const uint32_t ps1_range_start[] = {')
    d += ['    ' + ', '.join(f'{x:#010x}u' for x in funcs[i:i + 6]) + ','
          for i in range(0, len(funcs), 6)]
    d += ['};', '']
    ends = [func_end(f) for f in funcs]
    d.append('const uint32_t ps1_range_end[] = {')
    d += ['    ' + ', '.join(f'{x:#010x}u' for x in ends[i:i + 6]) + ','
          for i in range(0, len(ends), 6)]
    d += ['};', '']
    d.append('const ps1_at_fn ps1_range_at[] = {')
    d += [f'    f_{f:08x}_at,' for f in funcs]
    d += ['};', f'const uint32_t ps1_range_n = {len(funcs)}u;', '']
    d += [
        '/* Continua la ejecucion en una direccion arbitraria dentro de una funcion',
        '   recompilada: la que empieza mas tarde y la contiene. El limite de 8 rangos',
        '   hacia atras replica el `.take(8)` del backend Rust, fallos incluidos: si',
        '   aqui buscasemos mas lejos, acertariamos donde la referencia falla y el test',
        '   diferencial divergiria. */',
        'int ps1_dispatch_at(Cpu *c, uint32_t pc)',
        '{',
        '    uint32_t lo = 0u, hi = ps1_range_n, m, n, k, i;',
        '    while (lo < hi) {            /* == partition_point(start <= pc) */',
        '        m = lo + ((hi - lo) >> 1);',
        '        if (ps1_range_start[m] <= pc) lo = m + 1u; else hi = m;',
        '    }',
        '    n = (lo < 8u) ? lo : 8u;',
        '    for (k = 1u; k <= n; k++) {',
        '        i = lo - k;',
        '        if (pc < ps1_range_end[i]) { ps1_range_at[i](c, pc); return 1; }',
        '    }',
        '    return 0;',
        '}',
        '',
        '/* Despacha una llamada por direccion. Los inicios son unicos y, por',
        '   construccion, f_X(c) hace f_X_at(c, X): una busqueda binaria exacta sobre',
        '   ps1_range_start equivale al `match addr` del backend Rust. */',
        'int ps1_dispatch(Cpu *c, uint32_t addr)',
        '{',
        '    uint32_t lo = 0u, hi = ps1_range_n, m;',
        '    while (lo < hi) {',
        '        m = lo + ((hi - lo) >> 1);',
        '        if (ps1_range_start[m] < addr) lo = m + 1u;',
        '        else if (ps1_range_start[m] > addr) hi = m;',
        '        else { ps1_range_at[m](c, addr); return 1; }',
        '    }',
        '    return 0;',
        '}',
        '']
    open(os.path.join(out_dir, 'dispatch.c'), 'w', encoding='utf-8').write('\n'.join(d))

    # --- parts.mk y manifest.json ------------------------------------------
    mk = ['# Generado por recomp.py. Incluir desde el Makefile para compilar con -j.',
          'GEN_SRCS := \\']
    mk += [f'\tpart_{k:02}.c \\' for k in range(len(parts))]
    mk += ['\tdispatch.c', '']
    open(os.path.join(out_dir, 'parts.mk'), 'w', encoding='utf-8').write('\n'.join(mk))
    import json
    with open(os.path.join(out_dir, 'manifest.json'), 'w', encoding='utf-8') as fh:
        json.dump({'funcs': len(funcs), 'parts': len(parts), 'stubs': stubs,
                   'instrs_per_part': instrs_per_part, 'comments': comments,
                   'by_func': manifest}, fh, indent=1)
    return len(parts), stubs


if __name__ == '__main__':
    # usage: recomp.py OUT_DIR [--lang rust|c] [--all | ROOTS...] [--hle ADDRS...]
    #                          [--parts N | --instrs-per-part N] [--no-comments]
    out_dir = sys.argv[1]
    args = sys.argv[2:]
    if '--hle' in args:
        i = args.index('--hle')
        HLE_SET.update(int(x, 16) for x in args[i + 1:])
        args = args[:i]
    for line in open(os.path.join(UPSTREAM_TOOLS, 'hle_funcs.txt')):
        if line.strip() and not line.startswith('#'):
            HLE_SET.add(int(line.split()[0], 16))
    lang = 'rust'
    if '--lang' in args:
        i = args.index('--lang'); lang = args[i + 1]; args = args[:i] + args[i + 2:]
    comments = True
    if '--no-comments' in args:
        comments = False; args = [a for a in args if a != '--no-comments']
    instrs_per_part = 4000
    if '--instrs-per-part' in args:
        i = args.index('--instrs-per-part')
        instrs_per_part = int(args[i + 1]); args = args[:i] + args[i + 2:]
    parts_n = 48
    if '--parts' in args:
        i = args.index('--parts'); parts_n = int(args[i + 1]); args = args[:i] + args[i + 2:]
    roots = all_starts() if '--all' in args else [int(x, 16) for x in args]
    if lang == 'c':
        roots = sorted(set(roots) | C_EXTRA_STARTS)
    funcs = collect(roots)
    known = set(funcs)
    collect_entry_points(funcs)
    if lang == 'c':
        # los cuerpos HLE van al despacho, pero no a `known`: se llaman por ps1_call
        funcs = sorted(set(funcs) | C_HLE_BODIES)
        collect_entry_points(funcs)
        n_parts, stubs = emit_c(funcs, known, out_dir, instrs_per_part, comments)
        print(f'{len(funcs)} functions -> {out_dir} ({n_parts} parts, {stubs} stubs, C)')
        sys.exit(0)
    if lang != 'rust':
        sys.exit(f'--lang desconocido: {lang} (usa rust o c)')
    os.makedirs(out_dir, exist_ok=True)
    header = ['//! Generated by tools/recomp.py from NFS4.EXE (NTSC-U SLUS-00826). Do not edit by hand.',
              '#![allow(unused_variables, unused_assignments, unused_mut, clippy::all, unreachable_code, non_snake_case, unused_imports)]',
              'use ps1rt::Cpu;', 'use crate::*;', '']
    chunk = (len(funcs) + parts_n - 1) // parts_n
    for k in range(parts_n):
        sub = funcs[k * chunk:(k + 1) * chunk]
        body = header + [safe_gen(f, known) for f in sub]
        open(os.path.join(out_dir, f'part_{k:02}.rs'), 'w', encoding='utf-8').write(chr(10).join(body))
    lib = ['//! NFS4.EXE recompiled to Rust (generated by tools/recomp.py). Do not edit by hand.',
           '#![allow(clippy::all)]',
           'use ps1rt::Cpu;']
    for k in range(parts_n):
        lib.append(f'mod part_{k:02};')
        lib.append(f'pub use part_{k:02}::*;')
    lib.append('/// Functions replaced on the host side (excluded from recompilation).')
    lib.append('pub const HLE: &[u32] = &[' + ', '.join(f'{x:#x}' for x in sorted(HLE_SET)) + '];')
    lib.append('/// Extents of all recompiled functions: (start, end, entry-at-pc).')
    lib.append('pub static RANGES: &[(u32, u32, fn(&mut Cpu, u32))] = &[')
    for f in funcs:
        lib.append(f'    ({f:#x}, {func_end(f):#x}, f_{f:08x}_at),')
    lib.append('];')
    lib.append('/// Continue execution at an arbitrary address inside a recompiled function.')
    lib.append('pub fn dispatch_at(c: &mut Cpu, pc: u32) -> bool {')
    lib.append('    // innermost (latest-starting) function whose extent contains pc')
    lib.append('    let i = RANGES.partition_point(|r| r.0 <= pc);')
    lib.append('    for r in RANGES[..i].iter().rev().take(8) {')
    lib.append('        if pc < r.1 { (r.2)(c, pc); return true; }')
    lib.append('    }')
    lib.append('    false')
    lib.append('}')
    lib.append('/// Dispatch a call by address into recompiled code.')
    lib.append('pub fn dispatch(c: &mut Cpu, addr: u32) -> bool {')
    lib.append('    match addr {')
    for f in funcs:
        lib.append(f'        {f:#x} => f_{f:08x}(c),')
    lib.append('        _ => return false,')
    lib.append('    }')
    lib.append('    true')
    lib.append('}')
    open(os.path.join(out_dir, 'lib.rs'), 'w', encoding='utf-8').write(chr(10).join(lib))
    print(f'{len(funcs)} functions -> {out_dir} ({parts_n} parts)')
