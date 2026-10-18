"""Lee un --textlog y dice, por pantalla, que opciones hay y cual esta resaltada.

Existe para no navegar el front end a ciegas. El guion de entrada del test diferencial va
en numeros de frame, y adivinar pulsaciones cuesta una corrida de minuto por intento. Con
esto el juego lo dice el mismo: el host ya atiende Font_TextXY, asi que tenemos el texto en
pantalla con sus coordenadas.

El truco util: **la opcion resaltada se dibuja en una x distinta** de las demas. El front
end pinta cada entrada dos veces (sombra y texto), todas alineadas en la misma x, menos la
seleccionada, que va desplazada. Asi que la x que no es la moda identifica la seleccion, y
el orden de navegacion es el orden de la y.

    uso: dc/build/nfshs-dc --script S --ticks N --textlog 2> log
         python -I dc/test/diff/screens.py log [--frame N]
"""
import collections
import re
import sys

LINEA = re.compile(r'TEXTO f(\d+) \((-?\d+),(-?\d+)\): (.*)')


def leer(path):
    """{frame: [(x, y, texto)]}"""
    por_frame = collections.defaultdict(list)
    with open(path, encoding='utf-8', errors='replace') as fh:
        for l in fh:
            m = LINEA.match(l)
            if m:
                f, x, y, t = m.groups()
                por_frame[int(f)].append((int(x), int(y), t))
    return por_frame


def pantalla(entradas):
    """(titulo, [(y, texto, resaltada)]) de un frame."""
    # la y mas alta es el titulo, la mas baja la linea de ayuda
    ys = sorted({y for _, y, _ in entradas})
    if not ys:
        return None, []
    titulo = next((t for x, y, t in entradas if y == ys[0]), '')
    cuerpo = [e for e in entradas if ys[0] < e[1] < ys[-1]] or entradas
    # la x "normal" es la moda; la entrada cuya x se sale es la resaltada
    modal = collections.Counter(x for x, _, _ in cuerpo).most_common(1)[0][0]
    filas = {}
    for x, y, t in cuerpo:
        filas.setdefault(y, [t, True])
        if x == modal:
            filas[y][1] = False
    return titulo, [(y, filas[y][0], filas[y][1]) for y in sorted(filas)]


def main(argv):
    path = argv[0]
    por_frame = leer(path)
    if not por_frame:
        print('el registro no tiene lineas TEXTO (falta --textlog?)')
        return 1
    if '--frame' in argv:
        objetivo = [int(argv[argv.index('--frame') + 1])]
    else:
        # una muestra por pantalla: el ultimo frame de cada conjunto de textos distinto
        objetivo, previo, ultimo = [], None, None
        for f in sorted(por_frame):
            clave = tuple(sorted(t for _, _, t in por_frame[f]))
            if clave != previo:
                if ultimo is not None:
                    objetivo.append(ultimo)
                previo = clave
            ultimo = f
        objetivo.append(ultimo)

    for f in objetivo:
        titulo, filas = pantalla(por_frame[f])
        print(f'f{f}  [{titulo}]')
        for _, t, sel in filas:
            print(f'    {">>" if sel else "  "} {t}')
    return 0


sys.exit(main(sys.argv[1:]))
