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
    uint32_t off_blk, off_mat, off_sky, off_mdl, off_mvt, off_mqd, off_vtx, off_qd;
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

    if (memcmp(t->raw, "NFST", 4) != 0 || rd32(t->raw + 4) != 3u) {
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
    off_mat = off_blk + (uint32_t)t->blocks * 44u;
    off_sky = off_mat + (uint32_t)t->mats * 16u;
    off_mdl = off_sky + 12u * 8u;
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
    for (i = 0; i < 12u; i++) {
        const uint8_t *p = t->raw + off_sky + (size_t)i * 8u;

        t->sky[i].x = rd16(p);
        t->sky[i].y = rd16(p + 2u);
        t->sky[i].w = rd16(p + 4u);
        t->sky[i].h = rd16(p + 6u);
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
    vw->ox = sw * 0.5f;
    vw->oy = sh * 0.5f;
}

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
void triangulo(const View *vw, const Cv *tri, const TrackMat *m,
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

/* Dibuja una malla: `verts` es su charco y `quads` sus caras. */
/* Dibuja una malla colocada en `o` (metros, espacio de PS1) y, si `mat` no es NULL,
 * girada y escalada por el (3x3 por filas, ya transpuesta por quien llama). La pista usa
 * `mat` NULL; los objetos del escenario lo usan siempre. */
static void malla(const Track *t, const View *vw, const uint32_t *light,
                  const float o[3], const float *mat,
                  const int16_t *verts, uint32_t nv,
                  const uint16_t *quads, uint32_t nq,
                  TrackEmit emit, void *user, TrackStats *st)
{
    uint32_t q;

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
            {
                float pz[3], g[3];

                pz[0] = (float)v[0] / 64.0f;
                pz[1] = (float)v[1] / 64.0f;
                pz[2] = (float)v[2] / 64.0f;
                if (mat != NULL) {
                    g[0] = mat[0] * pz[0] + mat[1] * pz[1] + mat[2] * pz[2];
                    g[1] = mat[3] * pz[0] + mat[4] * pz[1] + mat[5] * pz[2];
                    g[2] = mat[6] * pz[0] + mat[7] * pz[1] + mat[8] * pz[2];
                } else {
                    g[0] = pz[0]; g[1] = pz[1]; g[2] = pz[2];
                }
                /* todo se calcula en espacio de PS1 y se espeja la X al final */
                w[0] = -(o[0] + g[0]);
                w[1] = o[1] + g[1];
                w[2] = o[2] + g[2];
            }
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
        float og[3];

        if (id < 0 || (uint32_t)id >= t->blocks)
            continue;                    /* hueco en la lista */
        if ((flags & CHUNK_GROUND) == 0u)
            continue;                    /* este frame no pide su suelo */
        b = &t->block[id];
        if (st != NULL)
            st->blocks_drawn++;

        /* resolucion distinta de cero = detalle completo; cero = la version lejana */
        suelo = rez != 0u ? (uint32_t)TB_GROUND_QUADS : (uint32_t)TB_GROUND_FAR;
        og[0] = (float)b->origin[0] / 64.0f;
        og[1] = (float)b->origin[1] / 64.0f;
        og[2] = (float)b->origin[2] / 64.0f;
        malla(t, &vw, light, og, NULL,
              t->vert + (size_t)b->off[TB_GROUND_VERTS] * 4u,
              b->n[TB_GROUND_VERTS],
              t->quad + (size_t)b->off[suelo] * 5u, b->n[suelo],
              emit, user, st);
        /* las superficies de detalle van siempre con el suelo */
        malla(t, &vw, light, og, NULL,
              t->vert + (size_t)b->off[TB_DETAIL_VERTS] * 4u,
              b->n[TB_DETAIL_VERTS],
              t->quad + (size_t)b->off[TB_DETAIL_QUADS] * 5u,
              b->n[TB_DETAIL_QUADS],
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
        malla(t, &vw, light, pos, mat,
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
    uint32_t anillo, seg, k;
    float ky;
    int mas_alta = 1;

    if (st != NULL)
        memset(st, 0, sizeof(*st));
    if (!s->ok)
        return;
    view_init(&vw, view, sw, sh);
    memset(&m, 0, sizeof m);

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
                const int16_t *v = s->dome[idx[k]];
                const uint8_t *c = s->dome_rgb[idx[k]];

                /* radio 1000 -> 100, y la X espejada como todo lo demas */
                sky_punto(&vw, &q[k], -(float)v[0] * 0.1f, (float)v[1] * 0.1f,
                          (float)v[2] * 0.1f);
                if (r != NULL) {
                    /* el borde alto de la textura va en el anillo de arriba */
                    static const uint8_t esq[4] = { 0, 1, 1, 0 };  /* izquierda/derecha */
                    static const uint8_t alto[4] = { 1, 1, 0, 0 }; /* abajo/arriba */
                    float iz = espejada ? (float)r->w : 0.0f;
                    float de = espejada ? 0.0f : (float)r->w;

                    q[k].u = ((float)r->x + (esq[k] != 0u ? de : iz)) / (float)t->atlas_w;
                    q[k].v = ((float)r->y + (alto[k] != 0u ? (float)r->h : 0.0f)) /
                             (float)t->atlas_h;
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
            q[k].u = ((float)r->x + (pi[k] != 0u ? (float)r->w : 0.0f)) / (float)t->atlas_w;
            q[k].v = ((float)r->y + (arr[k] != 0u ? 0.0f : (float)r->h)) / (float)t->atlas_h;
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
