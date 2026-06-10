// Per-tick slide toward move_target + shared movement helpers — technical.md §2.
#pragma once
#include "sim/types.h"

namespace neg {

// Advances both fighters one tick toward their move targets. X arrives exactly
// at the next resolution boundary; Y moves in half-beat legs (hop up to the
// instant, down to the resolution; launches/grounding likewise).
void slide_fighters(SimulationState& s);

// Applies both players' Neutral beat displacement simultaneously:
// A = close, B = far, C = hop (cosmetic vertical), D = hold ground.
// Clamps to walls and keeps min_gap so fighters never tunnel through
// each other in Neutral (design.md §10.2).
void apply_neutral_movement(SimulationState& s, const CharacterData chars[2]);

// Re-derives facing from relative position (frame-invariant inputs, design.md §4.3).
void update_facing(SimulationState& s);

// Horizontal gap between the two fighters (range gate input).
Fixed fighter_gap(const SimulationState& s);

// Walls clamp the target, never the slide (technical.md §2).
Fixed clamp_to_walls(Fixed x, const Tuning& t);

} // namespace neg
