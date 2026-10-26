/* Las vistas: que partes del dibujado las hace el host y cuales las deja correr.
 *
 * Por que existe este modulo. El juego dibuja el texto con Font_TextXY y el HUD con
 * cuatro ayudantes mas. En la referencia (src/ps1/host/views.rs) esas cinco funciones se
 * tratan **segun el modo**:
 *
 *   - en carrera y sin pausa: el original corre y el host graba sus primitivas;
 *   - en el front end, y en una carrera en pausa: **el host las atiende y el original NO
 *     corre**.
 *
 * El port las tenia como hooks puros (el host observa, el original siempre corre), y eso
 * fue la segunda divergencia que encontro el test diferencial: en la llamada 15 940 el
 * port hacia cuatro llamadas a 0x800CC00C que la referencia no hace, porque alli
 * Font_TextXY ya estaba atendida. Los ticks coincidian exactamente en las dos partes
 * (8 651 115), asi que no era temporizacion: era el modo.
 *
 * Para M1a lo que importa es **si el original corre y que devuelve**, no lo que se
 * dibuja; la grabacion de las primitivas es M2a. */
#include "addr.h"
#include "host.h"

#include <stdio.h>
#include <string.h>

/* Front_Menu y Front_BuildStream pasan a front end; Sim_MainGameLoop pasa a carrera.
 * Los tres son hooks: cambian el modo y el original corre igual. */
#define FRONT_MENU         0x80027B38u
#define FRONT_BUILDSTREAM  0x8002A7B0u
#define SIM_MAINGAMELOOP   0x800B7A20u

#define FONT_TEXTXY        0x800CC7F0u
#define HUD_BUILDSTRING    0x800D4E9Cu
#define HUD_HELPER_8690    0x800D8690u
#define HUD_HELPER_9ABC    0x800D9ABCu
#define HUD_HELPER_3558    0x800D3558u

/* Las diez rutinas del front end que **construyen sus propias primitivas de GPU**. La
 * referencia (CAPTURED_FE_DRAWING en src/ps1/host/mod.rs) no las deja correr en su sitio:
 * las ejecuta en un **marco anidado** para poder leer el buffer de primitivas antes y
 * despues y quedarse con los paquetes.
 *
 * Esa mecanica no es cosmetica, y fue la tercera divergencia que encontro el test
 * diferencial. `capture_prims` hace tres cosas con efecto medible:
 *   1. copia 16 palabras de argumentos de pila de sp+16 a (sp-0x100)&~7 + 16  -> escribe RAM;
 *   2. corre el original con ps1_call_nested, que mueve sp y es **una llamada mas** (y por
 *      tanto un tick mas);
 *   3. marca `capturing` para no capturar de forma anidada (DrawTV llama a DrawTVLines).
 *
 * El port las dejaba correr en su sitio, lo que es mas fiel al hardware real pero no a la
 * referencia: de ahi los 21 ticks y 21 llamadas de desfase por frame, los 60 bytes de pila
 * distintos, y que el port ejecutara DrawTVLines 32 veces y la referencia ninguna (al
 * correr con otro sp, la rama de 0x800226a4 se decide al contrario).
 *
 * Grabar los paquetes (record_packets) es M2a; aqui solo importan las mecanicas. */
static const uint32_t CAPTURED_FE_DRAWING[10] = {
    0x800224ACu,   /* DrawTV */
    0x8004EC64u,   /* POLY_GT4 de una forma escalada (ScaleShapeExtended) */
    0x800CECD0u,   /* Flare_2DHalo */
    0x80024C4Cu,   /* DrawSlider */
    0x80022158u,   /* DrawTVLines */
    0x8001C25Cu,   /* SubtractiveBox */
    0x800379FCu,   /* tScreenMain::DrawDropShadow */
    0x8004C2E0u,   /* DrawShape_SubtractNFS4RectEdges */
    0x8004F1C4u,   /* PSXDrawGouraudSquare */
    0x8004F2B4u,   /* PSXDrawTransGouraudSquare */
};

static int is_captured(uint32_t addr)
{
    uint32_t i;
    for (i = 0; i < 10u; i++)
        if (CAPTURED_FE_DRAWING[i] == addr)
            return 1;
    return 0;
}

/* Corre la rutina original en un marco anidado, como capture_prims de la referencia. */
uint32_t host_capture_prims(GameHost *h, Cpu *c, uint32_t addr)
{
    uint32_t args[4];
    uint32_t sp = c->r[29];
    uint32_t nested_sp = (sp - 0x100u) & ~7u;
    uint32_t i, ret;

    args[0] = c->r[4]; args[1] = c->r[5]; args[2] = c->r[6]; args[3] = c->r[7];
    /* los argumentos a partir del cuarto van en la pila del llamador: hay que copiarlos
     * a donde los espera el marco de la llamada anidada */
    for (i = 0; i < 16u; i++)
        ps1_w32(c, nested_sp + 16u + 4u * i, ps1_r32(c, sp + 16u + 4u * i));

    /* El buffer de primitivas crece desde el puntero del scratchpad: lo que la rutina
     * anada entre estas dos lecturas es lo que dibujo. */
    {
        uint32_t ini = ps1_r32(c, 0x1F800004u);
        uint32_t fin;

        h->capturing = 1;
        ret = ps1_call_nested(c, addr, args, 4u);
        h->capturing = 0;
        fin = ps1_r32(c, 0x1F800004u);
        overlay_record_packets(h, c, ini, fin);
    }
    h->views_captured++;
    return ret;
}

/* Las funciones del HUD que la referencia dibuja con su propio port (NATIVE_HUD de
 * src/ps1/host/mod.rs: Hud_RenderMapView, el uso de Hud_BlackThinBox en la cuenta atras,
 * Hud_RenderHudView y Hud_RenderTacView): lo 2D que llaman desde ahi no se graba. */
static int es_hud_nativo(uint32_t ra)
{
    static const uint32_t R[4][2] = {
        { 0x800D840Cu, 0x800D8690u }, { 0x800D887Cu, 0x800D8B84u },
        { 0x800D8EA4u, 0x800D981Cu }, { 0x800D981Cu, 0x800D9938u }
    };
    uint32_t i;

    for (i = 0; i < 4u; i++)
        if (ra >= R[i][0] && ra < R[i][1])
            return 1;
    return 0;
}

/* La carrera esta en pausa: su menu lo dibuja el host. */
static int paused(Cpu *c)
{
    return ps1_r32(c, SIM_PAUSED) != 0u;
}

int host_views_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    switch (addr) {
    case FRONT_MENU:
    case FRONT_BUILDSTREAM:
        h->mode = HOST_FRONTEND;
        return 0;                        /* el original corre */
    case SIM_MAINGAMELOOP:
        h->mode = HOST_RACE;
        return 0;
    default:
        break;
    }

    /* tScreenTrackSelect::DrawVideoWall llamada desde DrawBackground (ra 0x80041E5C), que
     * acaba de construir en el buffer de primitivas los dos cuadros del video de la pista que
     * gira (POLY_FT4, con los fotogramas volcados en la VRAM en x 512): se graban y la rutina
     * corre como siempre. Como la referencia (mod.rs). Sin esto faltaban esos dos cuadros en
     * la pantalla de pista, la "deuda de dos cuadros" de los menus desde M2a. */
    if (h->mode == HOST_FRONTEND && !h->capturing && addr == 0x800422DCu &&
        c->r[31] == 0x80041E5Cu) {
        uint32_t fin = ps1_r32(c, 0x1F800004u);

        if ((ps1_r32(c, fin - 80u) >> 24) == 9u && (ps1_r8(c, fin - 80u + 7u) & 0xFCu) == 0x2Cu &&
            (ps1_r32(c, fin - 40u) >> 24) == 9u && (ps1_r8(c, fin - 40u + 7u) & 0xFCu) == 0x2Cu)
            overlay_record_packets(h, c, fin - 80u, fin);
        return 0;
    }

    /* Las rutinas que construyen primitivas: en el front end se corren anidadas. El
     * guardia de `capturing` es imprescindible, porque DrawTV llama a DrawTVLines y la
     * anidada de dentro tiene que correr en su sitio. */
    if (h->mode == HOST_FRONTEND && !h->capturing && is_captured(addr)) {
        *ret = host_capture_prims(h, c, addr);
        return 1;
    }

    switch (addr) {
    case FONT_TEXTXY:
    case HUD_BUILDSTRING:
    case HUD_HELPER_8690:
    case HUD_HELPER_9ABC:
    case HUD_HELPER_3558:
        break;
    default:
        return 0;                        /* no es de este modulo */
    }

    /* En carrera y sin pausa el original construye sus primitivas: no se atiende. */
    if (h->mode == HOST_RACE && !paused(c))
        return 0;

    if (addr == FONT_TEXTXY) {
        /* Las dos ramas de la referencia (front end y carrera en pausa) devuelven 0.
         * Lo que dibujan (font_quads o el comando de texto del overlay) es M2a: aqui
         * solo importa que el original no corra y que v0 sea 0. */
        h->views_fonttext++;
        /* En el front end el texto son cuadros (uno por caracter); en una carrera en
         * pausa la referencia produce un comando de texto (ver OVL_HUD_CMD). */
        if (h->mode == HOST_FRONTEND) {
            overlay_font_quads(h, c, c->r[4], (int32_t)c->r[5], (int32_t)c->r[6]);
        } else {
            HudCmd m;
            int32_t pal;
            uint32_t i;

            memset(&m, 0, sizeof m);
            m.kind = HUD_TEXT;
            m.x = (int16_t)c->r[5];
            m.y = (int16_t)c->r[6];
            /* la paleta: cual de las 16 de la fuente es la vigente */
            pal = (int32_t)(ps1_r16(c, ADDR_FONT_CURRENT_CLUT) & 0x3Fu) -
                  (int32_t)(ps1_r16(c, ADDR_FONT_CLUT_X) >> 4);
            m.pal = (uint8_t)(pal < 0 ? 0 : pal > 15 ? 15 : pal);
            m.space = (uint8_t)ps1_r8(c, ADDR_FONT_SPACE);
            for (i = 0; i < 256u; i++) {
                uint8_t ch = (uint8_t)ps1_r8(c, c->r[4] + i);

                if (ch == 0u)
                    break;
                if (i < HUD_TEXT_MAX)
                    m.text[i] = (char)ch;
            }
            m.len = (uint8_t)(i < HUD_TEXT_MAX ? i : HUD_TEXT_MAX);
            overlay_hud_cmd(h, c, &m, c->r[4]);
        }
        if (h->textlog) {
            char buf[80];
            uint32_t i;
            for (i = 0; i + 1u < sizeof buf; i++) {
                uint32_t ch = ps1_r8(c, c->r[4] + i);
                if (ch == 0u)
                    break;
                buf[i] = (ch >= 32u && ch < 127u) ? (char)ch : '.';
            }
            buf[i] = '\0';
            if (i != 0u)
                fprintf(stderr, "TEXTO f%llu (%d,%d): %s\n",
                        (unsigned long long)h->frames, (int)(int16_t)c->r[5],
                        (int)(int16_t)c->r[6], buf);
        }
        *ret = 0;
        return 1;
    }

    /* Los cuatro ayudantes del HUD, como views.rs de la referencia: el original no corre,
     * se graba el comando y se devuelve lo que devolveria (el ancho en Hud_BuildString, 0
     * en el resto). Los que llama el HUD que la referencia dibuja con su propio port
     * (native_hud) no se graban. Se ejercitan en la pausa (guion pausa.txt). */
    {
        HudCmd m;
        int nativo = es_hud_nativo(c->r[31]);

        memset(&m, 0, sizeof m);
        switch (addr) {
        case HUD_BUILDSTRING: {
            /* Hud_BuildString(texto, x, y, color, relleno, solo medir): las letras y
             * cifras pequenas del HUD y los simbolos de los botones; devuelve el ancho */
            uint32_t sp = c->r[29];
            int medir = ps1_r32(c, sp + 20u) != 0u;
            int graba = !medir && !nativo;
            int32_t x0 = (int32_t)c->r[5], x = x0, y = (int32_t)c->r[6];
            uint32_t i;

            for (i = 0; i < 64u; i++) {
                uint32_t ch = ps1_r8(c, c->r[4] + i), forma;
                int32_t dy = 0;

                if (ch == 0u)
                    break;
                if (ch == 0x20u) {
                    x += 3;
                    continue;
                }
                if (ch == 0x2Au) {          /* '*': el boton X, dos pixeles dentro */
                    x += 2;
                    forma = 170u;
                } else if (ch == 0x5Eu) {
                    forma = 170u;
                } else if (ch == 0x28u) {
                    forma = 169u;
                } else if (ch == 0x29u) {
                    forma = 168u;
                } else if (ch == 0x26u) {
                    forma = 174u;
                } else if (ch >= 0x30u && ch <= 0x39u) {
                    forma = ch + 110u;
                } else if (ch == 0x2Du) {
                    forma = 72u;
                } else if (ch == 0x3Au) {
                    forma = 73u;
                } else if (ch >= 0xC0u && ch <= 0xDCu) {
                    forma = ch - 118u;
                    dy = -1;
                } else if (ch == 0xE5u) {
                    forma = 103u;
                    dy = -1;
                } else {
                    forma = ch + 67u;
                }
                forma &= 0xFFu;
                if (graba) {
                    m.kind = HUD_SHAPE;
                    m.shape = (uint8_t)forma;
                    m.x = (int16_t)x;
                    m.y = (int16_t)(y + dy);
                    m.colour = c->r[7] & 0xFFFFFFu;
                    overlay_hud_cmd(h, c, &m, 0u);
                }
                x += (int32_t)(int16_t)ps1_r16(c, ADDR_HUD_SHAPES + 20u * forma + 16u) +
                     (ch == 0x2Au ? 3 : 1);
            }
            *ret = (uint32_t)(x - x0);
            return 1;
        }
        case HUD_HELPER_8690:               /* Hud_BlackThinBox(x, y, w, h) */
            if (!nativo) {
                m.kind = HUD_THINBOX;
                m.x = (int16_t)c->r[4];
                m.y = (int16_t)c->r[5];
                m.w = (int16_t)c->r[6];
                m.h = (int16_t)c->r[7];
                overlay_hud_cmd(h, c, &m, 0u);
            }
            *ret = 0;
            return 1;
        case HUD_HELPER_9ABC:               /* Hud_RenderPauseBox(x, y, w, h) */
            m.kind = HUD_PAUSEBOX;
            m.x = (int16_t)c->r[4];
            m.y = (int16_t)c->r[5];
            m.w = (int16_t)c->r[6];
            m.h = (int16_t)c->r[7];
            overlay_hud_cmd(h, c, &m, 0u);
            *ret = 0;
            return 1;
        default: {                          /* Hud_FBuildF4(semi, x, y, w, h, color, dx0, dx1) */
            uint32_t sp = c->r[29];

            if (!nativo) {
                m.kind = HUD_RECT;
                m.semi = (uint8_t)(c->r[4] != 0u);
                m.x = (int16_t)c->r[5];
                m.y = (int16_t)c->r[6];
                m.w = (int16_t)c->r[7];
                m.h = (int16_t)ps1_r32(c, sp + 16u);
                m.colour = ps1_r32(c, sp + 20u) & 0xFFFFFFu;
                m.dx0 = (int8_t)ps1_r32(c, sp + 24u);
                m.dx1 = (int8_t)ps1_r32(c, sp + 28u);
                overlay_hud_cmd(h, c, &m, 0u);
            }
            *ret = 0;
            return 1;
        }
        }
    }
}
