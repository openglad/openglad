#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/core/constants.h>
#include <gtest/gtest.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include "test_gameplay_context_scope.h"

// --- From test_family_thief_coverage_push.cpp ---
const FamilyDescriptor& describe_family_thief();

namespace detail_family_thief_coverage_push {

TEST(FamilyThief, descriptor_shape_and_level_up)
{
    const FamilyDescriptor& desc = describe_family_thief();
    ASSERT_TRUE(desc.family_id == FAMILY_THIEF);
    ASSERT_TRUE(desc.do_special != nullptr);
    ASSERT_TRUE(desc.check_special_ai != nullptr);
    ASSERT_TRUE(desc.level_up != nullptr);

    guy g(FAMILY_THIEF);
    const short old_str = g.strength;
    const short old_dex = g.dexterity;
    const short old_con = g.constitution;
    const short old_int = g.intelligence;
    const short old_armor = g.armor;

    desc.level_up(&g, 2);
    ASSERT_TRUE(g.strength > old_str);
    ASSERT_TRUE(g.dexterity > old_dex);
    ASSERT_TRUE(g.constitution > old_con);
    ASSERT_TRUE(g.intelligence > old_int);
    ASSERT_TRUE(g.armor > old_armor);
}

TEST(FamilyThief, check_special_ai_foe_distance_paths)
{
    const FamilyDescriptor& desc = describe_family_thief();
    living self;
    living foe;
    self.set_current_special(1);
    self.set_foe(&foe);

    self.setxy(0, 0);
    foe.setxy(60, 0); // >35 and <130 => false
    ASSERT_TRUE(!desc.check_special_ai(&self));

    foe.setxy(10, 0); // <=35 => true
    ASSERT_TRUE(desc.check_special_ai(&self));

    self.set_current_special(2); // default branch => true
    ASSERT_TRUE(desc.check_special_ai(&self));
}

TEST(FamilyThief, do_special_busy_and_cloak_paths)
{
    const FamilyDescriptor& desc = describe_family_thief();
    living self;
    FixedRandom rng(7);
    self.stats()->set_level(3);

    self.set_current_special(2); // cloak
    self.set_invisibility_left(0);
    ASSERT_TRUE(desc.do_special(&self));
    ASSERT_TRUE(self.invisibility_left() > 0);

    self.set_current_special(3); // taunt/charm
    self.set_busy(1);
    ASSERT_TRUE(!desc.do_special(&self));

    self.set_current_special(4); // poison cloud/default
    self.set_busy(1);
    ASSERT_TRUE(!desc.do_special(&self));
}
} // namespace detail_family_thief_coverage_push

// --- From test_family_thief_r12.cpp ---
namespace detail_family_thief_r12 {
namespace {

struct ThiefR12Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    ThiefR12Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(ThiefR12Fixture& fx, unsigned char team, char family = FAMILY_THIEF)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(80, 80);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(FamilyThief, r12_check_ai_and_special_paths)
{
    const FamilyDescriptor& desc = describe_family_thief();
    ThiefR12Fixture fx;

    living* thief = add_living(fx, 0, FAMILY_THIEF);
    living* foe1 = add_living(fx, 1, FAMILY_ORC);
    living* foe2 = add_living(fx, 1, FAMILY_ORC);
    living* foe3 = add_living(fx, 1, FAMILY_ORC);
    ASSERT_TRUE(thief && foe1 && foe2 && foe3);

    thief->set_current_special(1);
    thief->set_foe(nullptr);
    foe1->setxy(200, 200);
    foe2->setxy(210, 200);
    foe3->setxy(220, 200);
    ASSERT_TRUE(!desc.check_special_ai(thief));

    foe1->setxy(90, 80);
    foe2->setxy(94, 80);
    foe3->setxy(98, 80);
    ASSERT_TRUE(desc.check_special_ai(thief));

    thief->set_current_special(3);
    thief->set_shifter_down(0);
    ASSERT_TRUE(desc.check_special_ai(thief));
    thief->set_shifter_down(1);
    ASSERT_TRUE(desc.check_special_ai(thief));

    thief->set_current_special(1);
    thief->set_user(-1);
    thief->stats()->set_level(4);
    ASSERT_TRUE(desc.do_special(thief));

    thief->set_current_special(3);
    thief->set_shifter_down(0);
    thief->set_busy(0);
    thief->stats()->name = "Bandit";
    ASSERT_TRUE(desc.do_special(thief));

    thief->set_current_special(3);
    thief->set_shifter_down(1);
    thief->set_busy(0);
    thief->set_foe(foe1);
    foe1->set_real_team_num(255);
    ASSERT_TRUE(desc.do_special(thief));

    thief->set_current_special(4);
    thief->set_busy(0);
    ASSERT_TRUE(desc.do_special(thief));
}
} // namespace detail_family_thief_r12
