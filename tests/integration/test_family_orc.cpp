#include <openglad/gameplay/families/family_descriptor.h>

#include "test_family_lookup.h"
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/interface/game_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/core/irandom.h>
#include <openglad/core/constants.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include <string>
#include <gtest/gtest.h>
#include "test_gameplay_context_scope.h"
#include "test_family_hook_dispatch.h"


namespace {

struct OrcR15Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;
    GameContext gc;

    OrcR15Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
    }

    ~OrcR15Fixture()
    {
        pop_test_context();
    }
};

living* add_living(OrcR15Fixture& fx, unsigned char team, char family, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
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

walker* add_stain(OrcR15Fixture& fx, short x, short y, unsigned char team, char old_family, std::int32_t level)
{
    walker* stain = fx.level.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    stain->set_team_num(team);
    stain->setxy(x, y);
    stain->set_dead(0);
    stain->stats()->set_old_family(old_family);
    stain->stats()->set_level(level);
    return stain;
}

} // namespace

TEST(FamilyOrc, family_big_orc_r15_level_up_and_orc_descriptor_hooks)
{
    const FamilyDescriptor& big_orc = describe_family(FAMILY_BIG_ORC);
    ASSERT_TRUE(big_orc.family_id == FAMILY_BIG_ORC);
    ASSERT_TRUE(og::test::has_level_up(big_orc));

    guy captain(FAMILY_BIG_ORC);
    const short old_str = captain.strength;
    const short old_dex = captain.dexterity;
    const short old_con = captain.constitution;
    const short old_int = captain.intelligence;
    const short old_arm = captain.armor;
    og::test::level_up(big_orc, &captain, 2);
    ASSERT_TRUE(captain.strength > old_str);
    ASSERT_TRUE(captain.dexterity > old_dex);
    ASSERT_TRUE(captain.constitution > old_con);
    ASSERT_TRUE(captain.intelligence > old_int);
    ASSERT_TRUE(captain.armor > old_arm);

    const FamilyDescriptor& orc = describe_family(FAMILY_ORC);
    ASSERT_TRUE(orc.family_id == FAMILY_ORC);
    ASSERT_TRUE(og::test::has_do_special(orc));
    ASSERT_TRUE(og::test::has_check_special_ai(orc));
    ASSERT_TRUE(og::test::has_set_difficulty(orc));
    ASSERT_TRUE(og::test::has_level_up(orc));
    ASSERT_TRUE(orc.promotion_new_level != nullptr);

    living w;
    w.set_damage(0.0f);
    const float old_hp = w.stats()->max_hitpoints();
    const float old_mp = w.stats()->max_magicpoints();
    const float old_damage = w.damage();
    const float old_armor = w.stats()->armor();
    og::test::set_difficulty(orc, &w, 2);
    ASSERT_TRUE(w.stats()->max_hitpoints() > old_hp);
    ASSERT_TRUE(w.stats()->max_magicpoints() > old_mp);
    ASSERT_TRUE(w.damage() >= old_damage);
    ASSERT_TRUE(w.stats()->armor() >= old_armor);

    guy grunt(FAMILY_ORC);
    const short grunt_old_str = grunt.strength;
    og::test::level_up(orc, &grunt, 1);
    ASSERT_TRUE(grunt.strength > grunt_old_str);
    ASSERT_TRUE(orc.promotion_new_level(5) == 1);
}

namespace {

int count_sound_events(const og::sim::SimEventLog& log, std::uint32_t sound_id)
{
    int n = 0;
    for (const auto& ev : log.events()) {
        if (ev.kind == og::sim::EventKind::PlaySound && ev.a == sound_id)
            n++;
    }
    return n;
}

int count_notifications(const og::sim::SimEventLog& log, const char* needle)
{
    int n = 0;
    for (const auto& ev : log.events()) {
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
            n++;
    }
    return n;
}

} // namespace

// The howl freezes every FOE in radius and nobody else, and the freeze it
// applies is the tuning table's yell_stun_base.
//
// A level-0 orc against a constitution-0 foe takes BOTH og.rand0 calls in
// living-14-orc.lua:36-38 down the n <= 0 shortcut (no draw at all), so
// stun = max(0, 10 + 0 - 0) = 10 exactly, with no dependence on the world
// RNG's state. That is what makes an exact pin possible here.
TEST(FamilyOrc, r15_howl_freezes_foes_in_radius_and_spares_the_horde)
{
    const FamilyDescriptor& orc = describe_family(FAMILY_ORC);
    OrcR15Fixture fx;
    living* self = add_living(fx, 1, FAMILY_ORC, 96, 96);
    living* foe = add_living(fx, 0, FAMILY_SOLDIER, 112, 96);
    living* packmate = add_living(fx, 1, FAMILY_ORC, 80, 96);
    ASSERT_TRUE(self && foe && packmate);

    foe->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    foe->myguy->constitution = 0;
    packmate->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
    packmate->myguy->constitution = 0;

    self->stats()->set_level(0);
    self->set_busy(0);
    self->set_current_special(1); // howl/freeze
    ASSERT_EQ(0, static_cast<int>(foe->stats()->frozen_delay()))
        << "the foe starts unfrozen";

    ASSERT_TRUE(og::test::do_special(orc, self)) << "the howl must go off";

    EXPECT_EQ(10, static_cast<int>(foe->stats()->frozen_delay()))
        << "yell_stun_base 10 with both rolls at 0 freezes the foe for "
           "exactly 10 ticks";
    EXPECT_EQ(0, static_cast<int>(packmate->stats()->frozen_delay()))
        << "a howl never freezes the orc's own team";
    EXPECT_FLOAT_EQ(2.0f, self->busy())
        << "howling costs the orc exactly 2 busy ticks";
    EXPECT_EQ(1, count_sound_events(fx.events, SOUND_ROAR))
        << "the howl is heard once";
}

// Eating a corpse converts the corpse's LEVEL into hitpoints at the tuning
// table's corpse_heal_per_level, consumes the stain, and pays experience.
TEST(FamilyOrc, r15_eating_a_corpse_heals_by_its_level_and_consumes_it)
{
    const FamilyDescriptor& orc = describe_family(FAMILY_ORC);
    OrcR15Fixture fx;
    living* self = add_living(fx, 1, FAMILY_ORC, 96, 96);
    ASSERT_TRUE(self != nullptr);
    self->set_current_special(2); // eat corpse
    self->stats()->set_max_hitpoints(200.0f);
    self->stats()->set_hitpoints(30.0f);
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
    self->myguy->name = "R15 ORC";
    self->myguy->exp = 0;
    cfg.apply_setting("effects", "heal_numbers", "on");

    walker* stain = add_stain(fx, 96, 96, 0, FAMILY_SOLDIER, 4);
    ASSERT_TRUE(stain != nullptr);
    ASSERT_TRUE(og::test::do_special(orc, self)) << "the corpse is in reach";

    EXPECT_EQ(1, stain->dead()) << "the corpse is consumed";
    EXPECT_FLOAT_EQ(50.0f, self->stats()->hitpoints())
        << "30 hp + corpse level 4 * corpse_heal_per_level 5 == 50, well "
           "under the 200 max so nothing is clamped away";
    EXPECT_EQ(20u, self->myguy->exp)
        << "eating pays corpse level * 5 experience";
    EXPECT_EQ(1, count_notifications(fx.events, "ate a corpse"))
        << "the meal is announced once";
}

TEST(FamilyOrc, r15_check_ai_and_guard_failures)
{
    const FamilyDescriptor& orc = describe_family(FAMILY_ORC);
    OrcR15Fixture fx;

    living* self = add_living(fx, 1, FAMILY_ORC, 50, 50);
    ASSERT_TRUE(self != nullptr);

    self->set_foe(nullptr);
    ASSERT_TRUE(!og::test::check_special_ai(orc, self));

    living* near_foe = add_living(fx, 0, FAMILY_SOLDIER, 60, 50);
    ASSERT_TRUE(near_foe != nullptr);
    ASSERT_TRUE(og::test::check_special_ai(orc, self));

    near_foe->setxy(800, 800);
    self->set_foe(near_foe);
    ASSERT_TRUE(!og::test::check_special_ai(orc, self));

    self->set_current_special(1);
    self->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(orc, self));

    self->set_current_special(2);
    self->set_busy(0);
    self->stats()->set_hitpoints(self->stats()->max_hitpoints());
    ASSERT_TRUE(!og::test::do_special(orc, self));
}
