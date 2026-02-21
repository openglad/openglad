#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>

#include <memory>

#include "unit/unit.h"

namespace {

struct WalkerFixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    WalkerFixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(WalkerFixture& fx, char family, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->lineofsight = 6;
    w->setxy(64, 64);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

} // namespace

OG_UNIT_TEST(test_walker_reset_compute_outline_and_act_paths)
{
    WalkerFixture fx;
    walker* w = add_living(fx, FAMILY_SOLDIER, 0);
    OG_ASSERT(w != nullptr);

    w->invisibility_left = 1;
    w->compute_outline(nullptr);
    OG_ASSERT(w->outline == w->query_team_color());

    w->outline = OUTLINE_NAMED;
    w->invisibility_left = 0;
    w->invulnerable_left = 1;
    w->compute_outline(nullptr);
    OG_ASSERT(w->outline == OUTLINE_INVULNERABLE);

    w->set_act_type(ACT_DIE);
    w->dead = 0;
    OG_ASSERT(w->act());
    OG_ASSERT(w->dead == 1);

    w->dead = 0;
    w->set_act_type(127);
    OG_ASSERT(!w->act());

    OG_ASSERT(w->reset());
}

OG_UNIT_TEST(test_walker_friendliness_and_distance_paths)
{
    WalkerFixture fx;
    walker* a = add_living(fx, FAMILY_SOLDIER, 0);
    walker* b = add_living(fx, FAMILY_ORC, 1);
    OG_ASSERT(a && b);

    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    b->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
    a->setxy(64, 64);
    b->setxy(96, 64);

    OG_ASSERT(a->distance_to_ob(b) > 0);
    OG_ASSERT(a->distance_to_ob_center(b) >= 0);
    OG_ASSERT(!a->is_friendly(b));
    OG_ASSERT(a->is_friendly_to_team(0));

    fx.save.allied_mode = 1;
    OG_ASSERT(a->is_friendly(b));
}

OG_UNIT_TEST(test_walker_death_save_all_and_misc_paths)
{
    WalkerFixture fx;
    walker* w = add_living(fx, FAMILY_SKELETON, 0); // no bloodspot branch
    OG_ASSERT(w != nullptr);
    w->stats()->name = "Named";
    w->dead = 1;
    fx.level.type = static_cast<char>(SCEN_TYPE_SAVE_ALL);

    OG_ASSERT(w->death());
    OG_ASSERT(fx.events.size() >= 1);

    walker misc;
    misc.set_order_family(Order::Generator, FAMILY_TENT);
    misc.sim_level = &fx.level;
    misc.sim_rng = &fx.rng;
    OG_ASSERT(misc.fire_check(1, 0));
    (void)misc.eat_me(nullptr);
    OG_ASSERT(misc.do_summon(0, 0) == nullptr);
    OG_ASSERT(!misc.check_special());
}
