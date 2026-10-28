"""M4: la reverb del SPU de PS1 como microprograma del DSP del AICA.

Construye el programa (MPRO), dice como se calculan sus coeficientes (COEF) y direcciones
(MADRS) a partir de los registros de reverb del SPU, lo prueba contra la reverb de PS1 y
escribe la cabecera C que usa el Dreamcast.

  - La reverb de PS1 (psx-spx; la de dc/src/host/psxspu.c, que es la de la referencia) va a
    22 050 Hz. El DSP del AICA corre a 44 100 Hz. Con TODOS los retardos al doble (tambien el
    `[m - 2]` de los filtros IIR, que pasa a 2 muestras), el DSP ejecuta dos copias exactas del
    sistema de PS1, una en las muestras pares y otra en las impares, con los mismos
    coeficientes. El anillo del DSP es de 8 K palabras; el retardo maximo del juego, 7 968.
  - La semantica del DSP es la de Flycast (core/hw/aica/dsp_interp.cpp), que es donde se
    prueba; coincide con la de MAME (aicadsp.cpp) salvo en detalles que aqui no se usan.
  - Entrada: todas las voces con reverb (EON) van al mismo MIXS (mono); PS1 la tiene en
    estereo segun el panorama de cada voz. Es la unica aproximacion.

Uso (desde la raiz):
  python -I dc/tools/dsp/reverb_aica.py --prueba     compara con la de PS1
  python -I dc/tools/dsp/reverb_aica.py --cabecera F escribe la cabecera C
"""
import math
import random
import sys

RBL = 8192                        # palabras del anillo del DSP (RBL = 0)
# Las dos copias del sistema de PS1 (pares e impares) reciben cada una la muestra entera y
# producen salida en cada muestra; en PS1 entra la media de cada par y la salida se mantiene
# dos muestras: el doble de energia. Medido con --prueba: +3 dB sin esta correccion.
ENTRADA = 0.70710678

# Los registros de reverb que pone el juego (traza de una carrera, M4.md seccion 10): el
# preset "Studio Small". Indices de 0x1C0 + 2 i.
PRESET = [0x0033, 0x0025, 0x70f0, 0x4fa8, 0xbce0, 0x4410, 0xc0f0, 0x9c00, 0x5280, 0x4ec0,
          0x03e4, 0x031b, 0x03a4, 0x02af, 0x0372, 0x0266, 0x031c, 0x025d, 0x025c, 0x018e,
          0x022f, 0x0135, 0x01d2, 0x00b7, 0x018f, 0x00b5, 0x00b4, 0x0080, 0x004c, 0x0026,
          0x8000, 0x8000]
VLOUT = 0x2040                    # 0x184/0x186 en carrera

(dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2,
 mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF,
 mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2,
 vLIN, vRIN) = range(32)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


# ---- formulas: COEF y MADRS a partir de los registros (las mismas en C) ----
# COEF: ('reg', i) = v[i]; ('mul', i, j) = v[i] * v[j]; ('entrada', i, j) = v[i]*v[j]/raiz(2);
#       ('1-', i) = 1 - v[i];
#       ('neg', i) = -v[i]; ('uno',) = 4095/4096; ('cero',); ('out', lado) = vLOUT/vROUT
#       entre el volumen principal (en PS1 la reverb sale sin el; aqui entra con el) y entre
#       16 (la escala de dentro del DSP es la de PS1, 16 veces la de las voces).
# MADRS: (i, extra): -(v[i] * 8) + extra, o (i, j, extra): -(v[i] - v[j]) * 8 + extra, en el
#        anillo. Los desplazamientos de PS1 son bytes a 22 kHz = muestras a 44,1 kHz, y el
#        sentido se invierte (en PS1 la posicion sube; en el AICA, MDEC_CT baja).
def coef_valor(f, r, out=(VLOUT, VLOUT), main=(0x7FFF, 0x7FFF)):
    """Valor de 13 bits (1,0 = 4096) del coeficiente."""
    v = [s16(x) for x in r]
    t = f[0]
    if t == 'reg':
        x = v[f[1]] / 32768
    elif t == 'mul':
        x = v[f[1]] * v[f[2]] / (32768 * 32768)
    elif t == 'entrada':
        x = v[f[1]] * v[f[2]] / (32768 * 32768) * ENTRADA
    elif t == '1-':
        x = 1 - v[f[1]] / 32768
    elif t == 'neg':
        x = -v[f[1]] / 32768
    elif t == 'uno':
        return 4095
    elif t == 'cero':
        return 0
    elif t == 'out':
        x = s16(out[f[1]]) / 32768 * 32767 / max(main[f[1]], 1) / 16
    else:
        raise ValueError(f)
    return max(-4096, min(4095, int(round(x * 4096))))


def madrs_valor(f, r):
    if len(f) == 2:
        off = r[f[0]] * 8
    else:
        off = (r[f[0]] - r[f[1]]) * 8
    return (-off + f[-1]) % RBL


# ---- el ensamblador ----
class Programa:
    def __init__(self):
        self.pasos = []           # dicts de campos
        self.coefs = []           # formula de cada paso (o ('cero',))
        self.madrs = []           # formulas de MADRS
        self.iwt = {}             # paso -> slot de MEMS que recoge la lectura de paso - 2
        self.temps = 0

    def _paso(self, campos, coef=('cero',)):
        s = len(self.pasos)
        c = dict(campos)
        if s in self.iwt:
            c['IWT'] = 1
            c['IWA'] = self.iwt.pop(s)
        self.pasos.append(c)
        self.coefs.append(coef)
        return s

    def relleno(self):
        # no vacio: Flycast trata el paso todo-cero como un atajo que toca ACC
        return self._paso({'ZERO': 1})

    def a_impar(self):
        if len(self.pasos) % 2 == 0:
            self.relleno()

    def a_par(self):
        if len(self.pasos) % 2 == 1:
            self.relleno()

    def temp(self):
        self.temps += 1
        return self.temps - 1

    def lee(self, madrs_f, slot):
        """[madrs] -> MEMS[slot] (utilizable desde 3 pasos despues)."""
        self.a_impar()
        masa = len(self.madrs)
        self.madrs.append(madrs_f)
        s = self._paso({'MRD': 1, 'MASA': masa, 'ZERO': 1})
        self.iwt[s + 2] = slot
        return s

    def acc(self, x, coef, b=None):
        """ACC = X * coef + B.  x: ('mems', k) | ('mixs', k) | ('temp', t);
        b: None | 'acc' | ('temp', t)."""
        c = {}
        if x[0] == 'temp':
            c['TRA'] = x[1]
        else:
            c['XSEL'] = 1
            c['IRA'] = x[1] if x[0] == 'mems' else 0x20 + x[1]
        c['YSEL'] = 1
        if b is None:
            c['ZERO'] = 1
        elif b == 'acc':
            c['BSEL'] = 1
        else:
            assert x[0] != 'temp' or x[1] == b[1], 'X y B de TEMP comparten TRA'
            c['TRA'] = b[1]
        return self._paso(c, coef)

    def guarda(self, temp=None, efreg=None, x2=False):
        """El ACC del paso anterior (SHIFTED, saturado a 24 bits; x2: al doble) a TEMP y/o
        EFREG."""
        c = {'SHIFT': 1} if x2 else {}
        if temp is not None:
            c['TWT'] = 1
            c['TWA'] = temp
        if efreg is not None:
            c['EWT'] = 1
            c['EWA'] = efreg
        return self._paso(c)

    def escribe(self, temp, madrs_f):
        """TEMP[temp] -> [madrs] (copia exacta: ACC = 0 + TEMP, y el paso impar siguiente
        escribe SHIFTED)."""
        self.a_par()
        self._paso({'TRA': temp, 'YSEL': 1}, ('cero',))      # ACC = TEMP*0 + TEMP
        masa = len(self.madrs)
        self.madrs.append(madrs_f)
        self._paso({'MWT': 1, 'MASA': masa, 'ZERO': 1})

    def fin(self):
        while self.iwt:
            self.relleno()
        assert len(self.pasos) <= 128, len(self.pasos)
        assert len(self.madrs) <= 32, len(self.madrs)
        assert self.temps <= 128


def construye():
    p = Programa()
    # 1. lecturas (todas son de muestras anteriores: no hay lectura y escritura de la misma
    #    direccion en la misma muestra con los registros del juego)
    lineas = [(mLSAME, dLSAME, 0), (mRSAME, dRSAME, 1), (mLDIFF, dRDIFF, 0), (mRDIFF, dLDIFF, 1)]
    slot = 0
    lec = {}
    for k, (tgt, src, lado) in enumerate(lineas):
        lec[('d', k)] = slot; p.lee((src, 0), slot); slot += 1
        lec[('p', k)] = slot; p.lee((tgt, 2), slot); slot += 1
    combs = [[mLCOMB1, mLCOMB2, mLCOMB3, mLCOMB4], [mRCOMB1, mRCOMB2, mRCOMB3, mRCOMB4]]
    for lado in range(2):
        for k, m in enumerate(combs[lado]):
            lec[('c', lado, k)] = slot; p.lee((m, 0), slot); slot += 1
    apfs = [[(mLAPF1, dAPF1), (mLAPF2, dAPF2)], [(mRAPF1, dAPF1), (mRAPF2, dAPF2)]]
    for lado in range(2):
        for k, (m, d) in enumerate(apfs[lado]):
            lec[('a', lado, k)] = slot; p.lee((m, d, 0), slot); slot += 1
    # 2. la entrada x16 con saturacion (cuatro x2): el fondo de escala del DSP (24 bits) pasa
    #    a ser el de PS1 (16 bits), y el DSP satura donde satura PS1. Llega como muestra * 16.
    g = p.temp()
    p.acc(('mixs', 0), ('uno',))
    p.guarda(temp=g, x2=True)
    for _ in range(3):
        p.acc(('temp', g), ('uno',))
        p.guarda(temp=g, x2=True)
    # 3. SAME y DIFF: y = iir*vIN*in + iir*wall*[d] + (1-iir)*[prev]
    vin = [vLIN, vRIN]
    for k, (tgt, src, lado) in enumerate(lineas):
        p.acc(('temp', g), ('entrada', vIIR, vin[lado]))
        p.acc(('mems', lec[('d', k)]), ('mul', vIIR, vWALL), 'acc')
        p.acc(('mems', lec[('p', k)]), ('1-', vIIR), 'acc')
        t = p.temp()
        p.guarda(temp=t)
        p.escribe(t, (tgt, 0))
    # 4. por lado: combs, dos all-pass y la salida (/16: vuelve a la escala de las voces)
    for lado in range(2):
        vc = [vCOMB1, vCOMB2, vCOMB3, vCOMB4]
        for k in range(4):
            p.acc(('mems', lec[('c', lado, k)]), ('reg', vc[k]), None if k == 0 else 'acc')
        o = p.temp()
        p.guarda(temp=o)
        for k, (m, d) in enumerate(apfs[lado]):
            va = [vAPF1, vAPF2][k]
            a = ('mems', lec[('a', lado, k)])
            p.acc(a, ('neg', va), ('temp', o))        # t = o - vAPF*[m - d]
            t = p.temp()
            p.guarda(temp=t)
            p.escribe(t, (m, 0))                      # [m] = t
            p.acc(a, ('uno',))                        # o = t*vAPF + [m - d]
            p.acc(('temp', t), ('reg', va), 'acc')
            o = p.temp()
            p.guarda(temp=o)
        p.acc(('temp', o), ('out', lado))
        p.guarda(efreg=lado)
    p.fin()
    return p


# ---- codificacion (4 palabras de 16 bits por paso; el AICA las tiene a 4 bytes) ----
def codifica(c):
    g = lambda k: c.get(k, 0)
    w0 = (g('TRA') << 9) | (g('TWT') << 8) | (g('TWA') << 1)
    w1 = (g('XSEL') << 15) | (g('YSEL') << 13) | (g('IRA') << 7) | (g('IWT') << 6) | (g('IWA') << 1)
    w2 = ((g('TABLE') << 15) | (g('MWT') << 14) | (g('MRD') << 13) | (g('EWT') << 12) |
          (g('EWA') << 8) | (g('ADRL') << 7) | (g('FRCL') << 6) | (g('SHIFT') << 4) |
          (g('YRL') << 3) | (g('NEGB') << 2) | (g('ZERO') << 1) | g('BSEL'))
    w3 = (g('MASA') << 9) | (g('ADREB') << 8) | (g('NXADR') << 7)
    return [w0, w1, w2, w3]


# ---- el DSP del AICA (Flycast, core/hw/aica/dsp_interp.cpp) ----
def pack(val):
    sign = (val >> 23) & 1
    temp = (val ^ (val << 1)) & 0xFFFFFF
    exponent = 0
    for _ in range(12):
        if temp & 0x800000:
            break
        temp <<= 1
        exponent += 1
    if exponent < 12:
        val = (val << exponent) & 0x3FFFFF
    else:
        val <<= 11
    val >>= 11
    val &= 0x7FF
    val |= sign << 15
    val |= exponent << 11
    return val & 0xFFFF


def unpack(val):
    sign = (val >> 15) & 1
    exponent = (val >> 11) & 0xF
    mantissa = val & 0x7FF
    uval = mantissa << 11
    if exponent > 11:
        exponent = 11
        uval |= sign << 22
    else:
        uval |= (sign ^ 1) << 22
    uval |= sign << 23
    uval &= 0xFFFFFF
    if uval & 0x800000:
        uval -= 0x1000000
    return uval >> exponent


def sx24(v):
    v &= 0xFFFFFF
    return v - 0x1000000 if v & 0x800000 else v


class DSP:
    def __init__(self, mpro, coef, madrs):
        self.mpro, self.coef, self.madrs = mpro, coef, madrs
        self.mem = [pack(0)] * RBL        # el cero del formato flotante es 0x6000, no 0
        self.temp = [0] * 128
        self.mems = [0] * 32
        self.ct = RBL

    def muestra(self, mixs0):
        ACC = SHIFTED = FRC = Y_REG = ADRS = 0
        MEMVAL = [0] * 4
        efreg = [0] * 16
        mixs = [0] * 16
        mixs[0] = mixs0
        for step in range(128):
            w = self.mpro[step]
            if w == [0, 0, 0, 0]:
                X = sx24(self.temp[self.ct & 0x7F])
                ACC = ((X * FRC) >> 12) + X
                continue
            TRA = (w[0] >> 9) & 0x7F; TWT = w[0] & 0x100; TWA = (w[0] >> 1) & 0x7F
            XSEL = w[1] & 0x8000; YSEL = (w[1] >> 13) & 3; IRA = (w[1] >> 7) & 0x3F
            IWT = w[1] & 0x40; IWA = (w[1] >> 1) & 0x1F
            EWT = w[2] & 0x1000; EWA = (w[2] >> 8) & 0xF; SHIFT = (w[2] >> 4) & 3
            NEGB = w[2] & 4; ZERO = w[2] & 2; BSEL = w[2] & 1
            if IRA <= 0x1F:
                INPUTS = self.mems[IRA]
            elif IRA <= 0x2F:
                INPUTS = sx24(mixs[IRA - 0x20] << 4)
            else:
                INPUTS = 0
            if IWT:
                self.mems[IWA] = MEMVAL[step & 3]
            if not ZERO:
                B = ACC if BSEL else sx24(self.temp[(TRA + self.ct) & 0x7F])
                if NEGB:
                    B = -B
            else:
                B = 0
            X = INPUTS if XSEL else sx24(self.temp[(TRA + self.ct) & 0x7F])
            if YSEL == 1:
                c = self.coef[step] & 0xFFFF
                c = c - 0x10000 if c & 0x8000 else c
                Y = c >> 3
            elif YSEL == 0:
                Y = FRC
            else:
                Y = 0
            SHIFTED = ACC if SHIFT in (0, 3) else ACC << 1
            if SHIFT < 2:
                SHIFTED = max(-0x800000, min(0x7FFFFF, SHIFTED))
            ACC = ((X * Y) >> 12) + B
            if TWT:
                self.temp[(TWA + self.ct) & 0x7F] = SHIFTED
            if step & 1:
                MWT = w[2] & 0x4000; MRD = w[2] & 0x2000
                if MRD or MWT:
                    MASA = (w[3] >> 9) & 0x3F
                    ADDR = (self.madrs[MASA] + self.ct) & (RBL - 1)
                    if MRD:
                        MEMVAL[(step + 2) & 3] = unpack(self.mem[ADDR])
                    if MWT:
                        self.mem[ADDR] = pack(SHIFTED & 0xFFFFFF)
            if EWT:
                efreg[EWA] = SHIFTED >> 8
        self.ct -= 1
        if self.ct == 0:
            self.ct = RBL
        return efreg[0], efreg[1]


def tablas(p, r, out=(VLOUT, VLOUT), main=(0x7FFF, 0x7FFF)):
    mpro = [codifica(c) for c in p.pasos] + [[0, 0, 0, 0]] * (128 - len(p.pasos))
    coef = [(coef_valor(f, r, out, main) << 3) & 0xFFFF for f in p.coefs] + [0] * (128 - len(p.coefs))
    madrs = [madrs_valor(f, r) for f in p.madrs] + [0] * (64 - len(p.madrs))
    return mpro, coef, madrs


# ---- la reverb de PS1 (dc/src/host/psxspu.c, reverb()) ----
class PS1:
    def __init__(self, r, out=(VLOUT, VLOUT)):
        self.r = r
        self.size = 0x1F40 if True else 0          # mBASE 0x7E0C0 del juego
        self.ram = [0] * (self.size // 2)
        self.pos = 0
        self.inl = self.inr = 0
        self.odd = False
        self.out = [0, 0]
        self.vout = out

    def rv(self, off):
        return self.ram[((self.pos + off) % self.size) // 2]

    def rvw(self, off, v):
        self.ram[((self.pos + off) % self.size) // 2] = max(-32768, min(32767, v))

    def paso(self):
        r = self.r
        M = lambda i: r[i] << 3
        V = lambda i: s16(r[i])
        MUL = lambda a, b: (a * b) >> 15
        lin, rin = MUL(self.inl, V(30)), MUL(self.inr, V(31))
        tgt = [M(10), M(11), M(18), M(19)]
        inp = [lin, rin, lin, rin]
        src = [M(16), M(17), M(25), M(24)]
        for k in range(4):
            prev = self.rv(tgt[k] - 2)
            v = MUL(inp[k] + MUL(self.rv(src[k]), V(7)) - prev, V(2)) + prev
            self.rvw(tgt[k], v)
        for side in range(2):
            combs = [M(12 + side), M(14 + side), M(20 + side), M(22 + side)]
            apf = [M(26 + side), M(28 + side)]
            d = [M(0), M(1)]
            vv = [V(8), V(9)]
            o = 0
            for k in range(4):
                o += MUL(V(3 + k), self.rv(combs[k]))
            o = max(-32768, min(32767, o))
            for k in range(2):
                t = self.rv(apf[k] - d[k])
                o = max(-32768, min(32767, o - MUL(vv[k], t)))
                self.rvw(apf[k], o)
                o = max(-32768, min(32767, MUL(o, vv[k]) + t))
            self.out[side] = MUL(o, s16(self.vout[side]))
        self.pos = (self.pos + 2) % self.size

    def muestra(self, x):
        self.inl = (self.inl + x) >> 1 if self.odd else x
        self.inr = self.inl
        if self.odd:
            self.paso()
        self.odd = not self.odd
        return tuple(self.out)


# ---- la prueba ----
def envolvente(sig, ventana=441):
    return [10 * math.log10(max(1e-9, sum(x * x for x in sig[i:i + ventana]) / ventana))
            for i in range(0, len(sig) - ventana + 1, ventana)]


def prueba():
    p = construye()
    mpro, coef, madrs = tablas(p, PRESET)
    print(f'programa: {len(p.pasos)} pasos de 128, {len(p.madrs)} direcciones, {p.temps} TEMP')
    n = 44100
    random.seed(1)
    senales = {
        'impulso': [16000 if i == 0 else 0 for i in range(n)],
        'ruido 50 ms': [random.randint(-8000, 8000) if i < 2205 else 0 for i in range(n)],
        # varias voces fuertes a la vez: la suma pasa de 16 bits y PS1 satura
        'ruido fuerte': [random.randint(-90000, 90000) if i < 4410 else 0 for i in range(n)],
    }
    ok = True
    for nombre, sig in senales.items():
        dsp, ps1 = DSP(mpro, coef, madrs), PS1(PRESET)
        a = [[], []]; b = [[], []]
        for x in sig:
            l, r = dsp.muestra(x)
            a[0].append(l * 16); a[1].append(r * 16)       # EFREG va en la escala de /16
            l, r = ps1.muestra(x)
            b[0].append(l); b[1].append(r)
        for lado in range(2):
            ea, eb = envolvente(a[lado]), envolvente(b[lado])
            ta = 10 * math.log10(sum(x * x for x in a[lado]) + 1e-9)
            tb = 10 * math.log10(sum(x * x for x in b[lado]) + 1e-9)
            # mientras la cola de PS1 este a menos de 35 dB de su pico: por debajo, cada uno
            # mide su propio redondeo (PS1 trunca a 16 bits y cae a 0; el AICA se queda en el
            # LSB de EFREG, ~-69 dBFS), y por encima de 10 LSB (20 dB)
            difs = [abs(x - y) for x, y in zip(ea, eb) if y > max(max(eb) - 35, 20)]
            md = sum(difs) / len(difs)
            print(f'{nombre:12} {"LR"[lado]}: energia AICA {ta:6.1f} dB, PS1 {tb:6.1f} dB '
                  f'(dif {ta - tb:+.2f}); envolvente cada 10 ms: dif media {md:.2f} dB, '
                  f'max {max(difs):.2f} dB en {len(difs)} ventanas')
            print('    PS1 :', ' '.join(f'{x:5.0f}' for x in eb[:20]))
            print('    AICA:', ' '.join(f'{x:5.0f}' for x in ea[:20]))
            # saturando, PS1 satura despues del IIR y aqui antes (la suma de entrada): algo mas
            lim = 1.5 if 'fuerte' in nombre else 1.0
            ok &= abs(ta - tb) < 1.0 and md < lim and max(difs) < 2.5
    print('OK' if ok else 'DIFIERE')
    return 0 if ok else 1


# ---- la cabecera C ----
COEF_TIPOS = {'cero': 0, 'reg': 1, 'mul': 2, '1-': 3, 'neg': 4, 'uno': 5, 'out': 6, 'entrada': 7}


def cabecera(path):
    p = construye()
    mpro = [codifica(c) for c in p.pasos]
    out = ['/* GENERADO por dc/tools/dsp/reverb_aica.py: la reverb del SPU de PS1 en el DSP del',
           ' * AICA. No editar a mano. */',
           '#ifndef REVERB_DSP_H', '#define REVERB_DSP_H', '',
           f'#define REVERB_PASOS {len(mpro)}',
           f'#define REVERB_MADRS {len(p.madrs)}',
           f'#define REVERB_RBL   {RBL}', '',
           '/* MPRO: 4 palabras de 16 bits por paso */',
           'static const uint16_t reverb_mpro[REVERB_PASOS][4] = {']
    for s, w in enumerate(mpro):
        out.append(f'    {{ 0x{w[0]:04x}, 0x{w[1]:04x}, 0x{w[2]:04x}, 0x{w[3]:04x} }},  /* {s} */')
    out += ['};', '',
            '/* COEF de cada paso: { tipo, a, b } (ver reverb_coef() en aica_spu.c):',
            ' * 0 cero, 1 v[a], 2 v[a]*v[b], 3 1-v[a], 4 -v[a], 5 4095/4096, 6 salida del lado a,',
            ' * 7 v[a]*v[b]/raiz(2) */',
            'static const uint8_t reverb_coef[REVERB_PASOS][3] = {']
    for s, f in enumerate(p.coefs):
        a = f[1] if len(f) > 1 else 0
        b = f[2] if len(f) > 2 else 0
        out.append(f'    {{ {COEF_TIPOS[f[0]]}, {a}, {b} }},')
    out += ['};', '',
            '/* MADRS: -(v[a] - v[b]) * 8 + extra (b = 0xFF: sin v[b]) */',
            'static const struct { uint8_t a, b; uint8_t extra; } reverb_madrs[REVERB_MADRS] = {']
    for f in p.madrs:
        if len(f) == 2:
            out.append(f'    {{ {f[0]}, 0xFF, {f[1]} }},')
        else:
            out.append(f'    {{ {f[0]}, {f[1]}, {f[2]} }},')
    out += ['};', '', '#endif /* REVERB_DSP_H */', '']
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
    print(f'escrito {path}: {len(mpro)} pasos')


if __name__ == '__main__':
    if '--prueba' in sys.argv:
        sys.exit(prueba())
    if '--cabecera' in sys.argv:
        cabecera(sys.argv[sys.argv.index('--cabecera') + 1])
        sys.exit(0)
    print(__doc__)
