#pragma comment(lib, "SDL3.lib")

#include <SDL3/SDL.h>

static void log_error(const char* msg="Unknown error")
{
    SDL_Log("[ERROR]: %s", msg);
}

static void log_sdl_error(const char* msg="Unknown SDL error")
{
    SDL_Log("[SDL ERROR]: %s: %s", msg, SDL_GetError());
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log_sdl_error("Couldn't initialise SDL");
        return 1;
    } else {
        SDL_Log("SDL Video initialised.");
    }

    SDL_Window* window = SDL_CreateWindow("Name", 640, 480, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        log_sdl_error("Couldn't create SDL window");
        return 1;
    } else {
        SDL_Log("SDL Window created.");
    }
    return(0);
}