// The meta-game: title, match setup, how-to, options, pause, results. Pure
// front-end state machine over menu-nav events — it never touches the sim;
// main.cpp interprets the returned Action (init a match, save settings, quit).
// While any menu screen is up, an attract-mode CPU-vs-CPU match plays dimmed
// behind it (main owns that sim; the shell only says which screen is active).
#pragma once
#include "sim/types.h"
#include "data/settings.h"

namespace neg {
namespace shell {

enum class Screen : uint8_t { Title, Setup, HowTo, Options, Match, Pause, Results };

// What main must do this frame as a result of menu input.
enum class Action : uint8_t {
    None,
    StartMatch,   // read Shell::setup, init the sim, screen is already Match
    ResumeMatch,  // leave pause, keep the sim as-is
    RestartMatch, // re-init the sim with the current setup
    ToTitle,      // back to menus; main re-arms the attract demo
    ApplySettings, // a settings value changed; apply live (volume/fullscreen)
    SaveSettings, // leaving Options: persist to disk
    QuitApp,
};

// Per-seat control: 0 = human, 1..3 = CPU Easy/Normal/Hard.
struct MatchSetup {
    CharId chars[2] = {CharId::Breaker, CharId::Ballerina};
    int control[2] = {0, 0};
    int rounds_idx = 1; // 0/1/2 -> best-of-1/3/5 (rounds_to_win 1/2/3)
};

struct Shell {
    Screen screen = Screen::Title;
    int cursor = 0;
    int page = 0; // how-to page, 0..HOWTO_PAGES-1
    MatchSetup setup;
    data::Settings* settings = nullptr; // owned by main; Options edits it live
};

struct Events {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, back = false;
};

constexpr int HOWTO_PAGES = 2;

// Advance the shell one frame. Returns at most one Action.
Action update(Shell& sh, const Events& ev);

inline uint8_t rounds_to_win(const MatchSetup& m) { return (uint8_t)(m.rounds_idx + 1); }

// Row/item counts, shared with the renderer so cursor and layout agree.
constexpr int TITLE_ITEMS = 6;  // VS PLAYER / VS CPU / WATCH / HOW TO / OPTIONS / QUIT
constexpr int SETUP_ROWS = 6;   // P1 char / P1 control / P2 char / P2 control / rounds / START
constexpr int OPTIONS_ROWS = 5; // volume / shake / fullscreen / debug hud / BACK
constexpr int PAUSE_ITEMS = 4;  // resume / restart / setup / title
constexpr int RESULT_ITEMS = 3; // rematch / setup / title

} // namespace shell
} // namespace neg
