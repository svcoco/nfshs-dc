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
    for (i = 0; i < FILES_MAX; i++) {
        if (fl->open[i].fh != NULL)
            fclose(fl->open[i].fh);
        free(fl->open[i].data);
    }
    for (i = 0; i < MOUNTS_MAX; i++) {
        if (fl->mounts[i].fh != NULL)
            fclose(fl->mounts[i].fh);
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

/* Lee n bytes del montaje desde el offset off. Si el BIGF esta abierto como archivo
 * (lo normal), hace seek y lee solo eso; si vino de un blob en memoria, copia. Acota
 * siempre contra el tamano real del archivo. */
static int mount_read(const Mount *mo, uint32_t off, void *dst, uint32_t n)
{
    if (off > mo->size || n > mo->size - off)
        return 0;
    if (mo->fh != NULL) {
        if (fseek(mo->fh, (long)off, SEEK_SET) != 0)
            return 0;
        return fread(dst, 1u, (size_t)n, mo->fh) == (size_t)n;
    }
    if (mo->blob == NULL)
        return 0;
    memcpy(dst, mo->blob + off, n);
    return 1;
}

/* Lee el archivo del disco extraido, o lo saca de un BIGF montado. Devuelve el buffer
 * (que el llamante libera) o NULL. */
/* Abre el archivo del disco extraido (gamedata en PC, /cd o /rd en Dreamcast) y da su
 * tamano, SIN leerlo. Lo usan tanto lookup() como el montaje de BIGF, que no quiere el
 * contenido. Devuelve 0 si no esta. */
static int disk_open(GameHost *h, const char *name, FILE **out, uint32_t *size)
{
    char norm[256], path[1024];
    FILE *f;
    long n;

    normalize(name, norm, sizeof(norm));
    snprintf(path, sizeof(path), "%s/%s", h->gamedata, norm);
    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
        fclose(f);
        return 0;
    }
    rewind(f);
    *out = f;
    *size = (uint32_t)n;
    return 1;
}

static uint8_t *lookup(GameHost *h, const char *name, uint32_t *size)
{
    FileLayer *fl = &h->files;
    char norm[256];
    FILE *f;
    uint8_t *buf;
    uint32_t n, m, k;

    /* Si esta en el disco se lee entero: son los sueltos (NFS4.EXE, FRONT.BIN...) y
     * son pequenos. Los BIGF no pasan por aqui, los monta FILE_addbigsync. Si esta
     * pero no se deja leer, se devuelve fallo sin mirar los montajes, como siempre. */
    if (disk_open(h, name, &f, &n)) {
        buf = (uint8_t *)malloc(n > 0u ? (size_t)n : 1u);
        if (buf != NULL && (n == 0u || fread(buf, 1, (size_t)n, f) == (size_t)n)) {
            fclose(f);
            *size = n;
            return buf;
        }
        free(buf);
        fclose(f);
        return NULL;
    }

    normalize(name, norm, sizeof(norm));

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
                    /* a demanda: solo estos bytes, no los 28 MB del archivo */
                    if (!mount_read(mo, mo->ent[k].off, buf, sz)) {
                        free(buf);
                        return NULL;
                    }
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

/* Localiza un archivo del juego y lo deja listo para leerse A DEMANDA, sin traer su
   contenido. Primero suelto en el disco; si no, dentro de los BIGF montados, del mas
   reciente al mas antiguo y por el nombre base, sin distinguir mayusculas (el mismo
   orden que hacia lookup()). Devuelve 0 si no esta. */
static int open_src(GameHost *h, const char *name, OpenFile *o)
{
    FileLayer *fl = &h->files;
    char norm[256];
    FILE *f;
    uint32_t n, m, k;

    memset(o, 0, sizeof(*o));
    if (disk_open(h, name, &f, &n)) {
        o->fh = f;
        o->base = 0u;
        o->size = n;
        return 1;
    }

    normalize(name, norm, sizeof(norm));
    {
        const char *base = basename_of(norm);
        for (m = MOUNTS_MAX; m > 0u; m--) {
            Mount *mo = &fl->mounts[m - 1u];
            if (mo->handle == 0u)
                continue;
            for (k = 0; k < mo->n; k++)
                if (eq_nocase(mo->ent[k].name, base)) {
                    o->mo = mo;
                    o->base = mo->ent[k].off;
                    o->size = mo->ent[k].size;
                    return 1;
                }
        }
    }
    return 0;
}

static uint32_t add_file(Cpu *c, FileLayer *fl, const OpenFile *src)
{
    uint32_t i;
    for (i = 0; i < FILES_MAX; i++)
        if (fl->open[i].handle == 0u) {
            fl->open[i] = *src;
            fl->open[i].handle = fl->next_handle++;
            return fl->open[i].handle;
        }
    if (src->fh != NULL)
        fclose(src->fh);
    free(src->data);
    ps1_panic(c, "demasiados archivos abiertos a la vez");
}

static void drop_file(FileLayer *fl, uint32_t handle)
{
    OpenFile *o = find_file(fl, handle);
    if (o != NULL) {
        if (o->fh != NULL)
            fclose(o->fh);
        free(o->data);
        memset(o, 0, sizeof(*o));
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

/* Copia un trozo del archivo a la RAM del invitado y devuelve cuantos bytes fueron.
   Lee del disco por tramos a traves de un buffer de rebote: asi no hace falta tener el
   archivo entero en memoria ni reservar nada por lectura. 16 KB cubre de una pasada la
   lectura tipica del juego (medido: 3,87 MB en 266 lecturas, ~14 KB de media). */
static uint32_t read_into(Cpu *c, OpenFile *o, uint32_t off, uint32_t addr, uint32_t want)
{
    static uint8_t bounce[16384];
    uint32_t n, done = 0;

    if (o == NULL)
        return 0;
    if (off > o->size)
        off = o->size;
    n = o->size - off;
    if (want < n)
        n = want;

    if (o->data != NULL) {
        ps1_write_bytes(c, addr, o->data + off, n);
        return n;
    }

    while (done < n) {
        uint32_t chunk = n - done;
        if (chunk > sizeof(bounce))
            chunk = (uint32_t)sizeof(bounce);
        if (o->mo != NULL) {
            if (!mount_read(o->mo, o->base + off + done, bounce, chunk))
                break;
        } else {
            if (fseek(o->fh, (long)(o->base + off + done), SEEK_SET) != 0)
                break;
            if (fread(bounce, 1u, (size_t)chunk, o->fh) != (size_t)chunk)
                break;
        }
        ps1_write_bytes(c, addr + done, bounce, chunk);
        done += chunk;
    }
    return done;
}

/* ---- BIGF (FILE_addbigsync) ---- */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Monta un BIGF. Recibe el archivo ABIERTO (fh) o, en el caso raro de que viniera de
 * dentro de otro montaje, su blob. Solo deja residente el directorio.
 *
 * Formato (todo big-endian): "BIGF", +4 tamano del archivo, +8 numero de entradas,
 * +12 tamano del directorio. Luego, por entrada: offset, tamano y el nombre terminado
 * en cero. Comprobado en ZZZFE.VIV: 435 entradas, directorio de 8559 bytes y la
 * primera entrada en 0x2170, justo despues.
 *
 * En caso de fallo deja el hueco limpio; cerrar fh o liberar blob es del llamante. */
static uint32_t mount_bigf(GameHost *h, FILE *fh, uint8_t *blob, uint32_t size)
{
    FileLayer *fl = &h->files;
    uint8_t head[16], *dir = NULL;
    uint32_t count, dir_n, o, i, m;
    Mount *mo = NULL;

    for (m = 0; m < MOUNTS_MAX; m++)
        if (fl->mounts[m].handle == 0u) {
            mo = &fl->mounts[m];
            break;
        }
    if (mo == NULL)
        return 0;
    memset(mo, 0, sizeof(*mo));
    mo->fh = fh;
    mo->blob = blob;
    mo->size = size;

    if (size < 16u || !mount_read(mo, 0u, head, 16u) || memcmp(head, "BIGF", 4) != 0)
        goto fail;
    count = be32(head + 8);
    dir_n = be32(head + 12);
    if (count > MOUNT_ENTRIES_MAX)
        goto fail;
    /* El campo +12 viene del archivo, asi que no se da por bueno: si no cuadra, se lee
     * un trozo acotado del principio y la pasada de abajo se encarga de validar. */
    if (dir_n < 16u || dir_n > size)
        dir_n = (size < 0x100000u) ? size : 0x100000u;

    dir = (uint8_t *)malloc(dir_n);
    if (dir == NULL || !mount_read(mo, 0u, dir, dir_n))
        goto fail;

    mo->ent = (MountEntry *)malloc(sizeof(MountEntry) * (count > 0u ? count : 1u));
    if (mo->ent == NULL)
        goto fail;

    o = 16u;
    mo->n = 0u;
    for (i = 0; i < count; i++) {
        uint32_t off, sz, len = 0;
        if (o + 8u > dir_n)
            goto fail;
        off = be32(dir + o);
        sz = be32(dir + o + 4u);
        o += 8u;
        while (o + len < dir_n && dir[o + len] != 0u)
            len++;
        /* el nombre tiene que caber en el directorio y la entrada dentro del archivo */
        if (o + len >= dir_n || off > size || sz > size - off)
            goto fail;
        if (len >= FILE_NAME_MAX)
            len = FILE_NAME_MAX - 1u;
        memcpy(mo->ent[mo->n].name, dir + o, len);
        mo->ent[mo->n].name[len] = '\0';
        mo->ent[mo->n].off = off;
        mo->ent[mo->n].size = sz;
        mo->n++;
        o += len + 1u;         /* ya pasado el terminador del nombre */
    }

    free(dir);
    mo->handle = fl->next_handle++;
    return mo->handle;

fail:
    free(dir);
    free(mo->ent);
    memset(mo, 0, sizeof(*mo));
    return 0;
}

/* ---- despacho ---- */

int host_file_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    FileLayer *fl = &h->files;
    uint32_t a0 = c->r[4], a1 = c->r[5], a2 = c->r[6], a3 = c->r[7];
    char name[256];
    uint8_t *data;
    OpenFile src;
    uint32_t size = 0, handle;

    switch (addr) {
    /* ---- sincronas ---- */
    case 0x800EB204u:                       /* FILE_opensync(nombre, modo, prio, &handle) */
        guest_cstr(c, a0, name, sizeof(name));
        if (open_src(h, name, &src)) {
            size = src.size;
            handle = add_file(c, fl, &src);
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
        /* El BIGF NO se lee entero: se abre, se monta su directorio y se deja abierto
         * (ver Mount en files.h). Solo si no esta como archivo suelto se recurre al
         * camino antiguo, que es un BIGF dentro de otro montaje. */
        {
            FILE *bf = NULL;
            uint32_t bsz = 0;
            data = NULL;
            if (disk_open(h, name, &bf, &bsz)) {
                handle = mount_bigf(h, bf, NULL, bsz);
                if (handle == 0u)
                    fclose(bf);
            } else {
                data = lookup(h, name, &size);
                handle = (data != NULL) ? mount_bigf(h, NULL, data, size) : 0u;
            }
        }
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
                if (fl->mounts[m].fh != NULL)
                    fclose(fl->mounts[m].fh);
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
        if (open_src(h, name, &src)) {
            size = src.size;
            handle = add_file(c, fl, &src);
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
