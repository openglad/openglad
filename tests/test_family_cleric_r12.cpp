#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>

#include "unit/unit.h"

const FamilyDescriptor& describe_family_cleric();

namespace {

struct ClericR12Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    ClericR12Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(ClericR12Fixture& fx, unsigned char team, char family = FAMILY_CLERIC)
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

walker* add_stain(ClericR12Fixture& fx, int x, int y, unsigned char team, char old_family)
{
    walker* stain = fx.level.add_ob(Order::Treasure, FAMILY_STAIN);
    stain->team_num = team;
    stain->setxy(x, y);
    stain->stats()->old_family = old_family;
    stain->dead = 0;
    return stain;
}

} // namespace

OG_UNIT_TEST(test_family_cleric_r12_ghost_raise_and_resurrect_penalty_paths)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    ClericR12Fixture fx;

    living* cleric = add_living(fx, 0, FAMILY_CLERIC);
    OG_ASSERT(cleric != nullptr);
    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->name = "R12";
    cleric->myguy->exp = 1;
    cleric->myguy->intelligence = 80;
    cleric->stats()->level = 10;
    cleric->stats()->magicpoints = 300.0f;

    // Case 3: raise ghost success path.
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    walker* near_stain = add_stain(fx, 84, 80, 1, FAMILY_ORC);
    OG_ASSERT(near_stain != nullptr);
    (void)desc.do_special(cleric);

    // Case 3: shifter_down turn-undead busy fail.
    cleric->shifter_down = 1;
    cleric->busy = 2;
    OG_ASSERT(!desc.do_special(cleric));
    cleric->busy = 0;

    // Case 4: friendly resurrect with exp floor path.
    cleric->current_special = 4;
    walker* friendly_stain = add_stain(fx, 82, 82, 0, FAMILY_SOLDIER);
    OG_ASSERT(friendly_stain != nullptr);
    (void)desc.do_special(cleric);

    // Case 4: hostile resurrect ghost path.
    walker* hostile_stain = add_stain(fx, 78, 82, 1, FAMILY_ORC);
    OG_ASSERT(hostile_stain != nullptr);
    (void)desc.do_special(cleric);

    // Case 1 heal branch with heal_numbers on and at least one ally damaged.
    living* ally = add_living(fx, 0, FAMILY_SOLDIER);
    ally->setxy(90, 80);
    ally->stats()->max_hitpoints = 100.0f;
    ally->stats()->hitpoints = 50.0f;
    cleric->current_special = 1;
    cleric->shifter_down = 0;
    cleric->stats()->magicpoints = 100.0f;
    cleric->stats()->max_magicpoints = 100.0f;
    cfg.apply_setting("effects", "heal_numbers", "on");
    OG_ASSERT(desc.do_special(cleric));
}
