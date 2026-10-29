/* Funciones del propio juego que el host reemplaza: los videos y, en M2, el dibujo
 * del front end y los cambios de modo.
 *
 * Portado de src/ps1/host/game.rs, solo la parte que el arranque necesita.
 */
#include "addr.h"
#include "host.h"

#include <math.h>

static int night_beam_read(GameHost *h, Cpu *c, uint32_t player);

/* Bandera que el juego pone para indicar que un video se salto. */
#define MOVIE_SKIPPED 0x80052EBCu
/* Si esta puesta, cualquier boton (no solo Start) marca el video como saltado. */
#define MOVIE_SKIP_FLAG 0x80052EBEu
/* Los nombres de las peliculas, uno por indice de Movie_Play (FRONT.BIN). */
#define MOVIE_NAMES 0x80052E68u

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

    /* Lo que se grabe de esta vista va a su hueco, el de la vista que va a ocupar. */
    h->vc = &h->cap[h->views_n < VIEWS_MAX ? h->views_n : VIEWS_MAX - 1u];

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
    /* y, en el mismo punto que la referencia, lo que el juego construye como 2D: farolas,
     * humo, clima y el flare del sol */
    host_record_glows(h, c, n);
    sky_read(h, c);

    /* La captura de la vista: camara, proyeccion, recorte y lista de trozos. A partir
     * de aqui el renderer no necesita volver a preguntarle nada al juego. */
    if (h->views_n < VIEWS_MAX) {
        RaceView *v = &h->view[h->views_n++];
        uint32_t i;

        v->beam = night_beam_read(h, c, player) ? &h->vc->beam : NULL;
        v->split = 0;
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
    host_record_halos(h, c);
    /* (las marcas de neumatico se leen en la frontera de frame: ver host_frame_top) */
}

/* El haz de los faros de noche de la vista de `player`, como NightBeam::read de la
 * referencia: el mapa y la tabla de Night (0x8013ED20 y 0x8013ED28), su forma
 * (Night_SetEnviroment) y los ejes del coche, como los usa su renderer (el coche con su
 * origen a ras de suelo, girado solo en horizontal). Devuelve si hay que aplicarlo. */
static int night_beam_read(GameHost *h, Cpu *c, uint32_t player)
{
    NightBeam *b = &h->vc->beam;
    uint32_t map = ps1_r32(c, 0x8013ED20u), table = ps1_r32(c, 0x8013ED28u), car, i;
    float row[3][3], pos[3], lift, n, fx, fz;

    b->on = 0;
    if (map == 0u || table == 0u || ps1_r32(c, 0x801144F8u) == 0u)
        return 0;
    car = ps1_r32(c, 0x80110CA0u + 4u * player);
    /* faros encendidos: Car_tObj+1095, bits 1 y 2 */
    if (car == 0u || (ps1_r8(c, car + 1095u) & 6u) == 0u)
        return 0;
    b->z0 = (int16_t)ps1_r16(c, 0x8013ED10u);
    b->xs = ps1_r8(c, 0x8013ED18u);
    b->zs = ps1_r8(c, 0x8013ED1Cu);
    for (i = 0; i < 4096u; i++) {
        b->map[i] = (uint8_t)ps1_r8(c, map + i);
        b->table[i] = (uint8_t)ps1_r8(c, table + i);
    }
    /* a metros y al espacio del mundo (la X espejada) */
    for (i = 0; i < 9u; i++)
        row[i / 3u][i % 3u] = (float)(int32_t)ps1_r32(c, car + 0xF0u + 4u * i) / 65536.0f *
                              (i % 3u == 0u ? -1.0f : 1.0f);
    for (i = 0; i < 3u; i++)
        pos[i] = (float)(int32_t)ps1_r32(c, car + 0xA0u + 4u * i) / 65536.0f * (i == 0u ? -1.0f : 1.0f);
    lift = (float)(int32_t)ps1_r32(c, car + 0x134u + 4u) / 65536.0f;
    n = sqrtf(row[1][0] * row[1][0] + row[1][1] * row[1][1] + row[1][2] * row[1][2]);
    for (i = 0; i < 3u; i++)
        b->origin[i] = pos[i] - (n > 1e-6f ? row[1][i] / n : (i == 1u ? 1.0f : 0.0f)) * lift;
    /* adelante y derecha en horizontal (Car::forward y Car::right de la referencia) */
    fx = row[2][0];
    fz = row[2][2];
    n = sqrtf(fx * fx + fz * fz);
    if (n < 1e-6f) {
        fx = 0.0f;
        fz = 1.0f;
    } else {
        fx /= n;
        fz /= n;
    }
    b->forward[0] = fx; b->forward[1] = 0.0f; b->forward[2] = fz;
    b->right[0] = -fz;  b->right[1] = 0.0f;   b->right[2] = fx;
    b->on = 1;
    return 1;
}

int host_game_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    uint32_t a0 = c->r[4];

    switch (addr) {
    case 0x8004D1C4u:
        /* Movie_Play(video): la pelicula la reproduce la plataforma (M3). Como el bucle
         * del original (play_movie en la referencia): cualquier boton la corta, y Start
         * solo (o la bandera 0x80052EBE del juego) la marca como saltada. Devuelve esa
         * marca. Sin plataforma de video no se reproduce nada, igual que la referencia
         * en el test diferencial. */
        h->movies_played++;
        h->last_movie = a0 & 0xFFu;
        if (h->play_movie != NULL) {
            uint32_t at = ps1_r32(c, MOVIE_NAMES + 4u * (a0 & 0xFFu));
            char name[16];
            unsigned k = 0;

            while (at != 0u && k + 1u < sizeof name && ps1_r8(c, at + k) != 0u) {
                name[k] = (char)ps1_r8(c, at + k);
                k++;
            }
            name[k] = '\0';
            if (k > 0u) {
                uint16_t b = h->play_movie(h, name);

                if (b != 0u && (b == (1u << 3) || ps1_r8(c, MOVIE_SKIP_FLAG) != 0u))
                    ps1_w16(c, MOVIE_SKIPPED, 1u);
            }
        }
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
    putv32(f, 6u);   /* version 6: objetos, destellos, sol y clima van en cada vista (M5) */
    h->view_dump = f;
    return 1;
}

void view_dump_frame(GameHost *h)
{
    FILE *f = h->view_dump;
    uint32_t i, k, j;

    if (f == NULL)
        return;
    putv32(f, (uint32_t)h->frames);
    putv32(f, h->views_n);
    for (i = 0; i < h->views_n; i++) {
        const RaceView *v = &h->view[i];
        const ViewCap *vc = &h->cap[i];

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
        /* lo que el juego dibuja en esta vista: los objetos del escenario, ... */
        putv32(f, vc->objects_n);
        for (j = 0; j < vc->objects_n; j++) {
            const ObjectDraw *o = &vc->objects[j];

            putv16(f, o->model);
            fputc(o->billboard, f);
            fputc(0, f);
            for (k = 0; k < 9u; k++)
                putv32(f, (uint32_t)o->matrix[k]);
            for (k = 0; k < 3u; k++)
                putv32(f, (uint32_t)o->pos[k]);
        }
        /* ... los destellos (ancla, coche, si es humo y sus cuadros), y el flare del sol y
         * el clima, cuadros sueltos */
        putv32(f, vc->glows_n);
        for (j = 0; j < vc->glows_n; j++) {
            const Glow *g = &vc->glows[j];

            for (k = 0; k < 3u; k++)
                putv32(f, (uint32_t)g->anchor[k]);
            putv32(f, g->car);
            fputc(g->smoke, f);
            fputc(0, f);
            putv16(f, g->n);
            for (k = 0; k < g->n; k++)
                overlay_dump_quad(f, &vc->glow_quad[g->first + k]);
        }
        putv32(f, vc->sky_quads_n);
        for (j = 0; j < vc->sky_quads_n; j++)
            overlay_dump_quad(f, &vc->sky_quad[j]);
        putv32(f, vc->weather_quads_n);
        for (j = 0; j < vc->weather_quads_n; j++)
            overlay_dump_quad(f, &vc->weather_quad[j]);
    }
    /* La captura de los coches de la ultima vista, en crudo: palabra a palabra como la
     * graba la referencia (99 por coche). Comparar el crudo es mas estricto que comparar
     * campos. */
    putv32(f, h->cars_cap);
    for (i = 0; i < h->cars_cap; i++)
        putv32(f, h->cars_cap_buf[i]);
    /* Las marcas de neumatico, del frame: por cuadro, 12 enteros de esquinas, 2 colores,
     * la textura y la seccion. */
    putv32(f, h->skids_n);
    for (i = 0; i < h->skids_n; i++) {
        const SkidQuad *q = &h->skids[i];

        for (k = 0; k < 12u; k++)
            putv32(f, (uint32_t)q->corners[k / 3u][k % 3u]);
        putv32(f, q->colour[0]);
        putv32(f, q->colour[1]);
        putv16(f, q->tex);
        putv16(f, (uint16_t)q->slice);
    }
}

void view_dump_close(GameHost *h)
{
    if (h->view_dump != NULL) {
        fclose(h->view_dump);
        h->view_dump = NULL;
    }
}
#endif /* PS1_DIAG */
