#include "platform/app.h"

namespace neg {
namespace platform {

App::App(const char* title, int w, int h) {
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "1");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return;
    }
    window = SDL_CreateWindow(title, w, h, 0);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return;
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return;
    }
    SDL_SetRenderVSync(renderer, 1);
    // Fixed logical canvas: fullscreen and window resizes letterbox-scale the
    // same 1280x720 the draw code targets.
    SDL_SetRenderLogicalPresentation(renderer, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    ok = true;
}

void set_fullscreen(App& app, bool on) {
    if (app.window) SDL_SetWindowFullscreen(app.window, on);
}

App::~App() {
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

} // namespace platform
} // namespace neg
