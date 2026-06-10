#include "data/loader.h"
#include "sim/chardata.h"

#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace neg {
namespace data {

static bool read_json(const std::string& path, json& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        f >> out;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

static void load_input_indexed(const json& j, const char* key, Fixed* dst) {
    if (!j.contains(key)) return;
    const json& o = j[key];
    static const char* names[4] = {"A", "B", "C", "D"};
    for (int i = 0; i < 4; ++i)
        if (o.contains(names[i])) dst[i + 1] = Fixed::from_int(o[names[i]].get<int32_t>());
}

static void load_input_indexed_i32(const json& j, const char* key, int32_t* dst) {
    if (!j.contains(key)) return;
    const json& o = j[key];
    static const char* names[4] = {"A", "B", "C", "D"};
    for (int i = 0; i < 4; ++i)
        if (o.contains(names[i])) dst[i + 1] = o[names[i]].get<int32_t>();
}

static void load_scale(const json& j, const char* key, int32_t* dst) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 5) return;
    for (int i = 0; i < 5; ++i) dst[i] = j[key][i].get<int32_t>();
}

static bool load_character(const std::string& path, CharacterData& c) {
    json j;
    if (!read_json(path, j)) return false;
    c.health = j.value("health", c.health);
    load_input_indexed(j, "range", c.range);
    load_input_indexed(j, "move", c.move_dist);
    load_input_indexed_i32(j, "damage", c.damage_ground);
    load_scale(j, "combo_scale_ground", c.combo_scale_ground);
    load_scale(j, "combo_scale_air", c.combo_scale_air);
    if (j.contains("launch_height")) c.launch_height = Fixed::from_int(j["launch_height"].get<int32_t>());
    c.air_beats = (uint8_t)j.value("air_beats", (int)c.air_beats);
    std::string passive = j.value("passive", std::string());
    if (passive == "stick_the_landing") c.passive = PassiveId::StickTheLanding;
    else if (passive == "sustain") c.passive = PassiveId::Sustain;
    else if (passive == "none") c.passive = PassiveId::None;
    return true;
}

static bool load_tuning(const std::string& path, Tuning& t) {
    json j;
    if (!read_json(path, j)) return false;
    t.bpm = (uint16_t)j.value("bpm", (int)t.bpm);
    t.perfect_ticks = (uint8_t)j.value("perfect_ticks", (int)t.perfect_ticks);
    t.normal_ticks = (uint8_t)j.value("normal_ticks", (int)t.normal_ticks);
    t.chip_damage = j.value("chip_damage", t.chip_damage);
    t.perfect_damage_pct = j.value("perfect_damage_pct", t.perfect_damage_pct);
    t.stick_landing_bonus_pct = j.value("stick_landing_bonus_pct", t.stick_landing_bonus_pct);
    t.combo_cap = (uint8_t)j.value("combo_cap", (int)t.combo_cap);
    t.round_seconds = (uint16_t)j.value("round_seconds", (int)t.round_seconds);
    t.rounds_to_win = (uint8_t)j.value("rounds_to_win", (int)t.rounds_to_win);
    t.intro_beats = (uint8_t)j.value("intro_beats", (int)t.intro_beats);
    t.round_end_ticks = (uint16_t)j.value("round_end_ticks", (int)t.round_end_ticks);
    t.stage_width = j.value("stage_width", t.stage_width);
    t.wall_margin = j.value("wall_margin", t.wall_margin);
    t.min_gap = j.value("min_gap", t.min_gap);
    t.start_offset = j.value("start_offset", t.start_offset);
    t.crossup_gap = j.value("crossup_gap", t.crossup_gap);
    // 60 Hz sim: BPM must divide 3600 so TPB stays integral (technical.md §1.1).
    if (t.bpm == 0 || 3600 % t.bpm != 0) t.bpm = 120;
    return true;
}

GameConfig load_config(const std::string& base_dir) {
    GameConfig cfg;
    cfg.chars[0] = default_character(CharId::Breaker);
    cfg.chars[1] = default_character(CharId::Ballerina);
    cfg.tune = default_tuning();

    cfg.notes += load_tuning(base_dir + "assets/config.json", cfg.tune)
                     ? "config.json "
                     : "config:builtin ";
    cfg.notes += load_character(base_dir + "assets/characters/breaker.json", cfg.chars[0])
                     ? "breaker.json "
                     : "breaker:builtin ";
    cfg.notes += load_character(base_dir + "assets/characters/ballerina.json", cfg.chars[1])
                     ? "ballerina.json"
                     : "ballerina:builtin";
    return cfg;
}

} // namespace data
} // namespace neg
