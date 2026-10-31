/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The two probes the Gameplay Fixes casts and bot gates ask with.
//
// 1. The quiet passability probe: GameWorld::query_passable(x, y, ob, floor,
//    true) asks what the loud call asks and changes nothing. The loud call is
//    the walker's own step: it eats the treasure it touches, opens a door it
//    holds the key to, posts "Key N needed!", calls collide() on the body it
//    meets (a living starts a swing) and rolls the weapon-vs-weapon miss and
//    the arrow slit's gamble. Every blocker kind is asked both ways here: the
//    answers agree, and the world after the quiet call is the world before
//    it. The one deliberate difference, an enemy weapon in flight on the box
//    (quiet passes it; loud rolls), is pinned on its own.
// 2. The detached body probe: a body that does not exist yet (the raised
//    corpse, the weapon on its pad) is a walker built with the probe tag,
//    never in a list or the obmap. body_spot_clear / body_spot_near place a
//    living body; walker::fire_pad_answer / fire_pad_clear ask fire()'s own
//    pad (walker::weapon_pad, the one table fire() and set_weapon_heading
//    read), for the current weapon or a named one.
//
// Plus GameWorld::find_blood_in_reach and the six Lua doors to all of it:
// s_forward_blocked(quiet), og.quiet_passable, og.body_spot_near,
// og.blood_in_reach, walker:fire_pad_clear and walker:fire_pad_answer.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

// Everything a probe could change, read at one moment.
struct WorldState
{
    std::size_t obmap = 0;
    std::size_t oblist = 0;
    std::size_t fxlist = 0;
    std::size_t weaplist = 0;
    std::size_t events = 0;
    std::uint32_t rng = 0;
    const walker* collide_ob = nullptr;
    float busy = 0.0f;
    signed char curdir = 0;
    char enddir = 0;
    char ani_type = 0;
    short x = 0;
    short y = 0;
    std::vector<bool> dead;  // the watched bodies, in the order given

    bool operator==(const WorldState&) const = default;

    std::string describe() const
    {
        std::string d;
        for (const bool b : dead)
            d += b ? '1' : '0';
        return std::format(
            "obmap {} oblist {} fxlist {} weaplist {} events {} rng {} "
            "collide_ob {} busy {} curdir {} enddir {} ani_type {} at {},{} "
            "dead {}",
            obmap, oblist, fxlist, weaplist, events, rng,
            static_cast<const void*>(collide_ob), busy,
            static_cast<int>(curdir), static_cast<int>(enddir),
            static_cast<int>(ani_type), x,
            y, d);
    }
};

using Answers = std::map<std::string, std::string>;

class QuietProbe : public ::testing::Test {
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
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    // An open field of grass, 1024 px a side, with nobody on it.
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
        w.rng_.state_ = 12345u;
    }

    GameWorld& world() { return tw_->world(); }

    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // Sizes first, then the position: the obmap registers a body in every
    // cell its box covers at the moment it is placed.
    template <class W>
    W* place(W* w, short x, short y, short sx, short sy)
    {
        if (w == nullptr)
            return nullptr;
        w->set_sizex(sx);
        w->set_sizey(sy);
        w->setxy(static_cast<short>(x + 1), y);  // force a real move
        w->setxy(x, y);
        return w;
    }

    walker* living_at(int family, unsigned char team, short x, short y,
                      short sx = 16, short sy = 16)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_act_type(ACT_RANDOM);
        w->stats()->set_level(4);
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_max_magicpoints(500.0f);
        w->stats()->set_magicpoints(500.0f);
        w->stats()->set_magic_per_round(0.0f);
        w->set_busy(0.0f);
        return place(w, x, y, sx, sy);
    }

    walker* weapon_at(int family, unsigned char team, short x, short y,
                      short sx = 6, short sy = 6)
    {
        walker* w = world().add_weap_ob(Order::Weapon, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        return place(w, x, y, sx, sy);
    }

    walker* stain_at(short x, short y, int floor = 0)
    {
        walker* w = world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
        if (w == nullptr)
            return nullptr;
        w->set_floor(static_cast<short>(floor));
        return place(w, x, y, 16, 16);
    }

    WorldState state(const walker* prober,
                     const std::vector<const walker*>& watched = {})
    {
        WorldState s;
        s.obmap = world().myobmap->size();
        s.oblist = world().oblist.size();
        s.fxlist = world().fxlist.size();
        s.weaplist = world().weaplist.size();
        s.events = tw_->events.size();
        s.rng = world().rng_.state_;
        if (prober != nullptr) {
            s.collide_ob = prober->collide_ob();
            s.busy = prober->busy();
            s.curdir = prober->curdir();
            s.enddir = prober->enddir();
            s.ani_type = prober->ani_type();
            s.x = prober->xpos();
            s.y = prober->ypos();
        }
        for (const walker* w : watched)
            s.dead.push_back(w->dead() != 0);
        return s;
    }

    // Runs `lua` as core:soldier's do_special for `self` and returns what it
    // logged as og.log('key', value), keyed by the first field.
    Answers lua(walker* self, const std::string& body)
    {
        og::script::register_pack_script(
            {"test.quiet_probe", "quiet_probe.lua",
             "local G = og.use('core:gates')\n"
             "og.register_hooks('living', 'core:soldier', {\n"
             "  do_special = function(self)\n"
             "    og.log('<probe>')\n" +
                 body +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
        const auto handled = og::script::hooks::do_special(
            get_family_descriptor(FAMILY_SOLDIER), self);
        EXPECT_TRUE(handled.has_value()) << last_error();
        Answers out;
        const auto& log = world().scripts().host().log();
        std::size_t start = log.size();
        while (start > 0 && log[start - 1] != "<probe>")
            --start;
        for (std::size_t i = start; i < log.size(); ++i) {
            const std::string& line = log[i];
            const auto tab = line.find('\t');
            if (tab == std::string::npos)
                out[line] = "";
            else
                out[line.substr(0, tab)] = line.substr(tab + 1);
        }
        return out;
    }

    std::string last_error()
    {
        const auto& errors = world().scripts().host().errors();
        return errors.empty() ? std::string("(no script error recorded)")
                              : errors.back().message;
    }

    // The box of a body the loader would make, as body_spot_clear sees it.
    std::pair<short, short> body_box(Order order, int family)
    {
        walker probe(walker::WeaponProfileProbeTag{});
        const PixieData* data =
            world().configure_existing_entity(probe, order, family);
        if (data == nullptr)
            return {-1, -1};
        probe.set_data(*data);
        return {probe.sizex(), probe.sizey()};
    }
};

// One blocker kind: what to build in front of a level-4 soldier (team 1,
// 16 by 16 at 320,320) and what the loud step one pixel right answers. A
// body in front starts at x = 335: collide() shrinks both boxes by a pixel
// a side, so the step meets it by one pixel (a body at 336 only touches).
struct Case
{
    const char* name;
    bool passable;  // the loud answer, worked out from obmap.cpp / the grid
};

}  // namespace

// For every blocker kind the probe meets, the quiet call answers what the
// loud call answers, and the world after the quiet call is the world before
// it (obmap, lists, events, the stream, the prober's collide_ob, busy,
// heading and attack row, and every body's life). The loud call then runs on
// the same world and is shown to change something wherever it has a side
// effect, so the quiet call's silence is not the silence of a probe that
// never reached the blocker.
// RED (planted): the treasure arm without `if (quiet) continue;` eats the
// drumstick (dead 0 -> 1, the eat's draw); `if (!quiet)` dropped from the
// body arm's collide() swings at the foe (busy and ani_type move).
TEST_F(QuietProbe, every_blocker_answers_like_the_loud_probe_and_changes_nothing)
{
    struct Built
    {
        walker* prober = nullptr;
        std::vector<const walker*> watched;
        bool loud_changes = false;  // the loud call has a visible side effect
    };
    const auto build = [&](const std::string& kind) -> Built {
        fresh_world();
        Built b;
        b.prober = living_at(FAMILY_SOLDIER, 1, 320, 320);
        b.prober->set_user(0);  // a seat: the locked door posts its notice
        b.prober->set_curdir(static_cast<signed char>(FACE_RIGHT));
        b.prober->set_enddir(static_cast<char>(FACE_RIGHT));
        b.prober->set_lastx(1.0f);
        b.prober->set_lasty(0.0f);
        if (kind == "open") {
        } else if (kind == "wall") {
            tile(21, 20, PIX_H_WALL1);
        } else if (kind == "tree tile") {
            tile(21, 20, PIX_TREE_M1);
        } else if (kind == "map edge") {
            b.prober->setxy(1024 - 17, 320);
        } else if (kind == "enemy living") {
            b.watched.push_back(living_at(FAMILY_ORC, 2, 335, 320));
            b.loud_changes = true;  // collide(): the soldier starts a swing
        } else if (kind == "ally living") {
            b.watched.push_back(living_at(FAMILY_SOLDIER, 1, 335, 320));
            b.loud_changes = true;  // collide(): collide_ob is set
        } else if (kind == "drumstick") {
            walker* food = world().add_fx_ob(Order::Treasure, FAMILY_DRUMSTICK);
            b.watched.push_back(place(food, 334, 322, 8, 8));
            b.prober->stats()->set_hitpoints(100.0f);  // hungry: food is eaten
            b.loud_changes = true;  // eaten
        } else if (kind == "locked door") {
            walker* door = weapon_at(FAMILY_DOOR, 3, 335, 320, 16, 16);
            door->stats()->set_level(2);
            b.watched.push_back(door);
            b.loud_changes = true;  // "Key 2 needed!" and collide()
        } else if (kind == "keyed door") {
            walker* door = weapon_at(FAMILY_DOOR, 3, 335, 320, 16, 16);
            door->stats()->set_level(2);
            b.prober->set_keys(static_cast<std::uint32_t>(1u << 2));
            b.watched.push_back(door);
            b.loud_changes = true;  // opened (the door dies)
        } else if (kind == "keyed door, no-collide prober") {
            walker* door = weapon_at(FAMILY_DOOR, 3, 335, 320, 16, 16);
            door->stats()->set_level(2);
            b.prober->set_keys(static_cast<std::uint32_t>(1u << 2));
            b.prober->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
            b.watched.push_back(door);
            b.loud_changes = true;  // opened
        } else if (kind == "generator") {
            walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
            tent->set_team_num(2);
            b.watched.push_back(place(tent, 335, 320, 16, 16));
            b.loud_changes = true;  // collide(): collide_ob is set
        } else if (kind == "tree") {
            b.watched.push_back(weapon_at(FAMILY_TREE, 3, 335, 320, 16, 16));
            b.loud_changes = true;  // collide(): collide_ob is set
        }
        return b;
    };
    const std::vector<Case> cases = {
        {"open", true},
        {"wall", false},
        {"tree tile", false},
        {"map edge", false},
        {"enemy living", false},
        {"ally living", false},
        {"drumstick", true},
        {"locked door", false},
        {"keyed door", false},  // opened, and still blocks this round
        {"keyed door, no-collide prober", true},
        {"generator", false},
        {"tree", false},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        Built b = build(c.name);
        ASSERT_NE(nullptr, b.prober);
        for (const walker* w : b.watched)
            ASSERT_NE(nullptr, w);
        const float x = static_cast<float>(b.prober->xpos() + 1);
        const float y = static_cast<float>(b.prober->ypos());
        const WorldState before = state(b.prober, b.watched);
        const bool quiet =
            world().query_passable(x, y, b.prober, b.prober->floor(), true);
        const WorldState after_quiet = state(b.prober, b.watched);
        EXPECT_EQ(before, after_quiet)
            << "before " << before.describe() << "\nafter  "
            << after_quiet.describe();
        EXPECT_EQ(c.passable, quiet) << "the quiet answer";

        const bool loud = world().query_passable(x, y, b.prober);
        EXPECT_EQ(c.passable, loud) << "the loud answer";
        const WorldState after_loud = state(b.prober, b.watched);
        EXPECT_EQ(b.loud_changes, !(after_loud == before))
            << "the loud call's side effect, after "
            << after_loud.describe();

        // statistics::forward_blocked asks the same question one pixel
        // along curdir (FACE_RIGHT here), on a fresh copy of the case.
        Built f = build(c.name);
        const WorldState fb = state(f.prober, f.watched);
        EXPECT_EQ(!c.passable, f.prober->stats()->forward_blocked(true));
        EXPECT_EQ(fb, state(f.prober, f.watched)) << "forward_blocked(true)";
        EXPECT_EQ(!c.passable, f.prober->stats()->forward_blocked());
    }
}

// The quiet door arm keeps the loud answer exactly, and nothing else: a
// locked door posts no "Key 2 needed!" (events stay empty), a keyed one is
// not opened. The loud call is the control: one notice; the door dead.
// RED (planted): the door arm without its quiet return opens the keyed door.
TEST_F(QuietProbe, a_door_is_neither_opened_nor_announced_by_a_quiet_probe)
{
    walker* soldier = living_at(FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_NE(nullptr, soldier);
    soldier->set_user(0);
    walker* door = weapon_at(FAMILY_DOOR, 3, 335, 320, 16, 16);
    ASSERT_NE(nullptr, door);
    door->stats()->set_level(2);

    EXPECT_FALSE(world().query_passable(321.0f, 320.0f, soldier, 0, true));
    EXPECT_EQ(0u, tw_->events.size()) << "no notice from a quiet probe";
    EXPECT_FALSE(world().query_passable(321.0f, 320.0f, soldier));
    EXPECT_EQ(1u, tw_->events.size()) << "the loud probe's Key 2 needed!";

    soldier->set_keys(static_cast<std::uint32_t>(1u << 2));
    soldier->set_skip_exit(0);
    EXPECT_FALSE(world().query_passable(321.0f, 320.0f, soldier, 0, true));
    EXPECT_EQ(0, door->dead()) << "a quiet probe opens nothing";
    EXPECT_FALSE(world().query_passable(321.0f, 320.0f, soldier));
    EXPECT_EQ(1, door->dead()) << "the loud probe opens the keyed door";
}

// The one deliberate difference: an enemy weapon in flight on the box. The
// loud probe rolls the weapons' miss (next(10) > 3 passes); the quiet probe
// never rolls and passes it. From a state where the roll says "hit", the
// loud answer is blocked and the stream moved by exactly that draw; the
// quiet answer is passable and the stream did not move.
// RED (planted): `quiet || rng_.next(10) > 3` -> `rng_.next(10) > 3`.
TEST_F(QuietProbe, an_enemy_weapon_in_flight_passes_a_quiet_probe_without_a_draw)
{
    walker* archer = living_at(FAMILY_ELF, 1, 200, 200);
    ASSERT_NE(nullptr, archer);
    walker* rock = weapon_at(FAMILY_ROCK, 1, 300, 300);
    ASSERT_NE(nullptr, rock);
    rock->set_owner(archer);
    walker* arrow = weapon_at(FAMILY_ARROW, 2, 302, 300);
    ASSERT_NE(nullptr, arrow);

    // The first state whose miss roll says "hit" (next(10) <= 3).
    std::uint32_t seed = 1;
    for (;; ++seed) {
        og::sim::SimRandom r(seed);
        if (r.next(10) <= 3)
            break;
    }
    og::sim::SimRandom after_roll(seed);
    (void)after_roll.next(10);

    world().rng_.state_ = seed;
    const WorldState before = state(rock, {arrow});
    EXPECT_TRUE(world().query_passable(300.0f, 300.0f, rock, 0, true));
    EXPECT_EQ(before, state(rock, {arrow})) << "no draw, nothing touched";

    EXPECT_FALSE(world().query_passable(300.0f, 300.0f, rock));
    EXPECT_EQ(after_roll.state_, world().rng_.state_)
        << "the loud probe drew its one miss roll";
}

// The arrow slit: a living is refused either way; a weapon with an owner is
// the shooter-distance gamble, which a quiet probe never rolls (it passes).
// At the pad's distance the loud gamble's bound is 0 or 1, so the loud
// answer is passable too: the same answer, and the quiet one draws nothing.
// RED (planted): the `if (!quiet)` around the gamble dropped: the quiet
// probe draws next(1) (the stream moves).
TEST_F(QuietProbe, an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing)
{
    walker* elf = living_at(FAMILY_ELF, 1, 320, 320);
    ASSERT_NE(nullptr, elf);
    walker* rock = weapon_at(FAMILY_ROCK, 1, 337, 325);
    ASSERT_NE(nullptr, rock);
    rock->set_owner(elf);
    tile(21, 20, PIX_WALL_ARROW_GRASS);

    const std::uint32_t start = world().rng_.state_;
    EXPECT_TRUE(world().query_passable(337.0f, 325.0f, rock, 0, true));
    EXPECT_EQ(start, world().rng_.state_) << "the quiet probe rolls nothing";
    EXPECT_TRUE(world().query_passable(337.0f, 325.0f, rock));
    EXPECT_NE(start, world().rng_.state_)
        << "the control: the loud gamble at 17 px rolls next(1)";

    EXPECT_FALSE(world().query_passable(321.0f, 320.0f, elf, 0, true))
        << "a living never passes a slit";
    walker* stray = weapon_at(FAMILY_ROCK, 2, 337, 340);
    ASSERT_NE(nullptr, stray);
    EXPECT_FALSE(world().query_passable(337.0f, 325.0f, stray, 0, true))
        << "an ownerless weapon is refused as before";
}

// The no-overlap arm clears collide_ob on the loud probe; the quiet probe
// leaves it as it was.
// RED (planted): `else if (!quiet)` -> `else` on set_collide_ob(nullptr).
TEST_F(QuietProbe, a_quiet_probe_keeps_the_collide_ob_it_found)
{
    walker* soldier = living_at(FAMILY_SOLDIER, 1, 320, 320);
    walker* foe = living_at(FAMILY_ORC, 2, 340, 320);
    ASSERT_NE(nullptr, soldier);
    ASSERT_NE(nullptr, foe);
    soldier->set_collide_ob(foe);
    // One pixel right: the foe shares the obmap cell but not the box.
    EXPECT_TRUE(world().query_passable(321.0f, 320.0f, soldier, 0, true));
    EXPECT_EQ(foe, soldier->collide_ob()) << "the quiet probe";
    EXPECT_TRUE(world().query_passable(321.0f, 320.0f, soldier));
    EXPECT_EQ(nullptr, soldier->collide_ob()) << "the loud probe clears it";
}

// fire()'s pad and fire_pad_clear's pad are one table. For each of the eight
// facings: on an open field fire_pad_clear says yes and fire() launches a
// rock exactly on weapon_pad's spot; with a wall tile under that spot it
// says no and fire() launches nothing; with a foe's body on the pad it says
// no and fire() lands a blow instead (no rock).
// The expected pad is worked out here from the 2002 table, so a wrong row
// in weapon_pad moves fire()'s rock and the probe together, off this pad.
// RED (planted): weapon_pad's FACE_UP row one pixel off
// (ypos() - weapon.sizey()-1 -> ypos() - weapon.sizey()-2).
TEST_F(QuietProbe, fire_pad_clear_matches_fires_own_pad)
{
    const struct Facing
    {
        int face;
        float dx;
        float dy;
    } facings[] = {
        {FACE_UP, 0.0f, -1.0f},       {FACE_UP_RIGHT, 1.0f, -1.0f},
        {FACE_RIGHT, 1.0f, 0.0f},     {FACE_DOWN_RIGHT, 1.0f, 1.0f},
        {FACE_DOWN, 0.0f, 1.0f},      {FACE_DOWN_LEFT, -1.0f, 1.0f},
        {FACE_LEFT, -1.0f, 0.0f},     {FACE_UP_LEFT, -1.0f, -1.0f},
    };
    // The true pad of a 16x16 elf at (320,320) firing a rock of box (rw,rh):
    // worked out from the 2002 table, not from weapon_pad.
    const auto [rw, rh] = body_box(Order::Weapon, FAMILY_ROCK);
    ASSERT_GT(rw, 0);
    ASSERT_GT(rh, 0);
    const auto true_pad = [&](int face) -> std::pair<int, int> {
        const int x = 320;
        const int y = 320;
        switch (face) {
        case FACE_RIGHT: return {x + 16 + 1, y + (16 - rh) / 2};
        case FACE_LEFT: return {x - rw - 1, y + (16 - rh) / 2};
        case FACE_DOWN: return {x + (16 - rw) / 2, y + 16 + 1};
        case FACE_UP: return {x + (16 - rw) / 2, y - rh - 1};
        case FACE_UP_RIGHT: return {x + 16 + 1, y - rh - 1};
        case FACE_UP_LEFT: return {x - rw - 1, y - rh - 1};
        case FACE_DOWN_RIGHT: return {x + 16 + 1, y + 16 + 1};
        case FACE_DOWN_LEFT: return {x - rw - 1, y + 16 + 1};
        default: return {-1, -1};
        }
    };
    const auto elf_facing = [&](const Facing& f) {
        walker* elf = living_at(FAMILY_ELF, 1, 320, 320);
        elf->set_current_weapon(FAMILY_ROCK);
        elf->set_lastx(f.dx);
        elf->set_lasty(f.dy);
        return elf;
    };
    for (const Facing& f : facings) {
        SCOPED_TRACE(std::format("facing {}", f.face));
        const auto [px, py] = true_pad(f.face);

        fresh_world();
        walker* elf = elf_facing(f);
        ASSERT_NE(nullptr, elf);
        EXPECT_TRUE(elf->fire_pad_clear()) << "an open field";
        walker* rock = elf->fire();
        ASSERT_NE(nullptr, rock) << "fire() launches on an open field";
        EXPECT_EQ(px, rock->xpos());
        EXPECT_EQ(py, rock->ypos());
        short wx = 0;
        short wy = 0;
        elf->weapon_pad(*rock, wx, wy);
        EXPECT_EQ(px, wx) << "weapon_pad is fire()'s spot";
        EXPECT_EQ(py, wy);

        fresh_world();
        elf = elf_facing(f);
        tile(px / GRID_SIZE, py / GRID_SIZE, PIX_H_WALL1);
        EXPECT_FALSE(elf->fire_pad_clear()) << "a wall under the pad";
        EXPECT_EQ(nullptr, elf->fire()) << "fire() meets the wall";

        fresh_world();
        elf = elf_facing(f);
        walker* foe = living_at(FAMILY_ORC, 2, static_cast<short>(px),
                                static_cast<short>(py), 4, 4);
        ASSERT_NE(nullptr, foe);
        EXPECT_FALSE(elf->fire_pad_clear()) << "a foe on the pad";
        EXPECT_EQ(nullptr, elf->fire()) << "fire() lands a blow instead";
        EXPECT_LT(foe->stats()->hitpoints(), 400.0f) << "the blow";

        fresh_world();
        elf = elf_facing(f);
        ASSERT_NE(nullptr, living_at(FAMILY_ELF, 1, static_cast<short>(px),
                                     static_cast<short>(py), 4, 4));
        EXPECT_TRUE(elf->fire_pad_clear()) << "an ally on the pad";
        EXPECT_NE(nullptr, elf->fire()) << "the rock flies over the ally";
    }
}

// set_weapon_heading (fire_check's launch) puts the weapon on fire()'s pad
// and gives it fire()'s heading. For each of the eight facings, a 17x13 elf
// (odd sizes, both unlike the rock's box, so the centring truncates) and a
// rock made by create_weapon: after set_weapon_heading the rock stands on
// the pad worked out here from the 2002 table, weapon_pad answers the same
// spot, and lastx / lasty are the 2002 heading rows for the waver drawn from
// a copy of the stream (one draw, no more).
// RED (planted): weapon_pad's FACE_DOWN_LEFT row one pixel off
// (ypos()+sizey()+1 -> ypos()+sizey()+2): the weapon_pad assertion reds,
// and once set_weapon_heading reads weapon_pad its own position does too.
TEST_F(QuietProbe, set_weapon_heading_lands_on_fires_pad)
{
    const struct Facing
    {
        int face;
        float dx;
        float dy;
    } facings[] = {
        {FACE_UP, 0.0f, -1.0f},       {FACE_UP_RIGHT, 1.0f, -1.0f},
        {FACE_RIGHT, 1.0f, 0.0f},     {FACE_DOWN_RIGHT, 1.0f, 1.0f},
        {FACE_DOWN, 0.0f, 1.0f},      {FACE_DOWN_LEFT, -1.0f, 1.0f},
        {FACE_LEFT, -1.0f, 0.0f},     {FACE_UP_LEFT, -1.0f, -1.0f},
    };
    constexpr int kX = 320;
    constexpr int kY = 320;
    constexpr int kW = 17;
    constexpr int kH = 13;
    for (const Facing& f : facings) {
        SCOPED_TRACE(std::format("facing {}", f.face));
        fresh_world();
        walker* elf = living_at(FAMILY_ELF, 1, kX, kY, kW, kH);
        ASSERT_NE(nullptr, elf);
        elf->set_current_weapon(FAMILY_ROCK);
        elf->set_lastx(f.dx);
        elf->set_lasty(f.dy);
        walker* rock = elf->create_weapon();
        ASSERT_NE(nullptr, rock);
        const int rw = rock->sizex();
        const int rh = rock->sizey();
        ASSERT_GT(rw, 0);
        ASSERT_GT(rh, 0);
        ASSERT_NE(kW, rw);
        ASSERT_NE(kH, rh);

        // The waver, from a copy of the stream as it stands now.
        og::sim::SimRandom r(world().rng_.state_);
        const float step = rock->stepsize();
        signed char waver = static_cast<signed char>(std::clamp(step / 2, 0.0f, 127.0f));
        waver = static_cast<signed char>(
            r.next(static_cast<std::uint32_t>(waver + 1)) -
            static_cast<std::uint32_t>(waver / 2));
        const float wv = static_cast<float>(waver);

        // The 2002 table: the pad, then the heading.
        int px = -1;
        int py = -1;
        float hx = 0.0f;
        float hy = 0.0f;
        switch (f.face) {
        case FACE_RIGHT:
            px = kX + kW + 1; py = kY + (kH - rh) / 2; hx = step; hy = wv; break;
        case FACE_LEFT:
            px = kX - rw - 1; py = kY + (kH - rh) / 2; hx = -step; hy = wv; break;
        case FACE_DOWN:
            px = kX + (kW - rw) / 2; py = kY + kH + 1; hx = wv; hy = step; break;
        case FACE_UP:
            px = kX + (kW - rw) / 2; py = kY - rh - 1; hx = wv; hy = -step; break;
        case FACE_UP_RIGHT:
            px = kX + kW + 1; py = kY - rh - 1;
            hx = step + wv; hy = -step + wv; break;
        case FACE_UP_LEFT:
            px = kX - rw - 1; py = kY - rh - 1;
            hx = -step - wv; hy = -step + wv; break;
        case FACE_DOWN_RIGHT:
            px = kX + kW + 1; py = kY + kH + 1;
            hx = step - wv; hy = step + wv; break;
        case FACE_DOWN_LEFT:
            px = kX - rw - 1; py = kY + kH + 1;
            hx = -step + wv; hy = step + wv; break;
        default:
            FAIL() << "not a facing";
        }

        elf->set_weapon_heading(rock);
        EXPECT_EQ(px, rock->xpos()) << "set_weapon_heading's x";
        EXPECT_EQ(py, rock->ypos()) << "set_weapon_heading's y";
        EXPECT_EQ(hx, rock->lastx()) << "the heading's x";
        EXPECT_EQ(hy, rock->lasty()) << "the heading's y";
        EXPECT_EQ(r.state_, world().rng_.state_) << "exactly the waver's draw";
        short wx = 0;
        short wy = 0;
        elf->weapon_pad(*rock, wx, wy);
        EXPECT_EQ(px, wx) << "weapon_pad's x";
        EXPECT_EQ(py, wy) << "weapon_pad's y";
    }
}

// fire_pad_clear makes nothing and changes nothing: the obmap, the three
// lists, the events and the stream are as they were, a drumstick and an
// enemy rock on the pad are untouched (the probe passes both), the elf's
// busy, heading and collide_ob are kept, and the next body made takes the
// next entity id (the probe spent none). The same for fire_pad_answer and
// for a named weapon's pad (the fire arrow's), which leaves the current
// weapon as it was.
// RED (planted): fire_pad_clear probing loud (quiet = false): the enemy
// rock's miss roll draws (the stream moves) and, from this state, blocks.
TEST_F(QuietProbe, fire_pad_clear_leaves_the_world_alone)
{
    walker* elf = living_at(FAMILY_ELF, 1, 320, 320);
    ASSERT_NE(nullptr, elf);
    elf->set_current_weapon(FAMILY_ROCK);
    elf->set_lastx(1.0f);
    elf->set_lasty(0.0f);
    walker* food = world().add_fx_ob(Order::Treasure, FAMILY_DRUMSTICK);
    ASSERT_NE(nullptr, place(food, 337, 322, 8, 8));
    walker* enemy_rock = weapon_at(FAMILY_ROCK, 2, 338, 323);
    ASSERT_NE(nullptr, enemy_rock);
    // A state whose miss roll would say "hit", so a loud probe would block.
    std::uint32_t seed = 1;
    for (;; ++seed) {
        og::sim::SimRandom r(seed);
        if (r.next(10) <= 3)
            break;
    }
    world().rng_.state_ = seed;
    const std::uint32_t last_id = enemy_rock->entity_id();

    const WorldState before = state(elf, {food, enemy_rock});
    EXPECT_TRUE(elf->fire_pad_clear());
    EXPECT_EQ(walker::PadAnswer::Clear, elf->fire_pad_answer());
    EXPECT_TRUE(elf->fire_pad_clear(FAMILY_FIRE_ARROW)) << "a named weapon's pad";
    EXPECT_EQ(FAMILY_ROCK, elf->current_weapon()) << "the probe swaps nothing";
    EXPECT_EQ(before, state(elf, {food, enemy_rock}))
        << "before " << before.describe() << "\nafter  "
        << state(elf, {food, enemy_rock}).describe();
    walker* next = world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, next);
    EXPECT_EQ(last_id + 1, next->entity_id()) << "the probe spent no id";
}

// A non-living asks nothing: fire_pad_clear is false for a generator.
TEST_F(QuietProbe, fire_pad_clear_is_false_for_a_non_living)
{
    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, place(tent, 320, 320, 16, 16));
    EXPECT_FALSE(tent->fire_pad_clear());
}

// fire_pad_answer names what the pad of a level-4 elf's rock holds (the elf
// 16x16 at 320,320 heading RIGHT: the pad is x 337, one pixel outside), and
// fire_pad_clear is exactly "the answer is Clear". The grid half answers
// first: a wall, the map edge and a tree's crown are Terrain (the crown for
// an arrow: a rock walks the forest, BIT_FORESTWALK, and crosses it); a
// weapon crosses water. Then the bodies: an enemy living and a generator are Body;
// an ally passes, and so do an enemy door, a grown tree and an enemy weapon
// in flight (the weapon-vs-weapon arm comes before the door arm, and a quiet
// probe never rolls its miss: the loud fire() blocks on those 4 times in
// 10, so no fire() control is run for them). Where fire() is
// deterministic it is the control: it launches exactly when the answer is
// Clear. Every case through the Lua door too, and the world after the probe
// is the world before it. A generator caster and a weapon the loader cannot
// make answer NoProbe.
// RED (planted): fire_pad_answer's grid half answering Body instead of
// Terrain (return PadAnswer::Terrain -> return PadAnswer::Body): the wall,
// the arrow's tree tile and the map edge cases red.
TEST_F(QuietProbe, fire_pad_answer_names_what_the_pad_holds)
{
    using PA = walker::PadAnswer;
    struct PadCase
    {
        const char* name;
        PA answer;
        const char* lua_answer;
        bool fire_is_deterministic;
    };
    const std::vector<PadCase> cases = {
        {"open", PA::Clear, "clear", true},
        {"wall", PA::Terrain, "terrain", true},
        {"tree tile", PA::Clear, "clear", true},
        {"tree tile, arrow", PA::Terrain, "terrain", true},
        {"water", PA::Clear, "clear", true},
        {"map edge", PA::Terrain, "terrain", true},
        {"enemy living", PA::Body, "body", true},
        {"ally living", PA::Clear, "clear", true},
        {"generator", PA::Body, "body", true},
        {"door", PA::Clear, "clear", false},
        {"tree", PA::Clear, "clear", false},
        {"weapon in flight", PA::Clear, "clear", false},
    };
    const auto build = [&](const std::string& kind,
                           std::vector<const walker*>& watched) -> walker* {
        fresh_world();
        watched.clear();
        walker* elf = living_at(FAMILY_ELF, 1, 320, 320);
        if (elf == nullptr)
            return nullptr;
        elf->set_current_weapon(FAMILY_ROCK);
        elf->set_lastx(1.0f);
        elf->set_lasty(0.0f);
        if (kind == "wall") {
            tile(21, 20, PIX_H_WALL1);
        } else if (kind == "tree tile") {
            tile(21, 20, PIX_TREE_M1);
        } else if (kind == "tree tile, arrow") {
            tile(21, 20, PIX_TREE_M1);
            elf->set_current_weapon(FAMILY_ARROW);
        } else if (kind == "water") {
            tile(21, 20, PIX_WATER1);
        } else if (kind == "map edge") {
            elf->setxy(1024 - 17, 320);
        } else if (kind == "enemy living") {
            watched.push_back(living_at(FAMILY_ORC, 2, 337, 320));
        } else if (kind == "ally living") {
            watched.push_back(living_at(FAMILY_ELF, 1, 337, 320));
        } else if (kind == "generator") {
            walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
            if (tent != nullptr)
                tent->set_team_num(2);
            watched.push_back(place(tent, 337, 320, 16, 16));
        } else if (kind == "door") {
            walker* door = weapon_at(FAMILY_DOOR, 2, 337, 320, 16, 16);
            if (door != nullptr)
                door->stats()->set_level(2);
            watched.push_back(door);
        } else if (kind == "tree") {
            watched.push_back(weapon_at(FAMILY_TREE, 2, 337, 320, 16, 16));
        } else if (kind == "weapon in flight") {
            watched.push_back(weapon_at(FAMILY_ARROW, 2, 338, 322));
        }
        return elf;
    };
    for (const PadCase& c : cases) {
        SCOPED_TRACE(c.name);
        std::vector<const walker*> watched;
        walker* elf = build(c.name, watched);
        ASSERT_NE(nullptr, elf);
        for (const walker* w : watched)
            ASSERT_NE(nullptr, w);
        const WorldState before = state(elf, watched);
        EXPECT_EQ(c.answer, elf->fire_pad_answer());
        EXPECT_EQ(c.answer == PA::Clear, elf->fire_pad_clear());
        EXPECT_EQ(before, state(elf, watched))
            << "before " << before.describe() << "\nafter  "
            << state(elf, watched).describe();
        Answers a = lua(elf,
                        "    og.log('answer', self:fire_pad_answer())\n"
                        "    og.log('clear', self:fire_pad_clear() and 1 or 0)\n");
        EXPECT_EQ(c.lua_answer, a["answer"]) << last_error();
        EXPECT_EQ(c.answer == PA::Clear ? "1" : "0", a["clear"]);
        if (c.fire_is_deterministic) {
            EXPECT_EQ(c.answer == PA::Clear, elf->fire() != nullptr)
                << "fire() launches exactly when the pad is clear";
        }
    }

    fresh_world();
    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, place(tent, 320, 320, 16, 16));
    EXPECT_EQ(PA::NoProbe, tent->fire_pad_answer()) << "a generator caster";
    EXPECT_FALSE(tent->fire_pad_clear());
    EXPECT_EQ("none", lua(tent, "    og.log('answer', self:fire_pad_answer())\n")["answer"]);

    walker* elf = living_at(FAMILY_ELF, 1, 400, 400);
    ASSERT_NE(nullptr, elf);
    // Holds while no shipped pack declares a weapon at wire id 100; the
    // NUM_FAMILY_SLOTS case below is the one that cannot rot.
    EXPECT_EQ(PA::NoProbe, elf->fire_pad_answer(100)) << "a weapon slot no pack declares";
    EXPECT_EQ(PA::NoProbe, elf->fire_pad_answer(NUM_FAMILY_SLOTS)) << "beyond the slots";
    EXPECT_FALSE(elf->fire_pad_clear(NUM_FAMILY_SLOTS));
    EXPECT_EQ(PA::Clear, elf->fire_pad_answer()) << "the control: its own rock flies";
}

// fire_pad_clear(weapon_family) probes that weapon's box instead of the
// current one: the archer's EXPLODING BOLT fires core:fire_arrow (9x9) while
// the archer holds core:arrow (7x7). An archer 16x16 at (192,184) heading UP
// with a wall at tile (12,10) (y 160..175): the arrow's pad (196,176) lies
// in row 11 and is clear, the fire arrow's pad (195,174) reaches row 10 and
// meets the wall. fire() agrees with both answers, and the Lua doors
// (walker:fire_pad_clear(fam), G.pad_clear(self, fam), fire_pad_answer(fam))
// answer the same. The probes make nothing and draw nothing.
// RED (planted): fire_pad_answer ignoring its argument
// (weapon_family >= 0 ? weapon_family : ... -> -1 >= 0 ? ...): the fire
// arrow is probed as an arrow and answers clear.
TEST_F(QuietProbe, fire_pad_clear_takes_a_weapon_family)
{
    EXPECT_EQ((std::pair<short, short>{7, 7}), body_box(Order::Weapon, FAMILY_ARROW));
    EXPECT_EQ((std::pair<short, short>{9, 9}), body_box(Order::Weapon, FAMILY_FIRE_ARROW));
    const auto archer_up = [&]() {
        fresh_world();
        tile(12, 10, PIX_H_WALL1);
        walker* archer = living_at(FAMILY_ARCHER, 1, 192, 184);
        if (archer == nullptr)
            return archer;
        archer->set_current_weapon(FAMILY_ARROW);
        archer->set_lastx(0.0f);
        archer->set_lasty(-1.0f);
        return archer;
    };
    walker* archer = archer_up();
    ASSERT_NE(nullptr, archer);
    const WorldState before = state(archer);
    EXPECT_TRUE(archer->fire_pad_clear()) << "the arrow's own pad";
    EXPECT_TRUE(archer->fire_pad_clear(FAMILY_ARROW));
    EXPECT_FALSE(archer->fire_pad_clear(FAMILY_FIRE_ARROW)) << "the fire arrow's pad";
    EXPECT_EQ(walker::PadAnswer::Terrain, archer->fire_pad_answer(FAMILY_FIRE_ARROW));
    EXPECT_EQ(FAMILY_ARROW, archer->current_weapon()) << "the probe swaps nothing";
    EXPECT_EQ(before, state(archer)) << "before " << before.describe()
                                     << "\nafter  " << state(archer).describe();

    Answers a = lua(
        archer,
        "    local fa = og.family_id('weapon', 'core:fire_arrow')\n"
        "    og.log('own', self:fire_pad_clear() and 1 or 0)\n"
        "    og.log('fire_arrow', self:fire_pad_clear(fa) and 1 or 0)\n"
        "    og.log('gate_own', G.pad_clear(self) and 1 or 0)\n"
        "    og.log('gate_fire_arrow', G.pad_clear(self, fa) and 1 or 0)\n"
        "    og.log('answer', self:fire_pad_answer(fa))\n"
        "    og.log('weapon', self:current_weapon())\n");
    EXPECT_EQ("1", a["own"]) << last_error();
    EXPECT_EQ("0", a["fire_arrow"]);
    EXPECT_EQ("1", a["gate_own"]);
    EXPECT_EQ("0", a["gate_fire_arrow"]);
    EXPECT_EQ("terrain", a["answer"]);
    EXPECT_EQ(std::to_string(FAMILY_ARROW), a["weapon"]);

    // The control: fire() itself, with each weapon current.
    EXPECT_NE(nullptr, archer->fire()) << "the arrow flies";
    archer = archer_up();
    ASSERT_NE(nullptr, archer);
    archer->set_current_weapon(FAMILY_FIRE_ARROW);
    EXPECT_EQ(nullptr, archer->fire()) << "the fire arrow meets the wall";
}

// #419: the soldier, the cleric and the archmage register weapon hooks
// (on_fire_weapon; customize_weapon), and none of them changes the weapon's
// box, so the detached probe's pad is fire()'s pad for all three. For each
// family and each of the eight facings, on open grass: fire_pad_clear says
// yes, and fire() makes a weapon of the probe's own box (the loader's,
// before any hook) standing exactly on the pad the 2002 table gives for that
// box. The one refusal the probe cannot see is the soldier's out-of-blades
// veto (on_fire_weapon, weapons_left 0): fire_pad_clear says yes and fire()
// returns nothing; no gate asks the soldier's pad.
// RED (planted): finalize_weapon_profile growing the cleric's weapon after
// its hook (weapon->set_sizex(weapon->sizex() + 2) for FAMILY_CLERIC): the
// cleric's glow is wider than the probe's and stands off the pad on the
// left-hand facings.
TEST_F(QuietProbe, the_probe_pad_is_fires_pad_for_every_family_with_a_weapon_hook)
{
    const struct Facing
    {
        int face;
        float dx;
        float dy;
    } facings[] = {
        {FACE_UP, 0.0f, -1.0f},       {FACE_UP_RIGHT, 1.0f, -1.0f},
        {FACE_RIGHT, 1.0f, 0.0f},     {FACE_DOWN_RIGHT, 1.0f, 1.0f},
        {FACE_DOWN, 0.0f, 1.0f},      {FACE_DOWN_LEFT, -1.0f, 1.0f},
        {FACE_LEFT, -1.0f, 0.0f},     {FACE_UP_LEFT, -1.0f, -1.0f},
    };
    const struct Family
    {
        int family;
        int weapon;
        const char* name;
    } families[] = {
        {FAMILY_SOLDIER, FAMILY_KNIFE, "soldier"},
        {FAMILY_CLERIC, FAMILY_GLOW, "cleric"},
        {FAMILY_ARCHMAGE, FAMILY_FIREBALL, "archmage"},
    };
    constexpr int kX = 320;
    constexpr int kY = 320;
    for (const Family& fam : families) {
        const auto [ww, wh] = body_box(Order::Weapon, fam.weapon);
        ASSERT_GT(ww, 0) << fam.name;
        ASSERT_GT(wh, 0) << fam.name;
        for (const Facing& f : facings) {
            SCOPED_TRACE(std::format("{} facing {}", fam.name, f.face));
            fresh_world();
            walker* caster = living_at(fam.family, 1, kX, kY);
            ASSERT_NE(nullptr, caster);
            ASSERT_EQ(fam.weapon, caster->current_weapon());
            caster->set_weapons_left(1);
            caster->set_lastx(f.dx);
            caster->set_lasty(f.dy);
            int px = -1;
            int py = -1;
            switch (f.face) {
            case FACE_RIGHT: px = kX + 16 + 1; py = kY + (16 - wh) / 2; break;
            case FACE_LEFT: px = kX - ww - 1; py = kY + (16 - wh) / 2; break;
            case FACE_DOWN: px = kX + (16 - ww) / 2; py = kY + 16 + 1; break;
            case FACE_UP: px = kX + (16 - ww) / 2; py = kY - wh - 1; break;
            case FACE_UP_RIGHT: px = kX + 16 + 1; py = kY - wh - 1; break;
            case FACE_UP_LEFT: px = kX - ww - 1; py = kY - wh - 1; break;
            case FACE_DOWN_RIGHT: px = kX + 16 + 1; py = kY + 16 + 1; break;
            case FACE_DOWN_LEFT: px = kX - ww - 1; py = kY + 16 + 1; break;
            default: FAIL() << "not a facing";
            }
            EXPECT_TRUE(caster->fire_pad_clear()) << "open grass";
            walker* shot = caster->fire();
            ASSERT_NE(nullptr, shot) << "fire() launches on open grass";
            EXPECT_EQ(ww, shot->sizex()) << "the hook kept the box";
            EXPECT_EQ(wh, shot->sizey());
            EXPECT_EQ(px, shot->xpos()) << "fire()'s weapon stands on the probe's pad";
            EXPECT_EQ(py, shot->ypos());
        }
    }

    fresh_world();
    walker* soldier = living_at(FAMILY_SOLDIER, 1, kX, kY);
    ASSERT_NE(nullptr, soldier);
    soldier->set_weapons_left(0);
    soldier->set_lastx(1.0f);
    soldier->set_lasty(0.0f);
    EXPECT_TRUE(soldier->fire_pad_clear()) << "the probe cannot see the veto";
    EXPECT_EQ(nullptr, soldier->fire()) << "out of blades: on_fire_weapon vetoes";
    soldier->set_weapons_left(1);
    EXPECT_NE(nullptr, soldier->fire()) << "the control: one blade left flies";
}

// body_spot_clear: a body of the family's own box fits on an open field and
// on a drumstick (which it does not eat); a wall tile, a living body, a
// generator and a delayed spawn that has not entered the world block it.
// Nothing draws, nothing is made.
// RED (planted): body_spot_clear without its respawn_spot_clear test
// (dormant check only) puts the body on the wall and on the living.
TEST_F(QuietProbe, body_spot_clear_asks_the_placement_rule_with_the_bodys_box)
{
    const int skeleton = FAMILY_SKELETON;
    const auto [bw, bh] = body_box(Order::Living, skeleton);
    ASSERT_GT(bw, 0);
    ASSERT_GT(bh, 0);
    walker* food = world().add_fx_ob(Order::Treasure, FAMILY_DRUMSTICK);
    ASSERT_NE(nullptr, place(food, 404, 404, 8, 8));
    walker* blocker = living_at(FAMILY_ORC, 2, 600, 600);
    ASSERT_NE(nullptr, blocker);
    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, place(tent, 700, 600, 16, 16));
    walker* sleeper = living_at(FAMILY_ORC, 2, 500, 700);
    ASSERT_NE(nullptr, sleeper);
    sleeper->set_dormant(true);
    tile(10, 10, PIX_H_WALL1);

    const WorldState before = state(nullptr, {food});
    EXPECT_TRUE(og::sim::body_spot_clear(world(), Order::Living, skeleton,
                                         300, 300, 0)) << "open grass";
    EXPECT_TRUE(og::sim::body_spot_clear(world(), Order::Living, skeleton,
                                         400, 400, 0)) << "on a drumstick";
    EXPECT_FALSE(og::sim::body_spot_clear(world(), Order::Living, skeleton,
                                          static_cast<short>(160 - bw + 1), 160, 0))
        << "one pixel of the box on the wall tile";
    EXPECT_TRUE(og::sim::body_spot_clear(world(), Order::Living, skeleton,
                                         static_cast<short>(160 - bw), 160, 0))
        << "the box just left of the wall tile";
    EXPECT_FALSE(og::sim::body_spot_clear(world(), Order::Living, skeleton,
                                          600 + 15, 600 + 15, 0))
        << "one pixel on the orc";
    EXPECT_TRUE(og::sim::body_spot_clear(world(), Order::Living, skeleton,
                                         600 + 16, 600, 0))
        << "touching the orc's right side";
    EXPECT_FALSE(og::sim::body_spot_clear(world(), Order::Living, skeleton,
                                          700, 600, 0)) << "on the tent";
    EXPECT_FALSE(og::sim::body_spot_clear(world(), Order::Living, skeleton,
                                          500, 700, 0))
        << "on a delayed spawn";
    EXPECT_EQ(before, state(nullptr, {food})) << before.describe();
}

// body_spot_near's ring order on fixed arrangements around the corner
// (400,400). A thin post (2 px wide, 60 tall) at x = 407 blocks the corner;
// for a body bw wide the first clear spots are x - (bw - 7) (left) and
// x + 9 (right), vertical never within 16. The ring answers the nearest r;
// at a tie, (-r,0) before (r,0). Wall the left side off and the right wins;
// wall both and, with a post that also stops short, the diagonals and the
// verticals are tried in their stated order.
// RED (planted): the ring's first two entries swapped ({1,0} before
// {-1,0}): the tie arrangement answers the right spot.
TEST_F(QuietProbe, body_spot_near_tries_the_corner_then_the_ring_in_order)
{
    const int skeleton = FAMILY_SKELETON;
    const auto [bw, bh] = body_box(Order::Living, skeleton);
    ASSERT_GT(bw, 9);
    ASSERT_GT(bh, 0);
    short sx = -1;
    short sy = -1;

    // An open corner: the corner itself.
    EXPECT_TRUE(og::sim::body_spot_near(world(), Order::Living, skeleton, 400,
                                        400, 0, 16, sx, sy));
    EXPECT_EQ(400, sx);
    EXPECT_EQ(400, sy);

    // The post overlaps the body at dx when dx > p - bw and dx < p + w
    // (p = post's offset, w = its width). Left clears at r = bw - p, right
    // at r = p + w. A tie: p + w == bw - p.
    const auto post = [&](short p, short w) {
        fresh_world();
        walker* b = living_at(FAMILY_ORC, 2, static_cast<short>(400 + p), 380,
                              w, 60);
        return b;
    };
    {
        // bw - p == p + w with w = 1 (bw odd) or w = 2 (bw even).
        const short w = static_cast<short>(bw % 2 == 1 ? 1 : 2);
        const short p = static_cast<short>((bw - w) / 2);
        ASSERT_NE(nullptr, post(p, w));
        const int r = bw - p;
        ASSERT_EQ(r, p + w);
        EXPECT_TRUE(og::sim::body_spot_near(world(), Order::Living, skeleton,
                                            400, 400, 0, 16, sx, sy));
        EXPECT_EQ(400 - r, sx) << "a tie: (-r,0) first";
        EXPECT_EQ(400, sy);
    }
    {
        // Left nearer than right.
        ASSERT_NE(nullptr, post(static_cast<short>(bw - 3), 6));
        EXPECT_TRUE(og::sim::body_spot_near(world(), Order::Living, skeleton,
                                            400, 400, 0, 16, sx, sy));
        EXPECT_EQ(400 - 3, sx) << "left clears at r = 3";
        EXPECT_EQ(400, sy);
    }
    {
        // Right nearer than left: the post sits on the corner's left edge.
        ASSERT_NE(nullptr, post(0, 2));
        EXPECT_TRUE(og::sim::body_spot_near(world(), Order::Living, skeleton,
                                            400, 400, 0, 16, sx, sy));
        EXPECT_EQ(402, sx) << "right clears at r = 2";
        EXPECT_EQ(400, sy);
    }
    {
        // A wide block over the whole row: only vertical moves clear. Up
        // clears at r = bh (the block's top is the corner's top), down at
        // r = 16 (the block is 16 tall), so (0,-r) wins at r = bh <= 16.
        fresh_world();
        ASSERT_NE(nullptr, living_at(FAMILY_ORC, 2, 360, 400, 100, 16));
        EXPECT_TRUE(og::sim::body_spot_near(world(), Order::Living, skeleton,
                                            400, 400, 0, 16, sx, sy));
        EXPECT_EQ(400, sx);
        EXPECT_EQ(400 - bh, sy) << "(0,-r) at r = bh";
    }
    {
        // Boxed in beyond the radius: no spot, outputs left alone.
        fresh_world();
        ASSERT_NE(nullptr, living_at(FAMILY_ORC, 2, 360, 360, 100, 100));
        sx = 7;
        sy = 8;
        EXPECT_FALSE(og::sim::body_spot_near(world(), Order::Living, skeleton,
                                             400, 400, 0, 16, sx, sy));
        EXPECT_EQ(7, sx);
        EXPECT_EQ(8, sy);
    }
}

// find_blood_in_reach: every live stain under the finder's 800 (squared
// centre distance), on the asker's floor, nearest first, ties in fxlist
// order; a dead stain, a stain on another floor and one at 800 or more are
// left out. Its first answer is find_nearest_blood's when every stain is on
// the asker's floor.
// RED (planted): the sort's compare turned around (a.first > b.first).
TEST_F(QuietProbe, find_blood_in_reach_walks_the_finders_stains_nearest_first)
{
    walker* cleric = living_at(FAMILY_CLERIC, 1, 400, 400);
    ASSERT_NE(nullptr, cleric);
    // Squared centre distances from a 16x16 cleric at (400,400) to 16x16
    // stains: (dx)^2 + (dy)^2.
    walker* far = stain_at(420, 400);          // 400
    walker* tie_a = stain_at(400, 410);        // 100
    walker* near = stain_at(403, 404);         // 25
    walker* tie_b = stain_at(410, 400);        // 100
    walker* edge = stain_at(428, 400);         // 784, still in
    walker* out = stain_at(400, 429);          // 841, out
    walker* dead = stain_at(401, 400);         // 1, but dead
    walker* upstairs = stain_at(400, 401, 1);  // 1, another floor
    for (walker* w : {far, tie_a, near, tie_b, edge, out, dead, upstairs})
        ASSERT_NE(nullptr, w);
    dead->set_dead(1);

    const std::uint32_t start = world().rng_.state_;
    const std::vector<walker*> got = world().find_blood_in_reach(cleric);
    EXPECT_EQ(start, world().rng_.state_);
    const std::vector<walker*> want = {near, tie_a, tie_b, far, edge};
    EXPECT_EQ(want, got);
    EXPECT_TRUE(world().find_blood_in_reach(nullptr).empty());

    upstairs->set_dead(1);
    EXPECT_EQ(near, world().find_nearest_blood(cleric));
}

// The five Lua doors, each asked from a pack script and checked against the
// C++ answer: s_forward_blocked(true) answers like s_forward_blocked() but
// leaves the swing out (busy unchanged; the loud one starts it);
// og.quiet_passable is query_passable(..., quiet); og.body_spot_near
// returns the spot (or nil); og.blood_in_reach returns the stains in order;
// walker:fire_pad_clear and walker:fire_pad_answer are the C++ members, a
// named weapon family included, and a family id out of range is refused.
// RED (planted): the binding passing quiet = false to forward_blocked: the
// quiet call swings (busy moves before the loud call).
TEST_F(QuietProbe, the_lua_doors_answer_what_the_engine_answers)
{
    walker* soldier = living_at(FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_NE(nullptr, soldier);
    soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
    soldier->set_enddir(static_cast<char>(FACE_RIGHT));
    soldier->set_lastx(1.0f);
    soldier->set_lasty(0.0f);
    soldier->set_current_weapon(FAMILY_KNIFE);
    ASSERT_NE(nullptr, living_at(FAMILY_ORC, 2, 335, 320));
    tile(10, 10, PIX_H_WALL1);
    walker* near = stain_at(323, 324);
    walker* far = stain_at(340, 320);
    ASSERT_NE(nullptr, near);
    ASSERT_NE(nullptr, far);
    const std::int32_t far_id = static_cast<std::int32_t>(far->entity_id());
    const std::int32_t near_id = static_cast<std::int32_t>(near->entity_id());

    Answers a = lua(
        soldier,
        "    local b = og.blood_in_reach(self)\n"
        "    local ids = ''\n"
        "    for i = 1, #b do ids = ids .. og.entity_id(b[i]) .. ';' end\n"
        "    og.log('blood', ids)\n"
        "    og.log('pad', self:fire_pad_clear() and 1 or 0)\n"
        "    og.log('pad_answer', self:fire_pad_answer())\n"
        "    local knife = og.family_id('weapon', 'core:knife')\n"
        "    og.log('pad_knife', self:fire_pad_clear(knife) and 1 or 0)\n"
        "    og.log('pad_knife_answer', self:fire_pad_answer(knife))\n"
        "    og.log('pad_bad', pcall(function() return self:fire_pad_clear(256) end)"
        " and 'taken' or 'refused')\n"
        "    og.log('open', og.quiet_passable(100, 100, self) and 1 or 0)\n"
        "    og.log('wall', og.quiet_passable(150, 160, self) and 1 or 0)\n"
        "    local x, y = og.body_spot_near('living',"
        " og.family_id('living', 'core:skeleton'), 600, 600, 0, 16)\n"
        "    og.log('spot', tostring(x) .. ',' .. tostring(y))\n"
        "    local nx = og.body_spot_near('living',"
        " og.family_id('living', 'core:skeleton'), 335, 320, 0, 0)\n"
        "    og.log('none', tostring(nx))\n"
        "    og.log('quiet_front', self:s_forward_blocked(true) and 1 or 0)\n"
        "    og.log('busy_after_quiet', self:busy() == 0 and 'idle' or 'busy')\n"
        "    og.log('loud_front', self:s_forward_blocked() and 1 or 0)\n"
        "    og.log('busy_after_loud', self:busy() == 0 and 'idle' or 'busy')\n");
    EXPECT_EQ("1", a["quiet_front"]) << last_error();
    EXPECT_EQ("idle", a["busy_after_quiet"]) << "the quiet probe swings at nobody";
    EXPECT_EQ("1", a["loud_front"]);
    EXPECT_EQ("busy", a["busy_after_loud"]) << "the control: the loud probe swings";
    EXPECT_EQ("1", a["open"]);
    EXPECT_EQ("0", a["wall"]);
    EXPECT_EQ("600,600", a["spot"]);
    EXPECT_EQ("nil", a["none"]);
    EXPECT_EQ(std::format("{};{};", near_id, far_id), a["blood"]);
    EXPECT_EQ(soldier->fire_pad_clear() ? "1" : "0", a["pad"]);
    EXPECT_EQ("0", a["pad"]) << "the orc is on the knife's pad";
    EXPECT_EQ("body", a["pad_answer"]);
    EXPECT_EQ(walker::PadAnswer::Body, soldier->fire_pad_answer());
    EXPECT_EQ("0", a["pad_knife"]);
    EXPECT_EQ("body", a["pad_knife_answer"]);
    EXPECT_EQ("refused", a["pad_bad"]) << "256 is no family slot";
}
