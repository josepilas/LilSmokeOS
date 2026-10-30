# Included AFTER the pinned upstream Makefile; platform adaptation only.
omit := src/pc/gfx/gfx_sdl.c src/pc/gfx/gfx_sdl_menu.c src/pc/controller/controller_sdl.c \
        src/pc/audio/audio_sdl.c src/pc/audio/audio_alsa.c src/pc/audio/audio_pulse.c src/pc/savestate.c
native_objects := lilsmoke-platform.o lilsmoke-input.o lilsmoke-common-input.o lilsmoke-keymap.o lilsmoke-log.o lilsmoke-pcm.o lilsmoke-audio.o
O_FILES := $(filter-out $(addprefix $(BUILD_DIR)/,$(omit:.c=.o)),$(O_FILES)) $(addprefix $(BUILD_DIR)/,$(native_objects))
native_input_flags := -std=c++17 -O2 $(SDL_CFLAGS) -I$(LILSMOKE_ROOT)/package/lilsmoke-ui/src/src \
    -include $(LILSMOKE_ROOT)/package/lilsmoke-od-sdl/src/input-wire.h
ifeq ($(LILSMOKE_SANITIZE),1)
$(BUILD_DIR)/src/pc/gfx/gfx_soft.o $(BUILD_DIR)/src/pc/gfx/gfx_pc.o $(BUILD_DIR)/lilsmoke-platform.o: override CFLAGS += -fsanitize=address,undefined -g
endif
ifneq ($(findstring mipsel,$(CC)),)
# Optimize the software renderer only; preserve original game floating-point rules.
$(BUILD_DIR)/src/pc/gfx/gfx_soft.o $(BUILD_DIR)/src/pc/gfx/gfx_pc.o: override CFLAGS += -O3 -mtune=74kc -fno-math-errno -mno-shared -mplt
endif
$(BUILD_DIR)/src/pc/gfx/gfx_soft.o $(BUILD_DIR)/src/pc/gfx/gfx_pc.o: $(LILSMOKE_ROOT)/tools/portsmoke/sm64-original/platform.mk
$(BUILD_DIR)/lilsmoke-platform.o: $(LILSMOKE_PLATFORM) $(LILSMOKE_ROOT)/package/lilsmoke-sm64-original/src/audio_resample.h $(LILSMOKE_ROOT)/package/lilsmoke-sm64-original/src/frame_pacing.h $(LILSMOKE_ROOT)/package/lilsmoke-sm64-original/src/world_quality.h $(LILSMOKE_ROOT)/package/lilsmoke-sm64-original/src/world_scale.h $(LILSMOKE_ROOT)/package/lilsmoke-sm64-original/src/menu.h $(LILSMOKE_ROOT)/tools/portsmoke/sm64-original/platform.mk
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/lilsmoke-input.o: $(LILSMOKE_ROOT)/package/lilsmoke-sm64-original/src/input_bridge.cpp
	$(CXX) $(native_input_flags) -c $< -o $@
$(BUILD_DIR)/lilsmoke-common-input.o: $(LILSMOKE_ROOT)/package/lilsmoke-ui/src/src/Input.cpp
	$(CXX) $(native_input_flags) -c $< -o $@
$(BUILD_DIR)/lilsmoke-keymap.o: $(LILSMOKE_ROOT)/package/lilsmoke-ui/src/src/Keymap.cpp
	$(CXX) $(native_input_flags) -c $< -o $@
$(BUILD_DIR)/lilsmoke-log.o: $(LILSMOKE_ROOT)/package/lilsmoke-ui/src/src/Log.cpp
	$(CXX) $(native_input_flags) -c $< -o $@
$(BUILD_DIR)/lilsmoke-pcm.o: $(LILSMOKE_ROOT)/package/opendingux-emulators/m8_pocketsnes_audio.c
	$(CC) -std=gnu11 -O2 -I$(LILSMOKE_ROOT)/board/segam-m8-v6-stock -c $< -o $@
$(BUILD_DIR)/lilsmoke-audio.o: $(LILSMOKE_ROOT)/board/segam-m8-v6-stock/m8-audio.c
	$(CC) -std=gnu11 -O2 -c $< -o $@
# The prerequisites of the original link rule were expanded before this include.
$(EXE): $(addprefix $(BUILD_DIR)/,$(native_objects))
$(addprefix $(BUILD_DIR)/,$(omit:.c=.o)):
	@true
