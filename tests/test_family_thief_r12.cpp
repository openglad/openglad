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

const FamilyDescriptor& describe_family_thief();

namespace {

struct ThiefR12Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    ThiefR12Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(ThiefR12Fixture& fx, unsigned char team, char family = FAMILY_THIEF)
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

} // namespace

OG_UNIT_TEST(test_family_thief_r12_check_ai_and_special_paths)
{
    const FamilyDescriptor& desc = describe_family_thief();
    ThiefR12Fixture fx;

    living* thief = add_living(fx, 0, FAMILY_THIEF);
    living* foe1 = add_living(fx, 1, FAMILY_ORC);
    living* foe2 = add_living(fx, 1, FAMILY_ORC);
    living* foe3 = add_living(fx, 1, FAMILY_ORC);
    OG_ASSERT(thief && foe1 && foe2 && foe3);

    thief->current_special = 1;
    thief->foe = nullptr;
    foe1->setxy(200, 200);
    foe2->setxy(210, 200);
    foe3->setxy(220, 200);
    OG_ASSERT(!desc.check_special_ai(thief));

    foe1->setxy(90, 80);
    foe2->setxy(94, 80);
    foe3->setxy(98, 80);
    OG_ASSERT(desc.check_special_ai(thief));

    thief->current_special = 3;
    thief->shifter_down = 0;
    OG_ASSERT(desc.check_special_ai(thief));
    thief->shifter_down = 1;
    OG_ASSERT(desc.check_special_ai(thief));

    thief->current_special = 1;
    thief->user = -1;
    thief->stats()->level = 4;
    OG_ASSERT(desc.do_special(thief));

    thief->current_special = 3;
    thief->shifter_down = 0;
    thief->busy = 0;
    thief->stats()->name = "Bandit";
    OG_ASSERT(desc.do_special(thief));

    thief->current_special = 3;
    thief->shifter_down = 1;
    thief->busy = 0;
    thief->foe = foe1;
    foe1->real_team_num = 255;
    OG_ASSERT(desc.do_special(thief));

    thief->current_special = 4;
    thief->busy = 0;
    OG_ASSERT(desc.do_special(thief));
}
