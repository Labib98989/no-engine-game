#include "platform/input.h"
#include <SDL3/SDL.h>

namespace neg {
namespace platform {

static int button_bit(SDL_Scancode sc, int& player) {
    switch (sc) {
    // P1 — WASD cluster
    case SDL_SCANCODE_D: player = 0; return 0; // A close
    case SDL_SCANCODE_A: player = 0; return 1; // B far
    case SDL_SCANCODE_W: player = 0; return 2; // C up
    case SDL_SCANCODE_S: player = 0; return 3; // D down
    // P2 — arrows
    case SDL_SCANCODE_LEFT: player = 1; return 0;
    case SDL_SCANCODE_RIGHT: player = 1; return 1;
    case SDL_SCANCODE_UP: player = 1; return 2;
    case SDL_SCANCODE_DOWN: player = 1; return 3;
    default: return -1;
    }
}

void poll_events(FrameInput& input, UiCommands& ui) {
    input = FrameInput{};
    ui = UiCommands{};

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            ui.quit = true;
            break;
        case SDL_EVENT_KEY_DOWN: {
            if (ev.key.repeat) break;
            int player = 0;
            int bit = button_bit(ev.key.scancode, player);
            if (bit >= 0) {
                input.pressed[player] |= (uint8_t)(1u << bit);
                break;
            }
            switch (ev.key.scancode) {
            case SDL_SCANCODE_ESCAPE: ui.quit = true; break;
            case SDL_SCANCODE_F1: ui.toggle_overlay = true; break;
            case SDL_SCANCODE_F5: ui.toggle_pause = true; break;
            case SDL_SCANCODE_F6: ui.step = true; break;
            case SDL_SCANCODE_F7: ui.resume = true; break;
            case SDL_SCANCODE_F9: ui.toggle_record = true; break;
            case SDL_SCANCODE_F10: ui.replay = true; break;
            case SDL_SCANCODE_RETURN: ui.restart = true; break;
            default: break;
            }
            break;
        }
        default: break;
        }
    }
}

} // namespace platform
} // namespace neg
