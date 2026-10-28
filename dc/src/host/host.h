/* Host del port: los reemplazos de alto nivel (HLE) de las funciones de NFS4.EXE que
 * manejan el hardware de la PS1.
 *
 * La lista de direcciones es la misma que usa el recompilador, tools/hle_funcs.txt.
 * En PC se lee en ejecucion, para que no pueda quedar desincronizada con el C
 * generado. En Dreamcast se usa la tabla horneada (ver HostHleBaked abajo), que sale
 * del mismo archivo en tiempo de compilacion y no puede desincronizarse tampoco.
 *
 * Estado: andamio de medicion. Todavia no hay ninguna funcion implementada; lo que
 * hace es registrar que pide el juego, en que orden y cuantas veces, porque eso es lo
 * que decide por donde seguir en vez de implementar 119 funciones a ciegas.
 */
#ifndef PS1_HOST_H
#define PS1_HOST_H

#include "ps1rt.h"
#include "files.h"
#include "overlay.h"
#include "texcache.h"
#include "track.h"

#include <stdio.h>

/* Diagnostico y soporte del test diferencial: volcado de estado por frame, guiones de
 * entrada y tabla de llamadas. Todo eso ESCRIBE archivos, y en Dreamcast no hay donde
 * (el GD-ROM es de solo lectura) ni a quien le sirva. Se compila fuera con -DPS1_DIAG=0
 * y asi no se arrastra ni el codigo ni los `FILE *`.
 *
 * No es portabilidad: `fopen` funciona en KOS, sobre su VFS (/cd, /rd, /pc, /vmu).
 * Es no llevar peso muerto. */
#ifndef PS1_DIAG
#define PS1_DIAG 1
#endif

#define HOST_HLE_MAX 256u
/* pasos del guion de entrada: el de una carrera entera con el piloto automatico de la
 * referencia (NFSHS_DIFF_AUTOPILOT) tiene ~820 por vuelta */
#define PAD_SCRIPT_MAX 4096u
/* Tabla de TODAS las llamadas que ve el host, por direccion (HLE y no HLE), para
 * poder diferenciarla contra la de la referencia. */
#define HOST_SEEN_MAX 4096u

/* Reloj virtual del host, en unidades de 1/1920 de segundo (los mismos valores que
 * TICK y FIELD en src/ps1/host/mod.rs de la referencia): 1920/15 = 128 Hz para el
 * root counter del juego, y 1920/32 = 60 Hz para los campos de video. */
#define HOST_TICK   15u
#define HOST_FIELD  32u
/* Con reloj real, campos que puede avanzar el juego como mucho en un frame (4: hasta 15 fps
 * el juego va a su velocidad; ver host_frame_top). */
#define HOST_REAL_MAX_FIELDS 4u
#define HOST_HZ     1920u

/* Espejo de la VRAM del PS1: 1024 x 512 pixeles de 16 bits (1 MB). El juego escribe
 * texturas y paletas y luego las vuelve a leer con StoreImage, asi que hay que
 * guardarlas aunque no se dibuje nada. */
#define VRAM_W 1024u
#define VRAM_H  512u

/* Que parte del original esta corriendo. Decide si las funciones de dibujado de texto
 * y HUD las atiende el host o las deja correr (ver views.c). */
typedef enum { HOST_FRONTEND = 0, HOST_RACE } HostMode;

/* Sim_Paused de la referencia (src/ps1/race.rs, addr::SIM_PAUSED). */
#define SIM_PAUSED 0x8013E71Cu

typedef struct {
    uint32_t addr;
    char     name[48];
    uint64_t calls;
    uint32_t first;          /* orden de primera aparicion, 1..n (0 = nunca) */
    int      implemented;    /* 1 si algun modulo del host la atendio */
} HleFunc;

/* Una vista de carrera tal y como el juego la deja preparada (M2b, paso 1).
 *
 * Es **todo** lo que el renderer necesita del juego, porque el juego no construye la
 * malla del mundo en su RAM: solo dice donde esta la camara y que bloques de pista se
 * ven, con que nivel de detalle. La malla vive en el GRP y se hornea en el PC
 * (docs/hitos/M2b.md). */
#define VIEWS_MAX        2u    /* un jugador; dos en M5 */
#define VIEW_CHUNKS_MAX 64u    /* lo que cabe en la lista del juego */

/* El cielo, tal como el juego lo deja en su RAM (M2b paso 4).
 *
 * Son dos piezas: una **cupula** de 5 anillos x 17 puntos con color por vertice (y, en
 * algunas pistas, textura por cuadro), y una **banda de horizonte** de 16 segmentos
 * alrededor de un anillo de 17 puntos. Las dos se dibujan centradas en la camara, asi que
 * solo les afecta su giro. */
typedef struct SkyData {
    int      ok;                /* el juego tiene cielo preparado */
    int16_t  dome[85][3];       /* gSkyMesh, radio 1000 */
    uint8_t  dome_rgb[85][3];
    int16_t  ring[17][2];       /* radio 32000 */
    int16_t  bottom, height;    /* alto de la banda, en unidades del anillo */
    uint8_t  top_rgb[17][3];
    uint8_t  bottom_rgb[17][3];
    uint8_t  segment_tex[16];   /* HZ01..HZ08 por segmento (indice & 7) */
    int      band;              /* la banda se dibuja (en los circuitos, no) */
    int      dome_tex_ok;
    uint8_t  dome_tex[64];      /* pixmap por cuadro: 0..11 HZ01..HZ12, 12..15 espejados */
} SkyData;

/* Un objeto del escenario que el juego decidio dibujar este frame (M2c paso 1). */
#define OBJECTS_MAX     256u   /* medidos ~47 por frame */
#define OBJ_MODELS_MAX 1024u   /* el tamano de la tabla del juego */

/* Lo que hace falta para DIBUJAR un coche, aparte de la captura que compara el oraculo:
 * su modelo, su pose y que partes se ven. Va separado a proposito, porque la captura
 * tiene que seguir siendo palabra a palabra la de la referencia. */
#define CARS_MAX 8u
#define CAR_ENV_PARTS   7u     /* las partes con reflejo */
#define CAR_ENV_NORMALS 1024u  /* medido: 294 en el coche del jugador */

typedef struct CarDraw {
    char     code[8];          /* las 4 letras del modelo, de Car_tObj + 0x240 */
    int32_t  pos[3];           /* 16.16 */
    int32_t  orient[9];        /* filas: derecha, arriba, adelante; 1,0 = 0x10000 */
    int32_t  dimension[3];     /* medias extensiones de la caja; la Y levanta el modelo */
    uint32_t light;            /* la luz de la pista en el coche, 0xBBGGRR */
    uint16_t wheel_rot[2];     /* giro de las ruedas delanteras y traseras */
    int32_t  wheel_steer;
    uint8_t  parts[57];        /* R3DCar_ObjectVisible: 1..16 se dibuja, >=17 es halo */
    /* La sombra en el suelo (DrawC_ShadowPrim): las esquinas que calculo
     * Newton_CalcRealShadowCoordinates, relativas a la posicion del coche en x y z (la y
     * es la altura de mundo), y su color. Solo se dibuja con `shadow_on`, con el coche
     * visible (`env_ok`) y sin aparcar, que es lo que exige la referencia. */
    uint8_t  shadow_on, env_ok, parked;
    uint8_t  mirror;           /* showroom: el reflejo en el suelo (y espejada); 0 en carrera */
    uint8_t  paint;            /* la pintura, como CarState::colour: color 0..15 y dos banderas */
    int32_t  shadow[4][3];
    uint8_t  shadow_rgb[3];
    /* Los reflejos (DrawC_PrimStart, en el cache del coche): dos de los mapas de reflejo
     * de la pista, como pixmaps (u, v, clut, u, v, tpage...; 0 en la primera palabra si no
     * hay), el desplazamiento del segundo, su color (0xBBGGRR) y la guinada de la camara
     * alrededor del coche (4096 por vuelta). Validos con `env_ok`. */
    uint32_t env_a[4], env_b[4];
    uint32_t env_scroll, env_colour;
    int16_t  env_yaw;
    /* Las normales de reflejo de sus partes 0, 8, 10, 41, 42, 45 y 46 como las tiene el
     * juego en memoria: env_n[k] normales desde env_norm[env_first[k]] (0: no hay). */
    uint16_t env_n[CAR_ENV_PARTS], env_first[CAR_ENV_PARTS];
    int16_t  env_norm[CAR_ENV_NORMALS][3];
} CarDraw;

/* 99 palabras por coche; hasta 8 coches mas holgura. */
#define CAR_WORDS        99u
#define CAR_CAPTURE_MAX (CAR_WORDS * 16u)

/* Un cuadro de marca de neumatico (M2c paso 6): este punto de borde y el de la entrada
 * anterior, y el color de cada una. 64 segmentos de hasta 23 entradas es el maximo del juego. */
#define SKIDS_MAX (64u * 23u)

typedef struct SkidQuad {
    int32_t  corners[4][3];    /* 16.16: p0 y p1 de esta entrada, y p0 y p1 de la anterior */
    uint32_t colour[2];        /* 0xBBGGRR de esta entrada y de la anterior */
    uint8_t  tex;              /* 0 = SKD0, 1 = SKD1 */
    int16_t  slice;            /* seccion de pista a la que pertenece el segmento */
} SkidQuad;

/* Los destellos (M2c paso 6): lo que el juego construye como 2D alrededor de un punto
 * del mundo. Las farolas de la pista (BWorld_BuildGlareEffects), el humo y el polvo de las
 * ruedas (Souffle_InsertFacet) y los halos de faros y frenos de los coches
 * (DrawC_PrimHalo). Se graban sus primitivas tal cual, en pantalla de PS1, y el punto del
 * que salen, que es lo que da su profundidad al dibujarlas. */
#define GLOWS_MAX       256u
#define GLOW_QUADS_MAX 1024u
#define SKY_QUADS_MAX    64u
#define WEATHER_QUADS_MAX 512u

typedef struct Glow {
    int32_t  anchor[3];        /* 16.16, ejes de PS1 */
    uint32_t car;              /* Car_tObj si es un halo de coche; 0 si no */
    uint8_t  smoke;            /* bocanada de humo o polvo (Sfx_BuildSouffleFacet) */
    uint16_t first, n;         /* sus cuadros en glow_quad[] */
    float    radius;           /* halo de coche: el radio de su caja, en metros */
} Glow;

/* El showroom del front end (M2c paso 7): el coche de la pantalla de seleccion, que el
 * original dibuja con R3DCar_InsertCarFacetMenuII (una vez para el reflejo en el suelo y otra
 * para el coche), y su plataforma giratoria (R3DCar_Showroom). Se graba como la referencia
 * (OverlayCmd::ShowroomCar / ShowroomFloor). */
#define SROOM_MAX          8u
#define SROOM_FLOOR_MAX  256u
#define SROOM_SHADOW_MAX  32u

typedef struct ShowroomCar {
    CarDraw  d;                /* el coche como en carrera: modelo, pose, partes, reflejo */
    uint8_t  colour;           /* la pintura, como CarState::colour */
    uint8_t  mirrored;         /* es su reflejo en el suelo (gp+3532 & 0x80) */
    uint32_t level;            /* su nivel de luz (128 = tal cual; a la mitad en el reflejo) */
    int32_t  cam_pos[3];       /* la camara de la vista, como en carrera */
    int32_t  cam_rot[9];
    uint32_t h;                /* la proyeccion del GTE: distancia y centro (16.16) */
    int32_t  ofx, ofy;
} ShowroomCar;

#define HUD_LISTS_MAX  16u
#define HUD_CMDS_MAX   256u   /* comandos de la pausa por frame */
#define HUD_KEPT_MAX   768u   /* el HUD que se conserva en pausa (~176 cuadros por frame) */
#define MENU_KEPT_MAX  512u   /* el menu de pausa que se conserva */
#define DRAW_ENVS_MAX  64u

typedef struct ObjectDraw {
    uint16_t model;
    uint8_t  billboard;        /* el motor lo gira hacia la camara */
    uint8_t  pad;
    int32_t  matrix[9];        /* giro y escala en espacio de PS1, 16.16, por filas */
    int32_t  pos[3];           /* 16.16 */
} ObjectDraw;

/* El haz de los faros de noche (Night_NightCalc), como NightBeam de la referencia: en el
 * espacio del haz (los ejes del coche del jugador desde su posicion, en 1/64 m), un vertice
 * de la pista con z0 < z < z0 + 2^(zs+6) y |x| < 2^(xs+5) cambia su indice de luz s por
 * table[s][map[(z - z0) >> zs][(x + 2^(xs+5)) >> xs]]. Se aplica por vertice en el SH-4
 * (decision de la seccion 3 de CLAUDE.md). */
typedef struct NightBeam {
    uint8_t  on;               /* noche y el coche del jugador con los faros encendidos */
    int32_t  z0;
    uint32_t xs, zs;
    float    origin[3];        /* metros, espacio del mundo (la X de PS1 espejada) */
    float    right[3], forward[3];
    uint8_t  map[4096];        /* 64 x 64 niveles */
    uint8_t  table[4096];      /* 256 x 16 indices de la tabla de luz */
} NightBeam;

typedef struct RaceView {
    const NightBeam *beam;     /* el haz de noche de esta vista, o NULL */
    uint32_t player;
    int16_t  rect[4];          /* recorte relativo al de la vista 0, y su tamano */
    int32_t  camera_pos[3];
    int32_t  camera_rot[9];    /* matriz 3x3, por filas */
    uint32_t projection;
    uint16_t chunks_n;
    int16_t  chunk[VIEW_CHUNKS_MAX];     /* bloque de pista; negativo = hueco */
    uint16_t detail[VIEW_CHUNKS_MAX];    /* nivel de detalle que pide el juego */
    /* El centro de la proyeccion en la pantalla de PS1 (16.16), si no es el del rectangulo:
     * el showroom lo fija con OFX/OFY del GTE. */
    uint8_t  centre_on;
    int32_t  centre[2];
} RaceView;

typedef struct GameHost {
    Ps1Host  iface;          /* lo que ve ps1rt; `self` apunta a este struct */
    HleFunc  hle[HOST_HLE_MAX];
    uint32_t hle_n;          /* ordenadas por direccion, para busqueda binaria */
    uint32_t seen_n;         /* cuantas distintas se han llamado ya */
    const char *gamedata;

    /* Si es 0, la primera HLE sin implementar para el programa con un diagnostico.
     * Si es 1, devuelve 0 en v0 y sigue, para ver el maximo de llamadas en una sola
     * ejecucion. Lo segundo da mas informacion, pero a partir de la primera el
     * estado del juego ya no es fiable. */
    int survey;
    /* --idlelog: en los primeros idles, imprime donde esta girando el juego. */
    uint64_t win_lo, win_hi;  /* --callwin lo hi: registra la secuencia de llamadas */
    uint64_t ram_at_call;     /* --ramdump N FILE: vuelca los 2 MB en esa llamada */
    const char *ram_at_path;
    /* --framedump N FILE: los 2 MB en la frontera del frame N. Es la "segunda pasada"
     * que anticipa el formato de dump.c: con el frame divergente ya localizado por los
     * hashes, se vuelve a correr volcando la RAM en crudo justo en ese punto. Hace falta
     * un disparador por frame y no por llamada, porque el volcado del frame ocurre
     * *dentro* de una llamada al host, despues de su manejador. */
    uint64_t frame_dump_at;
    const char *frame_dump_path;
    int      ram_dumped;
    int idlelog;
    /* --textlog: el texto que el juego manda a Font_TextXY, con su frame. Es la forma
     * barata de saber **en que pantalla** esta el front end sin adivinar: el juego lo
     * dice. Sirve para derivar la navegacion del guion de carrera en vez de probar
     * pulsaciones a ciegas. Solo lee RAM, asi que no altera la ejecucion. */
    int textlog;

    struct { uint32_t addr; uint64_t n; uint64_t first; } seen[HOST_SEEN_MAX];
    uint32_t seen_addrs;

    /* ---- vistas (views.c) ---- */
    HostMode mode;           /* arranca en el front end, como la referencia */
    int      capturing;      /* dentro de una rutina de dibujado capturada */
    int      unlock;         /* --unlock: pistas y coches disponibles (ver unlock.c) */
    uint64_t unlocks;
    uint64_t views_fonttext;      /* Font_TextXY atendidas por el host */
    uint64_t views_captured;      /* rutinas corridas en marco anidado */
    uint64_t showroom_cars;       /* R3DCar_InsertCarFacetMenuII atendidas */
    uint64_t showroom_floors;     /* R3DCar_Showroom atendidas */

    uint64_t unknown_calls;  /* llamadas al host que no estan en la lista HLE */
    uint32_t unknown_last;
    uint64_t idles;

    /* ---- estado del sistema ---- */
    uint64_t clock;          /* reloj virtual, 1/1920 s */
    /* Reloj REAL (Dreamcast): en cada frontera de frame el reloj alcanza al tiempo real,
     * como los temporizadores de la PS1, que avanzan tarde lo que tarde el frame. Sin el
     * (PC, test diferencial), un campo por frame: a menos de 60 fps el juego iria a
     * camara lenta. Necesita now_us. */
    int      real_clock;
    uint64_t real_us0, real_clock0;    /* el ancla: el reloj y now_us en la primera frontera */
    uint64_t real_dropped;             /* tiempo real descartado por el tope (1/1920 s) */
    uint32_t rcnt;           /* lo que devuelve GetRCnt */
    uint32_t next_event;     /* descriptores de OpenEvent */
    uint32_t pad_buffers[2];
    uint16_t pad_buttons[2];  /* botones pulsados, logica normal */
    /* La plataforma rellena pad_buttons aqui, una vez por frame, justo antes de que
     * se escriban los buffers de libpad. En Dreamcast lee el mando por maple; en el
     * host de PC es NULL y manda el guion de pad_steps. */
    void (*poll_input)(struct GameHost *h);
    /* La plataforma dibuja aqui: se llama con la lista COMPLETA y las texturas
     * ya convertidas, justo antes de vaciarla para el frame siguiente. */
    void (*on_draw)(struct GameHost *h);
    /* Movie_Play (M3): la plataforma reproduce la pelicula `name` (MOVIES\name del disco)
     * de principio a fin y devuelve los botones de PS1 con los que se corto (0 si acabo
     * sola o no se pudo reproducir). NULL: no hay video (el arnes de PC y el test
     * diferencial, que se lo saltan en los dos lados). */
    uint16_t (*play_movie)(struct GameHost *h, const char *name);
    struct { uint64_t frame; uint16_t buttons; } pad_steps[PAD_SCRIPT_MAX];
    uint32_t pad_steps_n, pad_at;
    uint64_t pad_end;         /* campo en el que termina el guion (0: sin fin) */

    /* ---- volcado para el test diferencial ---- */
#if PS1_DIAG
    FILE    *dump;
    FILE    *ovl_dump;
    FILE    *view_dump;
#endif
    uint64_t dump_frames;
    uint64_t pad_writes;

    /* ---- memory card: comando pendiente ---- */
    int      mc_pending;
    uint32_t mc_cmd, mc_result;
    uint64_t mc_syncs, mc_writes_refused;

    /* ---- videos del front end ---- */
    uint64_t video_calls;
    uint32_t last_video_call;
    uint64_t video_frames;        /* frames del video wall escritos en la VRAM (M3) */
    /* Reloj de la plataforma en microsegundos (NULL: no se mide). Con el, el tiempo
     * acumulado del video wall, de la conversion de texturas y del dibujo. */
    uint64_t (*now_us)(void);
    uint64_t video_us, tex_us, draw_us;

    /* ---- dibujo que se cuenta pero no se pinta (M2a) ---- */
    uint64_t fe_shapes, fe_squares, player_views;
    /* La lista de dibujo 2D de este frame (M2a paso 1). El port graba lo que el
     * juego quiere pintar; dibujarlo con el PVR son los pasos 3 y 4. */
    Overlay  overlay;
    uint64_t overlay_unknown;   /* primitivas de GPU que no se saben grabar */
    uint32_t overlay_max;       /* el frame con mas cuadros */
    /* Texturas convertidas de la VRAM de PS1 (M2a paso 3). El buffer de texeles
     * lo pone la plataforma: en Dreamcast sera memoria del PVR. */
    TexCache texcache;
    uint16_t *tex_buf;
    uint32_t  tex_cap;          /* en texeles */
    /* Las vistas de carrera preparadas en el frame en curso. */
    RaceView view[VIEWS_MAX];
    uint32_t views_n;

    /* Chunk_lightTable: el color de vertice de la pista, indexado por la sombra que
     * cada vertice trae horneada. Vive en la RAM del juego y cambia con la pista, la
     * variante y el clima, asi que se relee cuando el puntero se mueve. */
    uint32_t light_table[256];
    uint32_t light_ptr;
    SkyData  sky;

    /* Lo que el juego decide dibujar de cada coche, como lista de palabras (99 por
     * coche; ver cars.c para el orden). Se guarda en crudo para poder compararla palabra
     * a palabra con la referencia. */
    CarDraw  car_draw[CARS_MAX];
    SkidQuad skids[SKIDS_MAX];
    uint32_t skids_n, skids_max;
    uint64_t skids_lost;
    /* Los destellos del frame (ver Glow), en el orden de la referencia: farolas, humo y
     * despues los halos de los coches. El flare del sol y el clima son 2D sin ancla. */
    Glow        glows[GLOWS_MAX];
    uint32_t    glows_n;
    OverlayQuad glow_quad[GLOW_QUADS_MAX];
    uint32_t    glow_quads_n;
    OverlayQuad sky_quad[SKY_QUADS_MAX];
    uint32_t    sky_quads_n;
    OverlayQuad weather_quad[WEATHER_QUADS_MAX];
    uint32_t    weather_quads_n;
    uint64_t    glows_lost;   /* cuadros que no cupieron; si no es 0, suben los topes */
    /* Los tramos del buffer de primitivas con los halos de cada coche: se graban todos al
     * terminar los coches, como la referencia, porque el orden fija el modo de dibujo. */
    uint32_t    halo_rng[16][3];         /* inicio, fin, Car_tObj */
    uint32_t    halo_rng_n;
    /* El showroom del frame (ver ShowroomCar) y su suelo; la sombra se guarda aparte hasta
     * que la recoge el suelo, como la referencia (que puede ser en el frame siguiente). */
    ShowroomCar sroom[SROOM_MAX];
    uint32_t    sroom_n;
    OverlayQuad sroom_floor[SROOM_FLOOR_MAX];
    uint32_t    sroom_floor_n;
    OverlayQuad sroom_shadow[SROOM_SHADOW_MAX];
    uint32_t    sroom_shadow_n;
    int8_t      sroom_type;               /* DrawCar: el tipo de coche que se ensena */
    uint8_t     sroom_paint;
    uint8_t     sroom_type_ok;
    uint32_t cars_cap_buf[CAR_CAPTURE_MAX];
    uint32_t cars_cap;        /* palabras escritas */
    uint32_t cars_n;          /* coches anotados */
    uint64_t cars_sum, cars_lost;
    uint32_t cars_max;
    /* Ancho del cono de visibilidad de los coches. 1 es el original (4:3 de PS1); 2 es
     * la mejora de la referencia, que hace falta para que los dos lados se puedan
     * comparar. Ver docs/hitos/M1a.md. */
    uint32_t car_cone_width;

    /* Los objetos del escenario del frame en curso. */
    ObjectDraw objects[OBJECTS_MAX];
    uint32_t   objects_n;
    uint32_t   obj_models[OBJ_MODELS_MAX];
    uint64_t   objects_sum, objects_lost;
    uint32_t   objects_max;

    /* El nombre base del ultimo .GRP que el juego abrio: es como el port sabe QUE pista
     * tiene que cargar horneada, sin tener que leerselo al juego por otro sitio. */
    char     track_name[16];
    NightBeam beam;          /* el de la ultima vista grabada (ver NightBeam) */
    /* La letra de region de la ultima librea de policia que el juego abrio, por modelo (ver
     * host_car_file): `zzzgbmws.viv` deja 'G' para el BMW. 0 si no abrio ninguna. */
    char     police[5];
    int      light_ok;

    /* Trozos de pista que cada vista decide dibujar. Es la cifra que dimensiona M2b:
     * el juego los elige, el PVR los tendra que transformar y pintar. */
    uint32_t race_chunks;     /* los de la ultima vista */
    uint32_t race_chunks_max;
    uint64_t race_chunks_sum;
    uint64_t race_views;      /* cuantas vistas de carrera se han preparado */
    uint64_t fields;          /* campos de video de 60 Hz transcurridos */
    uint64_t last_top;        /* reloj en la frontera de frame anterior */
    uint64_t frames;          /* frames del juego (frame_top), la clave del volcado */
    uint32_t actuators[2];   /* motores del DualShock (PadSetAct) */
    uint32_t cb_vsync;       /* VSyncCallback */
    int32_t  draw_origin[2]; /* esquina del buffer de dibujo (PutDrawEnv) */

    /* ---- capa de archivos ---- */
    FileLayer files;

    /* ---- videos ---- */
    uint64_t movies_played;
    uint32_t last_movie;

    /* ---- espejo de VRAM ---- */
    uint16_t vram[VRAM_W * VRAM_H];
    uint16_t vram_tmp[VRAM_W * VRAM_H];   /* para MoveImage con solape */
    uint64_t vram_reads, vram_writes;
    uint64_t otags, flips;

    /* ---- el HUD de carrera (M2c paso 5) ----
     * En carrera los ayudantes del HUD no se sustituyen: corre el codigo original, que
     * construye sus primitivas en tablas de ordenacion. El host graba, en cada DrawOTag,
     * todas las tablas salvo la del mundo, y al cerrar el frame se queda con las que van
     * detras de las vistas de jugador. Cada tabla grabada es un tramo contiguo de la
     * lista 2D; aqui se apunta donde empieza cada uno. */
    uint32_t otags_frame;                 /* DrawOTag desde la ultima frontera de frame */
    uint32_t hud_start[HUD_LISTS_MAX];
    uint32_t hud_lists;
    uint32_t hud_end;                     /* donde acaba la ultima tabla grabada */
    uint64_t hud_single;                  /* frames con una sola tabla (ver overlay.c) */

    /* ---- la pausa (M2c) ----
     * En pausa el original no vuelve a pintar la imagen: copia la ultima y dibuja encima el
     * menu con Font_TextXY y los ayudantes del HUD, que el host atiende como la referencia
     * (comandos en hud_cmd, ver OVL_HUD_CMD). Para dibujar, el host conserva lo que la
     * pausa encontro: las vistas, el HUD y el flare del sol del ultimo frame jugado, y el
     * ultimo menu (el juego solo lo redibuja cuando cambia). */
    HudCmd      hud_cmd[HUD_CMDS_MAX];
    uint32_t    hud_cmds_n;
    uint8_t     ovl_hud;                  /* overlay_push marca OVL_HUD_QUAD */
    int         paused;                   /* este frame es de pausa (host_frame_top) */
    uint8_t     cams_held[2u * 0x110u];   /* Camera_gInfo al pausar (hold_cameras) */
    int         cams_held_ok;
    OverlayQuad sky_kept[SKY_QUADS_MAX];
    uint32_t    sky_kept_n;
    RaceView    views_kept[VIEWS_MAX];
    uint32_t    views_kept_n;
    OverlayQuad hud_kept[HUD_KEPT_MAX];
    uint32_t    hud_kept_n;
    OverlayQuad menu_kept[MENU_KEPT_MAX];
    uint32_t    menu_kept_n;
    uint64_t    hud_kept_lost;            /* cuadros que no cupieron; si no es 0, sube el tope */
    int32_t  area_tl[2];                  /* esquina del ultimo E3 */
    /* SetDrawEnv: el recorte y el desplazamiento de cada paquete de entorno, para
     * recortar las vistas de HUD al grabarlas. */
    /* Es un ANILLO: el front end llama a SetDrawEnv con decenas de paquetes distintos
     * y una tabla fija se llenaba de ellos antes de la carrera (en la referencia es un
     * HashMap sin limite). Se busca del mas reciente al mas antiguo. */
    struct { uint32_t pkt; int16_t clip[4]; int16_t ofs[2]; } draw_envs[DRAW_ENVS_MAX];
    uint32_t draw_envs_n;     /* entradas validas */
    uint32_t draw_envs_next;  /* donde va la siguiente nueva */
    uint16_t display[4];                  /* area de pantalla del ultimo PutDispEnv */
} GameHost;

/* Mueve el reloj virtual hasta t, encolando las interrupciones de timer que caen en
 * medio y la devolucion de VSync de cada campo. */
void host_advance_to(GameHost *h, Cpu *c, uint64_t t);
/* Reloj real en unidades del reloj del juego (1/1920 s); fija el ancla la primera vez.
 * Solo con real_clock y now_us. */
uint64_t host_real_clock(GameHost *h);

/* Modulos del host. Devuelven 1 si atendieron la llamada, y entonces dejan el
 * resultado en *ret. */
int  host_system_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_gpu_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_file_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_game_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_memcard_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_video_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
/* Abre un archivo del disco extraido (sin leerlo) y da su tamano; 0 si no esta. */
int  host_disk_open(GameHost *h, const char *name, FILE **out, uint32_t *size);
/* Hooks: funciones que SI estan recompiladas, donde el host solo mira o produce un
 * efecto lateral y despues deja correr el codigo original. */
void host_hook(GameHost *h, Cpu *c, uint32_t addr);
int  host_views_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);

/* Graba los objetos del escenario que el juego dibuja en la vista preparada. */
void host_record_objects(GameHost *h, Cpu *c);

/* Graba lo que el juego decide dibujar de cada coche. */
void host_record_cars(GameHost *h, Cpu *c);

/* Lee las marcas de neumatico de la RAM del juego. */
void host_read_skids(GameHost *h, Cpu *c);
/* glows.c: las farolas, el humo, el clima y el flare del sol de la vista que se graba
 * (llamar tras los objetos, con la lista de trozos ya hecha), y los halos de los coches
 * (tras host_record_cars). */
void host_record_glows(GameHost *h, Cpu *c, uint32_t chunks);
void host_record_halos(GameHost *h, Cpu *c);
void host_glows_reset(GameHost *h);

/* El HUD de carrera: grabar una tabla de ordenacion en DrawOTag, y componer las del
 * frame en la frontera. Ver overlay.c. */
uint32_t overlay_ot_len(Cpu *c, uint32_t start);
void     overlay_record_ot(GameHost *h, Cpu *c, uint32_t start);
void     overlay_race_compose(GameHost *h);
void host_unlock_everything(GameHost *h, Cpu *c);
uint32_t host_capture_prims(GameHost *h, Cpu *c, uint32_t addr);
int  host_showroom_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
/* Rellena los buffers de libpad. Se llama en el frame_top, no en cada campo. */
void pad_write(GameHost *h, Cpu *c);

/* Pide a la cache las texturas que usa la lista de dibujo de este frame y convierte
 * lo que falte. Se llama con la lista ya completa. */
void host_build_textures(GameHost *h);

/* El modelo horneado de un coche (el nombre de su .CAR, 4 letras y el 0 en `out`). Es su
 * codigo, salvo la policia: CBMW, CDBL, CCMR, CCOV y CCPR no son archivos, el juego cambia la
 * primera letra por la de la region de la pista y abre esa librea (CBMW -> zzzgbmws.viv en
 * Alemania). Aqui se usa la ultima que abrio, como opened_variant de la referencia. */
const char *host_car_file(const GameHost *h, const char *code, char out[5]);

/* Frontera de frame del juego, con la misma regla que la referencia:
 *   - front end: PutDispEnv del flip del motor, con ra == 0x800BF164
 *   - carrera:   systemtask en el tope del bucle principal, ra == 0x800B7AA4
 * Es donde se aplica la entrada y donde se vuelca el estado: los dos lados tienen que
 * hacerlo en el MISMO punto o la comparacion no significa nada. */
void host_frame_top(GameHost *h, Cpu *c);
#define RA_FLIP_PUT_DISP_ENV 0x800BF164u
#define RA_MAIN_LOOP_YIELD   0x800B7AA4u
/* Carga un guion de entrada; 0 si no pudo leerlo o no lo entiende. Sirve en las dos
 * plataformas: en PC lo pasa --script y en Dreamcast sale del propio disco. */
int  pad_load_script(GameHost *h, const char *path);

#if PS1_DIAG
/* Volcado de la LISTA DE DIBUJO por frame: el oraculo de la grabacion 2D de M2a. La
 * referencia escribe el mismo formato con NFSHS_DIFF_OVERLAY. */
int  overlay_dump_open(GameHost *h, const char *path);
void overlay_dump_frame(GameHost *h);
void overlay_dump_close(GameHost *h);
void overlay_dump_quad(FILE *f, const OverlayQuad *q);

/* Volcado de la VISTA DE CARRERA por frame: el oraculo del paso 1 de M2b. La referencia
 * escribe el mismo formato con NFSHS_DIFF_VIEW. */
int  view_dump_open(GameHost *h, const char *path);
void view_dump_frame(GameHost *h);
void view_dump_close(GameHost *h);

/* Volcado del estado por frame (ver dump.c para el formato). */
int  dump_open(GameHost *h, const char *path);
void dump_frame(GameHost *h, const Cpu *c);
int  dump_ram(const Cpu *c, const char *path);
void dump_close(GameHost *h);
#endif

/* La tabla HLE horneada desde tools/hle_funcs.txt por dc/tools/recomp/bake_hle.py,
 * en dc/gen/hle_table.c. Ya viene sin duplicados y ordenada por direccion. */
typedef struct {
    uint32_t    addr;
    const char *name;
} HostHleBaked;
extern const HostHleBaked host_hle_baked[];
extern const uint32_t     host_hle_baked_n;

/* Deja el host listo con la lista de funciones HLE. Si hle_funcs_path es NULL usa la
 * tabla horneada (lo que hace Dreamcast); si no, lee ese archivo (lo que hace el host
 * de PC, que lo tiene al lado). Devuelve 0 si se queda sin lista. */
int  host_init(GameHost *h, const char *hle_funcs_path, const char *gamedata);
void host_attach(GameHost *h, Cpu *c);
/* Informe de lo que el juego pidio, en orden de primera aparicion. */
void host_report(const GameHost *h, const Cpu *c);
#if PS1_DIAG
/* Tabla "direccion cuenta" de las HLE llamadas, para diferenciarla contra la
 * referencia. Devuelve 0 si no pudo escribir el archivo. */
int  host_call_counts(GameHost *h, const char *path);
#endif

#endif /* PS1_HOST_H */
