# Compila el C generado a objetos. Lo invoca `make genobj`; se separa para poder
# incluir gen/parts.mk, que lo escribe el propio recompilador.
include $(GEN)/parts.mk
OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(GEN_SRCS))

all: $(OBJS)
	@echo "$(words $(OBJS)) objetos en $(OBJDIR)"

$(OBJDIR)/%.o: $(GEN)/%.c
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@
