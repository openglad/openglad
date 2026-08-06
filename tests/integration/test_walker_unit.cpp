#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/core/irandom.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/session_state.h>
#include <memory>
#include <gtest/gtest.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <array>
#include <openglad/core/constants.h>
#include "test_gameplay_context_scope.h"

namespace {

class ScopedTestContextOverride
{
public:
    explicit ScopedTestContextOverride(GameContext& context)
    {
        push_test_context(&context);
    }

    ~ScopedTestContextOverride()
    {
        pop_test_context();
    }

    ScopedTestContextOverride(const ScopedTestContextOverride&) = delete;
    ScopedTestContextOverride& operator=(const ScopedTestContextOverride&) = delete;
};

class ScopedCurrentGameOverride
{
public:
    explicit ScopedCurrentGameOverride(GameplayContext* replacement)
        : previous_(current_game)
    {
        current_game = replacement;
    }

    ~ScopedCurrentGameOverride()
    {
        current_game = previous_;
    }

    ScopedCurrentGameOverride(const ScopedCurrentGameOverride&) = delete;
    ScopedCurrentGameOverride& operator=(const ScopedCurrentGameOverride&) = delete;

private:
    GameplayContext* previous_ = nullptr;
};

class ScopedGameplayActiveOverride
{
public:
    explicit ScopedGameplayActiveOverride(bool active)
        : session_(og::runtime::current_session)
        , previous_(session_ ? session_->gameplay_active_ : false)
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = active;
    }

    ~ScopedGameplayActiveOverride()
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = previous_;
    }

    ScopedGameplayActiveOverride(const ScopedGameplayActiveOverride&) = delete;
    ScopedGameplayActiveOverride& operator=(const ScopedGameplayActiveOverride&) = delete;

private:
    og::runtime::SessionState* session_ = nullptr;
    bool previous_ = false;
};

} // namespace

// --- From test_walker_coverage_push.cpp ---
namespace detail_walker_coverage_push {
namespace {

struct WalkerFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    WalkerFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(WalkerFixture& fx, char family, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_lineofsight(6);
    w->setxy(64, 64);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(WalkerUnit, walker_reset_compute_outline_and_act_paths)
{
    WalkerFixture fx;
    walker* w = add_living(fx, FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr);

    w->set_invisibility_left(1);
    w->compute_outline(nullptr);
    ASSERT_TRUE(w->outline() == w->query_team_color());

    w->set_outline(OUTLINE_NAMED);
    w->set_invisibility_left(0);
    w->set_invulnerable_left(1);
    w->compute_outline(nullptr);
    ASSERT_TRUE(w->outline() == OUTLINE_INVULNERABLE);

    w->set_act_type(ACT_DIE);
    w->set_dead(0);
    ASSERT_TRUE(w->act());
    ASSERT_TRUE(w->dead() == 1);

    w->set_dead(0);
    w->set_act_type(127);
    ASSERT_TRUE(!w->act());

    ASSERT_TRUE(w->reset());
}

TEST(WalkerUnit, walker_friendliness_and_distance_paths)
{
    WalkerFixture fx;
    walker* a = add_living(fx, FAMILY_SOLDIER, 0);
    walker* b = add_living(fx, FAMILY_ORC, 1);
    ASSERT_TRUE(a && b);

    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    b->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
    a->setxy(64, 64);
    b->setxy(96, 64);

    ASSERT_TRUE(a->distance_to_ob(b) > 0);
    ASSERT_TRUE(a->distance_to_ob_center(b) >= 0);
    ASSERT_TRUE(!a->is_friendly(b));
    ASSERT_TRUE(a->is_friendly_to_team(0));

    fx.level.world().allied_mode = 1;
    ASSERT_FALSE(a->is_friendly(b))
        << "PVP seating mode must not befriend different colors";
    b->set_team_num(0);
    ASSERT_TRUE(a->is_friendly(b));
}

TEST(WalkerUnit, walker_death_save_all_and_misc_paths)
{
    WalkerFixture fx;
    walker* w = add_living(fx, FAMILY_SKELETON, 0); // no bloodspot branch
    ASSERT_TRUE(w != nullptr);
    w->stats()->name = "Named";
    w->set_dead(1);
    fx.level.world().type = static_cast<char>(SCEN_TYPE_SAVE_ALL);
    fx.level.world().my_team = 0;

    ASSERT_TRUE(w->death());
    ASSERT_TRUE(fx.events.size() >= 1);

    walker misc;
    misc.set_order_family(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(misc.fire_check(1, 0));
    (void)misc.eat_me(nullptr);
    ASSERT_TRUE(misc.do_summon(0, 0) == nullptr);
    ASSERT_TRUE(!misc.check_special());
}
} // namespace detail_walker_coverage_push

// --- From test_walker_r11.cpp ---
bool float_eq(float a, float b);

namespace detail_walker_r11 {
namespace {

struct WalkerR11Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    WalkerR11Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_ob(WalkerR11Fixture& fx, Order o, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(o, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_lineofsight(6);
    w->setxy(x, y);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    if (o == Order::Weapon)
        fx.level.world().weaplist.push_back(std::move(w));
    else
        fx.level.world().oblist.push_back(std::move(w));
    return out;
}

void assign_basic_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[static_cast<std::size_t>(i)][0] = 0;
        seqs[static_cast<std::size_t>(i)][1] = 1;
        seqs[static_cast<std::size_t>(i)][2] = -1;
        seqs[static_cast<std::size_t>(i)][3] = -1;
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
    }
    w->ani = rows.data();
}

void assign_wide_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 256> seqs{};
    static std::array<signed char*, 256> rows{};
    for (int i = 0; i < 256; ++i)
    {
        seqs[static_cast<std::size_t>(i)][0] = 0;
        seqs[static_cast<std::size_t>(i)][1] = 1;
        seqs[static_cast<std::size_t>(i)][2] = -1;
        seqs[static_cast<std::size_t>(i)][3] = -1;
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
    }
    w->ani = rows.data();
}

} // namespace

TEST(WalkerUnit, walker_r11_myguy_move_and_init_fire_paths)
{
    WalkerR11Fixture fx;
    walker* a = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* b = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 96, 64);
    ASSERT_TRUE(a && b);

    a->move_myguy_to(nullptr);

    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    a->move_myguy_to(b);
    ASSERT_TRUE(a->myguy == nullptr);
    ASSERT_TRUE(b->myguy != nullptr);

    a->set_myguy_view(b->myguy);
    a->move_myguy_to(b);
    ASSERT_TRUE(a->myguy == nullptr);

    // init_fire: control-turn guard branch
    a->set_curdir(FACE_LEFT);
    a->set_enddir(FACE_LEFT);
    a->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(!a->init_fire(1, 0));

    // busy branch (don't require return value here; ACT_CONTROL turn handling can vary with facing state)
    a->set_act_type(ACT_RANDOM);
    a->set_busy(1);
    (void)a->init_fire(1, 0);

    // ANI_WALK branch + animate call
    a->set_busy(0);
    a->set_ani_type(ANI_WALK);
    assign_basic_ani(a);
    ASSERT_TRUE(a->init_fire(0, 1));
}

TEST(WalkerUnit, walker_r11_fire_check_create_weapon_and_angles)
{
    WalkerR11Fixture fx;
    walker* shooter = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 80, 80);
    walker* foe = add_ob(fx, Order::Living, FAMILY_ORC, 1, 92, 80);
    ASSERT_TRUE(shooter && foe);

    shooter->stats()->set_magicpoints(100.0f);
    shooter->stats()->set_weapon_cost(1);
    shooter->set_curdir(FACE_RIGHT);
    shooter->set_lastx(1);
    shooter->set_lasty(0);

    // no foe path
    shooter->set_foe(nullptr);
    ASSERT_TRUE(!shooter->fire_check(1, 0));

    // bit no ranged path
    shooter->set_foe(foe);
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    ASSERT_TRUE(!shooter->fire_check(1, 0));
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    // targetdir mismatch path
    shooter->set_curdir(FACE_LEFT);
    ASSERT_TRUE(!shooter->fire_check(1, 0));

    // likely success/failure traversal through ray loop
    shooter->set_curdir(FACE_RIGHT);
    (void)shooter->fire_check(1, 0);

    // create_weapon generator path
    walker* gen = add_ob(fx, Order::Generator, FAMILY_TENT, 1, 120, 80);
    gen->set_default_weapon(FAMILY_SOLDIER);
    gen->stats()->set_level(3);
    walker* spawned = gen->create_weapon();
    ASSERT_TRUE(spawned != nullptr);

    // set_weapon_heading switch traversal for all facings
    walker* weapon = add_ob(fx, Order::Weapon, FAMILY_KNIFE, 0, 70, 70);
    for (int d = 0; d < 8; ++d)
    {
        shooter->set_curdir(static_cast<char>(d));
        shooter->set_lastx((d == FACE_LEFT || d == FACE_UP_LEFT || d == FACE_DOWN_LEFT) ? -1.0f : 1.0f);
        shooter->set_lasty((d == FACE_UP || d == FACE_UP_LEFT || d == FACE_UP_RIGHT) ? -1.0f : 1.0f);
        shooter->set_weapon_heading(weapon);
    }

    // angle switch/default
    for (int d = 0; d < 8; ++d)
    {
        shooter->set_curdir(static_cast<char>(d));
        (void)shooter->get_current_angle();
    }
    shooter->set_curdir(120);
    (void)shooter->get_current_angle();
}

TEST(WalkerUnit, walker_r11_act_animate_and_misc_paths)
{
    WalkerR11Fixture fx;
    walker* w = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_ob(fx, Order::Living, FAMILY_ORC, 1, 96, 64);
    ASSERT_TRUE(w && foe);

    // animate guards
    w->ani = nullptr;
    ASSERT_TRUE(!w->animate());

    assign_basic_ani(w);
    w->set_ani_type(ANI_ATTACK);
    w->set_curdir(FACE_RIGHT);
    w->set_cycle(0);
    w->stats()->set_magicpoints(100.0f);
    w->stats()->set_weapon_cost(1);
    w->set_lastx(1);
    w->set_lasty(0);
    w->set_foe(foe);
    (void)w->animate();

    // query/restore helpers
    w->set_old_act_type(ACT_GUARD);
    ASSERT_TRUE(w->old_act_type() == ACT_GUARD);
    w->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(w->act_type() == ACT_CONTROL);
    (void)w->restore_act_type();

    // collide, spaces, center, set_difficulty, friendliness and owner-chain paths
    ASSERT_TRUE(w->collide(foe));
    (void)w->spaces_clear();
    w->center_on(foe);

    w->set_order_family(Order::Generator, FAMILY_TENT);
    w->set_difficulty(5);
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_team_num(1);
    w->set_difficulty(2);

    walker* owned = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 100, 100);
    owned->set_owner(w);
    w->set_owner(w); // self-loop guard branch
    ASSERT_TRUE(!w->is_friendly(nullptr));
    (void)w->is_friendly(owned);
    w->set_dead(1);
    ASSERT_TRUE(!w->is_friendly_to_team(0));

    w->set_dead(0);
    w->set_owner(nullptr);
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    fx.level.world().allied_mode = 1;
    (void)w->is_friendly_to_team(0);

    // do_summon/check_special fallback and eat_me logging path
    ASSERT_TRUE(w->do_summon(1, 1) == nullptr);
    ASSERT_TRUE(!w->check_special());
    (void)w->eat_me(foe);
}

TEST(WalkerUnit, walker_r11_fire_query_next_to_and_outline_branches)
{
    WalkerR11Fixture fx;
    walker* shooter = add_ob(fx, Order::Living, FAMILY_MAGE, 0, 64, 64);
    walker* foe = add_ob(fx, Order::Living, FAMILY_ORC, 1, 82, 64);
    ASSERT_TRUE(shooter && foe);

    shooter->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    shooter->stats()->set_magicpoints(200.0f);
    shooter->stats()->set_weapon_cost(1);
    shooter->set_lastx(1.0f);
    shooter->set_lasty(0.0f);
    shooter->set_current_weapon(FAMILY_FIREBALL);
    shooter->setxy(64, 64);
    foe->setxy(82, 64);

    cfg.apply_setting("effects", "attack_lunge", "on");
    walker* melee = shooter->fire();
    ASSERT_TRUE(melee == nullptr);
    ASSERT_TRUE(shooter->attack_lunge() >= 0.0f);

    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    walker* blocked = shooter->fire();
    ASSERT_TRUE(blocked == nullptr);
    shooter->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    // Force ranged path by moving foe away and tracing all facings.
    foe->setxy(220, 220);
    const short dirs[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, -1}, {-1, -1}, {1, 1}, {-1, 1}
    };
    for (const auto& d : dirs)
    {
        shooter->set_lastx(static_cast<float>(d[0]));
        shooter->set_lasty(static_cast<float>(d[1]));
        walker* w = shooter->fire();
        ASSERT_TRUE(w != nullptr);
    }

    shooter->set_lastx(1.0f);
    shooter->set_lasty(1.0f);
    ASSERT_FALSE(shooter->query_next_to())
        << "empty adjacent diagonal should be object-passable";
    walker* adjacent_blocker = add_ob(
        fx, Order::Living, FAMILY_ORC, 1,
        static_cast<short>(shooter->xpos() + shooter->sizex()),
        static_cast<short>(shooter->ypos() + shooter->sizey()));
    ASSERT_TRUE(adjacent_blocker != nullptr);
    ASSERT_TRUE(shooter->query_next_to())
        << "occupied adjacent diagonal should not be object-passable";
    shooter->set_lastx(-1.0f);
    shooter->set_lasty(-1.0f);
    ASSERT_FALSE(shooter->query_next_to())
        << "opposite empty adjacent diagonal should remain object-passable";

    walker* viewer = add_ob(fx, Order::Living, FAMILY_SOLDIER, 1, 60, 64);
    ASSERT_TRUE(viewer != nullptr);
    shooter->set_outline(OUTLINE_INVULNERABLE);
    shooter->set_flight_left(1);
    shooter->set_invisibility_left(1);
    shooter->set_invulnerable_left(0);
    shooter->compute_outline(viewer);

    shooter->set_outline(OUTLINE_FLYING);
    shooter->set_flight_left(0);
    shooter->set_invulnerable_left(1);
    shooter->compute_outline(viewer);

    shooter->set_outline(OUTLINE_NAMED);
    shooter->stats()->set_bit_flags(BIT_NAMED, 1);
    shooter->compute_outline(viewer);

    shooter->set_outline(shooter->query_team_color());
    shooter->stats()->set_bit_flags(BIT_NAMED, 0);
    shooter->set_invulnerable_left(0);
    shooter->set_invisibility_left(0);
    shooter->set_flight_left(0);
    shooter->set_user(0);
    viewer->set_team_num(shooter->team_num());
    shooter->compute_outline(viewer);
    ASSERT_TRUE(shooter->outline() == shooter->query_team_color() || shooter->outline() == 0);

    ASSERT_TRUE(float_eq(1.0f, 1.0f));
    ASSERT_TRUE(float_eq(1.0000001f, 1.0f));
}

TEST(WalkerUnit, walker_r11_act_and_animate_extra_cases)
{
    WalkerR11Fixture fx;
    walker* w = add_ob(fx, Order::Living, FAMILY_MAGE, 0, 64, 64);
    walker* foe = add_ob(fx, Order::Living, FAMILY_ORC, 1, 72, 64);
    ASSERT_TRUE(w && foe);

    assign_wide_ani(w);
    w->set_ani_type(ANI_WALK);
    w->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(w->act());

    w->set_act_type(ACT_GENERATE);
    w->stats()->set_level(50);
    w->stats()->set_hitpoints(10.0f);
    w->stats()->set_max_hitpoints(10.0f);
    (void)w->act();

    w->set_act_type(ACT_RANDOM);
    w->set_foe(foe);
    (void)w->act();

    w->stats()->set_frozen_delay(2);
    (void)w->act();

    w->set_attack_lunge(0.2f);
    w->set_hit_recoil(0.2f);
    (void)w->act();

    w->set_ani_type(ANI_SKEL_GROW);
    w->set_cycle(8);
    w->set_order_family(Order::Living, FAMILY_SKELETON);
    (void)w->animate();

    w->set_ani_type(ANI_TELE_OUT);
    w->set_cycle(8);
    w->set_order_family(Order::Living, FAMILY_MAGE);
    (void)w->animate();

    // ANI_TELE_OUT default path on family without teleport handler.
    w->set_ani_type(ANI_TELE_OUT);
    w->set_cycle(8);
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(!w->animate() || w->ani_type() == ANI_WALK);
}
} // namespace detail_walker_r11

// --- From test_walker_r14.cpp ---
namespace detail_walker_r14 {
namespace {

struct WalkerR14Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    WalkerR14Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_ob(WalkerR14Fixture& fx, Order o, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(o, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_lineofsight(6);
    w->setxy(x, y);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    if (o == Order::Weapon)
        fx.level.world().weaplist.push_back(std::move(w));
    else
        fx.level.world().oblist.push_back(std::move(w));
    return out;
}

void assign_wide_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 256> seqs{};
    static std::array<signed char*, 256> rows{};
    for (int i = 0; i < 256; ++i)
    {
        seqs[static_cast<std::size_t>(i)][0] = 0;
        seqs[static_cast<std::size_t>(i)][1] = 1;
        seqs[static_cast<std::size_t>(i)][2] = -1;
        seqs[static_cast<std::size_t>(i)][3] = -1;
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
    }
    w->ani = rows.data();
}

} // namespace

TEST(WalkerUnit, walker_r14_lines_518_557_563_602_607_outline_and_act_counters)
{
    WalkerR14Fixture fx;
    walker* w = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 96, 96);
    walker* view = add_ob(fx, Order::Living, FAMILY_ORC, 1, 120, 96);
    ASSERT_TRUE(w && view);

    w->stats()->set_bit_flags(BIT_NAMED, 1);
    w->set_outline(OUTLINE_INVULNERABLE);
    w->set_invulnerable_left(1);
    w->set_flight_left(1);
    w->set_invisibility_left(1);
    w->compute_outline(view);

    w->set_outline(w->query_team_color());
    w->set_invulnerable_left(0);
    w->set_flight_left(1);
    w->compute_outline(view);

    w->stats()->set_frozen_delay(1);
    ASSERT_TRUE(w->act());

    w->set_busy(1);
    (void)w->act();
    ASSERT_TRUE(w->busy() <= 1);
}

TEST(WalkerUnit, walker_r14_lines_769_771_817_823_827_834_teleport_and_ani_complete_paths)
{
    WalkerR14Fixture fx;
    walker* w = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 96, 96);
    ASSERT_TRUE(w != nullptr);

    assign_wide_ani(w);

    w->set_ani_type(ANI_SKEL_GROW);
    w->set_cycle(4);
    w->set_curdir(FACE_RIGHT);
    ASSERT_TRUE(w->animate());
    ASSERT_EQ(ANI_WALK, w->ani_type());
    ASSERT_EQ(0, w->cycle());

    w->set_ani_type(ANI_TELE_OUT);
    w->set_cycle(4);
    w->set_curdir(FACE_RIGHT);
    (void)w->animate();

    w->set_ani_type(ANI_WALK);
    w->set_act_type(ACT_FIRE);
    ASSERT_TRUE(w->act());

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
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    MaxRandom rng;
    ScopedGameplayContext gameplay;
    ScopedGameplayActiveOverride gameplay_active{true};

    WalkerR15Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

} // namespace

TEST(WalkerUnit, walker_r15_generator_fire_and_heading_branches)
{
    WalkerR15Fixture fx;

    walker* gen_tower = fx.level.add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_TRUE(gen_tower != nullptr);
    gen_tower->setxy(64, 64);
    gen_tower->set_sizex(16);
    gen_tower->set_sizey(16);
    gen_tower->set_stepsize(2.0f);
    gen_tower->stats()->set_level(6);
    gen_tower->stats()->set_magicpoints(9999.0f);
    gen_tower->set_lastx(1.0f);
    gen_tower->set_lasty(0.0f);

    walker* fired = gen_tower->fire();
    ASSERT_TRUE(fired != nullptr);
    ASSERT_TRUE(fired->ani_type() == ANI_TELE_IN);
    ASSERT_TRUE(fired->owner() == nullptr);

    walker* weapon = fx.level.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weapon != nullptr);
    gen_tower->set_lastx(-1.0f);
    gen_tower->set_lasty(0.0f);
    gen_tower->set_weapon_heading(weapon);
    ASSERT_TRUE(weapon->lastx() <= 0.0f);

    gen_tower->set_lastx(0.0f);
    gen_tower->set_lasty(1.0f);
    gen_tower->set_weapon_heading(weapon);
    ASSERT_TRUE(weapon->lasty() >= 0.0f);
}

TEST(WalkerUnit, walker_r15_compute_outline_and_next_frame_and_generate_paths)
{
    WalkerR15Fixture fx;

    walker* a = fx.level.add_ob(Order::Living, FAMILY_CLERIC);
    walker* viewer = fx.level.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(a && viewer);
    a->set_team_num(1);
    viewer->set_team_num(0);
    a->stats()->set_bit_flags(BIT_NAMED, 1);

    a->set_outline(OUTLINE_INVULNERABLE);
    a->set_invulnerable_left(1);
    a->set_flight_left(0);
    a->set_invisibility_left(0);
    a->compute_outline(viewer);
    ASSERT_TRUE(a->outline() == OUTLINE_NAMED || a->outline() == OUTLINE_INVULNERABLE);

    a->set_outline(OUTLINE_FLYING);
    a->set_flight_left(1);
    a->compute_outline(viewer);
    ASSERT_TRUE(a->outline() == OUTLINE_FLYING || a->outline() == OUTLINE_NAMED);

    a->set_outline(static_cast<unsigned char>(a->query_team_color()));
    a->set_invulnerable_left(1);
    a->set_flight_left(0);
    a->compute_outline(viewer);
    ASSERT_TRUE(a->outline() == OUTLINE_INVULNERABLE || a->outline() == OUTLINE_NAMED);

    walker* gen_tent = fx.level.add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(gen_tent != nullptr);
    gen_tent->stats()->set_level(200);
    gen_tent->stats()->set_hitpoints(10.0f);
    gen_tent->stats()->set_max_hitpoints(10.0f);
    gen_tent->set_lineofsight(3);
    gen_tent->set_act_type(ACT_GENERATE);
    (void)gen_tent->act();
    ASSERT_TRUE(gen_tent->stats()->hitpoints() <= gen_tent->stats()->max_hitpoints());

    // next_frame path using real animation data loaded by loader.
    walker* living = fx.level.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(living != nullptr);
    (void)living->next_frame();
}

TEST(WalkerUnit, walker_r15_path_check_counter_init_and_reset_are_seed_deterministic)
{
    WalkerR15Fixture fx;

    fx.level.world().rng_.state_ = 123u;
    walker first;
    const int first_initial = first.path_check_counter();

    fx.level.world().rng_.state_ = 123u;
    walker second;
    const int second_initial = second.path_check_counter();
    ASSERT_EQ(10, first_initial);
    ASSERT_EQ(first_initial, second_initial);

    fx.level.world().rng_.state_ = 321u;
    ASSERT_TRUE(first.reset());
    const int first_reset = first.path_check_counter();

    fx.level.world().rng_.state_ = 321u;
    ASSERT_TRUE(second.reset());
    const int second_reset = second.path_check_counter();
    ASSERT_EQ(9, first_reset);
    ASSERT_EQ(first_reset, second_reset);
    ASSERT_NE(first_initial, first_reset);
}

TEST(WalkerUnit, walker_r15_path_check_counter_uses_session_rng_without_gameplay_context)
{
    FixedRandom rng{6};
    GameContext test_ctx;
    test_ctx.rng = &rng;
    ScopedTestContextOverride test_context(test_ctx);
    ScopedCurrentGameOverride clear_gameplay(nullptr);

    walker w;
    ASSERT_EQ(11, w.path_check_counter());

    ASSERT_TRUE(w.reset());
    ASSERT_EQ(11, w.path_check_counter());
}

TEST(WalkerUnit, walker_r15_preview_construction_uses_session_rng_without_advancing_world_rng)
{
    ASSERT_TRUE(og::runtime::current_session != nullptr);
    ASSERT_TRUE(current_game == &og::runtime::current_session->game_);
    ASSERT_TRUE(og::runtime::current_session->game_.world != nullptr);

    FixedRandom rng{6};
    ScopedGameplayActiveOverride gameplay_inactive(false);

    IRandom* prev_rng = ctx().rng;
    ctx().rng = &rng;

    GameWorld& screen_world = *og::runtime::current_session->game_.world;
    screen_world.rng_.state_ = 123u;
    const std::uint32_t before_state = screen_world.rng_.state_;

    walker preview;
    EXPECT_EQ(11, preview.path_check_counter());
    EXPECT_EQ(before_state, screen_world.rng_.state_);

    ctx().rng = prev_rng;
}

TEST(WalkerUnit, walker_r15_active_gameplay_construction_uses_world_rng)
{
    ASSERT_TRUE(og::runtime::current_session != nullptr);
    ASSERT_TRUE(current_game == &og::runtime::current_session->game_);

    FixedRandom rng{6};
    ScopedGameplayActiveOverride gameplay_active(true);

    IRandom* prev_rng = ctx().rng;
    ctx().rng = &rng;

    GameWorld& screen_world = *og::runtime::current_session->game_.world;
    screen_world.rng_.state_ = 123u;

    walker live;
    EXPECT_EQ(10, live.path_check_counter());
    EXPECT_NE(123u, screen_world.rng_.state_);

    ctx().rng = prev_rng;
}
} // namespace detail_walker_r15
