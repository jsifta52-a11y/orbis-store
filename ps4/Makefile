# Orbis Store – PS4 homebrew Makefile
#
# Targets the OpenOrbis PS4 Toolchain.
# https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain
#
# Usage:
#   make          – build the PKG
#   make clean    – remove build artefacts
#
# Prerequisites:
#   - $OO_PS4_TOOLCHAIN set to the OpenOrbis toolchain root
#   - clang, llvm-ar, python3 on PATH

TARGET   := OrbisStore
VERSION  := 01.00
TITLE_ID := ORBS00001

SRCS := \
    source/main.c      \
    source/http.c      \
    source/db.c        \
    source/ui.c        \
    source/store.c     \
    source/library.c   \
    source/redeem.c    \
    source/installer.c

# ── Toolchain ────────────────────────────────────────────────────────────────

OO_TOOLCHAIN ?= $(OO_PS4_TOOLCHAIN)

CC      := clang
LD      := clang
AR      := llvm-ar
STRIP   := llvm-strip

CFLAGS  := \
    --target=x86_64-pc-freebsd12-elf \
    -fPIC \
    -funwind-tables \
    -fvisibility=hidden \
    -Wall \
    -Wextra \
    -D__PS4__ \
    -D_BSD_SOURCE \
    -isystem $(OO_TOOLCHAIN)/include \
    -isystem $(OO_TOOLCHAIN)/include/c++/v1

LDFLAGS := \
    --target=x86_64-pc-freebsd12-elf \
    -fPIC \
    -m elf_x86_64 \
    -pie \
    --script $(OO_TOOLCHAIN)/link.x \
    --eh-frame-hdr \
    -L$(OO_TOOLCHAIN)/lib \
    -lcurl \
    -lSQLite \
    -lpthread \
    -lc \
    -lkernel \
    -lSceUserService \
    -lScePad \
    -lSceSystemService \
    -lSceVideoOut

# ── Build ─────────────────────────────────────────────────────────────────────

OBJS    := $(SRCS:.c=.o)
ELF     := $(TARGET).elf
OELF    := $(TARGET).oelf

PKG_DIR := pkg_root
PKG_OUT := $(TARGET).pkg

.PHONY: all clean

all: $(PKG_OUT)

# Compile
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link ELF
$(ELF): $(OBJS)
	$(LD) $(OBJS) $(LDFLAGS) -o $@

# Generate open-ELF and PKG
$(OELF): $(ELF)
	python3 $(OO_TOOLCHAIN)/scripts/create-fself.py $< $@

$(PKG_OUT): $(OELF)
	mkdir -p $(PKG_DIR)/sce_sys
	cp $(OO_TOOLCHAIN)/assets/sce_sys/param.sfo.template $(PKG_DIR)/sce_sys/param.sfo
	python3 $(OO_TOOLCHAIN)/scripts/patch_param_sfo.py \
		$(PKG_DIR)/sce_sys/param.sfo \
		$(TITLE_ID) "$(TARGET)" "$(VERSION)"
	cp $(OELF) $(PKG_DIR)/eboot.bin
	python3 $(OO_TOOLCHAIN)/scripts/create-pkg.py \
		--title-id $(TITLE_ID) \
		$(PKG_DIR) $@
	@echo "Built: $@"

clean:
	rm -f $(OBJS) $(ELF) $(OELF) $(PKG_OUT)
	rm -rf $(PKG_DIR)
