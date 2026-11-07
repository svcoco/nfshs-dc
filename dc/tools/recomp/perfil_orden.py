"""M8: el orden de las funciones en .text, a partir de un perfil (make dc-perfil SERIE=1).

La cache de instrucciones del SH-4 tiene 8 KB y correspondencia directa: dos funciones a 8 KB justos
de distancia se echan una a otra. Aqui se agrupa lo que corre junto: por seccion del frame y, dentro
de cada una, lo mas caliente primero. Lo que se reparte entre secciones va en su propio grupo.

    perfil_orden.py VOLCADO.txt NM_S.txt ORDEN.ld

VOLCADO: lineas "PF <addr> <seccion> <muestras>" del muestreador.
NM_S: salida de `sh-elf-nm -n -S` del mismo ELF.
ORDEN.ld: para `ld --section-ordering-file`. Lleva nombres, no direcciones, y sirve entre builds;
un patron sin coincidencias no molesta al enlazador.
"""
import re
import sys

SECCIONES = ['juego', 'vista', 'grabar objetos', 'grabar destellos', 'cielo, vista y HUD', 'grabar coches',
             'frontera', 'texturas PS1', 'espera al PVR', 'dibujo: cielo', 'dibujo: pista', 'dibujo: objetos',
             'dibujo: coches', 'dibujo: translucido', 'dibujo: HUD', 'dibujo: resto', 'espera al reloj',
             'otros hilos']
DOMINANTE = 0.7
# Lo que se llama entre si dentro de un mismo bucle, seguido y en este orden: contiguo no se pisa en la
# cache mientras quepa en 8 KB. El perfil no ve quien llama a quien; esto sale de leer el codigo.
CUMULOS = [
    ('dibujo', ['car_draw', 'sal_tri', 'car_paint', 'pista_lote', 'triangulo', 'borde_guarda', 'mezcla',
                'pista_emite', 'malla', 'mv_calcula.isra.0', 'color_de', 'p2_argb']),
    ('runtime', ['ps1_call', 'host_call', 'host_hook', 'ps1_dispatch', 'ps1_r32_slow', 'ps1_w32_slow',
                 'ps1_r32_x', 'ps1_w32_x', 'ps1_gte', 'ps1_gte_write', 'ps1_gte_command', 'rtp.isra.0',
                 'gmatrix']),
]


def carga_nm(path):
    por_addr, tam = {}, {}
    for line in open(path):
        p = line.split()
        if len(p) == 4 and p[2] in 'Tt':
            addr, size, name = int(p[0], 16), int(p[1], 16), p[3][1:] if p[3].startswith('_') else p[3]
            por_addr.setdefault(addr, []).append(name)
            tam[name] = size
    return por_addr, tam


def main():
    volcado, nm, salida = sys.argv[1:4]
    por_addr, tam = carga_nm(nm)
    texto = open(volcado, errors='replace').read()
    if 'PF-FIN' not in texto:
        sys.exit('el volcado esta incompleto: falta PF-FIN')
    muestras = {}
    # la consola de Windows rellena con espacios y a veces deja basura detras
    for m in re.finditer(r'^PF ([0-9a-f]{8}) (\d+) (\d+)\s', texto, re.M):
        addr, sec, n = int(m[1], 16), int(m[2]), int(m[3])
        muestras.setdefault(addr, {})
        muestras[addr][sec] = muestras[addr].get(sec, 0) + n

    total = sum(sum(v.values()) for v in muestras.values())
    grupos, sin_nombre = {}, 0
    for addr, secs in muestras.items():
        nombres = por_addr.get(addr)
        if not nombres:
            sin_nombre += sum(secs.values())
            continue
        # f_X_at es la funcion de verdad; f_X, su entrada
        reales = [x for x in nombres if x.endswith('_at')] or nombres
        cuenta = sum(secs.values())
        sec, mejor = max(secs.items(), key=lambda kv: kv[1])
        grupo = sec if mejor >= DOMINANTE * cuenta else -1
        grupos.setdefault(grupo, []).append((cuenta, reales[0]))

    lineas = ['.text : {', '    *_kos_startup.o(.text .text.*)']
    resumen, puestos = [], set()
    for nombre, fs in CUMULOS:
        kb = sum(tam.get(f, 0) for f in fs) / 1024
        resumen.append(f'cumulo {nombre:15} {len(fs):5} funciones {kb:8.1f} KB')
        lineas.append(f'    /* cumulo: {nombre} */')
        for f in fs:
            puestos.add(f)
            lineas.append(f'    *(.text.{f})')
    for g in [-1] + list(range(len(SECCIONES))):
        fs = sorted(grupos.get(g, []), key=lambda t: (-t[0], t[1]))
        if not fs:
            continue
        nombre = 'varias secciones' if g < 0 else SECCIONES[g]
        kb = sum(tam.get(f, 0) for _, f in fs) / 1024
        c = sum(n for n, _ in fs)
        resumen.append(f'{nombre:22} {len(fs):5} funciones {kb:8.1f} KB {100 * c / total:5.1f} %')
        lineas.append(f'    /* {nombre} */')
        for _, f in fs:
            if f in puestos:
                continue
            puestos.add(f)
            lineas.append(f'    *(.text.{f})')
            if f.endswith('_at'):
                lineas.append(f'    *(.text.{f[:-3]})')
    lineas.append('}')
    with open(salida, 'w', newline='\n') as fh:
        fh.write('/* Generado por tools/recomp/perfil_orden.py: no editar a mano. */\n')
        fh.write('\n'.join(lineas) + '\n')
    print(f'{total} muestras, {sin_nombre} sin nombre, {len(puestos)} funciones')
    print('\n'.join(resumen))
    # cuanto codigo hace falta para cubrir cada parte del tiempo
    todas = sorted(((n, f) for fs in grupos.values() for n, f in fs), reverse=True)
    acum = kb = 0
    marcas = [.5, .75, .9, .99]
    for n, f in todas:
        acum += n
        kb += tam.get(f, 0) / 1024
        while marcas and acum >= marcas[0] * total:
            print(f'el {marcas.pop(0) * 100:.0f} % del tiempo cabe en {kb:.1f} KB')


main()
