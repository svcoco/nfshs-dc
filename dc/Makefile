# Port de NFS High Stakes a Dreamcast. Ejecutar desde dc/.
#
# Toolchain: MSYS2 + mingw-w64 (gcc 16.2, UCRT64) y GNU make 4.4.1.
# Los targets de analisis, generacion, compilacion x86 y test diferencial funcionan; los
# de Dreamcast esperan a KallistiOS (M1b) y los de assets a M2. `make help` lo detalla.

PYTHON ?= python
ROOT   := ..
GEN    := gen
BUILD  := build

# gcc de MSYS2 (UCRT64). Desde un shell que no sea el de MSYS2:
#   export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
CC       := gcc
CFLAGS   ?= -std=c11 -O2 -fwrapv -fno-strict-aliasing -Wall -Wextra
CPPFLAGS += -Isrc/ps1rt -I$(GEN)
# el codigo generado dispara estos avisos por construccion, no son problemas reales
GENFLAGS := -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable             -Wno-unused-value -Wno-unused-parameter

# La tabla HLE horneada desde tools/hle_funcs.txt (ver tools/recomp/bake_hle.py). Es
# codigo generado, asi que vive en gen/ y no se versiona, igual que las partes.
HLE_TABLE  := $(GEN)/hle_table.c

PS1RT_SRCS := $(wildcard src/ps1rt/*.c)
HOST_SRCS  := $(wildcard src/host/*.c)
PC_SRCS    := $(wildcard src/platform/pc/*.c)
CPPFLAGS   += -Isrc/host

# gcc de mingw escribe sus temporales en TMPDIR / TMP / TEMP. Si el shell no los
# trae (pasa al invocar make desde Git Bash), termina intentando C:\WINDOWS y falla
# por permisos. Se fijan dentro de build/, que ya esta en .gitignore.
TMPDIR := $(CURDIR)/$(BUILD)/tmp
export TMPDIR
export TMP  := $(TMPDIR)
export TEMP := $(TMPDIR)

.PHONY: help m0 metrics gen gen-rust parity test genobj gen-hle pc dc dc-check dc-gen dc-test dc-size bake disc diff clean

help:
	@echo "Analisis y generacion (Python, funcionan ya):"
	@echo "  make m0        cifras de viabilidad de M0; reproduce la seccion 6 de CLAUDE.md"
	@echo "  make metrics   dimensionado del backend C (tablas de salto, leaders, stubs)"
	@echo "  make gen       recompila NFS4.EXE+FRONT.BIN a C en $(GEN)/"
	@echo "  make gen-rust  regenera la referencia Rust en ../crates/nfs4rc/src"
	@echo "  make parity    compara la salida C con la Rust, funcion por funcion"
	@echo "  make test      pruebas unitarias de ps1rt"
	@echo "  make genobj    compila dc/gen a objetos (prueba de carga del generador)"
	@echo ""
	@echo "Compilacion y pruebas (funcionan):"
	@echo "  make pc        host x86 para el test diferencial               [M1a]"
	@echo "  make diff      test diferencial C contra la referencia Rust    [M1a]"
	@echo "                 SCRIPT=... elige el escenario (relativo a la raiz del repo)"
	@echo ""
	@echo "Pendientes:"
	@echo "  make dc        1ST_READ.BIN para Dreamcast (KOS, sh-elf-gcc)   [M1b]"
	@echo "  make bake      assets del disco a formato DC                   [M2]"
	@echo "  make disc      IP.BIN + GDI/CDI                                [M2]"

m0:
	$(PYTHON) -I tools/recomp/m0_stats.py

metrics:
	$(PYTHON) -I tools/recomp/codegen_metrics.py

gen: $(HLE_TABLE)
	$(PYTHON) -I tools/recomp/recomp.py $(GEN) --lang c --all

# Hornea tools/hle_funcs.txt a C. En PC el host puede leer el .txt, pero en Dreamcast
# no tiene sentido llevarlo en el disco: son 119 direcciones que no cambian.
gen-hle: $(HLE_TABLE)

$(HLE_TABLE): tools/recomp/bake_hle.py $(ROOT)/tools/hle_funcs.txt
	@mkdir -p $(GEN)
	$(PYTHON) -I tools/recomp/bake_hle.py $(GEN)

gen-rust:
	$(PYTHON) -I tools/recomp/recomp.py $(ROOT)/crates/nfs4rc/src --lang rust --all

parity:
	$(PYTHON) -I tools/recomp/check_parity.py

test: $(BUILD)/run_tests
	@$(BUILD)/run_tests

$(BUILD)/run_tests: test/unit/run_tests.c $(PS1RT_SRCS)
	@mkdir -p $(BUILD) $(TMPDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

# Prueba de carga: compila todo el C generado a objetos (no enlaza; para eso hace
# falta el host, que es el paso 3 de M1a).
genobj: $(GEN)/parts.mk
	@mkdir -p $(BUILD)/gen $(TMPDIR)
	@$(MAKE) --no-print-directory -f tools/genobj.mk 		CC="$(CC)" CFLAGS="$(CFLAGS) $(GENFLAGS)" CPPFLAGS="$(CPPFLAGS)" 		GEN="$(GEN)" OBJDIR="$(BUILD)/gen"

pc: $(BUILD)/nfshs-dc
	@echo "listo: $(BUILD)/nfshs-dc"

# Enlaza el C generado con ps1rt, el host y el arnes de x86.
$(BUILD)/nfshs-dc: genobj $(HLE_TABLE) $(PS1RT_SRCS) $(HOST_SRCS) $(PC_SRCS)
	@mkdir -p $(BUILD) $(TMPDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(PS1RT_SRCS) $(HOST_SRCS) $(PC_SRCS) $(HLE_TABLE) $(BUILD)/gen/*.o

# ---------------------------------------------------------------- Dreamcast (M1b)
#
# Hace falta el entorno de KallistiOS en el shell:
#     source /opt/toolchains/dc/kos/environ.sh
# `kos-cc` envuelve sh-elf-gcc con los flags y las librerias de KOS (-ml -m4-single
# incluidos), asi que no hay que repetirlos aqui.
KOS_BASE  ?= /opt/toolchains/dc/kos
KOS_CC_W  := $(KOS_BASE)/utils/build_wrappers/kos-cc
DCBUILD   := $(BUILD)/dc
# -Os y no -O2 a proposito: medido en SH-4, -Os deja el codigo generado en 3,43 MB
# frente a 7,85 MB, y con 8 KB de I-cache eso puede ser ademas mas rapido. La
# eleccion definitiva se decide midiendo ms/frame (docs/hitos/M1b.md, seccion 1).
DCFLAGS   := -std=c11 -Os -fwrapv -fno-strict-aliasing -Wall -Wextra
DCTEST_OBJS := $(DCBUILD)/run_tests.o $(DCBUILD)/test_main.o                $(patsubst src/ps1rt/%.c,$(DCBUILD)/%.o,$(PS1RT_SRCS))

dc-check:
	@test -x "$(KOS_CC_W)" || { 	  echo "No encuentro kos-cc en $(KOS_CC_W)."; 	  echo "Instala el SDK (docs/instalar-sdk.md) o pon KOS_BASE=<ruta>."; exit 1; }

# Pruebas unitarias de ps1rt corriendo en SH-4. Produce un ELF que Flycast arranca
# directamente; el resultado sale por dcload/serie si los hay, y si no a pantalla.
dc-test: $(DCBUILD)/run_tests.elf
	@echo "listo: dc/$< -- arrancalo con Flycast o mandalo al hardware"

$(DCBUILD)/%.o: src/ps1rt/%.c
	@mkdir -p $(DCBUILD) $(TMPDIR)
	$(KOS_CC_W) $(DCFLAGS) -Isrc/ps1rt -MMD -MP -c $< -o $@

-include $(DCTEST_OBJS:.o=.d)

# El runner es portable y define main(); se renombra para que el main de verdad sea
# el de la envoltura de KOS.
$(DCBUILD)/run_tests.o: test/unit/run_tests.c
	@mkdir -p $(DCBUILD) $(TMPDIR)
	$(KOS_CC_W) $(DCFLAGS) -Isrc/ps1rt -Dmain=ps1rt_tests_main -c $< -o $@

$(DCBUILD)/test_main.o: src/platform/dc/test_main.c
	@mkdir -p $(DCBUILD) $(TMPDIR)
	$(KOS_CC_W) $(DCFLAGS) -Isrc/ps1rt -c $< -o $@

$(DCBUILD)/run_tests.elf: dc-check $(DCTEST_OBJS)
	$(KOS_CC_W) $(DCTEST_OBJS) -o $@
	@sh-elf-size $@

# Tamano del codigo generado compilado para SH-4. Es la medicion del paso 1 de M1b.
dc-size: dc-check $(GEN)/parts.mk
	@mkdir -p $(DCBUILD)/gen $(TMPDIR)
	@$(MAKE) --no-print-directory -f tools/genobj.mk 	    CC="$(KOS_BASE)/../sh-elf/bin/sh-elf-gcc -ml -m4-single" 	    CFLAGS="$(DCFLAGS) $(GENFLAGS)" CPPFLAGS="$(CPPFLAGS)" 	    GEN="$(GEN)" OBJDIR="$(DCBUILD)/gen"
	@sh-elf-size -t $(DCBUILD)/gen/*.o | tail -1

# El juego en Dreamcast. PS1_DIAG=0 deja fuera el volcado y los guiones del test
# diferencial, que escriben archivos y en un GD-ROM no tienen sentido.
DC_HOST_SRCS := $(filter-out src/host/dump.c,$(HOST_SRCS))
# test_main.c es el arranque del ELF de PRUEBAS (dc-test), no el del juego.
DC_PLAT_SRCS := $(filter-out src/platform/dc/test_main.c,$(wildcard src/platform/dc/*.c))
DC_SRCS      := $(PS1RT_SRCS) $(DC_HOST_SRCS) $(HLE_TABLE) $(DC_PLAT_SRCS)
DC_OBJS      := $(patsubst %.c,$(DCBUILD)/o/%.o,$(DC_SRCS))

dc: $(DCBUILD)/nfshs.elf
	@echo "listo: dc/$<"

# -MMD -MP genera las dependencias de cabeceras al lado del objeto. Sin esto, cambiar
# un struct en host.h recompilaba solo el .c tocado y el resto se quedaba con el layout
# viejo: los modulos veian campos distintos del MISMO GameHost y el host se comportaba
# de forma incoherente sin dar ningun error. Costo una tarde de diagnostico.
$(DCBUILD)/o/%.o: %.c
	@mkdir -p $(dir $@) $(TMPDIR)
	$(KOS_CC_W) $(DCFLAGS) $(CPPFLAGS) -DPS1_DIAG=0 -MMD -MP -c $< -o $@

-include $(DC_OBJS:.o=.d)

# El C generado no se recompila aqui: lo hace dc-gen, que son 71 unidades y tarda.
dc-gen: dc-check $(GEN)/parts.mk
	@mkdir -p $(DCBUILD)/gen $(TMPDIR)
	@$(MAKE) --no-print-directory -f tools/genobj.mk 	    CC="$(KOS_BASE)/../sh-elf/bin/sh-elf-gcc -ml -m4-single" 	    CFLAGS="$(DCFLAGS) $(GENFLAGS)" CPPFLAGS="$(CPPFLAGS)" 	    GEN="$(GEN)" OBJDIR="$(DCBUILD)/gen"

$(DCBUILD)/nfshs.elf: dc-check dc-gen $(DC_OBJS)
	$(KOS_CC_W) $(DC_OBJS) $(DCBUILD)/gen/*.o -o $@
	@sh-elf-size $@

bake:
	@echo "Pendiente: M2." && exit 1

disc:
	@echo "Pendiente: M2." && exit 1

# Test diferencial contra la referencia Rust. Necesita cargo en el PATH y la referencia
# construida (cargo build --release en la raiz). SCRIPT elige el escenario.
# OJO: run.py pasa la ruta tal cual a los dos hijos, que corren desde la raiz del repo,
# asi que SCRIPT va relativo a la raiz y no a dc/.
SCRIPT ?= dc/test/diff/scripts/front_end_120.txt

diff: pc
	$(PYTHON) -I test/diff/run.py --script $(SCRIPT)

clean:
	rm -rf $(BUILD)
