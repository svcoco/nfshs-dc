/* Los coches en pantalla. Ver car.h para el porque. */
#include "host.h"
#include "car.h"
#include "trackint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAU 6.28318530718f

static uint16_t crd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t crd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static float crdf(const uint8_t *p)
{
    uint32_t w = crd32(p);
    float f;

    /* el horneado escribe IEEE-754 little-endian, que es lo que el SH-4 usa */
    memcpy(&f, &w, sizeof f);
    return f;
}

int car_load(CarModel *m, const char *path)
{
    FILE *f;
    long size;
    uint32_t off_parts, off_verts, atlas_off, env_off, i;

    memset(m, 0, sizeof(*m));
    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 36 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    m->raw = malloc((size_t)size);
    if (m->raw == NULL) {
        fclose(f);
        return 0;
    }
    if (fread(m->raw, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        car_free(m);
        return 0;
    }
    fclose(f);
    m->size = (uint32_t)size;

    if (size <= 40 || memcmp(m->raw, "NFSC", 4) != 0 || crd32(m->raw + 4) != 2u) {
        car_free(m);
        return 0;
    }
    m->parts = crd16(m->raw + 8);
    m->atlas_w = crd16(m->raw + 10);
    m->atlas_h = crd16(m->raw + 12);
    m->verts = crd32(m->raw + 16);
    atlas_off = crd32(m->raw + 20);
    for (i = 0; i < 3u; i++)
        m->half_extents[i] = crdf(m->raw + 24u + 4u * i);
    env_off = crd32(m->raw + 36u);

    off_parts = 40u;   /* la cabecera: 4+4+2+2+2+2+4+4+12+4 */
    off_verts = off_parts + (uint32_t)m->parts * 24u;
    if (off_verts + m->verts * 24u != atlas_off ||
        atlas_off + (uint32_t)m->atlas_w * m->atlas_h * 2u != env_off ||
        env_off + (uint32_t)m->parts * 8u > m->size) {
        car_free(m);
        return 0;
    }
    /* Las partes se decodifican, igual que los bloques de la pista: el registro mezcla
     * u32, u8 y float y superponerlo seria contar con el relleno del compilador. */
    m->part = calloc(m->parts != 0u ? m->parts : 1u, sizeof(*m->part));
    if (m->part == NULL) {
        car_free(m);
        return 0;
    }
    for (i = 0; i < m->parts; i++) {
        const uint8_t *p = m->raw + off_parts + (size_t)i * 24u;
        uint32_t k;

        m->part[i].vert_off = crd32(p);
        m->part[i].vert_n = crd32(p + 4u);
        m->part[i].wheel = p[8];
        m->part[i].subtract = p[9];
        for (k = 0; k < 3u; k++)
            m->part[i].hub[k] = crdf(p + 12u + 4u * k);
    }
    /* los reflejos: la cabecera por parte y despues los registros, en orden */
    {
        uint32_t rec = env_off + (uint32_t)m->parts * 8u;

        for (i = 0; i < m->parts; i++) {
            CarPart *cp = &m->part[i];

            cp->env_tris = crd32(m->raw + env_off + 8u * i);
            cp->env_geo = crd32(m->raw + env_off + 8u * i + 4u);
            if (cp->env_tris == 0u)
                continue;
            if (cp->env_tris * 3u != cp->vert_n || rec + cp->env_tris * 28u > m->size) {
                car_free(m);
                return 0;
            }
            cp->env_rec = m->raw + rec;
            rec += cp->env_tris * 28u;
        }
        if (rec != m->size) {
            car_free(m);
            return 0;
        }
    }
    m->vert = (const float *)(const void *)(m->raw + off_verts);
    m->atlas = (const uint16_t *)(const void *)(m->raw + atlas_off);
    return 1;
}

int car_refl_load(uint16_t *texels, const char *path)
{
    uint8_t cab[8];
    FILE *f = fopen(path, "rb");
    size_t n = (size_t)CAR_REFL_SIZE * CAR_REFL_SIZE;
    int ok;

    if (f == NULL)
        return 0;
    ok = fread(cab, 1, 8, f) == 8 && memcmp(cab, "NFSR", 4) == 0 && crd32(cab + 4) == 1u &&
         fread(texels, 2, n, f) == n;
    fclose(f);
    /* el archivo es little-endian, como el SH-4 y el x86 */
    return ok;
}

void car_free(CarModel *m)
{
    free(m->part);
    free(m->raw);
    memset(m, 0, sizeof(*m));
}

/* ---- el dibujo ---- */

/* El tono de la pista sobre el color del vertice. Los dos van con 128 como el 1,0 (el color
 * del horneado es 0x80 en lo neutro, y el PVR modula por color/255), asi que se duplica:
 * color * 2 * tono / 128, como la referencia (vertice 0,5 x tinte x 2 del shader). Antes no
 * se duplicaba y los coches salian a la mitad de brillo. */
static uint32_t tinta(uint32_t canal, uint32_t tono)
{
    uint32_t v = canal * tono / 64u;

    return v > 255u ? 255u : v;
}

static void norm3(float v[3])
{
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    if (n > 1e-6f) {
        v[0] /= n;
        v[1] /= n;
        v[2] /= n;
    } else {
        v[0] = 0.0f;
        v[1] = 0.0f;
        v[2] = 1.0f;
    }
}

int car_env_abr(const struct CarDraw *d, int pasada)
{
    const uint32_t *map = pasada == CAR_PASS_ENV_A ? d->env_a : d->env_b;

    if (!d->env_ok || map[0] == 0u)
        return -1;
    return (int)((map[1] >> 16 >> 5) & 3u);
}

/* La parte del GEO de cada una de las siete que reflejan, en el orden de env_n. */
static int parte_env(uint32_t pi)
{
    static const uint8_t pis[CAR_ENV_PARTS] = { 0, 8, 10, 41, 42, 45, 46 };
    uint32_t k;

    for (k = 0; k < CAR_ENV_PARTS; k++)
        if (pis[k] == pi)
            return (int)k;
    return -1;
}

void car_draw(const CarModel *m, const struct CarDraw *d, const struct RaceView *view,
              int pasada, float sw, float sh, TrackEmit emit, void *user, TrackStats *st)
{
    int solo_resta = pasada == CAR_PASS_SUBTRACT;
    int reflejo = pasada == CAR_PASS_ENV_A || pasada == CAR_PASS_ENV_B;
    const uint32_t *map = pasada == CAR_PASS_ENV_A ? d->env_a : d->env_b;
    uint8_t u0 = 0, v0 = 0, despl = 0;
    float ga = 0.0f, gs = 0.0f;
    uint32_t env_argb = 0;
    View vw;
    TrackMat mat;
    float ejes[3][3];            /* x, y, z del coche, en espacio de mundo */
    float pos[3], up[3];
    uint32_t pi, k, tono;

    if (st != NULL)
        memset(st, 0, sizeof(*st));
    if (m->raw == NULL)
        return;
    if (reflejo) {
        uint32_t cc[3], j;

        if (car_env_abr(d, pasada) < 0)
            return;
        u0 = (uint8_t)map[0];
        v0 = (uint8_t)(map[0] >> 8);
        despl = (uint8_t)d->env_scroll;
        /* la guinada de la camara alrededor del coche, 4096 por vuelta */
        ga = cosf((float)d->env_yaw * (TAU / 4096.0f));
        gs = sinf((float)d->env_yaw * (TAU / 4096.0f));
        /* su color, con el 0x80 de PS1 como el 1,0 */
        for (j = 0; j < 3u; j++) {
            cc[j] = ((d->env_colour >> (8u * j)) & 0xFFu) * 2u;
            if (cc[j] > 255u)
                cc[j] = 255u;
        }
        env_argb = 0x80000000u | (cc[0] << 16) | (cc[1] << 8) | cc[2];
    }
    view_init(&vw, view, sw, sh);
    memset(&mat, 0, sizeof mat);

    /* Las filas de `orient` son derecha, arriba y adelante en espacio de PS1; en mundo la
     * X va espejada. El modelo tiene +Y arriba, +Z adelante y +X a la IZQUIERDA del
     * coche, asi que su eje x es -derecha. */
    for (k = 0; k < 3u; k++) {
        ejes[0][k] = (float)d->orient[k] / 65536.0f;
        ejes[1][k] = (float)d->orient[3u + k] / 65536.0f;
        ejes[2][k] = (float)d->orient[6u + k] / 65536.0f;
    }
    for (k = 0; k < 3u; k++) {
        /* a mundo: X espejada */
        ejes[0][k] = (k == 0u ? 1.0f : -1.0f) * ejes[0][k];
        ejes[1][k] = (k == 0u ? -1.0f : 1.0f) * ejes[1][k];
        ejes[2][k] = (k == 0u ? -1.0f : 1.0f) * ejes[2][k];
    }
    /* ejes[0] quedo como -derecha ya espejada (el signo de la X se cancela en la
     * componente 0 y se invierte en las otras dos), que es justo el eje x del modelo */
    up[0] = ejes[1][0]; up[1] = ejes[1][1]; up[2] = ejes[1][2];
    norm3(up);
    /* Gram-Schmidt, como la referencia: z adelante, y arriba, x = y x z */
    norm3(ejes[2]);
    {
        float dp = ejes[1][0] * ejes[2][0] + ejes[1][1] * ejes[2][1] + ejes[1][2] * ejes[2][2];

        for (k = 0; k < 3u; k++)
            ejes[1][k] -= ejes[2][k] * dp;
        norm3(ejes[1]);
        ejes[0][0] = ejes[1][1] * ejes[2][2] - ejes[1][2] * ejes[2][1];
        ejes[0][1] = ejes[1][2] * ejes[2][0] - ejes[1][0] * ejes[2][2];
        ejes[0][2] = ejes[1][0] * ejes[2][1] - ejes[1][1] * ejes[2][0];
    }

    /* El origen del modelo esta a ras de suelo y la posicion simulada es el centro de la
     * caja del cuerpo: hay que bajarlo por la media extension en Y. */
    {
        float lift = (float)d->dimension[1] / 65536.0f;

        pos[0] = -(float)d->pos[0] / 65536.0f - up[0] * lift;
        pos[1] = (d->mirror ? -1.0f : 1.0f) * (float)d->pos[1] / 65536.0f - up[1] * lift;
        pos[2] = (float)d->pos[2] / 65536.0f - up[2] * lift;
    }

    /* el tono: la media de los canales de la luz de la pista, con 128 como el 1,0 */
    {
        uint32_t r = d->light & 0xFFu, g = (d->light >> 8) & 0xFFu, b = (d->light >> 16) & 0xFFu;

        tono = (r + g + b) / 3u;
        if (tono < 24u)
            tono = 24u;            /* car_light de la referencia */
        if (tono > 255u)
            tono = 255u;
    }
    /* las partes que se restan llevan su color tal cual, sin el tono de la luz: asi las
     * dibuja la referencia (car_arch_shadows) */
    if (solo_resta)
        tono = 128u;

    for (pi = 0; pi < m->parts && pi < 57u; pi++) {
        const CarPart *p = &m->part[pi];
        uint8_t on = d->parts[pi];
        float rw[3][3];          /* giro propio de la parte (las ruedas) */
        int es_rueda = p->wheel != 0xFFu;
        uint32_t t;

        int ke = -1;
        int ram = 0;

        /* 1..16 se dibuja; 0 no esta y 17 en adelante son halos de luz, que son 2D */
        if (on == 0u || on >= 17u || p->vert_n == 0u)
            continue;
        if (reflejo) {
            /* solo las partes que reflejan */
            if (p->env_tris == 0u || p->env_rec == NULL)
                continue;
            ke = parte_env(pi);
            ram = ke >= 0 && d->env_n[ke] != 0u && d->env_n[ke] == p->env_geo;
        } else if ((p->subtract != 0u) != (solo_resta != 0)) {
            /* cada pasada dibuja un tipo de partes: las normales, o solo las que se restan */
            continue;
        }

        if (es_rueda) {
            int tras = p->wheel >= 2u;
            float giro = (float)d->wheel_rot[tras ? 1u : 0u] * (TAU / 65536.0f);
            float vira = tras ? 0.0f : -(float)d->wheel_steer * (TAU / 65536.0f);
            float cg = cosf(giro), sg = sinf(giro);
            float cv = cosf(vira), sv = sinf(vira);

            /* Ry(vira) * Rx(giro) */
            rw[0][0] = cv;  rw[0][1] = sv * sg;  rw[0][2] = sv * cg;
            rw[1][0] = 0.0f; rw[1][1] = cg;      rw[1][2] = -sg;
            rw[2][0] = -sv; rw[2][1] = cv * sg;  rw[2][2] = cv * cg;
        }

        for (t = 0; t + 3u <= p->vert_n; t += 3u) {
            Cv tri[3];
            float wld[3][3];

            for (k = 0; k < 3u; k++) {
                const float *v = m->vert + (size_t)(p->vert_off + t + k) * 6u;
                float lv[3], q[3];
                uint32_t c;

                lv[0] = v[0]; lv[1] = v[1]; lv[2] = v[2];
                if (es_rueda) {
                    uint32_t j;

                    for (j = 0; j < 3u; j++)
                        q[j] = rw[j][0] * lv[0] + rw[j][1] * lv[1] + rw[j][2] * lv[2];
                    for (j = 0; j < 3u; j++)
                        q[j] += p->hub[j];
                } else {
                    q[0] = lv[0]; q[1] = lv[1]; q[2] = lv[2];
                }
                /* del espacio del modelo al mundo: las columnas son los ejes del coche */
                wld[k][0] = pos[0] + ejes[0][0] * q[0] + ejes[1][0] * q[1] + ejes[2][0] * q[2];
                wld[k][1] = pos[1] + ejes[0][1] * q[0] + ejes[1][1] * q[1] + ejes[2][1] * q[2];
                wld[k][2] = pos[2] + ejes[0][2] * q[0] + ejes[1][2] * q[1] + ejes[2][2] * q[2];
                /* el reflejo del showroom: como la referencia, la altura del coche negada y
                 * despues la y del mundo espejada (el suelo esta en y = 0) */
                if (d->mirror)
                    wld[k][1] = -wld[k][1];

                memcpy(&c, &v[5], sizeof c);
                tri[k].u = v[3];
                tri[k].v = v[4];
                tri[k].argb = 0xFF000000u | (tinta((c >> 16) & 0xFFu, tono) << 16) |
                              (tinta((c >> 8) & 0xFFu, tono) << 8) | tinta(c & 0xFFu, tono);
            }
            if (reflejo) {
                const uint8_t *r = p->env_rec + (size_t)(t / 3u) * 28u;
                float e1[3], e2[3], cr[3], tc[3];
                uint32_t j;

                /* el mapa B solo va en los poligonos marcados 0x3F3 */
                if (pasada == CAR_PASS_ENV_B && (crd16(r) & 0x3F3u) == 0u)
                    continue;
                /* NCLIP de DrawC_Prim: solo los que miran a la camara (el modelo tiene la X
                 * espejada respecto a PS1, asi que su sentido de giro es el contrario) */
                for (j = 0; j < 3u; j++) {
                    e1[j] = wld[1][j] - wld[0][j];
                    e2[j] = wld[2][j] - wld[0][j];
                    tc[j] = vw.cam[j] - wld[0][j];
                }
                cr[0] = e1[1] * e2[2] - e1[2] * e2[1];
                cr[1] = e1[2] * e2[0] - e1[0] * e2[2];
                cr[2] = e1[0] * e2[1] - e1[1] * e2[0];
                if (-(cr[0] * tc[0] + cr[1] * tc[1] + cr[2] * tc[2]) <= 0.0f)
                    continue;
                for (k = 0; k < 3u; k++) {
                    const uint8_t *c3 = r + 4u + 8u * k;
                    uint32_t gv = crd16(c3);
                    float n[3], hacia[3], dist, tira;
                    uint8_t u, v;

                    if (ram && gv < d->env_n[ke]) {
                        for (j = 0; j < 3u; j++)
                            n[j] = (float)d->env_norm[d->env_first[ke] + gv][j];
                    } else {
                        for (j = 0; j < 3u; j++)
                            n[j] = (float)(int16_t)crd16(c3 + 2u + 2u * j);
                    }
                    if (pasada == CAR_PASS_ENV_A) {
                        int x = (int)(ga * n[0] + gs * n[2]);
                        int z = (int)(-gs * n[0] + ga * n[2]);

                        u = (uint8_t)(u0 + 64u + (uint8_t)x);
                        v = (uint8_t)(v0 + (uint8_t)(z < 0 ? -z : z));
                        tira = 0.01f;
                    } else {
                        u = (uint8_t)(u0 + 64u + (uint8_t)(int)n[1]);
                        v = (uint8_t)(v0 + despl + (uint8_t)(int)n[2]);
                        u = (uint8_t)((u & 0x7Fu) | (u0 & 0x80u));
                        v = (uint8_t)((v & 0x3Fu) | (v0 & 0xC0u));
                        tira = 0.02f;
                    }
                    tri[k].u = ((float)u + 0.5f) / 256.0f;
                    tri[k].v = ((float)v + 0.5f) / 256.0f;
                    tri[k].argb = env_argb;
                    /* un pelo hacia la camara, sobre la carroceria que cubre */
                    for (j = 0; j < 3u; j++)
                        hacia[j] = vw.cam[j] - wld[k][j];
                    dist = sqrtf(hacia[0] * hacia[0] + hacia[1] * hacia[1] + hacia[2] * hacia[2]);
                    if (dist > 1e-4f)
                        for (j = 0; j < 3u; j++)
                            wld[k][j] += hacia[j] / dist * tira;
                }
            }
            for (k = 0; k < 3u; k++) {
                float dd[3];
                uint32_t j;

                for (j = 0; j < 3u; j++)
                    dd[j] = wld[k][j] - vw.cam[j];
                tri[k].x = vw.row[0][0] * dd[0] + vw.row[0][1] * dd[1] + vw.row[0][2] * dd[2];
                tri[k].y = vw.row[1][0] * dd[0] + vw.row[1][1] * dd[1] + vw.row[1][2] * dd[2];
                tri[k].z = vw.row[2][0] * dd[0] + vw.row[2][1] * dd[1] + vw.row[2][2] * dd[2];
            }
            triangulo(&vw, tri, &mat, emit, user, st);
        }
    }
}

/* ---- la sombra en el suelo ---- */

void car_shadow(const Track *t, const struct CarDraw *d, const struct RaceView *view,
                float sw, float sh, TrackEmit emit, void *user, TrackStats *st)
{
    static const int per[4] = { 0, 1, 3, 2 };   /* perimetro del cuadro */
    const TrackSkyRect *r = &t->sfx[SFX_SHAD];
    View vw;
    TrackMat mat;
    Cv q[4];
    float cx, cz, suelo;
    uint32_t luz, cb[3], k, argb;
    int todo_cero = 1;

    if (st != NULL)
        memset(st, 0, sizeof(*st));
    if (!d->shadow_on || !d->env_ok || d->parked || t->raw == NULL || r->w == 0u)
        return;
    for (k = 0; k < 12u; k++)
        if (d->shadow[k / 3u][k % 3u] != 0)
            todo_cero = 0;
    if (todo_cero)
        return;

    view_init(&vw, view, sw, sh);
    memset(&mat, 0, sizeof mat);

    /* el tono: la media de los canales de la luz, con el minimo de 24 de car_light */
    luz = ((d->light & 0xFFu) + ((d->light >> 8) & 0xFFu) + ((d->light >> 16) & 0xFFu)) / 3u;
    if (luz < 24u)
        luz = 24u;
    for (k = 0; k < 3u; k++) {
        cb[k] = (luz * (uint32_t)d->shadow_rgb[k]) >> 8;
        cb[k] = cb[k] * 2u > 255u ? 255u : cb[k] * 2u;   /* el 0x80 de PS1 es el 1,0 */
    }
    argb = 0xFF000000u | (cb[0] << 16) | (cb[1] << 8) | cb[2];

    cx = -(float)d->pos[0] / 65536.0f;
    cz = (float)d->pos[2] / 65536.0f;
    suelo = ((float)d->pos[1] - (float)d->dimension[1]) / 65536.0f;

    for (k = 0; k < 4u; k++) {
        int c = per[k];
        float p[3], to[3], dist, empuje, dd[3];
        /* la forma SHAD, con medio texel de margen como la referencia */
        float u = (c & 1) ? (float)r->w - 0.5f : 0.5f;
        float v = (c & 2) ? (float)r->h - 0.5f : 0.5f;
        uint32_t j;

        p[0] = cx - (float)d->shadow[c][0] / 65536.0f;
        p[1] = (float)d->shadow[c][1] / 65536.0f;
        p[2] = cz + (float)d->shadow[c][2] / 65536.0f;
        if (p[1] < suelo)
            p[1] = suelo;
        p[1] += 0.03f;
        /* El original no tiene buffer de profundidad: aqui el cuadro se acerca a la
         * camara (over_road de la referencia) para que no se hunda bajo la carretera. */
        for (j = 0; j < 3u; j++)
            to[j] = vw.cam[j] - p[j];
        dist = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        empuje = 0.25f + dist * 0.06f;
        if (empuje > 1.2f)
            empuje = 1.2f;
        if (empuje > dist * 0.5f)
            empuje = dist * 0.5f;
        for (j = 0; j < 3u; j++)
            p[j] += to[j] / (dist > 1e-3f ? dist : 1e-3f) * empuje;

        for (j = 0; j < 3u; j++)
            dd[j] = p[j] - vw.cam[j];
        q[k].x = vw.row[0][0] * dd[0] + vw.row[0][1] * dd[1] + vw.row[0][2] * dd[2];
        q[k].y = vw.row[1][0] * dd[0] + vw.row[1][1] * dd[1] + vw.row[1][2] * dd[2];
        q[k].z = vw.row[2][0] * dd[0] + vw.row[2][1] * dd[1] + vw.row[2][2] * dd[2];
        q[k].u = ((float)r->x + u) / (float)t->atlas_w;
        q[k].v = ((float)r->y + v) / (float)t->atlas_h;
        q[k].argb = argb;
    }
    {
        Cv tri[3];

        tri[0] = q[0]; tri[1] = q[1]; tri[2] = q[2];
        triangulo(&vw, tri, &mat, emit, user, st);
        tri[0] = q[0]; tri[1] = q[2]; tri[2] = q[3];
        triangulo(&vw, tri, &mat, emit, user, st);
    }
}
