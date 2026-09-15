#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"
#include <cstdint>
#include <memory>

#include "test_gameplay_context_scope.h"

// myscreen is now a macro defined in base.h (via game_session.h)

bool walkerIsAutoAttackable(walker* ob);

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w) return nullptr;
    w->setxy(50, 50);
    return w;
}

namespace
{
// The families every family-sweep case in this file walks.
constexpr char kLivingFamilies[] = {
    FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
    FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
    FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
    FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN,
};

// living::shove and living::walk read the GRID and the obmap of
// current_game->world, and the ambient integration world is shared with every
// other case in the binary (its scen1 terrain, its leftover walkers). Give the
// terrain-sensitive cases their own world instead: create_new_grid() lays down
// a 40x60 all-grass (all-passable) plane and the obmap starts empty, so a
// passable/blocked verdict is a property of the code under test and not of
// whatever ran before under --gtest_shuffle.
struct ControlledWorld
{
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    ControlledWorld()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &events, &cfg);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// walkerIsAutoAttackable tests
// ---------------------------------------------------------------------------

TEST(LivingFuncs, walkerIsAutoAttackable_soldier)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    bool result = walkerIsAutoAttackable(w.get());
    ASSERT_TRUE(result) << "soldier should be auto-attackable";

}


// walkerIsAutoAttackable (living.cpp:549) is an ORDER rule, not a family rule:
// every Order::Living and Order::Generator walker is auto-attackable whatever
// its family, Order::Treasure never is, and Order::Weapon defers to the weapon
// family's declared is_auto_attackable flag.
TEST(LivingFuncs, walkerIsAutoAttackable_true_for_every_living_family_and_order_discriminators)
{
    for (const char family : kLivingFamilies) {
        auto w = create_living(family);
        ASSERT_NE(nullptr, w.get()) << "create_walker failed for living family "
                                    << static_cast<int>(family);
        EXPECT_TRUE(walkerIsAutoAttackable(w.get()))
            << "Order::Living is auto-attackable regardless of family "
            << static_cast<int>(family);
    }

    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_NE(nullptr, l) << "the integration session must carry a loader";

    auto gen = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, gen.get()) << "generator walker should be created";
    EXPECT_TRUE(walkerIsAutoAttackable(gen.get()))
        << "Order::Generator is auto-attackable";

    auto loot = l->create_walker_owned(Order::Treasure, FAMILY_DRUMSTICK);
    ASSERT_NE(nullptr, loot.get()) << "treasure walker should be created";
    EXPECT_FALSE(walkerIsAutoAttackable(loot.get()))
        << "Order::Treasure is never auto-attackable";

    auto door = l->create_walker_owned(Order::Weapon, FAMILY_DOOR);
    ASSERT_NE(nullptr, door.get()) << "door weapon walker should be created";
    EXPECT_TRUE(walkerIsAutoAttackable(door.get()))
        << "weapon-18-door.lua declares is_auto_attackable = true";

    auto knife = l->create_walker_owned(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife.get()) << "knife weapon walker should be created";
    EXPECT_FALSE(walkerIsAutoAttackable(knife.get()))
        << "weapon-00-knife.lua declares is_auto_attackable = false";
}


// ---------------------------------------------------------------------------
// set_difficulty tests
// ---------------------------------------------------------------------------

// The soldier's set_difficulty hook is
// og.apply_difficulty_scaling(self, level, 13, 8, 5, 2)
// (packs/core/families/living-00-soldier.lua:133) -> hp/mp/armor scale with
// level^2 and damage with level (guy.cpp apply_difficulty_scaling); the hook
// then re-derives weapons_left = (level+1)/2. living::set_difficulty finally
// equalises the current pools to the new maxima.
// team 0 + a myguy exempts the walker from the query_difficulty_percent()
// rescale, so the pins hold at any ambient difficulty setting.
TEST(LivingFuncs, living_set_difficulty_soldier_applies_lua_coefficients_exactly)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    w->set_team_num(0);
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));

    const float before_hp = w->stats()->max_hitpoints();
    const float before_mp = w->stats()->max_magicpoints();
    const float before_dmg = w->damage();
    const float before_armor = w->stats()->armor();

    static_cast<living*>(w.get())->set_difficulty(5);

    EXPECT_FLOAT_EQ(before_hp + 13.0f * 25.0f, w->stats()->max_hitpoints())
        << "soldier hp coefficient 13 * level^2";
    EXPECT_FLOAT_EQ(before_mp + 8.0f * 25.0f, w->stats()->max_magicpoints())
        << "soldier mp coefficient 8 * level^2";
    EXPECT_FLOAT_EQ(before_dmg + 5.0f * 5.0f, w->damage())
        << "soldier damage coefficient 5 * level";
    EXPECT_FLOAT_EQ(before_armor + 2.0f * 25.0f, w->stats()->armor())
        << "soldier armor coefficient 2 * level^2";
    EXPECT_FLOAT_EQ(w->stats()->max_hitpoints(), w->stats()->hitpoints())
        << "set_difficulty tops the walker up to its new max hp";
    EXPECT_FLOAT_EQ(w->stats()->max_magicpoints(), w->stats()->magicpoints())
        << "set_difficulty tops the walker up to its new max mp";
    EXPECT_EQ(3, static_cast<int>(w->weapons_left()))
        << "the soldier hook re-derives weapons_left = (5 + 1) / 2";
}


TEST(LivingFuncs, living_set_difficulty_level_10)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    float hp_low, hp_high;
    static_cast<living*>(w.get())->set_difficulty(1);
    hp_low = w->stats()->max_hitpoints();

    auto w2 = create_living(FAMILY_SOLDIER);
    static_cast<living*>(w2.get())->set_difficulty(10);
    hp_high = w2->stats()->max_hitpoints();

    ASSERT_TRUE(hp_high > hp_low) << "higher difficulty level should give more HP";

}


// Every family STRICTLY raises max hp in set_difficulty (seven families carry
// a Lua hook with an hp coefficient of 7..18, the rest take living.cpp's
// default + 11 * level^2) and every family ends with hitpoints == max.
// FAMILY_GHOST ships no set_difficulty hook, so it anchors the default branch
// exactly the way the soldier case anchors the hook branch.
TEST(LivingFuncs, living_set_difficulty_raises_max_hp_for_every_family_and_tops_up)
{
    for (const char family : kLivingFamilies) {
        auto w = create_living(family);
        ASSERT_NE(nullptr, w.get()) << "create_walker failed for family "
                                    << static_cast<int>(family);
        w->set_team_num(0);
        w->set_owned_myguy(std::make_unique<guy>(family));

        const float before = w->stats()->max_hitpoints();
        static_cast<living*>(w.get())->set_difficulty(3);

        EXPECT_GT(w->stats()->max_hitpoints(), before)
            << "set_difficulty must raise max hp for family "
            << static_cast<int>(family);
        EXPECT_FLOAT_EQ(w->stats()->max_hitpoints(), w->stats()->hitpoints())
            << "set_difficulty tops hp up to max for family "
            << static_cast<int>(family);
        if (family == FAMILY_GHOST) {
            EXPECT_FLOAT_EQ(before + 11.0f * 9.0f, w->stats()->max_hitpoints())
                << "a hook-less family takes living.cpp's default "
                   "+ 11 * level^2";
        }
    }
}


// ---------------------------------------------------------------------------
// check_special smoke tests
// ---------------------------------------------------------------------------

// living::check_special (living.cpp:590) has two gates in front of the family
// hook: specials_disabled() returns false immediately (before anything else is
// touched), and a current_special the walker cannot afford is downgraded to the
// default special 1. The hook's own verdict is deliberately NOT pinned here:
// both families' check_special_ai read the shared world's foe population.
// (The former living_check_special_mage case, which only called through and
// discarded the result, folds into this table's FAMILY_MAGE row.)
TEST(LivingFuncs, living_check_special_gates_disabled_flag_then_downgrades_unaffordable)
{
    const char families[] = { FAMILY_SOLDIER, FAMILY_MAGE };
    for (const char family : families) {
        auto w = create_living(family);
        ASSERT_NE(nullptr, w.get()) << "create_walker failed for family "
                                    << static_cast<int>(family);
        living* lv = static_cast<living*>(w.get());
        w->set_owned_myguy(std::make_unique<guy>(family));
        w->stats()->set_max_magicpoints(100);
        w->stats()->set_magicpoints(100);
        w->stats()->set_special_cost(4, 200);
        w->set_current_special(4);

        w->set_specials_disabled(true);
        EXPECT_FALSE(lv->check_special())
            << "a specials-disabled walker never decides to special, family "
            << static_cast<int>(family);
        EXPECT_EQ(4, static_cast<int>(w->current_special()))
            << "the disabled gate returns BEFORE the affordability downgrade, "
               "family " << static_cast<int>(family);

        w->set_specials_disabled(false);
        w->stats()->set_magicpoints(0);
        (void)lv->check_special();
        EXPECT_EQ(1, static_cast<int>(w->current_special()))
            << "an unaffordable special downgrades to the default (1), family "
            << static_cast<int>(family);
    }
}


// ---------------------------------------------------------------------------
// living::facing tests
// ---------------------------------------------------------------------------

TEST(LivingFuncs, living_facing)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    short dir = static_cast<living*>(w.get())->facing(10, 0);
    ASSERT_EQ(FACE_RIGHT, (int)dir) << "facing right";

    dir = static_cast<living*>(w.get())->facing(0, -10);
    ASSERT_EQ(FACE_UP, (int)dir) << "facing up";

    dir = static_cast<living*>(w.get())->facing(-10, 10);
    ASSERT_EQ(FACE_DOWN_LEFT, (int)dir) << "facing down-left";

}


// ---------------------------------------------------------------------------
// shove test
// ---------------------------------------------------------------------------

// living::shove (living.cpp:434) probes the injected walk's baby step against
// the GRID before stealing the target's command queue: a passable step returns
// 1 and force-queues COMMAND_WALK(4, x, y) at the head of the (cleared) queue,
// a blocked step returns 0 and leaves the queue alone so the target's own
// search can re-path (the 2026-07-10 shove-livelock fix).
TEST(LivingFuncs, living_shove_force_queues_walk_on_passable_step_and_refuses_blocked_step)
{
    ControlledWorld world;
    FixedRandom never_shoved_back{1}; // next(3) == 1: we are not the shovee
    // living::shove draws current_game->world->rng_.next(3) unconditionally
    // and reads 0 as "we got shoved instead", so an unseeded draw decides the
    // verdict. GameContext::rng does NOT reach sim code; the sim guard does.
    ScopedSimRandom sim_rng{&never_shoved_back};

    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, a.get()) << "create a should succeed";
    ASSERT_NE(nullptr, b.get()) << "create b should succeed";

    a->set_team_num(0);
    b->set_team_num(0);
    b->set_act_type(ACT_GUARD); // ACT_CONTROL targets are never shoved
    a->setxy(200, 200);
    b->setxy(96, 96);
    b->stats()->clear_command();

    EXPECT_EQ(1, static_cast<int>(static_cast<living*>(a.get())->shove(b.get(), 1, 0)))
        << "an allied target whose baby step is grid-passable gets shoved";
    ASSERT_EQ(1u, b->stats()->commands.size())
        << "shove clears the queue and injects exactly one command";
    const command& injected = b->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, static_cast<int>(injected.commandtype))
        << "shove injects COMMAND_WALK";
    EXPECT_EQ(4, static_cast<int>(injected.commandcount))
        << "shove injects four walk iterations";
    EXPECT_EQ(1, static_cast<int>(injected.com1)) << "injected walk dx";
    EXPECT_EQ(0, static_cast<int>(injected.com2)) << "injected walk dy";
    EXPECT_TRUE(injected.forced)
        << "shove goes in through force_command, so the entry is forced";

    // Blocked arm: the baby step leaves the grid entirely, so shove must not
    // touch the queue.
    b->stats()->clear_command();
    b->setxy(0, 96);
    EXPECT_EQ(0, static_cast<int>(static_cast<living*>(a.get())->shove(b.get(), -1, 0)))
        << "a grid-blocked baby step denies the shove";
    EXPECT_TRUE(b->stats()->commands.empty())
        << "a denied shove must leave the target's command queue untouched";
}


// ---------------------------------------------------------------------------
// living walk smoke
// ---------------------------------------------------------------------------

// living::walk (living.cpp:477) has three arms: curdir already matches
// facing(x, y) and the destination is passable -> move by exactly (x, y) and
// return 1; curdir disagrees -> set enddir and turn WITHOUT moving, return 1;
// the destination is off the map -> return 0 and stay put.
TEST(LivingFuncs, living_walk_moves_on_matching_facing_turns_otherwise_and_refuses_off_map)
{
    ControlledWorld world;
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    living* lv = static_cast<living*>(w.get());
    w->set_act_type(ACT_GUARD);
    w->setxy(160, 160);

    // Turn arm: facing(1, 0) is FACE_RIGHT, curdir says FACE_LEFT.
    w->set_curdir(FACE_LEFT);
    EXPECT_TRUE(lv->walk(1, 0)) << "the turn arm still reports success";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(w->enddir()))
        << "the turn arm records the desired facing in enddir";
    EXPECT_FLOAT_EQ(160.0f, w->xpos())
        << "the turn arm must not move the walker";
    EXPECT_FLOAT_EQ(160.0f, w->ypos())
        << "the turn arm must not move the walker";

    // Move arm: curdir agrees and the all-grass grid is passable.
    w->set_curdir(FACE_RIGHT);
    EXPECT_TRUE(lv->walk(1, 0)) << "a matching facing onto passable ground moves";
    EXPECT_FLOAT_EQ(161.0f, w->xpos()) << "walk moves by exactly dx";
    EXPECT_FLOAT_EQ(160.0f, w->ypos()) << "walk must not move on the other axis";

    // Off-map arm.
    w->setxy(0, 160);
    w->set_curdir(FACE_LEFT);
    EXPECT_FALSE(lv->walk(-1, 0)) << "walking off the west edge fails";
    EXPECT_FLOAT_EQ(0.0f, w->xpos()) << "a refused walk leaves the walker put";
}

