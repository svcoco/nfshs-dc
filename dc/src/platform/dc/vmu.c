/* M6: la memory card de PS1 en la VMU (decisiones de Jorge, docs/hitos/M6.md).
 *
 * El host lleva la tarjeta de cada ranura como la imagen de 128 KB de PS1 (memcard.c). Aqui
 * se guarda en la VMU **archivo por archivo**: cada archivo de la tarjeta es un archivo VMS
 *
 *   NFS4xxxxxxxx    (un hash del nombre de PS1: la VMU solo admite 12 caracteres)
 *
 * con la descripcion y el icono que el juego pone en la cabecera de su archivo (el titulo en
 * Shift-JIS de ancho completo pasado a ASCII, y el icono de 16x16 a 16 colores doblado al de
 * 32x32 de la VMU), y de datos una cabecera nuestra de 32 bytes ("PS1F", el nombre de PS1 y
 * los bloques) seguida de los bloques de 8 KB tal cual. Una partida (1 bloque) son 18 bloques
 * de la VMU.
 *
 * La ranura 1 es la VMU del puerto A y la 2 la del puerto B. Al arrancar, y cada vez que el
 * juego pregunta si hay tarjeta, se mira si la VMU se puso (y entonces se leen sus archivos
 * para montar la tarjeta) o se quito. Tambien se le dice al host cuantos bloques de PS1 caben
 * de verdad, segun el sitio libre de la VMU.
 *
 * Escribir en la VMU es lento (un archivo de 18 bloques tarda mas de un segundo) y el juego no
 * espera tanto: si MemCardSync contesta "todavia no" mientras se escribe, da la escritura por
 * fallida y no hace la segunda (medido en Flycast: solo llegaba la cabecera). Asi que el
 * juego recibe el resultado enseguida, como con la tarjeta de PS1 y como la referencia (la
 * tarjeta de la RAM ya tiene lo escrito, y la relectura de comprobacion sale de ella), y la
 * VMU se escribe detras, en un hilo; las escrituras seguidas del mismo archivo se juntan en
 * una. Si una falla, lo cuenta la siguiente escritura del juego (mc_async_error).
 */
#include <kos.h>
#include <dc/vmufs.h>
#include <dc/vmu_pkg.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "vmu.h"

#define BLOQUE_PS1   8192u
#define CAB          32u              /* nuestra cabecera de datos */
#define VMU_POR_PS1  18u              /* bloques de VMU de un archivo de 1 bloque de PS1 */
#define COLA         4u

typedef struct {
    int      op;                      /* 0 libre, 1 escribir, 2 borrar, 3 borrar todos */
    uint32_t ranura;
    char     fn[13];
    uint8_t *datos;                   /* CAB + bloques (escribir) */
    uint32_t len;
    char     corta[17], larga[33];
    uint16_t pal[16];
    uint8_t  icono[512];
} Trabajo;

static Trabajo         s_cola[COLA];
static uint32_t        s_cab, s_n;    /* cabeza y cuantos */
static mutex_t         s_mutex;
static semaphore_t     s_hay;
static volatile int    s_ocupado;     /* trabajos en cola o en curso */
static GameHost       *s_host;
static int             s_puesta[2];   /* la ranura tenia VMU la ultima vez que se miro */
static volatile uint32_t s_escritas, s_fallos, s_ultimo_ms;

static maple_device_t *vmu_de(uint32_t ranura)
{
    int u;

    for (u = 1; u <= 2; u++) {
        maple_device_t *d = maple_enum_dev((int)ranura, u);

        if (d != NULL && d->valid && (d->info.functions & MAPLE_FUNC_MEMCARD))
            return d;
    }
    return NULL;
}

static void nombre_vmu(const char *ps1, char *fn)
{
    uint32_t h = 2166136261u;

    while (*ps1 != '\0')
        h = (h ^ (uint8_t)*ps1++) * 16777619u;
    snprintf(fn, 13, "NFS4%08lX", (unsigned long)h);
}

/* El titulo de la cabecera de PS1 (Shift-JIS, casi siempre de ancho completo) en ASCII. */
static void titulo(const uint8_t *s, char *out, uint32_t max)
{
    uint32_t i = 0, n = 0;

    while (i < 64u && s[i] != 0u && n + 1u < max) {
        uint8_t a = s[i], b = (i + 1u < 64u) ? s[i + 1u] : 0u;
        char ch = '?';

        if (a < 0x80u) {
            ch = (char)a;
            i += 1u;
        } else {
            if (a == 0x82u && b >= 0x60u && b <= 0x79u) ch = (char)('A' + b - 0x60u);
            else if (a == 0x82u && b >= 0x81u && b <= 0x9Au) ch = (char)('a' + b - 0x81u);
            else if (a == 0x82u && b >= 0x4Fu && b <= 0x58u) ch = (char)('0' + b - 0x4Fu);
            else if (a == 0x81u && b == 0x40u) ch = ' ';
            else if (a == 0x81u && b == 0x46u) ch = ':';
            else if (a == 0x81u && b == 0x44u) ch = '.';
            else if (a == 0x81u && b == 0x43u) ch = ',';
            else if (a == 0x81u && (b == 0x7Cu || b == 0x5Du)) ch = '-';
            i += 2u;
        }
        out[n++] = ch;
    }
    out[n] = '\0';
}

/* El icono de PS1 (16x16, 4 bits, el pixel de la izquierda en el nibble bajo; paleta de 15
 * bits con el 0 transparente) doblado al de la VMU (32x32, 4 bits, la izquierda en el nibble
 * alto; paleta ARGB4444). */
static void icono(const uint8_t *cab, uint16_t *pal, uint8_t *out)
{
    uint32_t i, x, y;

    for (i = 0; i < 16u; i++) {
        uint16_t c = (uint16_t)(cab[0x60u + 2u * i] | cab[0x61u + 2u * i] << 8);
        uint16_t r = (uint16_t)((c & 31u) >> 1), g = (uint16_t)(((c >> 5) & 31u) >> 1),
                 b = (uint16_t)(((c >> 10) & 31u) >> 1);

        pal[i] = (uint16_t)((c == 0u ? 0u : 0xF000u) | r << 8 | g << 4 | b);
    }
    for (y = 0; y < 32u; y++)
        for (x = 0; x < 32u; x += 2u) {
            uint8_t v = cab[0x80u + (y / 2u) * 8u + (x / 2u) / 2u];
            uint8_t p = (uint8_t)(((x / 2u) & 1u) ? v >> 4 : v & 15u);

            out[y * 16u + x / 2u] = (uint8_t)(p << 4 | p);
        }
}

/* ---- el hilo que escribe ---- */

static int trabajo(Trabajo *t)
{
    maple_device_t *d = vmu_de(t->ranura);
    int ok = 0;

    if (d == NULL)
        return 0;
    if (t->op == 1) {
        vmu_pkg_t pkg;
        uint8_t *buf = NULL;
        int n = 0;

        memset(&pkg, 0, sizeof pkg);
        strncpy(pkg.desc_short, t->corta, sizeof pkg.desc_short - 1u);
        strncpy(pkg.desc_long, t->larga, sizeof pkg.desc_long - 1u);
        strncpy(pkg.app_id, "NFSHS-DC", sizeof pkg.app_id - 1u);
        pkg.icon_cnt = 1;
        pkg.icon_anim_speed = 0;
        pkg.eyecatch_type = VMUPKG_EC_NONE;
        memcpy(pkg.icon_pal, t->pal, sizeof pkg.icon_pal);
        pkg.icon_data = t->icono;
        pkg.data_len = (int)t->len;
        pkg.data = t->datos;
        if (vmu_pkg_build(&pkg, &buf, &n) == 0) {
            uint64_t t0 = timer_ms_gettime64();

            ok = vmufs_write(d, t->fn, buf, n, VMUFS_OVERWRITE) == 0;
            s_ultimo_ms = (uint32_t)(timer_ms_gettime64() - t0);
            s_escritas++;
            printf("VMU %c: %s escrito (%d bytes) en %lu ms%s\n", 'A' + (int)t->ranura, t->fn, n,
                   (unsigned long)(timer_ms_gettime64() - t0), ok ? "" : " FALLO");
            free(buf);
        }
    } else if (t->op == 2) {
        /* borrar algo que no esta tambien es haberlo borrado */
        vmufs_delete(d, t->fn);
        ok = 1;
    } else {
        vmu_dir_t *dir = NULL;
        int n = 0, i;

        ok = 1;
        if (vmufs_readdir(d, &dir, &n) == 0) {
            for (i = 0; i < n; i++) {
                char fn[13];

                memcpy(fn, dir[i].filename, 12);
                fn[12] = '\0';
                if (strncmp(fn, "NFS4", 4) == 0 && vmufs_delete(d, fn) != 0)
                    ok = 0;
            }
            free(dir);
        }
    }
    return ok;
}

static void *escritor(void *arg)
{
    (void)arg;
    for (;;) {
        Trabajo t;

        sem_wait(&s_hay);
        mutex_lock(&s_mutex);
        t = s_cola[s_cab];
        s_cola[s_cab].op = 0;
        s_cola[s_cab].datos = NULL;
        s_cab = (s_cab + 1u) % COLA;
        s_n--;
        mutex_unlock(&s_mutex);

        if (!trabajo(&t)) {
            s_host->mc_async_error = 1;
            s_fallos++;
        }
        free(t.datos);
        mutex_lock(&s_mutex);
        s_ocupado--;
        mutex_unlock(&s_mutex);
    }
    return NULL;
}

static void encola(const Trabajo *t)
{
    for (;;) {
        uint32_t i;

        mutex_lock(&s_mutex);
        /* el mismo archivo ya esperando: se queda con lo ultimo */
        for (i = 0; i < s_n; i++) {
            Trabajo *q = &s_cola[(s_cab + i) % COLA];

            if (q->ranura == t->ranura && q->op != 3 && t->op != 3 && strcmp(q->fn, t->fn) == 0) {
                free(q->datos);
                *q = *t;
                mutex_unlock(&s_mutex);
                return;
            }
        }
        if (s_n < COLA) {
            s_cola[(s_cab + s_n) % COLA] = *t;
            s_n++;
            s_ocupado++;
            mutex_unlock(&s_mutex);
            sem_signal(&s_hay);
            return;
        }
        mutex_unlock(&s_mutex);
        thd_pass();                   /* cola llena: espera a que el hilo saque uno */
    }
}

/* ---- lo que llama el host ---- */

static int vmu_guarda(GameHost *h, uint32_t ranura, const char *name)
{
    Trabajo t;
    uint32_t n;

    memset(&t, 0, sizeof t);
    t.ranura = ranura;
    if (name == NULL) {
        t.op = 3;                     /* formatear: fuera todos nuestros archivos */
        encola(&t);
        return 1;
    }
    nombre_vmu(name, t.fn);
    t.datos = malloc(CAB + 15u * BLOQUE_PS1);
    if (t.datos == NULL)
        return 0;
    n = memcard_get(h->mc_card[ranura], name, t.datos + CAB, 15u);
    if (n == 0u) {
        free(t.datos);
        t.datos = NULL;
        t.op = 2;                     /* ya no esta: se borro */
        encola(&t);
        return 1;
    }
    memset(t.datos, 0, CAB);
    memcpy(t.datos, "PS1F", 4);
    strncpy((char *)t.datos + 4, name, 20);
    t.datos[24] = (uint8_t)n;
    t.len = CAB + n * BLOQUE_PS1;
    /* la cabecera de PS1 del archivo: magia "SC", titulo e icono */
    if (t.datos[CAB] == 'S' && t.datos[CAB + 1u] == 'C') {
        titulo(t.datos + CAB + 4u, t.larga, sizeof t.larga);
        titulo(t.datos + CAB + 4u, t.corta, sizeof t.corta);
        icono(t.datos + CAB, t.pal, t.icono);
    } else {
        strcpy(t.corta, "NFS HIGH STAKES");
        strcpy(t.larga, name);
    }
    t.op = 1;
    encola(&t);
    return 1;
}

/* Monta la tarjeta de la ranura con los archivos NFS4 de su VMU. */
static void vmu_carga(GameHost *h, uint32_t ranura, maple_device_t *d)
{
    vmu_dir_t *dir = NULL;
    int n = 0, i, libres;
    uint32_t usados = 0;

    memcard_format(h->mc_card[ranura]);
    if (vmufs_readdir(d, &dir, &n) == 0) {
        for (i = 0; i < n; i++) {
            char fn[13];
            void *buf = NULL;
            int len = 0;
            vmu_pkg_t pkg;

            memcpy(fn, dir[i].filename, 12);
            fn[12] = '\0';
            if (strncmp(fn, "NFS4", 4) != 0 || vmufs_read(d, fn, &buf, &len) != 0)
                continue;
            if (vmu_pkg_parse(buf, (size_t)len, &pkg) == 0 && pkg.data_len >= (int)CAB &&
                memcmp(pkg.data, "PS1F", 4) == 0) {
                char nombre[21];
                uint32_t bl = pkg.data[24];

                memcpy(nombre, pkg.data + 4, 20);
                nombre[20] = '\0';
                if (bl > 0u && (uint32_t)pkg.data_len >= CAB + bl * BLOQUE_PS1 &&
                    memcard_put(h->mc_card[ranura], nombre, bl, pkg.data + CAB))
                    usados += bl;
                else
                    printf("VMU %c: %s no cabe en la tarjeta\n", 'A' + (int)ranura, fn);
            }
            free(buf);
        }
        free(dir);
    }
    libres = vmufs_free_blocks(d);
    h->mc_limit[ranura] = usados + (libres > 0 ? (uint32_t)libres / VMU_POR_PS1 : 0u);
    if (h->mc_limit[ranura] > 15u)
        h->mc_limit[ranura] = 15u;
    h->mc_present[ranura] = 1;
    printf("VMU %c: %lu bloques de PS1 usados, caben %lu\n", 'A' + (int)ranura,
           (unsigned long)usados, (unsigned long)h->mc_limit[ranura]);
}

static void vmu_mira(GameHost *h, uint32_t ranura)
{
    maple_device_t *d = vmu_de(ranura);

    if (d == NULL) {
        h->mc_present[ranura] = 0;
        s_puesta[ranura] = 0;
        return;
    }
    /* recien puesta: se monta; mientras se escribe en ella, no se relee */
    if (!s_puesta[ranura] && s_ocupado == 0) {
        vmu_carga(h, ranura, d);
        s_puesta[ranura] = 1;
    }
}

void tarjeta_vmu_cifras(uint32_t *escritas, uint32_t *fallos, uint32_t *ultimo_ms)
{
    *escritas = s_escritas;
    *fallos = s_fallos;
    *ultimo_ms = s_ultimo_ms;
}

void tarjeta_vmu_init(GameHost *h)
{
    uint32_t r;

    s_host = h;
    mutex_init(&s_mutex, MUTEX_TYPE_NORMAL);
    sem_init(&s_hay, 0);
    thd_create(true, escritor, NULL);
    h->mc_two_slots = 1;
    h->mc_save = vmu_guarda;
    h->mc_probe = vmu_mira;
    for (r = 0; r < 2u; r++) {
        h->mc_present[r] = 0;
        vmu_mira(h, r);
    }
}
