// miniaudio glue. Reads the sim's beat/tick state and chases it with sound —
// strictly one-way (design.md §12): audio never influences the sim.
// All sounds are synthesized at init; placeholder audio needs no assets.
#pragma once
#include "sim/types.h"

namespace neg {
namespace audio {

struct Audio; // opaque (miniaudio types stay out of the header)

Audio* init();
void shutdown(Audio* a);

// Edge-detects beat instants and resolutions in the sim state and fires
// click / accent / hit / steal sounds. Call once per render frame.
void update(Audio* a, const SimulationState& s);

// Master volume, 0..1 (Options screen; applied to the whole engine).
void set_volume(Audio* a, float v);

// Menu blips (shell screens), reusing the synthesized pools.
void ui_move(Audio* a);
void ui_confirm(Audio* a);
void ui_back(Audio* a);

} // namespace audio
} // namespace neg
