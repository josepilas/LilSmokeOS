#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  SDL_Keycode key;
  const char *name;
  int down;
} Button;
static Button buttons[] = {
    {SDLK_UP, "Up", 0},        {SDLK_DOWN, "Down", 0},
    {SDLK_LEFT, "Left", 0},    {SDLK_RIGHT, "Right", 0},
    {SDLK_LCTRL, "A", 0},      {SDLK_LALT, "B", 0},
    {SDLK_SPACE, "X", 0},      {SDLK_LSHIFT, "Y", 0},
    {SDLK_TAB, "L", 0},        {SDLK_BACKSPACE, "R", 0},
    {SDLK_RETURN, "Start", 0}, {SDLK_ESCAPE, "Select", 0}};
static void label(SDL_Renderer *renderer, TTF_Font *font, const char *value,
                  int x, int y, SDL_Color color) {
  SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value, color);
  if (!surface)
    return;
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (texture) {
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
  }
  SDL_FreeSurface(surface);
}
int main(void) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0 || TTF_Init() < 0) {
    fprintf(stderr, "APP_DEMO_ERROR %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window *window = SDL_CreateWindow(
      "Button Tester", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640,
      360, SDL_WINDOW_FULLSCREEN);
  SDL_Renderer *renderer =
      window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : NULL;
  TTF_Font *font = TTF_OpenFont("runtime/assets/font.ttf", 20);
  if (!renderer || !font) {
    fprintf(stderr, "APP_DEMO_ERROR renderer/font\n");
    if (renderer)
      SDL_DestroyRenderer(renderer);
    if (window)
      SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }
  SDL_RenderSetLogicalSize(renderer, 640, 360);
  int running = 1, events = 0, dirty = 1;
  printf("APP_DEMO_READY video=%s software=1 canvas=640x360\n",
         SDL_GetCurrentVideoDriver());
  fflush(stdout);
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT)
        running = 0;
      if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
          if (buttons[i].key == event.key.keysym.sym)
            buttons[i].down = event.type == SDL_KEYDOWN;
        if (event.type == SDL_KEYDOWN && !event.key.repeat) {
          ++events;
          printf("APP_DEMO_KEY %d\n", event.key.keysym.sym);
          fflush(stdout);
        }
        dirty = 1;
        if (buttons[10].down && buttons[11].down)
          running = 0;
      }
      if (event.type == SDL_WINDOWEVENT)
        dirty = 1;
    }
    if (dirty) {
      SDL_SetRenderDrawColor(renderer, 239, 246, 251, 255);
      SDL_RenderClear(renderer);
      label(renderer, font, "Button Tester", 24, 16,
            (SDL_Color){28, 64, 87, 255});
      char value[64];
      snprintf(value, sizeof(value), "Input events: %d", events);
      label(renderer, font, value, 392, 18, (SDL_Color){71, 104, 124, 255});
      for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        SDL_Rect cell = {24 + (int)(i % 4) * 152, 70 + (int)(i / 4) * 88, 136,
                         70};
        SDL_SetRenderDrawColor(renderer, buttons[i].down ? 22 : 255,
                               buttons[i].down ? 140 : 255,
                               buttons[i].down ? 235 : 255, 255);
        SDL_RenderFillRect(renderer, &cell);
        SDL_SetRenderDrawColor(renderer, 139, 181, 211, 255);
        SDL_RenderDrawRect(renderer, &cell);
        label(renderer, font, buttons[i].name, cell.x + 14, cell.y + 22,
              buttons[i].down ? (SDL_Color){255, 255, 255, 255}
                              : (SDL_Color){36, 61, 80, 255});
      }
      SDL_RenderPresent(renderer);
      dirty = 0;
    }
    SDL_Delay(8);
  }
  FILE *journal = fopen("input-test.log", "a");
  if (journal) {
    fprintf(journal, "events=%d clean-exit=1\n", events);
    fclose(journal);
  }
  printf("APP_DEMO_EXIT events=%d cleanup=1\n", events);
  fflush(stdout);
  TTF_CloseFont(font);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  SDL_Quit();
  return 0;
}
