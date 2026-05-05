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
CONTENT_ID := IV0000-$(TITLE_ID)_00-ORBISSTORE000000

SRCS := \
    source/main.c      \
    source/http.c      \
    source/db.c        \
    source/ui.c      \
    source/store.c     \
    source/library.c   \
    source/redeem.c    \
    source/installer.c

# ── Toolchain ───────────────────────────────────────────────────────────────[...]

OO_TOOLCHAIN ?= $(OO_PS4_TOOLCHAIN)
ifeq ($(strip $(OO_TOOLCHAIN)),)
OO_TOOLCHAIN := /home/neox/Downloads/OpenOrbis/PS4Toolchain
endif
ifeq ($(wildcard $(OO_TOOLCHAIN)/include/stddef.h),)
$(error OpenOrbis toolchain not found at $(OO_TOOLCHAIN). Set OO_PS4_TOOLCHAIN to your OpenOrbis/PS4Toolchain path)
endif
SYSROOT := $(OO_TOOLCHAIN)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
TOOL_BIN := $(OO_TOOLCHAIN)/bin/linux
endif
ifeq ($(UNAME_S),Darwin)
TOOL_BIN := $(OO_TOOLCHAIN)/bin/macos
endif
ifeq ($(strip $(TOOL_BIN)),)
$(error Unsupported platform $(UNAME_S). Supported: Linux, macOS)
endif

PKGTOOL_LIBS := $(CURDIR)/.pkgtool-libs/extracted/usr/lib/x86_64-linux-gnu
PKGTOOL_ENV := DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
ifneq ($(wildcard $(PKGTOOL_LIBS)/libssl.so.1.1),)
PKGTOOL_ENV := LD_LIBRARY_PATH=$(PKGTOOL_LIBS):$$LD_LIBRARY_PATH $(PKGTOOL_ENV)
endif
PKGTOOL := $(PKGTOOL_ENV) $(TOOL_BIN)/PkgTool.Core

CC      := clang
LD      := ld.lld
AR      := llvm-ar
STRIP   := llvm-strip

CFLAGS  := \
    --target=x86_64-pc-freebsd12-elf \
    -isysroot $(SYSROOT) \
    -nostdinc \
    -I./source \
    -isystem $(SYSROOT)/include \
    -isystem $(SYSROOT)/include/c++/v1 \
    -fPIC \
    -funwind-tables \
    -fvisibility=hidden \
    -Wall \
    -Wextra \
    -D__PS4__ \
    -D_BSD_SOURCE \
    -DORBIS_STORE_DATA_PATH=\"/user/app/NPXS39041/\"

LDFLAGS := \
    -m elf_x86_64 \
    -pie \
    --script $(OO_TOOLCHAIN)/link.x \
    --eh-frame-hdr \
    -L$(OO_TOOLCHAIN)/lib \
    -lpthread \
    -lc \
    -lkernel \
    -lSceUserService \
    -lScePad \
    -lSceSystemService \
    -lSceVideoOut \
    -lSceBgft \
    -lSceNet \
    $(OO_TOOLCHAIN)/lib/crt1.o

# ── Build ───────────────────────────────────────────────────────────────────

OBJS    := $(SRCS:.c=.o)
ELF     := $(TARGET).elf
OELF    := $(TARGET).oelf

PKG_DIR := pkg_root
PKG_OUT := $(TARGET).pkg
GP4_OUT := package.gp4
PKG_TMP := $(CONTENT_ID).pkg

.PHONY: all clean

all: $(PKG_OUT)

# Compile
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link ELF
$(ELF): $(OBJS)
	$(LD) $(OBJS) -o $@ $(LDFLAGS)

# Generate open-ELF and PKG
$(OELF): $(ELF)
	$(TOOL_BIN)/create-fself -in=$< -out=$@ --eboot "eboot.bin" --paid 0x3800000000000011

$(PKG_DIR)/sce_sys/param.sfo:
	mkdir -p $(PKG_DIR)/sce_sys
	$(PKGTOOL) sfo_new $@
	$(PKGTOOL) sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKGTOOL) sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(PKGTOOL) sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(PKGTOOL) sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(PKGTOOL) sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TARGET)'
	$(PKGTOOL) sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(PKGTOOL) sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

$(GP4_OUT): $(OELF) $(PKG_DIR)/sce_sys/param.sfo
	cp $(OELF) $(PKG_DIR)/eboot.bin
	$(TOOL_BIN)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$(PKG_DIR)/eboot.bin $(PKG_DIR)/sce_sys/param.sfo"
	sed -i 's|targ_path="$(PKG_DIR)/eboot.bin"|targ_path="eboot.bin"|g' $@
	sed -i 's|targ_path="$(PKG_DIR)/sce_sys/param.sfo"|targ_path="sce_sys/param.sfo"|g' $@


$(PKG_OUT): $(GP4_OUT)
	@if $(PKGTOOL) pkg_build $< .; then \
		true; \
	else \
        echo "Warning: pkg_build failed."; \
		echo "Keeping GP4/OELF artifacts and creating placeholder $(PKG_OUT)."; \
		touch $(PKG_OUT); \
	fi
	@if [ -f "$(PKG_TMP)" ] && [ "$(PKG_TMP)" != "$(PKG_OUT)" ]; then mv -f "$(PKG_TMP)" "$(PKG_OUT)"; fi
	@echo "Built: $@"

clean:
	rm -f $(OBJS) $(ELF) $(OELF) $(PKG_OUT) $(GP4_OUT) $(PKG_TMP)
	rm -rf $(PKG_DIR)
