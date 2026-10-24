/* Direcciones del NFS4.EXE de retail (NTSC-U, SLUS-00826).
 *
 * Tomadas de `addr` en src/ps1/race.rs de la referencia, que es donde estan
 * identificadas. Solo las que hace falta para arrancar y para diagnosticar; el resto
 * se iran anadiendo cuando se necesiten.
 */
#ifndef PS1_HOST_ADDR_H
#define PS1_HOST_ADDR_H

#define ADDR_GP           0x8013D83Cu
#define ADDR_STACK_TOP    0x801FFFF0u
#define ADDR_BSS_START    0x8013F084u
#define ADDR_BSS_END      0x80149E40u

/* `main`: arranque, y despues front end y carreras para siempre. */
#define ADDR_MAIN         0x800A52A4u

/* Secuencia de arranque por partes, para poder entrar mas fino que por `main`. */
#define ADDR_INIT_A       0x800E4C30u
#define ADDR_INIT_B       0x800DD05Cu
#define ADDR_LIB_STARTUP  0x800A4950u   /* nfs2_system_n_lib_startup */
#define ADDR_LOAD_FE      0x800A506Cu   /* carga el overlay del front end */

/* ---- la vista del jugador en carrera (Render_RenderPlayerView, 0x800B4024) ----
 *
 * El manejador de la referencia (src/ps1/host/views.rs) mezcla dos cosas: LOGICA que el
 * original ejecuta mientras dibuja, y la GRABACION de la geometria para su renderer.
 * Aqui solo se porta la logica; la grabacion es M2b/M2c.
 */
#define ADDR_RENDER_VIEW        0x80118234u  /* DRender_tView: id, jugador, camara */
#define ADDR_START_WORLD_VIEW   0x800B3EE4u  /* camara, niebla y colores -> GTE */
#define ADDR_SETUP_BUILD_MAT    0x8007E2E8u  /* SetupBuildMatrices(view, dcache) */
#define ADDR_WORLD_SETUP_MAT    0x800C829Cu  /* DrawW_WorldSetUpMatrix(gWorldMat, +0x14) */
#define ADDR_G_WORLD_MAT        0x80110104u
#define ADDR_CHUNK_BUILD_LIST   0x8007E71Cu  /* SetupChunkBuildList(view) -> cuantos */
#define ADDR_CHUNK_BUILD_COUNT  (ADDR_GP + 608u)
#define ADDR_CHUNK_BUILD_LIST_D 0x801102B0u  /* pares (i16 bloque, u16 detalle), 64 max */
#define ADDR_CAMERA_ZOOM        0x8013DAD0u  /* proyeccion de la camara */
#define ADDR_DRAW_VIEWS         0x8011FF44u  /* Draw_gViews[]: 200 bytes por vista */
#define ADDR_LIGHT_TABLE        0x8013DB0Cu  /* PUNTERO a Chunk_lightTable: 256 x 0xBBGGRR */
#define ADDR_HUD_WRONG_WAY      0x8009BAC4u  /* DashHud_CheckWrongWay(jugador) */
#define ADDR_HUD_CALC           0x8009BB34u  /* DashHud_HudCalc: velocidad, marcha... */
/* Scratchpad: el original lo usa de cache de dibujo en estas llamadas. */
#define ADDR_DCACHE             0x1F800000u

/* ---- la fuente del front end (Font_TextXY) ----
 * El texto del front end no es un comando aparte: es un cuadro por caracter, sacado de
 * la imagen de 4 bits de la fuente actual. */
#define ADDR_FONT_BLITTER      (ADDR_GP + 6328u)  /* que rutina pinta los glifos */
#define ADDR_FONT_BLIT         0x800CC00Cu        /* la normal */
#define ADDR_FONT_UPSIDE_BLIT  0x8004F49Cu        /* el reflejo del suelo del concesionario */
#define ADDR_FONT_TABLE        0x80136F14u        /* tabla de glifos (11 bytes cada uno) */
#define ADDR_FONT_COUNT        0x80136F04u
#define ADDR_FONT_SHAPE        0x80136F18u        /* la forma con la imagen de la fuente */
#define ADDR_FONT_CURRENT_CLUT (ADDR_GP + 4880u)
#define ADDR_FONT_COLOUR       (ADDR_GP + 4872u)
#define ADDR_FONT_SPACE        (ADDR_GP + 4882u)  /* ancho del espacio */
#define ADDR_FONT_UP_CLUT      0x8013EB4Cu
#define ADDR_FONT_UP_COLOUR    0x8013EB44u

#endif /* PS1_HOST_ADDR_H */
