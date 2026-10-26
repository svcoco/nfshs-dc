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
#include "host.h"

#include <stdio.h>

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
         * pausa la referencia produce un comando de texto, que es del paso 5. */
        if (h->mode == HOST_FRONTEND)
            overlay_font_quads(h, c, c->r[4], (int32_t)c->r[5], (int32_t)c->r[6]);
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

    /* Los cuatro ayudantes del HUD devuelven anchos calculados (Hud_BuildString mide la
     * cadena con HUD_SHAPES), y portar ese calculo sin un escenario que lo ejercite seria
     * escribir a ciegas. En el escenario de M1a **no se llaman ni una vez**, asi que se
     * dejan correr y se cuentan: si un guion de carrera los toca, el informe lo grita en
     * vez de divergir en silencio. */
    h->views_hud_unported++;
    if (h->views_hud_unported == 1u)
        fprintf(stderr, "\n--- AVISO: ayudante del HUD sin portar (%#x) ---\n"
                        "  la referencia lo atiende en este modo y devuelve un ancho;\n"
                        "  el port deja correr el original, asi que la RAM va a divergir.\n",
                (unsigned)addr);
    return 0;
}
