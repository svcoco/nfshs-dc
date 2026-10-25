/* Las marcas de neumatico (M2c, paso 6).
 *
 * El juego las lleva en su RAM como segmentos (Skidmark, en gp+5484, con la cuenta en
 * gp+5488): cada segmento son 688 bytes con un origen en 16.16 y hasta 23 entradas de dos
 * puntos de borde, relativos a ese origen y en 1/1024 m, mas el color, unos indicadores y la
 * entrada anterior a la que se une el cuadro. Aqui solo se LEEN: dibujarlas es del renderer.
 *
 * Portado de read_skids (src/ps1/race.rs).
 */
#include "addr.h"
#include "host.h"

#define ADDR_SKID_BASE  (ADDR_GP + 5484u)
#define ADDR_SKID_COUNT (ADDR_GP + 5488u)

static int32_t sk_r16(Cpu *c, uint32_t a)
{
    return (int16_t)ps1_r16(c, a);
}

/* El punto de borde de una entrada, relativo al origen del segmento. */
static void sk_pt(Cpu *c, const int32_t origin[3], uint32_t a, int32_t out[3])
{
    uint32_t i;

    for (i = 0; i < 3u; i++)
        out[i] = origin[i] + (sk_r16(c, a + 2u * i) << 6);
}

void host_read_skids(GameHost *h, Cpu *c)
{
    uint32_t n = ps1_r32(c, ADDR_SKID_COUNT);
    uint32_t base = ps1_r32(c, ADDR_SKID_BASE);
    uint32_t s, e;

    h->skids_n = 0;
    if (base == 0u)
        return;
    if (n > 64u)
        n = 64u;
    for (s = 0; s < n; s++) {
        uint32_t seg = base + 688u * s;
        int32_t origin[3];
        int32_t count = sk_r16(c, seg + 12u);
        int16_t slice = (int16_t)sk_r16(c, seg + 14u);

        origin[0] = (int32_t)ps1_r32(c, seg);
        origin[1] = (int32_t)ps1_r32(c, seg + 4u);
        origin[2] = (int32_t)ps1_r32(c, seg + 8u);
        if (count < 0)
            count = 0;
        if (count > 23)
            count = 23;
        for (e = 0; e < (uint32_t)count; e++) {
            uint32_t ent = seg + 16u + 28u * e;
            uint32_t prev = ps1_r32(c, ent + 24u);
            uint32_t flags, c0, c1;
            SkidQuad *q;

            if (prev == 0u)
                continue;
            flags = ps1_r32(c, ent + 20u);
            if (flags != 0u) {
                c0 = 0x404040u;
                c1 = 0x404040u;
            } else {
                c0 = ps1_r32(c, ent + 16u) & 0xFFFFFFu;
                c1 = ps1_r32(c, prev + 16u) & 0xFFFFFFu;
            }
            if (c0 == 0u && c1 == 0u)
                continue;
            if (h->skids_n >= SKIDS_MAX) {
                h->skids_lost++;
                continue;
            }
            q = &h->skids[h->skids_n++];
            sk_pt(c, origin, ent, q->corners[0]);
            sk_pt(c, origin, ent + 8u, q->corners[1]);
            sk_pt(c, origin, prev, q->corners[2]);
            sk_pt(c, origin, prev + 8u, q->corners[3]);
            q->colour[0] = c0;
            q->colour[1] = c1;
            q->tex = (uint8_t)(flags & 1u);
            q->slice = slice;
        }
    }
    if (h->skids_n > h->skids_max)
        h->skids_max = h->skids_n;
}
