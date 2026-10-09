/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Specials, skeleton: DIG IN, BONE WALL / BONE STORM, REASSEMBLE /
// LEGION (packs/core/lib/kit_skeleton.lua, weapon_bone_wall.lua and
// families/weapon-20-bone_wall.lua), their bot gates and the LEGION kill
// hook. Every cast goes through the real walker::special() (the cost gate
// and the charge included) on a world with the setting ON; each special's
// setting-OFF twin asserts the classic answer (NOT ENOUGH MP, nothing made,
// nothing spent). Bot gates are asked through the family's own
// check_special_ai dispatch.
//
// One staged perturbation per special was run by hand against the copy
// under build/ci-test/packs (then re-staged) and is recorded on the test
// that caught it.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/kit_marker_role.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kSlotDigIn = 2;
constexpr int kSlotWall = 3;
constexpr int kSlotReassemble = 4;

// The REASSEMBLE ward's kit marker role.
constexpr int kRoleWard = MARKER_WARD;

struct Cast {
    bool ok = false;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
};

Cast cast(walker* w, int slot, short shift)
{
    w->set_current_special(static_cast<char>(slot));
    w->set_shifter_down(shift);
    Cast out;
    out.ok = w->special(&out.why, &out.reason);
    return out;
}

int bone_wall_family()
{
    return og::families::resolve_family_string_id(Order::Weapon,
                                                  "core:bone_wall");
}

int kit_marker_family()
{
    return og::families::resolve_family_string_id(Order::FX,
                                                  "core:kit_marker");
}

// Live entities of (order, family) on every list, optionally owned by
// `owner`.
std::vector<walker*> alive(GameWorld& world, Order order, int family,
                           const walker* owner = nullptr)
{
    std::vector<walker*> out;
    for (const auto* list : {&world.oblist, &world.fxlist, &world.weaplist})
        for (const auto& uptr : *list)
            if (uptr && !uptr->dead() && uptr->query_order() == order &&
                uptr->family() == family &&
                (owner == nullptr || uptr->owner() == owner))
                out.push_back(uptr.get());
    return out;
}

// The skeleton's live kit markers playing `role` (1 = BURROW, 2 = LEGION).
std::vector<walker*> markers(GameWorld& world, const walker* owner, int role)
{
    std::vector<walker*> out;
    for (walker* m : alive(world, Order::FX, kit_marker_family(), owner))
        if (m->ani_type() == role)
            out.push_back(m);
    return out;
}

int skeletons_on_team(GameWorld& world, unsigned char team)
{
    int n = 0;
    for (walker* w : alive(world, Order::Living, FAMILY_SKELETON))
        if (w->team_num() == team)
            ++n;
    return n;
}

bool in_obmap(GameWorld& world, walker* w)
{
    return world.myobmap->walker_to_pos.count(w) != 0;
}

// The first world-RNG state whose next draws are exactly `want` under the
// matching bounds (the bot's rolls, pinned without a spy).
std::uint32_t state_where_draws_are(const std::vector<std::uint32_t>& bounds,
                                    const std::vector<std::uint32_t>& want)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        bool match = true;
        for (std::size_t i = 0; i < bounds.size() && match; ++i)
            match = probe.next(bounds[i]) == want[i];
        if (match)
            return seed;
    }
}

class KitSkeleton : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    std::unique_ptr<TestGameWorld> tw_;
    og::test::ScopedHookFailureGuard guard_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        // A test elsewhere in the binary may have reset the mod slots that
        // hold the kit's families (ids above the core span): refresh before
        // the world's loader is built.
        if (bone_wall_family() != 20 || kit_marker_family() != 17)
            (void)og::resources::refresh_pack_scripts();
        ASSERT_EQ(20, bone_wall_family()) << "core:bone_wall is weapon 20";
        ASSERT_EQ(17, kit_marker_family());
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(32, 32);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.new_specials = 1;
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    GameWorld& world() { return tw_->world(); }

    void tick(int n = 1)
    {
        for (int i = 0; i < n; ++i)
            world().tick();
    }

    // A skeleton that takes no decisions of its own (ACT_CONTROL with no
    // seat: no AI, and hit_response leaves it alone) with `mp` mana and no
    // regeneration, so every MP and HP change in a test is the kit's (no
    // per-round regeneration, and a magic delay it never reaches, or a
    // skeleton that acts would still gain a point every few ticks).
    walker* add_skeleton(unsigned char team, short x, short y, float mp,
                         int level = 4)
    {
        walker* w = world().add_ob(Order::Living, FAMILY_SKELETON);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->setxy(x, y);
        w->set_act_type(ACT_CONTROL);
        w->set_ani_type(ANI_WALK);
        w->set_cycle(0);
        w->stats()->set_level(level);
        w->stats()->set_max_hitpoints(60.0f);
        w->stats()->set_hitpoints(60.0f);
        w->stats()->set_max_magicpoints(mp);
        w->stats()->set_magicpoints(mp);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_max_magic_delay(1000000);
        w->stats()->set_heal_per_round(0.0f);
        w->set_curdir(FACE_RIGHT);
        w->set_enddir(FACE_RIGHT);
        return w;
    }

    walker* add_orc(unsigned char team, short x, short y, int level = 4)
    {
        walker* w = world().add_ob(Order::Living, FAMILY_ORC);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->setxy(x, y);
        w->set_act_type(ACT_CONTROL);
        w->stats()->set_level(level);
        w->stats()->set_max_hitpoints(80.0f);
        w->stats()->set_hitpoints(80.0f);
        w->stats()->set_heal_per_round(0.0f);
        return w;
    }

    // Ticks until `w` is hidden; answers how many ticks it took (or -1).
    int ticks_until_hidden(walker* w, int limit = 20)
    {
        for (int t = 1; t <= limit; ++t) {
            tick();
            if (w->hidden())
                return t;
        }
        return -1;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// DIG IN
// ---------------------------------------------------------------------------

// The sink: four ticks visible, showing frames 24, 25, 26, 27 (the burrow
// sets them; the skeleton is held on the grow row, whose end is harmless,
// rewound every tick), hidden on the fourth tick with frame 27 held and
// ani_type parked on walk: the tele-out row (whose end is TUNNEL) is never
// started, so the skeleton has not moved. 15 MP paid (of 40, leaving a
// pool to stay down on); the burrow marker is the skeleton's.
//
// Perturbation (staged: SINK_TICKS 4 -> 3): RED — "still sinking on tick
// 3" and "and can still be hit" fail: it went under a tick early.
TEST_F(KitSkeleton, dig_in_sinks_four_ticks_then_hides_at_frame_27_without_tunnelling)
{
    walker* skel = add_skeleton(0, 96, 96, 40.0f);
    ASSERT_NE(nullptr, skel);
    const Cast c = cast(skel, kSlotDigIn, 0);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(25.0f, skel->stats()->magicpoints()) << "DIG IN costs 15";
    EXPECT_EQ(ANI_TELE_IN, skel->ani_type()) << "held, not the tele-out row";
    EXPECT_EQ(24, skel->frame()) << "the first sink frame at once";
    EXPECT_EQ(1u, markers(world(), skel, 1).size()) << "one burrow";
    EXPECT_NE(0, skel->kit_state() & KIT_CHANNEL)
        << "the sink is channelled: a press while going down is free";

    for (int t = 1; t <= 3; ++t) {
        tick();
        EXPECT_FALSE(skel->hidden()) << "still sinking on tick " << t;
        EXPECT_EQ(24 + t, skel->frame()) << "sink frame on tick " << t;
        EXPECT_TRUE(in_obmap(world(), skel)) << "and can still be hit";
    }
    tick();
    EXPECT_TRUE(skel->hidden()) << "under on the fourth tick";
    EXPECT_EQ(27, skel->frame()) << "the last sink frame is held";
    EXPECT_EQ(ANI_WALK, skel->ani_type()) << "parked: the row's end never runs";
    EXPECT_EQ(96, skel->xpos()) << "no tunnel hop";
    EXPECT_EQ(96, skel->ypos());
    EXPECT_FALSE(in_obmap(world(), skel));
    EXPECT_EQ(0, skel->kit_state() & KIT_CHANNEL) << "the channel ends";

    tick(5);
    EXPECT_TRUE(skel->hidden());
    EXPECT_EQ(96, skel->xpos());
}

// The same sink for a SEATED hero, driven the way every client's seat runs
// it (sim_process_player_input, then the world tick). A hero's animation is
// advanced twice a tick, once with its input and once when it acts, so the
// tele-out row the dig first used reached its end (TUNNEL) before the
// burrow looked: the hero hopped away and was buried where it landed.
// Standing still it goes under where it stood on the fourth tick; walking
// from the second tick it slides with its own steps but never hops.
//
// Perturbation (staged: the cast sets ANI_TELE_OUT and the sink waits for
// ANI_TELE_OUT, the shape before this fix): RED — standing still, "no
// tunnel hop" reads x 112, y 62 on tick 3; walking, hidden one tick early.
TEST_F(KitSkeleton, seated_hero_digs_in_where_it_stands)
{
    for (const bool walks : {false, true}) {
        SCOPED_TRACE(walks ? "walking from tick 2" : "standing still");
        walker* skel = add_skeleton(0, 96, 96 + (walks ? 64 : 0), 40.0f);
        ASSERT_NE(nullptr, skel);
        skel->set_user(0);
        skel->set_current_special(static_cast<char>(kSlotDigIn));
        walker* control = skel;
        SimInputDebounce debounce{};
        InputState input;
        PlayerInput& pi = input.players[0];
        const short start_y = skel->ypos();
        int hidden_tick = -1;
        short x_at_hide = -1;
        for (int t = 1; t <= 8; ++t) {
            input.clear();
            if (t == 1) {
                pi.pressed[static_cast<int>(InputKey::Special)] = true;
                pi.held[static_cast<int>(InputKey::Special)] = true;
            }
            if (walks && t >= 2)
                pi.held[static_cast<int>(InputKey::Right)] = true;
            sim_process_player_input(pi, control, world(), 0, 0, debounce,
                                     &tw_->events);
            if (t == 1) {
                EXPECT_FLOAT_EQ(25.0f, skel->stats()->magicpoints())
                    << "the cast went through the seat";
                EXPECT_EQ(1u, markers(world(), skel, 1).size());
            }
            tick();
            if (hidden_tick < 0 && skel->hidden()) {
                hidden_tick = t;
                x_at_hide = skel->xpos();
            }
            EXPECT_EQ(start_y, skel->ypos()) << "no tunnel hop, tick " << t;
            if (!walks) {
                EXPECT_EQ(96, skel->xpos()) << "no tunnel hop, tick " << t;
            }
        }
        EXPECT_EQ(4, hidden_tick) << "under on the fourth tick";
        EXPECT_EQ(27, skel->frame());
        EXPECT_EQ(ANI_WALK, skel->ani_type());
        if (walks) {
            EXPECT_GT(x_at_hide, 96) << "it walked while sinking";
            EXPECT_LE(x_at_hide, 96 + 3 * 6) << "by its own steps only";
            EXPECT_EQ(x_at_hide, skel->xpos()) << "a hidden hero stays put";
        }
    }
}

// A seated hero presses Switch Special during the sink (a level-4 skeleton
// has TUNNEL and DIG IN, so the press lands on TUNNEL). The burrow keeps
// DIG IN in hand while it lives, so the press after the latch, while
// buried, reaches DIG IN: the skeleton pops up where it went down, and
// TUNNEL never starts under the floor.
//
// RED without the burrow's hold on the slot: TUNNEL is in hand after the
// switch ("DIG IN stays in hand" reads 1), and the press while buried
// starts the tunnel's tele-out row (ani_type 2) on the hidden skeleton,
// which stays hidden with its burrow ("the press pops it up" fails).
TEST_F(KitSkeleton, switching_special_while_sinking_keeps_dig_in_in_hand)
{
    walker* skel = add_skeleton(0, 96, 96, 25.0f);
    ASSERT_NE(nullptr, skel);
    skel->set_user(0);
    skel->set_current_special(static_cast<char>(kSlotDigIn));
    walker* control = skel;
    SimInputDebounce debounce{};
    InputState input;
    PlayerInput& pi = input.players[0];
    // One seated tick: the seat's input (no key, or one key pressed), then
    // the world tick, the way every client's seat runs it.
    const auto seat_tick = [&](const InputKey* key) {
        input.clear();
        if (key != nullptr) {
            pi.pressed[static_cast<int>(*key)] = true;
            pi.held[static_cast<int>(*key)] = true;
        }
        sim_process_player_input(pi, control, world(), 0, 0, debounce,
                                 &tw_->events);
        tick();
    };
    const auto press = [&](InputKey key) { seat_tick(&key); };
    const auto idle = [&]() { seat_tick(nullptr); };
    press(InputKey::Special);  // tick 1: DIG IN
    ASSERT_EQ(1u, markers(world(), skel, 1).size());
    ASSERT_LT(skel->stats()->magicpoints(), 25.0f) << "DIG IN was paid";
    press(InputKey::SpecialSwitch);  // tick 2: still sinking
    EXPECT_FALSE(skel->hidden()) << "the switch came during the sink";
    EXPECT_EQ(kSlotDigIn, skel->current_special()) << "DIG IN stays in hand";
    for (int t = 3; t <= 12; ++t)
        idle();  // past the sink and the latch
    ASSERT_TRUE(skel->hidden()) << "buried";
    EXPECT_EQ(kSlotDigIn, skel->current_special());

    press(InputKey::Special);  // tick 13: free while buried
    EXPECT_FALSE(skel->hidden()) << "the press pops it up";
    EXPECT_EQ(ANI_TELE_IN, skel->ani_type()) << "up through the grow row";
    EXPECT_TRUE(markers(world(), skel, 1).empty());
    for (int t = 14; t <= 20; ++t) {
        idle();
        EXPECT_EQ(96, skel->xpos()) << "no tunnel hop, tick " << t;
        EXPECT_EQ(96, skel->ypos()) << "no tunnel hop, tick " << t;
        EXPECT_FALSE(skel->hidden()) << "tick " << t;
    }
}

// Buried, the skeleton heals dig_regen every dig_regen_pulse ticks and pays
// dig_drain mana on the same beat (and nothing else: a hidden walker does
// not act), a foe 40 px off does not
// wake it, and the first foe that comes within dig_trigger brings it up
// swinging at that foe.
//
// Perturbation (staged skeleton tuning dig_trigger = 20 -> 0): RED — the
// orc 16 px off no longer wakes it: "pops under the foe" fails (still
// hidden), and the foe, the grow row and the swing all fail with it.
TEST_F(KitSkeleton, dig_in_hides_regens_and_pops_when_a_foe_steps_close)
{
    walker* skel = add_skeleton(0, 96, 96, 40.0f);
    walker* orc = add_orc(1, 200, 96);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, orc);
    ASSERT_TRUE(cast(skel, kSlotDigIn, 0).ok);
    ASSERT_EQ(4, ticks_until_hidden(skel));
    skel->stats()->set_hitpoints(30.0f);

    tick(8);
    EXPECT_FLOAT_EQ(32.0f, skel->stats()->hitpoints())
        << "1 hp every 4 buried ticks";
    EXPECT_FLOAT_EQ(23.0f, skel->stats()->magicpoints())
        << "and 1 mp every 4 buried ticks: 25 after the dig, less 2";

    orc->setxy(136, 96);  // 40 px: outside the trigger
    tick();
    EXPECT_TRUE(skel->hidden()) << "a foe 40 px away does not wake it";

    const float orc_hp = orc->stats()->hitpoints();
    orc->setxy(112, 96);  // 16 px: on top of the burrow
    tick();
    EXPECT_FALSE(skel->hidden()) << "pops under the foe";
    EXPECT_TRUE(in_obmap(world(), skel));
    EXPECT_EQ(orc, skel->foe());
    EXPECT_EQ(ANI_TELE_IN, skel->ani_type()) << "up through the grow row";
    EXPECT_LT(orc->stats()->hitpoints(), orc_hp) << "pops up swinging";
    EXPECT_TRUE(markers(world(), skel, 1).empty());
}

// The press that brings a buried skeleton up costs nothing (the engine
// charges a hidden walker 0): 40 MP, 25 after the dig, 20 after twenty
// buried ticks (one every four), and still 20 once it is up.
TEST_F(KitSkeleton, second_press_pops_up_free_and_costs_nothing)
{
    walker* skel = add_skeleton(0, 96, 96, 40.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotDigIn, 0).ok);
    EXPECT_FLOAT_EQ(25.0f, skel->stats()->magicpoints());
    ASSERT_EQ(4, ticks_until_hidden(skel));
    tick(20);
    EXPECT_FLOAT_EQ(20.0f, skel->stats()->magicpoints()) << "20 buried ticks";
    const Cast up = cast(skel, kSlotDigIn, 0);
    EXPECT_TRUE(up.ok) << up.reason;
    EXPECT_FALSE(skel->hidden());
    EXPECT_FLOAT_EQ(20.0f, skel->stats()->magicpoints()) << "free";
    EXPECT_EQ(ANI_TELE_IN, skel->ani_type());
    EXPECT_TRUE(markers(world(), skel, 1).empty());
}

// A held key re-casts every tick: for kit_latch ticks after the dig every
// press is a silent script refusal (the held arm voices none) that spends
// nothing, during the sink and once buried; the press at age kit_latch
// brings the skeleton up.
//
// Perturbation (staged skeleton tuning kit_latch = 10 -> 0): RED — the
// held press at age 1 already ends the dig ("refused at age 1" fails), and
// the later presses meet NOT ENOUGH MP instead of the silent refusal.
TEST_F(KitSkeleton, held_special_does_not_pop_the_burrow)
{
    walker* skel = add_skeleton(0, 96, 96, 40.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotDigIn, 0).ok);
    for (int age = 1; age <= 9; ++age) {
        tick();
        const float mp_before = skel->stats()->magicpoints();
        const Cast held = cast(skel, kSlotDigIn, 0);
        EXPECT_FALSE(held.ok) << "refused at age " << age;
        EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, held.why)
            << "a script refusal, which a held key never voices (age "
            << age << ")";
        EXPECT_EQ("DIG IN SETTLING", held.reason) << "age " << age;
        EXPECT_FLOAT_EQ(mp_before, skel->stats()->magicpoints())
            << "a refused press spends nothing (age " << age << ")";
        EXPECT_EQ(age >= 4, skel->hidden()) << "age " << age;
    }
    tick();
    const Cast up = cast(skel, kSlotDigIn, 0);
    EXPECT_TRUE(up.ok) << up.reason;
    EXPECT_FALSE(skel->hidden()) << "the press at age 10 surfaces";
}

// With nobody coming, the burrow lets the skeleton up after dig_max ticks
// (a hidden hostile cannot hold a level open for good). 100 MP: 85 after
// the dig, more than the 75 that staying down all 300 ticks costs, so the
// time runs out before the mana does.
TEST_F(KitSkeleton, dig_in_expires_on_its_own)
{
    walker* skel = add_skeleton(1, 96, 96, 100.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotDigIn, 0).ok);
    ASSERT_EQ(4, ticks_until_hidden(skel));
    tick(299);
    EXPECT_TRUE(skel->hidden()) << "299 buried ticks";
    tick();
    EXPECT_FALSE(skel->hidden()) << "up at dig_max (300)";
    EXPECT_FLOAT_EQ(10.0f, skel->stats()->magicpoints())
        << "75 paid over the 300 ticks";
    EXPECT_EQ(nullptr, skel->foe());
    EXPECT_TRUE(markers(world(), skel, 1).empty());
}

// Staying down costs a mana every four buried ticks, and the burrow's clock
// is kept to what the mana can still pay for, so the countdown the player
// sees is when it really ends. 23 MP: 8 left after the dig. On the first
// buried tick the clock reads 35 (eight more payments four ticks apart and
// the three ticks to the first), the 32nd buried tick pays the last mana,
// and on the 36th the clock reads 0 and the skeleton comes up plainly: no
// foe, no swing, never below zero mana.
//
// RED (staged: the clamp line removed): the first buried reading is 299.
// RED (staged: the dry pop removed): the 36th tick pays a mana it does not
// have, and the pool reads -1.
TEST_F(KitSkeleton, burrow_surfaces_when_the_mana_runs_dry)
{
    walker* skel = add_skeleton(0, 96, 96, 23.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotDigIn, 0).ok);
    EXPECT_FLOAT_EQ(8.0f, skel->stats()->magicpoints());
    ASSERT_EQ(4, ticks_until_hidden(skel));
    const auto burrows = markers(world(), skel, 1);
    ASSERT_EQ(1u, burrows.size());
    walker* burrow = burrows[0];
    tick();
    EXPECT_EQ(35, burrow->lifetime()) << "the first buried reading";
    for (int t = 2; t <= 35; ++t) {
        tick();
        ASSERT_TRUE(skel->hidden()) << "buried tick " << t;
        EXPECT_EQ(36 - t, burrow->lifetime()) << "buried tick " << t;
        if (t == 31) {
            EXPECT_FLOAT_EQ(1.0f, skel->stats()->magicpoints());
        }
        if (t == 32) {
            EXPECT_FLOAT_EQ(0.0f, skel->stats()->magicpoints())
                << "the 32nd buried tick pays the last";
        }
    }
    // The 36th buried tick, acted by hand so the spent marker can still be
    // read before the world sweeps it.
    burrow->act();
    EXPECT_TRUE(burrow->dead());
    EXPECT_EQ(0, burrow->lifetime()) << "the clock reads 0 as it surfaces";
    EXPECT_FALSE(skel->hidden()) << "up when the mana runs dry";
    EXPECT_EQ(ANI_TELE_IN, skel->ani_type()) << "up through the grow row";
    EXPECT_EQ(nullptr, skel->foe());
    EXPECT_TRUE(alive(world(), Order::Weapon, FAMILY_BONE, skel).empty())
        << "nobody to swing at";
    EXPECT_FLOAT_EQ(0.0f, skel->stats()->magicpoints()) << "never below zero";
}

// The dig refuses when it would leave under dig_min_pool (8) mana to stay
// down on: 22 MP leaves 7 and is refused free; 23 leaves 8 and digs.
//
// RED (staged skeleton tuning dig_min_pool = 8 -> 0): 22 MP digs.
TEST_F(KitSkeleton, dig_in_refuses_without_a_pool_to_stay_down)
{
    walker* skel = add_skeleton(0, 96, 96, 22.0f);
    ASSERT_NE(nullptr, skel);
    const Cast poor = cast(skel, kSlotDigIn, 0);
    EXPECT_FALSE(poor.ok);
    EXPECT_EQ("NEED MANA TO STAY DOWN", poor.reason);
    EXPECT_FLOAT_EQ(22.0f, skel->stats()->magicpoints()) << "refused free";
    EXPECT_TRUE(markers(world(), skel, 1).empty());
    EXPECT_EQ(ANI_WALK, skel->ani_type());
    skel->stats()->set_magicpoints(23.0f);
    const Cast enough = cast(skel, kSlotDigIn, 0);
    EXPECT_TRUE(enough.ok) << enough.reason;
    EXPECT_FLOAT_EQ(8.0f, skel->stats()->magicpoints());
    EXPECT_EQ(1u, markers(world(), skel, 1).size());
}

// A skeleton digging in on floor 1 stays on floor 1, buried and back up;
// a foe walking over it on floor 0 does not wake it.
TEST_F(KitSkeleton, dig_in_on_floor_one_stays_on_floor_one)
{
    GameWorld& w = world();
    w.set_floor_count(2);
    {
        const int gw = w.grid.w;
        const int gh = w.grid.h;
        const std::size_t cells = static_cast<std::size_t>(gw) *
                                  static_cast<std::size_t>(gh);
        auto* buf = new unsigned char[cells];
        std::fill(buf, buf + cells,
                  static_cast<unsigned char>(PIX_GRASS1));
        w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                        static_cast<unsigned char>(gh), buf);
        w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
    }
    walker* skel = add_skeleton(0, 96, 96, 40.0f);
    walker* orc = add_orc(1, 200, 96);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, orc);
    skel->set_floor(1);
    ASSERT_TRUE(cast(skel, kSlotDigIn, 0).ok);
    ASSERT_EQ(4, ticks_until_hidden(skel));
    EXPECT_EQ(1, skel->floor());
    orc->setxy(108, 96);  // over the burrow, one floor down
    tick();
    EXPECT_TRUE(skel->hidden()) << "a foe on another floor walks under it";
    orc->set_floor(1);
    orc->setxy(108, 96);
    tick();
    EXPECT_FALSE(skel->hidden()) << "the same foe on its floor wakes it";
    EXPECT_EQ(1, skel->floor());
}

// Only a foe that walks wakes the burrow: a generator standing on it and a
// flyer hovering over it (by its flag or by a flight potion) do not.
TEST_F(KitSkeleton, dig_in_ignores_flyers_and_generators_over_the_burrow)
{
    walker* skel = add_skeleton(0, 96, 96, 40.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotDigIn, 0).ok);
    ASSERT_EQ(4, ticks_until_hidden(skel));
    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(1);
    tent->setxy(104, 96);
    tick();
    EXPECT_TRUE(skel->hidden()) << "a generator does not step on it";
    tent->set_dead(1);
    walker* flyer = add_orc(1, 108, 96);
    ASSERT_NE(nullptr, flyer);
    flyer->stats()->set_bit_flags(BIT_FLYING, 1);
    tick();
    EXPECT_TRUE(skel->hidden()) << "a flyer passes over";
    flyer->stats()->set_bit_flags(BIT_FLYING, 0);
    flyer->set_flight_left(50);
    tick();
    EXPECT_TRUE(skel->hidden()) << "so does a walker under a flight potion";
    flyer->set_flight_left(0);
    tick();
    EXPECT_FALSE(skel->hidden()) << "on foot, it wakes the skeleton";
}

// A ghost's body cannot dig (the possessed host is driven by the ghost's
// seat): the cast refuses before it spends anything.
TEST_F(KitSkeleton, possessed_body_cannot_dig_in)
{
    walker* skel = add_skeleton(1, 96, 96, 40.0f);
    ASSERT_NE(nullptr, skel);
    skel->set_possess_link(9999u);
    const Cast c = cast(skel, kSlotDigIn, 0);
    EXPECT_FALSE(c.ok);
    EXPECT_EQ("POSSESSED BODY", c.reason);
    EXPECT_FLOAT_EQ(40.0f, skel->stats()->magicpoints());
    EXPECT_TRUE(markers(world(), skel, 1).empty());
    skel->set_possess_link(0u);
    skel->set_ani_type(ANI_TELE_IN);
    const Cast busy = cast(skel, kSlotDigIn, 0);
    EXPECT_EQ("SPECIAL BUSY", busy.reason) << "mid-teleport";
}

// A ghost that takes the body during the four sinking ticks keeps it
// visible: the burrow gives up and leaves no channel behind.
TEST_F(KitSkeleton, a_body_possessed_mid_sink_stays_up)
{
    walker* skel = add_skeleton(1, 96, 96, 40.0f);
    walker* ghost = add_orc(0, 300, 300);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, ghost);
    ASSERT_TRUE(cast(skel, kSlotDigIn, 0).ok);
    tick();
    ghost->set_hidden(true);  // the rider is inside the body
    skel->set_possess_link(ghost->entity_id());
    ghost->set_possess_link(skel->entity_id());
    EXPECT_EQ(-1, ticks_until_hidden(skel, 6));
    EXPECT_TRUE(markers(world(), skel, 1).empty());
    EXPECT_EQ(0, skel->kit_state() & KIT_CHANNEL);
    skel->set_possess_link(0u);
    ghost->set_possess_link(0u);
    ghost->set_hidden(false);
}

// While the dig is in progress the skeleton's other specials refuse: the
// free channelled press belongs to the dig.
TEST_F(KitSkeleton, other_specials_wait_for_the_dig)
{
    walker* skel = add_skeleton(0, 96, 96, 40.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotDigIn, 0).ok);
    for (const int slot : {kSlotWall, kSlotReassemble})
        for (const short shift : {short{0}, short{1}}) {
            const Cast c = cast(skel, slot, shift);
            EXPECT_FALSE(c.ok) << slot << "/" << shift;
            EXPECT_EQ("SPECIAL BUSY", c.reason) << slot << "/" << shift;
        }
    EXPECT_TRUE(alive(world(), Order::Weapon, bone_wall_family()).empty());
    EXPECT_EQ(0, skel->kit_state() & KIT_WARD);
}

// Setting OFF: DIG IN is not a special at all (NOT ENOUGH MP from the 5000
// sentinel), nothing sinks, nothing is spent.
TEST_F(KitSkeleton, dig_in_off_twin_is_not_cast)
{
    world().new_specials = 0;
    walker* skel = add_skeleton(0, 96, 96, 500.0f);
    ASSERT_NE(nullptr, skel);
    const Cast c = cast(skel, kSlotDigIn, 0);
    EXPECT_FALSE(c.ok);
    EXPECT_EQ(walker::SpecialFailure::NoMP, c.why);
    EXPECT_FLOAT_EQ(500.0f, skel->stats()->magicpoints());
    EXPECT_EQ(ANI_WALK, skel->ani_type());
    EXPECT_TRUE(markers(world(), skel, 1).empty());
    tick(6);
    EXPECT_FALSE(skel->hidden());
}

// ---------------------------------------------------------------------------
// BONE WALL / BONE STORM
// ---------------------------------------------------------------------------

// Facing right, the wall is three 16 px segments across the facing, their
// centres 20 px ahead of the skeleton's centre (the skeleton is 15x13, so
// its centre is (103, 102)): owned, on its team, sitting, built at
// 40 + 4 per level, timed. A foe cannot walk or land into a segment; an
// ally can walk through it. The second press meets its own segments.
//
// Perturbation (staged skeleton tuning wall_ticks = 360 -> 1): RED — each
// segment's lifetime reads 1, "stands for its lifetime" sees 0 of 3, and
// the foe walks and lands where the wall was.
TEST_F(KitSkeleton, bone_wall_stands_three_segments_blocks_foes_passes_allies_and_landings)
{
    walker* skel = add_skeleton(0, 96, 96, 60.0f);
    walker* foe = add_orc(1, 200, 160);
    walker* ally = add_orc(0, 200, 200);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, ally);
    const Cast c = cast(skel, kSlotWall, 0);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(30.0f, skel->stats()->magicpoints()) << "BONE WALL costs 30";

    std::vector<walker*> walls =
        alive(world(), Order::Weapon, bone_wall_family(), skel);
    ASSERT_EQ(3u, walls.size());
    std::sort(walls.begin(), walls.end(), [](walker* a, walker* b) {
        return a->ypos() < b->ypos();
    });
    const short want_y[] = {78, 94, 110};
    for (int i = 0; i < 3; ++i) {
        walker* seg = walls[static_cast<std::size_t>(i)];
        EXPECT_EQ(115, seg->xpos()) << i;
        EXPECT_EQ(want_y[i], seg->ypos()) << i;
        EXPECT_EQ(0, seg->team_num());
        EXPECT_EQ(ACT_SIT, seg->act_type());
        EXPECT_FLOAT_EQ(56.0f, seg->stats()->hitpoints()) << "40 + 4 x level 4";
        EXPECT_FLOAT_EQ(56.0f, seg->stats()->max_hitpoints());
        EXPECT_EQ(360, seg->lifetime());
        EXPECT_TRUE(in_obmap(world(), seg));
    }

    tick(10);
    EXPECT_EQ(3u, alive(world(), Order::Weapon, bone_wall_family(), skel).size())
        << "stands for its lifetime";

    EXPECT_FALSE(world().query_passable(115, 94, foe))
        << "a foe walks into the wall";
    EXPECT_TRUE(world().query_passable(115, 94, ally))
        << "an ally walks through it";
    EXPECT_FALSE(og::sim::respawn_spot_clear(world(), foe, 115, 94, -1))
        << "nothing lands on a segment";

    const float mp_before = skel->stats()->magicpoints();
    const Cast again = cast(skel, kSlotWall, 0);
    EXPECT_FALSE(again.ok);
    EXPECT_EQ("NO ROOM FOR A WALL", again.reason);
    EXPECT_FLOAT_EQ(mp_before, skel->stats()->magicpoints())
        << "the refusal is free";
    tick();  // the probe segments the refusal made are swept
    EXPECT_EQ(3u, alive(world(), Order::Weapon, bone_wall_family()).size());
}

// A door (like a tree or a body) takes its segment's spot: the wall goes
// up beside it, and with every spot taken the cast refuses and spends
// nothing.
TEST_F(KitSkeleton, bone_wall_refuses_on_a_door)
{
    walker* skel = add_skeleton(0, 96, 96, 60.0f);
    ASSERT_NE(nullptr, skel);
    std::vector<walker*> doors;
    for (const short y : {short{78}, short{94}, short{110}}) {
        walker* door = world().add_weap_ob(Order::Weapon, FAMILY_DOOR);
        ASSERT_NE(nullptr, door);
        door->setxy(short{115}, y);
        doors.push_back(door);
    }
    const Cast none = cast(skel, kSlotWall, 0);
    EXPECT_FALSE(none.ok);
    EXPECT_EQ("NO ROOM FOR A WALL", none.reason);
    EXPECT_FLOAT_EQ(60.0f, skel->stats()->magicpoints());
    tick();
    EXPECT_TRUE(alive(world(), Order::Weapon, bone_wall_family()).empty());

    doors[0]->set_dead(1);
    doors[2]->set_dead(1);
    tick();
    const Cast two = cast(skel, kSlotWall, 0);
    ASSERT_TRUE(two.ok) << two.reason;
    tick();
    const auto walls = alive(world(), Order::Weapon, bone_wall_family(), skel);
    ASSERT_EQ(2u, walls.size()) << "the door keeps the middle";
    for (walker* seg : walls)
        EXPECT_NE(94, seg->ypos());
}

// Facing up-right (a diagonal), the wall runs across the diagonal; with a
// sentinel facing the last heading decides; with none, up.
TEST_F(KitSkeleton, bone_wall_follows_the_facing)
{
    walker* skel = add_skeleton(0, 96, 96, 200.0f);
    ASSERT_NE(nullptr, skel);
    skel->set_curdir(FACE_UP_RIGHT);
    ASSERT_TRUE(cast(skel, kSlotWall, 0).ok);
    std::vector<walker*> walls =
        alive(world(), Order::Weapon, bone_wall_family(), skel);
    ASSERT_EQ(3u, walls.size());
    std::sort(walls.begin(), walls.end(), [](walker* a, walker* b) {
        return a->xpos() < b->xpos();
    });
    // centre (103, 102) + (20, -20) = (123, 82); across: (16, 16) steps;
    // each segment's top-left is its centre less (8, 8).
    EXPECT_EQ(99, walls[0]->xpos());
    EXPECT_EQ(58, walls[0]->ypos());
    EXPECT_EQ(115, walls[1]->xpos());
    EXPECT_EQ(74, walls[1]->ypos());
    EXPECT_EQ(131, walls[2]->xpos());
    EXPECT_EQ(90, walls[2]->ypos());
    for (walker* seg : walls)
        seg->set_dead(1);
    tick();

    skel->set_curdir(static_cast<signed char>(-1));
    skel->set_lastx(-3.0f);
    skel->set_lasty(0.0f);
    ASSERT_TRUE(cast(skel, kSlotWall, 0).ok);
    for (walker* seg : alive(world(), Order::Weapon, bone_wall_family(), skel)) {
        EXPECT_EQ(75, seg->xpos()) << "the last heading points left";
        seg->set_dead(1);
    }
    tick();

    skel->set_curdir(static_cast<signed char>(-1));
    skel->set_lastx(0.0f);
    skel->set_lasty(0.0f);
    ASSERT_TRUE(cast(skel, kSlotWall, 0).ok);
    for (walker* seg : alive(world(), Order::Weapon, bone_wall_family(), skel))
        EXPECT_EQ(74, seg->ypos()) << "no heading at all: up";
}

// A segment shows its cracked frame below half health, and falls when its
// lifetime runs out.
//
// Perturbation (staged weapon_bone_wall.lua: og.fdiv(max_hp, 2.0) -> 4.0,
// cracked only below a quarter): RED — "cracked below half" reads frame 0.
TEST_F(KitSkeleton, wall_cracks_below_half)
{
    walker* skel = add_skeleton(0, 96, 96, 30.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotWall, 0).ok);
    walker* seg = alive(world(), Order::Weapon, bone_wall_family(), skel).front();
    tick();
    EXPECT_EQ(0, seg->frame()) << "whole";
    seg->stats()->set_hitpoints(27.0f);  // max 56
    tick();
    EXPECT_EQ(1, seg->frame()) << "cracked below half";
    seg->stats()->set_hitpoints(28.0f);
    tick();
    EXPECT_EQ(0, seg->frame()) << "exactly half is whole";
    seg->set_lifetime(2);
    tick();
    EXPECT_FALSE(seg->dead());
    tick();
    EXPECT_TRUE(seg->dead()) << "falls when its time is out";
}

// BONE STORM: eight bones from the skeleton, and every standing wall of
// THIS skeleton (wherever it is) shatters into its own eight, owned by the
// skeleton; another skeleton's wall stands.
//
// Perturbation (staged: the wall_burst(...) call in bone_storm removed):
// RED — 8 bones instead of 32, and 0 of them leave from the walls.
TEST_F(KitSkeleton, bone_storm_bursts_from_every_wall_and_the_skeleton)
{
    walker* skel = add_skeleton(0, 96, 96, 75.0f);
    walker* other = add_skeleton(0, 96, 300, 30.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, other);
    ASSERT_TRUE(cast(skel, kSlotWall, 0).ok);
    ASSERT_TRUE(cast(other, kSlotWall, 0).ok);
    ASSERT_EQ(3u, alive(world(), Order::Weapon, bone_wall_family(), skel).size());
    skel->setxy(300, 96);  // far from its wall: the storm finds it anyway
    skel->set_busy(0.0f);

    const Cast storm = cast(skel, kSlotWall, 1);
    ASSERT_TRUE(storm.ok) << storm.reason;
    EXPECT_FLOAT_EQ(0.0f, skel->stats()->magicpoints()) << "BONE STORM costs 45";
    const auto bones = alive(world(), Order::Weapon, FAMILY_BONE, skel);
    EXPECT_EQ(32u, bones.size()) << "8 from the skeleton + 8 from each wall";
    int near_wall = 0;
    for (walker* bone : bones) {
        EXPECT_EQ(0, bone->team_num());
        if (bone->xpos() < 160)
            ++near_wall;
    }
    EXPECT_EQ(24, near_wall) << "the walls' bones leave from the walls";
    EXPECT_TRUE(alive(world(), Order::Weapon, bone_wall_family(), skel).empty())
        << "the walls shatter";
    EXPECT_EQ(3u, alive(world(), Order::Weapon, bone_wall_family(), other).size())
        << "another skeleton's wall stands";

    skel->stats()->set_magicpoints(45.0f);
    skel->set_busy(3.0f);
    const Cast busy = cast(skel, kSlotWall, 1);
    EXPECT_EQ("SPECIAL BUSY", busy.reason);
    EXPECT_FLOAT_EQ(45.0f, skel->stats()->magicpoints());
}

// Setting OFF: BONE WALL and BONE STORM are not specials (NOT ENOUGH MP),
// nothing is built or thrown, nothing is spent.
TEST_F(KitSkeleton, bone_wall_off_twin_is_not_cast)
{
    world().new_specials = 0;
    walker* skel = add_skeleton(0, 96, 96, 500.0f);
    ASSERT_NE(nullptr, skel);
    for (const short shift : {short{0}, short{1}}) {
        const Cast c = cast(skel, kSlotWall, shift);
        EXPECT_FALSE(c.ok) << shift;
        EXPECT_EQ(walker::SpecialFailure::NoMP, c.why) << shift;
    }
    EXPECT_FLOAT_EQ(500.0f, skel->stats()->magicpoints());
    EXPECT_TRUE(alive(world(), Order::Weapon, bone_wall_family()).empty());
    EXPECT_TRUE(alive(world(), Order::Weapon, FAMILY_BONE).empty());
}

// ---------------------------------------------------------------------------
// REASSEMBLE / LEGION
// ---------------------------------------------------------------------------

// REASSEMBLE sets the ward (60 MP, a flash, and a WARD marker of the
// skeleton's that keeps its 360-tick clock); a second ward refuses free.
// A killing blow then stands the skeleton back up at a quarter health.
//
// Perturbation (staged: `self:kit_state() | C.KIT_WARD` written as
// `self:kit_state() | 0`): RED — no ward bit, the second ward is cast
// again (60 MP spent), and the blow kills ("it gets up" fails, hp -429).
TEST_F(KitSkeleton, warded_skeleton_stands_back_up_at_a_quarter)
{
    walker* skel = add_skeleton(0, 96, 96, 120.0f);
    walker* orc = add_orc(1, 112, 96);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, orc);
    const Cast ward = cast(skel, kSlotReassemble, 0);
    ASSERT_TRUE(ward.ok) << ward.reason;
    EXPECT_NE(0, skel->kit_state() & KIT_WARD);
    EXPECT_FLOAT_EQ(60.0f, skel->stats()->magicpoints()) << "REASSEMBLE costs 60";
    EXPECT_EQ(1u, alive(world(), Order::FX, FAMILY_FLASH).size()) << "a flash";
    const auto wards = markers(world(), skel, kRoleWard);
    ASSERT_EQ(1u, wards.size()) << "the ward keeps time on a marker";
    EXPECT_EQ(360, wards[0]->lifetime()) << "ward_ticks";

    const Cast again = cast(skel, kSlotReassemble, 0);
    EXPECT_FALSE(again.ok);
    EXPECT_EQ("ALREADY WARDED", again.reason);
    EXPECT_FLOAT_EQ(60.0f, skel->stats()->magicpoints());

    orc->set_damage(500.0f);
    ASSERT_TRUE(orc->attack(skel));
    EXPECT_FALSE(skel->dead()) << "it gets up";
    EXPECT_FLOAT_EQ(15.0f, skel->stats()->hitpoints()) << "a quarter of 60";
    EXPECT_EQ(0, skel->kit_state() & KIT_WARD) << "the ward is spent";
}

namespace {

// The ward's fade notices since `from`.
int ward_fade_notices(const TestGameWorld& tw, std::size_t from)
{
    int n = 0;
    const auto& events = tw.events.events();
    for (std::size_t i = from; i < events.size(); ++i)
        if (events[i].kind == og::sim::EventKind::Notification &&
            events[i].text.ends_with("'s ward fades"))
            ++n;
    return n;
}

}  // namespace

// With mana to spare the ward lasts ward_ticks (360) and fades on the
// 360th tick: the bit goes, the marker goes, the fade is told (this bot
// skeleton has no seat, so everyone is), and a killing blow after that is
// final.
//
// RED (staged: the out-of-time branch removed): still warded after 360
// ticks, no notice, and the blow is survived.
TEST_F(KitSkeleton, ward_fades_after_its_time)
{
    walker* skel = add_skeleton(0, 96, 96, 1060.0f);
    walker* orc = add_orc(1, 300, 300);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, orc);
    ASSERT_TRUE(cast(skel, kSlotReassemble, 0).ok);
    const std::size_t mark = tw_->events.events().size();
    tick(359);
    EXPECT_NE(0, skel->kit_state() & KIT_WARD) << "warded for 359 ticks";
    ASSERT_EQ(1u, markers(world(), skel, kRoleWard).size());
    EXPECT_EQ(1, markers(world(), skel, kRoleWard)[0]->lifetime());
    EXPECT_EQ(0, ward_fade_notices(*tw_, mark));
    tick();
    EXPECT_EQ(0, skel->kit_state() & KIT_WARD) << "faded on tick 360";
    EXPECT_TRUE(markers(world(), skel, kRoleWard).empty());
    EXPECT_EQ(1, ward_fade_notices(*tw_, mark)) << "the fade is told";
    EXPECT_FLOAT_EQ(1000.0f - 59.0f, skel->stats()->magicpoints())
        << "a mana every 6 ticks while it was armed";
    orc->set_damage(500.0f);
    ASSERT_TRUE(orc->attack(skel));
    EXPECT_TRUE(skel->dead()) << "no ward, no revival";
}

// The ward costs a mana every six ticks, and its clock is kept to what the
// mana can still pay for. 63 MP: 3 left after the price. The first reading
// is 23 (three payments six ticks apart and the five ticks to the first:
// the marker's first tick leaves 359, five short of the next payment); the
// payments come on the 6th, 12th and 18th ticks, and on the 23rd the clock
// reads 1 and on the 24th, as it reads 0, the ward fades with its notice.
//
// RED (staged: the clamp line removed): the first reading is 359.
// RED (staged: the payment removed): the pool still reads 3 at the fade.
TEST_F(KitSkeleton, ward_fades_when_the_mana_runs_dry)
{
    walker* skel = add_skeleton(0, 96, 96, 63.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotReassemble, 0).ok);
    EXPECT_FLOAT_EQ(3.0f, skel->stats()->magicpoints());
    const auto wards = markers(world(), skel, kRoleWard);
    ASSERT_EQ(1u, wards.size());
    walker* ward = wards[0];
    const std::size_t mark = tw_->events.events().size();
    tick();
    EXPECT_EQ(23, ward->lifetime()) << "the first reading";
    for (int t = 2; t <= 23; ++t) {
        tick();
        ASSERT_NE(0, skel->kit_state() & KIT_WARD) << "tick " << t;
        EXPECT_EQ(24 - t, ward->lifetime()) << "tick " << t;
    }
    EXPECT_FLOAT_EQ(0.0f, skel->stats()->magicpoints())
        << "paid on ticks 6, 12 and 18";
    EXPECT_EQ(0, ward_fade_notices(*tw_, mark));
    // The 24th tick, acted by hand so the spent marker can still be read.
    ward->act();
    EXPECT_TRUE(ward->dead());
    EXPECT_EQ(0, ward->lifetime()) << "the clock reads 0 as the ward fades";
    EXPECT_EQ(0, skel->kit_state() & KIT_WARD) << "faded on the 24th tick";
    EXPECT_EQ(1, ward_fade_notices(*tw_, mark));
    EXPECT_FLOAT_EQ(0.0f, skel->stats()->magicpoints()) << "never below zero";
}

// A death that spends the ward clears the bit; the marker then goes on its
// next tick, silently (the skeleton already stood up), so a ward bought
// again afterwards has one marker, its own, and keeps its full time.
//
// RED (staged: the spent-ward check in the marker removed): the old marker
// lives on after the revival.
TEST_F(KitSkeleton, a_spent_ward_retires_its_marker)
{
    walker* skel = add_skeleton(0, 96, 96, 180.0f);
    walker* orc = add_orc(1, 112, 96);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, orc);
    ASSERT_TRUE(cast(skel, kSlotReassemble, 0).ok);
    orc->set_damage(500.0f);
    ASSERT_TRUE(orc->attack(skel));
    ASSERT_FALSE(skel->dead()) << "it got up";
    ASSERT_EQ(0, skel->kit_state() & KIT_WARD) << "the ward is spent";
    const std::size_t mark = tw_->events.events().size();
    tick();
    EXPECT_TRUE(markers(world(), skel, kRoleWard).empty())
        << "the spent ward's marker goes";
    EXPECT_EQ(0, ward_fade_notices(*tw_, mark)) << "silently";
    ASSERT_TRUE(cast(skel, kSlotReassemble, 0).ok) << "bought again";
    const auto wards = markers(world(), skel, kRoleWard);
    ASSERT_EQ(1u, wards.size());
    EXPECT_EQ(360, wards[0]->lifetime());
}

// A ward bought again in the same tick a death spent the old one, before
// the old marker has acted: REASSEMBLE retires the old marker, so the new
// ward has one marker, its own, and its full 360 ticks; the old clock
// (100 ticks in) does not end it.
//
// RED (staged: the stale marker's retirement in REASSEMBLE removed): two
// WARD markers, and the old one fades the new ward 260 ticks later.
TEST_F(KitSkeleton, a_ward_bought_again_at_once_keeps_its_own_clock)
{
    walker* skel = add_skeleton(0, 96, 96, 250.0f);
    walker* orc = add_orc(1, 300, 300);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, orc);
    ASSERT_TRUE(cast(skel, kSlotReassemble, 0).ok);
    tick(100);
    ASSERT_NE(0, skel->kit_state() & KIT_WARD) << "still warded";
    orc->set_damage(500.0f);
    ASSERT_TRUE(orc->attack(skel));
    ASSERT_FALSE(skel->dead()) << "it got up";
    ASSERT_EQ(0, skel->kit_state() & KIT_WARD) << "the ward is spent";
    ASSERT_TRUE(cast(skel, kSlotReassemble, 0).ok) << "bought again at once";
    const auto wards = markers(world(), skel, kRoleWard);
    EXPECT_EQ(1u, wards.size()) << "only the new ward's marker";
    ASSERT_FALSE(wards.empty());
    EXPECT_EQ(360, wards.back()->lifetime());
    const std::size_t mark = tw_->events.events().size();
    tick(359);
    EXPECT_NE(0, skel->kit_state() & KIT_WARD) << "the new ward keeps its time";
    EXPECT_EQ(0, ward_fade_notices(*tw_, mark)) << "nothing faded";
    tick();
    EXPECT_EQ(0, skel->kit_state() & KIT_WARD) << "faded on its own 360th tick";
}

// A world with no room for the WARD marker cannot keep the ward's time:
// REASSEMBLE is refused, the bit is left clear, nothing is spent.
//
// RED (staged: the bit's clearing on that refusal removed): the skeleton is
// warded although the cast was refused and nothing was paid.
TEST_F(KitSkeleton, a_full_world_cannot_ward)
{
    walker* skel = add_skeleton(0, 96, 96, 120.0f);
    ASSERT_NE(nullptr, skel);
    const auto factory = world().entity_factory;
    world().entity_factory = [factory](Order order, std::int32_t family) {
        if (order == Order::FX)
            return std::unique_ptr<walker>();
        return factory(order, family);
    };
    const Cast c = cast(skel, kSlotReassemble, 0);
    world().entity_factory = factory;
    EXPECT_FALSE(c.ok);
    EXPECT_EQ("COULD NOT WARD", c.reason);
    EXPECT_EQ(0, skel->kit_state() & KIT_WARD);
    EXPECT_FLOAT_EQ(120.0f, skel->stats()->magicpoints());
}

// Without the ward the same blow is final.
TEST_F(KitSkeleton, unwarded_death_is_final)
{
    walker* skel = add_skeleton(0, 96, 96, 0.0f);
    walker* orc = add_orc(1, 112, 96);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, orc);
    orc->set_damage(500.0f);
    ASSERT_TRUE(orc->attack(skel));
    EXPECT_TRUE(skel->dead());
}

// LEGION: for legion_ticks, every living, non-undead foe the skeleton kills
// — by its own blow, by its thrown bone, by an explosion it owns — rises
// as a skeleton on its team at half the victim's level, where it fell, and
// its corpse and life gem are gone (the hook runs after death() left them).
// An undead victim stays down; once the window closes, kills stay kills.
//
// Perturbation (staged skeleton tuning legion_ticks = 300 -> 0): RED — the
// window closes on the marker's first tick, so the second LEGION is cast
// (60 MP spent, no LEGION ALREADY RISING) and nothing rises after it.
// Perturbation (staged: the window ends at "left < 0", one tick late):
// RED — "closed on tick 300" fails and the late kill rises (5, not 4).
TEST_F(KitSkeleton, legion_raises_melee_bone_and_explosion_kills_within_the_window)
{
    walker* skel = add_skeleton(0, 96, 96, 120.0f, 10);
    ASSERT_NE(nullptr, skel);
    const Cast c = cast(skel, kSlotReassemble, 1);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(60.0f, skel->stats()->magicpoints()) << "LEGION costs 60";
    ASSERT_EQ(1u, markers(world(), skel, 2).size());
    tick();

    const float mp_before = skel->stats()->magicpoints();
    const Cast again = cast(skel, kSlotReassemble, 1);
    EXPECT_EQ("LEGION ALREADY RISING", again.reason);
    EXPECT_FLOAT_EQ(mp_before, skel->stats()->magicpoints());

    // Melee: a hero orc, so a life gem drops too.
    walker* melee = add_orc(1, 112, 96, 6);
    ASSERT_NE(nullptr, melee);
    melee->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
    skel->set_damage(1000.0f);
    ASSERT_TRUE(skel->attack(melee));
    ASSERT_TRUE(melee->dead());
    EXPECT_EQ(1, skeletons_on_team(world(), 0) - 1) << "one risen";
    walker* risen = nullptr;
    for (walker* w : alive(world(), Order::Living, FAMILY_SKELETON))
        if (w != skel)
            risen = w;
    ASSERT_NE(nullptr, risen);
    EXPECT_EQ(112, risen->xpos());
    EXPECT_EQ(96, risen->ypos());
    EXPECT_EQ(3, risen->stats()->level()) << "half the victim's level 6";
    EXPECT_EQ(nullptr, risen->owner()) << "it rises for good";
    EXPECT_EQ(0, risen->lifetime());
    EXPECT_EQ(ANI_TELE_IN, risen->ani_type());
    EXPECT_TRUE(alive(world(), Order::Treasure, FAMILY_STAIN).empty())
        << "the corpse is consumed";
    EXPECT_TRUE(alive(world(), Order::Treasure, FAMILY_LIFE_GEM).empty())
        << "and the hero's gem with it";

    // A bone the skeleton threw.
    walker* shot = add_orc(1, 200, 160);
    walker* bone = world().add_ob(Order::Weapon, FAMILY_BONE);
    ASSERT_NE(nullptr, shot);
    ASSERT_NE(nullptr, bone);
    bone->set_team_num(0);
    bone->set_owner(skel);
    bone->setxy(184, 160);
    bone->set_damage(1000.0f);
    ASSERT_TRUE(bone->attack(shot));
    EXPECT_EQ(3, skeletons_on_team(world(), 0)) << "the bone kill rises";

    // An explosion the skeleton owns.
    walker* blasted = add_orc(1, 240, 240);
    walker* boom = world().add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, blasted);
    ASSERT_NE(nullptr, boom);
    boom->set_team_num(0);
    boom->set_owner(skel);
    boom->set_damage(1000.0f);
    ASSERT_TRUE(boom->attack(blasted));
    EXPECT_EQ(4, skeletons_on_team(world(), 0)) << "the blast kill rises";

    // An undead foe stays down.
    walker* undead = add_skeleton(1, 300, 300, 0.0f);
    ASSERT_NE(nullptr, undead);
    ASSERT_TRUE(skel->attack(undead));
    EXPECT_TRUE(undead->dead());
    EXPECT_EQ(4, skeletons_on_team(world(), 0)) << "the undead do not rise";

    // The window closes after exactly legion_ticks (300) ticks: one went
    // by before the kills.
    tick(298);
    EXPECT_EQ(1u, markers(world(), skel, 2).size()) << "open on tick 299";
    tick();
    EXPECT_TRUE(markers(world(), skel, 2).empty()) << "closed on tick 300";
    walker* late = add_orc(1, 360, 360);
    ASSERT_NE(nullptr, late);
    ASSERT_TRUE(skel->attack(late));
    EXPECT_TRUE(late->dead());
    EXPECT_EQ(4, skeletons_on_team(world(), 0)) << "after the window: no rise";
}

// A warded foe that gets back up inside death() was not killed: no hook,
// no risen skeleton, nothing scrubbed.
TEST_F(KitSkeleton, warded_victim_is_not_a_kill)
{
    walker* skel = add_skeleton(0, 96, 96, 60.0f, 10);
    walker* foe = add_skeleton(1, 112, 96, 0.0f);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, foe);
    ASSERT_TRUE(cast(skel, kSlotReassemble, 1).ok);
    foe->set_kit_state(KIT_WARD);
    skel->set_damage(1000.0f);
    ASSERT_TRUE(skel->attack(foe));
    EXPECT_FALSE(foe->dead()) << "the warded foe got up";
    EXPECT_EQ(1, skeletons_on_team(world(), 0)) << "no raise";
    EXPECT_EQ(1, skeletons_on_team(world(), 1));
}

// A blow the REASSEMBLE ward turns into a revival is not a death, for the
// killer as much as for LEGION: like a hit that does not kill, nobody is
// told "DIED!", the last-foe line "All foes defeated!" does not show while
// the skeleton fights on, no blood splats and the soldier's kill count
// stays where it was. The same blow on the same scene without the ward
// tells all of it and counts the kill.
//
// Perturbation (the old kill block: the warded early return in
// walker::attack removed): RED -- the warded blow reports "Rattles DIED!"
// and "All foes defeated!", splats blood and counts a kill.
TEST_F(KitSkeleton, warded_death_tells_nobody_and_counts_no_kill)
{
    walker* soldier = world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);
    soldier->set_team_num(0);
    soldier->setxy(64, 96);
    soldier->set_act_type(ACT_CONTROL);
    soldier->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    soldier->set_damage(500.0f);
    guy* const record = soldier->myguy;
    ASSERT_NE(nullptr, record);

    struct Told {
        int died = 0;
        int all_foes = 0;
        int reassembles = 0;
    };
    const auto told_since = [&](std::size_t from) {
        Told t;
        const auto& events = tw_->events.events();
        for (std::size_t i = from; i < events.size(); ++i) {
            const auto& e = events[i];
            if (e.kind != og::sim::EventKind::Notification)
                continue;
            if (e.text.find("DIED!") != std::string::npos)
                ++t.died;
            if (e.text == "All foes defeated!")
                ++t.all_foes;
            if (e.text.ends_with(" reassembles!"))
                ++t.reassembles;
        }
        return t;
    };
    const auto blood_count = [&] {
        return alive(world(), Order::Weapon, FAMILY_BLOOD).size();
    };

    // Unwarded: the named skeleton is the soldier's only foe, and the blow
    // that kills it is a kill in full.
    walker* plain = add_skeleton(1, 80, 96, 0.0f);
    ASSERT_NE(nullptr, plain);
    plain->stats()->name = "Rattles";
    ASSERT_EQ(1, world().remaining_foes(soldier));
    std::size_t mark = tw_->events.events().size();
    const auto kills0 = record->kills;
    const auto scen_kills0 = record->scen_kills;
    const auto level_kills0 = record->level_kills;
    const std::size_t blood0 = blood_count();
    ASSERT_TRUE(soldier->attack(plain));
    ASSERT_TRUE(plain->dead());
    Told t = told_since(mark);
    EXPECT_EQ(1, t.died) << "an unwarded death is told";
    EXPECT_EQ(1, t.all_foes) << "and it was the last foe";
    EXPECT_EQ(kills0 + 1, record->kills) << "the kill counts";
    EXPECT_EQ(scen_kills0 + 1, record->scen_kills);
    EXPECT_EQ(level_kills0 + plain->stats()->level(), record->level_kills);
    EXPECT_EQ(blood0 + 1, blood_count()) << "and the blood splats";

    // Warded: the same scene, the same blow.
    walker* warded = add_skeleton(1, 80, 112, 0.0f);
    ASSERT_NE(nullptr, warded);
    warded->stats()->name = "Rattles";
    warded->set_kit_state(KIT_WARD);
    ASSERT_EQ(1, world().remaining_foes(soldier));
    mark = tw_->events.events().size();
    const auto kills1 = record->kills;
    const auto scen_kills1 = record->scen_kills;
    const auto level_kills1 = record->level_kills;
    const std::size_t blood1 = blood_count();
    ASSERT_TRUE(soldier->attack(warded));
    ASSERT_FALSE(warded->dead()) << "the ward stood it back up";
    EXPECT_FLOAT_EQ(15.0f, warded->stats()->hitpoints()) << "a quarter of 60";
    t = told_since(mark);
    EXPECT_EQ(1, t.reassembles) << "the only line is the ward's own";
    EXPECT_EQ(0, t.died) << "nobody is told it died";
    EXPECT_EQ(0, t.all_foes) << "and the level is not called won";
    EXPECT_EQ(kills1, record->kills) << "no kill is counted";
    EXPECT_EQ(scen_kills1, record->scen_kills);
    EXPECT_EQ(level_kills1, record->level_kills);
    EXPECT_EQ(blood1, blood_count()) << "no blood splats";
    EXPECT_EQ(1, world().remaining_foes(soldier)) << "it fights on";

    // A ward that cannot take -- this walker's death already ran once (a
    // script stood it up without resetting it) -- leaves the walker dead,
    // and then the blow is a kill in full after all.
    warded->set_dead(1);
    walker* stale = add_skeleton(1, 80, 128, 0.0f);
    ASSERT_NE(nullptr, stale);
    stale->stats()->name = "Rattles";
    stale->set_kit_state(KIT_WARD);
    stale->set_death_called(1);
    mark = tw_->events.events().size();
    const auto kills2 = record->kills;
    const std::size_t blood2 = blood_count();
    ASSERT_TRUE(soldier->attack(stale));
    EXPECT_TRUE(stale->dead()) << "the ward did not take";
    t = told_since(mark);
    EXPECT_EQ(0, t.reassembles);
    EXPECT_EQ(1, t.died) << "so the death is told";
    EXPECT_EQ(1, t.all_foes);
    EXPECT_EQ(kills2 + 1, record->kills) << "and counted";
    EXPECT_EQ(blood2 + 1, blood_count());
}

// With the setting off the hook is inert: a LEGION window left from an ON
// session raises nothing and scrubs nothing (its first statement is the
// guard).
//
// Perturbation (staged: the on_kill setting guard removed): RED — two
// team-0 skeletons ("nothing rises" fails) and the corpse is gone.
TEST_F(KitSkeleton, legion_hook_is_inert_when_off)
{
    walker* skel = add_skeleton(0, 96, 96, 60.0f, 10);
    walker* orc = add_orc(1, 112, 96);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, orc);
    ASSERT_TRUE(cast(skel, kSlotReassemble, 1).ok);
    world().new_specials = 0;
    skel->set_damage(1000.0f);
    ASSERT_TRUE(skel->attack(orc));
    EXPECT_TRUE(orc->dead());
    EXPECT_EQ(1, skeletons_on_team(world(), 0)) << "nothing rises";
    EXPECT_EQ(1u, alive(world(), Order::Treasure, FAMILY_STAIN).size())
        << "the corpse stays";
}

// The window ends with its skeleton; and a kill only counts for a living
// skeleton against a foe: the hook passes over a dead killer (a bone still
// flying from a skeleton that has fallen) and over a victim that died on
// the skeleton's own side (a possessed ally).
TEST_F(KitSkeleton, legion_needs_a_living_skeleton_and_a_foe)
{
    walker* skel = add_skeleton(0, 96, 96, 60.0f, 10);
    ASSERT_NE(nullptr, skel);
    ASSERT_TRUE(cast(skel, kSlotReassemble, 1).ok);

    // An ally a ghost possessed fights for the ghost's side, so the
    // skeleton may strike it down; its death hands it back to the
    // skeleton's side before the hook runs. It does not rise.
    walker* ghost = world().add_ob(Order::Living, FAMILY_GHOST);
    walker* ally = add_orc(0, 200, 160);
    ASSERT_NE(nullptr, ghost);
    ASSERT_NE(nullptr, ally);
    ghost->set_team_num(1);
    ghost->setxy(200, 160);
    ally->set_act_type(ACT_GUARD);
    ASSERT_TRUE(og::sim::possess(world(), *ghost, *ally, 0).ok);
    ASSERT_EQ(1, ally->team_num()) << "the possessed ally fights for team 1";
    skel->set_damage(1000.0f);
    ASSERT_TRUE(skel->attack(ally));
    EXPECT_TRUE(ally->dead());
    EXPECT_EQ(0, ally->team_num()) << "released to its own side as it died";
    EXPECT_EQ(1, skeletons_on_team(world(), 0)) << "an ally does not rise";
    EXPECT_EQ(1u, alive(world(), Order::Treasure, FAMILY_STAIN).size())
        << "its corpse is left for its own side";

    // A dead skeleton's bone.
    walker* foe = add_orc(1, 240, 240);
    walker* bone = world().add_ob(Order::Weapon, FAMILY_BONE);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, bone);
    bone->set_team_num(0);
    bone->set_owner(skel);
    bone->set_damage(1000.0f);
    skel->set_dead(1);
    ASSERT_TRUE(bone->attack(foe));
    EXPECT_TRUE(foe->dead());
    EXPECT_EQ(0, skeletons_on_team(world(), 0)) << "nothing rises for the dead";

    // The window closes when its skeleton is gone.
    ASSERT_EQ(1u, markers(world(), skel, 2).size());
    tick();
    EXPECT_TRUE(markers(world(), nullptr, 2).empty())
        << "no LEGION window outlives its skeleton";
}

// TUNNEL is untouched beside the kit: it still refuses mid-teleport, and
// the skeleton still levels up by the classic formula.
TEST_F(KitSkeleton, tunnel_and_level_up_stay_classic)
{
    walker* skel = add_skeleton(0, 96, 96, 50.0f);
    ASSERT_NE(nullptr, skel);
    skel->set_ani_type(ANI_TELE_IN);
    const Cast busy = cast(skel, 1, 0);
    EXPECT_EQ("SPECIAL BUSY", busy.reason);
    EXPECT_FLOAT_EQ(50.0f, skel->stats()->magicpoints());
    skel->set_ani_type(ANI_WALK);
    ASSERT_TRUE(cast(skel, 1, 0).ok);
    EXPECT_EQ(ANI_TELE_OUT, skel->ani_type());
    EXPECT_FLOAT_EQ(40.0f, skel->stats()->magicpoints()) << "TUNNEL costs 10";

    guy bones(FAMILY_SKELETON);
    const short str = bones.strength;
    og::test::level_up(*get_family_descriptor(FAMILY_SKELETON), &bones, 1);
    EXPECT_EQ(str + 8, bones.strength) << "apply_level_up(8, 12, 4, 4, 1)";
}

// Setting OFF: REASSEMBLE and LEGION are not specials (NOT ENOUGH MP), no
// ward, no window, nothing spent.
TEST_F(KitSkeleton, reassemble_off_twin_is_not_cast)
{
    world().new_specials = 0;
    walker* skel = add_skeleton(0, 96, 96, 500.0f);
    ASSERT_NE(nullptr, skel);
    for (const short shift : {short{0}, short{1}}) {
        const Cast c = cast(skel, kSlotReassemble, shift);
        EXPECT_FALSE(c.ok) << shift;
        EXPECT_EQ(walker::SpecialFailure::NoMP, c.why) << shift;
    }
    EXPECT_FLOAT_EQ(500.0f, skel->stats()->magicpoints());
    EXPECT_EQ(0, skel->kit_state());
    EXPECT_TRUE(markers(world(), skel, 2).empty());
}

// ---------------------------------------------------------------------------
// Bot gates
// ---------------------------------------------------------------------------

namespace {

bool ask_gate(walker* w, int slot, short shift = 0)
{
    w->set_current_special(static_cast<char>(slot));
    w->set_shifter_down(shift);
    return og::test::check_special_ai(*get_family_descriptor(FAMILY_SKELETON),
                                      dynamic_cast<living*>(w));
}

}  // namespace

// DIG IN's gate: hurt below 60 % and two foes within 80, with the mana to
// stay down (dig_min_pool left after the price); never a possessed body.
//
// RED (staged: the gate's pool test removed): 22 MP answers true.
TEST_F(KitSkeleton, ai_dig_in_fires_when_hurt_and_outnumbered)
{
    walker* skel = add_skeleton(1, 96, 96, 40.0f);
    walker* a = add_orc(0, 150, 96);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, a);
    skel->stats()->set_hitpoints(30.0f);
    EXPECT_FALSE(ask_gate(skel, kSlotDigIn)) << "one foe is not outnumbered";
    walker* b = add_orc(0, 96, 150);
    ASSERT_NE(nullptr, b);
    EXPECT_TRUE(ask_gate(skel, kSlotDigIn)) << "hurt, two foes close";
    skel->stats()->set_hitpoints(36.0f);
    EXPECT_FALSE(ask_gate(skel, kSlotDigIn)) << "60 % is not hurt enough";
    skel->stats()->set_hitpoints(30.0f);
    skel->set_possess_link(77u);
    EXPECT_FALSE(ask_gate(skel, kSlotDigIn)) << "a possessed body";
    skel->set_possess_link(0u);
    b->setxy(96, 190);  // 94 px
    EXPECT_FALSE(ask_gate(skel, kSlotDigIn)) << "the second foe is too far";
    b->setxy(96, 150);
    ASSERT_TRUE(ask_gate(skel, kSlotDigIn));
    skel->stats()->set_magicpoints(22.0f);
    EXPECT_FALSE(ask_gate(skel, kSlotDigIn))
        << "7 left after the price: it could not stay down";
    skel->stats()->set_magicpoints(23.0f);
    EXPECT_TRUE(ask_gate(skel, kSlotDigIn)) << "8 left: enough";
}

// BONE WALL's gate: hurt below 70 % with a foe within 60 → the wall
// (shift 0); once a wall of its own stands and two foes are within 80 and
// it can pay 45 → the storm (shift 1).
TEST_F(KitSkeleton, ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd)
{
    walker* skel = add_skeleton(1, 96, 96, 100.0f);
    walker* a = add_orc(0, 150, 96);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, a);
    EXPECT_FALSE(ask_gate(skel, kSlotWall, 1)) << "healthy";
    skel->stats()->set_hitpoints(40.0f);
    EXPECT_TRUE(ask_gate(skel, kSlotWall, 1)) << "hurt, a foe within 60";
    EXPECT_EQ(0, skel->shifter_down()) << "the wall, not the storm";
    a->setxy(170, 96);  // 74 px
    EXPECT_FALSE(ask_gate(skel, kSlotWall)) << "no foe within 60";

    skel->set_curdir(FACE_LEFT);
    ASSERT_TRUE(cast(skel, kSlotWall, 0).ok);
    EXPECT_FALSE(ask_gate(skel, kSlotWall)) << "a wall stands, one foe";
    walker* b = add_orc(0, 96, 160);
    ASSERT_NE(nullptr, b);
    EXPECT_TRUE(ask_gate(skel, kSlotWall, 0)) << "a wall and two foes";
    EXPECT_EQ(1, skel->shifter_down()) << "the storm";
    skel->stats()->set_magicpoints(44.0f);
    EXPECT_FALSE(ask_gate(skel, kSlotWall)) << "cannot pay for the storm";
}

// REASSEMBLE's gate: below half and unwarded → the ward (shift 0); above
// half with three foes within 100 and no window yet → LEGION (shift 1).
TEST_F(KitSkeleton, ai_reassemble_fires_when_hurt_and_legion_when_crowded)
{
    walker* skel = add_skeleton(1, 96, 96, 120.0f);
    ASSERT_NE(nullptr, skel);
    skel->stats()->set_hitpoints(29.0f);
    EXPECT_TRUE(ask_gate(skel, kSlotReassemble, 1)) << "hurt, unwarded";
    EXPECT_EQ(0, skel->shifter_down());
    skel->set_kit_state(KIT_WARD);
    EXPECT_FALSE(ask_gate(skel, kSlotReassemble)) << "already warded";
    skel->set_kit_state(0);

    skel->stats()->set_hitpoints(50.0f);
    walker* a = add_orc(0, 150, 96);
    walker* b = add_orc(0, 96, 150);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    EXPECT_FALSE(ask_gate(skel, kSlotReassemble)) << "two foes";
    walker* c = add_orc(0, 40, 96);
    ASSERT_NE(nullptr, c);
    EXPECT_TRUE(ask_gate(skel, kSlotReassemble, 0)) << "three foes";
    EXPECT_EQ(1, skel->shifter_down()) << "LEGION";
    ASSERT_TRUE(cast(skel, kSlotReassemble, 1).ok);
    EXPECT_FALSE(ask_gate(skel, kSlotReassemble)) << "the window is open";
}

// With the setting off every new gate answers true at once (the guard) and
// touches nothing: no RNG draw, no shift written, no foe acquired. TUNNEL's
// classic gate answers as it always did, either way.
TEST_F(KitSkeleton, ai_off_answers_the_classic_gate)
{
    walker* skel = add_skeleton(1, 96, 96, 500.0f);
    walker* orc = add_orc(0, 150, 96);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, orc);
    skel->stats()->set_hitpoints(10.0f);
    world().new_specials = 0;
    for (const int slot : {kSlotDigIn, kSlotWall, kSlotReassemble}) {
        const std::uint32_t before = world().rng_.state_;
        EXPECT_TRUE(ask_gate(skel, slot, 1)) << slot;
        EXPECT_EQ(1, skel->shifter_down()) << slot;
        EXPECT_EQ(before, world().rng_.state_) << slot;
        EXPECT_EQ(nullptr, skel->foe()) << slot;
    }
    for (const short flag : {short{0}, short{1}}) {
        world().new_specials = flag;
        EXPECT_FALSE(ask_gate(skel, 1)) << "TUNNEL: a foe within 80 ("
                                        << flag << ")";
        orc->setxy(300, 300);
        EXPECT_TRUE(ask_gate(skel, 1)) << "TUNNEL: away from anybody ("
                                       << flag << ")";
        orc->setxy(150, 96);
    }
}

// A bot skeleton, hurt and outnumbered, digs in from its own act(): the
// 1-in-5 special roll, the slot roll landing on DIG IN, the shift coin, the
// gate, the cast. One foe fewer (or the setting off) and it does not.
TEST_F(KitSkeleton, bot_skeleton_digs_in_when_outnumbered)
{
    // level 4: the slot roll is next((4 + 2) / 3) + 1, so next(2) == 1 is
    // slot 2. The coin (next(2)) can land either way: DIG IN has no
    // alternate.
    const std::uint32_t seed = state_where_draws_are({5, 2}, {0, 1});
    struct Run {
        bool dug = false;
        float mp = 0.0f;
    };
    const auto run = [&](short flag, int foes) {
        world().new_specials = flag;
        walker* skel = add_skeleton(1, 96, 96, 40.0f);
        EXPECT_NE(nullptr, skel);
        skel->set_act_type(ACT_RANDOM);
        skel->stats()->set_hitpoints(30.0f);
        std::vector<walker*> crowd;
        for (int i = 0; i < foes; ++i)
            crowd.push_back(add_orc(0, static_cast<short>(130 + 20 * i), 96));
        world().rng_.state_ = seed;
        dynamic_cast<living*>(skel)->act();
        Run r;
        r.dug = !markers(world(), skel, 1).empty() &&
                skel->ani_type() == ANI_TELE_IN;
        r.mp = skel->stats()->magicpoints();
        skel->set_dead(1);
        for (walker* w : crowd)
            w->set_dead(1);
        tick();
        return r;
    };
    const Run outnumbered = run(1, 2);
    EXPECT_TRUE(outnumbered.dug) << "hurt and outnumbered: it digs in";
    EXPECT_FLOAT_EQ(25.0f, outnumbered.mp);
    const Run alone = run(1, 1);
    EXPECT_FALSE(alone.dug) << "one foe: it stands";
    EXPECT_FLOAT_EQ(40.0f, alone.mp);
    const Run off = run(0, 2);
    EXPECT_FALSE(off.dug) << "setting off: the classic skeleton";
    EXPECT_FLOAT_EQ(40.0f, off.mp);
}

// ---------------------------------------------------------------------------
// The bone wall's art and glyph
// ---------------------------------------------------------------------------

// The bone wall loads its own two-frame 16x16 sheet, draws a visible
// curses glyph, and is chop-able solid scenery.
TEST_F(KitSkeleton, new_kit_entities_load_art_and_glyphs)
{
    const WeaponFamilyDescriptor* wfd =
        get_weapon_family_descriptor(bone_wall_family());
    ASSERT_NE(nullptr, wfd);
    ASSERT_NE(nullptr, wfd->pix_filename);
    EXPECT_STREQ("bonewall.png", wfd->pix_filename);
    const PixieData art = read_pixie_file(wfd->pix_filename);
    ASSERT_TRUE(art.valid());
    EXPECT_EQ(2, art.frames);
    EXPECT_EQ(16, art.w);
    EXPECT_EQ(16, art.h);
    EXPECT_FALSE(wfd->glyph.transparent);
    EXPECT_EQ('#', wfd->glyph.ascii);
    EXPECT_EQ(U'▒', wfd->glyph.codepoint);
    EXPECT_TRUE(wfd->blocks_placement);
    EXPECT_TRUE(wfd->is_auto_attackable);
    walker* seg = world().add_weap_ob(Order::Weapon, bone_wall_family());
    ASSERT_NE(nullptr, seg);
    EXPECT_EQ(16, seg->sizex()) << "the world builds it from its art";
    EXPECT_EQ(16, seg->sizey());
    EXPECT_EQ(1, seg->ani_type()) << "built on the animate path";
}
