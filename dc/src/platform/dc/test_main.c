/* Arranque de Dreamcast para las pruebas unitarias de ps1rt (M1b, paso 2).
 *
 * El runner (test/unit/run_tests.c) es portable y no sabe nada de KOS: se compila
 * con -Dmain=ps1rt_tests_main y esta envoltura lo llama.
 *
 * La salida va al framebuffer ("fb"), FORZADO, y no por la autodeteccion de KOS.
 * scif_detected() devuelve 1 siempre (scif.c: "We are always detected, though we
 * might end up realizing there's no cable connected later"), asi que si se deja
 * automatico gana el puerto serie y la PANTALLA DEL DREAMCAST SE QUEDA NEGRA.
 *
 * Eso no es que no haya salida -- en Flycast con Debug.SerialConsoleEnabled=yes
 * aparece en una ventana aparte, "Flycast Serial Output", y con dcload llega al PC --
 * pero es una salida que hay que ir a buscar, y en hardware sin cable serie ni
 * dcload no existe. Con "fb" el resultado se lee donde uno lo mira, siempre.
 *
 * El banner de arranque de KOS se escapa igualmente por el serie, porque dbgio ya
 * esta inicializado cuando llega este main.
 */
#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

int ps1rt_tests_main(void);

int main(int argc, char **argv)
{
    int rc;

    (void)argc;
    (void)argv;

    /* A pantalla, pase lo que pase. Ver el comentario de arriba. */
    dbgio_dev_select("fb");
    dbgio_enable();

    rc = ps1rt_tests_main();

    printf("\n[ps1rt en SH-4: codigo de salida %d]\n", rc);
    printf("[START para salir]\n");

    /* Sin esto el programa retorna y KOS reinicia, y en Flycast no da tiempo a leer. */
    for(;;) {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *st = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;

        if(st && (st->buttons & CONT_START))
            break;

        thd_sleep(50);
    }

    return rc;
}
