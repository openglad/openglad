/* A12 regression tests: difficulty scaling of placed walkers and generator
 * spawns.
 *
 * The difficulty percent (50/100/200%) scales a walker off the player's team
 * only: living::set_difficulty and walker::set_difficulty's default arm ask
 * `team_num() != 0` ("do all EXCEPT player characters", 2002), on both
 * settings, as the old game does. No team-0 walker takes it: a placed allied
 * NPC (the SAVE_ALL Bearer, an allied war-host), a player's summon (a raised
 * skeleton or ghost, a WARBAND grunt, a slime child, an archmage's elemental
 * or phantom) and every team-0 shot keep 100% (#493, the maintainer's
 * ruling: allies are not scaled). From the A12 work to 2026-11-08 a team-0
 * walker with no player character took the percent (A12a) and, from #473,
 * its shots too; both are reverted.
 *
 * A12b: generator spawns received set_difficulty TWICE (create_weapon at the
 * generator's level, then fire()'s generator branch at the rolled level) —
 * legacy-original 2002 behavior that compounded two additive hp/regen boosts
 * and a squared difficulty multiplier. Now only the fire()-branch rolled-level
 * application remains: a spawn's stats equal a fresh living of the same
 * family given ONE set_difficulty at the rolled level.
 */
#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "../test_sim_random_scope.h"

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Body
{
    float max_hp = -1.0f;
    float max_mp = -1.0f;
    float damage = -1.0f;
};

Body body_of(walker* ob)
{
    if (ob == nullptr)
        return {};
    return {ob->stats()->max_hitpoints(), ob->stats()->max_magicpoints(), ob->damage()};
}

// A level-3 `family` walker on `team`, scaled as a placed walker is
// (set_difficulty at the world's current percent); `hero` gives it a player
// character.
walker* placed_living(GameWorld& w, int family, unsigned char team, bool hero = false)
{
    walker* ob = w.add_ob(Order::Living, family);
    if (ob == nullptr)
        return nullptr;
    ob->set_team_num(team);
    if (hero)
        ob->set_owned_myguy(std::make_unique<guy>(family));
    ob->stats()->set_level(3);
    ob->set_difficulty(3);
    return ob;
}

}  // namespace

// #493: a placed team-0 NPC (no player character: the SAVE_ALL Bearer, an
// allied war-host) never takes the difficulty percent, on both settings: on
// Hard and on Easy its hit points, magic and damage are the same NPC's at
// 100%, bit for bit (team 0 never multiplies). A team-1 soldier staged the
// same way in the same world takes the percent: the control that proves the
// percent was live.
TEST(DifficultyScaling, placed_team0_npcs_never_scale_with_difficulty)
{
    for (const short fixes : {short{0}, short{1}})
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.gameplay_fixes = fixes;
        w.difficulty = 100;
        const Body ref = body_of(placed_living(w, FAMILY_SOLDIER, 0));
        const Body foe_ref = body_of(placed_living(w, FAMILY_SOLDIER, 1));
        ASSERT_GT(ref.max_hp, 0.0f) << "staging: hit points to scale";
        ASSERT_GT(ref.max_mp, 0.0f) << "staging: magic to scale";
        ASSERT_GT(ref.damage, 0.0f) << "staging: damage to scale";
        for (const short percent : {short{200}, short{50}})
        {
            w.difficulty = percent;
            const float k = static_cast<float>(percent) / 100.0f;
            walker* npc = placed_living(w, FAMILY_SOLDIER, 0);
            ASSERT_NE(nullptr, npc);
            ASSERT_EQ(nullptr, npc->myguy) << "staging: a placed NPC carries no player character";
            const Body got = body_of(npc);
            EXPECT_EQ(ref.max_hp, got.max_hp)
                << "a placed team-0 NPC never takes the difficulty (#493), " << percent << "%, fixes " << fixes;
            EXPECT_EQ(ref.max_mp, got.max_mp)
                << "a placed team-0 NPC never takes the difficulty (#493), " << percent << "%, fixes " << fixes;
            EXPECT_EQ(ref.damage, got.damage)
                << "a placed team-0 NPC never takes the difficulty (#493), " << percent << "%, fixes " << fixes;

            const Body foe = body_of(placed_living(w, FAMILY_SOLDIER, 1));
            EXPECT_FLOAT_EQ(k * foe_ref.max_hp, foe.max_hp)
                << "control: a team-1 soldier takes " << percent << "%, fixes " << fixes;
            EXPECT_FLOAT_EQ(k * foe_ref.max_mp, foe.max_mp)
                << "control: a team-1 soldier takes " << percent << "%, fixes " << fixes;
            EXPECT_FLOAT_EQ(k * foe_ref.damage, foe.damage)
                << "control: a team-1 soldier takes " << percent << "%, fixes " << fixes;
        }
    }
}

// A team-0 walker carrying a player character never takes the percent, on
// both settings: on Hard and on Easy it is the same hero at 100%, bit for bit.
TEST(DifficultyScaling, player_characters_stay_exempt_from_difficulty)
{
    for (const short fixes : {short{0}, short{1}})
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.gameplay_fixes = fixes;
        w.difficulty = 100;
        const Body ref = body_of(placed_living(w, FAMILY_SOLDIER, 0, true));
        ASSERT_GT(ref.max_hp, 0.0f) << "staging: hit points to scale";
        ASSERT_GT(ref.damage, 0.0f) << "staging: damage to scale";
        for (const short percent : {short{200}, short{50}})
        {
            w.difficulty = percent;
            walker* player = placed_living(w, FAMILY_SOLDIER, 0, true);
            ASSERT_NE(nullptr, player);
            ASSERT_NE(nullptr, player->myguy) << "staging: a player character";
            const Body got = body_of(player);
            EXPECT_EQ(ref.max_hp, got.max_hp)
                << "player characters must never scale with the difficulty setting, " << percent
                << "%, fixes " << fixes;
            EXPECT_EQ(ref.max_mp, got.max_mp) << percent << "%, fixes " << fixes;
            EXPECT_EQ(ref.damage, got.damage) << percent << "%, fixes " << fixes;
        }
    }
}

// Off team 0 the percent multiplies max hp, max mp and damage, on both
// settings: on Hard twice the same foe's at 100%, on Easy half.
TEST(DifficultyScaling, hostile_team_legacy_scaling_unchanged)
{
    for (const short fixes : {short{0}, short{1}})
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.gameplay_fixes = fixes;
        w.difficulty = 100;
        const Body ref = body_of(placed_living(w, FAMILY_ORC, 1));
        ASSERT_GT(ref.max_hp, 0.0f) << "staging: hit points to scale";
        ASSERT_GT(ref.max_mp, 0.0f) << "staging: magic to scale";
        ASSERT_GT(ref.damage, 0.0f) << "staging: damage to scale";
        for (const short percent : {short{200}, short{50}})
        {
            w.difficulty = percent;
            const float k = static_cast<float>(percent) / 100.0f;
            const Body foe = body_of(placed_living(w, FAMILY_ORC, 1));
            EXPECT_FLOAT_EQ(k * ref.max_hp, foe.max_hp)
                << "hostile-team scaling is the legacy path and must not change, " << percent
                << "%, fixes " << fixes;
            EXPECT_FLOAT_EQ(k * ref.max_mp, foe.max_mp) << percent << "%, fixes " << fixes;
            EXPECT_FLOAT_EQ(k * ref.damage, foe.damage) << percent << "%, fixes " << fixes;
        }
    }
}

namespace {

// What a cleric's raise left behind.
struct Raised
{
    Body body;
    int team = -1;
    bool owned_by_the_cleric = false;
    bool carries_a_character = true;
};

// A level-4 cleric on `team` (`hero`: carrying a player character) at (80,80)
// on an open grass field casts `slot` (2: RAISE UNDEAD, 3: RAISE GHOST)
// through the real walker::special() and the shipped core pack, on an orc's
// bloodstain in reach, at `percent`. Every bounded sim draw answers 0, so the
// risen level is rand0(4) + 1 = 1 on every run: only the percent and the
// team differ between two raises.
void raise_once(short fixes, short percent, unsigned char team, bool hero, int slot, Raised* out)
{
    FixedRandom zero(0);
    ScopedSimRandom scoped(&zero);
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.resize_grid(64, 64);
    std::fill_n(w.grid.data.get(), static_cast<std::size_t>(w.grid.w) * w.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    w.my_team = 0;
    w.gameplay_fixes = fixes;
    w.allied_mode = 0;
    w.enemy_freeze = 0;
    w.rng_.state_ = 12345u;
    w.difficulty = percent;

    walker* cleric = w.add_ob(Order::Living, FAMILY_CLERIC);
    ASSERT_NE(nullptr, cleric);
    cleric->set_team_num(team);
    cleric->set_real_team_num(255);
    cleric->setxy(static_cast<short>(81), static_cast<short>(80));
    cleric->setxy(static_cast<short>(80), static_cast<short>(80));
    cleric->stats()->set_level(4);
    cleric->stats()->set_max_hitpoints(100.0f);
    cleric->stats()->set_hitpoints(100.0f);
    cleric->stats()->set_max_magicpoints(200.0f);
    cleric->stats()->set_magicpoints(200.0f);
    cleric->set_busy(0);
    cleric->set_user(-1);
    cleric->set_current_special(static_cast<char>(slot));
    cleric->set_shifter_down(0);
    if (hero)
    {
        auto g = std::make_unique<guy>(FAMILY_CLERIC);
        g->teamnum = team;
        g->exp = 0;
        g->total_shots = 0;
        g->scen_shots = 0;
        cleric->set_owned_myguy(std::move(g));
    }

    // The bloodstain on the first passable spot east, south, west or north of
    // the cleric inside the slot's reach (60 px for RAISE UNDEAD, 30 for
    // RAISE GHOST).
    const std::int32_t reach = slot == 2 ? 60 : 30;
    walker* stain = w.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain);
    stain->set_team_num(1);
    stain->set_real_team_num(1);
    stain->stats()->set_old_family(FAMILY_ORC);
    stain->stats()->set_level(1);
    stain->set_dead(0);
    bool placed = false;
    for (std::int32_t d = 1; d < reach && !placed; d++)
    {
        const std::int32_t ring[4][2] = {{d, 0}, {0, d}, {-d, 0}, {0, -d}};
        for (const auto& o : ring)
        {
            const float x = static_cast<float>(cleric->xpos() + o[0]);
            const float y = static_cast<float>(cleric->ypos() + o[1]);
            stain->setxy(x, y);
            if (cleric->distance_to_ob(stain) < reach && w.query_passable(x, y, stain))
            {
                placed = true;
                break;
            }
        }
    }
    ASSERT_TRUE(placed) << "staging: a passable stain spot within " << reach;

    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
    ASSERT_TRUE(cleric->special(&why, &reason)) << "slot " << slot << ": " << reason;
    const int family = slot == 2 ? FAMILY_SKELETON : FAMILY_GHOST;
    std::vector<walker*> risen;
    for (const GameWorld::EntityList* list : {&w.oblist, &w.fxlist, &w.weaplist})
        for (const auto& ob : *list)
            if (ob != nullptr && ob->dead() == 0 && ob->query_order() == Order::Living &&
                static_cast<int>(static_cast<unsigned char>(ob->family())) == family)
                risen.push_back(ob.get());
    ASSERT_EQ(1u, risen.size()) << "slot " << slot << ": exactly one walker rises";
    out->body = body_of(risen[0]);
    out->team = static_cast<int>(risen[0]->team_num());
    out->owned_by_the_cleric = risen[0]->owner() == cleric;
    out->carries_a_character = risen[0]->myguy != nullptr;
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

}  // namespace

// #493: a player's summon is an ally, so it never takes the percent, on both
// settings: the skeleton (RAISE UNDEAD) and the ghost (RAISE GHOST) a
// player's cleric raises on Hard and on Easy have the hit points, magic and
// damage of the same raise at 100%, bit for bit. The rule reads only the
// summon's team (0, its caster's), never its caster's character. A team-1
// cleric's skeleton takes the percent: the control. (A foe cleric's ghost
// gets its team only after it is scaled, so it is on team 0 then and keeps
// 100% in both games; that is not pinned here.)
TEST(DifficultyScaling, a_players_raised_skeleton_and_ghost_never_scale)
{
    for (const short fixes : {short{0}, short{1}})
    {
        for (const int slot : {2, 3})
        {
            Raised ref;
            ASSERT_NO_FATAL_FAILURE(raise_once(fixes, 100, 0, true, slot, &ref));
            ASSERT_EQ(0, ref.team) << "staging: the summon is on the player's team";
            ASSERT_TRUE(ref.owned_by_the_cleric) << "staging: the cleric's summon";
            ASSERT_FALSE(ref.carries_a_character) << "staging: a summon carries no character";
            ASSERT_GT(ref.body.max_hp, 0.0f) << "staging: hit points to scale";
            ASSERT_GT(ref.body.damage, 0.0f) << "staging: damage to scale";
            for (const short percent : {short{200}, short{50}})
            {
                Raised got;
                ASSERT_NO_FATAL_FAILURE(raise_once(fixes, percent, 0, true, slot, &got));
                ASSERT_EQ(0, got.team);
                ASSERT_TRUE(got.owned_by_the_cleric);
                ASSERT_FALSE(got.carries_a_character);
                EXPECT_EQ(ref.body.max_hp, got.body.max_hp)
                    << "a player's summon never takes the difficulty (#493), slot " << slot << ", "
                    << percent << "%, fixes " << fixes;
                EXPECT_EQ(ref.body.max_mp, got.body.max_mp)
                    << "a player's summon never takes the difficulty (#493), slot " << slot << ", "
                    << percent << "%, fixes " << fixes;
                EXPECT_EQ(ref.body.damage, got.body.damage)
                    << "a player's summon never takes the difficulty (#493), slot " << slot << ", "
                    << percent << "%, fixes " << fixes;
            }
        }

        Raised foe_ref;
        ASSERT_NO_FATAL_FAILURE(raise_once(fixes, 100, 1, false, 2, &foe_ref));
        ASSERT_EQ(1, foe_ref.team) << "staging: a foe's skeleton is on its caster's team";
        ASSERT_GT(foe_ref.body.max_hp, 0.0f) << "staging: hit points to scale";
        for (const short percent : {short{200}, short{50}})
        {
            const float k = static_cast<float>(percent) / 100.0f;
            Raised foe;
            ASSERT_NO_FATAL_FAILURE(raise_once(fixes, percent, 1, false, 2, &foe));
            EXPECT_FLOAT_EQ(k * foe_ref.body.max_hp, foe.body.max_hp)
                << "control: a team-1 cleric's skeleton takes " << percent << "%, fixes " << fixes;
            EXPECT_FLOAT_EQ(k * foe_ref.body.max_mp, foe.body.max_mp)
                << "control: a team-1 cleric's skeleton takes " << percent << "%, fixes " << fixes;
            EXPECT_FLOAT_EQ(k * foe_ref.body.damage, foe.body.damage)
                << "control: a team-1 cleric's skeleton takes " << percent << "%, fixes " << fixes;
        }
    }
}

TEST(DifficultyScaling, generator_spawn_gets_single_difficulty_application)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.difficulty = 100;

    walker* gen = w.add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(gen, nullptr);
    gen->set_sizex(32);
    gen->set_sizey(32);
    gen->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);
    gen->set_team_num(1);
    gen->stats()->set_level(5);
    gen->set_lastx(1.0f); // fire() reads the facing from lastx/lasty

    walker* spawn = gen->fire();
    ASSERT_NE(spawn, nullptr) << "the generator must emit a living";
    ASSERT_EQ(Order::Living, spawn->query_order());
    const std::int32_t rolled = spawn->stats()->level();
    ASSERT_GE(rolled, 1);
    ASSERT_LE(rolled, 5);

    // The intended semantics: exactly ONE set_difficulty at the rolled level.
    walker* reference = w.add_ob(Order::Living, spawn->family());
    ASSERT_NE(reference, nullptr);
    reference->set_team_num(1);
    reference->stats()->set_level(rolled);
    reference->set_difficulty(static_cast<std::uint32_t>(rolled));

    EXPECT_EQ(reference->stats()->max_hitpoints(),
              spawn->stats()->max_hitpoints())
        << "generator spawns must not compound the generator-level "
           "set_difficulty on top of the rolled-level one (A12b)";
    EXPECT_EQ(reference->stats()->max_magicpoints(),
              spawn->stats()->max_magicpoints());
    EXPECT_EQ(reference->stats()->heal_per_round(),
              spawn->stats()->heal_per_round())
        << "regen must not accumulate across double applications";
    EXPECT_EQ(reference->damage(), spawn->damage());
}

/* A12c: the Order::Generator arm of walker::set_difficulty wrote the fighting
 * hp and left max_hitpoints on the loader base — 0 for tower/bones/treehouse,
 * 100 for tent — so every difficulty-stamped generator ran at hp > max_hp.
 * Both fields move together now.
 */
TEST(DifficultyScaling, generator_set_difficulty_stamps_hp_and_its_denominator)
{
    constexpr int kFamilies[] = {FAMILY_TENT, FAMILY_TOWER, FAMILY_BONES,
                                 FAMILY_TREEHOUSE};
    for (const short difficulty : {short{50}, short{100}, short{200}})
    {
        for (const int family : kFamilies)
        {
            TestGameWorld tw;
            GameWorld& w = tw.world();
            w.difficulty = difficulty;

            walker* gen = w.add_ob(Order::Generator, family);
            ASSERT_NE(gen, nullptr) << "family " << family;
            gen->set_team_num(1);
            gen->stats()->set_level(3);
            gen->set_difficulty(3);

            const float expected =
                static_cast<float>((100u * 3u * static_cast<unsigned>(
                                                    difficulty)) / 100u);
            EXPECT_FLOAT_EQ(expected, gen->stats()->hitpoints())
                << "family " << family << " at " << difficulty << "%";
            EXPECT_FLOAT_EQ(expected, gen->stats()->max_hitpoints())
                << "family " << family << " at " << difficulty
                << "%: the denominator must match the fighting hp";
        }
    }
}

// The sim consequence, not just the display one: act_generate adds 1 hp per
// successful spawn and immediately takes it back when hp exceeds max. With the
// denominator left at the loader base that undo always fired, so a damaged
// generator could never recover.
TEST(DifficultyScaling, damaged_generator_regenerates_while_it_spawns)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.difficulty = 100;

    walker* gen = w.add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_NE(gen, nullptr);
    gen->setxy(80, 80);
    gen->set_team_num(1);
    gen->stats()->set_level(5);
    gen->set_difficulty(5);
    gen->set_act_type(ACT_GENERATE);

    const float full = gen->stats()->max_hitpoints();
    ASSERT_FLOAT_EQ(500.0f, full);
    gen->stats()->set_hitpoints(full - 100.0f);

    w.rng_.state_ = 0xC0FFEE42u;
    for (int i = 0; i < 4000; ++i)
    {
        gen->act();
        gen->set_ani_type(ANI_WALK); // keep the cadence gate rolling
    }

    EXPECT_GT(gen->stats()->hitpoints(), full - 100.0f)
        << "a spawning generator heals back toward its authored hp";
    EXPECT_LE(gen->stats()->hitpoints(), full)
        << "and never past it";
}

namespace {

// A level-3 archer on `team`, scaled as a placed walker is (set_difficulty at
// the world's current percent), facing right; `hero` gives it a player
// character.
walker* placed_archer(GameWorld& w, unsigned char team, bool hero = false)
{
    walker* a = w.add_ob(Order::Living, FAMILY_ARCHER);
    if (a == nullptr)
        return nullptr;
    a->set_team_num(team);
    if (hero)
        a->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHER));
    a->stats()->set_level(3);
    a->set_difficulty(3);
    a->set_lastx(1.0f);
    a->set_lasty(0.0f);
    return a;
}

struct Shot
{
    float damage = -1.0f;
    float max_hp = -1.0f;
};

// The arrow `shooter` fires (walker::create_weapon), as play makes it.
Shot arrow_of(walker* shooter)
{
    walker* arrow = shooter != nullptr ? shooter->create_weapon() : nullptr;
    if (arrow == nullptr)
        return {};
    EXPECT_EQ(shooter, arrow->owner()) << "staging: the arrow is the shooter's";
    EXPECT_EQ(shooter->team_num(), arrow->team_num()) << "staging: on the shooter's team";
    return {arrow->damage(), arrow->stats()->max_hitpoints()};
}

}  // namespace

// #493: a team-0 NPC's arrow never takes the difficulty percent, on both
// settings: on Hard and on Easy its damage and hit points are the same NPC's
// arrow at 100%, bit for bit. A team-1 archer's arrow takes the percent in
// the same world: the control.
TEST(DifficultyScaling, a_team0_npcs_arrow_never_scales)
{
    for (const short fixes : {short{0}, short{1}})
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.gameplay_fixes = fixes;
        w.difficulty = 100;
        const Shot ref = arrow_of(placed_archer(w, 0));
        const Shot foe_ref = arrow_of(placed_archer(w, 1));
        ASSERT_GT(ref.damage, 0.0f) << "staging: the arrow hurts";
        ASSERT_GT(ref.max_hp, 0.0f) << "staging: the arrow has hit points to scale";
        for (const short percent : {short{200}, short{50}})
        {
            w.difficulty = percent;
            const float k = static_cast<float>(percent) / 100.0f;
            const Shot npc = arrow_of(placed_archer(w, 0));
            const Shot foe = arrow_of(placed_archer(w, 1));
            EXPECT_EQ(ref.damage, npc.damage)
                << "a team-0 NPC's arrow never takes the difficulty (#493), " << percent << "%, fixes " << fixes;
            EXPECT_EQ(ref.max_hp, npc.max_hp)
                << "a team-0 NPC's arrow never takes the difficulty (#493), " << percent << "%, fixes " << fixes;
            EXPECT_FLOAT_EQ(k * foe_ref.damage, foe.damage)
                << "control: a hostile archer's arrow takes " << percent << "%, fixes " << fixes;
            EXPECT_FLOAT_EQ(k * foe_ref.max_hp, foe.max_hp)
                << "control: a hostile archer's arrow takes " << percent << "%, fixes " << fixes;
        }
    }
}

// #493: a player character's shot never takes the percent: a team-0 archer
// carrying a character fires on Hard and on Easy the arrow it fires at 100%.
TEST(DifficultyScaling, a_player_characters_arrow_never_scales)
{
    for (const short fixes : {short{0}, short{1}})
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.gameplay_fixes = fixes;
        w.difficulty = 100;
        const Shot ref = arrow_of(placed_archer(w, 0, true));
        ASSERT_GT(ref.damage, 0.0f) << "staging: the arrow hurts";
        for (const short percent : {short{200}, short{50}})
        {
            w.difficulty = percent;
            const Shot hero = arrow_of(placed_archer(w, 0, true));
            EXPECT_EQ(ref.damage, hero.damage) << percent << "%, fixes " << fixes;
            EXPECT_EQ(ref.max_hp, hero.max_hp) << percent << "%, fixes " << fixes;
        }
    }
}

// #493: no team-0 object scales, owned or not: a placed weapon with no owner
// keeps its values on Hard, and so does one a team-0 generator owns.
TEST(DifficultyScaling, an_unowned_team0_weapon_never_scales)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.difficulty = 200;
    walker* arrow = w.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, arrow);
    arrow->set_team_num(0);
    ASSERT_EQ(nullptr, arrow->owner());
    arrow->set_damage(7.0f);
    arrow->stats()->set_max_hitpoints(30.0f);
    arrow->set_difficulty(3);
    EXPECT_EQ(7.0f, arrow->damage()) << "unowned, 200%: unscaled";
    EXPECT_EQ(30.0f, arrow->stats()->max_hitpoints()) << "unowned, 200%: unscaled";

    walker* tent = w.add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(0);
    arrow->set_owner(tent);
    arrow->set_difficulty(3);
    EXPECT_EQ(7.0f, arrow->damage()) << "owned by a team-0 generator: unscaled";
}

// Team 0 never takes the multiply, at 100% too, which a float does not always
// give back: a team-0 NPC's shot whose damage does not survive
// (x * 100) / 100 keeps it bit for bit; the same shot off team 0 takes the
// legacy (x * 100) / 100.
TEST(DifficultyScaling, at_one_hundred_percent_a_team0_npcs_arrow_is_never_multiplied)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.difficulty = 100;
    walker* npc = placed_archer(w, 0);
    ASSERT_NE(nullptr, npc);
    walker* arrow = npc->create_weapon();
    ASSERT_NE(nullptr, arrow);
    float x = 0.1f;
    int tries = 0;
    while ((x * 100.0f) / 100.0f == x && tries < 100000)
    {
        x = std::nextafter(x, 1.0f);
        ++tries;
    }
    ASSERT_NE((x * 100.0f) / 100.0f, x) << "staging: a damage a multiply by one does not give back";

    arrow->set_damage(x);
    arrow->set_difficulty(3);
    EXPECT_EQ(x, arrow->damage()) << "team 0, 100%: no multiply";

    arrow->set_team_num(1);
    arrow->set_damage(x);
    arrow->set_difficulty(3);
    EXPECT_EQ((x * 100.0f) / 100.0f, arrow->damage()) << "team 1, 100%: the legacy multiply";
}
