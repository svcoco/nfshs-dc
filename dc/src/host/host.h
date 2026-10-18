/* Host del port: los reemplazos de alto nivel (HLE) de las funciones de NFS4.EXE que
 * manejan el hardware de la PS1.
 *
 * La lista de direcciones es la misma que usa el recompilador, tools/hle_funcs.txt, y
 * se lee en ejecucion para que no pueda quedar desincronizada con el C generado.
 *
 * Estado: andamio de medicion. Todavia no hay ninguna funcion implementada; lo que
 * hace es registrar que pide el juego, en que orden y cuantas veces, porque eso es lo
 * que decide por donde seguir en vez de implementar 119 funciones a ciegas.
 */
#ifndef PS1_HOST_H
#define PS1_HOST_H

#include "ps1rt.h"
#include "files.h"

#include <stdio.h>

#define HOST_HLE_MAX 256u
#define PAD_SCRIPT_MAX 256u
/* Tabla de TODAS las llamadas que ve el host, por direccion (HLE y no HLE), para
 * poder diferenciarla contra la de la referencia. */
#define HOST_SEEN_MAX 4096u

/* Reloj virtual del host, en unidades de 1/1920 de segundo (los mismos valores que
 * TICK y FIELD en src/ps1/host/mod.rs de la referencia): 1920/15 = 128 Hz para el
 * root counter del juego, y 1920/32 = 60 Hz para los campos de video. */
#define HOST_TICK   15u
#define HOST_FIELD  32u
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

typedef struct {
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
    uint64_t views_hud_unported;  /* ayudantes del HUD que habria que atender y no estan */

    uint64_t unknown_calls;  /* llamadas al host que no estan en la lista HLE */
    uint32_t unknown_last;
    uint64_t idles;

    /* ---- estado del sistema ---- */
    uint64_t clock;          /* reloj virtual, 1/1920 s */
    uint32_t rcnt;           /* lo que devuelve GetRCnt */
    uint32_t next_event;     /* descriptores de OpenEvent */
    uint32_t pad_buffers[2];
    uint16_t pad_buttons[2];  /* botones pulsados, logica normal */
    struct { uint64_t frame; uint16_t buttons; } pad_steps[PAD_SCRIPT_MAX];
    uint32_t pad_steps_n, pad_at;
    uint64_t pad_end;         /* campo en el que termina el guion (0: sin fin) */

    /* ---- volcado para el test diferencial ---- */
    FILE    *dump;
    uint64_t dump_frames;
    uint64_t pad_writes;

    /* ---- memory card: comando pendiente ---- */
    int      mc_pending;
    uint32_t mc_cmd, mc_result;
    uint64_t mc_syncs, mc_writes_refused;

    /* ---- videos del front end ---- */
    uint64_t video_calls;
    uint32_t last_video_call;

    /* ---- dibujo que se cuenta pero no se pinta (M2a) ---- */
    uint64_t fe_shapes, fe_squares, player_views;
    uint64_t fields;          /* campos de video de 60 Hz transcurridos */
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
    uint16_t display[4];                  /* area de pantalla del ultimo PutDispEnv */
} GameHost;

/* Mueve el reloj virtual hasta t, encolando las interrupciones de timer que caen en
 * medio y la devolucion de VSync de cada campo. */
void host_advance_to(GameHost *h, Cpu *c, uint64_t t);

/* Modulos del host. Devuelven 1 si atendieron la llamada, y entonces dejan el
 * resultado en *ret. */
int  host_system_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_gpu_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_file_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_game_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_memcard_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
int  host_video_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
/* Hooks: funciones que SI estan recompiladas, donde el host solo mira o produce un
 * efecto lateral y despues deja correr el codigo original. */
void host_hook(GameHost *h, Cpu *c, uint32_t addr);
int  host_views_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
void host_unlock_everything(GameHost *h, Cpu *c);
uint32_t host_capture_prims(GameHost *h, Cpu *c, uint32_t addr);
int  host_showroom_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret);
/* Rellena los buffers de libpad. Se llama en el frame_top, no en cada campo. */
void pad_write(GameHost *h, Cpu *c);

/* Frontera de frame del juego, con la misma regla que la referencia:
 *   - front end: PutDispEnv del flip del motor, con ra == 0x800BF164
 *   - carrera:   systemtask en el tope del bucle principal, ra == 0x800B7AA4
 * Es donde se aplica la entrada y donde se vuelca el estado: los dos lados tienen que
 * hacerlo en el MISMO punto o la comparacion no significa nada. */
void host_frame_top(GameHost *h, Cpu *c);
#define RA_FLIP_PUT_DISP_ENV 0x800BF164u
#define RA_MAIN_LOOP_YIELD   0x800B7AA4u
/* Carga un guion de entrada; 0 si no pudo leerlo o no lo entiende. */
int  pad_load_script(GameHost *h, const char *path);

/* Volcado del estado por frame (ver dump.c para el formato). */
int  dump_open(GameHost *h, const char *path);
void dump_frame(GameHost *h, const Cpu *c);
int  dump_ram(const Cpu *c, const char *path);
void dump_close(GameHost *h);

/* Carga tools/hle_funcs.txt y deja el host listo. Devuelve 0 si no pudo leer el
 * archivo. */
int  host_init(GameHost *h, const char *hle_funcs_path, const char *gamedata);
void host_attach(GameHost *h, Cpu *c);
/* Informe de lo que el juego pidio, en orden de primera aparicion. */
void host_report(const GameHost *h, const Cpu *c);
/* Tabla "direccion cuenta" de las HLE llamadas, para diferenciarla contra la
 * referencia. Devuelve 0 si no pudo escribir el archivo. */
int  host_call_counts(GameHost *h, const char *path);

#endif /* PS1_HOST_H */
