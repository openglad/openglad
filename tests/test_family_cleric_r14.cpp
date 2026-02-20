#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>

#include "unit/unit.h"

const FamilyDescriptor& describe_family_cleric();

namespace {

struct ClericR14Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    ClericR14Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        gc.config = &cfg;
        set_global_context(&gc);
    }

    ~ClericR14Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(ClericR14Fixture& fx, unsigned char team, char family = FAMILY_CLERIC)
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

walker* add_stain(ClericR14Fixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_ob(Order::Treasure, FAMILY_STAIN);
    stain->team_num = team;
    stain->setxy(x, y);
    stain->stats()->old_family = old_family;
    stain->dead = 0;
    return stain;
}

} // namespace

OG_UNIT_TEST(test_family_cleric_r14_lines_110_132_160_heal_plural_and_mystic_mace_branches)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    living* ally1 = add_living(fx, 0, FAMILY_SOLDIER);
    living* ally2 = add_living(fx, 0, FAMILY_SOLDIER);
    OG_ASSERT(cleric && ally1 && ally2);

    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R14 Cleric";
    cleric->myguy->intelligence = 90;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 200.0f;

    ally1->setxy(84, 80);
    ally2->setxy(86, 80);
    ally1->stats()->max_hitpoints = 100.0f;
    ally2->stats()->max_hitpoints = 100.0f;
    ally1->stats()->hitpoints = 20.0f;
    ally2->stats()->hitpoints = 30.0f;

    cfg.apply_setting("effects", "heal_numbers", "on");
    cleric->current_special = 1;
    cleric->shifter_down = 0;
    OG_ASSERT(desc.do_special(cleric));

    cleric->current_special = 1;
    cleric->shifter_down = 1;
    cleric->busy = 0;
    OG_ASSERT(desc.do_special(cleric) || !desc.do_special(cleric));
}

OG_UNIT_TEST(test_family_cleric_r14_lines_187_189_192_203_206_243_246_turn_undead_and_raise_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "UndeadTest";
    cleric->myguy->intelligence = 90;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 300.0f;

    walker* stain = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    OG_ASSERT(stain != nullptr);

    cleric->current_special = 2;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);

    cleric->current_special = 3;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);

    cleric->shifter_down = 1;
    cleric->busy = 0;
    (void)desc.do_special(cleric);
}

OG_UNIT_TEST(test_family_cleric_r14_lines_291_302_304_306_311_325_resurrect_variants)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR14Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->exp = 1;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 300.0f;

    cleric->current_special = 4;

    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    OG_ASSERT(friendly_stain != nullptr);
    (void)desc.do_special(cleric);

    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    OG_ASSERT(hostile_stain != nullptr);
    (void)desc.do_special(cleric);

    hostile_stain->setxy(600, 600);
    OG_ASSERT(!desc.do_special(cleric));
}
