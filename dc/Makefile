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

.PHONY: help m0 metrics gen gen-rust parity test genobj pc dc bake disc diff clean

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

gen:
	$(PYTHON) -I tools/recomp/recomp.py $(GEN) --lang c --all

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
$(BUILD)/nfshs-dc: genobj $(PS1RT_SRCS) $(HOST_SRCS) $(PC_SRCS)
	@mkdir -p $(BUILD) $(TMPDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(PS1RT_SRCS) $(HOST_SRCS) $(PC_SRCS) $(BUILD)/gen/*.o

dc:
	@echo "Pendiente: necesita KallistiOS con sh-elf-gcc (-ml -m4-single), que todavia"
	@echo "no esta instalado en esta maquina. El plan, el presupuesto de tamano medido y"
	@echo "la auditoria del codigo portable estan en docs/hitos/M1b.md."
	@exit 1

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
