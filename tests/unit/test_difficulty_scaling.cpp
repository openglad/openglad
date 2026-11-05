/* A12 regression tests: difficulty scaling of placed NPCs and generator
 * spawns.
 *
 * A12a: the difficulty multiplier (50/100/200%) was gated `team_num() != 0`
 * ("do all EXCEPT player characters") — but player crews never pass through
 * set_difficulty at all (guy::update_derived_stats owns their stats), so the
 * gate silently froze placed team-0 NPCs (the SAVE_ALL Bearer, allied
 * war-hosts) at 100% while their foes doubled on Hard. Now only walkers
 * actually carrying a player guy (myguy) are exempt. The team-0 multiply is
 * skipped entirely at 100% so parity goldens stay byte-identical. Their shots
 * follow them (#473): a team-0 shot whose owner is a Living with no player
 * character takes the same percent, on both settings, so an allied archer's
 * arrows double on Hard with its body as every hostile arrow does.
 *
 * A12b: generator spawns received set_difficulty TWICE (create_weapon at the
 * generator's level, then fire()'s generator branch at the rolled level) —
 * legacy-original 2002 behavior that compounded two additive hp/regen boosts
 * and a squared difficulty multiplier. Now only the fire()-branch rolled-level
 * application remains: a spawn's stats equal a fresh living of the same
 * family given ONE set_difficulty at the rolled level.
 */
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

TEST(DifficultyScaling, placed_team0_npcs_scale_with_difficulty)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    // Reference: placed team-0 NPC at 100% (the parity/default setting).
    w.difficulty = 100;
    walker* ref = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(ref, nullptr);
    ref->set_team_num(0);
    ref->set_difficulty(3);
    const float ref_hp = ref->stats()->max_hitpoints();
    const float ref_dmg = ref->damage();
    ASSERT_GT(ref_hp, 0.0f);

    // Same NPC on Hard (200%): doubled hp and damage, like any foe.
    w.difficulty = 200;
    walker* hard = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(hard, nullptr);
    hard->set_team_num(0);
    hard->set_difficulty(3);
    EXPECT_FLOAT_EQ(2.0f * ref_hp, hard->stats()->max_hitpoints())
        << "a placed team-0 NPC must scale with difficulty (A12a)";
    EXPECT_FLOAT_EQ(2.0f * ref_dmg, hard->damage());

    // ...and on Easy (50%): halved.
    w.difficulty = 50;
    walker* easy = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(easy, nullptr);
    easy->set_team_num(0);
    easy->set_difficulty(3);
    EXPECT_FLOAT_EQ(0.5f * ref_hp, easy->stats()->max_hitpoints());

    // A second 100% NPC must be BIT-identical to the reference: the team-0
    // multiply is skipped (not multiplied by 1) at 100%, the parity pin.
    w.difficulty = 100;
    walker* base = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(base, nullptr);
    base->set_team_num(0);
    base->set_difficulty(3);
    EXPECT_EQ(ref_hp, base->stats()->max_hitpoints());
    EXPECT_EQ(ref_dmg, base->damage());
}

TEST(DifficultyScaling, player_characters_stay_exempt_from_difficulty)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    w.difficulty = 100;
    walker* ref = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(ref, nullptr);
    ref->set_team_num(0);
    ref->set_difficulty(3);
    const float ref_hp = ref->stats()->max_hitpoints();

    // A team-0 walker CARRYING a player guy is a player character: exempt.
    w.difficulty = 200;
    walker* player = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(player, nullptr);
    player->set_team_num(0);
    player->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    player->set_difficulty(3);
    EXPECT_EQ(ref_hp, player->stats()->max_hitpoints())
        << "player characters must never scale with the difficulty setting";
}

TEST(DifficultyScaling, hostile_team_legacy_scaling_unchanged)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    w.difficulty = 100;
    walker* foe100 = w.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(foe100, nullptr);
    foe100->set_team_num(1);
    foe100->set_difficulty(3);
    const float foe100_hp = foe100->stats()->max_hitpoints();

    w.difficulty = 200;
    walker* foe200 = w.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(foe200, nullptr);
    foe200->set_team_num(1);
    foe200->set_difficulty(3);
    EXPECT_FLOAT_EQ(2.0f * foe100_hp, foe200->stats()->max_hitpoints())
        << "hostile-team scaling is the legacy path and must not change";
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

// #473: a team-0 NPC's arrow takes the difficulty percent with its body (A12a),
// on both settings: on Hard its damage and hit points are twice the same
// NPC's arrow at 100% and exactly a hostile archer's arrow; on Easy, half.
TEST(DifficultyScaling, a_team0_npcs_arrow_scales_with_its_body)
{
    for (const short fixes : {short{0}, short{1}})
    {
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.gameplay_fixes = fixes;
        w.difficulty = 100;
        const Shot ref = arrow_of(placed_archer(w, 0));
        ASSERT_GT(ref.damage, 0.0f) << "staging: the arrow hurts";
        ASSERT_GT(ref.max_hp, 0.0f) << "staging: the arrow has hit points to scale";
        for (const short percent : {short{200}, short{50}})
        {
            w.difficulty = percent;
            const float k = static_cast<float>(percent) / 100.0f;
            const Shot npc = arrow_of(placed_archer(w, 0));
            const Shot foe = arrow_of(placed_archer(w, 1));
            EXPECT_FLOAT_EQ(k * ref.damage, npc.damage)
                << "team-0 NPC's arrow at " << percent << "%, fixes " << fixes;
            EXPECT_FLOAT_EQ(k * ref.max_hp, npc.max_hp)
                << "team-0 NPC's arrow at " << percent << "%, fixes " << fixes;
            EXPECT_EQ(foe.damage, npc.damage)
                << "the same as a hostile archer's arrow at " << percent << "%, fixes " << fixes;
            EXPECT_EQ(foe.max_hp, npc.max_hp)
                << "the same as a hostile archer's arrow at " << percent << "%, fixes " << fixes;
        }
    }
}

// #473: a player character's shot never takes the percent: a team-0 archer
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

// #473: a team-0 object with no owner (a placed weapon, treasure or effect)
// keeps its values on Hard: only a shot whose shooter is a Living follows it.
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
    EXPECT_EQ(7.0f, arrow->damage()) << "owned by a generator, not a Living: unscaled";
}

// #473 and A12a at 100%: the team-0 arms skip the multiply rather than
// multiply by one, which a float does not always give back (the parity pin).
// A team-0 NPC's shot whose damage does not survive (x * 100) / 100 keeps it
// bit for bit; the same shot off team 0 takes the legacy multiply.
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
