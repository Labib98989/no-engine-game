// neg_headless — scripted deterministic runner (build-plan.md §3.2).
// Links neg_sim only: no SDL, no JSON. Drives sim::tick from a plain-text
// scenario and prints the beat log + final checksum.
//
//   neg_headless replays/script.txt [--checksums] [--verify] [--beats N]
//   neg_headless --ticklog session_ticks.txt [--checksums]
//
// Beat-script format (one line per beat, starting at beat 1):
//   <P0> <P1>     where each is  Input,Tier  or  -
//   e.g.  A,Perfect  B,Normal
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>

#include "sim/simulation.h"
#include "sim/chardata.h"

using namespace neg;

static const char* input_name(Input i) {
    switch (i) {
    case Input::A: return "A";
    case Input::B: return "B";
    case Input::C: return "C";
    case Input::D: return "D";
    default: return "-";
    }
}

static const char* tier_name(Tier t) {
    switch (t) {
    case Tier::Perfect: return "Perfect";
    case Tier::Normal: return "Normal";
    default: return "Miss";
    }
}

static const char* outcome_name(Outcome o) {
    switch (o) {
    case Outcome::BothWhiff: return "BothWhiff";
    case Outcome::OneLands: return "OneLands";
    case Outcome::RpsDecided: return "RpsDecided";
    case Outcome::SameTypeTie: return "SameTypeTie";
    case Outcome::BothMiss: return "BothMiss";
    case Outcome::ComboContinue: return "ComboContinue";
    case Outcome::ComboBreak: return "ComboBreak";
    case Outcome::ComboSteal: return "ComboSteal";
    case Outcome::ComboCapEnd: return "ComboCapEnd";
    case Outcome::ComboMissEnd: return "ComboMissEnd";
    case Outcome::Launch: return "Launch";
    case Outcome::GroundOut: return "GroundOut";
    default: return "None";
    }
}

struct ScheduledPress {
    uint64_t tick;
    int player;
    Input input;
};

static bool parse_commit(const std::string& tok, Input& in, Tier& tier) {
    if (tok == "-") { in = Input::None; return true; }
    size_t comma = tok.find(',');
    if (comma == std::string::npos || comma == 0) return false;
    char c = tok[0];
    switch (c) {
    case 'A': case 'a': in = Input::A; break;
    case 'B': case 'b': in = Input::B; break;
    case 'C': case 'c': in = Input::C; break;
    case 'D': case 'd': in = Input::D; break;
    default: return false;
    }
    std::string t = tok.substr(comma + 1);
    if (t == "Perfect" || t == "perfect") tier = Tier::Perfect;
    else if (t == "Normal" || t == "normal") tier = Tier::Normal;
    else if (t == "Miss" || t == "miss") tier = Tier::Miss;
    else return false;
    return true;
}

// Tier -> press offset from the beat instant (default tuning bands:
// 0 = Perfect, 6 = Normal, 13 = sloppy Miss).
static int64_t tier_offset(Tier t) {
    switch (t) {
    case Tier::Perfect: return 0;
    case Tier::Normal: return 6;
    default: return 13;
    }
}

static int run(const std::vector<ScheduledPress>& presses, uint64_t total_ticks, uint64_t seed,
               bool skip_intro, bool print_checksums, bool quiet, uint64_t* out_checksum,
               std::vector<uint64_t>* per_tick) {
    CharacterData chars[2] = {default_character(CharId::Breaker),
                              default_character(CharId::Ballerina)};
    Tuning tune = default_tuning();
    SimulationState s{};
    sim::init_state(s, chars, tune, seed, skip_intro);

    std::map<uint64_t, FrameInput> schedule;
    for (const ScheduledPress& p : presses)
        schedule[p.tick].pressed[p.player] |= (uint8_t)(1u << ((int)p.input - 1));

    if (!quiet)
        printf("beat | P1            | P2            | outcome       | dmg P1/P2 | hp P1/P2  | state\n");

    for (uint64_t t = 1; t <= total_ticks; ++t) {
        FrameInput in{};
        auto it = schedule.find(t);
        if (it != schedule.end()) in = it->second;
        sim::tick(s, in, chars);

        uint64_t cs = s.checksum();
        if (per_tick) per_tick->push_back(cs);
        if (print_checksums) printf("tick %llu checksum %016llx\n", (unsigned long long)s.tick,
                                    (unsigned long long)cs);

        if (!quiet && s.last_result.resolved_tick == s.tick &&
            s.last_result.outcome != Outcome::None) {
            const ResolutionResult& r = s.last_result;
            char st[32];
            if (s.duel.macro == Macro::Advantage)
                snprintf(st, sizeof st, "ADV P%d x%d%s", s.duel.attacker + 1, s.duel.combo_count,
                         r.air ? " AIR" : "");
            else
                snprintf(st, sizeof st, "NEUTRAL");
            printf("%4u | %s,%-11s | %s,%-11s | %-13s | %4d/%-4d | %4d/%-4d | %s\n", r.beat,
                   input_name(r.p0_input), tier_name(r.p0_tier), input_name(r.p1_input),
                   tier_name(r.p1_tier), outcome_name(r.outcome), r.damage_p0, r.damage_p1,
                   s.fighters[0].health, s.fighters[1].health, st);
        }
    }

    if (out_checksum) *out_checksum = s.checksum();
    return 0;
}

int main(int argc, char** argv) {
    std::string script, ticklog;
    bool checksums = false, verify = false;
    uint64_t beats_limit = 0;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--checksums") checksums = true;
        else if (a == "--verify") verify = true;
        else if (a == "--ticklog" && i + 1 < argc) ticklog = argv[++i];
        else if (a == "--beats" && i + 1 < argc) beats_limit = (uint64_t)atoll(argv[++i]);
        else if (a[0] != '-') script = a;
        else {
            fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 1;
        }
    }
    if (script.empty() && ticklog.empty()) {
        fprintf(stderr,
                "usage: neg_headless <script.txt> [--checksums] [--verify] [--beats N]\n"
                "       neg_headless --ticklog <session_ticks.txt> [--checksums]\n");
        return 1;
    }

    std::vector<ScheduledPress> presses;
    uint64_t total_ticks = 0;
    uint64_t seed = 0xC0FFEEULL;
    bool skip_intro = true;
    Tuning tune = default_tuning();
    uint16_t tpb = (uint16_t)(3600 / tune.bpm);

    if (!ticklog.empty()) {
        // Per-tick input log written by the game's F9 recorder.
        std::ifstream f(ticklog);
        if (!f.is_open()) {
            fprintf(stderr, "cannot open %s\n", ticklog.c_str());
            return 1;
        }
        skip_intro = false; // the game records from a fresh match incl. intro
        std::string line;
        uint64_t t = 0;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string key;
            ss >> key;
            if (key == "seed") { ss >> seed; continue; }
            if (key == "ticks") continue;
            int b0 = atoi(key.c_str()), b1 = 0;
            ss >> b1;
            ++t;
            for (int bit = 0; bit < 4; ++bit) {
                if (b0 & (1 << bit)) presses.push_back({t, 0, (Input)(bit + 1)});
                if (b1 & (1 << bit)) presses.push_back({t, 1, (Input)(bit + 1)});
            }
        }
        total_ticks = t;
    } else {
        std::ifstream f(script);
        if (!f.is_open()) {
            fprintf(stderr, "cannot open %s\n", script.c_str());
            return 1;
        }
        std::string line;
        uint32_t beat = 1; // beat 0's window is truncated; scripts start at 1
        while (std::getline(f, line)) {
            size_t hash = line.find('#');
            if (hash != std::string::npos) line = line.substr(0, hash);
            std::istringstream ss(line);
            std::string t0, t1;
            if (!(ss >> t0 >> t1)) continue;
            Input i0 = Input::None, i1 = Input::None;
            Tier r0 = Tier::Normal, r1 = Tier::Normal;
            if (!parse_commit(t0, i0, r0) || !parse_commit(t1, i1, r1)) {
                fprintf(stderr, "bad script line: %s\n", line.c_str());
                return 1;
            }
            uint64_t instant = (uint64_t)beat * tpb;
            if (i0 != Input::None) presses.push_back({instant + tier_offset(r0), 0, i0});
            if (i1 != Input::None) presses.push_back({instant + tier_offset(r1), 1, i1});
            beat++;
            if (beats_limit && beat > beats_limit) break;
        }
        total_ticks = (uint64_t)(beat + 1) * tpb; // run past the last resolution
    }

    uint64_t cs1 = 0;
    run(presses, total_ticks, seed, skip_intro, checksums, false, &cs1, nullptr);
    printf("final checksum %016llx\n", (unsigned long long)cs1);

    if (verify) {
        // Determinism harness: same inputs twice, per-tick checksums must match.
        std::vector<uint64_t> a, b;
        run(presses, total_ticks, seed, skip_intro, false, true, nullptr, &a);
        run(presses, total_ticks, seed, skip_intro, false, true, nullptr, &b);
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                printf("VERIFY FAIL: divergence at tick %llu\n", (unsigned long long)(i + 1));
                return 2;
            }
        }
        printf("VERIFY OK: %llu ticks, checksums identical\n", (unsigned long long)a.size());
    }
    return 0;
}
