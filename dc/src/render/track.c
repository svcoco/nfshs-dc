/* La pista en pantalla. Ver track.h para el convenio de la camara. */
#include "host.h"
#include "track.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* El registro de material SI se mira en el sitio, asi que su tamano tiene que ser
 * exactamente el que hornea bake.rs. */
_Static_assert(sizeof(TrackMat) == 16, "TrackMat tiene que medir 16 bytes");

/* Plano cercano, en metros. Por delante de el no se puede dividir por z. */
#define NEAR 0.25f

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
    uint32_t off_blk, off_mat, off_vtx, off_qd, atlas_off, i;

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

    if (memcmp(t->raw, "NFST", 4) != 0 || rd32(t->raw + 4) != 1u) {
        track_free(t);
        return 0;
    }
    t->blocks = rd16(t->raw + 8);
    t->mats = rd16(t->raw + 10);
    t->atlas_w = rd16(t->raw + 12);
    t->atlas_h = rd16(t->raw + 14);
    t->verts = rd32(t->raw + 16);
    t->quads = rd32(t->raw + 20);
    atlas_off = rd32(t->raw + 24);

    /* El horneado ya se verifico a si mismo en el PC (bake.rs); aqui solo se comprueba
     * que los tramos caben, porque un archivo truncado por el disco si es posible. */
    off_blk = 28u;
    off_mat = off_blk + (uint32_t)t->blocks * 44u;
    off_vtx = off_mat + (uint32_t)t->mats * 16u;
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
        const uint8_t *p = t->raw + off_blk + (size_t)i * 44u;
        uint32_t k;

        for (k = 0; k < 3u; k++)
            t->block[i].origin[k] = (int32_t)rd32(p + k * 4u);
        for (k = 0; k < 5u; k++) {
            t->block[i].off[k] = rd32(p + 12u + k * 6u);
            t->block[i].n[k] = rd16(p + 16u + k * 6u);
        }
    }
    t->mat = (const TrackMat *)(const void *)(t->raw + off_mat);
    t->vert = (const int16_t *)(const void *)(t->raw + off_vtx);
    t->quad = (const uint16_t *)(const void *)(t->raw + off_qd);
    t->atlas = (const uint16_t *)(const void *)(t->raw + atlas_off);
    return 1;
}

void track_free(Track *t)
{
    free(t->block);
    free(t->raw);
    memset(t, 0, sizeof(*t));
}

/* ---- la camara ---- */

typedef struct {
    float cam[3];             /* posicion, en metros y en espacio de mundo */
    float row[3][3];          /* derecha, arriba y adelante, divididas por |adelante| */
    float focal;              /* projection, en pixeles de la pantalla de PS1 */
    float ox, oy;             /* centro de la pantalla de salida */
    float kx, ky;             /* de pixeles de PS1 a pixeles de salida */
} View;

static float len3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void view_init(View *vw, const struct RaceView *v, float sw, float sh)
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
    vw->ox = sw * 0.5f;
    vw->oy = sh * 0.5f;
}

/* ---- un vertice en espacio de camara ---- */

typedef struct {
    float x, y, z;            /* camara: x derecha, y arriba, z adelante (metros) */
    float u, v;               /* atlas, 0..1 */
    uint32_t argb;
} Cv;

static void proyecta(const View *vw, const Cv *c, TrackVert *o)
{
    float inv = 1.0f / c->z;

    o->x = vw->ox + c->x * vw->focal * inv * vw->kx;
    o->y = vw->oy - c->y * vw->focal * inv * vw->ky;
    o->z = inv;               /* el PVR quiere 1/w */
    o->u = c->u;
    o->v = c->v;
    o->argb = c->argb;
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
static void triangulo(const View *vw, const Cv *tri, const TrackMat *m,
                      TrackEmit emit, void *user, TrackStats *st)
{
    Cv dentro[4];
    TrackVert out[4];
    uint32_t n = 0, i, k;
    int cuantos = 0;

    for (i = 0; i < 3u; i++)
        cuantos += tri[i].z >= NEAR;
    if (cuantos == 0) {
        if (st != NULL)
            st->behind++;
        return;
    }
    if (cuantos == 3) {
        for (i = 0; i < 3u; i++)
            proyecta(vw, &tri[i], &out[i]);
        emit(user, out, 3u, m);
        if (st != NULL)
            st->quads_out++;
        return;
    }

    /* Sutherland-Hodgman sobre un solo plano. */
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
    for (k = 0; k < n; k++)
        proyecta(vw, &dentro[k], &out[k]);
    emit(user, out, n, m);
    if (st != NULL) {
        st->quads_out++;
        st->clipped++;
    }
}

/* ---- el dibujo ---- */

static uint32_t color_sombra(const uint32_t *light, uint32_t s)
{
    uint32_t c;

    if (light == NULL)
        return 0xFF808080u;   /* gris neutro: el 0x80 de PS1 es el x1 */
    c = light[s & 0xFFu];
    /* Chunk_lightTable guarda 0xBBGGRR; el PVR quiere ARGB */
    return 0xFF000000u | ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
}

/* Dibuja una malla: `verts` es su charco y `quads` sus caras. */
static void malla(const Track *t, const View *vw, const uint32_t *light,
                  const int32_t origin[3], const int16_t *verts, uint32_t nv,
                  const uint16_t *quads, uint32_t nq,
                  TrackEmit emit, void *user, TrackStats *st)
{
    float o[3];
    uint32_t q;

    /* el origen del bloque, ya en metros y con la X espejada */
    o[0] = -(float)origin[0] / 64.0f;
    o[1] = (float)origin[1] / 64.0f;
    o[2] = (float)origin[2] / 64.0f;

    if (getenv("PS1_TRACKDBG") != NULL && nq != 0u) {
        const int16_t *v0 = verts;
        float w0[3], d0[3], z0;

        w0[0] = o[0] - (float)v0[0] / 64.0f;
        w0[1] = o[1] + (float)v0[1] / 64.0f;
        w0[2] = o[2] + (float)v0[2] / 64.0f;
        d0[0] = w0[0] - vw->cam[0];
        d0[1] = w0[1] - vw->cam[1];
        d0[2] = w0[2] - vw->cam[2];
        z0 = vw->row[2][0] * d0[0] + vw->row[2][1] * d0[1] + vw->row[2][2] * d0[2];
        printf("    DBG cam (%.1f %.1f %.1f) adelante (%.3f %.3f %.3f) "
               "origen (%.1f %.1f %.1f) v0 (%.1f %.1f %.1f) z=%.2f\n",
               (double)vw->cam[0], (double)vw->cam[1], (double)vw->cam[2],
               (double)vw->row[2][0], (double)vw->row[2][1], (double)vw->row[2][2],
               (double)o[0], (double)o[1], (double)o[2],
               (double)w0[0], (double)w0[1], (double)w0[2], (double)z0);
    }
    for (q = 0; q < nq; q++) {
        const uint16_t *f = quads + q * 5u;
        const TrackMat *m = &t->mat[f[4] < t->mats ? f[4] : 0u];
        Cv cv[4];
        Cv tri[3];
        uint32_t k;
        float au = (float)t->atlas_w, av = (float)t->atlas_h;
        /* las cuatro esquinas del rectangulo del material, en el atlas */
        float cu[4], cvv[4];

        if (st != NULL)
            st->quads_in++;

        cu[0] = (float)m->x;            cvv[0] = (float)m->y;
        cu[1] = (float)(m->x + m->w);   cvv[1] = (float)m->y;
        cu[2] = (float)m->x;            cvv[2] = (float)(m->y + m->h);
        cu[3] = (float)(m->x + m->w);   cvv[3] = (float)(m->y + m->h);

        for (k = 0; k < 4u; k++) {
            uint32_t vi = f[k];
            const int16_t *v;
            float w[3], d[3];
            uint32_t e = m->corner[k] & 3u;

            if (vi >= nv)
                vi = 0;
            v = verts + (size_t)vi * 4u;
            /* 1/64 m y la X espejada, igual que el origen */
            w[0] = o[0] - (float)v[0] / 64.0f;
            w[1] = o[1] + (float)v[1] / 64.0f;
            w[2] = o[2] + (float)v[2] / 64.0f;
            d[0] = w[0] - vw->cam[0];
            d[1] = w[1] - vw->cam[1];
            d[2] = w[2] - vw->cam[2];
            cv[k].x = vw->row[0][0] * d[0] + vw->row[0][1] * d[1] + vw->row[0][2] * d[2];
            cv[k].y = vw->row[1][0] * d[0] + vw->row[1][1] * d[1] + vw->row[1][2] * d[2];
            cv[k].z = vw->row[2][0] * d[0] + vw->row[2][1] * d[1] + vw->row[2][2] * d[2];
            cv[k].u = cu[e] / au;
            cv[k].v = cvv[e] / av;
            cv[k].argb = color_sombra(light, (uint32_t)(uint16_t)v[3] & 0xFFu);
        }

        /* Los vertices del cuadro van en orden CICLICO alrededor de el, no en zigzag,
         * asi que se parte en (0,1,2) y (0,2,3). Partirlo como una tira deja huecos
         * triangulares por toda la pista. */
        tri[0] = cv[0]; tri[1] = cv[1]; tri[2] = cv[2];
        triangulo(vw, tri, m, emit, user, st);
        tri[0] = cv[0]; tri[1] = cv[2]; tri[2] = cv[3];
        triangulo(vw, tri, m, emit, user, st);
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

    for (i = 0; i < view->chunks_n; i++) {
        int16_t id = view->chunk[i];
        uint16_t det = view->detail[i];
        uint32_t flags = (uint32_t)(det >> 8) & 0xFFu;
        uint32_t rez = det & 0xFFu;
        const TrackBlock *b;
        uint32_t suelo;

        if (id < 0 || (uint32_t)id >= t->blocks)
            continue;                    /* hueco en la lista */
        if ((flags & CHUNK_GROUND) == 0u)
            continue;                    /* este frame no pide su suelo */
        b = &t->block[id];
        if (st != NULL)
            st->blocks_drawn++;

        /* resolucion distinta de cero = detalle completo; cero = la version lejana */
        suelo = rez != 0u ? (uint32_t)TB_GROUND_QUADS : (uint32_t)TB_GROUND_FAR;
        malla(t, &vw, light, b->origin,
              t->vert + (size_t)b->off[TB_GROUND_VERTS] * 4u,
              b->n[TB_GROUND_VERTS],
              t->quad + (size_t)b->off[suelo] * 5u, b->n[suelo],
              emit, user, st);
        /* las superficies de detalle van siempre con el suelo */
        malla(t, &vw, light, b->origin,
              t->vert + (size_t)b->off[TB_DETAIL_VERTS] * 4u,
              b->n[TB_DETAIL_VERTS],
              t->quad + (size_t)b->off[TB_DETAIL_QUADS] * 5u,
              b->n[TB_DETAIL_QUADS],
              emit, user, st);
    }
}
