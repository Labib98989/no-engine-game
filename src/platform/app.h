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

// Borderless-fullscreen toggle. The renderer keeps a 1280x720 logical
// presentation (letterboxed), so all draw code stays in fixed coordinates.
void set_fullscreen(App& app, bool on);

} // namespace platform
} // namespace neg
