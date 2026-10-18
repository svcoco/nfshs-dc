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

#endif /* PS1_HOST_ADDR_H */
