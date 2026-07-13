// Player-facing options, persisted to settings.json next to the exe (same
// place the F9 ticklog goes). Missing/broken file falls back to defaults so
// the game always boots; save is best-effort.
#pragma once
#include <string>

namespace neg {
namespace data {

struct Settings {
    int volume = 7;          // 0..10 master volume
    bool screenshake = true; // render-side juice only; never touches the sim
    bool fullscreen = false; // borderless, letterboxed to the 1280x720 canvas
    bool debug_hud = false;  // frame/beat/fps + controls help lines
};

Settings load_settings(const std::string& base_dir);
void save_settings(const std::string& base_dir, const Settings& s);

} // namespace data
} // namespace neg
