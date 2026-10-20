/* Arranque de Dreamcast para las pruebas unitarias de ps1rt (M1b, paso 2).
 *
 * El runner (test/unit/run_tests.c) es portable y no sabe nada de KOS: se compila
 * con -Dmain=ps1rt_tests_main y esta envoltura lo llama.
 *
 * La salida va al framebuffer ("fb"), que es el unico dispositivo que existe
 * siempre: sin dcload ni cable serie el resultado se lee en pantalla, en Flycast
 * o en hardware. Si hay dcload o serie, dbgio_dev_select_auto() los habria cogido
 * antes, asi que se intentan primero y "fb" queda de respaldo.
 */
#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

int ps1rt_tests_main(void);

int main(int argc, char **argv)
{
    int rc;

    (void)argc;
    (void)argv;

    /* Si no hay dcload ni serie, a pantalla. */
    if(dbgio_dev_get() == NULL)
        dbgio_dev_select("fb");

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
