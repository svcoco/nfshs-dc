# Compila el C generado a objetos. Lo invoca `make genobj`; se separa para poder
# incluir gen/parts.mk, que lo escribe el propio recompilador.
include $(GEN)/parts.mk
OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(GEN_SRCS))

all: $(OBJS)
	@echo "$(words $(OBJS)) objetos en $(OBJDIR)"

# las calientes, con sus flags (HOTFLAGS va despues y gana: en Dreamcast, -O2 sobre -Os)
$(OBJDIR)/hot_%.o: $(GEN)/hot_%.c
	@$(CC) $(CFLAGS) $(HOTFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(GEN)/%.c
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@
