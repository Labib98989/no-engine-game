// Classic int main(), fixed-timestep 60 Hz loop with accumulator and
// anti-spiral clamps — research.md §2.1. The window is just one of three
// front-ends onto neg_sim (build-plan.md §3).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <vector>
#include <string>
#include <fstream>

#include "sim/simulation.h"
#include "sim/chardata.h"
#include "data/loader.h"
#include "platform/app.h"
#include "platform/input.h"
#include "render/draw.h"
#include "render/sprites.h"
#include "audio/audio.h"

using namespace neg;

static constexpr uint64_t DT_NS = 16'666'667ULL;        // 60 Hz
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
    if (!f.is_open()) return;
    f << "# neg ticklog v1 - one line per tick: <p0bits> <p1bits>\n";
    f << "seed " << MATCH_SEED << "\n";
    f << "ticks " << rec.inputs.size() << "\n";
    for (const FrameInput& in : rec.inputs)
        f << (int)in.pressed[0] << " " << (int)in.pressed[1] << "\n";
}

int main(int, char**) {
    platform::App app("No Engine Game - rhythm yomi duel [placeholder art]", 1280, 720);
    if (!app.ok) return 1;

    const char* base = SDL_GetBasePath();
    data::GameConfig cfg = data::load_config(base ? base : "");
    SDL_Log("config: %s", cfg.notes.c_str());

    SimulationState state{};
    sim::init_state(state, cfg.chars, cfg.tune, MATCH_SEED);

    // Sprite sheets are render-side; an invalid sheet (no JSON block or missing
    // PNG) makes draw_fighter fall back to the placeholder rectangle.
    std::string base_dir = base ? base : "";
    render::SpriteSheet sheets[2] = {
        render::load_sprite_sheet(app.renderer, base_dir, "assets/characters/breaker.json"),
        render::load_sprite_sheet(app.renderer, base_dir, "assets/characters/ballerina.json"),
    };
    SDL_Log("sprites: breaker=%s ballerina=%s", sheets[0].valid ? "loaded" : "rect",
            sheets[1].valid ? "loaded" : "rect");

    audio::Audio* snd = audio::init();

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
        if (frame_ns > MAX_FRAME_NS) frame_ns = MAX_FRAME_NS;
        if (frame_ns > 0) fps = fps * 0.95f + 0.05f * (1e9f / (float)frame_ns);

        platform::poll_events(input, ui);
        if (ui.quit) quit = true;
        if (ui.toggle_overlay) view.overlay = !view.overlay;
        if (ui.toggle_pause) mode = mode == LoopMode::Paused ? LoopMode::Running : LoopMode::Paused;
        if (ui.step && mode == LoopMode::Paused) step_request = true;
        if (ui.resume) mode = LoopMode::Running;
        if (ui.restart && state.match.phase == Phase::MatchEnd)
            sim::init_state(state, cfg.chars, cfg.tune, MATCH_SEED);

        // F9: arm/stop recording. Arming restarts the match from the fixed
        // seed so the input log alone reproduces the session (headless
        // cross-check, build-plan.md S5).
        if (ui.toggle_record && !rec.replaying) {
            if (!rec.recording) {
                rec = Recorder{};
                rec.recording = true;
                sim::init_state(state, cfg.chars, cfg.tune, MATCH_SEED);
            } else {
                rec.recording = false;
                write_ticklog(rec, base ? base : "");
            }
        }
        // F10: replay the last recording from the same fresh state,
        // verifying per-tick checksums (the determinism harness).
        if (ui.replay && !rec.recording && !rec.inputs.empty()) {
            rec.replaying = true;
            rec.cursor = 0;
            rec.status = 0;
            sim::init_state(state, cfg.chars, cfg.tune, MATCH_SEED);
        }

        if (mode == LoopMode::Running || step_request) {
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
                        if (rec.status == 0) rec.status = 1;
                        tick_in = input;
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
            if (accumulator >= DT_NS) accumulator = 0; // drop overflow (local play)
            if (mode == LoopMode::Paused) accumulator = 0;
            step_request = false;
        }

        audio::update(snd, state);

        view.paused = mode == LoopMode::Paused;
        view.recording = rec.recording;
        view.replaying = rec.replaying;
        view.replay_status = rec.status;
        view.diverge_tick = rec.diverge_tick;
        view.fps = fps;
        render::draw_frame(app.renderer, state, cfg.chars, sheets, view);
        SDL_RenderPresent(app.renderer);
    }

    render::unload_sprite_sheet(sheets[0]);
    render::unload_sprite_sheet(sheets[1]);
    audio::shutdown(snd);
    return 0;
}
