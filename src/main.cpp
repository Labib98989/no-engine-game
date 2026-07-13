// Classic int main(), fixed-timestep 60 Hz loop with accumulator and
// anti-spiral clamps — research.md §2.1. The window is just one of three
// front-ends onto neg_sim (build-plan.md §3).
//
// The shell (title/setup/options/pause/results) wraps the match: while any
// menu screen is up, an attract-mode CPU-vs-CPU demo plays dimmed behind it
// on the same sim, and the metronome doubles as title music.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <vector>
#include <string>
#include <fstream>

#include "sim/simulation.h"
#include "sim/chardata.h"
#include "sim/ai.h"
#include "data/loader.h"
#include "data/settings.h"
#include "shell/shell.h"
#include "platform/app.h"
#include "platform/input.h"
#include "render/draw.h"
#include "render/sprites.h"
#include "render/effects.h"
#include "audio/audio.h"

using namespace neg;

static constexpr uint64_t DT_NS = 16'666'667ULL;         // 60 Hz
static constexpr uint64_t MAX_FRAME_NS = 250'000'000ULL; // anti-spiral clamp
static constexpr int MAX_STEPS_PER_FRAME = 5;
static constexpr uint64_t MATCH_SEED = 0xC0FFEEULL;

enum class LoopMode { Running, Paused };

struct Recorder {
    bool recording = false;
    bool replaying = false;
    std::vector<FrameInput> inputs;
    std::vector<uint64_t> checksums;
    size_t cursor = 0;
    int status = 0; // 0 none, 1 ok, 2 diverged
    uint64_t diverge_tick = 0;
};

static void write_ticklog(const Recorder& rec, const std::string& dir) {
    std::ofstream f(dir + "session_ticks.txt");
    if (!f.is_open())
        return;
    f << "# neg ticklog v1 - one line per tick: <p0bits> <p1bits>\n";
    f << "seed " << MATCH_SEED << "\n";
    f << "ticks " << rec.inputs.size() << "\n";
    for (const FrameInput& in : rec.inputs)
        f << (int)in.pressed[0] << " " << (int)in.pressed[1] << "\n";
}

int main(int, char**) {
    platform::App app("No Engine Game - rhythm yomi duel [placeholder art]", 1280, 720);
    if (!app.ok)
        return 1;

    const char* base = SDL_GetBasePath();
    std::string base_dir = base ? base : "";
    data::GameConfig cfg = data::load_config(base_dir);
    SDL_Log("config: %s", cfg.notes.c_str());

    data::Settings settings = data::load_settings(base_dir);
    platform::set_fullscreen(app, settings.fullscreen);

    shell::Shell sh{};
    sh.settings = &settings;

    SimulationState state{};
    render::EffectSystem fx{};
    AiState bots[2];
    int active_control[2] = {2, 2}; // per-seat: 0 human, 1..3 CPU E/N/H
    uint64_t next_seed = MATCH_SEED;   // rotates per match for bot variety
    uint64_t attract_seed = 0xA77AC7ULL;

    // The demo behind the menus: two Normal CPUs, rotating seeds and casts.
    auto start_attract = [&]() {
        CharId p0 = (attract_seed & 1) ? CharId::Ballerina : CharId::Breaker;
        CharId p1 = (attract_seed & 2) ? CharId::Breaker : CharId::Ballerina;
        sim::init_state(state, cfg.chars, cfg.tune, attract_seed, p0, p1);
        ai_init(bots[0], attract_seed, 0);
        ai_init(bots[1], attract_seed, 1);
        attract_seed++;
        active_control[0] = 2;
        active_control[1] = 2;
        render::effects_init(fx, state);
    };

    auto start_match = [&]() {
        Tuning tune = cfg.tune;
        tune.rounds_to_win = shell::rounds_to_win(sh.setup);
        sim::init_state(state, cfg.chars, tune, next_seed, sh.setup.chars[0],
                        sh.setup.chars[1]);
        ai_init(bots[0], next_seed, 0);
        ai_init(bots[1], next_seed, 1);
        next_seed++;
        active_control[0] = sh.setup.control[0];
        active_control[1] = sh.setup.control[1];
        render::effects_init(fx, state);
    };

    start_attract();

    // Sprite sheets are render-side; an invalid sheet (no JSON block or missing
    // PNG) makes draw_fighter fall back to the placeholder rectangle.
    render::SpriteSheet sheets[2] = {
        render::load_sprite_sheet(app.renderer, base_dir, "assets/characters/breaker.json"),
        render::load_sprite_sheet(app.renderer, base_dir, "assets/characters/ballerina.json"),
    };
    SDL_Log("sprites: breaker=%s ballerina=%s", sheets[0].valid ? "loaded" : "rect",
            sheets[1].valid ? "loaded" : "rect");

    audio::Audio* snd = audio::init();
    audio::set_volume(snd, (float)settings.volume / 10.0f);

    LoopMode mode = LoopMode::Running;
    bool step_request = false;
    bool quit = false;
    Recorder rec;
    render::ViewState view{};
    view.config_notes = cfg.notes.c_str();

    FrameInput input{};
    platform::UiCommands ui{};

    uint64_t last_ns = SDL_GetTicksNS();
    uint64_t accumulator = 0;
    float fps = 60.0f;

    while (!quit) {
        uint64_t now_ns = SDL_GetTicksNS();
        uint64_t frame_ns = now_ns - last_ns;
        last_ns = now_ns;
        if (frame_ns > MAX_FRAME_NS)
            frame_ns = MAX_FRAME_NS;
        if (frame_ns > 0)
            fps = fps * 0.95f + 0.05f * (1e9f / (float)frame_ns);

        platform::poll_events(input, ui);
        bool in_match = sh.screen == shell::Screen::Match;

        if (ui.quit)
            quit = true;
        if (ui.toggle_overlay)
            view.overlay = !view.overlay;
        if (ui.toggle_pause)
            mode = mode == LoopMode::Paused ? LoopMode::Running : LoopMode::Paused;
        if (ui.step && mode == LoopMode::Paused)
            step_request = true;
        if (ui.resume)
            mode = LoopMode::Running;

        // F8: cycle P2 control mid-match (also updates the setup screen value).
        if (ui.cycle_cpu && in_match) {
            sh.setup.control[1] = (sh.setup.control[1] + 1) % 4;
            active_control[1] = sh.setup.control[1];
            ai_init(bots[1], MATCH_SEED, 1);
        }

        // F9: arm/stop recording (match only). Arming restarts from the fixed
        // seed and the standard cast so the input log alone reproduces the
        // session (headless cross-check, build-plan.md S5).
        if (ui.toggle_record && in_match && !rec.replaying) {
            if (!rec.recording) {
                rec = Recorder{};
                rec.recording = true;
                sim::init_state(state, cfg.chars, cfg.tune, MATCH_SEED);
                ai_init(bots[0], MATCH_SEED, 0);
                ai_init(bots[1], MATCH_SEED, 1);
            } else {
                rec.recording = false;
                write_ticklog(rec, base_dir);
            }
        }
        // F10: replay the last recording from the same fresh state,
        // verifying per-tick checksums (the determinism harness).
        if (ui.replay && in_match && !rec.recording && !rec.inputs.empty()) {
            rec.replaying = true;
            rec.cursor = 0;
            rec.status = 0;
            sim::init_state(state, cfg.chars, cfg.tune, MATCH_SEED);
            ai_init(bots[0], MATCH_SEED, 0);
            ai_init(bots[1], MATCH_SEED, 1);
        }

        // ---- shell: menu navigation + actions --------------------------------
        shell::Events ev{};
        ev.up = ui.nav_up;
        ev.down = ui.nav_down;
        ev.left = ui.nav_left;
        ev.right = ui.nav_right;
        ev.confirm = ui.confirm;
        ev.back = ui.back;

        shell::Screen before = sh.screen;
        shell::Action act = shell::update(sh, ev);

        // Menu blips: nav sounds only on menu screens; back always blips.
        if (before != shell::Screen::Match) {
            if (ev.up || ev.down || ev.left || ev.right)
                audio::ui_move(snd);
            if (ev.confirm)
                audio::ui_confirm(snd);
        }
        if (ev.back)
            audio::ui_back(snd);

        switch (act) {
        case shell::Action::StartMatch:
        case shell::Action::RestartMatch:
            rec = Recorder{}; // a fresh match invalidates the old recording
            start_match();
            break;
        case shell::Action::ToTitle:
            rec = Recorder{};
            start_attract();
            break;
        case shell::Action::ApplySettings:
        case shell::Action::SaveSettings:
            audio::set_volume(snd, (float)settings.volume / 10.0f);
            platform::set_fullscreen(app, settings.fullscreen);
            if (act == shell::Action::SaveSettings)
                data::save_settings(base_dir, settings);
            break;
        case shell::Action::QuitApp:
            quit = true;
            break;
        default:
            break;
        }

        // Match over -> results menu (once; Results freezes the sim so the
        // winner banner stays up behind the choices).
        if (sh.screen == shell::Screen::Match && state.match.phase == Phase::MatchEnd &&
            !rec.replaying) {
            sh.screen = shell::Screen::Results;
            sh.cursor = 0;
        }

        // ---- fixed-step simulation -------------------------------------------
        bool menus = sh.screen == shell::Screen::Title || sh.screen == shell::Screen::Setup ||
                     sh.screen == shell::Screen::HowTo || sh.screen == shell::Screen::Options;
        bool sim_active = menus || sh.screen == shell::Screen::Match;

        // Attract demo never ends: rotate to a fresh match at MatchEnd.
        if (menus && state.match.phase == Phase::MatchEnd)
            start_attract();

        if (sim_active && (mode == LoopMode::Running || step_request)) {
            int steps = 0;
            if (mode == LoopMode::Running) {
                accumulator += frame_ns;
            } else {
                accumulator = DT_NS; // paused single-step: exactly one tick
            }
            while (accumulator >= DT_NS && steps < MAX_STEPS_PER_FRAME) {
                FrameInput tick_in = input;
                if (rec.replaying) {
                    if (rec.cursor < rec.inputs.size()) {
                        tick_in = rec.inputs[rec.cursor];
                    } else {
                        rec.replaying = false;
                        if (rec.status == 0)
                            rec.status = 1;
                        tick_in = input;
                    }
                }

                // CPU seats (assignment, not |=: human keys go dead on a CPU
                // seat). The ticklog carries these bits, so replays re-feed
                // them verbatim — bots stay out of the loop while replaying.
                // In menus the human keys navigate, so both seats are CPU.
                if (!rec.replaying) {
                    for (int p = 0; p < 2; ++p) {
                        if (menus || active_control[p] > 0) {
                            int lvl = menus ? 2 : active_control[p];
                            AiView av = ai_make_view(state, p);
                            tick_in.pressed[p] = ai_update(bots[p], av, cfg.ai[lvl - 1], cfg.chars);
                        }
                    }
                }

                sim::tick(state, tick_in, cfg.chars);

                if (rec.recording) {
                    rec.inputs.push_back(tick_in);
                    rec.checksums.push_back(state.checksum());
                }
                if (rec.replaying) {
                    if (state.checksum() != rec.checksums[rec.cursor]) {
                        rec.status = 2;
                        rec.diverge_tick = state.tick;
                        rec.replaying = false;
                    }
                    rec.cursor++;
                    if (rec.replaying && rec.cursor >= rec.inputs.size()) {
                        rec.replaying = false;
                        rec.status = 1;
                    }
                }

                accumulator -= DT_NS;
                ++steps;
            }
            if (accumulator >= DT_NS)
                accumulator = 0; // drop overflow (local play)
            if (mode == LoopMode::Paused)
                accumulator = 0;
            step_request = false;
        } else if (!sim_active) {
            accumulator = 0; // pause/results: don't fast-forward on resume
        }

        audio::update(snd, state);

        render::effects_update(fx, state, (float)frame_ns / 1e9f);
        if (!settings.screenshake) {
            fx.shake_mag = 0.0f;
            fx.shake_x = 0.0f;
            fx.shake_y = 0.0f;
        }

        view.paused = mode == LoopMode::Paused;
        view.recording = rec.recording;
        view.replaying = rec.replaying;
        view.replay_status = rec.status;
        view.diverge_tick = rec.diverge_tick;
        view.cpu_p0 = active_control[0];
        view.cpu_p1 = active_control[1];
        view.debug_hud = settings.debug_hud;
        view.fps = fps;
        render::draw_frame(app.renderer, state, cfg.chars, sheets, fx, view);
        render::draw_shell(app.renderer, sh, fx);
        SDL_RenderPresent(app.renderer);
    }

    data::save_settings(base_dir, settings);
    render::unload_sprite_sheet(sheets[0]);
    render::unload_sprite_sheet(sheets[1]);
    audio::shutdown(snd);
    return 0;
}
