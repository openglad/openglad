/* A11 regression tests: the .fss per-object command byte is honored for
 * Order::Living GUARD commands (and ONLY those).
 *
 * The writer has round-tripped `act_type` since the original 2002 format, but
 * every loader read-and-dropped the byte, so authored guards roamed for 24
 * years. The restore is deliberately narrow:
 *   - Living + command==ACT_GUARD  -> set_act_type(ACT_GUARD)
 *   - Living + any other command   -> family default (ACT_CONTROL from a
 *     hostile file must never steal player control)
 *   - non-Living commands          -> ignored (serialization noise; stock
 *     treasures dump act 2)
 * No stock campaign ships a non-zero Living command byte (scanned: 0 across
 * gladiator/tryxian/arenas/ctf/concept), so legacy levels and all parity
 * goldens are byte-identical. The shipped Westlands package already carries
 * 184 authored Living GUARD bytes — pinned in test_westlands_levels.cpp.
 */
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/og_file.h>

#include <gtest/gtest.h>

#include <string>

namespace {

// Save `world` to the PhysFS write dir and reload it into `into`. Returns
// false on any IO failure.
bool round_trip(GameWorld& world, GameWorld& into, const std::string& file)
{
    og::data::LevelFileMetadata metadata;
    metadata.grid_file = "guardact";
    og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
    if (!og::data::save_level_scenario_file(world, file, metadata, &err))
        return false;

    auto infile = og::io::og_open_read(file.c_str());
    if (!infile)
        return false;
    char header[3] = {};
    char version = 0;
    if (infile->read(header, 1, 3) != 3 || infile->read(&version, 1, 1) != 1)
        return false;
    og::data::LevelFileMetadata meta2;
    return og::data::load_scenario_version(*infile, &into, &meta2,
                                           static_cast<short>(version)) == 1;
}

const walker* find_family(const GameWorld& w, int family)
{
    for (const auto& uptr : w.oblist)
    {
        const walker* ob = uptr.get();
        if (ob != nullptr && ob->family() == family)
            return ob;
    }
    return nullptr;
}

} // namespace

TEST(GuardActType, loader_restores_guard_command_for_livings_only)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* guard = w.add_ob(Order::Living, FAMILY_ORC);
    walker* control_like = w.add_ob(Order::Living, FAMILY_SKELETON);
    walker* treasure = w.add_fx_ob(Order::Treasure, FAMILY_DRUMSTICK);
    ASSERT_NE(guard, nullptr);
    ASSERT_NE(control_like, nullptr);
    ASSERT_NE(treasure, nullptr);
    guard->setxy(160, 160);
    guard->set_team_num(2);
    guard->set_act_type(ACT_GUARD);
    control_like->setxy(224, 224);
    control_like->set_team_num(2);
    control_like->set_act_type(ACT_CONTROL); // hostile-file scenario
    treasure->setxy(288, 288);
    // A GUARD byte on a non-Living order is serialization noise and must be
    // ignored by the loader (the restore is Living-only).
    treasure->set_act_type(ACT_GUARD);

    // A fresh walker of the same family carries the default act type the
    // loader must fall back to for anything but a Living GUARD byte.
    const short living_default = w.add_ob(Order::Living, FAMILY_ELF)->act_type();

    TestGameWorld tw2(2);
    GameWorld& w2 = tw2.world();
    ASSERT_TRUE(round_trip(w, w2, "guard_act_roundtrip.fss"));

    const walker* loaded_guard = find_family(w2, FAMILY_ORC);
    const walker* loaded_control = find_family(w2, FAMILY_SKELETON);
    const walker* loaded_treasure = find_family(w2, FAMILY_DRUMSTICK);
    ASSERT_NE(loaded_guard, nullptr);
    ASSERT_NE(loaded_control, nullptr);
    ASSERT_NE(loaded_treasure, nullptr);

    EXPECT_EQ(ACT_GUARD, loaded_guard->act_type())
        << "an authored Living GUARD command must survive loading";
    EXPECT_NE(ACT_CONTROL, loaded_control->act_type())
        << "a file must never assign ACT_CONTROL (player-control steal)";
    EXPECT_EQ(living_default, loaded_control->act_type())
        << "non-GUARD Living commands keep the family default";
    EXPECT_NE(ACT_GUARD, loaded_treasure->act_type())
        << "non-Living command bytes are serialization noise and stay ignored";
}

TEST(GuardActType, loaded_guard_holds_position_while_roamers_may_move)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* guard = w.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(guard, nullptr);
    guard->setxy(10 * GRID_SIZE, 10 * GRID_SIZE);
    guard->set_team_num(2);
    guard->set_act_type(ACT_GUARD);
    // The hero below is inside this guard's clear sight, and a plain guard
    // now WAKES into ACT_RANDOM pursuit on a genuine sighting (walker.cpp
    // act_guard, 2026-07-11). This test pins the classic stationary-sentry
    // contract, so author the hold-post policy bit (npc_flags bit 1) — it
    // must round-trip through the writer/loader with the GUARD byte.
    guard->set_guard_hold_post(true);

    // A distant hostile keeps the level alive and gives AI a foe to seek.
    walker* hero = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(hero, nullptr);
    hero->setxy(2 * GRID_SIZE, 2 * GRID_SIZE);
    hero->set_team_num(0);

    TestGameWorld tw2(2);
    GameWorld& w2 = tw2.world();
    ASSERT_TRUE(round_trip(w, w2, "guard_act_hold.fss"));

    walker* loaded_guard = nullptr;
    for (const auto& uptr : w2.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->family() == FAMILY_ORC)
            loaded_guard = ob;
    }
    ASSERT_NE(loaded_guard, nullptr);
    ASSERT_EQ(ACT_GUARD, loaded_guard->act_type());
    ASSERT_TRUE(loaded_guard->guard_hold_post())
        << "the hold-post bit must survive the round trip with the GUARD byte";

    const float gx = loaded_guard->xpos();
    const float gy = loaded_guard->ypos();
    for (int t = 0; t < 80; ++t)
        w2.tick();

    ASSERT_FALSE(loaded_guard->dead());
    EXPECT_EQ(gx, loaded_guard->xpos())
        << "a guard must hold its post (act_guard never walks)";
    EXPECT_EQ(gy, loaded_guard->ypos());
    EXPECT_EQ(ACT_GUARD, loaded_guard->act_type())
        << "nothing in the tick loop may demote a loaded hold-post guard";
    EXPECT_GE(w2.level_tick_count(), 80u) << "the sim must actually have run";
}
