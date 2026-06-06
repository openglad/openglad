#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/core/constants.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include <gtest/gtest.h>
#include "test_gameplay_context_scope.h"

namespace {

struct SpecialsFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    SpecialsFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(SpecialsFixture& fx, char family, unsigned char team)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(96, 96);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_marker(SpecialsFixture& fx, walker* owner, int x, int y, int life)
{
    walker* m = fx.level.add_ob(Order::FX, FAMILY_MARKER);
    m->set_owner(owner);
    m->set_dead(0);
    m->setxy(x, y);
    m->set_lifetime(life);
    return m;
}

} // namespace

TEST(WalkerSpecialsUnit, walker_specials_r11_special_and_teleport_paths)
{
    SpecialsFixture fx;
    living* w = add_living(fx, FAMILY_CLERIC, 0);
    ASSERT_TRUE(w != nullptr);

    w->set_dead(1);
    ASSERT_TRUE(!w->special());
    w->set_dead(0);

    walker* weapon = fx.level.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(weapon != nullptr);
    if (weapon) {
        weapon->set_dead(0);
        ASSERT_TRUE(!weapon->special());
    }

    // marker teleport success with marker expiry (lines 86-90)
    w->setxy(20, 20);
    walker* marker = add_marker(fx, w, 140, 140, 1);
    ASSERT_TRUE(marker != nullptr);
    ASSERT_TRUE(w->teleport());
    ASSERT_TRUE(marker->dead() == 1);

    // no marker path: random passable placement
    ASSERT_TRUE(w->teleport());

    // ranged teleport success path
    (void)w->teleport_ranged(40);
}

TEST(WalkerSpecialsUnit, walker_specials_r11_turn_undead_paths)
{
    SpecialsFixture fx;
    living* cleric = add_living(fx, FAMILY_CLERIC, 0);
    ASSERT_TRUE(cleric != nullptr);

    // No targets branch -> -1
    ASSERT_TRUE(cleric->turn_undead(40, 5) == -1);

    // Undead target in range triggers kill path.
    living* skeleton = add_living(fx, FAMILY_SKELETON, 1);
    skeleton->setxy(100, 96);
    skeleton->stats()->set_level(1);
    const std::int32_t killed = cleric->turn_undead(40, 5);
    ASSERT_TRUE(killed >= 0);
}
