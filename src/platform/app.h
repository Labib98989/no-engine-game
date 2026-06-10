// SDL3 init / window / renderer, RAII — research.md §1, §2.5.
#pragma once
#include <SDL3/SDL.h>

namespace neg {
namespace platform {

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool ok = false;

    App(const char* title, int w, int h);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
};

} // namespace platform
} // namespace neg
