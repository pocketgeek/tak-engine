// takview — TAK asset viewer. Currently just proves the SDL2 pipeline:
// opens a window and clears it until closed.

#include <SDL.h>

#include <cstdio>

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "takview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer =
        window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!renderer) {
        std::fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }
        SDL_SetRenderDrawColor(renderer, 24, 24, 32, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
