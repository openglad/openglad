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
    // rule: Teams 0-3 are named RED, GREEN, BLUE, YELLOW; FFA fighter teams 16-31 take the sixteen
    //   band names; any other team number reads YELLOW.
    // tests: ModeTick.team_color_name_pins_the_sixteen_band_names
    // tests: ModeBindings.team_color_name_band_names_and_edges
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
    // rule: Only teams 0-3 and FFA fighter teams 16-31 can score; every other team number is not a
    //   scoring side.
    // tests: ModeTick.is_scoring_identity_truth_table
    return t < SCORE_TEAM_COUNT ||
           (kFfaTeamBase <= t && t < kFfaTeamBase + kFfaTeamCount);
}

unsigned char team_ramp_base(int team)
{
    // rule: An FFA fighter team paints with its band's palette ramp; any other team uses the
    //   classic team*16+40 ramp.
    // tests: ModeTick.team_ramp_base_pins_the_band_table
    // tests: ModeTick.team_ramp_base_keeps_the_classic_formula_off_band
    if (kFfaTeamBase <= team && team < kFfaTeamBase + kFfaTeamCount)
        return kFfaRampBases[static_cast<std::size_t>(team - kFfaTeamBase)];
    return static_cast<unsigned char>(team * 16 + 40);
}

bool mode_scripted_active(const GameWorld& world)
{
    // rule: A level plays by its scripted mode rules only when it is flagged scripted AND its
    //   on_mode_init succeeded; otherwise classic completion rules apply.
    // tests: ClassicRespawn.predicate_and_suppression_truth_table
    // tests: RespawnEngine.team_wipe_suppression_tracks_match_lifecycle
    return (world.type & GameWorld::TYPE_SCRIPTED) != 0 && world.mode.active;
}

std::uint8_t authored_team_mask(const GameWorld& world)
{
    // Dead markers included: the level bootstrap kills the markers it
    // consumes for player placement, and the mask must not depend on how
    // many seats deployed (same rule as the respawn anchor scan).
    std::uint8_t mask = 0;
    // rule: The level's authored teams are the teams 0-3 that have a team start marker placed,
    //   counting markers already consumed (dead) by player placement, so the mask never depends on
    //   how many seats deployed.
    // tests: ModeBindings.team_masks_follow_markers_and_requested_count
    // tests: ModeBindings.matched_troops_request_feeds_no_mask
    // tests: SemWorld.the_authored_team_mask_counts_consumed_markers_and_only_markers
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        // rule: The level's authored teams are the teams 0-3 that have a team start marker placed,
        //   counting markers already consumed (dead) by player placement, so the mask never depends
        //   on how many seats deployed.
        // tests: ModeBindings.team_masks_follow_markers_and_requested_count
        // tests: ModeBindings.matched_troops_request_feeds_no_mask
        // tests: SemWorld.the_authored_team_mask_counts_consumed_markers_and_only_markers
        if (w == nullptr || w->query_order() != Order::Special ||
            w->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        const unsigned char team = w->team_num();
        // rule: The level's authored teams are the teams 0-3 that have a team start marker placed,
        //   counting markers already consumed (dead) by player placement, so the mask never depends
        //   on how many seats deployed.
        // tests: ModeBindings.team_masks_follow_markers_and_requested_count
        // tests: ModeBindings.matched_troops_request_feeds_no_mask
        // tests: SemWorld.the_authored_team_mask_counts_consumed_markers_and_only_markers
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
    // rule: With no team count requested (Auto) every authored team plays; a requested count N
    //   (clamped to 2..4) activates the first N authored teams in index order, skipping unauthored
    //   team numbers.
    // tests: ModeBindings.team_masks_follow_markers_and_requested_count
    // tests: ModeBindings.matched_troops_request_feeds_no_mask
    // tests: LobbyState.ctf_team_domain_uses_authored_order_and_safe_fallback
    // tests: SemWorld.a_one_team_request_is_clamped_to_two_teams
    if (requested <= 0)
        return authored;
    int remaining = std::clamp(requested, 2, static_cast<int>(SCORE_TEAM_COUNT));
    std::uint8_t effective = 0;
    for (int team = 0; team < SCORE_TEAM_COUNT && remaining > 0; ++team)
    {
        const auto bit =
            static_cast<std::uint8_t>(1u << static_cast<unsigned>(team));
        // rule: With no team count requested (Auto) every authored team plays. Here: `(authored &
        //   bit) == 0`.
        // tests: ModeBindings.team_masks_follow_markers_and_requested_count
        // tests: ModeBindings.matched_troops_request_feeds_no_mask
        // tests: LobbyState.ctf_team_domain_uses_authored_order_and_safe_fallback
        // tests: SemWorld.a_one_team_request_is_clamped_to_two_teams
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
    // rule: The first time a scripted match is ended, every queued respawn fires and eligible
    //   player corpses revive before the result is recorded; ending it again (last write wins on
    //   ending/next_level) does not re-flush.
    // tests: ModeTick.end_level_loss_shape_reasserts, ModeBindings.end_level_latches_and_validates
    // tests: SemWorld.ending_a_match_flushes_respawns_once_and_never_again
    if (!world.mode.win_latched)
        respawn_flush_revive_all(world);
    world.mode.win_latched = true;
    world.mode.win_ending = static_cast<std::int8_t>(ending);
    world.mode.win_next_level = static_cast<std::int16_t>(next_level);
}

void mode_declare_winner(GameWorld& world, int team)
{
    // rule: Same rule as above: declaring a winner first flushes respawns and revives
    //   player corpses (only on first arming), so a just-revived hero counts for the winner check.
    // tests: ModeTick.latch_arming_revives_player_corpses_before_winner_check
    // tests: RespawnEngine.win_latch_flush_revives_all_player_corpses
    // tests: RespawnEngine.win_latch_flush_breaks_charm_onto_true_team
    // tests: SemWorld.a_winner_declared_after_the_latch_revives_nobody
    if (!world.mode.win_latched)
        respawn_flush_revive_all(world);
    world.mode.winner_team = static_cast<std::int8_t>(team);
    // winner_is_player: any live myguy walker wearing the winning team's
    // color — the EXACT ctf.cpp run_win_check semantics (team_num equality,
    // not is_friendly_to_team), computed AFTER the flush so just-revived
    // heroes count.
    bool winner_is_player = false;
    // rule: A declared winner is the player's win when any live hero (myguy) wears exactly that
    //   team number (not merely an ally of it); a player win advances to the next level, otherwise
    //   the level replays.
    // tests: ModeTick.declare_winner_is_one_shot_and_reasserted_every_tick
    // tests: ModeTick.latch_arming_revives_player_corpses_before_winner_check
    // tests: ModeBindings.declare_winner_records_team_and_validates
    // tests: ModeBindings.declare_winner_accepts_band_rejects_gap_and_beyond
    // tests: ModesTdm.bot_only_win_is_rematch_shape
    // tests: SemMode.a_declared_winner_is_the_players_only_with_a_live_hero_on_that_team
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        // rule: A declared winner is the player's win when any live hero (myguy) wears exactly that
        //   team number (not merely an ally of it).
        // tests: ModeTick.declare_winner_is_one_shot_and_reasserted_every_tick
        // tests: ModeTick.latch_arming_revives_player_corpses_before_winner_check
        // tests: ModeBindings.declare_winner_records_team_and_validates
        // tests: ModeBindings.declare_winner_accepts_band_rejects_gap_and_beyond
        // tests: ModesTdm.bot_only_win_is_rematch_shape
        // tests: SemMode.a_declared_winner_is_the_players_only_with_a_live_hero_on_that_team
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
    // rule: A scripted mode's on_mode_init runs at most once per level, whether staged or lazily
    //   armed on the first tick.
    // tests: ModeTick.staged_init_parity_with_ticked_init
    // tests: ModeTick.scripted_bit_plus_hooks_activates_once
    if (world.mode.init_attempted)
        return;
    world.mode.init_attempted = true;
    // Engine-side prep BEFORE the Lua hook: resolve the respawn delay
    // (lobby request > default) and scan the team start markers into the
    // anchor arrays while the consumed-marker corpses are still in oblist
    // (at tick time the dead sweep runs after the completion fork; at stage
    // time dead-marked markers wait in oblist for the first tick's sweep).
    const short requested = world.ctf_requested_respawn_ticks;
    // rule: A mode's respawn delay is the lobby's requested delay when one was set, else the engine
    //   default.
    // tests: ModeTick.stage_init_activates_without_a_tick
    // tests: ModeTick.stage_init_failed_shape_defaults_and_stays_classic
    world.respawn.respawn_ticks =
        (requested > 0) ? static_cast<std::uint16_t>(requested)
                        : kRespawnDefaultTicks;
    respawn_scan_anchors(world);
    // rule: A mode becomes active only if its on_mode_init succeeds; a missing or erroring init
    //   leaves the level on classic completion rules.
    // tests: ModeTick.stage_init_activates_without_a_tick
    // tests: ModeTick.scripted_bit_plus_hooks_activates_once
    // tests: ModeTick.erroring_mode_init_falls_classic_next_tick
    // tests: ModeTick.scripted_bit_without_hooks_falls_classic_next_tick
    if (og::script::hooks::level_mode_init(&world))
        world.mode.active = true;
}

void mode_run_tick(GameWorld& world)
{
    // 0. Lazy init (a failed init owns its tick and the level falls to
    //    classic completion rules starting the NEXT tick). Staged worlds
    //    arrive with init_attempted already latched and skip this arm.
    // rule: An un-staged scripted level arms its mode lazily on the first tick; a failed init owns
    //   that tick (no Lua tick runs) and the level falls to classic rules; a world whose init
    //   already failed never runs mode phases.
    // tests: ModeTick.run_tick_refuses_failed_init_world_when_called_directly
    // tests: ModeTick.scripted_bit_plus_hooks_activates_once
    if (!world.mode.active && !world.mode.init_attempted)
    {
        mode_stage_init(world);
        // rule: An un-staged scripted level arms its mode lazily on the first tick.
        // tests: ModeTick.run_tick_refuses_failed_init_world_when_called_directly
        // tests: ModeTick.scripted_bit_plus_hooks_activates_once
        if (!world.mode.active)
            return;
    }
    if (!world.mode.active)
        return;

    // 1. Win latch — re-assert every tick (tick entry zeroed the fields);
    //    a decided match runs no further phases and no more Lua.
    // rule: Once a scripted match is decided it stays decided: the end shape (game_ended, ending,
    //   next_level) is re-asserted every tick and no more respawn timers or Lua mode ticks run; a
    //   win declared inside this tick's on_mode_tick takes effect the same tick.
    // tests: ModeTick.declare_winner_is_one_shot_and_reasserted_every_tick
    // tests: ModeTick.end_level_loss_shape_reasserts
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
    // rule: Once a scripted match is decided it stays decided.
    // tests: ModeTick.declare_winner_is_one_shot_and_reasserted_every_tick
    // tests: ModeTick.end_level_loss_shape_reasserts
    if (world.mode.win_latched)
        assert_win_shape(world);
}

} // namespace og::sim
