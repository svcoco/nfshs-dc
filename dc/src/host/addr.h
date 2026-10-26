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
/* Los coches (M2c paso 3). El juego decide que se dibuja de cada uno y lo dibuja; el
 * port corre su rutina anidada y lo anota. */
#define ADDR_R3DCAR_INSERT_ALL_LIST  0x800B38C0u  /* R3DCar_InsertAllListFacet(vista) */
#define ADDR_R3DCAR_INSERT_CAR_FACET_II 0x800B2E30u
#define ADDR_R3DCAR_VISIBILITY       0x800B03E4u
#define ADDR_CAR_DETAIL              0x8C2u       /* desplazamiento dentro de Car_tObj */
#define ADDR_R3DCAR_OBJECT_VISIBLE   0x80117B64u  /* 57 bytes: que partes se dibujan */
#define ADDR_LIGHT_CODES             0x801207F8u  /* 26 palabras, por textura */
#define ADDR_DRAWC_PRIM_START        0x800BFAC8u
#define ADDR_R3DCAR_CENTER           0x8013E674u
#define ADDR_CAR_CACHE               0x1F800000u  /* el scratchpad */
#define ADDR_BWORLDSM_QUAD_LIGHT     0x80080868u
#define ADDR_SHADOW_ON               (ADDR_GP + 3648u)
#define ADDR_SHADOW_COLOUR           (ADDR_GP + 3652u)

/* Los objetos del escenario (M2c paso 1). El juego no los deja listos en su RAM: los
 * dibuja, asi que se corre DrawW_DoObjects anidada y se interceptan sus rutinas. */
#define ADDR_DRAWW_DO_OBJECTS        0x800C9A24u  /* DoObjects(vista, lista de trozos) */
#define ADDR_OBJECT_DEFS             0x8013E7BCu  /* PUNTERO a la tabla de definiciones */
#define ADDR_DRAW_OBJECT_TRANSFORM   0x800C8C1Cu
#define ADDR_DRAW_OBJECT_SIMPLE      0x800C8F74u
#define ADDR_OBJECT_CLIPPED          0x800C9930u  /* recorta con el cono 4:3 de PS1 */
#define ADDR_CHUNK_OBJECT_DRAW_RA    0x800C989Cu  /* donde vuelve el dibujo de un bloque */
#define ADDR_FLARE_HALO              0x800CEBFCu
#define ADDR_FLARE_HALO2             0x800CE290u

#define ADDR_LIGHT_TABLE        0x8013DB0Cu  /* PUNTERO a Chunk_lightTable: 256 x 0xBBGGRR */

/* El cielo (M2b paso 4). No es un asset: sus vertices y colores viven en la RAM del
 * juego; del PSH solo salen las texturas HZ01..HZ12.
 *   - la cupula son 5 anillos x 17 puntos, el horizonte primero
 *   - el horizonte es un anillo de 17 puntos de radio 32000
 */
#define ADDR_SKY_HREC      (ADDR_GP + 4916u)  /* PUNTERO al registro del horizonte */
#define ADDR_SKY_DOMETEX   (ADDR_GP + 4920u)  /* PUNTERO al registro de textura de cupula */
#define ADDR_SKY_RING      (ADDR_GP + 4924u)  /* PUNTERO al anillo del horizonte */
#define ADDR_SKY_MESH          0x80121668u    /* gSkyMesh: 85 x 8 bytes (x, y, z) */
#define ADDR_SKY_COLOR         0x80121910u    /* gSkyColor: 85 x 4 bytes RGB */
#define ADDR_SKY_DOMETEX_TAB   0x80121A64u    /* 64 bytes: pixmap por cuadro de cupula */
#define ADDR_SKY_BOTTOM_COLORS 0x80121AE8u    /* 17 x 4 bytes RGB, borde bajo del horizonte */
#define ADDR_SKY_TOP_COLORS    0x80121B28u    /* 17 x 4 bytes RGB, borde alto */
#define ADDR_SKY_BAND          0x80124570u    /* si vale 0, el horizonte no se dibuja */
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
#define ADDR_FONT_CLUT_X       (ADDR_GP + 4864u)  /* x de la primera de sus 16 paletas */
/* HudPmx_gShapes: las formas del HUD, 20 bytes como las UV de un POLY_GT4 (u, v, paleta;
 * u, v, pagina; u, v; u, v) y despues ancho y alto. */
#define ADDR_HUD_SHAPES        0x80111F5Cu
/* Camera_gInfo: las camaras de los dos jugadores, 0x110 bytes cada una (modo en +0x70). */
#define ADDR_CAMERA_INFO       0x80110570u
#define ADDR_FONT_UP_CLUT      0x8013EB4Cu
#define ADDR_FONT_UP_COLOUR    0x8013EB44u

#endif /* PS1_HOST_ADDR_H */
