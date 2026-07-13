#include "shell/shell.h"

namespace neg {
namespace shell {

static int wrap(int v, int n) {
    if (v < 0) return n - 1;
    if (v >= n) return 0;
    return v;
}

static void toggle_char(CharId& c) {
    c = c == CharId::Breaker ? CharId::Ballerina : CharId::Breaker;
}

static Action update_title(Shell& sh, const Events& ev) {
    if (ev.up) sh.cursor = wrap(sh.cursor - 1, TITLE_ITEMS);
    if (ev.down) sh.cursor = wrap(sh.cursor + 1, TITLE_ITEMS);
    if (ev.back) return Action::QuitApp;
    if (!ev.confirm) return Action::None;
    switch (sh.cursor) {
    case 0: // VS PLAYER
    case 1: // VS CPU
    case 2: // WATCH (CPU vs CPU)
        sh.setup.control[0] = sh.cursor == 2 ? 2 : 0;
        sh.setup.control[1] = sh.cursor == 0 ? 0 : 2; // CPU defaults to Normal
        sh.screen = Screen::Setup;
        sh.cursor = SETUP_ROWS - 1; // start on START for one-press flow
        return Action::None;
    case 3:
        sh.screen = Screen::HowTo;
        sh.page = 0;
        return Action::None;
    case 4:
        sh.screen = Screen::Options;
        sh.cursor = 0;
        return Action::None;
    default:
        return Action::QuitApp;
    }
}

// left/right (or confirm, as a QoL alias on value rows) cycles the value.
static Action update_setup(Shell& sh, const Events& ev) {
    if (ev.up) sh.cursor = wrap(sh.cursor - 1, SETUP_ROWS);
    if (ev.down) sh.cursor = wrap(sh.cursor + 1, SETUP_ROWS);
    if (ev.back) {
        sh.screen = Screen::Title;
        sh.cursor = 0;
        return Action::None;
    }
    bool cycle = ev.left || ev.right || (ev.confirm && sh.cursor != SETUP_ROWS - 1);
    int dir = ev.left ? -1 : 1;
    if (cycle) {
        switch (sh.cursor) {
        case 0: toggle_char(sh.setup.chars[0]); break;
        case 1: sh.setup.control[0] = wrap(sh.setup.control[0] + dir, 4); break;
        case 2: toggle_char(sh.setup.chars[1]); break;
        case 3: sh.setup.control[1] = wrap(sh.setup.control[1] + dir, 4); break;
        case 4: sh.setup.rounds_idx = wrap(sh.setup.rounds_idx + dir, 3); break;
        default: break;
        }
    }
    if (ev.confirm && sh.cursor == SETUP_ROWS - 1) {
        sh.screen = Screen::Match;
        return Action::StartMatch;
    }
    return Action::None;
}

static Action update_howto(Shell& sh, const Events& ev) {
    if (ev.left) sh.page = wrap(sh.page - 1, HOWTO_PAGES);
    if (ev.right) sh.page = wrap(sh.page + 1, HOWTO_PAGES);
    if (ev.confirm) {
        if (sh.page < HOWTO_PAGES - 1) {
            sh.page++;
        } else {
            sh.screen = Screen::Title;
            sh.cursor = 3;
        }
    }
    if (ev.back) {
        sh.screen = Screen::Title;
        sh.cursor = 3;
    }
    return Action::None;
}

static Action update_options(Shell& sh, const Events& ev) {
    data::Settings& st = *sh.settings;
    if (ev.up) sh.cursor = wrap(sh.cursor - 1, OPTIONS_ROWS);
    if (ev.down) sh.cursor = wrap(sh.cursor + 1, OPTIONS_ROWS);
    bool leave = ev.back || (ev.confirm && sh.cursor == OPTIONS_ROWS - 1);
    if (leave) {
        sh.screen = Screen::Title;
        sh.cursor = 4;
        return Action::SaveSettings;
    }
    bool cycle = ev.left || ev.right || (ev.confirm && sh.cursor != OPTIONS_ROWS - 1);
    if (!cycle) return Action::None;
    switch (sh.cursor) {
    case 0: {
        int v = st.volume + (ev.left ? -1 : 1);
        st.volume = v < 0 ? 0 : (v > 10 ? 10 : v); // clamps, no wrap
        break;
    }
    case 1: st.screenshake = !st.screenshake; break;
    case 2: st.fullscreen = !st.fullscreen; break;
    case 3: st.debug_hud = !st.debug_hud; break;
    default: break;
    }
    return Action::ApplySettings;
}

static Action update_pause(Shell& sh, const Events& ev) {
    if (ev.up) sh.cursor = wrap(sh.cursor - 1, PAUSE_ITEMS);
    if (ev.down) sh.cursor = wrap(sh.cursor + 1, PAUSE_ITEMS);
    if (ev.back) {
        sh.screen = Screen::Match;
        return Action::ResumeMatch;
    }
    if (!ev.confirm) return Action::None;
    switch (sh.cursor) {
    case 0:
        sh.screen = Screen::Match;
        return Action::ResumeMatch;
    case 1:
        sh.screen = Screen::Match;
        return Action::RestartMatch;
    case 2:
        sh.screen = Screen::Setup;
        sh.cursor = SETUP_ROWS - 1;
        return Action::ToTitle; // re-arm the attract demo behind the setup menu
    default:
        sh.screen = Screen::Title;
        sh.cursor = 0;
        return Action::ToTitle;
    }
}

static Action update_results(Shell& sh, const Events& ev) {
    if (ev.up) sh.cursor = wrap(sh.cursor - 1, RESULT_ITEMS);
    if (ev.down) sh.cursor = wrap(sh.cursor + 1, RESULT_ITEMS);
    if (ev.back) {
        sh.screen = Screen::Title;
        sh.cursor = 0;
        return Action::ToTitle;
    }
    if (!ev.confirm) return Action::None;
    switch (sh.cursor) {
    case 0:
        sh.screen = Screen::Match;
        return Action::RestartMatch;
    case 1:
        sh.screen = Screen::Setup;
        sh.cursor = SETUP_ROWS - 1;
        return Action::ToTitle;
    default:
        sh.screen = Screen::Title;
        sh.cursor = 0;
        return Action::ToTitle;
    }
}

Action update(Shell& sh, const Events& ev) {
    switch (sh.screen) {
    case Screen::Title: return update_title(sh, ev);
    case Screen::Setup: return update_setup(sh, ev);
    case Screen::HowTo: return update_howto(sh, ev);
    case Screen::Options: return update_options(sh, ev);
    case Screen::Match:
        if (ev.back) { // Esc: pause menu (F5 stays the debug freeze)
            sh.screen = Screen::Pause;
            sh.cursor = 0;
        }
        return Action::None;
    case Screen::Pause: return update_pause(sh, ev);
    case Screen::Results: return update_results(sh, ev);
    }
    return Action::None;
}

} // namespace shell
} // namespace neg
