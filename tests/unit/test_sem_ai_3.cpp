/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch ai-3: the rules the review found in obmap.cpp, smooth.cpp, stats.cpp.
// Each test is named after the rule it asserts.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "../test_sim_random_scope.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace {

// A scripted stream that also counts what it was asked.
class CountingSequence final : public IRandom
{
public:
    explicit CountingSequence(std::initializer_list<std::uint32_t> vals) : vals_(vals) {}
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        bounds.push_back(max_exclusive);
        const std::uint32_t v = vals_.empty() ? 0u : vals_[idx_ % vals_.size()];
        ++idx_;
        return max_exclusive == 0 ? 0u : v % max_exclusive;
    }
    std::vector<std::uint32_t> bounds;  // one entry per draw

private:
    std::vector<std::uint32_t> vals_;
    std::size_t idx_ = 0;
};

// The LCG state `steps` draws after `seed` (the bound does not change the step).
std::uint32_t advanced(std::uint32_t seed, int steps)
{
    og::sim::SimRandom r(seed);
    for (int i = 0; i < steps; ++i)
        (void)r.next(2);
    return r.state_;
}

// An open field of grass, 1024 px a side, the core pack mounted.
class SemAi3 : public ::testing::Test
{
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        tw_.reset();  // the old world's context guard must unwind first
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(64, 64);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * static_cast<std::size_t>(w.grid.h),
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = 0;
        w.rng_.state_ = 12345u;
    }

    void TearDown() override { tw_.reset(); }

    GameWorld& world() { return tw_->world(); }
    og::sim::SimEventLog& events() { return tw_->events; }

    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // Sizes first, then the position: the obmap registers a body in every
    // cell its box covers at the moment it is placed.
    walker* place(walker* w, short x, short y, short sx, short sy)
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

    std::vector<const og::sim::Event*> notifications() const
    {
        std::vector<const og::sim::Event*> out;
        for (const auto& e : tw_->events.events())
            if (e.kind == og::sim::EventKind::Notification)
                out.push_back(&e);
        return out;
    }
};

// ---------------------------------------------------------------------------
// The smoother on a 5x5 field, its centre at (2,2).
// ---------------------------------------------------------------------------

PixieData field(unsigned char fill)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = 5;
    pd.h = 5;
    pd.data = std::make_unique<unsigned char[]>(25);
    std::fill_n(pd.data.get(), 25, fill);
    return pd;
}

void put(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[static_cast<std::size_t>(x + y * pd.w)] = v;
}

unsigned char at(const PixieData& pd, int x, int y)
{
    return pd.data[static_cast<std::size_t>(x + y * pd.w)];
}

// The neighbours of the centre, by name. kKeep leaves the fill.
constexpr unsigned char kKeep = 0xFE;
struct Around
{
    unsigned char up = kKeep, right = kKeep, down = kKeep, left = kKeep;
    unsigned char ul = kKeep, ur = kKeep, dl = kKeep, dr = kKeep;
};

// Paints `centre` with its neighbours on a field of `fill`, smooths the centre
// with every draw answering `roll`, and returns the centre's new tile.
int smooth_centre(unsigned char fill, unsigned char centre, const Around& a,
                  std::uint32_t roll = 0, int* returned = nullptr)
{
    PixieData pd = field(fill);
    put(pd, 2, 2, centre);
    const auto set = [&](int x, int y, unsigned char v) {
        if (v != kKeep)
            put(pd, x, y, v);
    };
    set(2, 1, a.up);
    set(3, 2, a.right);
    set(2, 3, a.down);
    set(1, 2, a.left);
    set(1, 1, a.ul);
    set(3, 1, a.ur);
    set(1, 3, a.dl);
    set(3, 3, a.dr);
    FixedRandom rng(roll);
    smoother s;
    s.set_rng(&rng);
    s.set_target(pd);
    const int r = s.smooth(2, 2);
    if (returned != nullptr)
        *returned = r;
    return at(pd, 2, 2);
}

} // namespace

// ===========================================================================
// obmap.cpp
// ===========================================================================

// obmap.cpp:399 (ob_pass_check). Height only matters when BOTH have one: a
// thing of height 0 is full height, so a knife flying 40 px up still hits a
// body of height 0, and a knife of height 0 still hits a 16-tall body.
TEST_F(SemAi3, a_thing_of_height_zero_is_full_height_and_a_high_knife_still_meets_it)
{
    walker* ground = living_at(FAMILY_SOLDIER, 1, 100, 100);
    walker* knife = weapon_at(FAMILY_KNIFE, 0, 100, 100);
    ASSERT_NE(nullptr, ground);
    ASSERT_NE(nullptr, knife);
    knife->set_real_team_num(255);

    // Control: both sized, spans disjoint -> the knife flies over.
    ground->set_sizez(16);
    knife->set_sizez(4);
    knife->set_worldz(40.0f);
    ASSERT_TRUE(world().query_object_passable(100.0f, 100.0f, knife, 0, true))
        << "control: a 4-tall knife at z 40 passes over a 16-tall body";

    ground->set_sizez(0);
    EXPECT_FALSE(world().query_object_passable(100.0f, 100.0f, knife, 0, true))
        << "arm 'either sizez 0' (the body): a body of height 0 is full height";

    ground->set_sizez(16);
    knife->set_sizez(0);
    EXPECT_FALSE(world().query_object_passable(100.0f, 100.0f, knife, 0, true))
        << "arm 'either sizez 0' (the knife): a knife of height 0 is full height";
}

// obmap.cpp:467 (ob_pass_check). A locked door blocks; a seat walker bumping
// it is told "Key N needed!" once per 10-tick cooldown; an NPC is never told.
TEST_F(SemAi3, a_locked_door_tells_a_seat_once_per_cooldown_and_never_tells_an_npc)
{
    walker* soldier = living_at(FAMILY_SOLDIER, 1, 320, 320);
    walker* door = weapon_at(FAMILY_DOOR, 3, 335, 320, 16, 16);
    ASSERT_NE(nullptr, soldier);
    ASSERT_NE(nullptr, door);
    door->stats()->set_level(2);

    // Seat, cooldown 0.
    soldier->set_user(0);
    soldier->set_skip_exit(0);
    EXPECT_FALSE(world().query_passable(321.0f, 320.0f, soldier)) << "seat: blocked";
    auto notes = notifications();
    ASSERT_EQ(1u, notes.size()) << "seat, cooldown 0: one notice";
    EXPECT_EQ("Key 2 needed!", notes[0]->text);
    EXPECT_EQ(0, notes[0]->target_player) << "addressed to the bumping seat";
    EXPECT_EQ(10, soldier->skip_exit()) << "seat: the cooldown starts at 10";
    EXPECT_EQ(door, soldier->collide_ob()) << "seat: the door is collided";
    EXPECT_EQ(0, door->dead()) << "a locked door stays shut";

    // Seat, cooldown running.
    events().clear();
    soldier->set_skip_exit(5);
    soldier->set_collide_ob(nullptr);
    EXPECT_FALSE(world().query_passable(321.0f, 320.0f, soldier)) << "cooldown: blocked";
    EXPECT_EQ(0u, notifications().size()) << "arm 'cooldown running': no notice";
    EXPECT_EQ(5, soldier->skip_exit()) << "arm 'cooldown running': the cooldown is not restarted";
    EXPECT_EQ(door, soldier->collide_ob()) << "cooldown: the door is still collided";

    // NPC.
    events().clear();
    soldier->set_user(-1);
    soldier->set_skip_exit(0);
    soldier->set_collide_ob(nullptr);
    EXPECT_FALSE(world().query_passable(321.0f, 320.0f, soldier)) << "NPC: blocked";
    EXPECT_EQ(0u, notifications().size()) << "arm 'NPC (user -1)': no notice";
    EXPECT_EQ(0, soldier->skip_exit()) << "arm 'NPC (user -1)': no cooldown";
    EXPECT_EQ(door, soldier->collide_ob()) << "NPC: the door is collided";
    EXPECT_EQ(0, door->dead()) << "a locked door stays shut";
}

// obmap.cpp:520 (ob_pass_check). Any other body blocks the step; a loud probe
// records it (collide(): a swing at an enemy living, collide_ob set at an
// ally); a no-collide mover records it the same way and passes.
TEST_F(SemAi3, a_body_blocks_a_normal_mover_and_a_no_collide_mover_records_it_and_passes)
{
    // Normal mover into an enemy living.
    walker* soldier = living_at(FAMILY_SOLDIER, 1, 320, 320);
    walker* orc = living_at(FAMILY_ORC, 2, 335, 320);
    ASSERT_NE(nullptr, soldier);
    ASSERT_NE(nullptr, orc);
    soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
    soldier->set_enddir(static_cast<char>(FACE_RIGHT));
    soldier->set_lastx(1.0f);  // the swing heads where the soldier faces
    soldier->set_lasty(0.0f);
    soldier->set_ani_type(static_cast<char>(ANI_WALK));
    EXPECT_FALSE(world().query_passable(321.0f, 320.0f, soldier))
        << "arm 'normal mover': an enemy body blocks";
    EXPECT_EQ(orc, soldier->collide_ob()) << "arm 'normal mover': collide_ob is the body met";
    EXPECT_EQ(ANI_ATTACK, soldier->ani_type()) << "arm 'normal mover': the swing at an enemy starts";
    EXPECT_FLOAT_EQ(soldier->fire_frequency(), soldier->busy()) << "the swing charges one fire_frequency";

    // Normal mover into an ally: collide_ob set, no swing.
    walker* ally = living_at(FAMILY_SOLDIER, 1, 320, 400);
    walker* mover = living_at(FAMILY_SOLDIER, 1, 305, 400);
    ASSERT_NE(nullptr, ally);
    ASSERT_NE(nullptr, mover);
    mover->set_curdir(static_cast<signed char>(FACE_RIGHT));
    mover->set_enddir(static_cast<char>(FACE_RIGHT));
    mover->set_lastx(1.0f);
    mover->set_lasty(0.0f);
    mover->set_ani_type(static_cast<char>(ANI_WALK));
    EXPECT_FALSE(world().query_passable(306.0f, 400.0f, mover))
        << "arm 'normal mover': an ally body blocks too";
    EXPECT_EQ(ally, mover->collide_ob()) << "arm 'normal mover': collide_ob is the ally";
    EXPECT_EQ(ANI_WALK, mover->ani_type()) << "no swing at an ally";
    EXPECT_FLOAT_EQ(0.0f, mover->busy()) << "no swing at an ally";

    // No-collide mover into an enemy living: recorded, and passes.
    walker* ghost = living_at(FAMILY_SOLDIER, 1, 320, 480);
    walker* orc2 = living_at(FAMILY_ORC, 2, 335, 480);
    ASSERT_NE(nullptr, ghost);
    ASSERT_NE(nullptr, orc2);
    ghost->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
    ghost->set_curdir(static_cast<signed char>(FACE_RIGHT));
    EXPECT_TRUE(world().query_passable(321.0f, 480.0f, ghost))
        << "arm 'BIT_NO_COLLIDE mover': the body does not block";
    EXPECT_EQ(orc2, ghost->collide_ob())
        << "arm 'BIT_NO_COLLIDE mover': collide() ran first and recorded the body";
}

// obmap.cpp:496 (ob_pass_check). Two livings already overlapping can always
// step apart: a step that strictly increases the centre distance passes the
// body; one that does not collides as usual.
TEST_F(SemAi3, two_overlapping_livings_may_step_apart_but_not_closer)
{
    walker* a = living_at(FAMILY_SOLDIER, 1, 100, 100);
    walker* b = living_at(FAMILY_ORC, 2, 104, 100);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);

    EXPECT_TRUE(world().query_passable(98.0f, 100.0f, a))
        << "arm 'already overlapping, step separates': moving away passes";
    EXPECT_EQ(nullptr, a->collide_ob()) << "the separating step collides with nothing";

    EXPECT_FALSE(world().query_passable(102.0f, 100.0f, a))
        << "arm 'already overlapping, step does not separate': moving closer collides";
    EXPECT_EQ(b, a->collide_ob()) << "the non-separating step collides with the body";

    a->set_collide_ob(nullptr);
    EXPECT_FALSE(world().query_passable(100.0f, 100.0f, a))
        << "arm 'already overlapping, step does not separate': standing still collides";
    EXPECT_EQ(b, a->collide_ob());

    // Exactly coincident: distance 0 now, any step separates.
    b->setxy(100, 100);
    a->set_collide_ob(nullptr);
    EXPECT_TRUE(world().query_passable(100.0f, 99.0f, a))
        << "coincident bodies: a one-pixel step separates";

    // Control: not overlapping now -> the ordinary rule (the step into it collides).
    b->setxy(117, 100);
    a->set_collide_ob(nullptr);
    EXPECT_TRUE(world().query_passable(102.0f, 100.0f, a)) << "control: the step stops short";
    EXPECT_FALSE(world().query_passable(104.0f, 100.0f, a)) << "control: the step into it collides";
    EXPECT_EQ(b, a->collide_ob());
}

// ===========================================================================
// smooth.cpp
// ===========================================================================

// smooth.cpp:366 (smoother::smooth, dark grass). Top right, right thin and
// top alone take the LL fade when plain grass is beside the open edge, else
// B2 / B1 / B1.
TEST(SemSmooth, dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass)
{
    constexpr unsigned char D = PIX_GRASS_DARK_1;
    constexpr unsigned char G = PIX_GRASS1;
    constexpr unsigned char X = PIX_DIRT_1;  // neither grass nor dark grass nor a caster
    // top right: dark left and below.
    {
        Around a; a.left = D; a.down = D; a.right = G; a.up = X;
        EXPECT_EQ(PIX_GRASS_DARK_LL, smooth_centre(G, D, a)) << "top right, grass right";
        a.right = X;
        EXPECT_EQ(PIX_GRASS_DARK_B2, smooth_centre(G, D, a)) << "top right, no grass right";
    }
    // right thin: dark left only.
    {
        Around a; a.left = D; a.down = X; a.up = X; a.right = G;
        EXPECT_EQ(PIX_GRASS_DARK_LL, smooth_centre(G, D, a)) << "right thin, grass right";
        a.right = X;
        EXPECT_EQ(PIX_GRASS_DARK_B1, smooth_centre(G, D, a)) << "right thin, no grass right";
    }
    // top alone: dark below only.
    {
        Around a; a.down = D; a.left = X; a.right = G; a.up = X;
        EXPECT_EQ(PIX_GRASS_DARK_LL, smooth_centre(G, D, a)) << "top alone, grass right";
        a.right = X; a.up = G;
        EXPECT_EQ(PIX_GRASS_DARK_LL, smooth_centre(G, D, a)) << "top alone, grass above";
        a.up = X;
        EXPECT_EQ(PIX_GRASS_DARK_B1, smooth_centre(G, D, a)) << "top alone, neither";
    }
}

// smooth.cpp:366 (smoother::smooth, dark grass). Bottom left, left alone and
// bottom alone take the UR fade when plain grass is beside the open edge,
// else B1.
TEST(SemSmooth, dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass)
{
    constexpr unsigned char D = PIX_GRASS_DARK_1;
    constexpr unsigned char G = PIX_GRASS1;
    constexpr unsigned char X = PIX_DIRT_1;
    // bottom left: dark right and above.
    {
        Around a; a.right = D; a.up = D; a.left = G; a.down = X;
        EXPECT_EQ(PIX_GRASS_DARK_UR, smooth_centre(G, D, a)) << "bottom left, grass left";
        a.left = X; a.down = G;
        EXPECT_EQ(PIX_GRASS_DARK_UR, smooth_centre(G, D, a)) << "bottom left, grass below";
        a.down = X;
        EXPECT_EQ(PIX_GRASS_DARK_B1, smooth_centre(G, D, a)) << "bottom left, neither";
    }
    // left alone: dark right only.
    {
        Around a; a.right = D; a.up = X; a.down = X; a.left = G;
        EXPECT_EQ(PIX_GRASS_DARK_UR, smooth_centre(G, D, a)) << "left alone, grass left";
        a.left = X;
        EXPECT_EQ(PIX_GRASS_DARK_B1, smooth_centre(G, D, a)) << "left alone, no grass left";
    }
    // bottom alone: dark above only.
    {
        Around a; a.up = D; a.left = X; a.right = X; a.down = G;
        EXPECT_EQ(PIX_GRASS_DARK_UR, smooth_centre(G, D, a)) << "bottom alone, grass below";
        a.down = X;
        EXPECT_EQ(PIX_GRASS_DARK_B1, smooth_centre(G, D, a)) << "bottom alone, no grass below";
    }
}

// smooth.cpp:505 (smoother::smooth, walls). The middle base of a wall (walls
// above, left and right) is a plain wallside unless the 1-in-10 roll cracks it.
TEST(SemSmooth, a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it)
{
    constexpr unsigned char W = PIX_H_WALL1;
    Around a; a.up = W; a.left = W; a.right = W;
    EXPECT_EQ(PIX_WALLSIDE1, smooth_centre(PIX_GRASS1, W, a, 1))
        << "arm '11', next(10) != 0: plain wallside";
    EXPECT_EQ(PIX_WALLSIDE_CRACK_C1, smooth_centre(PIX_GRASS1, W, a, 0))
        << "arm '11', next(10) == 0: cracked";
}

// smooth.cpp:505 (smoother::smooth, walls), a classic quirk the old game
// shares: a wall whose only wall neighbours are at its sides (masks 2, 8, 10:
// a horizontal run's interior or end) and a lone wall (mask 0) keep the tile
// they were painted with.
TEST(SemSmooth, classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile)
{
    constexpr unsigned char W = PIX_H_WALL1;
    constexpr unsigned char P = PIX_WALL5;  // a wall tile no arm would pick
    {
        Around a; a.right = W;
        EXPECT_EQ(P, smooth_centre(PIX_GRASS1, P, a)) << "mask 2 (right only)";
    }
    {
        Around a; a.left = W;
        EXPECT_EQ(P, smooth_centre(PIX_GRASS1, P, a)) << "mask 8 (left only)";
    }
    {
        Around a; a.left = W; a.right = W;
        EXPECT_EQ(P, smooth_centre(PIX_GRASS1, P, a)) << "mask 10 (left and right)";
    }
    EXPECT_EQ(P, smooth_centre(PIX_GRASS1, P, Around{})) << "mask 0 (alone)";
    // Control: mask 12 (left and below) is re-tiled.
    {
        Around a; a.left = W; a.down = W;
        EXPECT_EQ(PIX_H_WALL1, smooth_centre(PIX_GRASS1, P, a)) << "control: mask 12 re-tiles";
    }
}

// smooth.cpp:646 (smoother::smooth). Terrain with no smoothing rule is left
// exactly as painted: the unknown genre and every genre the switch has no arm
// for (air, glass, drop blocks, Z stairs, snow, lava, marsh, ash).
TEST(SemSmooth, terrain_with_no_smoothing_rule_is_left_exactly_as_painted)
{
    const std::vector<unsigned char> inert = {
        PIX_PAVEMENT1, PIX_FLOOR1,  // TYPE_UNKNOWN
        PIX_AIR, PIX_GLASS, PIX_DROPBLOCK_UP, PIX_ZSTAIR_UP,
        PIX_SNOW1, PIX_LAVA1, PIX_MARSH1, PIX_ASH1,
    };
    for (const unsigned char pix : inert) {
        SCOPED_TRACE(static_cast<int>(pix));
        int returned = -1;
        EXPECT_EQ(pix, smooth_centre(PIX_GRASS1, pix, Around{}, 0, &returned))
            << "arm 'no rule': the tile is unchanged";
        EXPECT_EQ(1, returned) << "smooth() reports it ran";
    }
}

// ===========================================================================
// stats.cpp
// ===========================================================================

// stats.cpp:174 (statistics::clear_command_for_control_switch). Only a LEADING
// run of forced WALK entries survives a control switch: a forced non-walk at
// the front, or an unforced walk, keeps nothing; the run stops at the first
// non-walk.
TEST_F(SemAi3, a_control_switch_keeps_only_a_leading_run_of_forced_walks)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, s);
    statistics* st = s->stats();

    // Forced FIRE at the front, an unforced walk behind it.
    st->clear_command();
    st->add_command(COMMAND_WALK, 30, 0, 1);
    st->force_command(COMMAND_FIRE, 4, 1, 0);
    st->clear_command_for_control_switch();
    EXPECT_TRUE(st->commands.empty())
        << "arm 'no forced walk at front' (a forced non-walk): the queue is wiped";

    // An unforced walk at the front.
    st->add_command(COMMAND_WALK, 30, 0, 1);
    st->clear_command_for_control_switch();
    EXPECT_TRUE(st->commands.empty())
        << "arm 'no forced walk at front' (an unforced walk): the queue is wiped";

    // forced WALK(8), forced FIRE(4), forced WALK(12): the run stops at the FIRE.
    st->force_command(COMMAND_WALK, 12, -1, 0);
    st->force_command(COMMAND_FIRE, 4, 1, 0);
    st->force_command(COMMAND_WALK, 8, 1, 0);
    s->set_current_weapon(static_cast<unsigned short>(s->default_weapon() + 1));
    st->clear_command_for_control_switch();
    ASSERT_EQ(1u, st->commands.size()) << "the forced run ends at the first non-walk";
    EXPECT_EQ(COMMAND_WALK, st->commands.front().commandtype);
    EXPECT_EQ(8, st->commands.front().commandcount);
    EXPECT_EQ(1, st->commands.front().com1);
    EXPECT_EQ(0, st->commands.front().com2);
    EXPECT_EQ(s->default_weapon(), s->current_weapon()) << "the weapon resets on a switch";
    EXPECT_EQ(nullptr, s->leader()) << "the leader clears on a switch";
}

// stats.cpp:213 (statistics::add_command). Queuing DIE marks the walker for
// deletion at once and queues nothing.
TEST_F(SemAi3, queuing_a_die_order_marks_the_walker_for_deletion_and_queues_nothing)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, s);
    statistics* st = s->stats();
    st->clear_command();
    st->add_command(COMMAND_WALK, 5, 1, 0);
    st->set_delete_me(0);
    st->add_command(COMMAND_DIE, 1, 0, 0);
    EXPECT_EQ(1, st->delete_me()) << "arm 'COMMAND_DIE': marked for deletion";
    ASSERT_EQ(1u, st->commands.size()) << "arm 'COMMAND_DIE': nothing appended";
    EXPECT_EQ(COMMAND_WALK, st->commands.back().commandtype);
    EXPECT_EQ(5, st->commands.back().commandcount);

    st->clear_command();
    st->set_delete_me(0);
    st->add_command(COMMAND_DIE, 1, 0, 0);
    EXPECT_EQ(1, st->delete_me());
    EXPECT_TRUE(st->commands.empty()) << "arm 'COMMAND_DIE': an empty queue stays empty";
}

// stats.cpp:352 (statistics::set_command(whatcommand, iterations)). RANDOM_WALK
// becomes a forced walk at the front in a direction of two rng(3)-1 draws (a
// (0,0) roll becomes SE); any other order is forced with zero arguments and
// draws nothing.
TEST_F(SemAi3, a_random_walk_order_becomes_a_forced_walk_in_a_rolled_direction)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, s);
    statistics* st = s->stats();

    struct Roll
    {
        std::uint32_t a, b;
        int com1, com2;
    };
    for (const Roll r : {Roll{0, 0, -1, -1}, Roll{2, 2, 1, 1}, Roll{1, 1, 1, 1}}) {
        SCOPED_TRACE(testing::Message() << "rolls " << r.a << "," << r.b);
        CountingSequence rolls({r.a, r.b});
        ScopedSimRandom scripted(&rolls);
        st->clear_command();
        st->add_command(COMMAND_SEARCH, 9, 0, 0);
        st->set_command(COMMAND_RANDOM_WALK, 3);
        ASSERT_EQ(2u, st->commands.size());
        const command& c = st->commands.front();
        EXPECT_EQ(COMMAND_WALK, c.commandtype) << "arm 'COMMAND_RANDOM_WALK': a walk at the front";
        EXPECT_EQ(3, c.commandcount);
        EXPECT_EQ(r.com1, c.com1);
        EXPECT_EQ(r.com2, c.com2);
        EXPECT_TRUE(c.forced);
        EXPECT_EQ((std::vector<std::uint32_t>{3u, 3u}), rolls.bounds) << "two rng(3) draws";
    }
    {
        // Mixed rolls: the two draws are one call's arguments (unsequenced),
        // so only the pair is pinned.
        CountingSequence rolls({0, 2});
        ScopedSimRandom scripted(&rolls);
        st->clear_command();
        st->set_command(COMMAND_RANDOM_WALK, 3);
        ASSERT_EQ(1u, st->commands.size());
        const command& c = st->commands.front();
        EXPECT_EQ(0, c.com1 + c.com2) << "one axis -1, the other +1";
        EXPECT_EQ(1, c.com1 * c.com1);
        EXPECT_EQ((std::vector<std::uint32_t>{3u, 3u}), rolls.bounds);
    }
    {
        CountingSequence rolls({2, 2});
        ScopedSimRandom scripted(&rolls);
        st->clear_command();
        st->set_command(COMMAND_SET_WEAPON, 3);
        ASSERT_EQ(1u, st->commands.size());
        const command& c = st->commands.front();
        EXPECT_EQ(COMMAND_SET_WEAPON, c.commandtype) << "arm 'other command': queued as itself";
        EXPECT_EQ(3, c.commandcount);
        EXPECT_EQ(0, c.com1) << "arm 'other command': zero arguments";
        EXPECT_EQ(0, c.com2) << "arm 'other command': zero arguments";
        EXPECT_TRUE(c.forced);
        EXPECT_TRUE(rolls.bounds.empty()) << "arm 'other command': no draw";
    }
}

// stats.cpp:483 (statistics::do_command, MULTIDO). MULTIDO(n) with n <= 0
// consumes itself and runs nothing else this round.
TEST_F(SemAi3, a_multido_of_zero_or_less_consumes_itself_and_runs_nothing)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 128, 128);
    ASSERT_NE(nullptr, s);
    s->set_curdir(static_cast<signed char>(FACE_RIGHT));
    s->set_enddir(static_cast<char>(FACE_RIGHT));
    statistics* st = s->stats();
    for (const int n : {-3, 0}) {
        SCOPED_TRACE(n);
        s->setxy(128, 128);
        st->clear_command();
        st->add_command(COMMAND_WALK, 1, 1, 0);
        st->force_command(COMMAND_MULTIDO, 1, n, 0);
        EXPECT_EQ(1, st->do_command());
        EXPECT_EQ(128, s->xpos()) << "arm 'com1 <= 0': the walk behind it did not run";
        EXPECT_EQ(128, s->ypos());
        ASSERT_EQ(1u, st->commands.size()) << "arm 'com1 <= 0': only the MULTIDO is consumed";
        EXPECT_EQ(COMMAND_WALK, st->commands.front().commandtype);
        EXPECT_EQ(1, st->commands.front().commandcount) << "the walk's count is untouched";
    }
    // Control: MULTIDO(1) runs the walk in the same round.
    s->setxy(128, 128);
    st->clear_command();
    st->add_command(COMMAND_WALK, 1, 1, 0);
    st->force_command(COMMAND_MULTIDO, 1, 1, 0);
    EXPECT_EQ(1, st->do_command());
    EXPECT_EQ(128 + static_cast<int>(s->stepsize()), s->xpos()) << "control: MULTIDO(1) steps once";
}

// stats.cpp:494 (statistics::do_command, RUSH). A rush moves only a living:
// a weapon holding the order does not move and strikes nothing.
TEST_F(SemAi3, a_rush_order_on_a_non_living_body_does_nothing_but_count_down)
{
    walker* arrow = weapon_at(FAMILY_ARROW, 0, 256, 256);
    walker* orc = living_at(FAMILY_ORC, 1, 262, 256);
    ASSERT_NE(nullptr, arrow);
    ASSERT_NE(nullptr, orc);
    const std::uint32_t arrow_id = arrow->entity_id();
    arrow->set_curdir(static_cast<signed char>(FACE_RIGHT));
    arrow->set_enddir(static_cast<char>(FACE_RIGHT));
    arrow->set_collide_ob(orc);
    orc->stats()->clear_command();
    orc->stats()->add_command(COMMAND_SEARCH, 7, 0, 0);
    const float orc_hp = orc->stats()->hitpoints();
    statistics* st = arrow->stats();
    st->clear_command();
    st->force_command(COMMAND_RUSH, 3, 1, 0);
    EXPECT_EQ(1, st->do_command());
    ASSERT_EQ(arrow, world().find_by_id(arrow_id));
    EXPECT_EQ(256, arrow->xpos()) << "arm 'not Living': no step";
    EXPECT_EQ(256, arrow->ypos()) << "arm 'not Living': no step";
    ASSERT_EQ(1u, st->commands.size());
    EXPECT_EQ(COMMAND_RUSH, st->commands.front().commandtype);
    EXPECT_EQ(2, st->commands.front().commandcount) << "the rush only counts down";
    EXPECT_FLOAT_EQ(orc_hp, orc->stats()->hitpoints()) << "arm 'not Living': no strike";
    ASSERT_EQ(1u, orc->stats()->commands.size()) << "arm 'not Living': no shove";
    EXPECT_EQ(COMMAND_SEARCH, orc->stats()->commands.front().commandtype);
}

// stats.cpp:537 (statistics::do_command, GOTO). A GOTO walks toward a fixed
// point, never attacks on the way (a foe held the whole time), and completes
// on the round it starts within 12 px of the point.
TEST_F(SemAi3, a_goto_order_walks_to_its_point_without_attacking_and_ends_on_arrival)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 96, 96);
    walker* orc = living_at(FAMILY_ORC, 1, 56, 96);
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, orc);
    s->set_curdir(static_cast<signed char>(FACE_DOWN_RIGHT));
    s->set_enddir(static_cast<char>(FACE_DOWN_RIGHT));
    s->set_path_check_counter(0);
    s->set_foe(orc);
    FixedRandom one(1);  // rng(300) != 0: the order is never reset mid-walk
    ScopedSimRandom scripted(&one);
    statistics* st = s->stats();
    st->clear_command();
    st->force_command(COMMAND_GOTO, 96, 200, 200);
    const std::size_t weapons = world().weaplist.size();
    bool arrived = false;
    for (int t = 0; t < 96 && !arrived; ++t) {
        SCOPED_TRACE(t);
        const int x0 = s->xpos();
        const int y0 = s->ypos();
        const int d0 = std::abs(200 - x0) + std::abs(200 - y0);
        orc->setxy(static_cast<short>(x0 - 40), static_cast<short>(y0));  // the foe stays behind it
        ASSERT_EQ(1, st->do_command());
        if (d0 < 12) {
            EXPECT_TRUE(st->commands.empty()) << "arrival: the GOTO completes this round";
            EXPECT_EQ(x0, s->xpos()) << "arrival: no step";
            EXPECT_EQ(y0, s->ypos()) << "arrival: no step";
            arrived = true;
        } else {
            ASSERT_EQ(1u, st->commands.size()) << "not there yet: the GOTO stays";
            EXPECT_GE(s->xpos(), x0) << "never steps away from the point";
            EXPECT_GE(s->ypos(), y0) << "never steps away from the point";
        }
        EXPECT_EQ(weapons, world().weaplist.size()) << "a GOTO never fires";
        EXPECT_FLOAT_EQ(0.0f, s->busy()) << "a GOTO never swings";
    }
    EXPECT_TRUE(arrived) << "arm 'front GOTO(x,y)': the walker reaches the point";
}

// stats.cpp:541 (statistics::do_command, RIGHT_WALK). With no foe a
// right-hand walk idles: no step, no turn, nothing queued, the count runs down.
TEST_F(SemAi3, a_right_walk_order_without_a_foe_idles)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 256, 256);
    ASSERT_NE(nullptr, s);
    s->set_curdir(static_cast<signed char>(FACE_UP));
    s->set_enddir(static_cast<char>(FACE_UP));
    s->set_foe(nullptr);
    statistics* st = s->stats();
    st->clear_command();
    st->force_command(COMMAND_RIGHT_WALK, 3, 0, 0);
    EXPECT_EQ(1, st->do_command());
    EXPECT_EQ(256, s->xpos()) << "arm 'no foe': no step";
    EXPECT_EQ(256, s->ypos()) << "arm 'no foe': no step";
    EXPECT_EQ(FACE_UP, s->curdir()) << "arm 'no foe': no turn";
    EXPECT_EQ(FACE_UP, s->enddir()) << "arm 'no foe': no turn";
    ASSERT_EQ(1u, st->commands.size()) << "arm 'no foe': nothing queued in front";
    EXPECT_EQ(COMMAND_RIGHT_WALK, st->commands.front().commandtype);
    EXPECT_EQ(2, st->commands.front().commandcount);
}

// stats.cpp:552 (statistics::do_command, ATTACK). An attack on a DEAD foe
// ends at once: popped, no step, no turn, no swing.
TEST_F(SemAi3, an_attack_order_on_a_dead_foe_ends_at_once)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 160, 160);
    walker* orc = living_at(FAMILY_ORC, 1, 176, 160);
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, orc);
    s->set_curdir(static_cast<signed char>(FACE_UP));
    s->set_enddir(static_cast<char>(FACE_UP));
    s->set_foe(orc);
    orc->set_dead(1);
    statistics* st = s->stats();
    st->clear_command();
    st->force_command(COMMAND_ATTACK, 5, 1, 0);
    const std::size_t weapons = world().weaplist.size();
    EXPECT_EQ(1, st->do_command());
    EXPECT_TRUE(st->commands.empty()) << "arm 'dead foe': the attack is popped";
    EXPECT_EQ(160, s->xpos());
    EXPECT_EQ(160, s->ypos());
    EXPECT_EQ(FACE_UP, s->curdir()) << "arm 'dead foe': no turn toward it";
    EXPECT_EQ(FACE_UP, s->enddir());
    EXPECT_FLOAT_EQ(0.0f, s->busy()) << "arm 'dead foe': no swing";
    EXPECT_EQ(weapons, world().weaplist.size());
}

// stats.cpp:561 (statistics::do_command, ATTACK). The attacker heads for its
// foe along an 8-way direction: an axis when one delta dominates 3:1, else a
// diagonal. Out of range, it walks one step that way.
TEST_F(SemAi3, an_attacker_out_of_range_steps_toward_its_foe_on_the_snapped_direction)
{
    struct Case
    {
        const char* name;
        int fx, fy;      // the foe's offset
        int face;        // the facing the snapped direction is
        int sx, sy;      // the snapped direction
    };
    const Case cases[] = {
        {"y-major north", 10, -256, FACE_UP, 0, -1},
        {"y-major south", -20, 300, FACE_DOWN, 0, 1},
        {"diagonal", 200, 150, FACE_DOWN_RIGHT, 1, 1},
        {"diagonal", -150, -200, FACE_UP_LEFT, -1, -1},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        SetUp();
        walker* s = living_at(FAMILY_SOLDIER, 0, 512, 512);
        walker* orc = living_at(FAMILY_ORC, 1, static_cast<short>(512 + c.fx),
                                static_cast<short>(512 + c.fy));
        ASSERT_NE(nullptr, s);
        ASSERT_NE(nullptr, orc);
        s->set_curdir(static_cast<signed char>(c.face));
        s->set_enddir(static_cast<char>(c.face));
        s->set_foe(orc);
        walker::FireCheckDenial denial = walker::FireCheckDenial::None;
        ASSERT_FALSE(s->fire_check(static_cast<short>(c.sx), static_cast<short>(c.sy), &denial));
        ASSERT_EQ(walker::FireCheckDenial::OutOfRange, denial);
        statistics* st = s->stats();
        st->clear_command();
        st->force_command(COMMAND_ATTACK, 5, 0, 0);
        EXPECT_EQ(1, st->do_command());
        const int step = static_cast<int>(s->stepsize());
        EXPECT_EQ(512 + c.sx * step, s->xpos()) << "the x half of the snapped step";
        EXPECT_EQ(512 + c.sy * step, s->ypos()) << "the y half of the snapped step";
        EXPECT_EQ(c.face, s->curdir()) << "it keeps facing the snapped direction";
    }
}

// stats.cpp:591 (statistics::do_command, ATTACK). An attacker standing exactly
// on its foe steps out along its own facing, without a draw.
TEST_F(SemAi3, an_attacker_merged_with_its_foe_steps_out_along_its_facing)
{
    struct Case
    {
        int face, sx, sy;
    };
    for (const Case c : {Case{FACE_RIGHT, 1, 0}, Case{FACE_DOWN_LEFT, -1, 1}}) {
        SCOPED_TRACE(c.face);
        SetUp();
        walker* s = living_at(FAMILY_SOLDIER, 0, 160, 160);
        walker* orc = living_at(FAMILY_ORC, 1, 160, 160);
        ASSERT_NE(nullptr, s);
        ASSERT_NE(nullptr, orc);
        s->set_curdir(static_cast<signed char>(c.face));
        s->set_enddir(static_cast<char>(c.face));
        s->set_foe(orc);
        statistics* st = s->stats();
        st->clear_command();
        st->force_command(COMMAND_ATTACK, 5, 0, 0);
        world().rng_.state_ = 777u;
        EXPECT_EQ(1, st->do_command());
        const int step = static_cast<int>(s->stepsize());
        EXPECT_EQ(160 + c.sx * step, s->xpos()) << "arm 'foe coincident': one step along curdir";
        EXPECT_EQ(160 + c.sy * step, s->ypos()) << "arm 'foe coincident': one step along curdir";
        // fire_check's own heading probe (walker::set_weapon_heading) draws
        // one next(4) before the denial; the step-out adds nothing.
        EXPECT_EQ(advanced(777u, 1), world().rng_.state_)
            << "arm 'foe coincident': the step-out draws nothing";
        EXPECT_EQ(160, orc->xpos()) << "the foe stays put";
    }
    // Arm 'not coincident': a foe far to the north of a walker facing east
    // takes the ordinary denial arm (out of range: walk toward it, which for
    // a walker facing elsewhere is a turn in place), not a step east.
    SetUp();
    walker* s = living_at(FAMILY_SOLDIER, 0, 160, 400);
    walker* orc = living_at(FAMILY_ORC, 1, 160, 100);
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, orc);
    s->set_curdir(static_cast<signed char>(FACE_RIGHT));
    s->set_enddir(static_cast<char>(FACE_RIGHT));
    s->set_foe(orc);
    s->stats()->clear_command();
    s->stats()->force_command(COMMAND_ATTACK, 5, 0, 0);
    EXPECT_EQ(1, s->stats()->do_command());
    EXPECT_EQ(160, s->xpos()) << "arm 'not coincident': no step-out along the facing";
    EXPECT_EQ(400, s->ypos()) << "arm 'not coincident': a turn, not a step";
    EXPECT_EQ(FACE_UP, s->enddir()) << "arm 'not coincident': it turns toward the foe";
}

// stats.cpp:571 (statistics::do_command, ATTACK), a classic quirk the old
// game shares: a clear shot forces FIRE(rng(5)) in front of the attack and
// fires now; the round's countdown lands on that FIRE, so with rng(5) <= 1 the
// FIRE is gone after the round and the ATTACK's own count is never touched.
TEST_F(SemAi3, classic_an_attack_that_fires_never_counts_down_its_own_rounds)
{
    for (const std::uint32_t roll : {0u, 1u}) {
        SCOPED_TRACE(roll);
        SetUp();
        walker* s = living_at(FAMILY_SOLDIER, 0, 50, 50);
        walker* slime = living_at(FAMILY_SMALL_SLIME, 1, 60, 50);
        ASSERT_NE(nullptr, s);
        ASSERT_NE(nullptr, slime);
        s->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        s->set_foe(slime);
        s->set_curdir(static_cast<signed char>(FACE_RIGHT));
        s->set_enddir(static_cast<char>(FACE_RIGHT));
        s->set_lastx(s->stepsize());
        s->set_lasty(0.0f);
        FixedRandom scripted_source(roll);
        ScopedSimRandom scripted(&scripted_source);
        statistics* st = s->stats();
        st->clear_command();
        st->add_command(COMMAND_ATTACK, 5, 0, 0);
        ASSERT_EQ(1, st->do_command());
        EXPECT_FLOAT_EQ(s->fire_frequency(), s->busy()) << "arm 'fire_check allows': it fired now";
        EXPECT_EQ(50, s->xpos()) << "shooting does not move the shooter";
        ASSERT_EQ(1u, st->commands.size()) << "rng(5) <= 1: the forced FIRE is already spent";
        EXPECT_EQ(COMMAND_ATTACK, st->commands.front().commandtype);
        EXPECT_EQ(5, st->commands.front().commandcount)
            << "the attack's count is untouched on a round it fires";
    }
}

// stats.cpp:571 (statistics::do_command, ATTACK). A melee-only family at bump
// range (denial NoRanged) turns to face its foe without moving.
TEST_F(SemAi3, a_melee_only_attacker_at_bump_range_turns_to_face_its_foe_without_moving)
{
    walker* orc = living_at(FAMILY_ORC, 0, 50, 50);
    walker* soldier = living_at(FAMILY_SOLDIER, 1, 60, 50);
    ASSERT_NE(nullptr, orc);
    ASSERT_NE(nullptr, soldier);
    orc->set_curdir(static_cast<signed char>(FACE_UP));
    orc->set_enddir(static_cast<char>(FACE_UP));
    orc->set_foe(soldier);
    walker::FireCheckDenial denial = walker::FireCheckDenial::None;
    ASSERT_FALSE(orc->fire_check(1, 0, &denial));
    ASSERT_EQ(walker::FireCheckDenial::NoRanged, denial);
    statistics* st = orc->stats();
    st->clear_command();
    st->force_command(COMMAND_ATTACK, 5, 0, 0);
    EXPECT_EQ(1, st->do_command());
    EXPECT_EQ(50, orc->xpos()) << "arm 'denial NoRanged': no step";
    EXPECT_EQ(50, orc->ypos()) << "arm 'denial NoRanged': no step";
    EXPECT_EQ(FACE_RIGHT, orc->curdir()) << "arm 'denial NoRanged': faces the foe";
    EXPECT_EQ(FACE_RIGHT, orc->enddir()) << "arm 'denial NoRanged': faces the foe";
}

// stats.cpp:633 (statistics::do_command, UNCHARM / default). UNCHARM and an
// unknown command are no-ops the countdown consumes: a charmed walker stays
// charmed (charm ends on its timer alone, #317).
TEST_F(SemAi3, an_uncharm_order_or_an_unknown_one_is_only_counted_down)
{
    for (const int type : {COMMAND_UNCHARM, 999}) {
        SCOPED_TRACE(type);
        SetUp();
        walker* s = living_at(FAMILY_SOLDIER, 1, 160, 160);
        ASSERT_NE(nullptr, s);
        s->set_real_team_num(0);  // charmed onto team 1 from team 0
        statistics* st = s->stats();
        st->clear_command();
        st->force_command(type, 3, 0, 0);
        EXPECT_EQ(1, st->do_command());
        EXPECT_EQ(1, s->team_num()) << "arm 'UNCHARM or unknown': still charmed";
        EXPECT_EQ(0, s->real_team_num());
        ASSERT_EQ(1u, st->commands.size()) << "nothing re-queued behind it";
        EXPECT_EQ(type, st->commands.front().commandtype);
        EXPECT_EQ(2, st->commands.front().commandcount);
        EXPECT_EQ(1, st->do_command());
        EXPECT_EQ(1, st->do_command());
        EXPECT_TRUE(st->commands.empty()) << "consumed by the countdown";
        EXPECT_EQ(1, s->team_num()) << "still charmed after the order ran out";
        EXPECT_EQ(0, s->real_team_num());
        EXPECT_EQ(160, s->xpos());
    }
}

class SemHit : public SemAi3
{
protected:
    walker* hero_at(int family, unsigned char team, short x, short y, float hp, bool named)
    {
        walker* w = living_at(family, team, x, y);
        if (w == nullptr)
            return nullptr;
        if (named) {
            auto g = std::make_unique<guy>(family);
            g->name = "Bob";
            w->set_owned_myguy(std::move(g));
        }
        w->stats()->set_max_hitpoints(100.0f);
        w->stats()->set_hitpoints(hp);
        w->set_yo_delay(0);
        w->set_specials_disabled(true);
        w->stats()->clear_command();
        return w;
    }
};

// stats.cpp:678 (statistics::hit_response). A dead attacker, a dead victim
// and a player-controlled victim never react: no retarget, no yell, no flee.
TEST_F(SemHit, a_dead_or_player_controlled_victim_and_a_dead_attacker_draw_no_response)
{
    struct Case
    {
        const char* name;
        bool victim_dead, attacker_dead, control;
    };
    const Case cases[] = {
        {"victim dead", true, false, false},
        {"attacker dead", false, true, false},
        {"victim ACT_CONTROL", false, false, true},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        SetUp();
        walker* hero = hero_at(FAMILY_SOLDIER, 0, 160, 160, 1.0f, true);
        walker* orc = living_at(FAMILY_ORC, 1, 176, 160);
        ASSERT_NE(nullptr, hero);
        ASSERT_NE(nullptr, orc);
        if (c.victim_dead)
            hero->set_dead(1);
        if (c.attacker_dead)
            orc->set_dead(1);
        if (c.control)
            hero->set_act_type(ACT_CONTROL);
        hero->stats()->add_command(COMMAND_SEARCH, 7, 0, 0);
        hero->stats()->hit_response(orc);
        EXPECT_EQ(nullptr, hero->foe()) << "no retarget";
        EXPECT_EQ(nullptr, orc->foe()) << "the attacker is not marked";
        EXPECT_EQ(0, hero->yo_delay()) << "no yell";
        ASSERT_EQ(1u, hero->stats()->commands.size()) << "the queue is untouched (no flee walk)";
        EXPECT_EQ(COMMAND_SEARCH, hero->stats()->commands.front().commandtype);
    }
    // Control: the same victim, alive and AI-driven, retargets and yells.
    SetUp();
    walker* hero = hero_at(FAMILY_SOLDIER, 0, 160, 160, 1.0f, true);
    walker* orc = living_at(FAMILY_ORC, 1, 176, 160);
    ASSERT_NE(nullptr, hero);
    ASSERT_NE(nullptr, orc);
    hero->stats()->hit_response(orc);
    EXPECT_EQ(orc, hero->foe());
    EXPECT_EQ(80, hero->yo_delay()) << "control: an AI victim yells";
}

// stats.cpp:695 (statistics::hit_response). A family's Lua hit_response hook
// replaces the default response: a badly hurt mage teleports by its hook and
// the C++ path's yell and flee never run.
TEST_F(SemHit, a_family_hit_response_hook_replaces_the_default_yell_and_flee)
{
    walker* mage = hero_at(FAMILY_MAGE, 0, 320, 320, 10.0f, true);
    walker* orc = living_at(FAMILY_ORC, 1, 336, 320);
    ASSERT_NE(nullptr, mage);
    ASSERT_NE(nullptr, orc);
    mage->set_specials_disabled(false);
    mage->stats()->set_hitpoints(10.0f);  // below the C++ path's 1/2 threshold
    mage->stats()->hit_response(orc);
    EXPECT_EQ(0, mage->yo_delay()) << "arm 'hook returns true': no C++ yell";
    for (const command& c : mage->stats()->commands)
        EXPECT_FALSE(c.commandtype == COMMAND_WALK && c.commandcount == 16)
            << "arm 'hook returns true': no C++ flee walk";
    EXPECT_EQ(1, mage->current_special()) << "the hook's own answer: the teleport slot";
}

// stats.cpp:699 (statistics::hit_response). Struck, an AI walker whose special
// is available fires it one time in three: check_special gates BEFORE the
// rng(3) draw.
TEST_F(SemHit, a_struck_walker_rolls_for_its_special_only_when_the_special_is_available)
{
    // check_special true (shifter roll 0), rng(3) == 1: no special, two draws.
    {
        walker* elf = hero_at(FAMILY_ELF, 0, 320, 320, 100.0f, false);
        walker* foe = living_at(FAMILY_SOLDIER, 1, 325, 320);
        ASSERT_NE(nullptr, elf);
        ASSERT_NE(nullptr, foe);
        elf->set_specials_disabled(false);
        const float mp = elf->stats()->magicpoints();
        const std::uint32_t seed = state_where_draws_are({2, 3}, {0, 1});
        world().rng_.state_ = seed;
        elf->stats()->hit_response(foe);
        EXPECT_EQ(advanced(seed, 2), world().rng_.state_)
            << "arm 'check_special true, rng(3) != 0': the shifter roll and rng(3), nothing else";
        EXPECT_FLOAT_EQ(mp, elf->stats()->magicpoints()) << "no special cast";
        EXPECT_EQ(foe, elf->foe()) << "the default response retargets";
    }
    // check_special false (specials disabled): no draw at all.
    {
        SetUp();
        walker* elf = hero_at(FAMILY_ELF, 0, 320, 320, 100.0f, false);
        walker* foe = living_at(FAMILY_SOLDIER, 1, 325, 320);
        ASSERT_NE(nullptr, elf);
        ASSERT_NE(nullptr, foe);
        const float mp = elf->stats()->magicpoints();
        world().rng_.state_ = 4242u;
        elf->stats()->hit_response(foe);
        EXPECT_EQ(4242u, world().rng_.state_) << "arm 'check_special false': no rng(3) draw";
        EXPECT_FLOAT_EQ(mp, elf->stats()->magicpoints());
        EXPECT_EQ(foe, elf->foe());
    }
    // check_special false (Gameplay Fixes AB: cannot pay): the shifter roll only.
    {
        SetUp();
        world().gameplay_fixes = 1;
        walker* elf = hero_at(FAMILY_ELF, 0, 320, 320, 100.0f, false);
        walker* foe = living_at(FAMILY_SOLDIER, 1, 325, 320);
        ASSERT_NE(nullptr, elf);
        ASSERT_NE(nullptr, foe);
        elf->set_specials_disabled(false);
        elf->stats()->set_magicpoints(0.0f);
        world().rng_.state_ = 4242u;
        elf->stats()->hit_response(foe);
        EXPECT_EQ(advanced(4242u, 1), world().rng_.state_)
            << "arm 'check_special false' (fixes on, no mana): one draw, no rng(3)";
        EXPECT_FLOAT_EQ(0.0f, elf->stats()->magicpoints());
    }
}

// stats.cpp:701 (statistics::hit_response). A named character flees for help
// below half its hp; a walker without a guy only below 5/16.
TEST_F(SemHit, a_named_hero_flees_below_half_hp_and_a_bot_only_below_five_sixteenths)
{
    struct Case
    {
        const char* name;
        bool named;
        float hp;
        bool yells;
    };
    const Case cases[] = {
        {"named, 40 of 100", true, 40.0f, true},
        {"bot, 40 of 100", false, 40.0f, false},
        {"named, 50 of 100 (the boundary)", true, 50.0f, false},
        {"named, 49 of 100", true, 49.0f, true},
        {"bot, 31.25 of 100 (the boundary)", false, 31.25f, false},
        {"bot, 31 of 100", false, 31.0f, true},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        SetUp();
        walker* s = hero_at(FAMILY_SOLDIER, 0, 160, 160, c.hp, c.named);
        walker* orc = living_at(FAMILY_ORC, 1, 176, 160);
        ASSERT_NE(nullptr, s);
        ASSERT_NE(nullptr, orc);
        s->stats()->hit_response(orc);
        EXPECT_EQ(orc, s->foe());
        if (c.yells) {
            EXPECT_EQ(80, s->yo_delay()) << "yells";
            ASSERT_EQ(1u, s->stats()->commands.size());
            const command& w = s->stats()->commands.front();
            EXPECT_EQ(COMMAND_WALK, w.commandtype) << "runs";
            EXPECT_EQ(16, w.commandcount);
            EXPECT_EQ(-1, w.com1) << "away from the attacker";
            EXPECT_EQ(0, w.com2);
            EXPECT_TRUE(w.forced);
        } else {
            EXPECT_EQ(0, s->yo_delay()) << "holds";
            EXPECT_TRUE(s->stats()->commands.empty()) << "no flee walk";
        }
    }
}

// stats.cpp:738 (statistics::yell_for_help). A yell recruits every living,
// non-dormant, non-hidden friend within 160 px (player-controlled heroes too):
// leader = the yeller, foe = the attacker; one switching onto that foe resets
// its chase distances to 32000, one already on it keeps them. Anyone else is
// untouched.
TEST_F(SemHit, a_yell_recruits_friends_in_range_and_resets_only_a_newly_switched_chase)
{
    walker* yeller = hero_at(FAMILY_SOLDIER, 0, 160, 160, 100.0f, false);
    walker* attacker = living_at(FAMILY_ORC, 1, 180, 160);
    walker* other_orc = living_at(FAMILY_ORC, 1, 800, 800);
    ASSERT_NE(nullptr, yeller);
    ASSERT_NE(nullptr, attacker);
    ASSERT_NE(nullptr, other_orc);

    const auto distances = [](walker* w, std::uint32_t last, std::int32_t current) {
        w->stats()->set_last_distance(last);
        w->stats()->set_current_distance(current);
    };
    walker* on_it = living_at(FAMILY_SOLDIER, 0, 130, 160);
    walker* hero = living_at(FAMILY_SOLDIER, 0, 160, 260);  // 100 px
    walker* far = living_at(FAMILY_SOLDIER, 0, 160, 360);   // 200 px
    walker* dormant = living_at(FAMILY_SOLDIER, 0, 160, 210);
    walker* hidden = living_at(FAMILY_SOLDIER, 0, 110, 160);
    walker* dead = living_at(FAMILY_SOLDIER, 0, 200, 200);
    walker* enemy = living_at(FAMILY_SOLDIER, 1, 140, 160);
    for (walker* w : {on_it, hero, far, dormant, hidden, dead, enemy})
        ASSERT_NE(nullptr, w);
    on_it->set_foe(attacker);
    distances(on_it, 77, 77);
    hero->set_act_type(ACT_CONTROL);
    hero->set_foe(other_orc);
    distances(hero, 55, 55);
    dormant->set_dormant(true);
    hidden->set_hidden(true);
    dead->set_dead(1);
    for (walker* w : {far, dormant, hidden, dead, enemy}) {
        w->set_foe(nullptr);
        w->set_leader(nullptr);
        distances(w, 55, 55);
    }

    yeller->stats()->yell_for_help(attacker);

    EXPECT_EQ(yeller, on_it->leader()) << "arm 'ally already on the attacker': led";
    EXPECT_EQ(attacker, on_it->foe());
    EXPECT_EQ(77u, on_it->stats()->last_distance()) << "arm 'ally already on the attacker': distances kept";
    EXPECT_EQ(77, on_it->stats()->current_distance());

    EXPECT_EQ(yeller, hero->leader()) << "arm 'ACT_CONTROL friend': a player's hero is recruited";
    EXPECT_EQ(attacker, hero->foe());
    EXPECT_EQ(32000u, hero->stats()->last_distance()) << "arm 'ACT_CONTROL friend': switched, reset";
    EXPECT_EQ(32000, hero->stats()->current_distance());

    const struct
    {
        const char* name;
        walker* w;
    } untouched[] = {{"beyond 160 px", far}, {"dormant", dormant}, {"hidden", hidden},
                     {"dead", dead}, {"an enemy", enemy}};
    for (const auto& u : untouched) {
        SCOPED_TRACE(u.name);
        EXPECT_EQ(nullptr, u.w->leader()) << "arm 'not a recruit': no leader";
        EXPECT_EQ(nullptr, u.w->foe()) << "arm 'not a recruit': no foe";
        EXPECT_EQ(55u, u.w->stats()->last_distance());
        EXPECT_EQ(55, u.w->stats()->current_distance());
    }
}

// stats.cpp:750 (statistics::yell_for_help). The yeller flees straight away
// from its attacker; a coincident attacker gives (0,0), which the forced
// walk's clamp turns into south-east.
TEST_F(SemHit, a_yeller_on_top_of_its_attacker_flees_south_east)
{
    walker* s = hero_at(FAMILY_SOLDIER, 0, 160, 160, 100.0f, false);
    walker* orc = living_at(FAMILY_ORC, 1, 160, 160);
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, orc);
    s->stats()->yell_for_help(orc);
    ASSERT_EQ(1u, s->stats()->commands.size());
    const command& w = s->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, w.commandtype);
    EXPECT_EQ(16, w.commandcount);
    EXPECT_EQ(1, w.com1) << "arm 'foe coincident': (0,0) clamps to east";
    EXPECT_EQ(1, w.com2) << "arm 'foe coincident': (0,0) clamps to south";
    EXPECT_TRUE(w.forced);
}

// stats.cpp:756 (statistics::yell_for_help). A fearless walker still yells
// (yo_delay +80) and still recruits, but holds its ground. The KIT_FEARLESS
// mark counts under both New Specials values; a friendly war banner's rally
// radius only with New Specials on.
TEST_F(SemHit, a_fearless_walker_yells_and_recruits_but_does_not_run)
{
    for (const int ns : {0, 1}) {
        SCOPED_TRACE(ns);
        SetUp();
        world().new_specials = static_cast<short>(ns);
        walker* s = hero_at(FAMILY_SOLDIER, 0, 160, 160, 100.0f, false);
        walker* orc = living_at(FAMILY_ORC, 1, 200, 160);
        walker* ally = living_at(FAMILY_SOLDIER, 0, 130, 160);
        ASSERT_NE(nullptr, s);
        ASSERT_NE(nullptr, orc);
        ASSERT_NE(nullptr, ally);
        s->set_kit_state(KIT_FEARLESS);
        s->set_yo_delay(5);
        s->stats()->yell_for_help(orc);
        EXPECT_EQ(85, s->yo_delay()) << "arm 'fearless': still yells";
        EXPECT_TRUE(s->stats()->commands.empty()) << "arm 'fearless': no flee walk";
        EXPECT_EQ(s, ally->leader()) << "arm 'fearless': friends still answer";
        EXPECT_EQ(orc, ally->foe());
    }
    for (const int ns : {0, 1}) {
        SCOPED_TRACE(testing::Message() << "banner, new_specials " << ns);
        SetUp();
        world().new_specials = static_cast<short>(ns);
        walker* s = hero_at(FAMILY_SOLDIER, 0, 160, 160, 100.0f, false);
        walker* orc = living_at(FAMILY_ORC, 1, 200, 160);
        walker* banner = weapon_at(21 /* core:war_banner */, 0, 180, 170, 16, 16);
        ASSERT_NE(nullptr, s);
        ASSERT_NE(nullptr, orc);
        ASSERT_NE(nullptr, banner);
        s->stats()->yell_for_help(orc);
        EXPECT_EQ(80, s->yo_delay());
        if (ns == 1) {
            EXPECT_TRUE(s->stats()->commands.empty()) << "New Specials on: the banner holds it";
        } else {
            ASSERT_EQ(1u, s->stats()->commands.size()) << "New Specials off: no banner aura";
            EXPECT_EQ(COMMAND_WALK, s->stats()->commands.front().commandtype);
            EXPECT_EQ(-1, s->stats()->commands.front().com1);
        }
    }
}

// stats.cpp:758 (statistics::yell_for_help), a classic quirk the old game
// shares: only a named character on team 0 announces its yell.
TEST_F(SemHit, classic_only_a_named_team_zero_hero_announces_its_yell)
{
    struct Case
    {
        const char* name;
        unsigned char team;
        bool named;
        bool announced;
    };
    const Case cases[] = {
        {"named, team 0", 0, true, true},
        {"named, team 1", 1, true, false},
        {"bot, team 0", 0, false, false},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        SetUp();
        walker* s = hero_at(FAMILY_SOLDIER, c.team, 160, 160, 100.0f, c.named);
        walker* orc = living_at(FAMILY_ORC, 2, 200, 160);
        ASSERT_NE(nullptr, s);
        ASSERT_NE(nullptr, orc);
        events().clear();
        s->stats()->yell_for_help(orc);
        const auto notes = notifications();
        if (c.announced) {
            ASSERT_EQ(1u, notes.size()) << "arm 'myguy and team 0': announced";
            EXPECT_EQ("Bob yells for help!", notes[0]->text);
            EXPECT_EQ(10u, notes[0]->a) << "shown for 10";
            EXPECT_EQ(-1, notes[0]->target_player) << "to everyone";
        } else {
            EXPECT_EQ(0u, notes.size()) << "arm 'no myguy or team != 0': silent";
        }
        EXPECT_EQ(80, s->yo_delay()) << "it yelled either way";
    }
}

// stats.cpp:798 (statistics::right_blocked). A facing outside 0..7 probes the
// walker's own spot: free on an empty spot; blocked by a body overlapping it,
// which the loud probe records and the quiet one does not.
TEST_F(SemAi3, the_right_probe_of_an_out_of_range_facing_asks_the_walkers_own_spot)
{
    walker* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
    walker* orc = living_at(FAMILY_ORC, 2, 334, 320);  // overlaps the soldier's own box by one pixel
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, orc);
    s->set_curdir(static_cast<signed char>(127));
    s->set_collide_ob(nullptr);
    EXPECT_TRUE(s->stats()->right_blocked(true)) << "quiet: the overlapping body blocks";
    EXPECT_EQ(nullptr, s->collide_ob()) << "quiet: nothing recorded";
    EXPECT_TRUE(s->stats()->right_blocked(false)) << "loud: the overlapping body blocks";
    EXPECT_EQ(orc, s->collide_ob()) << "loud: the body is recorded";

    orc->setxy(600, 600);
    s->set_collide_ob(nullptr);
    EXPECT_FALSE(s->stats()->right_blocked(false)) << "an empty spot is free";
    EXPECT_EQ(nullptr, s->collide_ob());
}
