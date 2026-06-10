// The single pure tick pipeline — technical.md §4. Nothing else mutates
// SimulationState; render, audio, input and analyzer only read it.
#pragma once
#include "sim/types.h"

namespace neg {
namespace sim {

// Fresh match. skip_intro starts directly in Fighting (tests / headless).
void init_state(SimulationState& s, const CharacterData chars[2], const Tuning& tune,
                uint64_t seed, bool skip_intro = false);

void tick(SimulationState& s, const FrameInput& in, const CharacterData chars[2]);

} // namespace sim
} // namespace neg
