LILSMOKE_MEDIA_VERSION = 0.1.0
LILSMOKE_MEDIA_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-media/src
LILSMOKE_MEDIA_SITE_METHOD = local
LILSMOKE_MEDIA_LICENSE = MPL-2.0
LILSMOKE_MEDIA_LICENSE_FILES = LICENSE
LILSMOKE_MEDIA_DEPENDENCIES = lilsmoke-ffmpeg sdl2 sdl2_image sdl2_ttf lilsmoke-ui
LILSMOKE_MEDIA_PCM_FLAGS = -DM8_PCM_BACKEND_TIMEOUT_MS=500U -DM8_PCM_BACKEND_MAX_POLLS=256U \
    -DM8_PCM_ACK_TIMEOUT_MS=1000U -DM8_PENDING_LIMIT_MS=500U -DM8_STALL_LIMIT=256U
LILSMOKE_MEDIA_FLAGS = $(TARGET_CXXFLAGS) -std=c++17 -O2 -Wall -Wextra \
    -DM8_PCM_INFLIGHT_LIMIT=16U \
    -Wno-misleading-indentation -Wno-unused-parameter \
    -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src \
    -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/opendingux-emulators \
    -include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-od-sdl/src/input-wire.h \
    $(shell $(PKG_CONFIG_HOST_BINARY) --cflags sdl2 SDL2_image SDL2_ttf libavformat libavcodec libavutil libswscale libswresample)
define LILSMOKE_MEDIA_BUILD_CMDS
    $(TARGET_CC) $(TARGET_CFLAGS) $(LILSMOKE_MEDIA_PCM_FLAGS) -O2 -std=gnu11 -DM8_PCM_INFLIGHT_LIMIT=16U \
        -I$(BR2_EXTERNAL_LILSMOKE_PATH)/board/segam-m8-v6-stock \
        -c $(BR2_EXTERNAL_LILSMOKE_PATH)/package/opendingux-emulators/m8_pocketsnes_audio.c -o $(@D)/pcm.o
    $(TARGET_CC) $(TARGET_CFLAGS) $(LILSMOKE_MEDIA_PCM_FLAGS) -O2 -std=gnu11 \
        -c $(BR2_EXTERNAL_LILSMOKE_PATH)/board/segam-m8-v6-stock/m8-audio.c -o $(@D)/audio.o
    $(TARGET_CXX) $(LILSMOKE_MEDIA_FLAGS) $(@D)/Player.cpp $(@D)/Decoder.cpp $(@D)/NativeVideo.cpp \
        $(addprefix $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src/,Renderer.cpp Font.cpp Input.cpp Keymap.cpp Log.cpp Cursor.cpp) \
        $(@D)/pcm.o $(@D)/audio.o $(TARGET_LDFLAGS) -Wl,--gc-sections \
        $(shell $(PKG_CONFIG_HOST_BINARY) --libs sdl2 SDL2_image SDL2_ttf libavformat libavcodec libavutil libswscale libswresample) \
        -ldl -lwebp -pthread -o $(@D)/lilsmoke-media
endef
define LILSMOKE_MEDIA_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/lilsmoke-media $(TARGET_DIR)/usr/bin/lilsmoke-media
    $(INSTALL) -D -m 0755 $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-media/lilsmoke-media-open \
        $(TARGET_DIR)/usr/lib/lilsmoke/lilsmoke-media-open
endef
$(eval $(generic-package))
