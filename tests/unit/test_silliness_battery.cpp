/* Runaway-specials WP-4: the silliness battery + switch-launder regressions
 * (docs/runaway-effects-design.md §6.2 + §6.3).
 *
 * Headless scripted casters drive every §2 effect end-to-end through the REAL
 * production paths (walker::special -> family do_special, effect on_death,
 * weapon on_hit_target, sim_process_player_input, living::act) and assert the
 * §2 AFTER values exactly. The legacy BEFORE values are in comments — this
 * file is the evidence artifact for the PR description.
 *
 * Determinism: the unit groups link the production component libraries
 * (compiled without TESTING), so the SimRandom override hook is compiled out
 * of the gameplay call sites. Instead each scripted draw is steered by
 * reseeding the world RNG's public LCG state: seed_for_roll(r) makes the NEXT
 * draw return exactly r for any bound > r, and leaves the post-draw state at
 * exactly (r << 16) — which doubles as a "one draw consumed" stream check.
 * Every production draw keeps its exact bound and call count while the test
 * picks each result. No test here touches sim behavior — WP-4 is tests only.
 */
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/interface/input_state.h>
#include <openglad/core/constants.h>
#include <openglad/core/combat_math.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "test_gameplay_context_scope.h"
#include "test_family_hook_dispatch.h"

namespace {

// og::sim::SimRandom's LCG (glibc constants) and its multiplicative inverse
// mod 2^32. seed_for_roll(r) picks the unique state whose next step lands at
// exactly (r << 16): the following next(bound) returns ((r<<16) >> 16) %
// bound == r for every bound > r, and the post-draw state equals (r << 16)
// so a test can assert that EXACTLY one draw was consumed (stream discipline,
// spec §0.1 / §2.6a: gated SETs still draw).
inline constexpr std::uint32_t kLcgMul = 1103515245u;
inline constexpr std::uint32_t kLcgInc = 12345u;
inline constexpr std::uint32_t kLcgInv = 4005161829u; // kLcgMul * kLcgInv == 1 (mod 2^32)
static_assert(kLcgMul * kLcgInv == 1u);

constexpr std::uint32_t post_roll_state(std::uint32_t r)
{
    return r << 16;
}

constexpr std::uint32_t seed_for_roll(std::uint32_t r)
{
    return (post_roll_state(r) - kLcgInc) * kLcgInv;
}
static_assert((seed_for_roll(65) * kLcgMul + kLcgInc) == post_roll_state(65));

struct BatteryFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0; // legacy sim-context slot (world owns the live bank)
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    BatteryFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }

    // Script the next world-RNG draw to return exactly `roll`.
    void script_next_roll(std::uint32_t roll)
    {
        level.world().rng_.state_ = seed_for_roll(roll);
    }

    // True iff exactly one draw happened since script_next_roll(roll).
    bool one_draw_consumed(std::uint32_t roll) const
    {
        return level.world().rng_.state_ == post_roll_state(roll);
    }

    BatteryFixture(const BatteryFixture&) = delete;
    BatteryFixture& operator=(const BatteryFixture&) = delete;
};

living* add_living(BatteryFixture& fx, char family, unsigned char team,
                   int x, int y, int level = 1, signed char user = -1)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    w->set_user(user);
    w->stats()->set_level(level);
    w->stats()->set_hitpoints(100.0f);
    w->stats()->set_max_hitpoints(100.0f);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

living* add_caster(BatteryFixture& fx, char family, int level, int x = 80, int y = 80)
{
    living* c = add_living(fx, family, 0, x, y, level, 0);
    c->stats()->set_max_magicpoints(10000.0f);
    c->stats()->set_magicpoints(10000.0f);
    // Test-built walkers default all special costs to 0 (the loader fills
    // them from the family table in production); mirror the real costs so
    // MP-pool math (§2.12) and the MP economy behave exactly like the game.
    const FamilyDescriptor* fd = get_family_descriptor(family);
    EXPECT_TRUE(fd != nullptr);
    if (fd != nullptr)
        for (int i = 0; i < NUM_SPECIALS; ++i)
            c->stats()->set_special_cost(i, fd->special_cost[i]);
    return c;
}

// Newest live FX walker of `family` (add_ob appends Order::FX to oblist).
walker* newest_live_fx(BatteryFixture& fx, int family)
{
    walker* found = nullptr;
    for (const auto& p : fx.level.world().oblist)
    {
        if (p && !p->dead() && p->query_order() == Order::FX &&
            static_cast<int>(p->family()) == family)
            found = p.get();
    }
    return found;
}

std::vector<walker*> all_live_fx(BatteryFixture& fx, int family)
{
    std::vector<walker*> found;
    for (const auto& p : fx.level.world().oblist)
    {
        if (p && !p->dead() && p->query_order() == Order::FX &&
            static_cast<int>(p->family()) == family)
            found.push_back(p.get());
    }
    return found;
}

// FX like scare/bomb/explosion apply their payload from on_death; the game
// reaches it when the animation completes. Detonate directly (virtual
// dispatch: walker* -> effect::death -> descriptor on_death).
void detonate(walker* fx_ob)
{
    fx_ob->set_dead(1);
    fx_ob->death();
}

// Cast the ghost scare and burst the spawned effect (the production delivery
// path: family_ghost do_special -> FAMILY_GHOST_SCARE FX -> ghost_scare_on_death).
void cast_scare(BatteryFixture& fx, living* ghost)
{
    ASSERT_TRUE(ghost->special());
    walker* scare = newest_live_fx(fx, FAMILY_GHOST_SCARE);
    ASSERT_TRUE(scare != nullptr);
    detonate(scare);
}

const WeaponFamilyDescriptor& sprinkle_descriptor()
{
    const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(FAMILY_SPRINKLE);
    EXPECT_TRUE(wfd != nullptr && og::test::has_on_hit_target(*wfd));
    return *wfd;
}

} // namespace

// ===========================================================================
// §6.2.1 — MAGE FREEZE TIME: the pending time-stop bank is bounded.
// ===========================================================================

TEST(SillinessBattery, mage_freeze_bank_capped_under_chain_casting)
{
    BatteryFixture fx;
    living* mage = add_caster(fx, FAMILY_MAGE, 30);
    mage->set_current_special(3); // freeze time (player/team-0 branch, 500 MP, no busy)

    // L30 mage, mana potions refilling mid-freeze (spec §2.1: "held key
    // recasts 1/tick; mana potions refill mid-freeze"): cast every tick for
    // 2000 ticks, with the world's own 1/tick bank decrement mirrored
    // (game_world.cpp "if (enemy_freeze) enemy_freeze--").
    //
    // BEFORE: enemy_freeze += 20 + 11*30 = 350 per cast, uncapped -> the bank
    // climbs ~349/tick without bound (~698,000 pending ticks by tick 2000;
    // the no-refill 10k-MP variant still banks 7000 = 9.5 minutes of stopped
    // time). AFTER: bank == 300 (kEnemyFreezeBankCap) after every cast;
    // chain-casting at the cap just wastes the 500 MP.
    for (int t = 0; t < 2000; ++t)
    {
        mage->stats()->set_magicpoints(10000.0f); // potion refill
        ASSERT_TRUE(mage->special());
        ASSERT_LE(fx.level.world().enemy_freeze, og::combat::kEnemyFreezeBankCap)
            << "tick " << t;
        ASSERT_EQ(og::combat::kEnemyFreezeBankCap, fx.level.world().enemy_freeze)
            << "tick " << t;
        if (fx.level.world().enemy_freeze)
            fx.level.world().enemy_freeze--; // the world's per-tick drain
    }
}

TEST(SillinessBattery, mage_freeze_single_cast_spectacle_preserved)
{
    BatteryFixture fx;

    // Single cast from an empty bank. BEFORE == AFTER at L20 (240) and L14
    // (174): the per-cast formula 20 + 11*L is untouched at all levels; only
    // the accumulated bank is clamped (spec §2.1).
    living* l20 = add_caster(fx, FAMILY_MAGE, 20);
    l20->set_current_special(3);
    fx.level.world().enemy_freeze = 0;
    ASSERT_TRUE(l20->special());
    ASSERT_EQ(240, fx.level.world().enemy_freeze); // the L20 spectacle, intact

    living* l14 = add_caster(fx, FAMILY_MAGE, 14);
    l14->set_current_special(3);
    fx.level.world().enemy_freeze = 0;
    ASSERT_TRUE(l14->special());
    ASSERT_EQ(174, fx.level.world().enemy_freeze);

    // L30 single cast: BEFORE 350 -> AFTER 300 (bank cap), still > L14's 174
    // (reward guardrail §6.1).
    living* l30 = add_caster(fx, FAMILY_MAGE, 30);
    l30->set_current_special(3);
    fx.level.world().enemy_freeze = 0;
    ASSERT_TRUE(l30->special());
    ASSERT_EQ(300, fx.level.world().enemy_freeze);
    ASSERT_GT(300, 174);
}

// ===========================================================================
// §6.2.2 — GHOST SCARE: repeat casts merge to one bounded fright.
// ===========================================================================

TEST(SillinessBattery, ghost_scare_five_recasts_merge_bounded)
{
    BatteryFixture fx;
    living* ghost = add_caster(fx, FAMILY_GHOST, 20, 200, 80);
    ghost->set_team_num(1);
    ghost->set_real_team_num(255);
    living* victim = add_living(fx, FAMILY_SOLDIER, 0, 80, 80, 1);
    // Victim has no myguy: the legacy myguy-only rng(con) resist draw does
    // not fire, so the duration is the exact wrapper value.

    // L20 scare duration: BEFORE 25*20 = 500; AFTER soften(500, 325, 375) =
    // 364. Five recasts BEFORE: force_command PREPENDS -> 5 entries stack
    // end-to-end = 1820 forced-walk ticks (~2.5 minutes). AFTER: force_fright
    // merges into ONE bounded entry (max-remaining, never a sum).
    for (int cast = 0; cast < 4; ++cast)
    {
        cast_scare(fx, ghost);
        ASSERT_EQ(1u, victim->stats()->commands.size()) << "cast " << cast;
        ASSERT_EQ(COMMAND_WALK, victim->stats()->commands.front().commandtype);
        ASSERT_TRUE(victim->stats()->commands.front().forced);
        ASSERT_EQ(364, victim->stats()->commands.front().commandcount);
        ASSERT_EQ(-1, victim->stats()->commands.front().com1)
            << "fleeing west, away from the ghost standing east";
    }

    // Cast 5 from the WEST: the merged entry re-points to the latest cast's
    // direction while the count refreshes to max instead of summing.
    ghost->setxy(8, 80);
    cast_scare(fx, ghost);
    ASSERT_EQ(1u, victim->stats()->commands.size());
    ASSERT_EQ(364, victim->stats()->commands.front().commandcount);
    ASSERT_EQ(1, victim->stats()->commands.front().com1)
        << "direction tracks the latest cast (now fleeing east)";

    // Total queued forced-walk ticks <= 375 (the scare ceiling) after FIVE
    // casts — the merge rule's whole point. BEFORE: 1820.
    int total_forced_walk = 0;
    for (const auto& cmd : victim->stats()->commands)
        if (cmd.forced && cmd.commandtype == COMMAND_WALK)
            total_forced_walk += cmd.commandcount;
    ASSERT_LE(total_forced_walk, og::combat::kScareDurationCeiling);

    // The victim regains self-command within 375 ticks of the last cast: the
    // fright drains 1/tick through the ordinary command drain.
    int drain_ticks = 0;
    while (victim->stats()->has_commands() && drain_ticks < 375)
    {
        victim->stats()->do_command();
        ++drain_ticks;
    }
    ASSERT_TRUE(victim->stats()->commands.empty())
        << "victim still command-locked after 375 ticks";
    ASSERT_LE(drain_ticks, 364);
    victim->stats()->add_command(COMMAND_WALK, 3, 0, 1);
    ASSERT_FALSE(victim->stats()->commands.front().forced)
        << "the victim's own command is accepted once the fright expires";
}

// ===========================================================================
// §6.2.3 — FAERIE SPRINKLE vs a player-controlled victim: thaw immunity.
// ===========================================================================

namespace {

struct SprinkleRunStats {
    int actable_total = 0;
    int immunity_cycles = 0;     // ticks where raw first went negative
    int min_post_freeze_run = 0; // shortest completed actable run after a freeze
    int max_post_freeze_run = 0;
    int completed_runs = 0;
    int max_frozen_seen = 0;
    short min_raw_seen = 0;
};

// The real per-tick order for a player-controlled victim (GameServer::step):
// input handler first (player-side drain, §3.3), then the world act phase
// (living::act: immunity climb + the legacy living drain), then the weapon
// phase where the sprinkle hit lands (every `cadence` ticks, scripted roll).
SprinkleRunStats run_player_victim_sprinkle(BatteryFixture& fx, living* faerie,
                                            living* victim, int ticks,
                                            std::uint32_t roll, int cadence)
{
    const WeaponFamilyDescriptor& wfd = sprinkle_descriptor();
    SimInputDebounce debounce{};
    static std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;
    input.clear();
    walker* control = victim;

    SprinkleRunStats st;
    int cur_run = 0;
    bool seen_freeze = false;
    bool prev_immune = false;
    for (int t = 1; t <= ticks; ++t)
    {
        const bool actable = (victim->stats()->frozen_delay() == 0);
        if (actable)
        {
            ++st.actable_total;
            ++cur_run;
        }
        else
        {
            if (cur_run > 0 && seen_freeze)
            {
                ++st.completed_runs;
                if (st.min_post_freeze_run == 0 || cur_run < st.min_post_freeze_run)
                    st.min_post_freeze_run = cur_run;
                if (cur_run > st.max_post_freeze_run)
                    st.max_post_freeze_run = cur_run;
            }
            cur_run = 0;
            seen_freeze = true;
        }

        (void)sim_process_player_input(input.players[0], control,
                                       fx.level.world(), 0, 0, debounce,
                                       special_names, &fx.events);
        EXPECT_TRUE(control == victim);
        (void)victim->act();

        if (t % cadence == 0)
        {
            fx.script_next_roll(roll);
            og::test::on_hit_target(wfd, faerie, victim, faerie);
            // Stream discipline: exactly one draw per hit, refused or not.
            EXPECT_TRUE(fx.one_draw_consumed(roll)) << "tick " << t;
        }

        const short raw = victim->stats()->frozen_delay_raw();
        if (raw < st.min_raw_seen)
            st.min_raw_seen = raw;
        if (raw < 0 && !prev_immune)
            ++st.immunity_cycles;
        prev_immune = (raw < 0);
        const int frozen = victim->stats()->frozen_delay();
        if (frozen > st.max_frozen_seen)
            st.max_frozen_seen = frozen;
    }
    return st;
}

} // namespace

TEST(SillinessBattery, sprinkle_player_victim_immunity_gives_actable_windows)
{
    BatteryFixture fx;
    living* faerie = add_caster(fx, FAMILY_FAERIE, 30, 96, 80);
    faerie->set_team_num(1);
    living* victim = add_living(fx, FAMILY_SOLDIER, 0, 80, 80, 1, 0);
    victim->set_act_type(ACT_CONTROL); // player-controlled, con 0 (no myguy)

    // L30 faerie, con-0 victim: draw bound 40 + 2*30 = 100 at EVERY hit.
    // Scripted roll 65 (odd, below knee 79 -> identity), cadence 13 — longer
    // than the 10-tick §2.6b gate window, so every freeze cycle reaches thaw.
    // BEFORE: every landing hit re-SET frozen_delay -> the player was frozen
    // for ~100% of the run (0 actable ticks). AFTER: the player drain writes
    // -12 on its 1->0 transition and the sprinkle SET is refused during the
    // negative phase, so every thaw cycle the player drain terminates yields
    // >= 12 consecutive actable ticks.
    SprinkleRunStats st =
        run_player_victim_sprinkle(fx, faerie, victim, 2000, 65, 13);

    ASSERT_GT(st.completed_runs, 30) << "expected ~38 freeze/thaw cycles";
    ASSERT_GE(st.min_post_freeze_run, 12)
        << "every post-freeze actable window must span the full immunity";
    ASSERT_GE(st.actable_total, 300) // >= 15% of 2000 (before: ~0)
        << "the controlled hero must get a real share of the fight back";
    ASSERT_GT(st.immunity_cycles, 30);
    ASSERT_LE(st.max_frozen_seen, 91)
        << "every freeze span <= 91 (soften ceiling at the L30 con-0 bound)";
    // The player drain writes -12; the same tick's living::act climb makes
    // the first value observable after the act phase -11.
    ASSERT_EQ(-(og::combat::kFreezeThawImmunityTicks - 1),
              static_cast<int>(st.min_raw_seen));
}

TEST(SillinessBattery, sprinkle_player_victim_sub_gate_cadence_residual)
{
    // HONEST RESIDUAL PIN #1 (flagged in the WP-4 report). §6.2.3's literal
    // script — hits every 5 ticks — cannot show the immunity at all: the
    // §2.6b refresh gate deliberately keeps the charm-mirrored 10-tick floor,
    // so a sustained cadence at or below the window re-SETs the freeze while
    // frozen_delay is in (0, 10] — BEFORE the 1->0 transition that arms the
    // §3.3 immunity. The player relocks exactly like the AI variant. The
    // immunity guarantee therefore applies per freeze cycle THAT REACHES
    // THAW (any cadence longer than the gate window — previous test); under
    // relentless sub-window cadence the stunlock persists by the gate's own
    // design. Pinned here so the shape is a documented decision, not a
    // surprise; a fix would need an in-window player refusal (a sim change,
    // out of WP-4 scope).
    BatteryFixture fx;
    living* faerie = add_caster(fx, FAMILY_FAERIE, 30, 96, 80);
    faerie->set_team_num(1);
    living* victim = add_living(fx, FAMILY_SOLDIER, 0, 80, 80, 1, 0);
    victim->set_act_type(ACT_CONTROL);

    SprinkleRunStats st =
        run_player_victim_sprinkle(fx, faerie, victim, 500, 65, 5);

    ASSERT_EQ(0, st.completed_runs)
        << "sub-gate cadence relocks the player before every thaw";
    ASSERT_EQ(0, st.immunity_cycles) << "the 1->0 transition is never reached";
    ASSERT_EQ(5, st.actable_total) << "only the ticks before the first hit";
    ASSERT_LE(st.max_frozen_seen, 91) << "the span cap still holds throughout";
}

TEST(SillinessBattery, sprinkle_player_victim_even_span_residual)
{
    // HONEST RESIDUAL PIN #2 (flagged in the WP-4 report). A frozen
    // player-controlled walker drains TWICE per tick — the player-side drain
    // (sim_input_handler) plus the legacy living::act drain, both master
    // behavior. Only the player drain writes the -12 immunity phase, so a
    // freeze span with EVEN parity terminates at the living::act site
    // (1 -> 0, no immunity) and the next hit re-lands after only the cadence
    // gap. §3.3's ">= 12 actable ticks per cycle" holds for cycles the
    // player drain terminates (odd spans, first test); even spans get only
    // the short gap. Changing either drain site is a parity-visible sim
    // change and is out of scope by §0 — this test pins the shipped shape so
    // nobody "fixes" it silently.
    BatteryFixture fx;
    living* faerie = add_caster(fx, FAMILY_FAERIE, 30, 96, 80);
    faerie->set_team_num(1);
    living* victim = add_living(fx, FAMILY_SOLDIER, 0, 80, 80, 1, 0);
    victim->set_act_type(ACT_CONTROL);

    // Even roll 64 (identity below the knee) -> even span every cycle;
    // cadence 13 keeps every cycle reaching thaw (as in the odd test).
    SprinkleRunStats st =
        run_player_victim_sprinkle(fx, faerie, victim, 500, 64, 13);

    ASSERT_EQ(0, st.immunity_cycles)
        << "even spans thaw at the living::act drain: no -12 write";
    ASSERT_EQ(0, static_cast<int>(st.min_raw_seen));
    ASSERT_GT(st.actable_total, 0);
    ASSERT_GT(st.completed_runs, 5);
    ASSERT_LE(st.max_post_freeze_run, 13)
        << "without immunity the actable gap is only the hit cadence";
    ASSERT_LE(st.max_frozen_seen, 91);
}

TEST(SillinessBattery, sprinkle_ai_victim_gate_window_and_escape)
{
    BatteryFixture fx;
    living* faerie = add_caster(fx, FAMILY_FAERIE, 30, 96, 80);
    faerie->set_team_num(1);
    living* victim = add_living(fx, FAMILY_SOLDIER, 0, 80, 80, 1);
    // AI-driven victim: drains once per tick in living::act only. ACT_CONTROL
    // act-branch keeps the scripted RNG budget exact (no AI-pathing draws);
    // the drain under test runs before the act_type switch either way.
    victim->set_act_type(ACT_CONTROL);

    const WeaponFamilyDescriptor& wfd = sprinkle_descriptor();
    int actable_phase_a = 0, actable_phase_b = 0;
    int gate_skips = 0, landed_sets = 0;

    auto tick = [&](int t, std::uint32_t roll, int& actable_counter) {
        if (victim->stats()->frozen_delay() == 0)
            ++actable_counter;
        (void)victim->act();
        if (t % 5 == 0)
        {
            const int pre = victim->stats()->frozen_delay();
            fx.script_next_roll(roll);
            og::test::on_hit_target(wfd, faerie, victim, faerie);
            EXPECT_TRUE(fx.one_draw_consumed(roll)) << "tick " << t;
            const int post = victim->stats()->frozen_delay();
            if (pre > og::combat::kSprinkleRefreshFloor)
            {
                ++gate_skips;
                EXPECT_EQ(pre, post)
                    << "L30 owner may not re-SET a freeze still above the gate";
            }
            else if (post != pre)
                ++landed_sets;
        }
    };

    // Phase A: max rolls. BEFORE: SET rng(100) -> up to 99 on every hit.
    // AFTER: soften(99, 79, 110) = 91 — and re-freezes land only inside the
    // <= 10-tick gate window, where the 5-tick cadence still catches an AI
    // victim (the accepted stunlock residual while rolls stay big; spec
    // Risk 3 / §6.2.3).
    for (int t = 1; t <= 400; ++t)
        tick(t, 99, actable_phase_a);
    ASSERT_EQ(5, actable_phase_a)
        << "only the ticks before the first hit are actable: big rolls "
           "relock the AI victim through the gate window";
    ASSERT_LE(victim->stats()->frozen_delay(), 91);

    // Phase B: the geometric escape — a small roll (3 < cadence 5) thaws the
    // victim before the next hit, so it gets actable ticks again.
    for (int t = 401; t <= 600; ++t)
        tick(t, 3, actable_phase_b);
    ASSERT_GT(actable_phase_b, 0) << "small roll = escape from the lock";
    ASSERT_GT(gate_skips, 0);
    ASSERT_GT(landed_sets, 0);
}

TEST(SillinessBattery, sprinkle_refresh_gate_level_boundary_and_immunity)
{
    BatteryFixture fx;
    const WeaponFamilyDescriptor& wfd = sprinkle_descriptor();
    living* victim = add_living(fx, FAMILY_SOLDIER, 0, 80, 80, 1);

    // Owner L21 (>= kSprinkleRefreshOwnerLevel): the SET is refused while the
    // victim is still frozen above the 10-tick floor — but the roll is STILL
    // drawn (stream discipline; §2.6b).
    living* l21 = add_caster(fx, FAMILY_FAERIE, 21, 96, 80);
    l21->set_team_num(1);
    victim->stats()->set_frozen_delay(50);
    fx.script_next_roll(60);
    og::test::on_hit_target(wfd, l21, victim, l21);
    ASSERT_EQ(50, victim->stats()->frozen_delay()) << "refresh gate holds at L21";
    ASSERT_TRUE(fx.one_draw_consumed(60)) << "the refused SET still drew once";

    // Owner L20: the gate is level-bound OFF (the weapon_sprinkle_emission
    // golden runs an L20 wielder whose victim thaw schedule must stay
    // byte-identical), so the legacy re-SET lands even mid-freeze. Roll 60 at
    // the L20 con-0 bound 80 is below knee 79 -> identity.
    living* l20 = add_caster(fx, FAMILY_FAERIE, 20, 96, 80);
    l20->set_team_num(1);
    victim->stats()->set_frozen_delay(50);
    fx.script_next_roll(60); // L20 con-0 bound 80, roll 60 <= knee 79: identity
    og::test::on_hit_target(wfd, l20, victim, l20);
    ASSERT_EQ(60, victim->stats()->frozen_delay())
        << "L20 keeps the legacy re-SET (golden-protecting boundary)";

    // Thaw immunity refuses the SET at EVERY owner level (§3.3): the player
    // fix is not level-gated.
    victim->stats()->set_frozen_delay(static_cast<short>(-5));
    fx.script_next_roll(60);
    og::test::on_hit_target(wfd, l20, victim, l20);
    ASSERT_EQ(-5, static_cast<int>(victim->stats()->frozen_delay_raw()));
    ASSERT_TRUE(fx.one_draw_consumed(60)) << "immunity-refused SET still drew";
}

// ===========================================================================
// §6.2.4 — THIEF CLOAK: total bounded, potions never reduced.
// ===========================================================================

TEST(SillinessBattery, thief_cloak_capped_and_potion_never_reduced)
{
    BatteryFixture fx;
    living* thief = add_caster(fx, FAMILY_THIEF, 20);
    thief->set_current_special(2); // cloak, 125 MP, no busy

    // L20, scripted rng(20) roll 10 -> gain 20 + 10*20 = 220 per cast.
    // BEFORE: invisibility_left += gain, uncapped -> 2200 after 10 casts
    // (~3 minutes invisible; ~2100 at mean rolls; short overflow at high L).
    // AFTER: cloak_total caps the TOTAL at 350 (28.6 s).
    fx.script_next_roll(10);
    ASSERT_TRUE(thief->special());
    ASSERT_EQ(220, static_cast<int>(thief->invisibility_left()));
    for (int cast = 1; cast < 10; ++cast)
    {
        fx.script_next_roll(10);
        ASSERT_TRUE(thief->special());
        ASSERT_LE(static_cast<int>(thief->invisibility_left()),
                  og::combat::kInvisibilityCloakCap) << "cast " << cast;
    }
    ASSERT_EQ(og::combat::kInvisibilityCloakCap,
              static_cast<int>(thief->invisibility_left()));

    // Potion-granted 450 (invisibility potions are NOT capped): the monotonic
    // cap idiom never reduces an existing value (§0.7).
    thief->set_invisibility_left(static_cast<short>(450));
    fx.script_next_roll(10);
    ASSERT_TRUE(thief->special());
    ASSERT_EQ(450, static_cast<int>(thief->invisibility_left()));
}

// ===========================================================================
// §6.2.5 — ORC YELL: victim stun total capped, immunity discard.
// ===========================================================================

TEST(SillinessBattery, orc_yell_stack_capped_and_immunity_discard)
{
    BatteryFixture fx;
    living* orc = add_caster(fx, FAMILY_ORC, 20);
    orc->set_current_special(1); // yell, 25 MP
    living* victim = add_living(fx, FAMILY_SOLDIER, 1, 160, 80, 1);
    victim->stats()->set_hitpoints(20.0f); // hp/30 = 0: the con-0 proxy

    // L20 yell vs con-0 victim, scripted draws: rng(200) = 50, rng(0) = 0 ->
    // add = 10 + 50 - 0 = 60 per yell (BOTH legacy draws performed verbatim).
    // BEFORE: frozen_delay += add, unbounded -> 600 after 10 yells with these
    // rolls (~1100 at mean rolls). AFTER: stun_total caps the victim TOTAL at
    // 150 (12.2 s).
    for (int yell = 0; yell < 10; ++yell)
    {
        orc->set_busy(0.0f); // scripted caster: strip the yell's 2-tick busy
        fx.script_next_roll(50); // rng(200) = 50; rng(0) returns 0 drawless
        ASSERT_TRUE(orc->special());
        ASSERT_LE(static_cast<int>(victim->stats()->frozen_delay()),
                  og::combat::kFrozenStunStackCap) << "yell " << yell;
    }
    ASSERT_EQ(og::combat::kFrozenStunStackCap,
              static_cast<int>(victim->stats()->frozen_delay()));

    // Thaw immunity (§3.3): a yell landing inside the victim's negative phase
    // is discarded entirely (stun_total returns cur unchanged).
    victim->stats()->set_frozen_delay(static_cast<short>(-5));
    orc->set_busy(0.0f);
    fx.script_next_roll(50);
    ASSERT_TRUE(orc->special());
    ASSERT_EQ(-5, static_cast<int>(victim->stats()->frozen_delay_raw()));
}

// ===========================================================================
// §6.2.6 — THIEF BOMB: damage softened, knockback slapstick preserved.
// ===========================================================================

TEST(SillinessBattery, bomb_triple_knockback_prepends_legacy)
{
    BatteryFixture fx;
    living* thief = add_caster(fx, FAMILY_THIEF, 10, 100, 100);
    thief->set_act_type(ACT_CONTROL); // player thief: no AI run-away draws
    thief->set_current_special(1);    // drop bomb, 35 MP, no busy
    living* victim = add_living(fx, FAMILY_SOLDIER, 1, 120, 100, 1);
    victim->set_act_type(ACT_CONTROL); // ACT_CONTROL victim: hit_response is a no-op
    victim->stats()->set_hitpoints(1000.0f);
    victim->stats()->set_max_hitpoints(1000.0f);

    // Three bombs armed before any detonation ("3 simultaneous L10 bombs").
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(thief->special());
    std::vector<walker*> bombs = all_live_fx(fx, FAMILY_BOMB);
    ASSERT_EQ(3u, bombs.size());
    // L10 damage is below the knee: BEFORE == AFTER == 15*(10+1) = 165.
    for (walker* b : bombs)
        ASSERT_EQ(165.0f, b->damage());

    // Detonate: bomb -> explosion -> explosion_on_death applies per-bomb
    // knockback with the LEGACY force_command prepend (deliberately NOT
    // force_fright — multi-bomb slapstick is half the game's charm, §2.7).
    for (walker* b : bombs)
        detonate(b);
    std::vector<walker*> explosions = all_live_fx(fx, FAMILY_EXPLOSION);
    ASSERT_EQ(3u, explosions.size());
    for (walker* e : explosions)
        detonate(e);

    // Three SEPARATE prepended entries (not merged), each capped by the
    // legacy min(2 + L/15, 8) = 2 at L10.
    ASSERT_EQ(3u, victim->stats()->commands.size());
    for (const auto& cmd : victim->stats()->commands)
    {
        ASSERT_EQ(COMMAND_WALK, cmd.commandtype);
        ASSERT_TRUE(cmd.forced);
        ASSERT_EQ(2, cmd.commandcount);
        ASSERT_LE(cmd.commandcount, 8);
    }
    ASSERT_LT(victim->stats()->hitpoints(), 1000.0f) << "the blasts still hurt";
}

TEST(SillinessBattery, bomb_damage_table_softened)
{
    BatteryFixture fx;
    // BEFORE (15*(L+1)): L20 315 / L30 465 / L50 765 — one-shots every living
    // from L10 up. AFTER (soften, knee 210 = L13, ceiling 300): 258/277/287.
    const int levels[] = {20, 30, 50};
    const float expected[] = {258.0f, 277.0f, 287.0f};
    for (int i = 0; i < 3; ++i)
    {
        living* thief = add_caster(fx, FAMILY_THIEF, levels[i], 60 + 60 * i, 200);
        thief->set_act_type(ACT_CONTROL);
        thief->set_current_special(1);
        ASSERT_TRUE(thief->special());
        walker* bomb = newest_live_fx(fx, FAMILY_BOMB);
        ASSERT_TRUE(bomb != nullptr);
        ASSERT_EQ(expected[i], bomb->damage()) << "L" << levels[i];
        detonate(bomb); // clear the way for the next newest_live_fx scan
        walker* boom = newest_live_fx(fx, FAMILY_EXPLOSION);
        ASSERT_TRUE(boom != nullptr);
        ASSERT_EQ(expected[i], boom->damage()) << "explosion inherits the curve";
        boom->set_dead(1); // do not detonate: keep the fixture's walkers still
    }
}

// ===========================================================================
// §6.2.7 — MAGE HEARTBURST: MP-fueled pool capped.
// ===========================================================================

TEST(SillinessBattery, heartburst_pool_capped)
{
    {
        BatteryFixture fx;
        living* mage = add_caster(fx, FAMILY_MAGE, 30);
        mage->stats()->set_max_magicpoints(4000.0f);
        mage->stats()->set_magicpoints(4000.0f);
        mage->set_current_special(5); // heartburst, 100 MP
        (void)add_living(fx, FAMILY_SOLDIER, 1, 150, 80, 1); // one foe in range

        // BEFORE: pool = (4000 - 100)/2 = 1950 damage onto a single foe (the
        // gold-bought-INT / 200%-difficulty axis). AFTER: pool capped at 600
        // (kMpPoolDamageCap; binds only above ~1300 MP).
        ASSERT_TRUE(mage->special());
        walker* burst = newest_live_fx(fx, FAMILY_EXPLOSION);
        ASSERT_TRUE(burst != nullptr);
        ASSERT_EQ(600.0f, burst->damage());
    }
    {
        // Below the bind point the cap is the identity map (golden spawns sit
        // far below it: mage mp 600 -> pool 250).
        BatteryFixture fx;
        living* mage = add_caster(fx, FAMILY_MAGE, 30);
        mage->stats()->set_max_magicpoints(1200.0f);
        mage->stats()->set_magicpoints(1200.0f);
        mage->set_current_special(5);
        (void)add_living(fx, FAMILY_SOLDIER, 1, 150, 80, 1);
        ASSERT_TRUE(mage->special());
        walker* burst = newest_live_fx(fx, FAMILY_EXPLOSION);
        ASSERT_TRUE(burst != nullptr);
        ASSERT_EQ(550.0f, burst->damage()); // (1200 - 100)/2, uncapped
    }
}

// ===========================================================================
// Charm ceiling (§2.9) — thief charm end-to-end (archmage curve is pinned by
// the WP-1 compute_charm_duration tables).
// ===========================================================================

TEST(SillinessBattery, thief_charm_ceiling_and_natural_expiry)
{
    BatteryFixture fx;
    living* thief = add_caster(fx, FAMILY_THIEF, 50);
    thief->set_current_special(3);
    thief->set_shifter_down(1); // shifted taunt = charm opponent
    living* victim = add_living(fx, FAMILY_SOLDIER, 1, 100, 80, 1);
    victim->set_act_type(ACT_CONTROL);

    // Diff 49 (L50 vs L1). BEFORE: charm_left = 75 + 25*49 = 1300 (~1.8
    // minutes). AFTER: soften(1300, 375, 490) = 477. The resist branch draws
    // rng(20) first — scripted 7 keeps the legacy draw and takes the charm.
    fx.script_next_roll(7);
    ASSERT_TRUE(thief->special());
    ASSERT_EQ(0, static_cast<int>(victim->team_num()));
    ASSERT_EQ(1, static_cast<int>(victim->real_team_num()));
    ASSERT_EQ(477, static_cast<int>(victim->charm_left()));

    // Expiry belongs solely to charm_left decay in living::act (§4c): no
    // command tricks needed, the charm just runs out.
    victim->stats()->set_hitpoints(100.0f);
    victim->set_charm_left(static_cast<short>(3));
    for (int i = 0; i < 3; ++i)
        (void)victim->act();
    ASSERT_EQ(1, static_cast<int>(victim->team_num()));
    ASSERT_EQ(255, static_cast<int>(victim->real_team_num()));
    ASSERT_EQ(0, static_cast<int>(victim->charm_left()));
}

// ===========================================================================
// §2.10 — CLERIC GLOW: flat cap (identity-FORCED at L20 by the glow golden).
// ===========================================================================

TEST(SillinessBattery, cleric_glow_flat_cap)
{
    BatteryFixture fx;
    const FamilyDescriptor* cleric_fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(cleric_fd != nullptr && og::test::has_customize_weapon(*cleric_fd));

    // BEFORE (init 350 + 110*L): L20 2550 / L30 3650 / L50 5850 (up to 477 s
    // of MAXOBS pressure at 8 MP each). AFTER: bonus = min(110*L, 2200), so
    // L20 stays byte-identical (110*20 == 2200 == cap) and everything above
    // flattens to 2550 total.
    const int levels[] = {20, 30, 50};
    for (int level : levels)
    {
        living* cleric = add_caster(fx, FAMILY_CLERIC, level, 60, 60);
        living* glow = add_living(fx, FAMILY_GLOW, 0, 60, 60, 1);
        glow->set_lifetime(350);
        og::test::customize_weapon(*cleric_fd, cleric, glow);
        ASSERT_EQ(2550, static_cast<int>(glow->lifetime())) << "L" << level;
    }
}

// ===========================================================================
// §6.3 — Switch-launder regression: forced effects survive control switches.
// ===========================================================================

TEST(SwitchLaunderRegression, scare_survives_double_switch)
{
    BatteryFixture fx;
    living* a = add_living(fx, FAMILY_SOLDIER, 0, 80, 80, 1, 0);
    a->set_act_type(ACT_CONTROL);
    living* b = add_living(fx, FAMILY_SOLDIER, 0, 80, 190, 1);
    living* ghost = add_caster(fx, FAMILY_GHOST, 5, 150, 80);
    ghost->set_team_num(1);
    ghost->set_user(-1);

    // A real scare on the player walker (L5 = 125 ticks, below-knee identity;
    // b sits outside the L5 scare radius of 100 px).
    cast_scare(fx, ghost);
    ASSERT_EQ(1u, a->stats()->commands.size());
    ASSERT_EQ(125, a->stats()->commands.front().commandcount);
    ASSERT_EQ(-1, a->stats()->commands.front().com1);
    ASSERT_TRUE(a->stats()->commands.front().forced);

    // Switch hygiene inputs on the scared walker.
    a->set_leader(b);
    a->set_default_weapon(FAMILY_KNIFE);
    a->set_current_weapon(FAMILY_ARROW);

    SimInputDebounce debounce{};
    static std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;
    walker* control = a;
    auto tick = [&](bool press_switch) {
        input.clear();
        if (press_switch)
            input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
        (void)sim_process_player_input(input.players[0], control,
                                       fx.level.world(), 0, 0, debounce,
                                       special_names, &fx.events);
    };

    // The pre-fix launder: SwitchChar away and back cleared the whole queue.
    tick(true);  // a -> b (cycle)
    ASSERT_TRUE(control == b);
    tick(false); // claim b (selective clear on b)
    ASSERT_EQ(0, static_cast<int>(b->user()));
    tick(true);  // b -> a (cycle back)
    ASSERT_TRUE(control == a);
    tick(false); // claim a: the selective clear runs on the scared walker

    // The fright SURVIVES (pre-fix behavior is GONE: two SwitchChar presses
    // no longer launder a scare — the queue is not empty and the forced walk
    // is intact, count and direction).
    ASSERT_FALSE(a->stats()->commands.empty())
        << "REGRESSION: control switch laundered the scare again";
    ASSERT_EQ(1u, a->stats()->commands.size());
    ASSERT_TRUE(a->stats()->commands.front().forced);
    ASSERT_EQ(COMMAND_WALK, a->stats()->commands.front().commandtype);
    ASSERT_EQ(125, a->stats()->commands.front().commandcount);
    ASSERT_EQ(-1, a->stats()->commands.front().com1);

    // Legacy switch hygiene still applies: weapon reset, leader cleared.
    ASSERT_EQ(static_cast<unsigned short>(FAMILY_KNIFE), a->current_weapon());
    ASSERT_TRUE(a->leader() == nullptr);
}

TEST(SwitchLaunderRegression, charm_survives_claim_and_expires_naturally)
{
    BatteryFixture fx;
    living* thief = add_caster(fx, FAMILY_THIEF, 50);
    thief->set_current_special(3);
    thief->set_shifter_down(1);
    living* victim = add_living(fx, FAMILY_SOLDIER, 1, 100, 80, 1);

    // Real thief charm: victim joins team 0 with real_team_num 1.
    fx.script_next_roll(7);
    ASSERT_TRUE(thief->special());
    ASSERT_EQ(0, static_cast<int>(victim->team_num()));
    ASSERT_EQ(1, static_cast<int>(victim->real_team_num()));
    const int charm_before = static_cast<int>(victim->charm_left());
    ASSERT_EQ(477, charm_before);

    // Run the claim path on the charmed walker (user -1 -> claimed). BEFORE:
    // clear_command restored real_team_num — switching characters silently
    // un-charmed. AFTER: the selective clear keeps the charm.
    SimInputDebounce debounce{};
    static std::string special_names[NUM_FAMILIES][NUM_SPECIALS] = {};
    InputState input;
    input.clear();
    walker* control = victim;
    (void)sim_process_player_input(input.players[0], control, fx.level.world(),
                                   0, 0, debounce, special_names, &fx.events);
    ASSERT_EQ(0, static_cast<int>(victim->user()));
    ASSERT_EQ(0, static_cast<int>(victim->team_num()));
    ASSERT_EQ(1, static_cast<int>(victim->real_team_num()))
        << "REGRESSION: the claim path un-charmed again";
    ASSERT_EQ(charm_before, static_cast<int>(victim->charm_left()));

    // And it still expires normally via charm_left decay — the bounded charm
    // ceiling (477 <= 490) is the worst case, not a softlock.
    victim->set_charm_left(static_cast<short>(2));
    (void)victim->act();
    (void)victim->act();
    ASSERT_EQ(1, static_cast<int>(victim->team_num()));
    ASSERT_EQ(255, static_cast<int>(victim->real_team_num()));
}

TEST(SwitchLaunderRegression, level_load_full_clear_unchanged)
{
    BatteryFixture fx;
    living* w = add_living(fx, FAMILY_SOLDIER, 0, 80, 80, 1);
    w->stats()->force_fright(200, 1, 0);
    w->set_real_team_num(2);
    w->set_team_num(0);

    // The level-load path keeps FULL legacy semantics (§4): fright and charm
    // must not cross levels.
    w->stats()->clear_command();
    ASSERT_TRUE(w->stats()->commands.empty());
    ASSERT_EQ(2, static_cast<int>(w->team_num()));
    ASSERT_EQ(255, static_cast<int>(w->real_team_num()));
}
