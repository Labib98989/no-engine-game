// Reads SimulationState, never mutates (research.md §1.4). M0/M1 placeholder
// art: floor line, two colored rectangles, rectangle HUD, 5x7 text.
#pragma once
#include <SDL3/SDL.h>
#include "sim/types.h"
#include "render/sprites.h"
#include "render/effects.h"
#include "shell/shell.h"

namespace neg {
namespace render {

struct ViewState {
    bool overlay = false; // F1
    bool paused = false;
    bool recording = false;
    bool replaying = false;
    int replay_status = 0; // 0 none, 1 verified OK, 2 diverged
    uint64_t diverge_tick = 0;
    int cpu_p0 = 0, cpu_p1 = 0; // per-seat control, 0 human / 1..3 CPU E/N/H
    bool debug_hud = true;      // frame/beat/fps + controls help lines
    float fps = 0.0f;
    const char* config_notes = "";
};

void draw_frame(SDL_Renderer* ren, const SimulationState& s, const CharacterData chars[2],
                const SpriteSheet sheets[2], const EffectSystem& fx, const ViewState& view);

// Shell screens (title/setup/how-to/options/pause/results), drawn OVER the
// frame — menus dim the attract demo behind them; pause/results dim the match.
void draw_shell(SDL_Renderer* ren, const shell::Shell& sh, const EffectSystem& fx);

} // namespace render
} // namespace neg
