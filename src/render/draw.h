// Reads SimulationState, never mutates (research.md §1.4). M0/M1 placeholder
// art: floor line, two colored rectangles, rectangle HUD, 5x7 text.
#pragma once
#include <SDL3/SDL.h>
#include "sim/types.h"
#include "render/sprites.h"

namespace neg {
namespace render {

struct ViewState {
    bool overlay = false; // F1
    bool paused = false;
    bool recording = false;
    bool replaying = false;
    int replay_status = 0; // 0 none, 1 verified OK, 2 diverged
    uint64_t diverge_tick = 0;
    float fps = 0.0f;
    const char* config_notes = "";
};

void draw_frame(SDL_Renderer* ren, const SimulationState& s, const CharacterData chars[2],
                const SpriteSheet sheets[2], const ViewState& view);

} // namespace render
} // namespace neg
