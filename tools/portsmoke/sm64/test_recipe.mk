# Standalone exercise of the real package recipe. Never uses SDK target/images.
PROJECT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
QA := $(PROJECT)/tools/portsmoke/sm64/_build/recipe
SDK ?= /root/lilsmoke-os/build/buildroot/output
BR2_EXTERNAL_LILSMOKE_PATH := $(PROJECT)
TARGET_CXX := $(SDK)/host/bin/mipsel-buildroot-linux-musl-g++
TARGET_CC := $(SDK)/host/bin/mipsel-buildroot-linux-musl-gcc
TARGET_CXXFLAGS := -O2
TARGET_STRIP := $(SDK)/host/bin/mipsel-buildroot-linux-musl-strip
HOST_DIR := $(SDK)/host
BASE_DIR := $(SDK)
TARGET_DIR := $(SDK)/target
SDL2_DIR := $(firstword $(wildcard $(SDK)/build/sdl2-*))
HOST_GCC_FINAL_DIR := $(firstword $(wildcard $(SDK)/build/host-gcc-final-*))
BINARIES_DIR := $(QA)/images
INSTALL := install
include $(PROJECT)/package/portsmoke-sm64/portsmoke-sm64.mk
.PHONY: all $(QA)/src/.tested
all: $(QA)/src/.tested
$(QA)/src/.tested:
	mkdir -p $(@D)
	cp -a $(PORTSMOKE_SM64_SITE)/. $(@D)/
	$(PORTSMOKE_SM64_BUILD_CMDS)
	$(PORTSMOKE_SM64_INSTALL_IMAGES_CMDS)
	cp $(BINARIES_DIR)/SM64-BYO-Data-Development-$(PORTSMOKE_SM64_VERSION).lilpck $(QA)/../SM64-BYO-Data-Development-$(PORTSMOKE_SM64_VERSION).lilpck
