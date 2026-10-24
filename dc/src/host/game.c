/* Funciones del propio juego que el host reemplaza: los videos y, en M2, el dibujo
 * del front end y los cambios de modo.
 *
 * Portado de src/ps1/host/game.rs, solo la parte que el arranque necesita.
 */
#include "addr.h"
#include "host.h"

/* Bandera que el juego pone para indicar que un video se salto. */
#define MOVIE_SKIPPED 0x80052EBCu

/* Render_RenderPlayerView(vista, jugador): LA PARTE DE LOGICA.
 *
 * El mundo lo dibuja el host (por eso esta en la lista HLE), pero el original hace
 * aqui dentro trabajo que NO es dibujar y del que depende la simulacion: prepara la
 * camara, decide que trozos de pista entran, y calcula el HUD (velocidad, marcha,
 * tiempos, posicion) y la deteccion de sentido contrario.
 *
 * M1a dejo esta funcion como un contador vacio dando por hecho que todo lo que hacia
 * era grabar geometria para el renderer. **No era cierto**: medido contra la
 * referencia, sin esto la carrera entra y se queda CONGELADA (0 paginas de RAM
 * cambian, 2 ticks por frame, frente a ~3 000 de la referencia). Ver docs/hitos/M1b.md.
 *
 * Lo que sigue sin portarse, a proposito, es la GRABACION: cielo y destello del sol,
 * destellos de farolas, humo de ruedas, clima, facetas y halos de los coches, y los
 * objetos del escenario. Eso es lo que M2b y M2c escriben con el PVR delante.
 */

/* Lee el cielo de la RAM del juego (M2b paso 4). Son unas 700 lecturas por frame, que al
 * lado de los ~15 000 ticks que cuesta un frame no se notan, y asi el cielo sigue los
 * cambios de luz sin tener que adivinar cuando cambian. */
static void sky_read(GameHost *h, Cpu *c)
{
    SkyData *s = &h->sky;
    uint32_t hrec = ps1_r32(c, ADDR_SKY_HREC);
    uint32_t ring = ps1_r32(c, ADDR_SKY_RING);
    uint32_t rec, i, k;

    s->ok = 0;
    if (hrec == 0u || ring == 0u)
        return;

    for (i = 0; i < 85u; i++) {
        for (k = 0; k < 3u; k++) {
            s->dome[i][k] = (int16_t)ps1_r16(c, ADDR_SKY_MESH + 8u * i + 2u * k);
            s->dome_rgb[i][k] = (uint8_t)ps1_r8(c, ADDR_SKY_COLOR + 4u * i + k);
        }
    }
    for (i = 0; i < 17u; i++) {
        s->ring[i][0] = (int16_t)ps1_r16(c, ring + 8u * i);
        s->ring[i][1] = (int16_t)ps1_r16(c, ring + 8u * i + 4u);
        for (k = 0; k < 3u; k++) {
            s->top_rgb[i][k] = (uint8_t)ps1_r8(c, ADDR_SKY_TOP_COLORS + 4u * i + k);
            s->bottom_rgb[i][k] = (uint8_t)ps1_r8(c, ADDR_SKY_BOTTOM_COLORS + 4u * i + k);
        }
    }
    s->bottom = (int16_t)ps1_r16(c, hrec + 8u);
    s->height = (int16_t)ps1_r16(c, hrec + 12u);
    for (i = 0; i < 16u; i++)
        s->segment_tex[i] = (uint8_t)ps1_r8(c, hrec + 32u + i);
    s->band = ps1_r16(c, ADDR_SKY_BAND) != 0u;

    /* La cupula lleva textura solo si el juego lo pide: la primera palabra de su registro
     * vale 1. Si no, va con color plano por vertice. */
    rec = ps1_r32(c, ADDR_SKY_DOMETEX);
    s->dome_tex_ok = (rec != 0u && ps1_r32(c, rec) == 1u);
    if (s->dome_tex_ok)
        for (i = 0; i < 64u; i++)
            s->dome_tex[i] = (uint8_t)ps1_r8(c, ADDR_SKY_DOMETEX_TAB + i);
    s->ok = 1;
}

static void race_view_logic(GameHost *h, Cpu *c, uint32_t view, uint32_t player)
{
    uint32_t args[2];
    uint32_t n;

    /* Donde el original espera encontrar la vista y el jugador. */
    ps1_w32(c, ADDR_RENDER_VIEW, view);
    ps1_w32(c, ADDR_RENDER_VIEW + 4u, player);

    /* Camara, niebla y colores del entorno: deja el GTE programado. */
    args[0] = view;
    (void)ps1_call_nested(c, ADDR_START_WORLD_VIEW, args, 1u);

    /* Preparacion de BWorld_OnyxBuildFacets: matrices y lista de trozos de pista que
     * se ven este frame. La cuenta la guarda el juego en gp + 608. */
    args[0] = ADDR_RENDER_VIEW;
    args[1] = ADDR_DCACHE;
    (void)ps1_call_nested(c, ADDR_SETUP_BUILD_MAT, args, 2u);
    args[0] = ADDR_G_WORLD_MAT;
    args[1] = ADDR_DCACHE + 0x14u;
    (void)ps1_call_nested(c, ADDR_WORLD_SETUP_MAT, args, 2u);
    args[0] = ADDR_RENDER_VIEW;
    n = ps1_call_nested(c, ADDR_CHUNK_BUILD_LIST, args, 1u);
    ps1_w32(c, ADDR_CHUNK_BUILD_COUNT, n);
    h->race_chunks = n;
    h->race_chunks_sum += n;
    h->race_views++;
    if (n > h->race_chunks_max)
        h->race_chunks_max = n;

    /* La tabla de color de vertice. El juego la mueve al cargar una pista, asi que se
     * relee solo cuando el puntero cambia: son 256 lecturas, no una por frame. */
    {
        uint32_t p = ps1_r32(c, ADDR_LIGHT_TABLE);

        if (p != h->light_ptr) {
            h->light_ptr = p;
            h->light_ok = 0;
            if (p != 0u) {
                uint32_t k;

                for (k = 0; k < 256u; k++)
                    h->light_table[k] = ps1_r32(c, p + 4u * k);
                h->light_ok = 1;
            }
        }
    }

    /* Los objetos del escenario: hay que grabarlos DESPUES de la lista de trozos,
     * porque es sobre ella sobre la que el juego decide. */
    host_record_objects(h, c);
    sky_read(h, c);

    /* La captura de la vista: camara, proyeccion, recorte y lista de trozos. A partir
     * de aqui el renderer no necesita volver a preguntarle nada al juego. */
    if (h->views_n < VIEWS_MAX) {
        RaceView *v = &h->view[h->views_n++];
        uint32_t i;

        v->player = player;
        for (i = 0; i < 3u; i++)
            v->camera_pos[i] = (int32_t)ps1_r32(c, ADDR_RENDER_VIEW + 8u + 4u * i);
        for (i = 0; i < 9u; i++)
            v->camera_rot[i] = (int32_t)ps1_r32(c, ADDR_RENDER_VIEW + 0x14u + 4u * i);
        v->projection = ps1_r32(c, ADDR_CAMERA_ZOOM);
        /* El recorte de esta vista va relativo al de la vista 0, que es como lo guarda
         * la referencia; el tamano es absoluto. */
        for (i = 0; i < 2u; i++)
            v->rect[i] = (int16_t)((int16_t)ps1_r16(c, ADDR_DRAW_VIEWS + 200u * view + 8u + 2u * i) -
                                   (int16_t)ps1_r16(c, ADDR_DRAW_VIEWS + 8u + 2u * i));
        v->rect[2] = (int16_t)ps1_r16(c, ADDR_DRAW_VIEWS + 200u * view + 12u);
        v->rect[3] = (int16_t)ps1_r16(c, ADDR_DRAW_VIEWS + 200u * view + 14u);
        v->chunks_n = (uint16_t)(n < VIEW_CHUNKS_MAX ? n : VIEW_CHUNKS_MAX);
        for (i = 0; i < v->chunks_n; i++) {
            v->chunk[i] = (int16_t)ps1_r16(c, ADDR_CHUNK_BUILD_LIST_D + 4u * i);
            v->detail[i] = (uint16_t)ps1_r16(c, ADDR_CHUNK_BUILD_LIST_D + 4u * i + 2u);
        }
    }

    /* El HUD: no es dibujo, son los numeros del juego (velocidad en unidades de
     * pantalla, marcha, tiempos, posicion) y el aviso de sentido contrario. */
    args[0] = player;
    (void)ps1_call_nested(c, ADDR_HUD_WRONG_WAY, args, 1u);
    (void)ps1_call_nested(c, ADDR_HUD_CALC, args, 1u);

    /* Los coches van DESPUES del HUD, en el mismo orden que la referencia: el original
     * los dibuja ahi y ese orden decide que ve cada rutina. */
    host_record_cars(h, c);
}

int host_game_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    uint32_t a0 = c->r[4];

    switch (addr) {
    case 0x8004D1C4u:
        /* Movie_Play(video): los videos los reproduce el host. En M1a no hay video
         * (los STR/MDEC son M5), asi que no se reproduce nada y se devuelve la
         * bandera de "saltado" tal como la deja el juego, que es exactamente lo que
         * devuelve la referencia. */
        h->movies_played++;
        h->last_movie = a0 & 0xFFu;
        *ret = (uint32_t)(int32_t)(int16_t)(uint16_t)ps1_r16(c, MOVIE_SKIPPED);
        return 1;
    /* ---- dibujo que el host se queda ----
     * Estas son HLE porque en la referencia las dibuja su motor. En M1a no se dibuja
     * nada, asi que solo se cuentan: la cuenta ya dice cuanto dibujo pide el front
     * end, que es dato util para dimensionar M2a. La grabacion de verdad es M2. */
    case 0x8004E74Cu: {
        /* El emisor de formas del front end (DrawShapeExtended y companeras). Los
         * argumentos quinto y sexto van en la pila: la direccion de los cuatro colores
         * de esquina y el modo de mezcla. */
        uint32_t sp = c->r[29];
        uint32_t colours_at = ps1_r32(c, sp + 16u);
        uint32_t abr = ps1_r32(c, sp + 20u);
        uint32_t colours[4];
        uint32_t k;

        for (k = 0; k < 4u; k++)
            colours[k] = ps1_r32(c, colours_at + 4u * k) & 0xFFFFFFu;
        h->fe_shapes++;
        overlay_shape_quads(h, c, a0, c->r[5], (int32_t)c->r[6], (int32_t)c->r[7],
                            colours, abr);
        *ret = 0;
        return 1;
    }
    case 0x8004F12Cu:                       /* PSXDrawSquare(color, x, y, w, h) */
    case 0x8004F3B8u: {                     /* PSXDrawTransSquare(..., veces) */
        /* El quinto argumento (la altura) va en la pila; el transparente se dibuja
         * varias veces encima de si mismo, que es como el original lo oscurece. */
        uint32_t sp = c->r[29];
        int32_t  hh = (int32_t)ps1_r32(c, sp + 16u);
        int semi = (addr == 0x8004F3B8u);
        int32_t veces = semi ? (int16_t)ps1_r16(c, sp + 20u) : 1;
        int32_t i;

        h->fe_squares++;
        for (i = 0; i < veces; i++)
            overlay_square(h, (int32_t)c->r[5], (int32_t)c->r[6], (int32_t)c->r[7],
                           hh, a0, semi);
        *ret = 0;
        return 1;
    }
    case 0x800B4024u:                       /* Render_RenderPlayerView: la vista 3D */
        h->player_views++;
        race_view_logic(h, c, a0, c->r[5]);
        *ret = 0;
        return 1;

    default:
        return 0;
    }
}

#if PS1_DIAG
/* ---- volcado de la vista de carrera (oraculo del paso 1 de M2b) ----
 *
 * Mismo formato que escribe la referencia con NFSHS_DIFF_VIEW, para que
 * dc/test/diff/cmpview.py los compare campo a campo. Little-endian.
 */
static void putv16(FILE *f, uint16_t v)
{
    fputc((int)(v & 0xFFu), f);
    fputc((int)((v >> 8) & 0xFFu), f);
}

static void putv32(FILE *f, uint32_t v)
{
    putv16(f, (uint16_t)v);
    putv16(f, (uint16_t)(v >> 16));
}

int view_dump_open(GameHost *h, const char *path)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL)
        return 0;
    fwrite("NFSV", 1, 4, f);
    putv32(f, 3u);   /* version 3: anade la captura de los coches */
    h->view_dump = f;
    return 1;
}

void view_dump_frame(GameHost *h)
{
    FILE *f = h->view_dump;
    uint32_t i, k;

    if (f == NULL)
        return;
    putv32(f, (uint32_t)h->frames);
    putv32(f, h->views_n);
    for (i = 0; i < h->views_n; i++) {
        const RaceView *v = &h->view[i];

        putv32(f, v->player);
        for (k = 0; k < 4u; k++)
            putv16(f, (uint16_t)v->rect[k]);
        for (k = 0; k < 3u; k++)
            putv32(f, (uint32_t)v->camera_pos[k]);
        for (k = 0; k < 9u; k++)
            putv32(f, (uint32_t)v->camera_rot[k]);
        putv32(f, v->projection);
        putv16(f, v->chunks_n);
        for (k = 0; k < v->chunks_n; k++) {
            putv16(f, (uint16_t)v->chunk[k]);
            putv16(f, v->detail[k]);
        }
    }
    /* Los objetos del escenario van detras de las vistas: el port los graba una vez por
     * frame, no por vista, que en un jugador es lo mismo. */
    putv32(f, h->objects_n);
    for (i = 0; i < h->objects_n; i++) {
        const ObjectDraw *o = &h->objects[i];

        putv16(f, o->model);
        fputc(o->billboard, f);
        fputc(0, f);
        for (k = 0; k < 9u; k++)
            putv32(f, (uint32_t)o->matrix[k]);
        for (k = 0; k < 3u; k++)
            putv32(f, (uint32_t)o->pos[k]);
    }
    /* La captura de los coches, en crudo: palabra a palabra como la graba la referencia
     * (99 por coche). Comparar el crudo es mas estricto que comparar campos. */
    putv32(f, h->cars_cap);
    for (i = 0; i < h->cars_cap; i++)
        putv32(f, h->cars_cap_buf[i]);
}

void view_dump_close(GameHost *h)
{
    if (h->view_dump != NULL) {
        fclose(h->view_dump);
        h->view_dump = NULL;
    }
}
#endif /* PS1_DIAG */
