/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch presentation-1: what the simulation hands each
// machine for its sounds and its display effects. Each test is named after
// the rule it asserts.
//
// Sounds (#468). In the old game every in-world sound played only while its
// walker was on screen (pixie::on_screen, asked of the walker the call sat
// in). The simulation cannot ask that: the server is headless and every
// machine has its own views. So each in-world sound is placed: its event
// carries the source's box in Event::c (sound_source.h), and each machine
// plays it only while one of its own views shows that box (the screen's
// dispatcher, tests/integration/test_runtime_coverage_paths.cpp). A cue the
// old game played whatever the view (the YO, a seat's failure clang) goes
// out unplaced, c == 0. The tests here pin the placing: each sound is placed
// at exactly the walker the old game asked on_screen() of. No visibility test
// is installed unless a test says so, as in play.
//
// Display effects (#484). The old game decided each display effect where it
// was made, from its one machine's effect toggles (and, for the melee lunge,
// only on screen). The current game records them all in the simulation and
// each machine's drawing code applies its own toggles (walker_draw.cpp). The
// tests here turn every toggle off and show the simulation recording the
// marks anyway.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/view_box.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/sound_source.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gparser.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using og::sim::pack_sound_source;
using og::sim::SoundSource;
using og::sim::unpack_sound_source;

bool never_visible(const walker*, std::uint32_t, void*) { return false; }

// The process cfg's effect toggles as the test found them.
struct EffectOverridesRestore
{
    decltype(cfg.overrides) saved = cfg.overrides;
    ~EffectOverridesRestore() { cfg.overrides = saved; }
};

class SemPresentation : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    og::test::ScopedHookFailureGuard guard_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        fresh_world();
    }

    void TearDown() override
    {
        if (current_game != nullptr)
        {
            current_game->positional_sound_visible = nullptr;
            current_game->positional_sound_visible_user = nullptr;
        }
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    // An open field of grass, 1024 px a side, nobody on it, the setting off,
    // and no visibility test installed (play installs none).
    void fresh_world()
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(64, 64);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = 0;
        w.new_specials = 0;
        w.allied_mode = 0;
        w.enemy_freeze = 0;
        w.rng_.state_ = 12345u;
    }

    GameWorld& world() { return tw_->world(); }
    og::sim::SimEventLog& events() { return tw_->events; }

    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // Sizes first, then the position: the obmap files a body under every
    // cell its box covers at the moment it is placed.
    static walker* place(walker* w, short x, short y, short sx, short sy)
    {
        if (w == nullptr)
            return nullptr;
        w->set_sizex(sx);
        w->set_sizey(sy);
        w->setxy(static_cast<short>(x + 1), y);  // force a real move
        w->setxy(x, y);
        return w;
    }

    // A bot (no seat, no character), 400 hp, `mp` mana, no regeneration.
    walker* living_at(int family, unsigned char team, short x, short y,
                      short level = 4, float mp = 500.0f, short sx = 16,
                      short sy = 16)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_act_type(ACT_RANDOM);
        w->stats()->set_level(level);
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_max_magicpoints(std::max(mp, 1.0f));
        w->stats()->set_magicpoints(mp);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_max_magic_delay(1000000);
        w->set_busy(0.0f);
        w->set_user(-1);
        return place(w, x, y, sx, sy);
    }

    walker* treasure_at(int family, short x, short y, short level = 3)
    {
        walker* w = world().add_fx_ob(Order::Treasure, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(0);
        w->stats()->set_level(level);
        return place(w, x, y, 16, 16);
    }

    static void make_hero(walker* w, short intelligence = 0)
    {
        auto g = std::make_unique<guy>(static_cast<int>(w->family()));
        g->intelligence = intelligence;
        g->teamnum = w->team_num();
        g->name = "Gorik";
        w->set_owned_myguy(std::move(g));
    }

    // The cast as play makes it: walker::special, the slot's price charged on
    // success.
    static bool cast(walker* w, int slot, short shift, std::string* reason)
    {
        w->set_current_special(static_cast<char>(slot));
        w->set_shifter_down(shift);
        walker::SpecialFailure why = walker::SpecialFailure::None;
        return w->special(&why, reason);
    }

    // Where a walker's sound is placed: its box as the event packs it.
    static std::uint32_t at(const walker* w)
    {
        return pack_sound_source(w->xpos(), w->ypos(), w->sizex(), w->sizey());
    }

    // The place (Event::c) of every queued sound `id`, in queue order.
    std::vector<std::uint32_t> placed(int id)
    {
        std::vector<std::uint32_t> out;
        for (const og::sim::Event& e : events().events())
            if (e.kind == og::sim::EventKind::PlaySound &&
                e.a == static_cast<std::uint32_t>(id))
                out.push_back(e.c);
        return out;
    }

    // The live entities of an order and family, on all three lists.
    std::vector<walker*> live(Order order, int family)
    {
        std::vector<walker*> out;
        for (const GameWorld::EntityList* list :
             {&world().oblist, &world().fxlist, &world().weaplist})
            for (const auto& ob : *list)
                if (ob != nullptr && ob->dead() == 0 && ob->query_order() == order &&
                    static_cast<int>(static_cast<unsigned char>(ob->family())) == family)
                    out.push_back(ob.get());
        return out;
    }

    // One input tick of global player 0 driving `control`.
    void run_seat(walker* control, InputAction action, SimInputDebounce& debounce)
    {
        InputState input;
        input.clear();
        input.players[0].pressed[static_cast<int>(action)] = true;
        input.players[0].held[static_cast<int>(action)] = true;
        (void)sim_process_player_input(input.players[0], control, world(), 0, 0,
                                       debounce, &events());
    }
};

}  // namespace

// ===========================================================================
// sound_source.h, view_box.h: the place a sound carries and the test of it
// ===========================================================================

// The place packs x, y and the larger side of the box; coordinates clamp to
// 0..4095, the side to 1..255, and nothing packs to 0, which stays the
// unplaced cue's mark.
TEST_F(SemPresentation, a_sound_source_packs_x_y_and_the_larger_side_and_never_packs_to_zero)
{
    EXPECT_EQ((std::optional<SoundSource>{SoundSource{320, 320, 16}}),
              unpack_sound_source(pack_sound_source(320, 320, 16, 16)))
        << "a 16 x 16 walker at (320, 320) round-trips";
    EXPECT_EQ((320u << 20) | (320u << 8) | 16u, pack_sound_source(320, 320, 16, 16))
        << "x in bits 20-31, y in bits 8-19, the side in bits 0-7";
    EXPECT_EQ((std::optional<SoundSource>{SoundSource{0, 0, 1}}),
              unpack_sound_source(pack_sound_source(0, 0, 0, 0)))
        << "a box with no size at the map's corner packs to side 1";
    EXPECT_NE(0u, pack_sound_source(0, 0, 0, 0)) << "and so never to 0";
    EXPECT_EQ(0, unpack_sound_source(pack_sound_source(-5, 7, 16, 16))->x)
        << "x below the map clamps to 0";
    EXPECT_EQ(7, unpack_sound_source(pack_sound_source(-5, 7, 16, 16))->y);
    EXPECT_EQ(4095, unpack_sound_source(pack_sound_source(5000, 7, 16, 16))->x)
        << "x past 4095 clamps to 4095";
    EXPECT_EQ(4095, unpack_sound_source(pack_sound_source(7, 5000, 16, 16))->y)
        << "y past 4095 clamps to 4095";
    EXPECT_EQ(255, unpack_sound_source(pack_sound_source(7, 7, 300, 16))->span)
        << "a side past 255 clamps to 255";
    EXPECT_EQ(29, unpack_sound_source(pack_sound_source(7, 7, 25, 29))->span)
        << "the side is the larger of the two (a 25 x 29 tower)";
    EXPECT_EQ(25, unpack_sound_source(pack_sound_source(7, 7, 25, 3))->span)
        << "whichever axis it is on";
    EXPECT_EQ(std::nullopt, unpack_sound_source(0u)) << "0 is unplaced";
}

// The old game's on-screen test: a box is off a view only when it lies
// wholly to one side of it, so a box touching an edge counts as on. Each
// edge is shown on and one pixel past it off; the view is 100 x 80 at
// (200, 300).
TEST_F(SemPresentation, the_on_screen_box_test_counts_a_box_touching_the_view_edge_as_on)
{
    constexpr std::int32_t tx = 200, ty = 300, xv = 100, yv = 80;
    const auto on = [](std::int32_t x, std::int32_t y) {
        return og::core::box_on_view(x, y, 16, 16, tx, ty, xv, yv);
    };
    EXPECT_TRUE(on(250, 340)) << "inside";
    EXPECT_TRUE(on(tx - 16, 340)) << "left: x + w == topx is on";
    EXPECT_FALSE(on(tx - 17, 340)) << "left: x + w == topx - 1 is off";
    EXPECT_TRUE(on(tx + xv, 340)) << "right: x == topx + xview is on";
    EXPECT_FALSE(on(tx + xv + 1, 340)) << "right: x == topx + xview + 1 is off";
    EXPECT_TRUE(on(250, ty - 16)) << "top: y + h == topy is on";
    EXPECT_FALSE(on(250, ty - 17)) << "top: y + h == topy - 1 is off";
    EXPECT_TRUE(on(250, ty + yv)) << "bottom: y == topy + yview is on";
    EXPECT_FALSE(on(250, ty + yv + 1)) << "bottom: y == topy + yview + 1 is off";
}

// ===========================================================================
// sim_emit.h: placed sounds and cues
// ===========================================================================

// A landed blow's clang is placed at the soldier that swung; the YO and a
// seat's failure clang, which the old game played whatever the view, go out
// unplaced; so does a sound with no walker to place it at.
TEST_F(SemPresentation, a_positional_sound_is_placed_at_its_source_and_a_cue_is_not)
{
    walker* soldier = living_at(FAMILY_SOLDIER, 0, 320, 320);
    walker* orc = living_at(FAMILY_ORC, 1, 337, 320);
    ASSERT_TRUE(soldier && orc);
    soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
    soldier->set_enddir(static_cast<char>(FACE_RIGHT));
    soldier->set_lastx(1.0f);
    soldier->set_lasty(0.0f);
    events().clear();
    EXPECT_EQ(nullptr, soldier->fire());
    const std::vector<std::uint32_t> clang = placed(SOUND_CLANG);
    ASSERT_EQ(1u, clang.size()) << "the blow lands and clangs once";
    EXPECT_EQ((std::optional<SoundSource>{SoundSource{320, 320, 16}}),
              unpack_sound_source(clang[0]))
        << "the clang is placed at the soldier's box";

    // The YO: a seated hero yells.
    fresh_world();
    walker* hero = living_at(FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_NE(nullptr, hero);
    hero->set_user(0);
    hero->set_act_type(ACT_CONTROL);
    hero->set_yo_delay(0);
    events().clear();
    SimInputDebounce yell{};
    run_seat(hero, InputAction::Yell, yell);
    EXPECT_EQ(std::vector<std::uint32_t>{0u}, placed(SOUND_YO))
        << "the YO is a cue: unplaced";

    // A seat's failure clang: a seated body whose slot is not in play.
    fresh_world();
    walker* knife = world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->setxy(200, 200);
    knife->set_team_num(0);
    knife->set_user(0);
    knife->set_act_type(ACT_CONTROL);
    knife->set_current_special(1);
    knife->stats()->set_special_cost(1, kSpecialCostDisabled);
    knife->stats()->set_max_magicpoints(100.0f);
    knife->stats()->set_magicpoints(100.0f);
    events().clear();
    SimInputDebounce cue{};
    run_seat(knife, InputAction::Special, cue);
    ASSERT_EQ(1u, placed(SOUND_CLANG).size()) << "the failure is voiced with a clang";
    EXPECT_EQ(0u, placed(SOUND_CLANG)[0]) << "the seat's clang is a cue: unplaced";
    EXPECT_EQ(0, events().events().back().target_player)
        << "and it is still addressed to its seat";

    EXPECT_EQ(0u, og::sim::sound_source_of(nullptr)) << "no walker: unplaced";
}

// The parity recorder's visibility test still decides whether a placed sound
// is queued at all: answering "off view", no clang and no generator
// explosion is queued, as the old game played neither off screen.
TEST_F(SemPresentation, the_recorders_off_view_answer_still_drops_a_positional_sound)
{
    walker* soldier = living_at(FAMILY_SOLDIER, 0, 320, 320);
    walker* orc = living_at(FAMILY_ORC, 1, 337, 320);
    ASSERT_TRUE(soldier && orc);
    soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
    soldier->set_enddir(static_cast<char>(FACE_RIGHT));
    soldier->set_lastx(1.0f);
    soldier->set_lasty(0.0f);
    current_game->positional_sound_visible = &never_visible;
    events().clear();
    EXPECT_EQ(nullptr, soldier->fire());
    EXPECT_GT(400.0f, orc->stats()->hitpoints()) << "the blow landed";
    EXPECT_TRUE(placed(SOUND_CLANG).empty()) << "off view: no clang";

    walker* tower = world().add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_NE(nullptr, tower);
    place(tower, 600, 600, 40, 40);
    tower->stats()->set_level(3);
    tower->set_dead(1);
    tower->death();
    EXPECT_EQ(4u, live(Order::FX, FAMILY_EXPLOSION).size()) << "the tower still bursts";
    EXPECT_TRUE(placed(SOUND_EXPLODE).empty()) << "off view: no explosion sound";
}

// ===========================================================================
// The class pack's casts (packs/core/families, packs/core/lib)
// ===========================================================================

// Every classic cast's sound is placed at the caster, the walker the old
// game asked on_screen() of (walker.cpp at 7ff1654a: CHARGE :2612 and :2719,
// HEAL :2796, TURN UNDEAD :2881, HEARTBURST :3265, the archmage's burst
// :3388, PROTECTION :4190, HOWL :4234). HEARTBURST and the archmage's burst
// make their explosions on the foes but sound at the caster.
TEST_F(SemPresentation, every_classic_cast_sound_is_placed_at_its_caster)
{
    std::string reason;

    {   // the soldier's CHARGE, nothing in front
        fresh_world();
        walker* soldier = living_at(FAMILY_SOLDIER, 1, 200, 200);
        ASSERT_NE(nullptr, soldier);
        soldier->set_stepsize(4.0f);
        soldier->set_lastx(4.0f);
        soldier->set_lasty(0.0f);
        soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
        events().clear();
        ASSERT_TRUE(cast(soldier, 1, 0, &reason)) << "CHARGE: " << reason;
        EXPECT_EQ(std::vector<std::uint32_t>{at(soldier)}, placed(SOUND_CHARGE))
            << "CHARGE sounds at the soldier";
    }
    {   // the soldier's DISARM, a tower in front
        fresh_world();
        walker* soldier = living_at(FAMILY_SOLDIER, 1, 200, 200);
        walker* tower = living_at(FAMILY_TOWER1, 2, 212, 200, 4, 0.0f, 25, 29);
        ASSERT_TRUE(soldier && tower);
        soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
        soldier->set_lastx(4.0f);
        events().clear();
        ASSERT_TRUE(cast(soldier, 4, 0, &reason)) << "DISARM: " << reason;
        EXPECT_EQ(std::vector<std::uint32_t>{at(soldier)}, placed(SOUND_CHARGE))
            << "DISARM's CHARGE sounds at the soldier, not the tower";
        EXPECT_NE(at(tower), at(soldier));
    }
    {   // the cleric's HEAL
        fresh_world();
        walker* cleric = living_at(FAMILY_CLERIC, 0, 160, 160, 4, 100.0f);
        walker* ally = living_at(FAMILY_SOLDIER, 0, 180, 160);
        ASSERT_TRUE(cleric && ally);
        ally->stats()->set_hitpoints(20.0f);
        events().clear();
        ASSERT_TRUE(cast(cleric, 1, 0, &reason)) << "HEAL: " << reason;
        EXPECT_EQ(std::vector<std::uint32_t>{at(cleric)}, placed(SOUND_HEAL))
            << "HEAL sounds at the cleric, not the healed";
    }
    {   // the cleric's TURN UNDEAD (a hero's classic turn with nothing turned)
        fresh_world();
        walker* cleric = living_at(FAMILY_CLERIC, 0, 160, 160, 10, 500.0f);
        walker* orc = living_at(FAMILY_ORC, 2, 180, 160, 1);
        ASSERT_TRUE(cleric && orc);
        make_hero(cleric, 100);
        events().clear();
        ASSERT_TRUE(cast(cleric, 2, 1, &reason)) << "TURN UNDEAD: " << reason;
        EXPECT_EQ(std::vector<std::uint32_t>{at(cleric)}, placed(SOUND_HEAL))
            << "TURN UNDEAD sounds at the cleric";
    }
    {   // the mage's HEARTBURST on two foes
        fresh_world();
        walker* mage = living_at(FAMILY_MAGE, 0, 160, 160, 4, 1000.0f);
        walker* east = living_at(FAMILY_ORC, 1, 200, 160);
        walker* south = living_at(FAMILY_ORC, 1, 160, 204);
        ASSERT_TRUE(mage && east && south);
        events().clear();
        ASSERT_TRUE(cast(mage, 5, 0, &reason)) << "HEARTBURST: " << reason;
        ASSERT_EQ(2u, live(Order::FX, FAMILY_EXPLOSION).size()) << "one burst on each foe";
        EXPECT_EQ((std::vector<std::uint32_t>{at(mage), at(mage)}), placed(SOUND_EXPLODE))
            << "both bursts sound at the mage, not the foe they land on";
    }
    {   // the archmage's HEARTBURST on three foes
        fresh_world();
        walker* arch = living_at(FAMILY_ARCHMAGE, 1, 200, 200, 8, 1200.0f);
        walker* o1 = living_at(FAMILY_ORC, 2, 210, 200, 1);
        walker* o2 = living_at(FAMILY_ORC, 2, 220, 200, 1);
        walker* o3 = living_at(FAMILY_ORC, 2, 230, 200, 1);
        ASSERT_TRUE(arch && o1 && o2 && o3);
        arch->stats()->set_special_cost(2, 0);
        events().clear();
        ASSERT_TRUE(cast(arch, 2, 0, &reason)) << "archmage burst: " << reason;
        ASSERT_EQ(3u, live(Order::FX, FAMILY_EXPLOSION).size()) << "one burst on each foe";
        EXPECT_EQ((std::vector<std::uint32_t>{at(arch), at(arch), at(arch)}),
                  placed(SOUND_EXPLODE))
            << "every burst sounds at the archmage";
    }
    {   // the druid's PROTECTION
        fresh_world();
        walker* druid = living_at(FAMILY_DRUID, 1, 160, 160, 5, 1000.0f);
        walker* friend_ = living_at(FAMILY_SOLDIER, 1, 160, 190, 1);
        ASSERT_TRUE(druid && friend_);
        events().clear();
        ASSERT_TRUE(cast(druid, 4, 0, &reason)) << "PROTECTION: " << reason;
        EXPECT_EQ(std::vector<std::uint32_t>{at(druid)}, placed(SOUND_HEAL))
            << "PROTECTION sounds at the druid, not the protected";
    }
    {   // the orc's HOWL
        fresh_world();
        walker* orc = living_at(FAMILY_ORC, 1, 160, 160);
        walker* foe = living_at(FAMILY_SOLDIER, 0, 200, 160, 1);
        ASSERT_TRUE(orc && foe);
        events().clear();
        ASSERT_TRUE(cast(orc, 1, 0, &reason)) << "HOWL: " << reason;
        EXPECT_EQ(std::vector<std::uint32_t>{at(orc)}, placed(SOUND_ROAR))
            << "HOWL roars at the orc";
    }
}

// A drumstick, a gold bar and a silver bar sound where they lie; a key
// sounds at its eater. The old game asked the treasure (treasure.cpp at
// 7ff1654a :85, :94, :103) and, for the key, `eater->on_screen()` (:311).
// Each treasure lies 200 px from its eater, so the two places differ.
TEST_F(SemPresentation, a_pickup_sounds_at_the_treasure_and_a_key_at_its_eater)
{
    struct Pickup { int family; int sound; bool at_eater; const char* arm; };
    for (const Pickup p : {Pickup{FAMILY_DRUMSTICK, SOUND_EAT, false, "drumstick"},
                           Pickup{FAMILY_GOLD_BAR, SOUND_MONEY, false, "gold bar"},
                           Pickup{FAMILY_SILVER_BAR, SOUND_MONEY, false, "silver bar"},
                           Pickup{FAMILY_KEY, SOUND_MONEY, true, "key"}})
    {
        fresh_world();
        walker* eater = living_at(FAMILY_SOLDIER, 0, 300, 300);
        walker* item = treasure_at(p.family, 96, 96);
        ASSERT_TRUE(eater && item) << p.arm;
        eater->stats()->set_hitpoints(50.0f);  // a drumstick wants a hurt eater
        eater->set_keys(0);
        const std::uint32_t want = p.at_eater ? at(eater) : at(item);
        ASSERT_NE(at(eater), at(item)) << p.arm;
        events().clear();
        ASSERT_TRUE(og::test::on_eat(*get_treasure_family_descriptor(p.family),
                                     static_cast<treasure*>(item), eater))
            << p.arm;
        EXPECT_EQ(std::vector<std::uint32_t>{want}, placed(p.sound))
            << p.arm << (p.at_eater ? ": sounds at the eater" : ": sounds at the treasure");
    }
}

// A fire arrow, a bomb and a chain-lightning bolt each sound their explosion
// at themselves (weap.cpp :200, effect.cpp :637 and :426 at 7ff1654a). The
// arrow and the bomb blow up as they die; the bolt as it strikes its target.
TEST_F(SemPresentation, an_explosion_sounds_at_what_blew)
{
    {
        walker* archer = living_at(FAMILY_ARCHER, 0, 100, 100, 7);
        walker* arrow = world().add_ob(Order::Weapon, FAMILY_FIRE_ARROW);
        ASSERT_TRUE(archer && arrow);
        arrow->setxy(200, 200);
        arrow->set_owner(archer);
        arrow->set_skip_exit(1);
        arrow->set_damage(7.0f);
        arrow->set_dead(1);
        const std::uint32_t want = at(arrow);
        events().clear();
        ASSERT_TRUE(arrow->death());
        EXPECT_EQ(std::vector<std::uint32_t>{want}, placed(SOUND_EXPLODE))
            << "the fire arrow sounds where it blew";
        EXPECT_NE(at(archer), want);
    }
    {
        fresh_world();
        walker* thief = living_at(FAMILY_THIEF, 0, 300, 300);
        walker* bomb = world().add_ob(Order::FX, FAMILY_BOMB);
        ASSERT_TRUE(thief && bomb);
        bomb->setxy(128, 128);
        bomb->set_owner(thief);
        bomb->set_damage(12.0f);
        bomb->set_dead(1);
        const std::uint32_t want = at(bomb);
        events().clear();
        ASSERT_TRUE(bomb->death());
        EXPECT_EQ(std::vector<std::uint32_t>{want}, placed(SOUND_EXPLODE))
            << "the bomb sounds where it blew, not at its thrower";
        EXPECT_NE(at(thief), want);
    }
    {
        fresh_world();
        walker* owner = living_at(FAMILY_SOLDIER, 0, 50, 600, 1);
        walker* leader = living_at(FAMILY_ORC, 1, 100, 100, 1);
        walker* bolt = world().add_ob(Order::FX, FAMILY_CHAIN);
        ASSERT_TRUE(owner && leader && bolt);
        bolt->setxy(100, 100);
        bolt->set_team_num(0);
        bolt->set_owner(owner);
        bolt->set_leader(leader);
        bolt->set_damage(10.0f);
        bolt->set_lineofsight(20);
        const std::uint32_t want = at(bolt);
        events().clear();
        ASSERT_TRUE(bolt->act());
        ASSERT_EQ(1u, live(Order::FX, FAMILY_EXPLOSION).size()) << "the bolt struck";
        EXPECT_EQ(std::vector<std::uint32_t>{want}, placed(SOUND_EXPLODE))
            << "the bolt sounds where it struck, not at its caster";
        EXPECT_NE(at(owner), want);
    }
}

// ===========================================================================
// src/gameplay/walker.cpp, walker_combat.cpp: deaths
// ===========================================================================

// A destroyed generator's four explosions each sound at the generator (the
// old game: `if (on_screen())` before each, walker.cpp :5123-5124 at
// 7ff1654a; #445).
TEST_F(SemPresentation, a_dying_generators_explosions_sound_at_the_generator)
{
    walker* tower = world().add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_NE(nullptr, tower);
    place(tower, 120, 120, 40, 40);
    tower->stats()->set_level(3);
    events().clear();
    tower->set_dead(1);
    tower->death();
    ASSERT_EQ(4u, live(Order::FX, FAMILY_EXPLOSION).size()) << "four explosions";
    EXPECT_EQ((std::vector<std::uint32_t>{at(tower), at(tower), at(tower), at(tower)}),
              placed(SOUND_EXPLODE))
        << "each explosion sounds at the generator";
    EXPECT_EQ((std::optional<SoundSource>{SoundSource{120, 120, 40}}),
              unpack_sound_source(at(tower)));
}

// A living's death cry is placed at the hitter, the walker whose on_screen()
// the old game asked (walker.cpp :2253 at 7ff1654a), not the victim. The cry's
// coin is drawn in play whether or not any machine hears it.
TEST_F(SemPresentation, a_death_cry_is_placed_at_the_hitter)
{
    walker* red = living_at(FAMILY_SOLDIER, 0, 96, 96);
    walker* orc = living_at(FAMILY_ORC, 1, 128, 96);
    ASSERT_TRUE(red && orc);
    red->set_damage(5000.0f);
    orc->stats()->set_max_hitpoints(1.0f);
    orc->stats()->set_hitpoints(1.0f);
    events().clear();
    ASSERT_EQ(1, static_cast<int>(red->attack(orc)));
    ASSERT_EQ(1, static_cast<int>(orc->dead()));
    std::vector<std::uint32_t> cries = placed(SOUND_DIE1);
    const std::vector<std::uint32_t> die2 = placed(SOUND_DIE2);
    cries.insert(cries.end(), die2.begin(), die2.end());
    EXPECT_EQ(std::vector<std::uint32_t>{at(red)}, cries)
        << "one cry, placed at the hitter";
    EXPECT_NE(at(orc), at(red));
}

// ===========================================================================
// Display effects: the simulation records them whatever this machine's
// toggles (#484)
// ===========================================================================

// A landed blow sets the full lunge along the attacker's facing with the
// attack-lunge effect off and the recorder's view answering "off view": the
// old game skipped both (walker.cpp :784-795 at 7ff1654a). Only the sound
// follows the view; each machine's drawing reads the toggle.
TEST_F(SemPresentation, a_landed_blow_lunges_whatever_the_lunge_setting_or_the_view)
{
    EffectOverridesRestore restore;
    cfg.apply_override("effects", "attack_lunge", "off");
    ASSERT_FALSE(cfg.is_on("effects", "attack_lunge"));
    walker* soldier = living_at(FAMILY_SOLDIER, 0, 320, 320);
    walker* orc = living_at(FAMILY_ORC, 1, 337, 320);
    ASSERT_TRUE(soldier && orc);
    soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
    soldier->set_enddir(static_cast<char>(FACE_RIGHT));
    soldier->set_lastx(1.0f);
    soldier->set_lasty(0.0f);
    current_game->positional_sound_visible = &never_visible;
    events().clear();
    EXPECT_EQ(nullptr, soldier->fire());
    EXPECT_GT(400.0f, orc->stats()->hitpoints()) << "the blow landed";
    EXPECT_FLOAT_EQ(1.0f, soldier->attack_lunge()) << "a full lunge, toggle off, off view";
    EXPECT_FLOAT_EQ(soldier->get_current_angle(), soldier->attack_lunge_angle())
        << "along the soldier's facing";
    EXPECT_TRUE(placed(SOUND_CLANG).empty()) << "the recorder dropped the clang";
}

// A walker stuck in a wall loses its point and queues its red 1 with the
// damage-numbers effect off (the old game pushed it only with the effect on,
// living.cpp :175-176 at 7ff1654a).
TEST_F(SemPresentation, a_wall_bite_queues_its_red_one_with_damage_numbers_off)
{
    EffectOverridesRestore restore;
    cfg.apply_override("effects", "damage_numbers", "off");
    ASSERT_FALSE(cfg.is_on("effects", "damage_numbers"));
    tile(10, 10, PIX_H_WALL1);
    walker* stuck = living_at(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, stuck);
    stuck->set_act_type(ACT_CONTROL);
    stuck->stats()->set_max_hitpoints(100.0f);
    stuck->stats()->set_hitpoints(10.0f);
    stuck->stats()->set_heal_per_round(0.0f);
    stuck->stats()->set_max_heal_delay(30000);
    stuck->set_regen_delay(0);
    stuck->damage_numbers.clear();
    stuck->act();
    EXPECT_FLOAT_EQ(9.0f, stuck->stats()->hitpoints()) << "the wall bites";
    ASSERT_EQ(1u, stuck->damage_numbers.size()) << "one number, toggle off";
    EXPECT_FLOAT_EQ(1.0f, stuck->damage_numbers.front().value);
    EXPECT_EQ(RED, stuck->damage_numbers.front().color);
}

// A hit queues the attacker's orange and the victim's red number, flashes
// and recoils the victim and spawns its hit animation; a heal queues its two
// numbers in colour 56; every display effect off (the old game gated each at
// the source: walker.cpp :1871, :1881, :1892, :1922, :1925 at 7ff1654a).
TEST_F(SemPresentation, hits_and_heals_queue_their_marks_with_every_display_effect_off)
{
    EffectOverridesRestore restore;
    for (const char* key : {"damage_numbers", "heal_numbers", "hit_flash", "hit_recoil",
                            "hit_anim"}) {
        cfg.apply_override("effects", key, "off");
        ASSERT_FALSE(cfg.is_on("effects", key)) << key;
    }
    walker* soldier = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* orc = living_at(FAMILY_ORC, 1, 130, 140);
    ASSERT_TRUE(soldier && orc);
    soldier->damage_numbers.clear();
    orc->damage_numbers.clear();
    const std::size_t flashes = live(Order::FX, FAMILY_HIT).size();
    soldier->do_hit_effects(soldier, orc, 5);
    ASSERT_EQ(1u, soldier->damage_numbers.size()) << "the attacker's number";
    EXPECT_EQ(235, soldier->damage_numbers.back().color) << "orange";
    ASSERT_EQ(1u, orc->damage_numbers.size()) << "the victim's number";
    EXPECT_EQ(RED, orc->damage_numbers.back().color);
    EXPECT_FLOAT_EQ(5.0f, orc->damage_numbers.back().value);
    EXPECT_TRUE(orc->hurt_flash()) << "the hurt flash";
    EXPECT_FLOAT_EQ(1.0f, orc->hit_recoil()) << "the recoil";
    EXPECT_EQ(flashes + 1, live(Order::FX, FAMILY_HIT).size()) << "the hit animation";

    walker* cleric = living_at(FAMILY_CLERIC, 0, 300, 300);
    walker* ally = living_at(FAMILY_SOLDIER, 0, 320, 300);
    ASSERT_TRUE(cleric && ally);
    cleric->damage_numbers.clear();
    ally->damage_numbers.clear();
    cleric->do_heal_effects(cleric, ally, 7);
    ASSERT_EQ(1u, cleric->damage_numbers.size()) << "the healer's number";
    EXPECT_EQ(56, cleric->damage_numbers.back().color);
    ASSERT_EQ(1u, ally->damage_numbers.size()) << "the healed's number";
    EXPECT_EQ(56, ally->damage_numbers.back().color);
    EXPECT_FLOAT_EQ(7.0f, ally->damage_numbers.back().value);
}
