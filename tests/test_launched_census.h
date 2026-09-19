#pragma once

// GO, then census the world the launch ADOPTED — at its opening tick,
// before the first blows — and abort back out.
//
// It lived in tests/integration/test_lineup_ui.cpp's anonymous namespace
// until the SETUP wizard's own GO story wanted the same oracle in another
// binary. The rule this header carries is not "count some walkers": it is
// WHERE the count may be taken (a one-shot main-thread window inside
// gameplay, because a poll from an injector thread cannot promise the sim
// is between ticks), WHEN (the first completed frame, so the shape is the
// staged one), and how the armed observer and the injector's disarm avoid
// racing over a dead stack frame. A second copy of that would be a second
// chance to get it subtly wrong.
//
// The caller owns the door: this presses "go" on whatever screen is up.

#include <SDL3/SDL.h>

#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/pause_menu.h>

#include "test_input_helpers.h"
#include "test_interact.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

// The launch observables (picker.cpp): the outcome flows ride these to tell
// the world GO adopted from the page that composed it.
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
extern std::atomic<int> g_test_game_frame_ticks;
// One-shot main-thread window inside gameplay (picker.cpp): the only seam
// where an injector can read the launched world with the sim between ticks.
void picker_testing_observe_next_game_frame(std::function<void()> observer);

inline constexpr int kOutcomeStartTimeoutMs = 30000;
inline constexpr int kOutcomeAbortTimeoutMs = 20000;

// The solved squad is separable from the map's own units by LEVEL alone:
// gladiator scen 1 ships its twelve at L1/L2, and the D22 solver's answer
// against a level-50 hero never lands under L5 (WEAK solves L5, BRUTAL
// L6+2). Every assertion that uses the split also pins the member count, so
// a mis-split fails loudly as a count rather than quietly as an f-sum.
inline constexpr int kSolvedSquadLevelFloor = 3;

// f(w): the §4.1 power metric, the test's own oracle. Deliberately a
// re-derivation of packs/core/lib/lineup.lua stat_power over measured_base
// and NOT a call into the pack seam — a squad solved against a broken metric
// must not be able to agree with itself. og.div truncates toward zero
// (og_div, script_host.cpp) and every stat getter is a float the model
// truncates at the boundary.
inline long long walker_f(const walker& w)
{
    const statistics* const st = w.stats();
    if (st == nullptr)
        return 0;
    const long long hp =
        static_cast<long long>(std::trunc(st->max_hitpoints()));
    const long long mp =
        static_cast<long long>(std::trunc(st->max_magicpoints()));
    const long long armor = static_cast<long long>(std::trunc(st->armor()));
    const long long dmg = static_cast<long long>(std::trunc(w.damage()));
    const long long sp = static_cast<long long>(std::trunc(w.stepsize()));
    const long long ff = std::max<long long>(
        1, static_cast<long long>(std::trunc(w.fire_frequency())));
    const long long level = static_cast<long long>(st->level());
    const long long ed = dmg * (level + 3) / 4;
    const long long rate = 120 / ff;
    const long long off = ed * rate + 5 * sp;
    const long long ehp = hp + 4 * armor + mp / 2;
    return ehp * (off + 60) / 60;
}

struct LaunchedTeamCensus
{
    int livings = 0;   // live Livings wearing this team's colour
    int guys = 0;      // ... carrying a roster character (the company)
    int bots = 0;      // ... carrying none (map troops and solved squads)
    int squad = 0;     // ... bots the solver levelled (see the floor above)
    long long guy_f = 0;
    long long squad_f = 0;
};

struct LaunchedCensus
{
    bool captured = false;
    std::array<LaunchedTeamCensus, 4> teams{};
    long long reference = 0;  // the weakest human team's f-sum (B3)
    int foes_of_hero = 0;
    int frame_ticks = 0;
};

// Shared-owned so the armed observer and the injector's disarm can never
// race over a dead stack frame (see go_and_census).
struct CensusHandoff
{
    LaunchedCensus census;
    std::atomic<bool> done{false};
};

// Walk the launched world on the GAME thread, between two ticks. The
// presenter handshake the menu captures use has nothing to freeze here (the
// game loop's redraw is compiled out under TESTING) and the main-thread task
// queue only pumps under a menu, so the read rides
// picker_testing_observe_next_game_frame instead: the sim is not mutating
// oblist while this runs, which a poll from the injector thread could never
// promise.
inline void census_launched_world_here(LaunchedCensus& out)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    if (scr == nullptr)
        return;
    walker* hero = nullptr;
    {
        for (const auto& uptr : scr->world().oblist) {
            walker* const w = uptr.get();
            if (w == nullptr || w->dead() ||
                w->query_order() != Order::Living)
            {
                continue;
            }
            const int team = static_cast<int>(w->team_num());
            if (team < 0 || team >= 4)
                continue;
            LaunchedTeamCensus& tc =
                out.teams[static_cast<std::size_t>(team)];
            ++tc.livings;
            if (w->myguy != nullptr) {
                ++tc.guys;
                tc.guy_f += walker_f(*w);
                if (hero == nullptr && team == 0)
                    hero = w;
            } else {
                ++tc.bots;
                if (w->stats() != nullptr &&
                    w->stats()->level() >= kSolvedSquadLevelFloor)
                {
                    ++tc.squad;
                    tc.squad_f += walker_f(*w);
                }
            }
        }
        for (const LaunchedTeamCensus& tc : out.teams) {
            if (tc.guy_f > 0 && (out.reference == 0 ||
                                 tc.guy_f < out.reference))
            {
                out.reference = tc.guy_f;
            }
        }
        if (hero != nullptr)
            out.foes_of_hero = scr->world().remaining_foes(hero);
    }
    out.captured = hero != nullptr;
}

// One Esc opens the PAUSED menu; the queued Quit outcome stands in for
// clicking QUIT MISSION and confirming it (the fairy-death idiom).
inline void quit_the_mission()
{
    if (!g_test_in_game.load(std::memory_order_acquire))
        return;
    og::ui::pause_menu_testing_clear_queue();
    og::ui::pause_menu_testing_queue_outcome(og::ui::PauseMenuResult::Quit,
                                             /*release_pause=*/false);
    inject_key_press(SDLK_ESCAPE);
    for (int waited = 0; waited < kOutcomeAbortTimeoutMs &&
                         g_test_in_game.load(std::memory_order_acquire);
         waited += 50)
    {
        SDL_Delay(50);
    }
    og::ui::pause_menu_testing_clear_queue();
}

// GO from the team menu, census the world it adopted at its opening tick,
// then abort back out. The census rides the FIRST frame so the count is the
// staged shape and not whatever the first exchange of blows left behind.
// `door` is the id the launch is pressed on: the Base Camp strip's own GO
// by default, or the SETUP wizard's GO row, which reaches the same strip
// button through the wizard's deferred-GO fold (D20).
inline bool go_and_census(LaunchedCensus& out, const char* door = "go")
{
    const int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    interact(door);
    for (int waited = 0; waited < kOutcomeStartTimeoutMs; waited += 25) {
        if (g_test_game_epoch.load(std::memory_order_acquire) != epoch_before)
            break;
        SDL_Delay(25);
    }
    if (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before) {
        fprintf(stderr, "  [launch] GO never launched a level\n");
        return false;
    }
    // The census must land on the world the launch ADOPTED, before the first
    // blows: the observer fires on the very next completed frame, so the
    // shape it counts is the staged one. The handoff is shared-owned, not a
    // reference to this stack frame — a level that ends before its first
    // frame leaves the lambda armed until the disarm below, and the two can
    // race for the same instant.
    auto handoff = std::make_shared<CensusHandoff>();
    picker_testing_observe_next_game_frame([handoff] {
        census_launched_world_here(handoff->census);
        handoff->census.frame_ticks =
            g_test_game_frame_ticks.load(std::memory_order_acquire);
        handoff->done.store(true, std::memory_order_release);
    });
    for (int waited = 0; waited < kOutcomeStartTimeoutMs; waited += 5) {
        if (handoff->done.load(std::memory_order_acquire))
            break;
        if (!g_test_in_game.load(std::memory_order_acquire))
            break;
        SDL_Delay(5);
    }
    picker_testing_observe_next_game_frame(nullptr);
    if (handoff->done.load(std::memory_order_acquire))
        out = handoff->census;
    const bool ok = handoff->done.load(std::memory_order_acquire) &&
                    out.captured;
    fprintf(stderr,
            "  [launch] launched census at tick %d: t0 %d/%d t1 %d(+%d squad)"
            " t2 %d(+%d squad) ref=%lld t1_squad_f=%lld t2_squad_f=%lld\n",
            out.frame_ticks, out.teams[0].guys, out.teams[0].bots,
            out.teams[1].livings, out.teams[1].squad, out.teams[2].livings,
            out.teams[2].squad, out.reference, out.teams[1].squad_f,
            out.teams[2].squad_f);
    quit_the_mission();
    return ok;
}

