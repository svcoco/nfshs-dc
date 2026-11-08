/* La pista en pantalla. Ver track.h para el convenio de la camara. */
#include "host.h"
#include "track.h"
#include "trackint.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* El registro de material SI se mira en el sitio, asi que su tamano tiene que ser
 * exactamente el que hornea bake.rs. */
_Static_assert(sizeof(TrackMat) == 16, "TrackMat tiene que medir 16 bytes");

#include "vmath.h"

/* ---- carga ---- */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int track_load(Track *t, const char *path)
{
    FILE *f;
    long size;
    uint32_t off_blk, off_mat, off_sky, off_sfx, off_mdl, off_mvt, off_mqd, off_vtx, off_qd;
    uint32_t atlas_off, i;

    memset(t, 0, sizeof(*t));
    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 28 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    t->raw = malloc((size_t)size);
    if (t->raw == NULL) {
        fclose(f);
        return 0;
    }
    if (fread(t->raw, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(t->raw);
        t->raw = NULL;
        return 0;
    }
    fclose(f);
    t->size = (uint32_t)size;

    if (memcmp(t->raw, "NFST", 4) != 0 || rd32(t->raw + 4) != 5u) {
        track_free(t);
        return 0;
    }
    t->blocks = rd16(t->raw + 8);
    t->mats = rd16(t->raw + 10);
    t->atlas_w = rd16(t->raw + 12);
    t->atlas_h = rd16(t->raw + 14);
    t->verts = rd32(t->raw + 16);
    t->quads = rd32(t->raw + 20);
    t->models = rd16(t->raw + 24);
    t->mverts = rd32(t->raw + 28);
    t->mquads = rd32(t->raw + 32);
    atlas_off = rd32(t->raw + 36);

    /* El horneado ya se verifico a si mismo en el PC (bake.rs); aqui solo se comprueba
     * que los tramos caben, porque un archivo truncado por el disco si es posible. */
    off_blk = 40u;
    off_mat = off_blk + (uint32_t)t->blocks * 56u;
    off_sky = off_mat + (uint32_t)t->mats * 16u;
    off_sfx = off_sky + 12u * 8u;
    off_mdl = off_sfx + 13u * 8u;
    off_mvt = off_mdl + (uint32_t)t->models * 16u;
    off_mqd = off_mvt + t->mverts * 8u;
    off_vtx = off_mqd + t->mquads * 10u;
    off_qd = off_vtx + t->verts * 8u;
    if (off_qd + t->quads * 10u != atlas_off ||
        atlas_off + (uint32_t)t->atlas_w * t->atlas_h * 2u != t->size) {
        track_free(t);
        return 0;
    }
    /* Los bloques se decodifican; el resto sí se puede mirar en el sitio, porque sus
     * registros miden 16, 8 y 10 bytes y caen alineados. */
    t->block = calloc(t->blocks != 0u ? t->blocks : 1u, sizeof(*t->block));
    if (t->block == NULL) {
        track_free(t);
        return 0;
    }
    for (i = 0; i < t->blocks; i++) {
        const uint8_t *p = t->raw + off_blk + (size_t)i * 56u;
        uint32_t k;

        for (k = 0; k < 3u; k++)
            t->block[i].origin[k] = (int32_t)rd32(p + k * 4u);
        for (k = 0; k < 7u; k++) {
            t->block[i].off[k] = rd32(p + 12u + k * 6u);
            t->block[i].n[k] = rd16(p + 16u + k * 6u);
        }
    }
    t->mat = (const TrackMat *)(const void *)(t->raw + off_mat);
    for (i = 0; i < 12u; i++) {
        const uint8_t *p = t->raw + off_sky + (size_t)i * 8u;

        t->sky[i].x = rd16(p);
        t->sky[i].y = rd16(p + 2u);
        t->sky[i].w = rd16(p + 4u);
        t->sky[i].h = rd16(p + 6u);
    }
    for (i = 0; i < 13u; i++) {
        const uint8_t *p = t->raw + off_sfx + (size_t)i * 8u;

        t->sfx[i].x = rd16(p);
        t->sfx[i].y = rd16(p + 2u);
        t->sfx[i].w = rd16(p + 4u);
        t->sfx[i].h = rd16(p + 6u);
    }
    t->vert = (const int16_t *)(const void *)(t->raw + off_vtx);
    t->quad = (const uint16_t *)(const void *)(t->raw + off_qd);
    t->mvert = (const int16_t *)(const void *)(t->raw + off_mvt);
    t->mquad = (const uint16_t *)(const void *)(t->raw + off_mqd);
    t->model = calloc(t->models != 0u ? t->models : 1u, sizeof(*t->model));
    if (t->model == NULL) {
        track_free(t);
        return 0;
    }
    for (i = 0; i < t->models; i++) {
        const uint8_t *p = t->raw + off_mdl + (size_t)i * 16u;

        t->model[i].vert_off = rd32(p);
        t->model[i].vert_n = rd16(p + 4u);
        t->model[i].quad_off = rd32(p + 6u);
        t->model[i].quad_n = rd16(p + 10u);
        t->model[i].flags = rd16(p + 12u);
    }
    t->atlas = (const uint16_t *)(const void *)(t->raw + atlas_off);
    return 1;
}

void track_free(Track *t)
{
    free(t->model);
    free(t->block);
    free(t->raw);
    memset(t, 0, sizeof(*t));
}

/* El atlas es el ultimo tramo del archivo, asi que basta con encoger el bloque: realloc no
 * mueve al encoger, y los punteros a las mallas, que van delante, siguen valiendo. */
void track_suelta_atlas(Track *t)
{
    size_t queda;

    if (t->raw == NULL || t->atlas == NULL)
        return;
    queda = (size_t)((const uint8_t *)t->atlas - t->raw);
    t->atlas = NULL;
    t->size = (uint32_t)queda;
    {
        void *mismo = realloc(t->raw, queda);

        (void)mismo;
    }
}

/* ---- la camara ---- */

static float len3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void view_init(View *vw, const struct RaceView *v, float sw, float sh)
{
    float pw = v->rect[2] > 0 ? (float)v->rect[2] : 320.0f;
    float ph = v->rect[3] > 0 ? (float)v->rect[3] : 240.0f;
    float adelante;
    uint32_t i, k;

    /* 16.16 y la X espejada: el mundo horneado tiene la X al reves que PS1 */
    vw->cam[0] = -(float)v->camera_pos[0] / 65536.0f;
    vw->cam[1] = (float)v->camera_pos[1] / 65536.0f;
    vw->cam[2] = (float)v->camera_pos[2] / 65536.0f;

    for (i = 0; i < 3u; i++) {
        vw->row[i][0] = -(float)v->camera_rot[i * 3u];
        vw->row[i][1] = (float)v->camera_rot[i * 3u + 1u];
        vw->row[i][2] = (float)v->camera_rot[i * 3u + 2u];
    }
    /* Dividir las tres por |adelante| deja "adelante" unitaria (asi z sale en metros) y
     * conserva el aplastamiento vertical que el juego mete en la fila de "arriba". */
    adelante = len3(vw->row[2]);
    if (adelante < 1e-6f)
        adelante = 1.0f;
    for (i = 0; i < 3u; i++)
        for (k = 0; k < 3u; k++)
            vw->row[i][k] /= adelante;

    vw->focal = v->projection != 0u ? (float)v->projection : 190.0f;
    vw->kx = sw / pw;
    vw->ky = sh / ph;
    vw->fx = vw->focal * vw->kx;
    vw->fy = vw->focal * vw->ky;
    vw->ox = sw * 0.5f;
    vw->oy = sh * 0.5f;
    if (v->centre_on) {
        vw->ox = (float)v->centre[0] / 65536.0f * vw->kx;
        vw->oy = (float)v->centre[1] / 65536.0f * vw->ky;
    }
    /* M5: a pantalla partida, la vista va en su sitio (rect, relativo a la vista 0) y se
     * recorta a su franja */
    vw->recorta_y = 0;
    vw->y0 = 0.0f;
    vw->y1 = sh;
    if (v->split) {
        vw->ox += (float)v->rect[0] * vw->kx;
        vw->oy += (float)v->rect[1] * vw->ky;
        vw->recorta_y = 1;
        vw->y0 = (float)v->rect[1] * vw->ky;
        vw->y1 = vw->y0 + sh;
    }
}

void view_size(const struct RaceView *v, float dw, float dh, float W, float H,
               float *sw, float *sh)
{
    *sw = W;
    *sh = H;
    if (v->split && dw > 0.0f && dh > 0.0f && v->rect[2] > 0 && v->rect[3] > 0) {
        *sw = W * (float)v->rect[2] / dw;
        *sh = H * (float)v->rect[3] / dh;
    }
}

/* Interpola entre dos vertices de camara, en `s` de 0 a 1. */
static void mezcla(Cv *o, const Cv *a, const Cv *b, float s)
{
    uint32_t k;

    o->x = a->x + (b->x - a->x) * s;
    o->y = a->y + (b->y - a->y) * s;
    o->z = a->z + (b->z - a->z) * s;
    o->u = a->u + (b->u - a->u) * s;
    o->v = a->v + (b->v - a->v) * s;
    o->argb = 0;
    for (k = 0; k < 4u; k++) {
        uint32_t ca = (a->argb >> (k * 8u)) & 0xFFu;
        uint32_t cb = (b->argb >> (k * 8u)) & 0xFFu;
        uint32_t c = (uint32_t)((float)ca + ((float)cb - (float)ca) * s + 0.5f);

        o->argb |= (c > 255u ? 255u : c) << (k * 8u);
    }
}

/* Recorta un triangulo contra el plano cercano y lo emite. El PVR no recorta: un vertice
 * con z <= 0 se proyectaria detras de la camara y saldria un triangulo gigante al reves.
 * Cortar da 0, 3 o 4 vertices. */
/* La distancia de un vertice a los bordes de la franja de la vista, en espacio de camara
 * (>= 0 dentro). En pantalla y = oy - y * f * ky / z, asi que y >= y0 es
 * (oy - y0) z - y f ky >= 0, y y <= y1 es (y1 - oy) z + y f ky >= 0: planos que pasan por la
 * camara, y entre dos vertices se interpola como en el plano cercano. */
static float borde_y(const View *vw, const Cv *c, int abajo)
{
    float fy = c->y * vw->focal * vw->ky;

    return abajo ? (vw->y1 - vw->oy) * c->z + fy : (vw->oy - vw->y0) * c->z - fy;
}

/* Sutherland-Hodgman de `in` (n vertices) contra un borde de la franja. */
static uint32_t recorta_borde(const View *vw, const Cv *in, uint32_t n, Cv *out, int abajo)
{
    uint32_t i, m = 0;

    for (i = 0; i < n; i++) {
        const Cv *a = &in[i], *b = &in[(i + 1u) % n];
        float da = borde_y(vw, a, abajo), db = borde_y(vw, b, abajo);

        if (da >= 0.0f)
            out[m++] = *a;
        if ((da >= 0.0f) != (db >= 0.0f))
            mezcla(&out[m++], a, b, da / (da - db));
    }
    return m;
}

/* Distancia a un borde de la banda de guarda en espacio de camara (>= 0 dentro). Como en
 * borde_y: son planos que pasan por la camara. */
static float borde_guarda(const View *vw, const Cv *c, uint32_t b)
{
    switch (b) {
    case 0:  return c->x * vw->focal * vw->kx + (vw->ox - GUARD_X0) * c->z;
    case 1:  return (GUARD_X1 - vw->ox) * c->z - c->x * vw->focal * vw->kx;
    case 2:  return (vw->oy - GUARD_Y0) * c->z - c->y * vw->focal * vw->ky;
    default: return (GUARD_Y1 - vw->oy) * c->z + c->y * vw->focal * vw->ky;
    }
}

/* Los planos de la banda de guarda (bits 0-3) y el cercano (bit 4) que dejan fuera a un punto
 * en espacio de camara. */
static inline uint32_t fuera_guarda(const View *vw, float x, float y, float z)
{
    float fx = x * vw->fx, fy = y * vw->fy;
    uint32_t f = z < NEAR ? 0x10u : 0u;

    if (fx + (vw->ox - GUARD_X0) * z < 0.0f) f |= 1u;
    if ((GUARD_X1 - vw->ox) * z - fx < 0.0f) f |= 2u;
    if ((vw->oy - GUARD_Y0) * z - fy < 0.0f) f |= 4u;
    if ((GUARD_Y1 - vw->oy) * z + fy < 0.0f) f |= 8u;
    return f;
}

static uint32_t recorta_guarda(const View *vw, const Cv *in, uint32_t n, Cv *out, uint32_t b)
{
    uint32_t i, m = 0;

    for (i = 0; i < n; i++) {
        const Cv *p = &in[i], *q = &in[(i + 1u) % n];
        float dp = borde_guarda(vw, p, b), dq = borde_guarda(vw, q, b);

        if (dp >= 0.0f)
            out[m++] = *p;
        if ((dp >= 0.0f) != (dq >= 0.0f))
            mezcla(&out[m++], p, q, dp / (dp - dq));
    }
    return m;
}

/* Recorta un triangulo contra el plano cercano y lo emite. El PVR no recorta: un vertice
 * con z <= 0 se proyectaria detras de la camara y saldria un triangulo gigante al reves.
 * Cortar da 0, 3 o 4 vertices; a pantalla partida, los bordes de la franja dan hasta 6, y
 * cada borde de la banda de guarda anade como mucho uno mas. */
void triangulo(const View *vw, const Cv *tri, const TrackMat *m,
               TrackEmit emit, void *user, TrackStats *st)
{
    Cv dentro[12], otro[12];
    const Cv *pol = tri;
    TrackVert out[12];
    uint32_t n = 0, i, k;
    int cuantos = 0, cortado = 0;

    for (i = 0; i < 3u; i++)
        cuantos += tri[i].z >= NEAR;
    if (cuantos == 0) {
        if (st != NULL)
            st->behind++;
        return;
    }
    if (cuantos == 3) {
        /* Lo normal: nada que recortar, y proyectar ya da lo que hace falta para saberlo. */
        if (!vw->recorta_y) {
            proyecta(vw, &tri[0], &out[0]);
            proyecta(vw, &tri[1], &out[1]);
            proyecta(vw, &tri[2], &out[2]);
            if (dentro_guarda(out[0].x, out[0].y) && dentro_guarda(out[1].x, out[1].y) &&
                dentro_guarda(out[2].x, out[2].y)) {
                emit(user, out, 3u, m);
                if (st != NULL)
                    st->quads_out++;
                return;
            }
        }
        n = 3u;
    } else {
        /* Sutherland-Hodgman sobre el plano cercano. */
        for (i = 0; i < 3u; i++) {
            const Cv *a = &tri[i];
            const Cv *b = &tri[(i + 1u) % 3u];
            int ad = a->z >= NEAR, bd = b->z >= NEAR;

            if (ad)
                dentro[n++] = *a;
            if (ad != bd && n < 4u) {
                float d = b->z - a->z;

                mezcla(&dentro[n++], a, b, d != 0.0f ? (NEAR - a->z) / d : 0.0f);
            }
        }
        if (n < 3u)
            return;
        pol = dentro;
        cortado = 1;
    }
    if (vw->recorta_y) {
        /* lo que esta del todo dentro, que es casi todo, no se toca */
        int fuera = 0;

        for (k = 0; k < n; k++)
            fuera |= (borde_y(vw, &pol[k], 0) < 0.0f) | ((borde_y(vw, &pol[k], 1) < 0.0f) << 1);
        if (fuera != 0) {
            n = recorta_borde(vw, pol, n, otro, 0);
            n = recorta_borde(vw, otro, n, dentro, 1);
            if (n < 3u) {
                if (st != NULL)
                    st->clipped++;
                return;
            }
            pol = dentro;
            cortado = 1;
        }
    }
    {
        /* lo normal es que todo caiga dentro: entonces solo cuesta estas comparaciones */
        uint32_t b, fuera = 0;

        for (k = 0; k < n; k++)
            for (b = 0; b < 4u; b++)
                if (borde_guarda(vw, &pol[k], b) < 0.0f)
                    fuera |= 1u << b;
        for (b = 0; b < 4u && fuera != 0u; b++) {
            if ((fuera & (1u << b)) == 0u)
                continue;
            n = recorta_guarda(vw, pol, n, otro, b);
            if (n < 3u)
                return;
            for (k = 0; k < n; k++)
                dentro[k] = otro[k];
            pol = dentro;
            cortado = 1;
        }
    }
    for (k = 0; k < n; k++)
        proyecta(vw, &pol[k], &out[k]);
    emit(user, out, n, m);
    if (st != NULL) {
        st->quads_out++;
        st->clipped += (uint32_t)cortado;
    }
}

/* Dobla y satura: el 0x80 de PS1 es el 1,0, igual que en el overlay 2D de M2a. */
static uint32_t dobla(const uint8_t *c)
{
    uint32_t r = (uint32_t)c[0] * 2u, g = (uint32_t)c[1] * 2u, b = (uint32_t)c[2] * 2u;

    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* ---- el dibujo ---- */

static uint32_t color_sombra(const uint32_t *light, uint32_t s)
{
    uint32_t c;

    if (light == NULL)
        return 0xFFFFFFFFu;   /* neutro ya doblado */
    c = light[s & 0xFFu];
    /* Chunk_lightTable guarda 0xBBGGRR; el PVR quiere ARGB. Y se dobla, porque el 0x80
     * de PS1 es el 1,0: sin doblar, la pista sale a media luz. */
    {
        uint8_t rgb[3];

        rgb[0] = (uint8_t)(c & 0xFFu);
        rgb[1] = (uint8_t)((c >> 8) & 0xFFu);
        rgb[2] = (uint8_t)((c >> 16) & 0xFFu);
        return dobla(rgb);
    }
}

/* M8: la tabla de luz del frame ya convertida a ARGB doblado (luz_prepara), para no hacerlo
 * en cada vertice. Vale para la tabla `s_luz_de`. */
static uint32_t s_luz[256];
static const uint32_t *s_luz_de;

static void luz_prepara(const uint32_t *light)
{
    uint32_t i;

    s_luz_de = light;
    if (light != NULL)
        for (i = 0; i < 256u; i++)
            s_luz[i] = color_sombra(light, i);
}

static inline uint32_t luz_de(const uint32_t *light, uint32_t s)
{
    if (light == s_luz_de && light != NULL)
        return s_luz[s & 0xFFu];
    return color_sombra(light, s);
}

/* El haz de los faros de noche de la vista que se esta dibujando (ver NightBeam), o NULL.
 * Lo fija track_draw; solo lo usa la pista. */
static const NightBeam *s_haz;

/* El indice de luz `s` de un vertice de la pista en `w` (metros, espacio del mundo) con el
 * haz encima: Night_NightCalc, como track_colour en el shader de la referencia. */
static uint32_t sombra_haz(const NightBeam *b, const float w[3], uint32_t s)
{
    float d[3];
    int32_t x, z, mitad;
    uint32_t k;

    for (k = 0; k < 3u; k++)
        d[k] = (w[k] - b->origin[k]) * 64.0f;
    x = (int32_t)floorf(d[0] * b->right[0] + d[1] * b->right[1] + d[2] * b->right[2]);
    z = (int32_t)floorf(d[0] * b->forward[0] + d[1] * b->forward[1] + d[2] * b->forward[2]);
    mitad = (int32_t)1 << (b->xs + 5u);
    if (z <= b->z0 || z >= b->z0 + ((int32_t)1 << (b->zs + 6u)) || x <= -mitad || x >= mitad)
        return s;
    k = b->map[(((uint32_t)(z - b->z0) >> b->zs) & 63u) * 64u + (((uint32_t)(x + mitad) >> b->xs) & 63u)];
    return b->table[(s & 0xFFu) * 16u + (k & 15u)];
}

/* Si el triangulo mira a la camara. La pista y los objetos fijos son de UNA cara, como en
 * PS1 (el NCLIP de DrawW_DrawQuad) y como en la referencia (FrontFace::Ccw con Face::Back):
 * sin esto se ven las espaldas de muros y laderas, que tapan lo que hay detras, y los rotulos
 * (un cuadro delante y otro detras, coplanares) pelean en el Z. En espacio de camara, con la
 * X a la derecha, la Y arriba y la Z adelante, el triangulo gira a izquierdas en pantalla si
 * p0 . (p1 x p2) > 0; es la cara del plano en que esta la camara, asi que vale tambien para
 * los que cruzan el plano cercano. */
static int de_frente(const Cv *a, const Cv *b, const Cv *c)
{
    float nx = b->y * c->z - b->z * c->y;
    float ny = b->z * c->x - b->x * c->z;
    float nz = b->x * c->y - b->y * c->x;

    return a->x * nx + a->y * ny + a->z * nz > 0.0f;
}

/* Dibuja una malla: `verts` es su charco y `quads` sus caras. */
/* Dibuja una malla colocada en `o` (metros, espacio de PS1) y, si `mat` no es NULL,
 * girada y escalada por el (3x3 por filas, ya transpuesta por quien llama). La pista usa
 * `mat` NULL; los objetos del escenario lo usan siempre. `dos_caras`: sin la prueba de cara
 * (los billboard; los materiales con el indicador 1 tampoco la pasan). */
/* Un vertice de la malla ya transformado. Los cuadros comparten vertices (cada uno se usaba
 * hasta cuatro veces), asi que se calcula la primera vez que se pide en esta malla. `ok`: esta
 * delante del plano cercano y dentro de la banda de guarda, asi que no hay que recortar. */
typedef struct {
    uint32_t gen;
    float x, y, z;
    uint32_t argb;
    float sx, sy, iz;
    uint32_t ok;
    uint32_t fuera;           /* fuera_guarda, solo si no esta `ok` (si lo esta, 0) */
} MVert;

#define MV_MAX 1024u
static MVert s_mv[MV_MAX];
static uint32_t s_mv_gen;

/* La matriz de una malla: de las coordenadas enteras del horneado (1/64 de metro) a la camara.
 * Es lo que se hacia por vertice: girar con `mat`, sumar `p`, espejar la X, restar la camara y
 * aplicar la vista. */
static void mv_matriz(const View *vw, const float p[3], const float *mat, float mm[3][3], float tt[3])
{
    float d[3];
    uint32_t f, k;

    d[0] = -p[0] - vw->cam[0];
    d[1] = p[1] - vw->cam[1];
    d[2] = p[2] - vw->cam[2];
    for (f = 0; f < 3u; f++) {
        for (k = 0; k < 3u; k++) {
            float g0 = mat != NULL ? mat[k] : (k == 0u ? 1.0f : 0.0f);
            float g1 = mat != NULL ? mat[3u + k] : (k == 1u ? 1.0f : 0.0f);
            float g2 = mat != NULL ? mat[6u + k] : (k == 2u ? 1.0f : 0.0f);

            mm[f][k] = (-vw->row[f][0] * g0 + vw->row[f][1] * g1 + vw->row[f][2] * g2) / 64.0f;
        }
        tt[f] = vw->row[f][0] * d[0] + vw->row[f][1] * d[1] + vw->row[f][2] * d[2];
    }
}

static inline void mv_calcula(MVert *o, const View *vw, const int16_t *v, const uint32_t *light,
                              const float mm[3][3], const float tt[3],
                              const float p[3], const float *mat)
{
    uint32_t s;

    vm_aplica(mm, tt, (float)v[0], (float)v[1], (float)v[2], &o->x, &o->y, &o->z);
    s = (uint32_t)(uint16_t)v[3] & 0xFFu;
    /* de noche, la pista dentro del haz de los faros */
    if (s_haz != NULL && mat == NULL) {
        float w[3];

        w[0] = -(p[0] + (float)v[0] / 64.0f);
        w[1] = p[1] + (float)v[1] / 64.0f;
        w[2] = p[2] + (float)v[2] / 64.0f;
        s = sombra_haz(s_haz, w, s);
    }
    o->argb = luz_de(light, s);
    o->ok = 0;
    if (o->z >= NEAR && !vw->recorta_y) {
        o->iz = vm_inv(o->z);
        o->sx = vw->ox + o->x * vw->fx * o->iz;
        o->sy = vw->oy - o->y * vw->fy * o->iz;
        o->ok = dentro_guarda(o->sx, o->sy);
    }
    o->fuera = o->ok ? 0u : fuera_guarda(vw, o->x, o->y, o->z);
}

/* El doble del area en pantalla (la Y va hacia abajo): negativa si el triangulo mira a la
 * camara. Delante del plano cercano es la misma prueba que de_frente, con menos cuentas. */
static float area_pantalla(const MVert *a, const MVert *b, const MVert *c)
{
    return (b->sx - a->sx) * (c->sy - a->sy) - (c->sx - a->sx) * (b->sy - a->sy);
}

static void tv_pon(TrackVert *o, const MVert *p, float u, float v)
{
    o->x = p->sx;
    o->y = p->sy;
    o->z = p->iz;
    o->u = u;
    o->v = v;
    o->argb = p->argb;
}

static void malla(const Track *t, const View *vw, const uint32_t *light,
                  const float o[3], const float *mat, int dos_caras,
                  const int16_t *verts, uint32_t nv,
                  const uint16_t *quads, uint32_t nq,
                  TrackEmit emit, void *user, TrackStats *st)
{
    /* M8: por el inverso, sin dividir 8 veces por cuadro (el atlas mide potencias de dos,
     * asi que el resultado es el mismo) */
    float au = 1.0f / (float)t->atlas_w, av = 1.0f / (float)t->atlas_h;
    float mm[3][3], tt[3];
    uint32_t q;

    if (nq == 0u)
        return;
    mv_matriz(vw, o, mat, mm, tt);
    vm_carga(mm, tt);
    s_mv_gen++;
    for (q = 0; q < nq; q++) {
        const uint16_t *f = quads + q * 5u;
        const TrackMat *m = &t->mat[f[4] < t->mats ? f[4] : 0u];
        MVert propio[4];
        const MVert *pv[4];
        float cu[4], cvv[4], u[4], v[4];
        uint32_t k;
        int dos;

        if (st != NULL)
            st->quads_in++;

        /* Medio texel hacia dentro: el texel de al lado es la separacion del atlas, que vale 0
         * (transparente). Muestreando sin filtro justo en el borde, el PVR toma a veces ese
         * texel y la lista punch-through descarta el pixel: salen puntos y rayas de lo que hay
         * detras (el cielo sobre las laderas, el fondo sobre el asfalto). PS1 tampoco llega
         * al borde: sus UV van de x a x + w - 1. */
        cu[0] = (float)m->x + 0.5f;           cvv[0] = (float)m->y + 0.5f;
        cu[1] = (float)(m->x + m->w) - 0.5f;  cvv[1] = (float)m->y + 0.5f;
        cu[2] = (float)m->x + 0.5f;           cvv[2] = (float)(m->y + m->h) - 0.5f;
        cu[3] = (float)(m->x + m->w) - 0.5f;  cvv[3] = (float)(m->y + m->h) - 0.5f;

        for (k = 0; k < 4u; k++) {
            uint32_t vi = f[k], e = m->corner[k] & 3u;
            MVert *p;

            if (vi >= nv)
                vi = 0;
            if (vi < MV_MAX) {
                p = &s_mv[vi];
                if (p->gen != s_mv_gen) {
                    mv_calcula(p, vw, verts + (size_t)vi * 4u, light, mm, tt, o, mat);
                    p->gen = s_mv_gen;
                }
            } else {
                p = &propio[k];
                mv_calcula(p, vw, verts + (size_t)vi * 4u, light, mm, tt, o, mat);
            }
            pv[k] = p;
            u[k] = cu[e] * au;
            v[k] = cvv[e] * av;
        }
        dos = dos_caras || (m->flags & 1u) != 0u;

        /* Lo normal: nada que recortar. El cuadro va como UNA tira de cuatro vertices y no
         * como dos triangulos de tres. El sumidero la monta 0, 1, 3, 2: empezando por el
         * vertice 1 la diagonal es la 0-2, la misma de (0,1,2) y (0,2,3). */
        if (pv[0]->ok && pv[1]->ok && pv[2]->ok && pv[3]->ok) {
            int f1 = dos || area_pantalla(pv[0], pv[1], pv[2]) < 0.0f;
            int f2 = dos || area_pantalla(pv[0], pv[2], pv[3]) < 0.0f;
            TrackVert out[4];

            if (f1 && f2) {
                tv_pon(&out[0], pv[1], u[1], v[1]);
                tv_pon(&out[1], pv[2], u[2], v[2]);
                tv_pon(&out[2], pv[3], u[3], v[3]);
                tv_pon(&out[3], pv[0], u[0], v[0]);
                emit(user, out, 4u, m);
                if (st != NULL)
                    st->quads_out += 2u;
            } else if (f1 || f2) {
                uint32_t b = f1 ? 1u : 2u;

                tv_pon(&out[0], pv[0], u[0], v[0]);
                tv_pon(&out[1], pv[b], u[b], v[b]);
                tv_pon(&out[2], pv[b + 1u], u[b + 1u], v[b + 1u]);
                emit(user, out, 3u, m);
                if (st != NULL) {
                    st->quads_out++;
                    st->espalda++;
                }
            } else if (st != NULL) {
                st->espalda += 2u;
            }
            continue;
        }

        /* M8: entero fuera del mismo plano de la banda de guarda, o detras del cercano: no se
         * ve (ver sky_quad), y no hace falta partirlo ni recortarlo */
        {
            uint32_t todos = 0x1Fu;

            for (k = 0; k < 4u; k++)
                todos &= pv[k]->fuera;
            if (todos != 0u)
                continue;
        }

        /* Los vertices del cuadro van en orden CICLICO alrededor de el, no en zigzag,
         * asi que se parte en (0,1,2) y (0,2,3). Partirlo como una tira deja huecos
         * triangulares por toda la pista. */
        {
            Cv tri[3];
            static const uint8_t TRI[2][3] = { {0, 1, 2}, {0, 2, 3} };
            uint32_t a;

            for (a = 0; a < 2u; a++) {
                for (k = 0; k < 3u; k++) {
                    uint32_t i = TRI[a][k];

                    tri[k].x = pv[i]->x;
                    tri[k].y = pv[i]->y;
                    tri[k].z = pv[i]->z;
                    tri[k].u = u[i];
                    tri[k].v = v[i];
                    tri[k].argb = pv[i]->argb;
                }
                if (dos || de_frente(&tri[0], &tri[1], &tri[2]))
                    triangulo(vw, tri, m, emit, user, st);
                else if (st != NULL)
                    st->espalda++;
            }
        }
    }
}

void track_draw(const Track *t, const struct RaceView *view, const uint32_t *light,
                float sw, float sh, TrackEmit emit, void *user, TrackStats *st)
{
    View vw;
    uint32_t i;

    if (t->raw == NULL || t->blocks == 0u)
        return;
    if (st != NULL)
        memset(st, 0, sizeof(*st));
    view_init(&vw, view, sw, sh);
    s_haz = (view->beam != NULL && view->beam->on) ? view->beam : NULL;
    luz_prepara(light);

    for (i = 0; i < view->chunks_n; i++) {
        int16_t id = view->chunk[i];
        uint16_t det = view->detail[i];
        uint32_t flags = (uint32_t)(det >> 8) & 0xFFu;
        uint32_t rez = det & 0xFFu;
        const TrackBlock *b;
        uint32_t suelo;
        float og[3];

        if (id < 0 || (uint32_t)id >= t->blocks)
            continue;                    /* hueco en la lista */
        if ((flags & (CHUNK_GROUND | CHUNK_LINES)) == 0u)
            continue;                    /* este frame no pide nada de este trozo */
        b = &t->block[id];
        if (st != NULL)
            st->blocks_drawn++;

        /* resolucion distinta de cero = detalle completo; cero = la version lejana */
        suelo = rez != 0u ? (uint32_t)TB_GROUND_QUADS : (uint32_t)TB_GROUND_FAR;
        og[0] = (float)b->origin[0] / 64.0f;
        og[1] = (float)b->origin[1] / 64.0f;
        og[2] = (float)b->origin[2] / 64.0f;
        if ((flags & CHUNK_GROUND) != 0u) {
            malla(t, &vw, light, og, NULL, 0,
                  t->vert + (size_t)b->off[TB_GROUND_VERTS] * 4u,
                  b->n[TB_GROUND_VERTS],
                  t->quad + (size_t)b->off[suelo] * 5u, b->n[suelo],
                  emit, user, st);
            /* las superficies de detalle van siempre con el suelo */
            malla(t, &vw, light, og, NULL, 0,
                  t->vert + (size_t)b->off[TB_DETAIL_VERTS] * 4u,
                  b->n[TB_DETAIL_VERTS],
                  t->quad + (size_t)b->off[TB_DETAIL_QUADS] * 5u,
                  b->n[TB_DETAIL_QUADS],
                  emit, user, st);
        }
        /* las lineas pintadas, solo si el juego las pide para este trozo (indicador 4) */
        if ((flags & CHUNK_LINES) != 0u)
            malla(t, &vw, light, og, NULL, 0,
                  t->vert + (size_t)b->off[TB_LINE_VERTS] * 4u,
                  b->n[TB_LINE_VERTS],
                  t->quad + (size_t)b->off[TB_LINE_QUADS] * 5u,
                  b->n[TB_LINE_QUADS],
                  emit, user, st);
    }
}

/* ---- los objetos del escenario (M2c paso 2) ----
 *
 * El juego ya decidio cuales se dibujan y donde (host/objects.c). Aqui solo se colocan:
 *
 *   - los **fijos** se colocan con la **transpuesta** de su matriz. El original carga el
 *     GTE con (M * vista)^T = vista^T * M^T, asi que al modelo le toca M^T, o sea las
 *     COLUMNAS de la matriz tal como viene.
 *   - los **billboard** se giran hacia la camara alrededor de Y, y su escala sale de la
 *     longitud de las dos primeras columnas: la primera da el ancho y la segunda el alto.
 */
void objects_draw(const Track *t, const struct ObjectDraw *obj, uint32_t n,
                  const struct RaceView *view, const uint32_t *light,
                  float sw, float sh, TrackEmit emit, void *user, TrackStats *st)
{
    View vw;
    uint32_t i;

    if (st != NULL)
        memset(st, 0, sizeof(*st));
    if (t->raw == NULL || t->models == 0u)
        return;
    view_init(&vw, view, sw, sh);
    luz_prepara(light);

    for (i = 0; i < n; i++) {
        const struct ObjectDraw *o = &obj[i];
        const TrackModel *m;
        float mat[9], pos[3];
        uint32_t k;

        if (o->model >= t->models)
            continue;
        m = &t->model[o->model];
        if (m->quad_n == 0u)
            continue;

        pos[0] = (float)o->pos[0] / 65536.0f;
        pos[1] = (float)o->pos[1] / 65536.0f;
        pos[2] = (float)o->pos[2] / 65536.0f;

        if (o->billboard) {
            /* ancho y alto de las dos primeras columnas, y un giro hacia la camara */
            float c0[3], c1[3], an, al, dx, dz, d;

            for (k = 0; k < 3u; k++) {
                c0[k] = (float)o->matrix[k * 3u] / 65536.0f;
                c1[k] = (float)o->matrix[k * 3u + 1u] / 65536.0f;
            }
            an = len3(c0);
            al = len3(c1);
            /* La camara esta en espacio de MUNDO (X espejada) y el objeto en PS1, asi
             * que el vector hacia la camara es cam - (-pos.x, pos.y, pos.z). */
            dx = vw.cam[0] + pos[0];
            dz = vw.cam[2] - pos[2];
            d = sqrtf(dx * dx + dz * dz);
            if (d < 1e-4f) {
                dx = 0.0f;
                dz = 1.0f;
                d = 1.0f;
            }
            dx /= d;
            dz /= d;
            /* `malla` espeja la X al final, asi que la matriz que hay que darle no es
             * el giro R(theta) del mundo sino S*R*S*D, y S*R*S = R(-theta). Con
             * cos = dz y sin = dx (ya normalizados) y la escala por columnas: */
            mat[0] = dz * an;  mat[1] = 0.0f;  mat[2] = -dx * an;
            mat[3] = 0.0f;     mat[4] = al;    mat[5] = 0.0f;
            mat[6] = dx * an;  mat[7] = 0.0f;  mat[8] = dz * an;
        } else {
            /* la transpuesta: filas de `mat` = columnas de la matriz del juego */
            for (k = 0; k < 3u; k++) {
                mat[k * 3u] = (float)o->matrix[k] / 65536.0f;
                mat[k * 3u + 1u] = (float)o->matrix[k + 3u] / 65536.0f;
                mat[k * 3u + 2u] = (float)o->matrix[k + 6u] / 65536.0f;
            }
        }
        malla(t, &vw, light, pos, mat, o->billboard != 0,
              t->mvert + (size_t)m->vert_off * 4u, m->vert_n,
              t->mquad + (size_t)m->quad_off * 5u, m->quad_n,
              emit, user, st);
    }
}

/* ---- el cielo (M2b paso 4) ----
 *
 * Dos piezas, las dos CENTRADAS EN LA CAMARA: solo les afecta su giro, no su posicion.
 *
 *   - la **cupula**: 4 anillos x 16 segmentos sobre una malla de 5x17 puntos de radio
 *     1000, con color por vertice. Algunas pistas la texturan por cuadro (HZ01..HZ12, y
 *     los indices 12..15 son HZ09..HZ12 espejadas); otras la dejan en color plano.
 *   - la **banda de horizonte**: 16 segmentos alrededor de un anillo de 17 puntos de
 *     radio 32000, cada uno con su textura HZ01..HZ08 y tan alto como diga su forma
 *     frente a la mas alta del anillo. Las formas de 1x1 que algunas pistas usan de
 *     relleno no dibujan nada. En los circuitos la banda esta apagada.
 *
 * El color va sin doblar cuando el cuadro NO lleva textura, porque ahi PS1 pinta el
 * color del vertice tal cual; con textura se dobla, como en todo lo demas.
 */
static void sky_quad(const View *vw, const Cv *q, const TrackMat *m,
                     TrackEmit emit, void *user, TrackStats *st)
{
    Cv tri[3];
    uint32_t todos = 0x1Fu, k;

    /* M8: el cuadro entero fuera del mismo plano de la banda de guarda, o detras del plano
     * cercano, no se ve: la banda es un cono convexo con la camara en el vertice, asi que
     * si sus cuatro esquinas estan fuera del mismo plano, todo el cuadro lo esta. Casi toda
     * la cupula cae fuera, y se ahorra partirla en triangulos y recortarlos uno a uno. */
    for (k = 0; k < 4u; k++)
        todos &= fuera_guarda(vw, q[k].x, q[k].y, q[k].z);
    if (todos != 0u)
        return;

    tri[0] = q[0]; tri[1] = q[1]; tri[2] = q[2];
    triangulo(vw, tri, m, emit, user, st);
    tri[0] = q[0]; tri[1] = q[2]; tri[2] = q[3];
    triangulo(vw, tri, m, emit, user, st);
}

/* Pone un punto del cielo en espacio de camara (solo giro). */
static void sky_punto(const View *vw, Cv *o, float x, float y, float z)
{
    o->x = vw->row[0][0] * x + vw->row[0][1] * y + vw->row[0][2] * z;
    o->y = vw->row[1][0] * x + vw->row[1][1] * y + vw->row[1][2] * z;
    o->z = vw->row[2][0] * x + vw->row[2][1] * y + vw->row[2][2] * z;
}

void sky_draw(const Track *t, const struct SkyData *s, const struct RaceView *view,
              float sw, float sh, TrackEmit emit, void *user, TrackStats *st)
{
    View vw;
    TrackMat m;
    Cv q[4];
    Cv punto[85];             /* M8: los puntos de la cupula, girados una sola vez */
    uint32_t anillo, seg, k;
    float ky;
    /* por el inverso: el atlas mide potencias de dos, asi que da lo mismo que dividir */
    float iau = 1.0f / (float)t->atlas_w, iav = 1.0f / (float)t->atlas_h;
    int mas_alta = 1;

    if (st != NULL)
        memset(st, 0, sizeof(*st));
    if (!s->ok)
        return;
    view_init(&vw, view, sw, sh);
    memset(&m, 0, sizeof m);
    for (k = 0; k < 85u; k++) {
        const int16_t *v = s->dome[k];

        /* radio 1000 -> 100, y la X espejada como todo lo demas */
        sky_punto(&vw, &punto[k], -(float)v[0] * 0.1f, (float)v[1] * 0.1f, (float)v[2] * 0.1f);
    }

    /* ---- la cupula ---- */
    for (anillo = 0; anillo < 4u; anillo++) {
        for (seg = 0; seg < 16u; seg++) {
            uint32_t idx[4];
            const TrackSkyRect *r = NULL;
            int espejada = 0;

            idx[0] = anillo * 17u + seg;
            idx[1] = anillo * 17u + seg + 1u;
            idx[2] = (anillo + 1u) * 17u + seg + 1u;
            idx[3] = (anillo + 1u) * 17u + seg;

            if (s->dome_tex_ok) {
                uint32_t c = s->dome_tex[anillo * 16u + seg];
                uint32_t forma = c < 12u ? c : c - 4u;   /* 12..15 son HZ09..HZ12 */

                espejada = c >= 12u;
                if (forma < 12u && t->sky[forma].w != 0u)
                    r = &t->sky[forma];
            }
            for (k = 0; k < 4u; k++) {
                const uint8_t *c = s->dome_rgb[idx[k]];

                q[k].x = punto[idx[k]].x;
                q[k].y = punto[idx[k]].y;
                q[k].z = punto[idx[k]].z;
                if (r != NULL) {
                    /* el borde alto de la textura va en el anillo de arriba */
                    static const uint8_t esq[4] = { 0, 1, 1, 0 };  /* izquierda/derecha */
                    static const uint8_t alto[4] = { 1, 1, 0, 0 }; /* abajo/arriba */
                    /* medio texel hacia dentro, como en malla() */
                    float iz = espejada ? (float)r->w - 0.5f : 0.5f;
                    float de = espejada ? 0.5f : (float)r->w - 0.5f;

                    q[k].u = ((float)r->x + (esq[k] != 0u ? de : iz)) * iau;
                    q[k].v = ((float)r->y + (alto[k] != 0u ? (float)r->h - 0.5f : 0.5f)) * iav;
                    q[k].argb = dobla(c);
                } else {
                    q[k].u = 0.0f;
                    q[k].v = 0.0f;
                    /* sin textura PS1 pinta el color del vertice tal cual */
                    q[k].argb = 0xFF000000u | ((uint32_t)c[0] << 16) |
                                ((uint32_t)c[1] << 8) | c[2];
                }
            }
            sky_quad(&vw, q, r != NULL ? &m : NULL, emit, user, st);
        }
    }

    /* ---- la banda de horizonte ---- */
    if (!s->band)
        return;
    for (seg = 0; seg < 16u; seg++) {
        uint32_t f = (uint32_t)s->segment_tex[seg] & 7u;
        int e = t->sky[f].h != 0 ? (int)t->sky[f].h - 1 : 0;

        if (e > mas_alta)
            mas_alta = e;
    }
    ky = 100.0f / 32000.0f;
    for (seg = 0; seg < 16u; seg++) {
        uint32_t f = (uint32_t)s->segment_tex[seg] & 7u;
        const TrackSkyRect *r = &t->sky[f];
        int e = r->h != 0 ? (int)r->h - 1 : 0;
        float ratio, y0, y1;
        static const uint8_t pi[4] = { 0, 1, 1, 0 };   /* punto del anillo */
        static const uint8_t arr[4] = { 1, 1, 0, 0 };  /* borde alto */

        if (r->w == 0u || e <= 0)
            continue;                 /* forma de relleno: no dibuja nada */
        ratio = (float)e / (float)mas_alta;
        y0 = (float)s->bottom * ky;
        y1 = ((float)s->bottom + (float)s->height * ratio) * ky;
        for (k = 0; k < 4u; k++) {
            uint32_t i = seg + pi[k];
            const uint8_t *c = arr[k] != 0u ? s->top_rgb[i] : s->bottom_rgb[i];

            sky_punto(&vw, &q[k], -(float)s->ring[i][0] * ky,
                      arr[k] != 0u ? y1 : y0, (float)s->ring[i][1] * ky);
            q[k].u = ((float)r->x + (pi[k] != 0u ? (float)r->w - 0.5f : 0.5f)) * iau;
            q[k].v = ((float)r->y + (arr[k] != 0u ? 0.5f : (float)r->h - 0.5f)) * iav;
            q[k].argb = dobla(c);
        }
        sky_quad(&vw, q, &m, emit, user, st);
    }
}

uint32_t sky_zenith(const struct SkyData *s)
{
    if (!s->ok)
        return 0;
    /* el anillo 4 es el mas alto de los cinco */
    return 0xFF000000u | ((uint32_t)s->dome_rgb[4 * 17][0] << 16) |
           ((uint32_t)s->dome_rgb[4 * 17][1] << 8) | s->dome_rgb[4 * 17][2];
}
