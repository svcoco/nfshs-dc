/* M8: sin mensajes en los CDI. dbgio_disable() solo tira la salida: el printf ya ha formateado
 * el texto. Con el enlazador (--wrap, ver DCSILENCIO en el Makefile) printf y compania vuelven
 * sin hacer nada una vez que arranca el juego; hasta entonces siguen, para que un fallo de
 * arranque se vea en pantalla. Con make dc SERIE=1 nada de esto se enlaza. */
#include <stdarg.h>
#include <stdio.h>

#include "silencio.h"

#ifndef NFSHS_SERIE
static volatile int s_callado;

int __real_puts(const char *s);
int __real_putchar(int c);

void serie_calla(void)
{
    s_callado = 1;
}

int __wrap_printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    if (s_callado)
        return 0;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int __wrap_puts(const char *s)
{
    return s_callado ? 0 : __real_puts(s);
}

int __wrap_putchar(int c)
{
    return s_callado ? c : __real_putchar(c);
}
#endif
