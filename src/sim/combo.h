// Advantage resolver: combo continue / break / steal / cap, cross-up,
// airborne sub-state, passives — technical.md §6.
#pragma once
#include "sim/types.h"

namespace neg {

// Runs once at the window-close boundary while macro == Advantage.
// RPS is dropped here: the defender breaks by exact match only (design.md §5.3).
void resolve_combo(SimulationState& s, const CharacterData chars[2]);

} // namespace neg
