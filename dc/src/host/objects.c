/* Grabacion de los objetos del escenario (M2c, paso 1).
 *
 * El juego decide por frame **que objetos** dibuja de los bloques que se ven: los que
 * siguen en pie, los tumbados, los animados. No los construye en su RAM de forma que se
 * pueda leer luego: los va dibujando. Asi que se corre `DrawW_DoObjects` **anidada** con
 * un hook interno que intercepta sus dos rutinas de dibujo y, en vez de dejarlas
 * construir primitivas, **anota** el modelo, su matriz y su posicion.
 *
 * El hook tambien se come tres llamadas a proposito:
 *   - `ObjectClipped`, porque recorta con el cono 4:3 mas estrecho de PS1 y el port
 *     dibuja a 640x480;
 *   - las dos de destello (`FlareHalo`), que son efectos 2D y se graban por otro lado.
 *
 * Portado de src/ps1/host/objects.rs.
 */
#include "addr.h"
#include "host.h"

/* El hook interno es un puntero a funcion sin contexto, asi que el destino de la
 * grabacion tiene que ser global, igual que en showroom.c. */
static GameHost *s_host;

static uint32_t modelo_de(GameHost *h, uint32_t objdef)
{
    uint32_t i;

    for (i = 0; i < OBJ_MODELS_MAX; i++)
        if (h->obj_models[i] == objdef)
            return i;
    return 0xFFFFFFFFu;
}

static void lee3(Cpu *c, uint32_t p, int32_t *o)
{
    uint32_t i;

    for (i = 0; i < 3u; i++)
        o[i] = (int32_t)ps1_r32(c, p + 4u * i);
}

static int object_hook(Cpu *c, uint32_t addr)
{
    GameHost *h = s_host;
    ObjectDraw *d;
    uint32_t objdef, pos, modelo, i;

    if (h == NULL)
        return 0;
    switch (addr) {
    case ADDR_OBJECT_CLIPPED:
    case ADDR_FLARE_HALO:
    case ADDR_FLARE_HALO2:
        break;                       /* atendidas sin hacer nada */

    case ADDR_DRAW_OBJECT_TRANSFORM: {
        uint32_t matrix = c->r[6];

        objdef = c->r[7];
        pos = ps1_r32(c, c->r[29] + 16u);
        modelo = modelo_de(h, objdef);
        if (modelo != 0xFFFFFFFFu && h->objects_n < OBJECTS_MAX) {
            d = &h->objects[h->objects_n++];
            d->model = (uint16_t)modelo;
            /* el bit 0 del byte 5 del registro: el motor lo gira hacia la camara */
            d->billboard = (c->r[31] == ADDR_CHUNK_OBJECT_DRAW_RA &&
                            (ps1_r8(c, pos - 8u + 5u) & 1u) != 0u);
            for (i = 0; i < 9u; i++)
                d->matrix[i] = (int32_t)ps1_r32(c, matrix + 4u * i);
            lee3(c, pos, d->pos);
        } else if (modelo != 0xFFFFFFFFu) {
            h->objects_lost++;
        }
        break;
    }

    case ADDR_DRAW_OBJECT_SIMPLE:
        objdef = c->r[6];
        pos = c->r[7];
        modelo = modelo_de(h, objdef);
        if (modelo != 0xFFFFFFFFu && h->objects_n < OBJECTS_MAX) {
            static const int32_t ident[9] = { 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x10000 };

            d = &h->objects[h->objects_n++];
            d->model = (uint16_t)modelo;
            d->billboard = 0;
            for (i = 0; i < 9u; i++)
                d->matrix[i] = ident[i];
            lee3(c, pos, d->pos);
        } else if (modelo != 0xFFFFFFFFu) {
            h->objects_lost++;
        }
        break;

    default:
        return 0;
    }
    c->r[2] = 0;
    return 1;
}

void host_record_objects(GameHost *h, Cpu *c)
{
    uint32_t tabla = ps1_r32(c, ADDR_OBJECT_DEFS);
    uint32_t args[2], i;

    h->objects_n = 0;
    for (i = 0; i < OBJ_MODELS_MAX; i++)
        h->obj_models[i] = tabla != 0u ? ps1_r32(c, tabla + 4u * i) : 0u;

    s_host = h;
    c->inner_hook = object_hook;
    args[0] = ADDR_RENDER_VIEW;
    args[1] = ADDR_CHUNK_BUILD_LIST_D;
    (void)ps1_call_nested(c, ADDR_DRAWW_DO_OBJECTS, args, 2u);
    c->inner_hook = NULL;
    s_host = NULL;
    h->objects_sum += h->objects_n;
    if (h->objects_n > h->objects_max)
        h->objects_max = h->objects_n;
}
