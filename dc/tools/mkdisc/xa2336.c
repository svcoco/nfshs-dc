/* ISO (sectores de 2048 bytes) -> sectores de 2336 bytes de CD-ROM XA, Mode 2 Form 1.
 *
 * Es el formato de datos de la plantilla de cdi4dc / 240pSuite.cdi (la que arranca en una
 * Dreamcast real): cada sector lleva el subencabezado de 8 bytes, los 2048 de datos, el EDC
 * de 4 bytes y el ECC (P de 172 y Q de 104). La sincronia y la cabecera de 4 bytes (la
 * direccion) las pone quien graba el disco; el ECC se calcula con esa cabecera a cero, como
 * pide el Mode 2 Form 1, para que no dependa de donde caiga el sector.
 *
 * EDC: CRC-32 de polinomio 0x8001801B (reflejado 0xD8018001), inicio 0, sin xor final, en
 * little-endian, sobre el subencabezado y los datos. ECC: Reed-Solomon RS(26,24) y RS(45,43)
 * sobre GF(2^8) con el polinomio 0x11D (ECMA-130, anexo A). Es el algoritmo de ecm.c de
 * Neill Corlett.
 *
 *   uso: xa2336 <entrada.iso> <salida.bin>
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t edc_lut[256];
static uint8_t ecc_f_lut[256], ecc_b_lut[256];

static void tablas(void)
{
    uint32_t i, j;

    for (i = 0; i < 256u; i++) {
        uint32_t edc = i;

        j = (i << 1) ^ ((i & 0x80u) ? 0x11Du : 0u);
        ecc_f_lut[i] = (uint8_t)j;
        ecc_b_lut[i ^ j] = (uint8_t)i;
        for (j = 0; j < 8u; j++)
            edc = (edc >> 1) ^ ((edc & 1u) ? 0xD8018001u : 0u);
        edc_lut[i] = edc;
    }
}

static uint32_t edc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0;

    while (n--)
        c = (c >> 8) ^ edc_lut[(c ^ *p++) & 0xFFu];
    return c;
}

static void ecc_bloque(const uint8_t *src, uint32_t mayor_n, uint32_t menor_n,
                       uint32_t mayor_mult, uint32_t menor_inc, uint8_t *dest)
{
    uint32_t size = mayor_n * menor_n, mayor, menor;

    for (mayor = 0; mayor < mayor_n; mayor++) {
        uint32_t index = (mayor >> 1) * mayor_mult + (mayor & 1u);
        uint8_t a = 0, b = 0;

        for (menor = 0; menor < menor_n; menor++) {
            uint8_t t = src[index];

            index += menor_inc;
            if (index >= size)
                index -= size;
            a ^= t;
            b ^= t;
            a = ecc_f_lut[a];
        }
        a = ecc_b_lut[ecc_f_lut[a] ^ b];
        dest[mayor] = a;
        dest[mayor + mayor_n] = (uint8_t)(a ^ b);
    }
}

/* Un sector de 2352 bytes (con la cabecera a cero) -> los 2336 que van al CDI. */
static void sector(const uint8_t *datos, uint8_t *out2336)
{
    uint8_t s[2352];
    uint32_t e;

    memset(s, 0, sizeof s);
    s[0] = 0x00; memset(s + 1, 0xFF, 10); s[11] = 0x00;       /* sincronia */
    /* cabecera (12..15): a cero para el ECC */
    s[16] = 0; s[17] = 0; s[18] = 0x08; s[19] = 0;           /* subencabezado, dos veces: */
    s[20] = 0; s[21] = 0; s[22] = 0x08; s[23] = 0;           /* datos, Form 1 */
    memcpy(s + 24, datos, 2048);
    e = edc(s + 16, 0x808);
    s[0x818] = (uint8_t)e; s[0x819] = (uint8_t)(e >> 8);
    s[0x81A] = (uint8_t)(e >> 16); s[0x81B] = (uint8_t)(e >> 24);
    ecc_bloque(s + 0xC, 86, 24, 2, 86, s + 0x81C);            /* P */
    ecc_bloque(s + 0xC, 52, 43, 86, 88, s + 0x8C8);           /* Q */
    memcpy(out2336, s + 16, 2336);
}

int main(int argc, char **argv)
{
    FILE *in, *out;
    uint8_t d[2048], o[2336];
    size_t n;
    unsigned long total = 0;

    if (argc != 3) {
        fprintf(stderr, "uso: xa2336 <entrada.iso> <salida.bin>\n");
        return 2;
    }
    in = fopen(argv[1], "rb");
    out = fopen(argv[2], "wb");
    if (in == NULL || out == NULL) {
        fprintf(stderr, "xa2336: no puedo abrir los archivos\n");
        return 1;
    }
    tablas();
    while ((n = fread(d, 1, sizeof d, in)) == sizeof d) {
        sector(d, o);
        if (fwrite(o, 1, sizeof o, out) != sizeof o) {
            fprintf(stderr, "xa2336: error al escribir\n");
            return 1;
        }
        total++;
    }
    if (n != 0) {
        fprintf(stderr, "xa2336: el ISO no es multiplo de 2048\n");
        return 1;
    }
    fclose(in);
    if (fclose(out) != 0)
        return 1;
    fprintf(stderr, "xa2336: %lu sectores\n", total);
    return 0;
}
