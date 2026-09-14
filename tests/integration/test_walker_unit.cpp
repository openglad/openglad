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
#include <cmath>
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

// Count the events of one kind in a log: the SAVE_ALL oracle below must be
// blind to unrelated sound/notification pushes but exact about EndGame.
int count_events(const og::sim::SimEventLog& log, og::sim::EventKind kind)
{
    int n = 0;
    for (const auto& e : log.events())
        if (e.kind == kind)
            ++n;
    return n;
}

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
    // The two distance measures are deliberately different rules:
    // distance_to_ob is MANHATTAN (|dx| + |dy|) and body-size blind, while
    // distance_to_ob_center is the SQUARED separation of the two body centers.
    a->setxy(64, 64);
    b->setxy(96, 64);
    ASSERT_EQ(32, a->distance_to_ob(b)) << "|96-64| + |64-64| == 32";
    ASSERT_EQ(1024, a->distance_to_ob_center(b)) << "32*32 + 0*0 == 1024";

    b->setxy(96, 88);
    ASSERT_EQ(56, a->distance_to_ob(b))
        << "Manhattan adds the two axes (32 + 24), it does not hypotenuse them";
    ASSERT_EQ(1600, a->distance_to_ob_center(b)) << "32*32 + 24*24 == 1600";

    // Only the center measure shifts when the target's body grows.
    b->set_sizex(32);
    ASSERT_EQ(56, a->distance_to_ob(b)) << "Manhattan is body-size blind";
    ASSERT_EQ(2176, a->distance_to_ob_center(b))
        << "the +(32-16)/2 center offset makes it (32+8)^2 + 24^2";
    b->set_sizex(16);
    b->setxy(96, 64);

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
    ASSERT_EQ(1, count_events(fx.events, og::sim::EventKind::EndGame))
        << "a named team-0 living dying in a SAVE_ALL level ends the mission";
    const og::sim::Event* end_game = nullptr;
    for (const auto& e : fx.events.events())
        if (e.kind == og::sim::EventKind::EndGame)
            end_game = &e;
    ASSERT_NE(nullptr, end_game);
    EXPECT_EQ(static_cast<std::uint32_t>(SCEN_TYPE_SAVE_ALL), end_game->a)
        << "the ending type names the SAVE_ALL loss";
    EXPECT_EQ(static_cast<std::uint32_t>(-1), end_game->b)
        << "no next level is chosen by a mission loss";

    // Negative control: an UNNAMED walker on the same team is scenery, and
    // its death must not fail the mission.
    fx.events.clear();
    walker* nameless = add_living(fx, FAMILY_SKELETON, 0);
    ASSERT_NE(nullptr, nameless);
    nameless->stats()->name.clear();
    nameless->set_dead(1);
    ASSERT_TRUE(nameless->death());
    ASSERT_EQ(0, count_events(fx.events, og::sim::EventKind::EndGame))
        << "an unnamed casualty must not end a SAVE_ALL mission";

    walker misc;
    misc.set_order_family(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(misc.fire_check(1, 0));
    // Only a treasure can be eaten: the base walker refuses every eater,
    // including none at all.
    ASSERT_FALSE(misc.eat_me(nullptr)) << "a non-treasure is never consumed";
    ASSERT_FALSE(misc.eat_me(&misc)) << "a real eater does not change that";
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
    ASSERT_TRUE(a->myguy == nullptr) << "moving a myguy we do not own is a no-op";

    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    a->move_myguy_to(b);
    ASSERT_TRUE(a->myguy == nullptr);
    ASSERT_TRUE(b->myguy != nullptr);

    a->set_myguy_view(b->myguy);
    a->move_myguy_to(b);
    ASSERT_TRUE(a->myguy == nullptr);

    // init_fire aims first: a request in another direction records the wanted
    // facing in enddir, and the player-controlled walker refuses to auto-turn
    // (the control stick owns its facing), so the shot does not start.
    a->set_curdir(FACE_LEFT);
    a->set_enddir(FACE_LEFT);
    a->set_act_type(ACT_CONTROL);
    a->set_busy(0.0f);
    ASSERT_FALSE(a->init_fire(1, 0)) << "ACT_CONTROL never auto-turns to fire";
    ASSERT_EQ(FACE_RIGHT, (int)a->enddir())
        << "the wanted facing is still recorded for the player's own turn";
    ASSERT_EQ(FACE_LEFT, (int)a->curdir()) << "the refusal leaves us facing as we were";
    ASSERT_FLOAT_EQ(0.0f, a->busy()) << "a refused shot costs no firing delay";

    // The busy gate, reached only once facing already matches: a walker still
    // recovering from its last shot refuses and is charged nothing.
    a->set_curdir(FACE_RIGHT);
    a->set_enddir(FACE_RIGHT);
    a->set_act_type(ACT_RANDOM);
    a->set_ani_type(ANI_WALK);
    a->set_fire_frequency(4.0f);
    a->set_busy(1.0f);
    ASSERT_FALSE(a->init_fire(1, 0)) << "a busy walker cannot start a shot";
    ASSERT_FLOAT_EQ(1.0f, a->busy()) << "the refusal adds no fire_frequency";
    ASSERT_EQ(ANI_WALK, a->ani_type()) << "the refusal starts no attack animation";

    // Free and already facing the target: the ANI_WALK arm charges exactly one
    // fire_frequency, restarts the cycle at 0 and animates the first attack
    // frame (cycle 0 -> 1).
    a->set_busy(0.0f);
    a->set_curdir(FACE_DOWN);
    a->set_enddir(FACE_DOWN);
    a->set_ani_type(ANI_WALK);
    a->set_cycle(7);
    assign_basic_ani(a);
    ASSERT_TRUE(a->init_fire(0, 1)) << "a free, aimed walker starts its shot";
    ASSERT_EQ(ANI_ATTACK, a->ani_type()) << "ANI_WALK swaps to ANI_ATTACK";
    ASSERT_FLOAT_EQ(4.0f, a->busy()) << "init_fire charges exactly one fire_frequency";
    ASSERT_EQ(1, (int)a->cycle())
        << "the attack restarts at cycle 0 and animate() advances it one frame";
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

    // set_weapon_heading reads the OWNER's lastx/lasty (never curdir) and
    // writes both the weapon's spawn cell and its flight vector. The waver
    // term is world-rng next(stepsize/2 + 1) - (stepsize/2)/2, which is
    // exactly 0 for a stepsize-1 weapon, so every value below is fixed.
    walker* weapon = add_ob(fx, Order::Weapon, FAMILY_KNIFE, 0, 70, 70);
    ASSERT_NE(nullptr, weapon);
    ASSERT_FLOAT_EQ(1.0f, weapon->stepsize());
    shooter->setxy(80, 80);
    struct HeadingCase {
        float dx, dy;       // owner heading
        int ex, ey;         // expected weapon spawn cell
        float lastx, lasty; // expected weapon flight vector
        const char* name;
    };
    const HeadingCase headings[8] = {
        {  0.0f, -1.0f, 80, 63,  0.0f, -1.0f, "FACE_UP" },
        {  1.0f, -1.0f, 97, 63,  1.0f, -1.0f, "FACE_UP_RIGHT" },
        {  1.0f,  0.0f, 97, 80,  1.0f,  0.0f, "FACE_RIGHT" },
        {  1.0f,  1.0f, 97, 97,  1.0f,  1.0f, "FACE_DOWN_RIGHT" },
        {  0.0f,  1.0f, 80, 97,  0.0f,  1.0f, "FACE_DOWN" },
        { -1.0f,  1.0f, 63, 97, -1.0f,  1.0f, "FACE_DOWN_LEFT" },
        { -1.0f,  0.0f, 63, 80, -1.0f,  0.0f, "FACE_LEFT" },
        { -1.0f, -1.0f, 63, 63, -1.0f, -1.0f, "FACE_UP_LEFT" },
    };
    for (const auto& h : headings)
    {
        weapon->setxy(0, 0);
        weapon->set_lastx(99.0f);
        weapon->set_lasty(99.0f);
        shooter->set_lastx(h.dx);
        shooter->set_lasty(h.dy);
        shooter->set_weapon_heading(weapon);
        EXPECT_EQ(h.ex, static_cast<int>(weapon->xpos())) << h.name << " spawn x";
        EXPECT_EQ(h.ey, static_cast<int>(weapon->ypos())) << h.name << " spawn y";
        EXPECT_FLOAT_EQ(h.lastx, weapon->lastx()) << h.name << " flight x";
        EXPECT_FLOAT_EQ(h.lasty, weapon->lasty()) << h.name << " flight y";
    }

    // get_current_angle is a fixed facing -> radians table, with 0 as the
    // fallback for an out-of-range facing.
    const float expected_angle[8] = {
        -static_cast<float>(M_PI_2),      // FACE_UP
        -static_cast<float>(M_PI_4),      // FACE_UP_RIGHT
        0.0f,                             // FACE_RIGHT
        static_cast<float>(M_PI_4),       // FACE_DOWN_RIGHT
        static_cast<float>(M_PI_2),       // FACE_DOWN
        static_cast<float>(3 * M_PI_4),   // FACE_DOWN_LEFT
        static_cast<float>(M_PI),         // FACE_LEFT
        static_cast<float>(5 * M_PI_4),   // FACE_UP_LEFT
    };
    for (int d = 0; d < 8; ++d)
    {
        shooter->set_curdir(static_cast<char>(d));
        EXPECT_FLOAT_EQ(expected_angle[d], shooter->get_current_angle())
            << "facing " << d << " must map to its own angle";
    }
    shooter->set_curdir(120);
    EXPECT_FLOAT_EQ(0.0f, shooter->get_current_angle())
        << "an out-of-range facing uses the default angle";
}

TEST(WalkerUnit, walker_r11_act_animate_and_misc_paths)
{
    WalkerR11Fixture fx;
    walker* w = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_ob(fx, Order::Living, FAMILY_ORC, 1, 96, 64);
    ASSERT_TRUE(w && foe);

    // animate guards
    w->ani = nullptr;
    ASSERT_FALSE(w->animate()) << "a walker with no animation table cannot animate";

    // One mid-sequence ANI_ATTACK step: the cycle advances by exactly one and
    // the animation is still running, so the attack has not released yet.
    assign_basic_ani(w);
    w->set_ani_type(ANI_ATTACK);
    w->set_curdir(FACE_RIGHT);
    w->set_cycle(0);
    w->stats()->set_magicpoints(100.0f);
    w->stats()->set_weapon_cost(1);
    w->set_lastx(1);
    w->set_lasty(0);
    w->set_foe(foe);
    ASSERT_TRUE(w->animate()) << "a mid-sequence animate reports the animation running";
    ASSERT_EQ(ANI_ATTACK, w->ani_type()) << "the attack is not finished after one frame";
    ASSERT_EQ(1, static_cast<int>(w->cycle()))
        << "animate advances the cycle by exactly one frame";

    // set_act_type banks whatever we were doing as old_act_type - it is the
    // setter, not the caller, that remembers - and restore_act_type goes back
    // to it.
    w->set_act_type(ACT_GUARD);
    ASSERT_EQ(ACT_GUARD, w->act_type());
    w->set_act_type(ACT_CONTROL);
    ASSERT_EQ(ACT_CONTROL, w->act_type());
    ASSERT_EQ(ACT_GUARD, w->old_act_type())
        << "set_act_type banks the act it replaced, whatever was in old_act_type before";
    ASSERT_EQ(ACT_GUARD, w->restore_act_type())
        << "restore_act_type returns the act it restored";
    ASSERT_EQ(ACT_GUARD, w->act_type()) << "and puts it back on the walker";

    // collide records who we hit.
    ASSERT_TRUE(w->collide(foe));
    ASSERT_EQ(foe, w->collide_ob()) << "collide records the object we ran into";

    // spaces_clear counts the passable neighbours of the eight cells around
    // us, never our own cell.
    foe->setxy(300, 300);
    w->setxy(96, 96);
    ASSERT_EQ(8, w->spaces_clear()) << "all eight neighbours of open ground are clear";
    fx.level.world().grid.data[static_cast<std::size_t>(6 * fx.level.world().grid.w + 7)] =
        PIX_TREE_M1;
    ASSERT_EQ(7, w->spaces_clear()) << "one blocked neighbour drops the count by one";

    // center_on puts OUR center on the target's center, so the offset is the
    // difference of the two half-sizes.
    foe->set_sizex(32);
    foe->set_sizey(8);
    w->center_on(foe);
    ASSERT_EQ(308, w->xpos()) << "300 + 32/2 - 16/2";
    ASSERT_EQ(296, w->ypos()) << "300 + 8/2 - 16/2";

    // Difficulty scaling: a generator's post strength IS its HP bar
    // denominator, and both take the percentage.
    fx.level.world().difficulty = 150;
    w->set_order_family(Order::Generator, FAMILY_TENT);
    w->set_difficulty(5);
    ASSERT_FLOAT_EQ(750.0f, w->stats()->hitpoints()) << "100 * 5 * 150 / 100";
    ASSERT_FLOAT_EQ(750.0f, w->stats()->max_hitpoints())
        << "a generator's max HP tracks its strength, or its bar has no fraction";

    // A non-generator scales only if it is NOT a player character (team 0).
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_team_num(0);
    w->stats()->set_max_hitpoints(200.0f);
    w->set_damage(10.0f);
    w->set_difficulty(2);
    ASSERT_FLOAT_EQ(200.0f, w->stats()->max_hitpoints())
        << "difficulty never scales a player character";
    ASSERT_FLOAT_EQ(10.0f, w->damage()) << "difficulty never scales a player character";
    w->set_team_num(1);
    w->set_difficulty(2);
    ASSERT_FLOAT_EQ(300.0f, w->stats()->max_hitpoints()) << "200 * 150 / 100";
    ASSERT_FLOAT_EQ(15.0f, w->damage()) << "10 * 150 / 100";

    // Alliance follows the OWNER CHAIN: a walker we own is friendly to us even
    // though its own team colour differs, and stops being so the moment the
    // chain is cut.
    walker* owned = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 100, 100);
    owned->set_owner(w);
    w->set_owner(w); // self-loop guard branch: the chain walk must terminate
    ASSERT_EQ(0, w->is_friendly(nullptr)) << "a null target is never friendly";
    ASSERT_EQ(1, w->is_friendly(owned))
        << "the owner chain resolves both heads to us, whatever the owned team";
    owned->set_owner(nullptr);
    ASSERT_EQ(0, w->is_friendly(owned))
        << "with the chain cut, team 1 and team 0 are enemies again";
    owned->set_owner(w);
    w->set_dead(1);
    ASSERT_EQ(0, w->is_friendly_to_team(0)) << "a corpse is friendly to nobody";
    ASSERT_EQ(0, w->is_friendly_to_team(1)) << "not even to its own colour";

    // Team colour is the only alliance authority: neither allied_mode nor a
    // saved-character myguy pointer makes different colours friendly.
    w->set_dead(0);
    w->set_owner(nullptr);
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    fx.level.world().allied_mode = 1;
    ASSERT_EQ(0, w->is_friendly_to_team(0))
        << "allied_mode plus a myguy must not befriend another colour";
    ASSERT_EQ(1, w->is_friendly_to_team(1)) << "our own colour stays friendly";

    // do_summon/check_special fallback and eat_me logging path
    ASSERT_TRUE(w->do_summon(1, 1) == nullptr);
    ASSERT_TRUE(!w->check_special());
    ASSERT_FALSE(w->eat_me(foe)) << "a living is not food";
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
    const int team_color = static_cast<int>(shooter->query_team_color());

    // Block 1: INVULNERABLE + flight -> FLYING, then invisibility hides the
    // marker under the plain team color.
    shooter->set_outline(OUTLINE_INVULNERABLE);
    shooter->set_flight_left(1);
    shooter->set_invisibility_left(1);
    shooter->set_invulnerable_left(0);
    shooter->compute_outline(viewer);
    ASSERT_EQ(team_color, static_cast<int>(shooter->outline()))
        << "an invisible flyer shows nothing but its team color";

    // Same block WITHOUT invisibility: the flying marker survives.
    shooter->set_outline(OUTLINE_INVULNERABLE);
    shooter->set_invisibility_left(0);
    shooter->compute_outline(viewer);
    ASSERT_EQ(static_cast<int>(OUTLINE_FLYING), static_cast<int>(shooter->outline()))
        << "INVULNERABLE + flight -> FLYING when nothing is hiding it";

    // Block 2: FLYING with the flight expired but invulnerability running,
    // and invisibility back on -> team color again.
    shooter->set_outline(OUTLINE_FLYING);
    shooter->set_flight_left(0);
    shooter->set_invulnerable_left(1);
    shooter->set_invisibility_left(1);
    shooter->compute_outline(viewer);
    ASSERT_EQ(team_color, static_cast<int>(shooter->outline()))
        << "invisibility wins over invulnerability in the FLYING arm";

    // Same block without invisibility: the invulnerable marker shows.
    shooter->set_outline(OUTLINE_FLYING);
    shooter->set_invisibility_left(0);
    shooter->compute_outline(viewer);
    ASSERT_EQ(static_cast<int>(OUTLINE_INVULNERABLE), static_cast<int>(shooter->outline()))
        << "FLYING + invulnerability (no flight, no invisibility) -> INVULNERABLE";

    // Block 3: NAMED + invisibility -> team color.
    shooter->set_outline(OUTLINE_NAMED);
    shooter->set_invisibility_left(1);
    shooter->stats()->set_bit_flags(BIT_NAMED, 1);
    shooter->compute_outline(viewer);
    ASSERT_EQ(team_color, static_cast<int>(shooter->outline()))
        << "an invisible named walker shows only its team color";

    // Block 4: no marker timers and BIT_NAMED off -> no outline at all.
    // mark_player_controls defaults to false, so a same-team player control
    // gets no courtesy outline either.
    shooter->set_outline(shooter->query_team_color());
    shooter->stats()->set_bit_flags(BIT_NAMED, 0);
    shooter->set_invulnerable_left(0);
    shooter->set_invisibility_left(0);
    shooter->set_flight_left(0);
    shooter->set_user(0);
    viewer->set_team_num(shooter->team_num());
    shooter->compute_outline(viewer);
    ASSERT_EQ(0, static_cast<int>(shooter->outline()))
        << "nothing to mark means outline 0, not the team color";
    // ... unless the caller opts into the networked player-control marker.
    shooter->set_outline(shooter->query_team_color());
    shooter->compute_outline(viewer, true);
    ASSERT_EQ(team_color, static_cast<int>(shooter->outline()))
        << "mark_player_controls paints a same-team peer's control";

    // float_eq is a tolerance compare, not an equality alias.
    ASSERT_TRUE(float_eq(1.0f, 1.0f));
    ASSERT_TRUE(float_eq(1.0000001f, 1.0f)) << "a sub-tolerance difference compares equal";
    ASSERT_FALSE(float_eq(1.0f, 1.001f)) << "a difference above tolerance must not compare equal";
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

    // ACT_GENERATE's spawn cadence is one comparison of two draws:
    // next(level*3) * rate > next(300 + living*8) * 100. Seeded so the first
    // draw loses, a generator tick does nothing at all.
    w->set_act_type(ACT_GENERATE);
    w->stats()->set_level(50);
    w->stats()->set_hitpoints(10.0f);
    w->stats()->set_max_hitpoints(20.0f);
    w->set_ani_type(ANI_WALK);
    w->set_busy(0.0f);
    w->set_curdir(FACE_LEFT);
    w->set_enddir(FACE_LEFT);
    w->set_lastx(0.0f);
    w->set_lasty(0.0f);
    w->stats()->clear_command();
    fx.level.world().living_count = 0;
    fx.level.world().generator_rate = 100;
    fx.level.world().rng_.state_ = 1u; // next(150) == 38, next(300) == 126
    ASSERT_FALSE(w->act()) << "the ACT_GENERATE arm breaks out of the switch, returning 0";
    ASSERT_FLOAT_EQ(10.0f, w->stats()->hitpoints())
        << "a losing cadence draw spawns nothing and regenerates nothing";
    ASSERT_EQ(ANI_WALK, w->ani_type()) << "and starts no firing animation";
    ASSERT_EQ(2524885223u, fx.level.world().rng_.state_)
        << "a losing tick still costs exactly the two cadence draws";

    // Seeded so the first draw wins: the spawn heading is (1 - next(3)) per
    // axis, the generator starts its firing animation, and the post heals
    // exactly one HP (capped at max).
    fx.level.world().rng_.state_ = 2u; // 76 > 17, then next(3) == 2, next(3) == 1
    ASSERT_FALSE(w->act()) << "a spawning ACT_GENERATE tick still returns 0";
    ASSERT_FLOAT_EQ(-1.0f, w->lastx()) << "lastx = 1 - next(3) == -1";
    ASSERT_FLOAT_EQ(0.0f, w->lasty()) << "lasty = 1 - next(3) == 0";
    ASSERT_EQ(ANI_ATTACK, w->ani_type()) << "a spawning generator starts its fire animation";
    ASSERT_FLOAT_EQ(11.0f, w->stats()->hitpoints())
        << "a spawn tick regenerates exactly one HP while under max";
    ASSERT_EQ(2993822286u, fx.level.world().rng_.state_)
        << "cadence pair plus the two heading draws, in that order";

    // ACT_RANDOM, 3 times in 4: keep hunting with a long COMMAND_SEARCH.
    w->set_ani_type(ANI_WALK);
    w->set_act_type(ACT_RANDOM);
    w->set_foe(foe);
    w->stats()->clear_command();
    fx.level.world().rng_.state_ = 1u; // next(4) == 2, so not the random walk
    ASSERT_TRUE(w->act()) << "the search arm reports it acted";
    ASSERT_EQ(1u, w->stats()->commands.size()) << "exactly one order is queued";
    EXPECT_EQ(COMMAND_SEARCH, w->stats()->commands.front().commandtype)
        << "a foe in hand means search, not wander";
    EXPECT_EQ(500, w->stats()->commands.front().commandcount)
        << "the search runs for 500 ticks";
    EXPECT_EQ(1103527590u, fx.level.world().rng_.state_)
        << "the search arm costs exactly one draw";

    // 1 in 4, then 1 in 20: try the family special first, and only wander when
    // there is no special to cast. Duration and heading come from the same
    // stream (their draw ORDER is the compiler's argument order, so the stream
    // position is pinned rather than the individual parameters).
    w->set_specials_disabled(true);
    w->stats()->clear_command();
    fx.level.world().rng_.state_ = 189u; // next(4) == 0, next(20) == 0
    ASSERT_TRUE(w->act()) << "the random-walk arm reports it acted";
    ASSERT_EQ(1u, w->stats()->commands.size()) << "exactly one order is queued";
    EXPECT_EQ(COMMAND_WALK, w->stats()->commands.front().commandtype)
        << "the 1-in-80 arm wanders instead of searching";
    EXPECT_EQ(62712830u, fx.level.world().rng_.state_)
        << "two gate draws plus the walk's duration and two heading draws";
    w->set_specials_disabled(false);

    // A frozen walker burns exactly one freeze tick and returns before the
    // busy drain and before its act arm runs.
    w->set_ani_type(ANI_WALK);
    w->stats()->clear_command();
    w->set_act_type(ACT_CONTROL);
    w->stats()->set_frozen_delay(2);
    w->set_busy(3.0f);
    ASSERT_TRUE(w->act());
    ASSERT_EQ(1, static_cast<int>(w->stats()->frozen_delay()))
        << "act() decrements frozen_delay by exactly one";
    ASSERT_FLOAT_EQ(3.0f, w->busy())
        << "a frozen act() returns before the busy drain";

    // Unfrozen: busy drains by 1.0, attack_lunge by 0.4, hit_recoil by 0.6.
    w->stats()->set_frozen_delay(0);
    w->set_attack_lunge(1.0f);
    w->set_hit_recoil(1.0f);
    ASSERT_TRUE(w->act());
    ASSERT_FLOAT_EQ(2.0f, w->busy()) << "busy drains by exactly 1.0 per act";
    ASSERT_FLOAT_EQ(0.6f, w->attack_lunge())
        << "attack_lunge decays by exactly 0.4 per unfrozen act";
    ASSERT_FLOAT_EQ(0.4f, w->hit_recoil())
        << "hit_recoil decays by exactly 0.6 per unfrozen act";

    // Completing a SKEL_GROW animation drops the walker back to the walk cycle.
    w->set_order_family(Order::Living, FAMILY_SKELETON);
    w->set_ani_type(ANI_SKEL_GROW);
    w->set_curdir(FACE_RIGHT);
    w->set_cycle(8); // past the end of the test sequence
    ASSERT_TRUE(w->animate());
    ASSERT_EQ(ANI_WALK, w->ani_type())
        << "a finished SKEL_GROW returns to the walk animation";
    ASSERT_EQ(0, static_cast<int>(w->cycle()));

    // A mage's TELE_OUT completion runs the family teleport handler, which
    // re-enters the TELE_IN animation.
    w->set_order_family(Order::Living, FAMILY_MAGE);
    w->set_ani_type(ANI_TELE_OUT);
    w->set_cycle(8);
    fx.level.world().rng_.state_ = 11u;
    ASSERT_TRUE(w->animate());
    ASSERT_EQ(ANI_TELE_IN, w->ani_type())
        << "the mage teleport handler swaps TELE_OUT for TELE_IN";
    ASSERT_EQ(0, static_cast<int>(w->cycle()));

    // A family with no teleport handler just stops: back to walk, reports 0.
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_ani_type(ANI_TELE_OUT);
    w->set_cycle(8);
    ASSERT_FALSE(w->animate())
        << "the default TELE_OUT arm reports the animation stopped";
    ASSERT_EQ(ANI_WALK, w->ani_type());
    ASSERT_EQ(0, static_cast<int>(w->cycle()));
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
    ASSERT_EQ(static_cast<int>(w->query_team_color()), static_cast<int>(w->outline()))
        << "INVULNERABLE + flight becomes FLYING, then invisibility hides it";

    w->set_outline(w->query_team_color());
    w->set_invulnerable_left(0);
    w->set_flight_left(1);
    w->compute_outline(view);
    ASSERT_EQ(static_cast<int>(OUTLINE_FLYING), static_cast<int>(w->outline()))
        << "team color + flight (no invulnerability) -> FLYING";

    // act() on a frozen walker burns exactly one freeze tick.
    w->set_act_type(ACT_CONTROL);
    w->stats()->set_frozen_delay(1);
    w->set_busy(4.0f);
    ASSERT_TRUE(w->act());
    ASSERT_EQ(0, static_cast<int>(w->stats()->frozen_delay()))
        << "act() decrements frozen_delay by exactly one";
    ASSERT_FLOAT_EQ(4.0f, w->busy())
        << "the frozen arm returns before the busy drain";

    // Unfrozen, busy drains by exactly 1.0 per act.
    w->set_busy(1.0f);
    ASSERT_TRUE(w->act());
    ASSERT_FLOAT_EQ(0.0f, w->busy()) << "busy drains by exactly 1.0 per act";
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

    // A finished TELE_OUT on a family with no teleport handler simply stops:
    // back to the walk cycle, and the animation reports itself over.
    w->set_ani_type(ANI_TELE_OUT);
    w->set_cycle(4);
    w->set_curdir(FACE_RIGHT);
    ASSERT_FALSE(w->animate())
        << "the default TELE_OUT arm reports the animation stopped";
    ASSERT_EQ(ANI_WALK, w->ani_type()) << "and settles back on the walk animation";
    ASSERT_EQ(0, w->cycle()) << "restarting the walk cycle from frame 0";

    // ACT_FIRE burns exactly one unit of the weapon's remaining range per tick.
    w->set_ani_type(ANI_WALK);
    w->set_act_type(ACT_FIRE);
    w->set_lineofsight(6);
    ASSERT_TRUE(w->act()) << "a weapon's act always reports it acted";
    ASSERT_EQ(5, w->lineofsight()) << "each ACT_FIRE tick spends one unit of range";

    // ACT_GUARD with nobody to find: no foe, no order, and act() falls out of
    // the switch with 0.
    w->set_act_type(ACT_GUARD);
    w->set_foe(nullptr);
    w->stats()->clear_command();
    ASSERT_FALSE(w->act()) << "the ACT_GUARD arm breaks, so act() returns 0";
    ASSERT_EQ(nullptr, w->foe()) << "an empty level offers the guard no foe";
    ASSERT_FALSE(w->stats()->has_commands())
        << "a guard with no foe issues no order";

    // An enemy inside the guard's sight: it acquires the foe, turns to face
    // it, WAKES to ACT_RANDOM and queues its parting shot at the foe's delta.
    walker* intruder = add_ob(fx, Order::Living, FAMILY_ORC, 1, 128, 96);
    ASSERT_NE(nullptr, intruder);
    w->set_lineofsight(6);
    w->set_act_type(ACT_GUARD);
    w->set_curdir(FACE_UP);
    w->stats()->clear_command();
    ASSERT_FALSE(w->act()) << "the guard arm still returns 0";
    ASSERT_EQ(intruder, w->foe()) << "the guard acquires the nearest foe";
    ASSERT_EQ(FACE_RIGHT, (int)w->curdir()) << "and turns to face it";
    ASSERT_EQ(ACT_RANDOM, w->act_type())
        << "a guard that genuinely sights a foe wakes instead of staying a statue";
    ASSERT_EQ(1u, w->stats()->commands.size()) << "exactly one order is queued";
    EXPECT_EQ(COMMAND_FIRE, w->stats()->commands.front().commandtype)
        << "the parting order is a shot";
    EXPECT_EQ(32, w->stats()->commands.front().com1)
        << "aimed by the foe's x delta, not due north";
    EXPECT_EQ(0, w->stats()->commands.front().com2) << "and its y delta";
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
    // stepsize 1 makes the waver term exactly 0 (next(1) - 0), so the spawn
    // cell and flight vector below are fully determined.
    weapon->set_sizex(16);
    weapon->set_sizey(16);
    weapon->set_stepsize(1.0f);

    gen_tower->set_lastx(-1.0f);
    gen_tower->set_lasty(0.0f);
    gen_tower->set_weapon_heading(weapon);
    ASSERT_FLOAT_EQ(-1.0f, weapon->lastx())
        << "FACE_LEFT flies at exactly -weapon stepsize";
    ASSERT_FLOAT_EQ(0.0f, weapon->lasty())
        << "the waver is 0 for a stepsize-1 weapon";
    ASSERT_EQ(64 - 16 - 1, static_cast<int>(weapon->xpos()))
        << "a left-thrown weapon starts one pixel west of the owner";
    ASSERT_EQ(64, static_cast<int>(weapon->ypos()))
        << "equal sizes centre the weapon on the owner's row";

    gen_tower->set_lastx(0.0f);
    gen_tower->set_lasty(1.0f);
    gen_tower->set_weapon_heading(weapon);
    ASSERT_FLOAT_EQ(1.0f, weapon->lasty())
        << "FACE_DOWN flies at exactly +weapon stepsize";
    ASSERT_FLOAT_EQ(0.0f, weapon->lastx());
    ASSERT_EQ(64 + 16 + 1, static_cast<int>(weapon->ypos()))
        << "a down-thrown weapon starts one pixel south of the owner";
    ASSERT_EQ(64, static_cast<int>(weapon->xpos()));
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

    // A BIT_NAMED walker seen by the OTHER team always resolves to NAMED,
    // whichever marker it was wearing, as long as nothing hides it.
    a->set_outline(OUTLINE_INVULNERABLE);
    a->set_invulnerable_left(1);
    a->set_flight_left(0);
    a->set_invisibility_left(0);
    a->compute_outline(viewer);
    ASSERT_EQ(static_cast<int>(OUTLINE_NAMED), static_cast<int>(a->outline()))
        << "INVULNERABLE with no flight, seen by a foe, becomes NAMED";

    a->set_outline(OUTLINE_FLYING);
    a->set_flight_left(1);
    a->compute_outline(viewer);
    ASSERT_EQ(static_cast<int>(OUTLINE_NAMED), static_cast<int>(a->outline()))
        << "FLYING seen by a foe becomes NAMED";

    a->set_outline(static_cast<unsigned char>(a->query_team_color()));
    a->set_invulnerable_left(1);
    a->set_flight_left(0);
    a->compute_outline(viewer);
    ASSERT_EQ(static_cast<int>(OUTLINE_INVULNERABLE), static_cast<int>(a->outline()))
        << "the team-color arm checks invulnerability before the NAMED marker";

    // act_generate's cadence roll is `next(level*3) * rate > next(300 +
    // living_count*8) * 100`, drawn from the world LCG. Both outcomes are
    // pinned here: a losing roll neither fires nor heals, a winning roll
    // adds exactly one hitpoint while the generator is below max.
    walker* gen_tent = fx.level.add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(gen_tent != nullptr);
    gen_tent->stats()->set_level(200); // first draw bound 600
    gen_tent->stats()->set_hitpoints(5.0f);
    gen_tent->stats()->set_max_hitpoints(10.0f);
    gen_tent->set_lineofsight(3);
    gen_tent->set_act_type(ACT_GENERATE);
    gen_tent->stats()->set_magicpoints(9999.0f);
    ASSERT_EQ(2, fx.level.world().living_count)
        << "the second draw bound is 300 + 8*living_count == 316";

    gen_tent->set_ani_type(ANI_WALK);
    fx.level.world().rng_.state_ = 9u; // draws 72 then 315: the roll loses
    (void)gen_tent->act();
    ASSERT_FLOAT_EQ(5.0f, gen_tent->stats()->hitpoints())
        << "a generator that loses its cadence roll gains no hitpoint";

    gen_tent->set_ani_type(ANI_WALK);
    gen_tent->set_busy(0.0f);
    fx.level.world().rng_.state_ = 7u; // draws 132 then 10: the roll wins
    (void)gen_tent->act();
    ASSERT_FLOAT_EQ(6.0f, gen_tent->stats()->hitpoints())
        << "a firing generator adds exactly one hitpoint while below max";

    // next_frame wraps the frame index modulo the loaded frame count, and
    // refuses (0) rather than dividing by zero when there is no table.
    walker* living = fx.level.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(living != nullptr);
    short frame_count = 0; // walker::frames is protected; probe set_frame's gate
    while (frame_count < 256 && living->set_frame(frame_count) == 1)
        frame_count = static_cast<short>(frame_count + 1);
    ASSERT_NE(0, frame_count) << "the loader must give a soldier a frame table";
    living->set_direct_frame(static_cast<short>(frame_count + 2));
    ASSERT_EQ(1, living->next_frame())
        << "next_frame must re-seat an out-of-range frame, not refuse it";
    ASSERT_EQ(2 % frame_count, static_cast<int>(living->frame()))
        << "next_frame wraps the frame index modulo the table length";

    walker bare; // no render data attached: frames == 0
    ASSERT_EQ(0, bare.next_frame())
        << "a frameless walker must refuse instead of dividing by zero";
    ASSERT_EQ(0, static_cast<int>(bare.frame()));
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
