// Built-in placeholder character data and tuning. These are the in-code
// defaults used by neg_tests and neg_headless (which link neg_sim only);
// the game overrides them from assets/*.json when present.
// All numbers are deliberate placeholders — design.md §11 fixes only the
// *direction* of each delta; values get dialed in M1 playtesting.
#pragma once
#include "sim/types.h"

namespace neg {

CharacterData default_character(CharId id);
Tuning default_tuning();

} // namespace neg
