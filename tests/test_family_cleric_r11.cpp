#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/core/constants.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>

#include "unit/unit.h"

const FamilyDescriptor& describe_family_cleric();

namespace {

struct ClericFixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    ClericFixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(ClericFixture& fx, unsigned char team, char family = FAMILY_CLERIC)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(80, 80);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

walker* add_stain(ClericFixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_ob(Order::Treasure, FAMILY_STAIN);
    stain->team_num = team;
    stain->setxy(x, y);
    stain->stats()->old_family = old_family;
    stain->dead = 0;
    return stain;
}

} // namespace

OG_UNIT_TEST(test_family_cleric_r11_check_ai_and_heal_fail_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);

    cleric->current_special = 1;
    cleric->stats()->max_magicpoints = 100.0f;
    cleric->stats()->magicpoints = 10.0f;
    OG_ASSERT(!desc.check_special_ai(cleric)); // line 63

    cleric->stats()->magicpoints = 60.0f;
    OG_ASSERT(desc.check_special_ai(cleric));
    OG_ASSERT(cleric->shifter_down == 1);

    cleric->shifter_down = 0;
    cleric->stats()->level = 5;
    cleric->stats()->magicpoints = 20.0f;

    // only cleric in range => howmany <= 1 path
    OG_ASSERT(!desc.do_special(cleric));

    // ally at full HP => didheal remains 0 path
    living* ally = add_living(fx, 0, FAMILY_SOLDIER);
    ally->setxy(90, 80);
    ally->stats()->hitpoints = ally->stats()->max_hitpoints;
    OG_ASSERT(!desc.do_special(cleric));
}

OG_UNIT_TEST(test_family_cleric_r11_heal_and_mace_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    living* ally = add_living(fx, 0, FAMILY_SOLDIER);
    OG_ASSERT(cleric != nullptr && ally != nullptr);

    cleric->setxy(80, 80);
    ally->setxy(90, 80);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->stats()->level = 6;
    cleric->stats()->magicpoints = 60.0f;
    cleric->stats()->max_magicpoints = 120.0f;
    cleric->current_special = 1;
    cleric->shifter_down = 0;
    ally->stats()->max_hitpoints = 100.0f;
    ally->stats()->hitpoints = 40.0f;

    const float ally_before = ally->stats()->hitpoints;
    const float mp_before = cleric->stats()->magicpoints;
    OG_ASSERT(desc.do_special(cleric));
    OG_ASSERT(ally->stats()->hitpoints > ally_before);
    OG_ASSERT(cleric->stats()->magicpoints < mp_before);

    // mystic mace int requirement fail (lines 147-152)
    cleric->shifter_down = 1;
    cleric->busy = 0;
    cleric->myguy->intelligence = 30;
    OG_ASSERT(!desc.do_special(cleric));

    // mystic mace success path (lines 158-171)
    cleric->myguy->intelligence = 70;
    cleric->stats()->magicpoints = 80.0f;
    cleric->stats()->special_cost[1] = 2;
    const float mp_before_mace = cleric->stats()->magicpoints;
    OG_ASSERT(desc.do_special(cleric));
    OG_ASSERT(cleric->busy >= 5);
    OG_ASSERT(cleric->stats()->magicpoints < mp_before_mace);
}

OG_UNIT_TEST(test_family_cleric_r11_turn_and_raise_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->intelligence = 70;
    cleric->stats()->level = 8;
    cleric->stats()->magicpoints = 200.0f;

    // case 2, turn undead with no targets => -1 branch
    cleric->current_special = 2;
    cleric->shifter_down = 1;
    OG_ASSERT(!desc.do_special(cleric));

    // case 2 raise skeleton, no blood => false
    cleric->shifter_down = 0;
    OG_ASSERT(!desc.do_special(cleric));

    // add nearby blood and summon skeleton success path
    walker* stain = add_stain(fx, 88, 80, 1, FAMILY_SOLDIER);
    OG_ASSERT(stain != nullptr);
    (void)desc.do_special(cleric);

    // case 3 raise ghost distance fail + no blood
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    walker* far_stain = add_stain(fx, 300, 300, 1, FAMILY_SOLDIER);
    OG_ASSERT(far_stain != nullptr);
    OG_ASSERT(!desc.do_special(cleric));
    far_stain->dead = 1;
    OG_ASSERT(!desc.do_special(cleric));

    // case 3 turn undead int fail branch
    cleric->shifter_down = 1;
    cleric->myguy->intelligence = 10;
    OG_ASSERT(!desc.do_special(cleric));
}

OG_UNIT_TEST(test_family_cleric_r11_resurrect_friendly_and_hostile_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericFixture fx;
    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);

    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->exp = 10000;
    cleric->stats()->level = 9;
    cleric->stats()->magicpoints = 300.0f;
    cleric->current_special = 4;

    // no blood => false path
    OG_ASSERT(!desc.do_special(cleric));

    // friendly blood resurrect path + exp penalty floor branch
    walker* friendly_stain = add_stain(fx, 88, 80, 0, FAMILY_SOLDIER);
    cleric->myguy->exp = 0;
    (void)desc.do_special(cleric);

    // hostile blood branch summons ghost
    walker* hostile_stain = add_stain(fx, 86, 84, 1, FAMILY_ORC);
    (void)desc.do_special(cleric);
}
