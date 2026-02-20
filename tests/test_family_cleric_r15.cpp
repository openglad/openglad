#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/game_context.h>
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

struct ClericR15Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    ClericR15Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        gc.config = &cfg;
        set_global_context(&gc);
    }

    ~ClericR15Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(ClericR15Fixture& fx, unsigned char team, char family, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
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

walker* add_stain(ClericR15Fixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    stain->team_num = team;
    stain->setxy(static_cast<short>(x), static_cast<short>(y));
    stain->stats()->old_family = old_family;
    stain->dead = 0;
    return stain;
}

} // namespace

OG_UNIT_TEST(test_family_cleric_r15_low_magic_heal_branch_and_mystic_mace_guard)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR15Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC, 80, 80);
    living* ally = add_living(fx, 0, FAMILY_SOLDIER, 84, 80);
    OG_ASSERT(cleric && ally);

    cleric->stats()->level = 8;
    cleric->stats()->magicpoints = 1.0f; // force low-magic adjustment branch
    ally->stats()->max_hitpoints = 100.0f;
    ally->stats()->hitpoints = 5.0f;
    cleric->current_special = 1;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);

    cleric->current_special = 1;
    cleric->shifter_down = 1;
    cleric->busy = 1;
    OG_ASSERT(!desc.do_special(cleric));
}

OG_UNIT_TEST(test_family_cleric_r15_turn_undead_raise_and_resurrect_branches)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR15Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC, 80, 80);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R15 Cleric";
    cleric->myguy->intelligence = 90;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 300.0f;

    // Turn undead branch with valid undead target.
    living* undead = add_living(fx, 1, FAMILY_SKELETON, 92, 80);
    OG_ASSERT(undead != nullptr);
    cleric->current_special = 2;
    cleric->shifter_down = 1;
    (void)desc.do_special(cleric);

    // Raise skeleton and ghost from nearby blood.
    walker* stain1 = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    walker* stain2 = add_stain(fx, 86, 80, 1, FAMILY_ORC);
    OG_ASSERT(stain1 && stain2);
    cleric->current_special = 2;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);

    // Resurrect both friendly and hostile stains.
    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    OG_ASSERT(friendly_stain && hostile_stain);
    cleric->current_special = 4;
    cleric->shifter_down = 0;
    (void)desc.do_special(cleric);
    (void)desc.do_special(cleric);
}
