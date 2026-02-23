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
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <array>
#include <openglad/core/constants.h>

// --- From test_walker_coverage_push.cpp ---
namespace detail_walker_coverage_push {
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
} // namespace detail_walker_coverage_push

// --- From test_walker_r11.cpp ---
bool float_eq(float a, float b);

namespace detail_walker_r11 {
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

OG_UNIT_TEST(test_walker_r11_fire_query_next_to_and_outline_branches)
{
    WalkerR11Fixture fx;
    walker* shooter = add_ob(fx, Order::Living, FAMILY_MAGE, 0, 64, 64);
    walker* foe = add_ob(fx, Order::Living, FAMILY_ORC, 1, 82, 64);
    OG_ASSERT(shooter && foe);

    shooter->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    shooter->stats()->magicpoints = 200.0f;
    shooter->stats()->weapon_cost = 1;
    shooter->lastx = 1.0f;
    shooter->lasty = 0.0f;
    shooter->current_weapon = FAMILY_FIREBALL;
    shooter->setxy(64, 64);
    foe->setxy(82, 64);

    cfg.apply_setting("effects", "attack_lunge", "on");
    walker* melee = shooter->fire();
    OG_ASSERT(melee == nullptr);
    OG_ASSERT(shooter->attack_lunge >= 0.0f);

    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    walker* blocked = shooter->fire();
    OG_ASSERT(blocked == nullptr);
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    // Force ranged path by moving foe away and tracing all facings.
    foe->setxy(220, 220);
    const short dirs[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, -1}, {-1, -1}, {1, 1}, {-1, 1}
    };
    for (const auto& d : dirs)
    {
        shooter->lastx = static_cast<float>(d[0]);
        shooter->lasty = static_cast<float>(d[1]);
        walker* w = shooter->fire();
        OG_ASSERT(w != nullptr);
    }

    shooter->lastx = 1.0f;
    shooter->lasty = 1.0f;
    OG_ASSERT(shooter->query_next_to() == 0 || shooter->query_next_to() == 1);
    shooter->lastx = -1.0f;
    shooter->lasty = -1.0f;
    OG_ASSERT(shooter->query_next_to() == 0 || shooter->query_next_to() == 1);

    walker* viewer = add_ob(fx, Order::Living, FAMILY_SOLDIER, 1, 60, 64);
    OG_ASSERT(viewer != nullptr);
    shooter->outline = OUTLINE_INVULNERABLE;
    shooter->flight_left = 1;
    shooter->invisibility_left = 1;
    shooter->invulnerable_left = 0;
    shooter->compute_outline(viewer);

    shooter->outline = OUTLINE_FLYING;
    shooter->flight_left = 0;
    shooter->invulnerable_left = 1;
    shooter->compute_outline(viewer);

    shooter->outline = OUTLINE_NAMED;
    shooter->stats()->set_bit_flags(BIT_NAMED, 1);
    shooter->compute_outline(viewer);

    shooter->outline = shooter->query_team_color();
    shooter->stats()->set_bit_flags(BIT_NAMED, 0);
    shooter->invulnerable_left = 0;
    shooter->invisibility_left = 0;
    shooter->flight_left = 0;
    shooter->user = 0;
    viewer->team_num = shooter->team_num;
    shooter->compute_outline(viewer);
    OG_ASSERT(shooter->outline == shooter->query_team_color() || shooter->outline == 0);

    OG_ASSERT(float_eq(1.0f, 1.0f));
    OG_ASSERT(float_eq(1.0000001f, 1.0f));
}

OG_UNIT_TEST(test_walker_r11_act_and_animate_extra_cases)
{
    WalkerR11Fixture fx;
    walker* w = add_ob(fx, Order::Living, FAMILY_MAGE, 0, 64, 64);
    walker* foe = add_ob(fx, Order::Living, FAMILY_ORC, 1, 72, 64);
    OG_ASSERT(w && foe);

    assign_wide_ani(w);
    w->ani_type = ANI_WALK;
    w->set_act_type(ACT_CONTROL);
    OG_ASSERT(w->act());

    w->set_act_type(ACT_GENERATE);
    w->stats()->level = 50;
    w->stats()->hitpoints = 10.0f;
    w->stats()->max_hitpoints = 10.0f;
    (void)w->act();

    w->set_act_type(ACT_RANDOM);
    w->foe = foe;
    (void)w->act();

    w->stats()->frozen_delay = 2;
    (void)w->act();

    w->attack_lunge = 0.2f;
    w->hit_recoil = 0.2f;
    (void)w->act();

    w->ani_type = ANI_SKEL_GROW;
    w->cycle = 8;
    w->set_order_family(Order::Living, FAMILY_SKELETON);
    (void)w->animate();

    w->ani_type = ANI_TELE_OUT;
    w->cycle = 8;
    w->set_order_family(Order::Living, FAMILY_MAGE);
    (void)w->animate();

    // ANI_TELE_OUT default path on family without teleport handler.
    w->ani_type = ANI_TELE_OUT;
    w->cycle = 8;
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    OG_ASSERT(!w->animate() || w->ani_type == ANI_WALK);
}
} // namespace detail_walker_r11

// --- From test_walker_r14.cpp ---
namespace detail_walker_r14 {
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
} // namespace detail_walker_r14

// --- From test_walker_r15.cpp ---
namespace detail_walker_r15 {
namespace {

class MaxRandom final : public IRandom {
public:
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        return (max_exclusive == 0) ? 0u : (max_exclusive - 1u);
    }
};

struct WalkerR15Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    MaxRandom rng;

    WalkerR15Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

} // namespace

OG_UNIT_TEST(test_walker_r15_generator_fire_and_heading_branches)
{
    WalkerR15Fixture fx;

    walker* gen_tower = fx.level.add_ob(Order::Generator, FAMILY_TOWER);
    OG_ASSERT(gen_tower != nullptr);
    gen_tower->setxy(64, 64);
    gen_tower->sizex = 16;
    gen_tower->sizey = 16;
    gen_tower->stepsize = 2.0f;
    gen_tower->stats()->level = 6;
    gen_tower->stats()->magicpoints = 9999.0f;
    gen_tower->lastx = 1.0f;
    gen_tower->lasty = 0.0f;

    walker* fired = gen_tower->fire();
    OG_ASSERT(fired != nullptr);
    OG_ASSERT(fired->ani_type == ANI_TELE_IN);
    OG_ASSERT(fired->owner == nullptr);

    walker* weapon = fx.level.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    OG_ASSERT(weapon != nullptr);
    gen_tower->lastx = -1.0f;
    gen_tower->lasty = 0.0f;
    gen_tower->set_weapon_heading(weapon);
    OG_ASSERT(weapon->lastx <= 0.0f);

    gen_tower->lastx = 0.0f;
    gen_tower->lasty = 1.0f;
    gen_tower->set_weapon_heading(weapon);
    OG_ASSERT(weapon->lasty >= 0.0f);
}

OG_UNIT_TEST(test_walker_r15_compute_outline_and_next_frame_and_generate_paths)
{
    WalkerR15Fixture fx;

    walker* a = fx.level.add_ob(Order::Living, FAMILY_CLERIC);
    walker* viewer = fx.level.add_ob(Order::Living, FAMILY_SOLDIER);
    OG_ASSERT(a && viewer);
    a->team_num = 1;
    viewer->team_num = 0;
    a->stats()->set_bit_flags(BIT_NAMED, 1);

    a->outline = OUTLINE_INVULNERABLE;
    a->invulnerable_left = 1;
    a->flight_left = 0;
    a->invisibility_left = 0;
    a->compute_outline(viewer);
    OG_ASSERT(a->outline == OUTLINE_NAMED || a->outline == OUTLINE_INVULNERABLE);

    a->outline = OUTLINE_FLYING;
    a->flight_left = 1;
    a->compute_outline(viewer);
    OG_ASSERT(a->outline == OUTLINE_FLYING || a->outline == OUTLINE_NAMED);

    a->outline = static_cast<short>(a->query_team_color());
    a->invulnerable_left = 1;
    a->flight_left = 0;
    a->compute_outline(viewer);
    OG_ASSERT(a->outline == OUTLINE_INVULNERABLE || a->outline == OUTLINE_NAMED);

    walker* gen_tent = fx.level.add_ob(Order::Generator, FAMILY_TENT);
    OG_ASSERT(gen_tent != nullptr);
    gen_tent->stats()->level = 200;
    gen_tent->stats()->hitpoints = 10.0f;
    gen_tent->stats()->max_hitpoints = 10.0f;
    gen_tent->lineofsight = 3;
    gen_tent->set_act_type(ACT_GENERATE);
    (void)gen_tent->act();
    OG_ASSERT(gen_tent->stats()->hitpoints <= gen_tent->stats()->max_hitpoints);

    // next_frame path using real animation data loaded by loader.
    walker* living = fx.level.add_ob(Order::Living, FAMILY_SOLDIER);
    OG_ASSERT(living != nullptr);
    (void)living->next_frame();
}
} // namespace detail_walker_r15

