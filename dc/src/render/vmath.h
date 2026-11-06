/* M8: la transformacion de vertices con las instrucciones del SH-4. FTRV hace la matriz de un
 * golpe y FSRRA da la inversa sin dividir; en PC, lo mismo en C. Sigue siendo portable: no
 * incluye cabeceras de KOS.
 *
 * XMTRX (el segundo banco del FPU) no lo toca el compilador con -m4-single, y KOS guarda los
 * dos bancos al cambiar de hilo, asi que vale de vm_carga a los vm_aplica que la siguen. */
#ifndef PS1_RENDER_VMATH_H
#define PS1_RENDER_VMATH_H

#if defined(__SH4_SINGLE__)

static inline void vm_carga(const float m[3][3], const float t[3])
{
    /* por columnas, que es como se llena XMTRX */
    const float c[16] __attribute__((aligned(8))) = {
        m[0][0], m[1][0], m[2][0], 0.0f,
        m[0][1], m[1][1], m[2][1], 0.0f,
        m[0][2], m[1][2], m[2][2], 0.0f,
        t[0],    t[1],    t[2],    1.0f,
    };
    const float *p = c;

    __asm__ __volatile__(
        "frchg\n\t"
        "fmov.s @%0+,fr0\n\tfmov.s @%0+,fr1\n\tfmov.s @%0+,fr2\n\tfmov.s @%0+,fr3\n\t"
        "fmov.s @%0+,fr4\n\tfmov.s @%0+,fr5\n\tfmov.s @%0+,fr6\n\tfmov.s @%0+,fr7\n\t"
        "fmov.s @%0+,fr8\n\tfmov.s @%0+,fr9\n\tfmov.s @%0+,fr10\n\tfmov.s @%0+,fr11\n\t"
        "fmov.s @%0+,fr12\n\tfmov.s @%0+,fr13\n\tfmov.s @%0+,fr14\n\tfmov.s @%0+,fr15\n\t"
        "frchg\n"
        : "+r" (p) : "m" (c));
}

static inline void vm_aplica(const float m[3][3], const float t[3], float x, float y, float z,
                             float *ox, float *oy, float *oz)
{
    register float a __asm__("fr0") = x;
    register float b __asm__("fr1") = y;
    register float c __asm__("fr2") = z;
    register float d __asm__("fr3") = 1.0f;

    (void)m; (void)t;
    __asm__ __volatile__("ftrv xmtrx,fv0" : "+f" (a), "+f" (b), "+f" (c), "+f" (d));
    *ox = a;
    *oy = b;
    *oz = c;
}

/* 1/z, con z > 0: 1/sqrt(z*z) */
static inline float vm_inv(float z)
{
    float r = z * z;

    __asm__ __volatile__("fsrra %0" : "+f" (r));
    return r;
}

#else

static inline void vm_carga(const float m[3][3], const float t[3])
{
    (void)m; (void)t;
}

static inline void vm_aplica(const float m[3][3], const float t[3], float x, float y, float z,
                             float *ox, float *oy, float *oz)
{
    *ox = m[0][0] * x + m[0][1] * y + m[0][2] * z + t[0];
    *oy = m[1][0] * x + m[1][1] * y + m[1][2] * z + t[1];
    *oz = m[2][0] * x + m[2][1] * y + m[2][2] * z + t[2];
}

static inline float vm_inv(float z)
{
    return 1.0f / z;
}

#endif
#endif /* PS1_RENDER_VMATH_H */
