"""Verifica la salida de dc/tools/mkdisc/xa2336.c con una comprobacion INDEPENDIENTE del codigo.

El EDC y el ECC de un CD-ROM XA tienen que cumplir la definicion de la norma (ECMA-130), no
solo coincidir con la implementacion que los genero: si no, la unidad de la consola los toma
por sectores erroneos. Aqui se calculan desde la definicion (CRC-32 bit a bit; las sindromes
de los codigos de Reed-Solomon con el polinomio generador 0x11D y alfa = 2), sin las tablas
ni el recorrido de ecm.c.

    uso: python -I verifica_xa.py <iso> <salida de xa2336>     (comprueba todos los sectores)
"""
import sys

# ---- GF(2^8), polinomio x^8 + x^4 + x^3 + x^2 + 1 (0x11D), alfa = 2 ----
EXP = [0] * 512
LOG = [0] * 256
x = 1
for i in range(255):
    EXP[i] = x
    LOG[x] = i
    x <<= 1
    if x & 0x100:
        x ^= 0x11D
for i in range(255, 512):
    EXP[i] = EXP[i - 255]


def mul_alfa(v, k):
    """v * alfa^k"""
    return 0 if v == 0 else EXP[(LOG[v] + k) % 255]


def edc(datos):
    """CRC-32 bit a bit, polinomio reflejado 0xD8018001, inicio 0, sin xor final."""
    c = 0
    for b in datos:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ (0xD8018001 if c & 1 else 0)
    return c


def vector_p(s, major):
    """Los 26 bytes de un vector P (24 de datos y 2 de paridad), segun ECMA-130."""
    # los 24 de datos: columna par/impar de a pares de bytes, de 86 en 86
    base = 12 + (major >> 1) * 2 + (major & 1)
    datos = [s[(base - 12 + 86 * k) % 2064 + 12] for k in range(24)]
    return datos + [s[0x81C + major], s[0x81C + 86 + major]]


def vector_q(s, major):
    """Los 45 bytes de un vector Q (43 de datos y 2 de paridad)."""
    base = (major >> 1) * 86 + (major & 1)
    datos = [s[12 + (base + 88 * k) % 2236] for k in range(43)]
    return datos + [s[0x8C8 + major], s[0x8C8 + 52 + major]]


def sindromes(c):
    """S0 = suma de bytes; S1 = suma de c_i * alfa^(n-1-i). Un codeword valido: (0, 0)."""
    n = len(c)
    s0 = s1 = 0
    for i, v in enumerate(c):
        s0 ^= v
        s1 ^= mul_alfa(v, n - 1 - i)
    return s0, s1


def completo(datos2336):
    """Reconstruye el sector de 2352 con la cabecera a cero (como se calculo el ECC)."""
    s = bytearray(2352)
    s[1:11] = b'\xff' * 10
    s[16:2352] = datos2336
    return s


def main():
    iso = open(sys.argv[1], 'rb').read()
    xa = open(sys.argv[2], 'rb').read()
    n = len(iso) // 2048
    assert len(xa) == n * 2336, (len(xa), n * 2336)
    malos = 0
    paso = max(1, n // 4000)       # los primeros y una muestra amplia; con --todos, todos
    rango = range(n) if '--todos' in sys.argv else range(0, n, paso)
    for i in rango:
        d = xa[i * 2336:(i + 1) * 2336]
        if d[8:2056] != iso[i * 2048:(i + 1) * 2048]:
            print('sector', i, ': los datos no coinciden'); malos += 1; continue
        if d[0:4] != d[4:8] or d[2] != 0x08:
            print('sector', i, ': subencabezado raro', d[:8].hex()); malos += 1; continue
        s = completo(d)
        e = int.from_bytes(s[0x818:0x81C], 'little')
        if e != edc(bytes(s[16:0x818])):
            print('sector', i, ': EDC distinto'); malos += 1; continue
        for m in range(86):
            if sindromes(vector_p(s, m)) != (0, 0):
                print('sector', i, ': P', m, 'no cumple la definicion'); malos += 1; break
        else:
            for m in range(52):
                if sindromes(vector_q(s, m)) != (0, 0):
                    print('sector', i, ': Q', m, 'no cumple la definicion'); malos += 1; break
    print('sectores comprobados:', len(rango), 'malos:', malos)
    return 1 if malos else 0


if __name__ == '__main__':
    sys.exit(main())
