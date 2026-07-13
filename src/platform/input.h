// Event pump -> FrameInput + debug/UI commands. Scancodes only
// (research.md §4.2). The sim never sees SDL.
//
// Inputs are frame-invariant (close/far/up/down — design.md §4.3):
//   P1: D = A(close)   A = B(far)   W = C(up)   S = D(down)
//   P2: Left = A(close) Right = B(far) Up = C(up) Down = D(down)
#pragma once
#include "sim/types.h"

namespace neg {
namespace platform {

struct UiCommands {
    bool quit = false;           // window close
    bool toggle_overlay = false; // F1
    bool toggle_pause = false;   // F5
    bool step = false;           // F6
    bool resume = false;         // F7
    bool toggle_record = false;  // F9
    bool replay = false;         // F10
    bool cycle_cpu = false;      // F8: P2 Human -> CPU Easy -> Normal -> Hard

    // Menu navigation (shell screens). The same physical keys also produce
    // FrameInput bits; the shell and the sim each read only their own side.
    bool nav_up = false;    // W / Up
    bool nav_down = false;  // S / Down
    bool nav_left = false;  // A / Left
    bool nav_right = false; // D / Right
    bool confirm = false;   // Enter
    bool back = false;      // Esc (pause menu in-match; back/quit in menus)
};

// Drains the SDL event queue; fills this frame's press edges and UI commands.
void poll_events(FrameInput& input, UiCommands& ui);

} // namespace platform
} // namespace neg
