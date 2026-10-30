CC ?= cc
CXX ?= c++
CFLAGS ?= -O2
CXXFLAGS ?= -O2
PROJECT ?= ../..
SOURCE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SDL2_INCLUDE ?= /usr/include/SDL2
SDL12_INCLUDE ?= /usr/include/SDL
INPUT = $(PROJECT)/package/lilsmoke-ui/src/src
POCKET = $(PROJECT)/package/opendingux-emulators
BOARD = $(PROJECT)/board/segam-m8-v6-stock
CPPFLAGS += -I$(SOURCE)src -I$(INPUT) -I$(POCKET) -I$(BOARD)
FLAGS = -fPIC -Wall -Wextra -Werror
OBJECTS = frontend.o video.o audio.o events1.o events2.o input.o common-input.o common-keymap.o common-log.o pcm.o backend.o
all: libod-sdl.so
frontend.o: $(SOURCE)src/frontend.c $(SOURCE)src/od-sdl.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FLAGS) -std=gnu11 -c $< -o $@
audio.o: $(SOURCE)src/audio.c $(SOURCE)src/od-sdl.h
	$(CC) $(CPPFLAGS) -I$(SDL2_INCLUDE) $(CFLAGS) $(FLAGS) -std=gnu11 -c $< -o $@
video.o: $(SOURCE)src/video.c $(SOURCE)src/od-sdl.h
	$(CC) $(CPPFLAGS) -I$(SDL2_INCLUDE) $(CFLAGS) $(FLAGS) -std=gnu11 -c $< -o $@
events1.o: $(SOURCE)src/events.c $(SOURCE)src/od-sdl.h
	$(CC) $(CPPFLAGS) -I$(SDL12_INCLUDE) $(CFLAGS) $(FLAGS) -std=gnu11 -DOD_SDL_API=1 -c $< -o $@
events2.o: $(SOURCE)src/events.c $(SOURCE)src/od-sdl.h
	$(CC) $(CPPFLAGS) -I$(SDL2_INCLUDE) $(CFLAGS) $(FLAGS) -std=gnu11 -DOD_SDL_API=2 -c $< -o $@
input.o: $(SOURCE)src/input.cpp $(SOURCE)src/od-sdl.h $(SOURCE)src/input-wire.h $(INPUT)/Input.h $(INPUT)/Keymap.h $(INPUT)/gamepad-state.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FLAGS) -std=c++17 -include $(SOURCE)src/input-wire.h -c $< -o $@
common-input.o: $(INPUT)/Input.cpp $(SOURCE)src/input-wire.h $(INPUT)/Input.h $(INPUT)/Keymap.h $(INPUT)/gamepad-state.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FLAGS) -std=c++17 -include $(SOURCE)src/input-wire.h -c $< -o $@
common-keymap.o: $(INPUT)/Keymap.cpp $(SOURCE)src/input-wire.h $(INPUT)/Input.h $(INPUT)/Keymap.h $(INPUT)/gamepad-state.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FLAGS) -std=c++17 -include $(SOURCE)src/input-wire.h -c $< -o $@
common-log.o: $(INPUT)/Log.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FLAGS) -std=c++17 -c $< -o $@
pcm.o: $(POCKET)/m8_pocketsnes_audio.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -Wall -Wextra -std=gnu11 -c $< -o $@
backend.o: $(BOARD)/m8-audio.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -Wall -Wextra -std=gnu11 -c $< -o $@
libod-sdl.so: $(OBJECTS)
	$(CXX) -shared -Wl,-z,defs -Wl,-soname,libod-sdl.so $(OBJECTS) -lpthread -ldl -o $@
clean:
	rm -f $(OBJECTS) libod-sdl.so
.PHONY: all clean
