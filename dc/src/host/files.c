#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void files_init(FileLayer *fl)
{
    memset(fl, 0, sizeof(*fl));
    /* Los handles empiezan en 0x100 y las operaciones en 1, como la referencia
     * (GameHost::new). No es cosmetico: el juego **guarda el handle en su RAM**, asi
     * que numerar distinto hace divergir el test diferencial desde la primera apertura
     * de archivo. Fue la primera divergencia real que encontro el diff. */
    fl->next_handle = 0x100u;
    fl->next_op = 1u;
}

void files_free(FileLayer *fl)
{
    uint32_t i;
    for (i = 0; i < FILES_MAX; i++)
        free(fl->open[i].data);
    for (i = 0; i < MOUNTS_MAX; i++) {
        free(fl->mounts[i].blob);
        free(fl->mounts[i].ent);
    }
    memset(fl, 0, sizeof(*fl));
}

/* ---- utilidades ---- */

static void guest_cstr(Cpu *c, uint32_t a, char *out, size_t n)
{
    size_t i;
    for (i = 0; i + 1u < n; i++) {
        uint32_t ch = ps1_r8(c, a + (uint32_t)i);
        if (ch == 0u)
            break;
        out[i] = (char)ch;
    }
    out[i] = '\0';
}

/* Normaliza un nombre del juego a la forma del disco extraido: quita el prefijo
 * "cdrom:", las barras iniciales y la version ";1", pasa a mayusculas y usa '/' como
 * separador. Igual que resolve() de la referencia. */
static void normalize(const char *name, char *out, size_t n)
{
    const char *p = name;
    size_t i = 0;
    if (strncmp(p, "cdrom:", 6) == 0 || strncmp(p, "CDROM:", 6) == 0)
        p += 6;
    while (*p == '\\' || *p == '/')
        p++;
    for (; *p != '\0' && *p != ';' && i + 1u < n; p++) {
        char ch = *p;
        if (ch == '\\')
            ch = '/';
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
        out[i++] = ch;
    }
    out[i] = '\0';
}

static const char *basename_of(const char *name)
{
    const char *b = name;
    const char *p;
    for (p = name; *p != '\0'; p++)
        if (*p == '/' || *p == '\\' || *p == ':')
            b = p + 1;
    return b;
}

static int eq_nocase(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
        if (x != y)
            return 0;
    }
    return *a == *b;
}

/* Lee el archivo del disco extraido, o lo saca de un BIGF montado. Devuelve el buffer
 * (que el llamante libera) o NULL. */
static uint8_t *lookup(GameHost *h, const char *name, uint32_t *size)
{
    FileLayer *fl = &h->files;
    char norm[256], path[1024];
    FILE *f;
    uint8_t *buf;
    long n;
    uint32_t m, k;

    normalize(name, norm, sizeof(norm));
    snprintf(path, sizeof(path), "%s/%s", h->gamedata, norm);
    f = fopen(path, "rb");
    if (f != NULL) {
        if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0) {
            rewind(f);
            buf = (uint8_t *)malloc((size_t)n > 0u ? (size_t)n : 1u);
            if (buf != NULL && (n == 0 || fread(buf, 1, (size_t)n, f) == (size_t)n)) {
                fclose(f);
                *size = (uint32_t)n;
                return buf;
            }
            free(buf);
        }
        fclose(f);
        return NULL;
    }

    /* no esta en el disco: buscar en los BIGF montados, del mas reciente al mas
     * antiguo y por el nombre base, sin distinguir mayusculas */
    {
        const char *base = basename_of(norm);
        for (m = MOUNTS_MAX; m > 0u; m--) {
            Mount *mo = &fl->mounts[m - 1u];
            if (mo->handle == 0u)
                continue;
            for (k = 0; k < mo->n; k++)
                if (eq_nocase(mo->ent[k].name, base)) {
                    uint32_t sz = mo->ent[k].size;
                    buf = (uint8_t *)malloc(sz > 0u ? sz : 1u);
                    if (buf == NULL)
                        return NULL;
                    memcpy(buf, mo->blob + mo->ent[k].off, sz);
                    *size = sz;
                    return buf;
                }
        }
    }
    return NULL;
}

/* ---- tablas de archivos abiertos y de operaciones ---- */

static OpenFile *find_file(FileLayer *fl, uint32_t handle)
{
    uint32_t i;
    if (handle == 0u)
        return NULL;
    for (i = 0; i < FILES_MAX; i++)
        if (fl->open[i].handle == handle)
            return &fl->open[i];
    return NULL;
}

static uint32_t add_file(Cpu *c, FileLayer *fl, uint8_t *data, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < FILES_MAX; i++)
        if (fl->open[i].handle == 0u) {
            fl->open[i].handle = fl->next_handle++;
            fl->open[i].data = data;
            fl->open[i].size = size;
            return fl->open[i].handle;
        }
    free(data);
    ps1_panic(c, "demasiados archivos abiertos a la vez");
}

static void drop_file(FileLayer *fl, uint32_t handle)
{
    OpenFile *o = find_file(fl, handle);
    if (o != NULL) {
        free(o->data);
        o->handle = 0u;
        o->data = NULL;
        o->size = 0u;
    }
}

/* Una operacion nace ya completada: no hay disco lento que esperar. */
static uint32_t new_op(Cpu *c, FileLayer *fl, uint32_t result, uint32_t user)
{
    uint32_t i;
    for (i = 0; i < FILE_OPS_MAX; i++)
        if (fl->ops[i].op == 0u) {
            fl->ops[i].op = fl->next_op++;
            fl->ops[i].result = result;
            fl->ops[i].user = user;
            return fl->ops[i].op;
        }
    ps1_panic(c, "demasiadas operaciones de archivo vivas");
}

static FileOp *find_op(FileLayer *fl, uint32_t op)
{
    uint32_t i;
    if (op == 0u)
        return NULL;
    for (i = 0; i < FILE_OPS_MAX; i++)
        if (fl->ops[i].op == op)
            return &fl->ops[i];
    return NULL;
}

/* Copia un trozo del archivo a la RAM del invitado y devuelve cuantos bytes fueron. */
static uint32_t read_into(Cpu *c, OpenFile *o, uint32_t off, uint32_t addr, uint32_t want)
{
    uint32_t n;
    if (o == NULL)
        return 0;
    if (off > o->size)
        off = o->size;
    n = o->size - off;
    if (want < n)
        n = want;
    ps1_write_bytes(c, addr, o->data + off, n);
    return n;
}

/* ---- BIGF (FILE_addbigsync) ---- */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t mount_bigf(GameHost *h, uint8_t *blob, uint32_t size)
{
    FileLayer *fl = &h->files;
    uint32_t count, o, i, m;
    Mount *mo = NULL;

    if (size < 16u || memcmp(blob, "BIGF", 4) != 0)
        return 0;
    count = be32(blob + 8);
    if (count > MOUNT_ENTRIES_MAX)
        return 0;
    for (m = 0; m < MOUNTS_MAX; m++)
        if (fl->mounts[m].handle == 0u) {
            mo = &fl->mounts[m];
            break;
        }
    if (mo == NULL)
        return 0;

    mo->ent = (MountEntry *)malloc(sizeof(MountEntry) * (count > 0u ? count : 1u));
    if (mo->ent == NULL)
        return 0;
    o = 16u;
    mo->n = 0u;
    for (i = 0; i < count; i++) {
        uint32_t off, sz, len = 0;
        if (o + 8u > size) {
            free(mo->ent);
            mo->ent = NULL;
            return 0;
        }
        off = be32(blob + o);
        sz = be32(blob + o + 4u);
        o += 8u;
        while (o + len < size && blob[o + len] != 0u)
            len++;
        if (o + len >= size || off > size || sz > size - off) {
            free(mo->ent);
            mo->ent = NULL;
            return 0;
        }
        if (len >= FILE_NAME_MAX)
            len = FILE_NAME_MAX - 1u;
        memcpy(mo->ent[mo->n].name, blob + o, len);
        mo->ent[mo->n].name[len] = '\0';
        mo->ent[mo->n].off = off;
        mo->ent[mo->n].size = sz;
        mo->n++;
        o += len + 1u;         /* ya pasado el terminador del nombre */
    }
    mo->blob = blob;
    mo->blob_size = size;
    mo->handle = fl->next_handle++;
    return mo->handle;
}

/* ---- despacho ---- */

int host_file_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    FileLayer *fl = &h->files;
    uint32_t a0 = c->r[4], a1 = c->r[5], a2 = c->r[6], a3 = c->r[7];
    char name[256];
    uint8_t *data;
    uint32_t size = 0, handle;

    switch (addr) {
    /* ---- sincronas ---- */
    case 0x800EB204u:                       /* FILE_opensync(nombre, modo, prio, &handle) */
        guest_cstr(c, a0, name, sizeof(name));
        data = lookup(h, name, &size);
        if (data != NULL) {
            handle = add_file(c, fl, data, size);
            ps1_w32(c, a3, handle);
            fl->opens++;
            if (fl->log)
                fprintf(stderr, "file: abre %s (%u bytes)\n", name, (unsigned)size);
            *ret = 1;
        } else {
            ps1_w32(c, a3, 0);
            fl->opens_failed++;
            snprintf(fl->last_missing, sizeof(fl->last_missing), "%s", name);
            if (fl->log)
                fprintf(stderr, "file: NO ESTA %s\n", name);
            *ret = 0;
        }
        return 1;
    case 0x800EB27Cu: {                     /* FILE_readsync(handle, off, addr, size) */
        uint32_t n = read_into(c, find_file(fl, a0), a1, a2, a3);
        fl->reads++;
        fl->bytes_read += n;
        *ret = n;
        return 1;
    }
    case 0x800EB2ACu:                       /* FILE_closesync(handle) */
        drop_file(fl, a0);
        *ret = 0;
        return 1;
    case 0x800EB300u: {                     /* FILE_sizesync(handle) */
        OpenFile *o = find_file(fl, a0);
        *ret = (o != NULL) ? o->size : 0u;
        return 1;
    }
    case 0x800EB354u:                       /* FILE_addbigsync(nombre, modo, prio, &handle) */
        guest_cstr(c, a0, name, sizeof(name));
        data = lookup(h, name, &size);
        handle = (data != NULL) ? mount_bigf(h, data, size) : 0u;
        if (handle == 0u) {
            free(data);
            fl->mounts_failed++;
            if (fl->log)
                fprintf(stderr, "file: no pude montar %s\n", name);
            ps1_w32(c, a3, 0);
            *ret = 0;
        } else {
            fl->mounts_ok++;
            if (fl->log)
                fprintf(stderr, "file: monta %s\n", name);
            ps1_w32(c, a3, handle);
            *ret = 1;
        }
        return 1;
    case 0x800EB3CCu: {                     /* FILE_delbigsync(handle, prio) */
        uint32_t m;
        for (m = 0; m < MOUNTS_MAX; m++)
            if (fl->mounts[m].handle == a0) {
                free(fl->mounts[m].blob);
                free(fl->mounts[m].ent);
                memset(&fl->mounts[m], 0, sizeof(fl->mounts[m]));
            }
        *ret = 0;
        return 1;
    }

    /* ---- asincronas: terminan en el acto ---- */
    case 0x800ECCC8u:                       /* FILE_open(nombre, modo, prio, user) -> op */
        guest_cstr(c, a0, name, sizeof(name));
        data = lookup(h, name, &size);
        if (data != NULL) {
            handle = add_file(c, fl, data, size);
            fl->opens++;
            if (fl->log)
                fprintf(stderr, "file: abre %s (%u bytes, async)\n", name, (unsigned)size);
        } else {
            handle = 0u;
            fl->opens_failed++;
            snprintf(fl->last_missing, sizeof(fl->last_missing), "%s", name);
            if (fl->log)
                fprintf(stderr, "file: NO ESTA %s\n", name);
        }
        *ret = new_op(c, fl, handle, a3);
        return 1;
    case 0x800ECE48u: {                     /* FILE_read(handle, off, addr, size, prio, user) */
        /* el sexto argumento va en la pila, en sp+20 */
        uint32_t user = ps1_r32(c, c->r[29] + 20u);
        uint32_t n = read_into(c, find_file(fl, a0), a1, a2, a3);
        fl->reads++;
        fl->bytes_read += n;
        *ret = new_op(c, fl, n, user);
        return 1;
    }
    case 0x800ECF2Cu: {                     /* FILE_size(handle, prio, user) */
        OpenFile *o = find_file(fl, a0);
        *ret = new_op(c, fl, (o != NULL) ? o->size : 0u, a2);
        return 1;
    }
    case 0x800ECD88u:                       /* FILE_close(handle, prio, user) */
        drop_file(fl, a0);
        *ret = new_op(c, fl, 0u, a2);
        return 1;
    case 0x800EC720u:                       /* FILE_opstatus: terminada */
    case 0x800ECB18u:                       /* FILE_waitop: terminada */
        *ret = (find_op(fl, a0) != NULL) ? 1u : 0u;
        return 1;
    case 0x800EC778u:                       /* FILE_operror */
        *ret = 0;
        return 1;
    case 0x800EC828u:                       /* FILE_priorityop */
        *ret = 0;
        return 1;
    case 0x800ECC0Cu: {                     /* FILE_completeop(op) -> resultado */
        FileOp *o = find_op(fl, a0);
        *ret = 0;
        if (o != NULL) {
            *ret = o->result;
            o->op = 0u;
        }
        return 1;
    }
    case 0x800EC964u: {                     /* FILE_cancelop(op) */
        FileOp *o = find_op(fl, a0);
        if (o != NULL)
            o->op = 0u;
        *ret = 0;
        return 1;
    }
    case 0x800EC7A8u: {                     /* FILE_callbackop(op, callback) */
        /* la operacion ya esta hecha, asi que la devolucion corre ahora mismo, con
         * (op, estado, user). Va por la cola de diferidas para que el host este
         * disponible cuando corra. */
        FileOp *o = find_op(fl, a0);
        if (o != NULL) {
            uint32_t args[3];
            args[0] = a0;
            args[1] = 1u;
            args[2] = o->user;
            ps1_defer(c, a1, args, 3u);
        }
        *ret = 0;
        return 1;
    }
    default:
        (void)a2;
        return 0;
    }
}
