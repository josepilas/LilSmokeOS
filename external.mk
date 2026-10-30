# LilSmokeOS — Buildroot external tree makefile
#
# This file is sourced by Buildroot when BR2_EXTERNAL_LILSMOKE_PATH is set.
# It sets up the path variable and includes our package makefiles.

export BR2_EXTERNAL_LILSMOKE_PATH := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# Include our package definitions
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-scripts/lilsmoke-scripts.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-apps/lilsmoke-apps.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-syspck/lilsmoke-syspck.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/portsmoke/portsmoke.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/portsmoke-sm64/portsmoke-sm64.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/directfb-mips/directfb-mips.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/lilsmoke-ui.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/libvterm/libvterm.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-terminal/lilsmoke-terminal.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ffmpeg/lilsmoke-ffmpeg.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-media/lilsmoke-media.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-fbtest/lilsmoke-fbtest.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/retroarch-mips/retroarch-mips.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-n64/lilsmoke-n64.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-gamepads/lilsmoke-gamepads.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-usb-gamepads/lilsmoke-usb-gamepads.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-usb-transport/lilsmoke-usb-transport.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-bluetooth/lilsmoke-bluetooth.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/pcsx4all/pcsx4all.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/opendingux-emulators/opendingux-emulators.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-sdl12/lilsmoke-sdl12.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-sqfs/lilsmoke-sqfs.mk
include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-opendingux/lilsmoke-opendingux.mk

# Apply only when the optional Bluetooth package is selected.
ifeq ($(BR2_PACKAGE_LILSMOKE_BLUETOOTH),y)
BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES += $(BR2_EXTERNAL_LILSMOKE_PATH)/board/common/bluetooth.fragment
endif

# Per-board hooks — select the right board directory based on the
# chosen target device. Defaults to qemu-mipsel if nothing is set
# (e.g., during early build before the target is selected).
ifeq ($(BR2_LILSMOKE_TARGET_QEMU),y)
LILSMOKE_BOARD_DIR := $(BR2_EXTERNAL_LILSMOKE_PATH)/board/qemu-mipsel
else ifeq ($(BR2_LILSMOKE_TARGET_SF3000),y)
LILSMOKE_BOARD_DIR := $(BR2_EXTERNAL_LILSMOKE_PATH)/board/sf3000
else ifeq ($(BR2_LILSMOKE_TARGET_GB350),y)
LILSMOKE_BOARD_DIR := $(BR2_EXTERNAL_LILSMOKE_PATH)/board/gb350
else ifeq ($(BR2_LILSMOKE_TARGET_VBOX_X86_64),y)
LILSMOKE_BOARD_DIR := $(BR2_EXTERNAL_LILSMOKE_PATH)/board/vbox-x86_64
else
LILSMOKE_BOARD_DIR := $(BR2_EXTERNAL_LILSMOKE_PATH)/board/qemu-mipsel
endif

# DirectFB/musl fixes are applied through BR2_GLOBAL_PATCH_DIR, configured in
# the board defconfig as $(BR2_EXTERNAL_LILSMOKE_PATH)/patches.

# --- Register post-build / post-image hooks ---
# These run after the rootfs is populated and after the image is
# packed, respectively. Each board has its own post-build.sh and
# post-image.sh.
ifeq ($(BR2_PACKAGE_LILSMOKE_OS),y)
define LILSMOKE_POST_BUILD_HOOK
    $(LILSMOKE_BOARD_DIR)/post-build.sh $(TARGET_DIR) $(LILSMOKE_BOARD_DIR)
    $(SHELL) $(BR2_EXTERNAL_LILSMOKE_PATH)/scripts/install-syspck-policy.sh $(TARGET_DIR) $(BR2_LILSMOKE_TARGET)
endef
TARGET_FINALIZE_HOOKS += LILSMOKE_POST_BUILD_HOOK

define LILSMOKE_POST_IMAGE_HOOK
    $(LILSMOKE_BOARD_DIR)/post-image.sh $(BINARIES_DIR) $(LILSMOKE_BOARD_DIR)
endef
ifneq ($(filter y,$(BR2_LILSMOKE_TARGET_QEMU) $(BR2_LILSMOKE_TARGET_VBOX_X86_64)),)
POST_IMAGE_HOOKS += LILSMOKE_POST_IMAGE_HOOK
endif
endif
