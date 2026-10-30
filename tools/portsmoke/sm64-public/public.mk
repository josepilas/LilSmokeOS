# SPDX-License-Identifier: MPL-2.0
# Included after the original engine Makefile. No replacement gameplay logic.
omit := src/pc/gfx/gfx_sdl.c src/pc/gfx/gfx_sdl_menu.c src/pc/controller/controller_sdl.c \
        src/pc/audio/audio_sdl.c src/pc/audio/audio_alsa.c src/pc/audio/audio_pulse.c src/pc/savestate.c
native_objects := lilsmoke-platform.o lilsmoke-input.o lilsmoke-common-input.o \
                  lilsmoke-keymap.o lilsmoke-log.o lilsmoke-pcm.o lilsmoke-audio.o
O_FILES := $(filter-out $(addprefix $(BUILD_DIR)/,$(omit:.c=.o)),$(O_FILES)) \
           $(addprefix $(BUILD_DIR)/,$(native_objects))

$(BUILD_DIR)/src/pc/gfx/gfx_soft.o $(BUILD_DIR)/src/pc/gfx/gfx_pc.o: override CFLAGS += -O3 -fno-math-errno -mno-shared -mplt
$(BUILD_DIR)/lilsmoke-platform.o: lilsmoke/platform_sdl2.c
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/lilsmoke-pcm.o: lilsmoke/m8_pocketsnes_audio.c
	$(CC) $(CFLAGS) -std=gnu11 -Ililsmoke -c $< -o $@
$(BUILD_DIR)/lilsmoke-audio.o: lilsmoke/m8-audio.c
	$(CC) $(CFLAGS) -std=gnu11 -c $< -o $@
$(addprefix $(BUILD_DIR)/,$(omit:.c=.o)):
	@true

$(EXE): $(addprefix $(BUILD_DIR)/,$(native_objects))
	$(CC) -mips32r2 -mabi=32 -mfp32 -mhard-float -mdspr2 -nostdlib -no-pie \
	    -Wl,--dynamic-linker=/lib/ld-musl-mipsel.so.1,--wrap=gfx_run,--wrap=create_next_audio_buffer \
	    -o $@ $(RUNTIME_SYSROOT)/lib/crt1.o $(RUNTIME_SYSROOT)/lib/crti.o \
	    $(RUNTIME_SYSROOT)/gcc/crtbegin.o $(O_FILES) $(SOUND_OBJ_FILES) $(ULTRA_O_FILES) $(GODDARD_O_FILES) \
	    -L$(RUNTIME_SYSROOT)/usr/lib -L$(RUNTIME_SYSROOT)/lib -L$(RUNTIME_LIB) \
	    -lSDL2 -lstdc++ -lm -lpthread -ldl -lc -lgcc -lgcc_s \
	    $(RUNTIME_SYSROOT)/gcc/crtend.o $(RUNTIME_SYSROOT)/lib/crtn.o
