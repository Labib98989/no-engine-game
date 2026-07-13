// JSON -> POD config loaders. Lives outside neg_sim: the sim consumes the
// PODs, never the JSON. Missing/broken files fall back to the built-in
// placeholder data in sim/chardata.h so the game always boots.
#pragma once
#include <string>
#include "sim/types.h"
#include "sim/ai.h"

namespace neg {
namespace data {

struct GameConfig {
    CharacterData chars[2];
    Tuning tune;
    AiConfig ai[3]; // Easy / Normal / Hard presets (F8 vs-CPU mode)
    std::string notes; // which files loaded vs fell back (HUD/log)
};

GameConfig load_config(const std::string& base_dir);

} // namespace data
} // namespace neg
