# Configuracao do programa
TARGET       = banner
INSTALL_PATH = usr/bin

## Configuracao de diretorios
ROOT_DIR  = ../..
TOOLCHAIN = $(ROOT_DIR)/output/host/bin
OVERLAY   = $(ROOT_DIR)/overlay

## Configuracao de ferramentas
TRIPLET = $(TOOLCHAIN)/i686-buildroot-linux-gnu
CC      = $(TRIPLET)-gcc

default: $(OVERLAY)/$(INSTALL_PATH)/$(TARGET)

$(OVERLAY)/$(INSTALL_PATH)/$(TARGET): $(OVERLAY)/$(INSTALL_PATH) $(TARGET).c
	@$(CC) $(TARGET).c -o $@ -O2

$(OVERLAY)/$(INSTALL_PATH):
	@mkdir -p $@

clean:
	@rm -f $(OVERLAY)/$(INSTALL_PATH)/$(TARGET)
