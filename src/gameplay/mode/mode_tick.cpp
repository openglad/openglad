/* Scripted-mode (TYPE_SCRIPTED) engine.
 *
 * Owns the lazy on_mode_init activation latch, the per-tick win-latch
 * re-assert (GameWorld::tick zeroes game_ended/ending/next_level at entry —
 * the trap that has bitten twice), the engine respawn timers, and the
 * on_mode_tick dispatch. All match POLICY lives in campaign-pack Lua; this
 * file is the fixed frame those hooks run in. Everything here is
 * deterministic and draws no world RNG.
 */

#include <openglad/gameplay/mode/mode_state.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <bit>

namespace og::sim {

namespace {

// Band color names, index-parallel to kFfaRampBases (docs/ffa-design.md §4).
constexpr std::array<const char*, 16> kFfaBandNames = {
    "RED",      "GREEN",  "BLUE",   "YELLOW", "MAGENTA", "CYAN",
    "TAN",      "ROSE",   "LAVENDER", "SALMON", "ORANGE", "PINK",
    "VIOLET",   "TEAL",   "GOLD",   "SLATE"};

static_assert(kFfaRampBases.size() == static_cast<std::size_t>(kFfaTeamCount));
static_assert(kFfaBandNames.size() == kFfaRampBases.size());

}  // namespace

const char* team_color_name(int team)
{
    if (kFfaTeamBase <= team && team < kFfaTeamBase + kFfaTeamCount)
        return kFfaBandNames[static_cast<std::size_t>(team - kFfaTeamBase)];
    switch (team)
    {
        case 0: return "RED";
        case 1: return "GREEN";
        case 2: return "BLUE";
        default: return "YELLOW";
    }
}

bool is_scoring_identity(int t)
{
    return t < SCORE_TEAM_COUNT ||
           (kFfaTeamBase <= t && t < kFfaTeamBase + kFfaTeamCount);
}

unsigned char team_ramp_base(int team)
{
    if (kFfaTeamBase <= team && team < kFfaTeamBase + kFfaTeamCount)
        return kFfaRampBases[static_cast<std::size_t>(team - kFfaTeamBase)];
    return static_cast<unsigned char>(team * 16 + 40);
}

bool mode_scripted_active(const GameWorld& world)
{
    return (world.type & GameWorld::TYPE_SCRIPTED) != 0 && world.mode.active;
}

std::uint8_t authored_team_mask(const GameWorld& world)
{
    // Dead markers included: the level bootstrap kills the markers it
    // consumes for player placement, and the mask must not depend on how
    // many seats deployed (same rule as the respawn anchor scan).
    std::uint8_t mask = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->query_order() != Order::Special ||
            w->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        const unsigned char team = w->team_num();
        if (team < SCORE_TEAM_COUNT)
        {
            mask = static_cast<std::uint8_t>(
                mask | (1u << static_cast<unsigned>(team)));
        }
    }
    return mask;
}

std::uint8_t effective_team_mask(std::uint8_t authored, int requested) noexcept
{
    const auto all_mask =
        static_cast<std::uint8_t>((1u << SCORE_TEAM_COUNT) - 1u);
    authored = static_cast<std::uint8_t>(authored & all_mask);
    if (requested <= 0)
        return authored;
    int remaining = std::clamp(requested, 2, static_cast<int>(SCORE_TEAM_COUNT));
    std::uint8_t effective = 0;
    for (int team = 0; team < SCORE_TEAM_COUNT && remaining > 0; ++team)
    {
        const auto bit =
            static_cast<std::uint8_t>(1u << static_cast<unsigned>(team));
        if ((authored & bit) == 0)
            continue;
        effective = static_cast<std::uint8_t>(effective | bit);
        --remaining;
    }
    return effective;
}

void mode_end_level(GameWorld& world, int ending, int next_level)
{
    // First arming flushes the respawn engine (D2): every queued entry
    // fires and eligible player corpses revive BEFORE any winner math, so
    // save-merge/rematch flows never see mid-respawn heroes. Re-arming
    // (last write wins) never re-flushes.
    if (!world.mode.win_latched)
        respawn_flush_revive_all(world);
    world.mode.win_latched = true;
    world.mode.win_ending = static_cast<std::int8_t>(ending);
    world.mode.win_next_level = static_cast<std::int16_t>(next_level);
}

void mode_declare_winner(GameWorld& world, int team)
{
    if (!world.mode.win_latched)
        respawn_flush_revive_all(world);
    world.mode.winner_team = static_cast<std::int8_t>(team);
    // winner_is_player: any live myguy walker wearing the winning team's
    // color — the EXACT ctf.cpp run_win_check semantics (team_num equality,
    // not is_friendly_to_team), computed AFTER the flush so just-revived
    // heroes count.
    bool winner_is_player = false;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            w->team_num() == static_cast<unsigned char>(team) &&
            w->myguy != nullptr)
        {
            winner_is_player = true;
            break;
        }
    }
    world.mode.winner_is_player = winner_is_player;
    world.mode.win_latched = true;
    world.mode.win_ending = 0;
    world.mode.win_next_level = static_cast<std::int16_t>(
        winner_is_player ? world.id + 1 : world.id);
}

namespace {

void assert_win_shape(GameWorld& world)
{
    world.game_ended = true;
    world.ending = world.mode.win_ending;
    world.next_level = world.mode.win_next_level;
}

} // namespace

void mode_stage_init(GameWorld& world)
{
    // The CTF activation discipline: latch init_attempted FIRST,
    // unconditionally; a failed init leaves the level to classic completion
    // rules. The latch also makes this callable from both the stager and
    // mode_run_tick's lazy arm without a second dispatch.
    if (world.mode.init_attempted)
        return;
    world.mode.init_attempted = true;
    // Engine-side prep BEFORE the Lua hook: resolve the respawn delay
    // (lobby request > default) and scan the team start markers into the
    // anchor arrays while the consumed-marker corpses are still in oblist
    // (at tick time the dead sweep runs after the completion fork; at stage
    // time dead-marked markers wait in oblist for the first tick's sweep).
    const short requested = world.ctf_requested_respawn_ticks;
    world.respawn.respawn_ticks =
        (requested > 0) ? static_cast<std::uint16_t>(requested)
                        : kRespawnDefaultTicks;
    respawn_scan_anchors(world);
    if (og::script::hooks::level_mode_init(&world))
        world.mode.active = true;
}

void mode_run_tick(GameWorld& world)
{
    // 0. Lazy init (a failed init owns its tick and the level falls to
    //    classic completion rules starting the NEXT tick). Staged worlds
    //    arrive with init_attempted already latched and skip this arm.
    if (!world.mode.active && !world.mode.init_attempted)
    {
        mode_stage_init(world);
        if (!world.mode.active)
            return;
    }
    if (!world.mode.active)
        return;

    // 1. Win latch — re-assert every tick (tick entry zeroed the fields);
    //    a decided match runs no further phases and no more Lua.
    if (world.mode.win_latched)
    {
        assert_win_shape(world);
        return;
    }

    // 2. Engine respawn timers: fires due entries in place through the
    //    RNG-free classic paths and dispatches on_respawn per fired revive
    //    — BEFORE on_mode_tick so the Lua tick observes this tick's
    //    revivals and their hook-side placement corrections.
    respawn_run_timers(world);

    // 3. The post-act Lua hook. A hook ERROR is deterministic and latched
    //    (R9); the mode stays active — a mid-match script bug must not
    //    silently flip the level to classic rules.
    og::script::hooks::level_mode_tick(&world);

    // 4. Fold a Lua-declared win into the world (og.end_level /
    //    og.declare_winner wrote the latch during step 3).
    if (world.mode.win_latched)
        assert_win_shape(world);
}

} // namespace og::sim
