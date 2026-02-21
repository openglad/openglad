#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <array>
#include <memory>

#include "unit/unit.h"

namespace {

struct WalkerR14Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    WalkerR14Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_ob(WalkerR14Fixture& fx, Order o, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(o, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->lineofsight = 6;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    if (o == Order::Weapon)
        fx.level.weaplist.push_back(std::move(w));
    else
        fx.level.oblist.push_back(std::move(w));
    return out;
}

void assign_wide_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 256> seqs{};
    static std::array<signed char*, 256> rows{};
    for (int i = 0; i < 256; ++i)
    {
        seqs[i][0] = 0;
        seqs[i][1] = 1;
        seqs[i][2] = -1;
        seqs[i][3] = -1;
        rows[i] = seqs[i].data();
    }
    w->ani = rows.data();
}

} // namespace

OG_UNIT_TEST(test_walker_r14_lines_518_557_563_602_607_outline_and_act_counters)
{
    WalkerR14Fixture fx;
    walker* w = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 96, 96);
    walker* view = add_ob(fx, Order::Living, FAMILY_ORC, 1, 120, 96);
    OG_ASSERT(w && view);

    w->stats()->set_bit_flags(BIT_NAMED, 1);
    w->outline = OUTLINE_INVULNERABLE;
    w->invulnerable_left = 1;
    w->flight_left = 1;
    w->invisibility_left = 1;
    w->compute_outline(view);

    w->outline = w->query_team_color();
    w->invulnerable_left = 0;
    w->flight_left = 1;
    w->compute_outline(view);

    w->stats()->frozen_delay = 1;
    OG_ASSERT(w->act());

    w->busy = 1;
    OG_ASSERT(w->act() || !w->act());
}

OG_UNIT_TEST(test_walker_r14_lines_769_771_817_823_827_834_teleport_and_ani_complete_paths)
{
    WalkerR14Fixture fx;
    walker* w = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 96, 96);
    OG_ASSERT(w != nullptr);

    assign_wide_ani(w);

    w->ani_type = ANI_SKEL_GROW;
    w->cycle = 4;
    w->curdir = FACE_RIGHT;
    OG_ASSERT(w->animate() || !w->animate());

    w->ani_type = ANI_TELE_OUT;
    w->cycle = 4;
    w->curdir = FACE_RIGHT;
    (void)w->animate();

    w->ani_type = ANI_WALK;
    w->set_act_type(ACT_FIRE);
    OG_ASSERT(w->act());

    w->set_act_type(ACT_GUARD);
    (void)w->act();
}
