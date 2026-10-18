/* La capa de archivos de EA (FILE_*), servida desde el disco extraido.
 *
 * El juego la usa de dos formas: las `*sync` son sincronas y devuelven el resultado al
 * momento, y las asincronas (FILE_open, FILE_read, FILE_size, FILE_close) devuelven un
 * identificador de operacion que luego se consulta. Aqui, igual que en la referencia,
 * **todas terminan en el acto**: no hay disco lento que esperar, asi que una operacion
 * nace ya completada.
 *
 * Portado de src/ps1/host/files.rs.
 */
#ifndef PS1_HOST_FILES_H
#define PS1_HOST_FILES_H

#include "ps1rt.h"

#define FILES_MAX          64u     /* archivos abiertos a la vez */
#define FILE_OPS_MAX       64u     /* operaciones asincronas vivas */
#define MOUNTS_MAX          4u     /* archivos BIGF montados (FILE_addbigsync) */
#define FILE_NAME_MAX      64u
/* Tope de cordura, no un limite de diseno: las entradas se reservan segun las que
 * declare el archivo (ZZZFE.VIV, el del front end, tiene 435). */
#define MOUNT_ENTRIES_MAX 65536u

typedef struct {
    uint32_t handle;               /* 0: libre */
    uint8_t *data;
    uint32_t size;
} OpenFile;

typedef struct {
    uint32_t op;                   /* 0: libre */
    uint32_t result;
    uint32_t user;
} FileOp;

typedef struct {
    char     name[FILE_NAME_MAX];
    uint32_t off, size;
} MountEntry;

typedef struct {
    uint32_t    handle;            /* 0: libre */
    uint8_t    *blob;
    uint32_t    blob_size;
    MountEntry *ent;               /* reservadas al montar */
    uint32_t    n;
} Mount;

typedef struct {
    OpenFile open[FILES_MAX];
    FileOp   ops[FILE_OPS_MAX];
    Mount    mounts[MOUNTS_MAX];
    uint32_t next_handle;
    uint32_t next_op;

    /* medidas para el informe */
    uint64_t opens, opens_failed, reads, bytes_read, mounts_ok, mounts_failed;
    char     last_missing[256];   /* cabe una ruta completa del juego */
    int      log;                  /* --filelog */
} FileLayer;

void files_init(FileLayer *fl);
void files_free(FileLayer *fl);

#endif /* PS1_HOST_FILES_H */
