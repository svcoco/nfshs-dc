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
