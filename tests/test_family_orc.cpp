#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/core/constants.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include <gtest/gtest.h>
#include "test_gameplay_context_scope.h"
#include "test_family_hook_dispatch.h"

const FamilyDescriptor& describe_family_orc();
const FamilyDescriptor& describe_family_big_orc();

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
    const FamilyDescriptor& big_orc = describe_family_big_orc();
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

    const FamilyDescriptor& orc = describe_family_orc();
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

TEST(FamilyOrc, r15_special_howl_and_eat_paths)
{
    const FamilyDescriptor& orc = describe_family_orc();
    {
        OrcR15Fixture fx;
        living* self = add_living(fx, 1, FAMILY_ORC, 96, 96);
        living* foe = add_living(fx, 0, FAMILY_SOLDIER, 112, 96);
        ASSERT_TRUE(self && foe);

        foe->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        foe->myguy->constitution = 18;

        self->stats()->set_level(6);
        self->set_busy(0);
        self->set_current_special(1); // howl/freeze
        ASSERT_TRUE(og::test::do_special(orc, self));
        ASSERT_TRUE(self->busy() > 0);
        ASSERT_TRUE(foe->stats()->frozen_delay() >= 0);
        ASSERT_TRUE(fx.events.size() > 0);
    }

    {
        OrcR15Fixture fx;
        living* self = add_living(fx, 1, FAMILY_ORC, 96, 96);
        ASSERT_TRUE(self != nullptr);
        self->set_current_special(2); // eat corpse
        self->stats()->set_max_hitpoints(200.0f);
        self->stats()->set_hitpoints(30.0f);
        self->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
        self->myguy->name = "R15 ORC";
        cfg.apply_setting("effects", "heal_numbers", "on");

        walker* stain = add_stain(fx, 96, 96, 0, FAMILY_SOLDIER, 4);
        ASSERT_TRUE(stain != nullptr);
        ASSERT_TRUE(og::test::do_special(orc, self));
        ASSERT_TRUE(stain->dead() == 1);
        ASSERT_TRUE(self->stats()->hitpoints() <= self->stats()->max_hitpoints());
    }
}

TEST(FamilyOrc, r15_check_ai_and_guard_failures)
{
    const FamilyDescriptor& orc = describe_family_orc();
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
