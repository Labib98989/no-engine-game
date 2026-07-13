#include "data/settings.h"

#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace neg {
namespace data {

Settings load_settings(const std::string& base_dir) {
    Settings s{};
    std::ifstream f(base_dir + "settings.json");
    if (!f.is_open()) return s;
    try {
        json j;
        f >> j;
        s.volume = j.value("volume", s.volume);
        if (s.volume < 0) s.volume = 0;
        if (s.volume > 10) s.volume = 10;
        s.screenshake = j.value("screenshake", s.screenshake);
        s.fullscreen = j.value("fullscreen", s.fullscreen);
        s.debug_hud = j.value("debug_hud", s.debug_hud);
    } catch (const std::exception&) {
        return Settings{};
    }
    return s;
}

void save_settings(const std::string& base_dir, const Settings& s) {
    std::ofstream f(base_dir + "settings.json");
    if (!f.is_open()) return;
    json j;
    j["volume"] = s.volume;
    j["screenshake"] = s.screenshake;
    j["fullscreen"] = s.fullscreen;
    j["debug_hud"] = s.debug_hud;
    f << j.dump(2) << "\n";
}

} // namespace data
} // namespace neg
