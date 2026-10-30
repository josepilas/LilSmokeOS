# LilSmokeUI: native C++17, SDL2 software rendering (DirectFB on the console).
CXX ?= g++
PKG_CONFIG ?= pkg-config
CXXFLAGS += -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
            -ffunction-sections -fdata-sections \
            $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_image SDL2_ttf libpng json-c)
LDLIBS += $(shell $(PKG_CONFIG) --libs sdl2 SDL2_image SDL2_ttf libpng json-c) -lz -lwebp
LDFLAGS += -Wl,--gc-sections
CPPFLAGS += -I../../lilsmoke-scripts/src
CPPFLAGS += -I../../lilsmoke-media/src
SRCS := src/main.cpp src/Log.cpp src/Config.cpp src/Theme.cpp src/ThemeRender.cpp \
        src/ROMScanner.cpp src/RetroArchPlaylists.cpp src/Input.cpp src/Keymap.cpp src/Renderer.cpp src/Font.cpp \
        src/Cursor.cpp src/BluetoothView.cpp src/RemoteAccessView.cpp src/FileExplorerView.cpp src/ListView.cpp \
        src/SystemView.cpp src/GridView.cpp src/RetroArchMenuView.cpp src/SettingsView.cpp \
        src/InputSettingsView.cpp src/OTAView.cpp src/Launcher.cpp src/ProcessLaunch.cpp \
        src/VirtualKeyboard.cpp src/EmulatorSettingsView.cpp src/ApplicationsView.cpp src/ApplicationArtwork.cpp \
        src/GameHistory.cpp src/RecentGamesView.cpp src/FavoritesStore.cpp src/FavoritesView.cpp src/GameOptionsView.cpp \
        src/TreeFrogCatalog.cpp src/TreeFrogPortsView.cpp
OBJS := $(SRCS:.cpp=.o)
TARGET := lilsmoke-ui
all: $(TARGET) lilsmoke-session
lilsmoke-session: apps/session.cpp src/GameSession.cpp src/GameHistory.cpp src/GameHistory.h src/FilePersistence.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc -o $@ apps/session.cpp src/GameSession.cpp src/GameHistory.cpp $(LDFLAGS)
$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<
clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET) lilsmoke-session
install: $(TARGET) lilsmoke-session
	install -D -m 0755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	install -D -m 0755 lilsmoke-session $(DESTDIR)/usr/lib/lilsmoke/lilsmoke-session
	install -D -m 0644 assets/lilsmoke-cursors.webp $(DESTDIR)/usr/share/lilsmoke/cursors/lilsmoke-cursors.webp
-include $(OBJS:.o=.d)
.PHONY: all clean install
