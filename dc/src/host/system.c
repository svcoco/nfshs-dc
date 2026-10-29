/* El BIOS y las librerias de sistema, reemplazados: timers y VSync, devoluciones,
 * eventos, mandos y CD.
 *
 * Portado de src/ps1/host/system.rs de la referencia. Las direcciones son las del
 * NFS4.EXE de retail (NTSC-U, SLUS-00826) y todas estan en tools/hle_funcs.txt, que
 * es la lista que el recompilador excluye: aqui no hay nada que caiga despues al
 * codigo recompilado.
 */
#include "addr.h"
#include "host.h"

/* El reloj avanza en unidades de 1/1920 s. Cada frontera de HOST_TICK encola una
 * interrupcion del root counter de 128 Hz (la que ps1_service_interrupts entrega), y
 * cada frontera de HOST_FIELD, la devolucion de VSync del campo de video. */
uint64_t host_real_clock(GameHost *h)
{
    uint64_t ahora = h->now_us();

    if (h->real_us0 == 0u) {
        h->real_us0 = ahora;
        h->real_clock0 = h->clock;
    }
    return h->real_clock0 + (ahora - h->real_us0) * 1920u / 1000000u;
}

void host_advance_to(GameHost *h, Cpu *c, uint64_t t)
{
    uint64_t ticks_before = h->clock / HOST_TICK;
    uint64_t ticks_after = t / HOST_TICK;
    uint64_t fields;

    if (t <= h->clock)
        return;
    /* Con reloj real, el reloj del juego NUNCA adelanta al real: las esperas del juego
     * (VSync, los bucles de espera activa, un frame mas rapido que el campo) duran lo que en
     * la PS1. Si no, el driver de sonido cree que su stream va por delante de lo que el AICA
     * esta tocando y rellena encima de lo que aun no ha sonado (en la PS1 rellena el trozo
     * recien tocado, ~23 ms por detras de la voz; medido en la referencia, M4.md). */
    if (h->real_clock && h->now_us != NULL)
        while (host_real_clock(h) < t)
            ;

    c->pending_ticks += (uint32_t)(ticks_after - ticks_before);

    /* La devolucion de VSync (el Force_Vbl del juego, los motores del DualShock) corre
     * como la interrupcion que es: en cuanto esta llamada al host termine. Se limita a
     * 4 por salto para no acumular una avalancha si el reloj da un brinco. */
    if (h->cb_vsync != 0) {
        fields = t / HOST_FIELD - h->clock / HOST_FIELD;
        if (fields > 4u)
            fields = 4u;
        while (fields-- > 0u)
            ps1_defer(c, h->cb_vsync, NULL, 0);
    }

    /* Los campos solo se cuentan. La entrada y el volcado van en host_frame_top, que
     * es el punto del juego donde lo hace la referencia. */
    h->fields += t / HOST_FIELD - h->clock / HOST_FIELD;
    h->clock = t;
}


/* Tabla hash de direccion abierta: la direccion del paquete, mezclada, da el primer hueco y
 * se sigue hasta encontrarlo o un hueco libre. Nunca se borra, asi que no hay lapidas. */
DrawEnvRec *host_draw_env(GameHost *h, uint32_t pkt, int nueva)
{
    uint32_t i = ((pkt >> 2) * 2654435761u) & (DRAW_ENVS_MAX - 1u), k;

    for (k = 0; k < DRAW_ENVS_MAX; k++, i = (i + 1u) & (DRAW_ENVS_MAX - 1u)) {
        DrawEnvRec *e = &h->draw_envs[i];

        if (e->pkt == pkt)
            return e;
        if (e->pkt == 0u) {
            /* deja siempre un hueco, para que una busqueda sin exito termine */
            if (!nueva || h->draw_envs_n + 1u >= DRAW_ENVS_MAX)
                return NULL;
            e->pkt = pkt;
            h->draw_envs_n++;
            return e;
        }
    }
    return NULL;
}

int host_system_call(GameHost *h, Cpu *c, uint32_t addr, uint32_t *ret)
{
    uint32_t a0 = c->r[4], a1 = c->r[5], a2 = c->r[6], a3 = c->r[7];

    switch (addr) {
    /* ---- libetc, timers y devoluciones ---- */
    case 0x800F2C78u:                       /* VSync(mode) */
        /* mode 0 espera al campo siguiente; cualquier otro solo lee el contador */
        if (a0 == 0u)
            host_advance_to(h, c, (h->clock / HOST_FIELD + 1u) * HOST_FIELD);
        *ret = (uint32_t)(h->clock / HOST_FIELD);
        return 1;
    case 0x800F31A8u:                       /* ResetCallback */
        *ret = 0;
        return 1;
    case 0x800F31D8u: {                     /* InterruptCallback(linea, manejador) */
        *ret = 0;
        if (a0 < 11u) {
            *ret = c->irq_callbacks[a0];
            c->irq_callbacks[a0] = a1;
        }
        if (h->idlelog)
            fprintf(stderr, "InterruptCallback(linea=%#x, manejador=%#x) -> %#x "
                        "(llamada %llu, ra=%#x)\n", (unsigned)a0, (unsigned)a1,
                (unsigned)*ret, (unsigned long long)c->calls, (unsigned)c->r[31]);
        return 1;
    }
    case 0x800F3208u: {                     /* DMACallback(canal, manejador) */
        *ret = 0;
        if (a0 < 7u) {
            *ret = c->dma_callbacks[a0];
            c->dma_callbacks[a0] = a1;
        }
        return 1;
    }
    case 0x800F3238u:                       /* VSyncCallback(manejador) */
        *ret = h->cb_vsync;
        h->cb_vsync = a0;
        return 1;
    case 0x800EA7CCu:                       /* SetRCnt */
    case 0x800EA8A0u:                       /* StartRCnt */
        *ret = 1;
        return 1;
    case 0x800EA868u:                       /* GetRCnt */
        /* el juego lo usa para medir, no para sincronizar: avanza siempre */
        h->rcnt += 64u;
        *ret = h->rcnt & 0xFFFFu;
        return 1;
    case 0x80107000u:                       /* ChangeClearPAD */
    case 0x80107010u:                       /* ChangeClearRCnt */
        *ret = 0;
        return 1;
    case 0x800F4D30u:                       /* FlushCache */
        *ret = 0;
        return 1;
    case 0x80107318u:                       /* ExitCriticalSection */
    case 0x80107338u:                       /* EnterCriticalSection */
        *ret = 1;
        return 1;

    /* ---- mandos ---- */
    case 0x800FE6D4u:                       /* PadInitDirect(buf1, buf2) */
        h->pad_buffers[0] = a0;
        h->pad_buffers[1] = a1;
        /* 0xFF = sin mando conectado, hasta que el host escriba uno */
        if (a0 != 0u)
            ps1_w8(c, a0, 0xFFu);
        if (a1 != 0u)
            ps1_w8(c, a1, 0xFFu);
        *ret = 1;
        return 1;
    case 0x800F07BCu:                       /* PadStartCom */
        *ret = 1;
        return 1;
    case 0x800F07FCu:                       /* PadGetState: PadStateStable */
        *ret = 6;
        return 1;
    case 0x800F08BCu:                       /* PadInfoMode: mando digital (ID 4) */
        *ret = (a1 == 1u) ? 4u : 0u;
        return 1;
    case 0x800F0A88u:                       /* PadSetActAlign */
        *ret = 1;
        return 1;
    case 0x800F0B08u: {                     /* PadSetAct(puerto, datos, longitud) */
        uint32_t port = (a0 >> 4) & 1u;
        h->actuators[port] = (a2 >= 2u) ? a1 : 0u;
        *ret = 1;
        return 1;
    }

    /* ---- eventos del BIOS ---- */
    case 0x801077CCu: {                     /* OpenEvent(desc, spec, modo, manejador) */
        /* 0xF2000002 = interrupcion de RCNT2, que es el timer de 128 Hz del juego */
        if (a0 == 0xF2000002u && a3 != 0u) {
            uint32_t i;
            int dup = 0;
            for (i = 0; i < c->timer_handler_n; i++)
                if (c->timer_handlers[i] == a3)
                    dup = 1;
            if (!dup && c->timer_handler_n < 16u)
                c->timer_handlers[c->timer_handler_n++] = a3;
        }
        *ret = h->next_event++;
        return 1;
    }
    case 0x801077BCu:                       /* EnableEvent */
    case 0x801077DCu:                       /* DisableEvent */
    case 0x8010D4C0u:                       /* CloseEvent */
    case 0x8010D354u:                       /* DeliverEvent */
    case 0x8010D4D0u:                       /* TestEvent: todo entregado */
        *ret = 1;
        return 1;

    /* ---- CD: todos los datos del juego pasan por la capa FILE_*, el streaming no ---- */
    case 0x80109CC8u:                       /* CdInit */
        *ret = 1;
        return 1;
    case 0x800F81A8u:                       /* CdSync: completo */
    case 0x80109924u:                       /* CdReadSync: completo */
        *ret = 0;
        return 1;
    case 0x800F80DCu:                       /* CdStatus: motor encendido, en reposo */
        *ret = 0x02u;
        return 1;
    case 0x800F81C8u:                       /* CdReady */
        *ret = 0;
        return 1;
    case 0x800FAD44u:                       /* CD_Init */
    case 0x800FAE58u:                       /* CD_Restart */
        *ret = 1;
        return 1;

    /* ---- streaming, MDEC y memory card: no disponibles ----
     * Exactamente lo que devuelve system.rs de la referencia. Las de memory card que
     * ella delega a su propio modulo (MemCardEnd, MemCardExist, MemCardAccept,
     * MemCardReadFile, MemCardWriteFile) se dejan sin implementar a proposito: si el
     * juego las pide, el informe lo dira, en vez de inventarles un comportamiento. */
    case 0x800F9954u:                       /* StSetStream */
    case 0x800F92C4u:                       /* StClearRing */
    case 0x800F9824u:                       /* StUnSetRing */
    case 0x800FA354u:                       /* StSetRing */
    case 0x800F9324u:                       /* DecDCTReset */
    case 0x800F9358u:                       /* DecDCTin */
    case 0x800F93F4u:                       /* DecDCTinSync */
    /* la memory card la atiende memcard.c, que se consulta antes */

    default:
        return 0;
    }
}

/* Hooks: el codigo original SI corre despues. */
void host_hook(GameHost *h, Cpu *c, uint32_t addr)
{
    if (addr == 0x800E77F0u) {
        if (c->r[31] == RA_MAIN_LOOP_YIELD) {
            /* tope del bucle principal de una carrera: es una frontera de frame */
            host_frame_top(h, c);
        } else {
            /* systemtask dentro de un bucle de espera: dejar pasar el tiempo hasta el
             * siguiente tick del timer es lo que saca al juego de la espera activa. */
            host_advance_to(h, c, (h->clock / HOST_TICK + 1u) * HOST_TICK);
        }
        return;
    }
    if (addr == 0x800EED6Cu) {
        /* SetDrawEnv(paquete, entorno): se apunta el recorte y el desplazamiento de cada
         * paquete, para recortar las vistas de HUD al grabarlas (overlay_record_ot). Uno
         * a pantalla completa, o el de la primera vista, es el buffer donde estan. */
        uint32_t pkt = c->r[4], env = c->r[5], k;
        int16_t clip[4], ofs[2];
        int primera = 0;

        for (k = 0; k < 4u; k++)
            clip[k] = (int16_t)ps1_r16(c, env + 2u * k);
        ofs[0] = (int16_t)ps1_r16(c, env + 8u);
        ofs[1] = (int16_t)ps1_r16(c, env + 10u);
        {
            DrawEnvRec *e = host_draw_env(h, pkt, 1);

            if (e != NULL) {
                e->seq = ++h->draw_envs_seq;
                for (k = 0; k < 4u; k++)
                    e->clip[k] = clip[k];
                e->ofs[0] = ofs[0];
                e->ofs[1] = ofs[1];
            } else {
                h->draw_envs_lost++;
            }
        }
        for (k = 0; k < 2u; k++) {
            int16_t v0 = (int16_t)ps1_r16(c, ADDR_DRAW_VIEWS + 8u + 92u * k);
            int16_t v1 = (int16_t)ps1_r16(c, ADDR_DRAW_VIEWS + 8u + 92u * k + 2u);

            if (v0 == clip[0] && v1 == clip[1])
                primera = 1;
        }
        primera = primera && clip[2] >= 256;
        if ((clip[2] >= 256 && clip[3] >= 200) || primera) {
            h->draw_origin[0] = clip[0];
            h->draw_origin[1] = clip[1];
        }
        return;
    }
    if (addr == 0x800EEC70u && h->mode == HOST_FRONTEND) {
        /* SetDrawArea(DR_AREA, rect): el front end recorta parte de lo que dibuja (el
         * texto que se desplaza). En terminos de VRAM el rectangulo cae dentro de uno
         * de los dos buffers de 256 lineas, asi que se le quita esa mitad. */
        uint32_t rect = c->r[5];
        int16_t x = (int16_t)ps1_r16(c, rect);
        int16_t y = (int16_t)ps1_r16(c, rect + 2u);
        int16_t oy = (y >= 256) ? 256 : 0;

        overlay_clip(h, x, (int16_t)(y - oy), (int16_t)ps1_r16(c, rect + 4u),
                     (int16_t)ps1_r16(c, rect + 6u));
    }
}
