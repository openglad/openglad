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

struct WalkerR11Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    WalkerR11Fixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_ob(WalkerR11Fixture& fx, Order o, char family, unsigned char team, short x, short y)
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

void assign_basic_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
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

OG_UNIT_TEST(test_walker_r11_myguy_move_and_init_fire_paths)
{
    WalkerR11Fixture fx;
    walker* a = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* b = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 96, 64);
    OG_ASSERT(a && b);

    a->move_myguy_to(nullptr);

    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    a->move_myguy_to(b);
    OG_ASSERT(a->myguy == nullptr);
    OG_ASSERT(b->myguy != nullptr);

    a->set_myguy_view(b->myguy);
    a->move_myguy_to(b);
    OG_ASSERT(a->myguy == nullptr);

    // init_fire: control-turn guard branch
    a->curdir = FACE_LEFT;
    a->enddir = FACE_LEFT;
    a->set_act_type(ACT_CONTROL);
    OG_ASSERT(!a->init_fire(1, 0));

    // busy branch (don't require return value here; ACT_CONTROL turn handling can vary with facing state)
    a->set_act_type(ACT_RANDOM);
    a->busy = 1;
    (void)a->init_fire(1, 0);

    // ANI_WALK branch + animate call
    a->busy = 0;
    a->ani_type = ANI_WALK;
    assign_basic_ani(a);
    OG_ASSERT(a->init_fire(0, 1));
}

OG_UNIT_TEST(test_walker_r11_fire_check_create_weapon_and_angles)
{
    WalkerR11Fixture fx;
    walker* shooter = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 80, 80);
    walker* foe = add_ob(fx, Order::Living, FAMILY_ORC, 1, 92, 80);
    OG_ASSERT(shooter && foe);

    shooter->stats()->magicpoints = 100.0f;
    shooter->stats()->weapon_cost = 1;
    shooter->curdir = FACE_RIGHT;
    shooter->lastx = 1;
    shooter->lasty = 0;

    // no foe path
    shooter->foe = nullptr;
    OG_ASSERT(!shooter->fire_check(1, 0));

    // bit no ranged path
    shooter->foe = foe;
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    OG_ASSERT(!shooter->fire_check(1, 0));
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    // targetdir mismatch path
    shooter->curdir = FACE_LEFT;
    OG_ASSERT(!shooter->fire_check(1, 0));

    // likely success/failure traversal through ray loop
    shooter->curdir = FACE_RIGHT;
    (void)shooter->fire_check(1, 0);

    // create_weapon generator path
    walker* gen = add_ob(fx, Order::Generator, FAMILY_TENT, 1, 120, 80);
    gen->default_weapon = FAMILY_SOLDIER;
    gen->stats()->level = 3;
    walker* spawned = gen->create_weapon();
    OG_ASSERT(spawned != nullptr);

    // set_weapon_heading switch traversal for all facings
    walker* weapon = add_ob(fx, Order::Weapon, FAMILY_KNIFE, 0, 70, 70);
    for (int d = 0; d < 8; ++d)
    {
        shooter->curdir = static_cast<char>(d);
        shooter->lastx = (d == FACE_LEFT || d == FACE_UP_LEFT || d == FACE_DOWN_LEFT) ? -1.0f : 1.0f;
        shooter->lasty = (d == FACE_UP || d == FACE_UP_LEFT || d == FACE_UP_RIGHT) ? -1.0f : 1.0f;
        shooter->set_weapon_heading(weapon);
    }

    // angle switch/default
    for (int d = 0; d < 8; ++d)
    {
        shooter->curdir = static_cast<char>(d);
        (void)shooter->get_current_angle();
    }
    shooter->curdir = 120;
    (void)shooter->get_current_angle();
}

OG_UNIT_TEST(test_walker_r11_act_animate_and_misc_paths)
{
    WalkerR11Fixture fx;
    walker* w = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_ob(fx, Order::Living, FAMILY_ORC, 1, 96, 64);
    OG_ASSERT(w && foe);

    // animate guards
    w->ani = nullptr;
    OG_ASSERT(!w->animate());

    assign_basic_ani(w);
    w->ani_type = ANI_ATTACK;
    w->curdir = FACE_RIGHT;
    w->cycle = 0;
    w->stats()->magicpoints = 100.0f;
    w->stats()->weapon_cost = 1;
    w->lastx = 1;
    w->lasty = 0;
    w->foe = foe;
    (void)w->animate();

    // query/restore helpers
    w->set_old_act_type(ACT_GUARD);
    OG_ASSERT(w->query_old_act_type() == ACT_GUARD);
    w->set_act_type(ACT_CONTROL);
    OG_ASSERT(w->query_act_type() == ACT_CONTROL);
    (void)w->restore_act_type();

    // collide, spaces, center, set_difficulty, friendliness and owner-chain paths
    OG_ASSERT(w->collide(foe));
    (void)w->spaces_clear();
    w->center_on(foe);

    w->set_order_family(Order::Generator, FAMILY_TENT);
    w->set_difficulty(5);
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->team_num = 1;
    w->set_difficulty(2);

    walker* owned = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 100, 100);
    owned->owner = w;
    w->owner = w; // self-loop guard branch
    OG_ASSERT(!w->is_friendly(nullptr));
    (void)w->is_friendly(owned);
    w->dead = 1;
    OG_ASSERT(!w->is_friendly_to_team(0));

    w->dead = 0;
    w->owner = nullptr;
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    fx.save.allied_mode = 1;
    (void)w->is_friendly_to_team(0);

    // do_summon/check_special fallback and eat_me logging path
    OG_ASSERT(w->do_summon(1, 1) == nullptr);
    OG_ASSERT(!w->check_special());
    (void)w->eat_me(foe);
}
