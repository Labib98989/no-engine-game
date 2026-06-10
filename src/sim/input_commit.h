// FrameInput -> per-beat Commit: first-press-wins + tier banding (technical.md §1.4).
#pragma once
#include "sim/types.h"

namespace neg {

// Captures this tick's fresh presses into each fighter's commit for the open
// beat. Idempotent for a repeated snapshot (catch-up safe): once locked,
// later presses in the same window are ignored.
void capture_commits(SimulationState& s, const FrameInput& in);

} // namespace neg
