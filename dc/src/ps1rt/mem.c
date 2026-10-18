/* Rutas lentas de memoria: scratchpad, registros de hardware y el bus que pone el
 * host. Portado de Mem de crates/ps1rt/src/lib.rs.
 *
 * La ruta rapida (RAM plana) esta como static inline en ps1rt.h. Aqui solo cae lo
 * que no es RAM, y a proposito sin inline: mantenerlo fuera de linea es lo que
 * protege la I-cache de 8 KB del SH-4.
 */
#include "ps1rt.h"

#include <string.h>

/* Tope de accesos a IO antes de dar por hecho que el juego esta encuestando
 * hardware que nadie va a mover. Igual que en la referencia. */
#define PS1_IO_LIMIT 20000000u

void ps1_init(Cpu *c, uint8_t *ram_buf)
{
    memset(c, 0, sizeof(*c));
    c->ram = ram_buf;
    memset(c->ram, 0, PS1_RAM_SIZE + PS1_RAM_PAD);
    c->tick_limit = UINT64_MAX;
}

/* Un registro de hardware del que se encarga el IoBus del host. */
static int on_bus(const Cpu *c, uint32_t a)
{
    uint32_t p = a & 0x1FFFFFFFu;
    return c->bus != NULL && p >= 0x1F801000u && p < 0x1F803000u;
}

static uint32_t bus_read(Cpu *c, uint32_t a, uint32_t n)
{
    c->io_accesses++;
    c->last_io = a & 0x1FFFFFFFu;
    return c->bus->read(c, c->bus->self, a & 0x1FFFFFFFu, n);
}

static void bus_write(Cpu *c, uint32_t a, uint32_t v, uint32_t n)
{
    c->io_accesses++;
    c->last_io = a & 0x1FFFFFFFu;
    c->bus->write(c, c->bus->self, a & 0x1FFFFFFFu, v, n);
}

/* Equivalente de Mem::slot(): devuelve el puntero de n bytes al que toca escribir
 * o del que toca leer. `io` es el buffer de usar y tirar para los registros de
 * hardware, que se pone a cero en cada acceso (por eso una escritura nunca se ve
 * en una lectura posterior).
 */
static uint8_t *slot(Cpu *c, uint32_t addr, uint32_t n, uint8_t io[4])
{
    uint32_t a = addr & 0x1FFFFFFFu;
    if (a >= PS1_SCRATCH_BASE && a < PS1_SCRATCH_BASE + PS1_SCRATCH_SIZE)
        return c->scratch + (a - PS1_SCRATCH_BASE);
    if (a >= PS1_IO_BASE) {
        c->io_accesses++;
        c->last_io = a;
        if (PS1_UNLIKELY(c->io_accesses > PS1_IO_LIMIT))
            ps1_panic(c, "encuesta de hardware sin fin");
        io[0] = io[1] = io[2] = io[3] = 0;
        /* Lo unico que el modelo sin IoBus devuelve distinto de 0: el volumen de
         * envolvente de las voces del SPU, que el driver de sonido lee para saber
         * cuando ha terminado una voz. */
        if (a >= 0x1F801C00u && a < 0x1F801D80u && (a & 0xFu) == 0xCu) {
            uint16_t v = c->spu_volume[(a - 0x1F801C00u) >> 4];
            io[0] = (uint8_t)v;
            io[1] = (uint8_t)(v >> 8);
        }
        return io;
    }
    (void)n;
    return c->ram + (a & PS1_RAM_MASK);
}

uint32_t ps1_r8_slow(Cpu *c, uint32_t a)
{
    uint8_t io[4];
    if (on_bus(c, a))
        return (uint8_t)bus_read(c, a, 1);
    return slot(c, a, 1, io)[0];
}

uint32_t ps1_r16_slow(Cpu *c, uint32_t a)
{
    uint8_t io[4];
    const uint8_t *p;
    if (on_bus(c, a))
        return (uint16_t)bus_read(c, a, 2);
    p = slot(c, a, 2, io);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

uint32_t ps1_r32_slow(Cpu *c, uint32_t a)
{
    uint8_t io[4];
    const uint8_t *p;
    if (on_bus(c, a))
        return bus_read(c, a, 4);
    p = slot(c, a, 4, io);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ps1_w8_slow(Cpu *c, uint32_t a, uint32_t v)
{
    uint8_t io[4];
    if (on_bus(c, a)) {
        bus_write(c, a, v & 0xFFu, 1);
        return;
    }
    slot(c, a, 1, io)[0] = (uint8_t)v;
}

void ps1_w16_slow(Cpu *c, uint32_t a, uint32_t v)
{
    uint8_t io[4];
    uint8_t *p;
    if (on_bus(c, a)) {
        bus_write(c, a, v & 0xFFFFu, 2);
        return;
    }
    p = slot(c, a, 2, io);
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void ps1_w32_slow(Cpu *c, uint32_t a, uint32_t v)
{
    uint8_t io[4];
    uint8_t *p;
    if (on_bus(c, a)) {
        bus_write(c, a, v, 4);
        return;
    }
    p = slot(c, a, 4, io);
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Byte a byte, igual que la referencia: asi el scratchpad, la RAM y el IO se
 * comportan igual que en accesos sueltos y los contadores cuadran. */
void ps1_read_bytes(Cpu *c, uint32_t a, uint8_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        out[i] = (uint8_t)ps1_r8(c, a + i);
}

void ps1_write_bytes(Cpu *c, uint32_t a, const uint8_t *data, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ps1_w8(c, a + i, data[i]);
}

/* Copia la imagen de texto/datos de un PS-X EXE a la RAM, en su direccion de carga. */
void ps1_load_exe(Cpu *c, const uint8_t *exe, size_t len)
{
    uint32_t t_addr, t_size;
    size_t avail;
    if (len < 0x800u)
        ps1_panic(c, "PS-X EXE truncado");
    t_addr = (uint32_t)exe[0x18] | ((uint32_t)exe[0x19] << 8) |
             ((uint32_t)exe[0x1A] << 16) | ((uint32_t)exe[0x1B] << 24);
    t_size = (uint32_t)exe[0x1C] | ((uint32_t)exe[0x1D] << 8) |
             ((uint32_t)exe[0x1E] << 16) | ((uint32_t)exe[0x1F] << 24);
    avail = len - 0x800u;
    if ((size_t)t_size < avail)
        avail = t_size;
    ps1_write_bytes(c, t_addr, exe + 0x800, (uint32_t)avail);
}
