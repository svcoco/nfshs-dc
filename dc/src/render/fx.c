/* Efectos de carrera. Ver fx.h. */
#include "host.h"
#include "fx.h"
#include "trackint.h"

#include <math.h>
#include <string.h>

/* El color de un canal de 0..255 con el 0x80 de PS1 como 1,0: se dobla y se satura. */
static uint32_t doblar(uint32_t canal)
{
    canal *= 2u;
    return canal > 255u ? 255u : canal;
}

void skids_draw(const Track *t, const struct SkidQuad *sk, uint32_t n,
                const struct RaceView *view, float sw, float sh,
                TrackEmit emit, void *user, TrackStats *st)
{
    static const int per[4] = { 0, 1, 3, 2 };   /* perimetro del cuadro */
    View vw;
    TrackMat mat;
    uint32_t i;

    if (st != NULL)
        memset(st, 0, sizeof(*st));
    if (t->raw == NULL || n == 0u)
        return;
    view_init(&vw, view, sw, sh);
    memset(&mat, 0, sizeof mat);

    for (i = 0; i < n; i++) {
        const struct SkidQuad *q = &sk[i];
        const TrackSkyRect *r = &t->sfx[SFX_SKD0 + (q->tex & 1u)];
        float w[4][3];
        float dx, dz, mayor, menor;
        uint32_t k, j, ok = 0;
        Cv v[4];
        uint32_t col[2][3];

        if (r->w == 0u)
            continue;
        /* en la lista de trozos? (si no hay lista, valen todas) */
        if (view->chunks_n == 0u) {
            ok = 1;
        } else {
            int sec = q->slice / 8;

            for (j = 0; j < view->chunks_n; j++)
                if (view->chunk[j] == sec) {
                    ok = 1;
                    break;
                }
        }
        if (!ok)
            continue;

        /* a mundo: X espejada */
        for (k = 0; k < 4u; k++) {
            w[k][0] = -(float)q->corners[k][0] / 65536.0f;
            w[k][1] = (float)q->corners[k][1] / 65536.0f;
            w[k][2] = (float)q->corners[k][2] / 65536.0f;
        }
        /* Draw_CircleClip: a menos de 50 m, con su estimacion de la distancia */
        dx = fabsf(w[0][0] - vw.cam[0]);
        dz = fabsf(w[0][2] - vw.cam[2]);
        mayor = dx > dz ? dx : dz;
        menor = dx > dz ? dz : dx;
        if (mayor + menor / 4.0f >= 50.0f)
            continue;

        for (j = 0; j < 2u; j++) {
            col[j][0] = doblar(q->colour[j] & 0xFFu);
            col[j][1] = doblar((q->colour[j] >> 8) & 0xFFu);
            col[j][2] = doblar((q->colour[j] >> 16) & 0xFFu);
        }

        for (k = 0; k < 4u; k++) {
            int c = per[k];
            float p[3], to[3], dist, empuje, dd[3];
            float u = (c & 1) ? (float)r->w - 0.5f : 0.5f;
            float vv = (c & 2) ? (float)r->h - 0.5f : 0.5f;
            /* los dos primeros puntos son los de esta entrada; los otros, los de la anterior */
            const uint32_t *cc = col[c >= 2 ? 1 : 0];

            p[0] = w[c][0];
            p[1] = w[c][1] + 0.02f;
            p[2] = w[c][2];
            /* el original no tiene buffer de profundidad: acercar el cuadro a la camara
             * (over_road de la referencia) para que no se hunda bajo la carretera */
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
            v[k].x = vw.row[0][0] * dd[0] + vw.row[0][1] * dd[1] + vw.row[0][2] * dd[2];
            v[k].y = vw.row[1][0] * dd[0] + vw.row[1][1] * dd[1] + vw.row[1][2] * dd[2];
            v[k].z = vw.row[2][0] * dd[0] + vw.row[2][1] * dd[1] + vw.row[2][2] * dd[2];
            v[k].u = ((float)r->x + u) / (float)t->atlas_w;
            v[k].v = ((float)r->y + vv) / (float)t->atlas_h;
            v[k].argb = 0xFF000000u | (cc[0] << 16) | (cc[1] << 8) | cc[2];
        }
        {
            Cv tri[3];

            tri[0] = v[0]; tri[1] = v[1]; tri[2] = v[2];
            triangulo(&vw, tri, &mat, emit, user, st);
            tri[0] = v[0]; tri[1] = v[2]; tri[2] = v[3];
            triangulo(&vw, tri, &mat, emit, user, st);
        }
    }
}

/* ---- la profundidad de los destellos ---- */

static float punto3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Inversa de una 3x3 por cofactores; 0 si es singular. */
static int inversa3(const float m[3][3], float o[3][3])
{
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    float k;

    if (det > -1e-9f && det < 1e-9f)
        return 0;
    k = 1.0f / det;
    o[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * k;
    o[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k;
    o[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k;
    o[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * k;
    o[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k;
    o[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k;
    o[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * k;
    o[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k;
    o[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k;
    return 1;
}

int glow_depth(const struct RaceView *view, const struct Glow *g, const OverlayQuad *q,
               float *z)
{
    /* Todo en ejes de PS1, que es como vienen el ancla y la camara: aqui no se espeja X,
     * porque no se toca el mundo horneado. */
    float pw = view->rect[2] > 0 ? (float)view->rect[2] : 320.0f;
    float ph = view->rect[3] > 0 ? (float)view->rect[3] : 240.0f;
    float focal = view->projection != 0u ? (float)view->projection : 190.0f;
    float cam[3], d[3], r[3][3], adelante, cz, dist;
    uint32_t i, k, n = (uint32_t)g->n * 4u;

    for (i = 0; i < 3u; i++) {
        cam[i] = (float)view->camera_pos[i] / 65536.0f;
        d[i] = (float)g->anchor[i] / 65536.0f - cam[i];
        for (k = 0; k < 3u; k++)
            r[i][k] = (float)view->camera_rot[i * 3u + k];
    }
    /* como en view_init: "adelante" unitaria, y el aplastamiento de "arriba" se conserva */
    adelante = sqrtf(punto3(r[2], r[2]));
    if (adelante < 1e-6f)
        return 0;
    for (i = 0; i < 3u; i++)
        for (k = 0; k < 3u; k++)
            r[i][k] /= adelante;
    cz = punto3(r[2], d);
    dist = sqrtf(punto3(d, d));
    if (cz < 0.3f || dist < 1e-3f)
        return 0;

    if (!g->smoke) {
        float hacia, zf;

        if (g->car != 0u) {
            hacia = g->radius + 0.2f;
        } else {
            /* su tamano en el mundo, a partir del que tiene en pantalla */
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, medio;

            for (i = 0; i < g->n; i++) {
                if (q[i].kind != OVL_QUAD)
                    continue;
                for (k = 0; k < 4u; k++) {
                    float x = (float)q[i].pts[k][0], y = (float)q[i].pts[k][1];

                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
            }
            medio = (x1 >= x0) ? 0.5f * fmaxf(x1 - x0, y1 - y0) : 0.0f;
            hacia = 2.0f * (medio * dist / focal) + 1.0f;
            if (hacia > dist * 0.5f)
                hacia = dist * 0.5f;
        }
        if (hacia > dist * 0.9f)
            hacia = dist * 0.9f;
        zf = 1.0f / (cz * (1.0f - hacia / dist));
        for (i = 0; i < n; i++)
            z[i] = zf;
        return 1;
    }

    /* la bocanada */
    {
        float zc = cz * (1.0f - 0.5f / dist), m[3][3], plano;

        if (zc < 0.1f)
            zc = 0.1f;
        plano = (float)g->anchor[1] / 65536.0f + 0.05f;   /* y crece hacia arriba */
        if (!inversa3(r, m)) {
            for (i = 0; i < n; i++)
                z[i] = 1.0f / zc;
            return 1;
        }
        for (i = 0; i < g->n; i++) {
            for (k = 0; k < 4u; k++) {
                /* el rayo de esa esquina: en camara (a, b, 1), en el mundo m * (a, b, 1);
                 * como adelante * rayo = 1, la t del corte con el suelo es su profundidad */
                float a = ((float)q[i].pts[k][0] - pw * 0.5f) / focal;
                float b = -((float)q[i].pts[k][1] - ph * 0.5f) / focal;
                float ry = m[1][0] * a + m[1][1] * b + m[1][2];
                float zv = 1.0f / zc;

                if (ry < -1e-4f) {
                    float t = (plano - cam[1]) / ry;

                    if (t > 0.1f && 1.0f / t > zv)
                        zv = 1.0f / t;
                }
                z[i * 4u + k] = zv;
            }
        }
    }
    return 1;
}
