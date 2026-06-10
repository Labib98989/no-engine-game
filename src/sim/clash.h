// Neutral resolver: the asymmetric 4-way RPS clash — technical.md §5.
#pragma once
#include "sim/types.h"

namespace neg {

// beats_table[x][y] == true iff input x beats input y (A=0..D=3).
// Cycle A>B>C>D>A plus diagonals A>C, D>B (design.md §5.1).
extern const bool rps_beats[4][4];

inline bool input_beats(Input x, Input y) {
    return rps_beats[(int)x - 1][(int)y - 1];
}

// Runs once at the window-close boundary while macro == Neutral.
// Applies movement, damage, and the Neutral -> Advantage transition.
void resolve_clash(SimulationState& s, const CharacterData chars[2]);

// Damage of a landed neutral/combo hit before combo scaling: base per input,
// modulated by timing tier (design.md §6).
int32_t base_damage(const CharacterData& cd, Input in, Tier tier, const Tuning& t);

} // namespace neg
