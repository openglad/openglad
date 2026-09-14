#include <openglad/core/constants.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gparser.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/families/family_descriptor.h>

#include "test_family_lookup.h"
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/game_context.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <gtest/gtest.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/resources/level_data_hooks.h>
#include <string>
#include <openglad/core/combat_math.h>
#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/sim_input_handler.h>
#include "test_gameplay_context_scope.h"
#include "test_family_hook_dispatch.h"

// --- From test_coverage_r17.cpp ---
namespace detail_coverage_r17 {
namespace {

struct SeqRandom final : IRandom {
    std::vector<std::uint32_t> vals;
    std::size_t i = 0;

    explicit SeqRandom(std::initializer_list<std::uint32_t> init)
        : vals(init)
    {}

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        if (vals.empty())
            return 0;
        const std::uint32_t v = vals[i % vals.size()] % max_exclusive;
        ++i;
        return v;
    }
};

struct R17Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;
    GameContext gc;

    R17Fixture()
        : gameplay(level, save, events, cfg)
    {
        init_family_registry();
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;

        push_test_context(&gc);
    }

    ~R17Fixture()
    {
        pop_test_context();
    }
};

living* add_living(R17Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_normal_stepsize(1.0f);
    w->set_lineofsight(8);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_fx(R17Fixture& fx, char family, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::FX, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_team_num(0);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
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

PixieData make_grid(unsigned char fill, int w = 9, int h = 9)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = static_cast<unsigned char>(w);
    pd.h = static_cast<unsigned char>(h);
    pd.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        pd.data[static_cast<std::size_t>(i)] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[static_cast<std::size_t>(x + y * pd.w)] = v;
}

} // namespace

TEST(CoverageMisc, coverage_r17_family_mage_specials_and_reactions)
{
    R17Fixture fx;
    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    ASSERT_TRUE(mage != nullptr);

    living* self = add_living(fx, FAMILY_MAGE, 0, 64, 64);
    ASSERT_TRUE(self != nullptr);
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    self->myguy->name = "R17 Mage";
    self->myguy->intelligence = 70;
    self->set_user(0);
    self->stats()->set_level(8);
    self->stats()->set_magicpoints(300.0f);

    self->set_current_special(1);
    self->set_shifter_down(1);
    ASSERT_TRUE(!og::test::do_special(*mage, self));

    self->myguy->intelligence = 90;
    walker* marker = add_fx(fx, FAMILY_MARKER, 66, 64);
    ASSERT_TRUE(marker != nullptr);
    marker->set_owner(self);
    marker->set_dead(0);
    self->set_busy(0);
    self->set_current_special(1);
    self->set_shifter_down(1);
    ASSERT_TRUE(og::test::do_special(*mage, self));

    self->set_busy(0);
    self->set_current_special(2);
    self->set_shifter_down(0);
    self->set_lastx(1.0f);
    self->set_lasty(0.0f);
    ASSERT_TRUE(og::test::do_special(*mage, self));

    living* foe = add_living(fx, FAMILY_ORC, 1, 96, 64);
    ASSERT_TRUE(foe != nullptr);
    self->stats()->set_hitpoints(self->stats()->max_hitpoints());
    self->stats()->set_level(5);
    self->set_foe(nullptr);
    foe->set_foe(nullptr);
    og::test::hit_response(*mage, self->stats(), foe);
    ASSERT_TRUE(self->foe() == foe);
    ASSERT_TRUE(foe->foe() == self);

    add_living(fx, FAMILY_ORC, 1, 100, 64);
    add_living(fx, FAMILY_ORC, 1, 104, 64);
    add_living(fx, FAMILY_ORC, 1, 108, 64);
    self->set_current_special(1);
    ASSERT_TRUE(og::test::check_special_ai(*mage, self));
}

TEST(CoverageMisc, coverage_r17_family_druid_protection_tree_and_faerie)
{
    R17Fixture fx;
    const FamilyDescriptor* druid = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(druid != nullptr);

    living* self = add_living(fx, FAMILY_DRUID, 0, 80, 80);
    living* ally1 = add_living(fx, FAMILY_SOLDIER, 0, 84, 80);
    living* ally2 = add_living(fx, FAMILY_SOLDIER, 0, 88, 80);
    ASSERT_TRUE(self && ally1 && ally2);

    self->set_owned_myguy(std::make_unique<guy>(FAMILY_DRUID));
    self->stats()->set_level(7);
    self->stats()->set_magicpoints(300.0f);
    self->set_busy(0);

    self->set_current_special(1);
    self->set_lastx(1.0f);
    self->set_lasty(0.0f);
    ASSERT_TRUE(og::test::do_special(*druid, self));

    walker* existing = fx.level.add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(existing != nullptr);
    existing->set_owner(ally2);
    existing->set_team_num(ally2->team_num());
    existing->setxy(ally2->xpos(), ally2->ypos());

    self->set_busy(0);
    self->set_current_special(4);
    ASSERT_TRUE(og::test::do_special(*druid, self));

    self->set_busy(0);
    self->set_current_special(2);
    self->setxy(0, 0);
    self->set_lastx(-1.0f);
    self->set_lasty(0.0f);
    (void)og::test::do_special(*druid, self);
}

TEST(CoverageMisc, coverage_r17_walker_movement_and_act_cleanup)
{
    R17Fixture fx;

    walker* actor = fx.level.add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(actor != nullptr);
    actor->setxy(static_cast<short>(64), static_cast<short>(64));
    actor->set_sizex(16);
    actor->set_sizey(16);
    actor->set_stepsize(1.0f);
    actor->set_team_num(0);

    living* dead_foe = add_living(fx, FAMILY_ORC, 1, 80, 64);
    living* dead_leader = add_living(fx, FAMILY_ORC, 1, 84, 64);
    living* dead_owner = add_living(fx, FAMILY_ORC, 1, 88, 64);
    ASSERT_TRUE(actor && dead_foe && dead_leader && dead_owner);
    dead_foe->set_dead(1);
    dead_leader->set_dead(1);
    dead_owner->set_dead(1);

    actor->set_foe(dead_foe);
    actor->set_leader(dead_leader);
    actor->set_owner(dead_owner);
    actor->set_ani_type(ANI_WALK);
    actor->set_attack_lunge(0.2f);
    actor->set_hit_recoil(0.3f);
    actor->stats()->clear_command();
    actor->stats()->set_frozen_delay(0);
    actor->set_act_type(ACT_CONTROL);
    actor->set_collide_ob(dead_foe);
    const unsigned char drawcycle_before = actor->drawcycle();
    // level.add_ob(Order::FX, ...) builds an `effect`, so effect::act runs
    // (not walker::act -- walker::act's lunge/recoil decay is pinned by the
    // sibling coverage_r18_walker_act_decay_cleanup_and_guard_branches).
    // effect::act nulls every dead pointer it holds and clears the collision
    // record BEFORE any branch and advances drawcycle so headless sims
    // animate without a renderer; mid-animation (ani_type != ANI_WALK) it
    // hands off to animate(), which steps the sequence one frame on and
    // answers 1 instead of expiring the fx.
    actor->set_ani_type(static_cast<char>(ANI_ATTACK));
    actor->set_cycle(0);
    EXPECT_TRUE(actor->act()) << "mid-animation, act hands off to animate()";
    EXPECT_EQ(1, static_cast<int>(actor->cycle()))
        << "animate() advanced exactly one frame";
    EXPECT_EQ(nullptr, actor->foe()) << "a dead foe is dropped";
    EXPECT_EQ(nullptr, actor->leader()) << "a dead leader is dropped";
    EXPECT_EQ(nullptr, actor->owner()) << "a dead owner is dropped";
    EXPECT_EQ(nullptr, actor->collide_ob())
        << "act starts each tick with no collision";
    EXPECT_EQ(static_cast<unsigned char>(drawcycle_before + 1),
              actor->drawcycle())
        << "the headless sim advances drawcycle itself";
    EXPECT_FALSE(actor->dead())
        << "mid-animation, the fx has not expired yet";

    // At ANI_WALK with nothing left to animate the fx expires, and the
    // shipped blast (packs/core/lib/effect_bomb.lua explosion_on_death)
    // attributes an ownerless explosion to itself.
    actor->set_ani_type(static_cast<char>(ANI_WALK));
    EXPECT_FALSE(actor->act()) << "an expiring fx answers 0";
    EXPECT_TRUE(actor->dead()) << "an fx with no animation left expires";
    EXPECT_EQ(actor, actor->owner())
        << "an ownerless explosion owns its own blast";
    actor->set_dead(0); // the movement legs below need a live walker
    actor->set_owner(nullptr);

    // walkstep answers 0 when the full step, the baby step AND the fallbacks
    // all leave the map, and it leaves the walker where it stood. Bottom-left
    // corner, heading down-left: both axes are off the map.
    assign_basic_ani(actor);
    const short bottom = static_cast<short>(fx.level.world().pixmaxy - 1);
    actor->set_user(-1);
    actor->setxy(static_cast<short>(0), bottom);
    actor->set_curdir(FACE_DOWN_LEFT);
    EXPECT_FALSE(actor->walkstep(-1.0f, 1.0f))
        << "npc leg: both cardinal fallbacks leave the map too";
    EXPECT_EQ(0, actor->xpos()) << "npc leg: did not move";
    EXPECT_EQ(bottom, actor->ypos()) << "npc leg: did not move";

    actor->set_user(0);
    actor->setxy(static_cast<short>(0), bottom);
    actor->set_curdir(FACE_DOWN_LEFT);
    EXPECT_FALSE(actor->walkstep(-1.0f, 1.0f))
        << "user leg: neither slide axis is passable";
    EXPECT_EQ(0, actor->xpos()) << "user leg: did not move";
    EXPECT_EQ(bottom, actor->ypos()) << "user leg: did not move";

    // turn(): distance = curdir - target = 127 - 2 = 125 >= 4, so the turn is
    // clockwise -- (127 + 1) % 8 == 0 == FACE_UP -- and the new facing rewrites
    // lastx/lasty from stepsize (FAMILY_EXPLOSION is Order::FX, so the
    // is_stationary branch is skipped).
    actor->set_curdir(127);
    actor->set_stepsize(2.0f);
    ASSERT_TRUE(actor->turn(FACE_RIGHT));
    EXPECT_EQ(FACE_UP, static_cast<int>(actor->curdir()))
        << "one clockwise step off 127 lands on FACE_UP";
    EXPECT_FLOAT_EQ(0.0f, actor->lastx()) << "FACE_UP has no x component";
    EXPECT_FLOAT_EQ(-2.0f, actor->lasty()) << "FACE_UP is -stepsize in y";
}

TEST(CoverageMisc, coverage_r17_smooth_grass_water_and_dark_variants)
{
    // smoother::smooth rewrites the CENTRE tile from its neighbour genres and
    // stores it, so every case is read back with query_x_y. The rng arms are
    // the only draws, in this order: grass variant, grass variant, dark
    // bottom, dark rubble.
    SeqRandom rng{1, 2, 0, 0, 1, 0};
    GameContext gc;
    gc.rng = &rng;
    push_test_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 4;
    const int y = 4;

    // Water at upleft+upright+downleft+up+left: the UL arm (no rng draw).
    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x - 1, y - 1, PIX_WATER1);
    set_at(pd, x + 1, y - 1, PIX_WATER1);
    set_at(pd, x - 1, y + 1, PIX_WATER1);
    set_at(pd, x, y - 1, PIX_WATER1);
    set_at(pd, x - 1, y, PIX_WATER1);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_GRASSWATER_UL, s.query_x_y(x, y)) << "water UL arm";

    // Cumulative: all eight neighbours are water now, and the FIRST arm in
    // source order (LL) is the one that wins.
    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x + 1, y - 1, PIX_WATER1);
    set_at(pd, x + 1, y + 1, PIX_WATER1);
    set_at(pd, x - 1, y + 1, PIX_WATER1);
    set_at(pd, x + 1, y, PIX_WATER1);
    set_at(pd, x, y + 1, PIX_WATER1);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_GRASSWATER_LL, s.query_x_y(x, y))
        << "all-water: the first arm (LL) wins";

    // All grass: grass_variants[next_random(4)], twice -> draws 1 then 2.
    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x - 1, y - 1, PIX_GRASS1);
    set_at(pd, x + 1, y - 1, PIX_GRASS1);
    set_at(pd, x - 1, y + 1, PIX_GRASS1);
    set_at(pd, x + 1, y + 1, PIX_GRASS1);
    set_at(pd, x, y - 1, PIX_GRASS1);
    set_at(pd, x, y + 1, PIX_GRASS1);
    set_at(pd, x - 1, y, PIX_GRASS1);
    set_at(pd, x + 1, y, PIX_GRASS1);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_GRASS2, s.query_x_y(x, y)) << "grass_variants[1]";
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_GRASS3, s.query_x_y(x, y)) << "grass_variants[2]";

    // Dark grass with trees at upleft+up: the "bottom middle" arm, whose
    // switch is on the genre BELOW. Grass below takes the grass case ->
    // grass_dark_bottom[next_random(2)] = [0], then next_random(20) draws 0
    // and the rubble overrides it.
    set_at(pd, x, y, PIX_GRASS_DARK_1);
    set_at(pd, x - 1, y - 1, PIX_TREE_B1);
    set_at(pd, x, y - 1, PIX_TREE_B1);
    set_at(pd, x, y + 1, PIX_GRASS1);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_GRASS_RUBBLE, s.query_x_y(x, y))
        << "bottom-middle over grass: the 1-in-20 rubble draw lands";

    pop_test_context();
}

TEST(CoverageMisc, coverage_r17_save_data_reset_defaults)
{
    SaveData save;
    save.current_campaign = "custom.campaign";
    save.completed_levels["custom.campaign"].insert(4);
    save.current_levels["custom.campaign"] = 9;
    save.team_size = 2;
    save.score = 123;
    save.totalcash = 456;
    save.totalscore = 789;
    save.scen_num = 9;
    save.my_team = 3;

    save.reset();

    ASSERT_TRUE(save.current_campaign == "gladiator");
    ASSERT_TRUE(save.team_size == 0);
    ASSERT_TRUE(save.scen_num == 1);
    ASSERT_TRUE(save.my_team == 0);
    ASSERT_TRUE(save.current_levels["gladiator"] == 1);
}
} // namespace detail_coverage_r17

// --- From test_coverage_r18.cpp ---

namespace detail_coverage_r18 {
namespace {

class MenuOnlyClient final : public og::ui::IPickerClient
{
public:
    std::vector<const og::ui::PickerMenuItem*> scripted;
    int present_calls = 0;
    int handle_calls = 0;
    int networking_calls = 0;

    const og::ui::PickerMenuItem* present_menu(og::ui::PickerMenuId) override
    {
        if (present_calls >= static_cast<int>(scripted.size()))
            return nullptr;
        return scripted[static_cast<size_t>(present_calls++)];
    }

    void handle_menu_item(og::ui::PickerMenuId, const og::ui::PickerMenuItem&) override
    {
        ++handle_calls;
    }

    bool configure_networking() override
    {
        ++networking_calls;
        return false;
    }

    std::string show_campaign_select() override { return {}; }
    void show_options() override {}
    void show_help() override {}
    void run_game() override {}
    bool load_game() override { return false; }
    bool save_game() override { return false; }
};

class ScriptedPickerClient final : public og::ui::IPickerClient
{
public:
    std::vector<og::ui::MainMenuAction> main_actions;
    size_t main_index = 0;
    og::ui::TeamBuildAction team_build_action = og::ui::TeamBuildAction::BackToMainMenu;
    std::string campaign_result;
    bool new_game_ok = true;
    bool networking_ok = true;
    bool load_ok = true;
    bool save_ok = true;
    og::ui::PickerScreen after_game = og::ui::PickerScreen::MainMenu;

    int show_main_menu_calls = 0;
    int show_team_build_calls = 0;
    int show_campaign_calls = 0;
    int options_calls = 0;
    int help_calls = 0;
    int run_game_calls = 0;
    int networking_calls = 0;
    int load_calls = 0;
    int save_calls = 0;
    int prep_calls = 0;

    og::ui::MainMenuAction show_main_menu() override
    {
        ++show_main_menu_calls;
        if (main_index < main_actions.size())
            return main_actions[main_index++];
        return og::ui::MainMenuAction::Quit;
    }

    og::ui::TeamBuildAction show_team_build() override
    {
        ++show_team_build_calls;
        return team_build_action;
    }

    std::string show_campaign_select() override
    {
        ++show_campaign_calls;
        return campaign_result;
    }

    bool prepare_new_game() override
    {
        ++prep_calls;
        return new_game_ok;
    }

    bool configure_networking() override
    {
        ++networking_calls;
        return networking_ok;
    }

    void show_options() override
    {
        ++options_calls;
    }

    void show_help() override
    {
        ++help_calls;
    }

    void run_game() override
    {
        ++run_game_calls;
    }

    bool load_game() override
    {
        ++load_calls;
        return load_ok;
    }

    bool save_game() override
    {
        ++save_calls;
        return save_ok;
    }

    og::ui::PickerScreen screen_after_game() const override
    {
        return after_game;
    }
};

struct MovementFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    MovementFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

struct SeqRandom final : IRandom {
    std::uint32_t n = 0;
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = n % max_exclusive;
        ++n;
        return v;
    }
};

walker* add_living(MovementFixture& fx, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, w.get());
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(x, y);
    walker* out = w.get();
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

void assign_resetting_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[static_cast<std::size_t>(i)][0] = 0;
        seqs[static_cast<std::size_t>(i)][1] = -1;
        seqs[static_cast<std::size_t>(i)][2] = -1;
        seqs[static_cast<std::size_t>(i)][3] = -1;
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
    }
    w->ani = rows.data();
}

void assign_long_ani_no_sentinel(walker* w)
{
    static std::array<std::array<signed char, 128>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        for (int j = 0; j < 128; ++j)
            seqs[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = static_cast<signed char>(j % 4);
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
    }
    w->ani = rows.data();
}

PixieData make_grid(unsigned char fill, int w = 9, int h = 9)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = static_cast<unsigned char>(w);
    pd.h = static_cast<unsigned char>(h);
    pd.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        pd.data[static_cast<std::size_t>(i)] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[static_cast<std::size_t>(x + y * pd.w)] = v;
}

void set_neighbors_mask(PixieData& pd, int cx, int cy, unsigned char center,
                        unsigned char same_genre, unsigned char other, int mask)
{
    set_at(pd, cx, cy, center);
    set_at(pd, cx, cy - 1, (mask & TO_UP) ? same_genre : other);
    set_at(pd, cx + 1, cy, (mask & TO_RIGHT) ? same_genre : other);
    set_at(pd, cx, cy + 1, (mask & TO_DOWN) ? same_genre : other);
    set_at(pd, cx - 1, cy, (mask & TO_LEFT) ? same_genre : other);
}

int g_clear_stale_view_controls_calls = 0;

void test_clear_stale_view_controls(LevelRuntimeData*)
{
    ++g_clear_stale_view_controls_calls;
}

} // namespace

TEST(CoverageMisc, coverage_r18_picker_show_main_and_team_build_mappings)
{
    MenuOnlyClient client;
    static const og::ui::PickerMenuItem unknown{"noop", "noop", og::ui::PickerMenuCommand::SetDifficulty, 0};
    static const og::ui::PickerMenuItem new_game{"new", "new", og::ui::PickerMenuCommand::BeginNewGame, 0};
    static const og::ui::PickerMenuItem cont{"continue", "continue", og::ui::PickerMenuCommand::ContinueGame, 0};
    static const og::ui::PickerMenuItem networking{"networking", "networking", og::ui::PickerMenuCommand::Networking, 0};
    static const og::ui::PickerMenuItem options{"options", "options", og::ui::PickerMenuCommand::Options, 0};
    static const og::ui::PickerMenuItem help{"help", "help", og::ui::PickerMenuCommand::Help, 0};
    static const og::ui::PickerMenuItem quit{"quit", "quit", og::ui::PickerMenuCommand::Quit, 0};
    static const og::ui::PickerMenuItem go{"go", "go", og::ui::PickerMenuCommand::StartGame, 0};
    static const og::ui::PickerMenuItem back{"back", "back", og::ui::PickerMenuCommand::Back, 0};

    client.scripted = {&unknown, &new_game};
    client.present_calls = 0;
    ASSERT_TRUE(client.show_main_menu() == og::ui::MainMenuAction::NewGame);
    ASSERT_TRUE(client.handle_calls == 1);

    client.scripted = {&cont};
    client.present_calls = 0;
    ASSERT_TRUE(client.show_main_menu() == og::ui::MainMenuAction::ViewTeam);

    client.scripted = {&networking};
    client.present_calls = 0;
    ASSERT_TRUE(client.show_main_menu() == og::ui::MainMenuAction::Networking);

    client.scripted = {&options};
    client.present_calls = 0;
    ASSERT_TRUE(client.show_main_menu() == og::ui::MainMenuAction::Options);

    client.scripted = {&help};
    client.present_calls = 0;
    ASSERT_TRUE(client.show_main_menu() == og::ui::MainMenuAction::Help);

    client.scripted = {&quit};
    client.present_calls = 0;
    ASSERT_TRUE(client.show_main_menu() == og::ui::MainMenuAction::Quit);

    client.scripted = {nullptr};
    client.present_calls = 0;
    ASSERT_TRUE(client.show_main_menu() == og::ui::MainMenuAction::Quit);

    client.scripted = {&unknown, &go};
    client.present_calls = 0;
    const int old_handles = client.handle_calls;
    ASSERT_TRUE(client.show_team_build() == og::ui::TeamBuildAction::PlayGame);
    ASSERT_TRUE(client.handle_calls == old_handles + 1);

    client.scripted = {&back};
    client.present_calls = 0;
    ASSERT_TRUE(client.show_team_build() == og::ui::TeamBuildAction::BackToMainMenu);

    client.scripted = {&networking, &back};
    client.present_calls = 0;
    const int old_networking_calls = client.networking_calls;
    ASSERT_TRUE(client.show_team_build() == og::ui::TeamBuildAction::BackToMainMenu);
    ASSERT_TRUE(client.networking_calls == old_networking_calls + 1);

    client.scripted = {nullptr};
    client.present_calls = 0;
    ASSERT_TRUE(client.show_team_build() == og::ui::TeamBuildAction::BackToMainMenu);
}

TEST(CoverageMisc, coverage_r18_picker_run_picker_state_switches)
{
    ScriptedPickerClient a;
    a.main_actions = {
        og::ui::MainMenuAction::NewGame,
        og::ui::MainMenuAction::Networking,
        og::ui::MainMenuAction::LoadGame,
        og::ui::MainMenuAction::SaveGame,
        og::ui::MainMenuAction::Options,
        og::ui::MainMenuAction::Help,
        og::ui::MainMenuAction::Quit,
    };
    a.campaign_result = "campaign";
    a.team_build_action = og::ui::TeamBuildAction::BackToMainMenu;
    og::ui::run_picker(a);
    ASSERT_TRUE(a.prep_calls == 1);
    ASSERT_TRUE(a.show_campaign_calls == 1);
    ASSERT_TRUE(a.networking_calls == 1);
    ASSERT_TRUE(a.load_calls == 1);
    ASSERT_TRUE(a.save_calls == 1);
    ASSERT_TRUE(a.options_calls == 1);
    ASSERT_TRUE(a.help_calls == 1);
    ASSERT_TRUE(a.show_team_build_calls == 2);
    ASSERT_TRUE(a.run_game_calls == 0);

    ScriptedPickerClient b;
    b.main_actions = {
        og::ui::MainMenuAction::NewGame,
        og::ui::MainMenuAction::LoadGame,
        og::ui::MainMenuAction::SaveGame,
        og::ui::MainMenuAction::HireTeam,
        og::ui::MainMenuAction::TrainTeam,
        og::ui::MainMenuAction::ViewTeam,
        og::ui::MainMenuAction::Multiplayer,
        og::ui::MainMenuAction::Quit,
    };
    b.new_game_ok = false;
    b.load_ok = false;
    b.save_ok = false;
    b.team_build_action = og::ui::TeamBuildAction::BackToMainMenu;
    og::ui::run_picker(b);
    ASSERT_TRUE(b.prep_calls == 1);
    ASSERT_TRUE(b.show_campaign_calls == 0);
    ASSERT_TRUE(b.load_calls == 1);
    ASSERT_TRUE(b.save_calls == 1);
    ASSERT_TRUE(b.show_team_build_calls == 5);
    ASSERT_TRUE(b.run_game_calls == 0);

    ScriptedPickerClient c;
    c.main_actions = {og::ui::MainMenuAction::ViewTeam};
    c.team_build_action = og::ui::TeamBuildAction::PlayGame;
    c.after_game = og::ui::PickerScreen::Quit;
    og::ui::run_picker(c);
    ASSERT_TRUE(c.show_team_build_calls == 1);
    ASSERT_TRUE(c.run_game_calls == 1);

    ScriptedPickerClient d;
    d.main_actions = {og::ui::MainMenuAction::ContinueGame};
    d.after_game = static_cast<og::ui::PickerScreen>(99);
    og::ui::run_picker(d);
    ASSERT_TRUE(d.run_game_calls == 1);
    ASSERT_TRUE(d.show_main_menu_calls == 1);

    ScriptedPickerClient e;
    e.main_actions = {og::ui::MainMenuAction::ContinueGame};
    e.after_game = og::ui::PickerScreen::Quit;
    og::ui::run_picker(e);
    ASSERT_TRUE(e.run_game_calls == 1);
}

TEST(CoverageMisc, coverage_r18_family_cleric_check_special_default_false)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    MovementFixture fx;
    living self;
    self.set_order_family(Order::Living, FAMILY_CLERIC);
    bind_test_entity_sim_context(fx.level, &self);
    self.set_current_special(1);
    self.stats()->set_max_magicpoints(100.0f);
    self.stats()->set_magicpoints(1.0f);
    ASSERT_TRUE(!og::test::check_special_ai(desc, &self));
}

TEST(CoverageMisc, coverage_r18_walker_movement_blocked_user_paths)
{
    MovementFixture fx;
    walker* user = add_living(fx, static_cast<short>(fx.level.world().pixmaxx - 1), static_cast<short>(fx.level.world().pixmaxy - 1));
    ASSERT_TRUE(user != nullptr);
    assign_basic_ani(user);

    user->set_user(0);
    user->set_curdir(FACE_DOWN_RIGHT);
    ASSERT_TRUE(!user->walkstep(1.0f, 1.0f));

    user->setxy(0, 0);
    user->set_curdir(FACE_UP);
    ASSERT_TRUE(!user->walkstep(0.0f, -1.0f));

    // Hit user-slide branches where only one axis can move.
    user->setxy(static_cast<short>(fx.level.world().pixmaxx - 1), static_cast<short>(10));
    user->set_curdir(FACE_DOWN_RIGHT);
    (void)user->walkstep(1.0f, 1.0f);

    user->setxy(static_cast<short>(10), static_cast<short>(fx.level.world().pixmaxy - 1));
    user->set_curdir(FACE_DOWN_LEFT);
    (void)user->walkstep(-1.0f, 1.0f);

    user->setxy(static_cast<short>(fx.level.world().pixmaxx - 1), static_cast<short>(10));
    user->set_curdir(FACE_UP_RIGHT);
    (void)user->walkstep(1.0f, -1.0f);

    // Stationary family short-circuit in walk().
    walker* tower = add_living(fx, 32, 32);
    ASSERT_TRUE(tower != nullptr);
    tower->set_order_family(Order::Living, FAMILY_TOWER1);
    ASSERT_TRUE(tower->walk(1.0f, 0.0f));

    // BIT_ANIMATE invalid-move branch (blocked by terrain, not map edge).
    user->setxy(20, 20);
    user->stats()->set_bit_flags(BIT_ANIMATE, 1);
    assign_resetting_ani(user);
    const int blocked_x = (user->xpos() - 1) / GRID_SIZE;
    const int blocked_y = user->ypos() / GRID_SIZE;
    const int blocked_index = blocked_x + blocked_y * fx.level.world().grid.w;
    fx.level.world().grid.data[static_cast<std::size_t>(blocked_index)] = PIX_H_WALL1;
    user->set_curdir(FACE_LEFT);
    ASSERT_TRUE(!user->walk(-1.0f, 0.0f));
}

TEST(CoverageMisc, coverage_r18_walker_act_decay_cleanup_and_guard_branches)
{
    MovementFixture fx;
    walker* self = add_living(fx, 64, 64);
    walker* foe = add_living(fx, 80, 64);
    walker* leader = add_living(fx, 60, 64);
    walker* owner = add_living(fx, 64, 80);
    ASSERT_TRUE(self && foe && leader && owner);

    self->set_foe(foe);
    self->set_leader(leader);
    self->set_owner(owner);
    foe->set_dead(1);
    leader->set_dead(1);
    owner->set_dead(1);

    self->set_attack_lunge(0.2f);
    self->set_hit_recoil(0.2f);
    self->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(self->act());
    ASSERT_TRUE(self->foe() == nullptr);
    ASSERT_TRUE(self->leader() == nullptr);
    ASSERT_TRUE(self->owner() == nullptr);
    ASSERT_TRUE(self->attack_lunge() == 0.0f);
    ASSERT_TRUE(self->hit_recoil() == 0.0f);

    self->set_team_num(0);
    foe->set_dead(0);
    foe->set_team_num(1);
    self->setxy(64, 64);
    foe->setxy(72, 64);
    self->set_act_type(ACT_GUARD);
    (void)self->act();
}

TEST(CoverageMisc, coverage_r18_walker_animate_invalid_sequence_guard)
{
    MovementFixture fx;
    walker* self = add_living(fx, 64, 64);
    ASSERT_TRUE(self != nullptr);

    assign_long_ani_no_sentinel(self);
    self->set_ani_type(ANI_ATTACK);
    self->set_cycle(0);

    ASSERT_TRUE(self->animate() == 0);
    ASSERT_TRUE(self->ani_type() == ANI_WALK);
    ASSERT_TRUE(self->cycle() == 0);
}

TEST(CoverageMisc, coverage_r18_level_data_resize_and_delete_cleanup_branches)
{
    MovementFixture fx;
    walker* keep = add_living(fx, 10, 10);
    walker* off_map = add_living(fx, 400, 400);
    ASSERT_TRUE(keep && off_map);

    fx.level.world().oblist.push_back(std::unique_ptr<walker>{});
    fx.level.world().fxlist.push_back(std::unique_ptr<walker>{});
    fx.level.world().weaplist.push_back(std::unique_ptr<walker>{});

    fx.level.resize_grid(3, 3);
    for (auto& uptr : fx.level.world().oblist)
    {
        ASSERT_TRUE(uptr != nullptr);
        ASSERT_TRUE(uptr.get() != off_map);
    }
    for (auto& uptr : fx.level.world().fxlist)
        ASSERT_TRUE(uptr != nullptr);
    for (auto& uptr : fx.level.world().weaplist)
        ASSERT_TRUE(uptr != nullptr);

    LevelDataHooks hooks{};
    hooks.clear_stale_view_controls = test_clear_stale_view_controls;
    g_clear_stale_view_controls_calls = 0;
    LevelRuntimeData with_hooks(1, true, &hooks);
    with_hooks.create_new_grid();
    with_hooks.world().myobmap->walker_to_pos[reinterpret_cast<walker*>(0x1)] = {};
    with_hooks.delete_objects();
    ASSERT_TRUE(g_clear_stale_view_controls_calls == 1);
    ASSERT_TRUE(with_hooks.world().myobmap->walker_to_pos.empty());
    ASSERT_TRUE(with_hooks.world().myobmap->pos_to_walker.empty());
}

TEST(CoverageMisc, coverage_r18_smooth_targeted_mask_branches)
{
    // Every arm is read back with query_x_y. This SeqRandom answers 0, 1, 2,
    // 3, ... in order (each draw modulo the bound), so the four water arms
    // below consume draws 0..3 and nothing before them draws at all.
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    push_test_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);
    const int x = 4;
    const int y = 4;

    // TYPE_GRASS with water at upright+downright+downleft+right+down.
    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x + 1, y - 1, PIX_WATER1);
    set_at(pd, x + 1, y + 1, PIX_WATER1);
    set_at(pd, x - 1, y + 1, PIX_WATER1);
    set_at(pd, x + 1, y, PIX_WATER1);
    set_at(pd, x, y + 1, PIX_WATER1);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_GRASSWATER_LR, s.query_x_y(x, y)) << "water LR arm";

    // TYPE_WALL is a switch on `around`. 3 and 9 are the two base cases; 2
    // (TO_RIGHT) has no case at all, and the default leaves the tile as it
    // was -- a legacy quirk, pinned as-is.
    set_neighbors_mask(pd, x, y, PIX_WALL2, PIX_WALL2, PIX_GRASS1, TO_UP | TO_RIGHT);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_WALLSIDE_L, s.query_x_y(x, y)) << "wall around == 3";
    set_neighbors_mask(pd, x, y, PIX_WALL2, PIX_WALL2, PIX_GRASS1, TO_UP | TO_LEFT);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_WALLSIDE_R, s.query_x_y(x, y)) << "wall around == 9";
    set_neighbors_mask(pd, x, y, PIX_WALL2, PIX_WALL2, PIX_GRASS1, TO_RIGHT);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_WALL2, s.query_x_y(x, y))
        << "wall around == 2 has no case: the default keeps the tile";

    // TYPE_WATER single-neighbour arms, each a two-entry table indexed by
    // next_random(2). Draws 0, 1, 0, 1 in this order.
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_UP);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_WATERGRASS_LL, s.query_x_y(x, y)) << "watergrass_up[0]";
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_DOWN);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_WATERGRASS_UR, s.query_x_y(x, y)) << "watergrass_down[1]";
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_LEFT);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_WATERGRASS_UR, s.query_x_y(x, y)) << "watergrass_left[0]";
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_RIGHT);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_WATERGRASS_LL, s.query_x_y(x, y)) << "watergrass_right[1]";

    // TYPE_TREES TO_AROUND draws NO rng: with all eight corners trees it is
    // PIX_TREE_M1 every time, so the repeat is a fixed point.
    set_neighbors_mask(pd, x, y, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1, TO_AROUND);
    set_at(pd, x - 1, y - 1, PIX_TREE_B1);
    set_at(pd, x + 1, y - 1, PIX_TREE_B1);
    set_at(pd, x - 1, y + 1, PIX_TREE_B1);
    set_at(pd, x + 1, y + 1, PIX_TREE_B1);
    for (int rep = 0; rep < 3; ++rep)
    {
        ASSERT_EQ(1, s.smooth(x, y));
        EXPECT_EQ(PIX_TREE_M1, s.query_x_y(x, y))
            << "trees all-around, repeat " << rep << ": no rng, no drift";
    }

    // TYPE_DIRT_DARK is a straight table lookup on `around`.
    set_neighbors_mask(pd, x, y, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, TO_AROUND);
    ASSERT_EQ(1, s.smooth(x, y));
    EXPECT_EQ(PIX_DIRT_DARK_1, s.query_x_y(x, y))
        << "dirt_dark_by_surround[15]";

    // set_x_y()'s no-grid guard: smooth still answers 1 on a smoother with no
    // target, and writes nothing (query_x_y answers its own PIX_GRASS1 guard).
    smoother empty;
    EXPECT_FALSE(empty.has_target()) << "no target set";
    EXPECT_EQ(1, empty.smooth(0, 0)) << "smooth(x, y) always answers 1";
    EXPECT_FALSE(empty.has_target()) << "and never acquires one";

    pop_test_context();
}

TEST(CoverageMisc, coverage_r18_gparser_more_commandline_switches)
{
    cfg_store local_cfg;
    std::vector<std::string> arg_storage = {"openglad", "-S", "-s", "-d", "-e", "-x", "-f", "-z"};
    std::vector<char*> argv_buf;
    argv_buf.reserve(arg_storage.size());
    for (std::string& s : arg_storage)
        argv_buf.push_back(s.data());

    int argc = static_cast<int>(argv_buf.size());
    char** argv = argv_buf.data();
    local_cfg.commandline(argc, argv);
    ASSERT_TRUE(local_cfg.get_setting("sound", "sound") == "on");
    ASSERT_TRUE(local_cfg.get_setting("graphics", "render") == "sai");
    ASSERT_TRUE(local_cfg.get_setting("graphics", "fullscreen") == "on");
}
} // namespace detail_coverage_r18

// --- From test_coverage_r19.cpp ---
namespace detail_coverage_r19 {
namespace {

struct SeqRandom final : IRandom {
    std::array<std::uint32_t, 8> vals{};
    std::size_t i = 0;

    explicit SeqRandom(std::initializer_list<std::uint32_t> init)
    {
        std::size_t idx = 0;
        for (std::uint32_t v : init)
        {
            if (idx >= vals.size())
                break;
            vals[idx++] = v;
        }
    }

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = vals[i % vals.size()] % max_exclusive;
        ++i;
        return v;
    }
};

struct R19Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;
    GameContext gc;

    R19Fixture()
        : gameplay(level, save, events, cfg)
    {
        init_family_registry();
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;

        push_test_context(&gc);
    }

    ~R19Fixture()
    {
        pop_test_context();
    }
};

living* add_living(R19Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_normal_stepsize(1.0f);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

void assign_short_ani(walker* w)
{
    static std::array<std::array<signed char, 3>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[static_cast<std::size_t>(i)][0] = 0;
        seqs[static_cast<std::size_t>(i)][1] = -1;
        seqs[static_cast<std::size_t>(i)][2] = -1;
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
    }
    w->ani = rows.data();
}

} // namespace

TEST(CoverageMisc, coverage_r19_family_cleric_check_special_true_paths)
{
    R19Fixture fx;
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);

    living* self = add_living(fx, FAMILY_CLERIC, 0, 64, 64);
    living* ally = add_living(fx, FAMILY_SOLDIER, 0, 70, 64);
    ASSERT_TRUE(self && ally);

    self->set_current_special(1);
    self->stats()->set_max_magicpoints(100.0f);
    self->stats()->set_magicpoints(80.0f);
    self->set_shifter_down(1);
    ASSERT_TRUE(og::test::check_special_ai(desc, self));
    ASSERT_TRUE(self->shifter_down() == 0);

    ally->setxy(300, 300);
    self->set_shifter_down(0);
    ASSERT_TRUE(og::test::check_special_ai(desc, self));
    ASSERT_TRUE(self->shifter_down() == 1);
}

TEST(CoverageMisc, coverage_r19_walker_init_fire_busy_and_fire_fail_paths)
{
    R19Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 48, 48);
    ASSERT_TRUE(self != nullptr);

    const short fire_dir = self->facing(1, 0);
    self->set_curdir(static_cast<char>(fire_dir));
    self->set_enddir(static_cast<char>(fire_dir));
    self->set_busy(1.0f);
    ASSERT_TRUE(!self->init_fire(1, 0));

    self->set_busy(0.0f);
    self->set_ani_type(ANI_ATTACK);
    self->stats()->set_weapon_cost(1);
    self->stats()->set_magicpoints(0.0f);
    ASSERT_TRUE(!self->init_fire(1, 0));
}

TEST(CoverageMisc, coverage_r19_walker_animate_attack_completion_branch)
{
    R19Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    ASSERT_TRUE(self != nullptr);

    assign_short_ani(self);
    self->set_ani_type(ANI_ATTACK);
    self->set_cycle(0);
    self->stats()->set_weapon_cost(1);
    self->stats()->set_magicpoints(0.0f);

    ASSERT_TRUE(self->animate());
    ASSERT_TRUE(self->ani_type() == ANI_WALK);
    ASSERT_TRUE(self->cycle() == 0);
}

// living::act's ACT_RANDOM arm. The sim draws from current_game->world->rng_
// (SimRandom), NOT from the GameContext rng -- and this binary compiles
// og_gameplay WITHOUT -DTESTING, so og::sim::set_sim_random_override is not
// even linked in here. The stream is steered by seeding the LCG state
// instead: state 1 answers next(5) = 3, next(5) = 1, next(2) = 1, which is
// the "4 of 5" arm followed (when no foe is findable) by the RANDOM_WALK
// branch. find_near_foe / find_far_foe draw only next(0), which never
// advances the state.
TEST(CoverageMisc, coverage_r19_living_act_random_acquires_a_foe_and_queues_a_command)
{
    R19Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    living* foe = add_living(fx, FAMILY_ORC, 1, 120, 64);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, foe);

    self->set_lineofsight(1);
    self->set_foe(nullptr);
    self->set_act_type(ACT_RANDOM);
    // An ODD facing, so the arm's snap to an even facing is observable.
    self->set_curdir(static_cast<signed char>(FACE_DOWN_RIGHT));
    self->set_enddir(static_cast<char>(FACE_DOWN_RIGHT));
    fx.level.world().rng_.state_ = 1u;

    ASSERT_TRUE(self->act());
    EXPECT_EQ(foe, self->foe())
        << "with no foe the arm acquires the nearest hostile living";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(self->curdir()))
        << "facing snaps to (enddir / 2) * 2";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(self->enddir()))
        << "enddir snaps with it";
    EXPECT_TRUE(self->stats()->has_commands())
        << "COMMAND_SEARCH 300 is queued";

    // With the only hostile dead, the foe is dropped at the top of act and
    // cannot be re-acquired (find_far_foe skips dead walkers), so the arm
    // falls to the RANDOM_WALK branch instead.
    foe->set_dead(1);
    self->set_foe(foe);
    self->stats()->clear_command();
    self->set_curdir(static_cast<signed char>(FACE_UP));
    self->set_enddir(static_cast<char>(FACE_UP));
    fx.level.world().rng_.state_ = 1u;

    ASSERT_TRUE(self->act());
    EXPECT_EQ(nullptr, self->foe())
        << "a dead foe is dropped and never re-adopted";
    EXPECT_TRUE(self->stats()->has_commands())
        << "COMMAND_RANDOM_WALK 20 is queued instead";
}
} // namespace detail_coverage_r19

// --- From test_coverage_r20.cpp ---

namespace detail_coverage_r20 {
namespace {

struct ConstantRandom final : IRandom {
    std::uint32_t value = 0;

    explicit ConstantRandom(std::uint32_t v)
        : value(v)
    {
    }

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        return value % max_exclusive;
    }
};

struct SequenceRandom final : IRandom {
    std::array<std::uint32_t, 8> values{};
    std::size_t idx = 0;

    explicit SequenceRandom(std::initializer_list<std::uint32_t> init)
    {
        std::size_t i = 0;
        for (std::uint32_t v : init)
        {
            if (i >= values.size())
                break;
            values[i++] = v;
        }
    }

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = values[idx % values.size()] % max_exclusive;
        ++idx;
        return v;
    }
};

struct R20Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    ConstantRandom rng{1};
    ScopedGameplayContext gameplay;
    GameContext gc;

    R20Fixture()
        : gameplay(level, save, events, cfg)
    {
        init_family_registry();
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);

        gc.rng = &rng;

        push_test_context(&gc);
    }

    ~R20Fixture()
    {
        pop_test_context();
    }
};

walker* add_walker(R20Fixture& fx, Order order, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(order, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_normal_stepsize(1.0f);
    w->set_lineofsight(4);
    w->setxy(x, y);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    if (order == Order::Weapon)
        fx.level.world().weaplist.push_back(std::move(w));
    else
        fx.level.world().oblist.push_back(std::move(w));
    return out;
}

living* add_living(R20Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->set_normal_stepsize(1.0f);
    w->set_lineofsight(4);
    w->setxy(x, y);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[static_cast<std::size_t>(x + y * pd.w)] = v;
}

PixieData make_grid(unsigned char fill, int w = 7, int h = 7)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = static_cast<unsigned char>(w);
    pd.h = static_cast<unsigned char>(h);
    pd.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        pd.data[static_cast<std::size_t>(i)] = fill;
    return pd;
}

void set_neighbors_mask(PixieData& pd, int cx, int cy, unsigned char center,
                        unsigned char same_genre, unsigned char other, int mask)
{
    set_at(pd, cx, cy, center);
    set_at(pd, cx, cy - 1, (mask & TO_UP) ? same_genre : other);
    set_at(pd, cx + 1, cy, (mask & TO_RIGHT) ? same_genre : other);
    set_at(pd, cx, cy + 1, (mask & TO_DOWN) ? same_genre : other);
    set_at(pd, cx - 1, cy, (mask & TO_LEFT) ? same_genre : other);

    set_at(pd, cx - 1, cy - 1, other);
    set_at(pd, cx + 1, cy - 1, other);
    set_at(pd, cx - 1, cy + 1, other);
    set_at(pd, cx + 1, cy + 1, other);
}

} // namespace

// walker::act's own ACT_RANDOM arm (a plain walker, so living::act's richer
// version does not run): 3 of 4 times it adopts find_far_foe's answer and, IF
// it has a foe, queues COMMAND_SEARCH 500 -- otherwise it queues nothing at
// all. The sim draws from world.rng_ (see the r19 note above); state 1 answers
// next(4) = 2, which is the 3-of-4 arm.
TEST(CoverageMisc, coverage_r20_walker_act_random_queues_search_only_with_a_foe)
{
    R20Fixture fx;

    walker* self = add_walker(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    ASSERT_NE(nullptr, self);

    self->set_foe(nullptr);
    self->set_lineofsight(1);
    self->set_act_type(ACT_RANDOM);
    fx.level.world().rng_.state_ = 1u;
    ASSERT_TRUE(self->act());
    EXPECT_EQ(nullptr, self->foe())
        << "alone in the world there is no far foe to adopt";
    EXPECT_FALSE(self->stats()->has_commands())
        << "no foe, no COMMAND_SEARCH: the arm queues nothing";

    walker* foe = add_walker(fx, Order::Living, FAMILY_ORC, 1, 220, 64);
    ASSERT_NE(nullptr, foe);
    self->stats()->clear_command();
    self->set_foe(foe);
    self->set_lineofsight(1);
    self->set_collide_ob(foe);
    fx.level.world().rng_.state_ = 1u;
    ASSERT_TRUE(self->act());
    EXPECT_EQ(nullptr, self->collide_ob())
        << "act clears the collision record on entry";
    EXPECT_EQ(foe, self->foe()) << "the standing foe is kept";
    EXPECT_TRUE(self->stats()->has_commands())
        << "with a foe the arm queues COMMAND_SEARCH 500";
}

TEST(CoverageMisc, coverage_r20_walker_movement_stationary_walkstep_walk_turn)
{
    R20Fixture fx;

    walker* tower = add_walker(fx, Order::Living, FAMILY_TOWER1, 0, 80, 80);
    ASSERT_TRUE(tower != nullptr);

    tower->set_stepsize(2.0f);
    const float old_lastx = tower->lastx();
    const float old_lasty = tower->lasty();

    ASSERT_TRUE(tower->walkstep(1.0f, 0.0f));
    ASSERT_TRUE(tower->lastx() == 1.0f);
    ASSERT_TRUE(tower->lasty() == 0.0f);

    ASSERT_TRUE(tower->walk(0.0f, -1.0f));

    tower->set_curdir(FACE_UP);
    tower->turn(FACE_LEFT);
    ASSERT_TRUE(tower->lastx() == 1.0f);
    ASSERT_TRUE(tower->lasty() == 0.0f);
    ASSERT_TRUE(tower->curdir() != FACE_UP);
    ASSERT_TRUE(old_lastx != tower->lastx() || old_lasty != tower->lasty());
}

TEST(CoverageMisc, coverage_r20_level_data_add_paths_and_clear_reset)
{
    R20Fixture fx;

    walker* as_weapon = fx.level.add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(as_weapon != nullptr);

    walker* fx_ob = fx.level.add_fx_ob(Order::FX, FAMILY_HIT);
    ASSERT_TRUE(fx_ob != nullptr);

    walker* weap = fx.level.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(weap != nullptr);

    fx.level.world().title = "changed";
    fx.level.world().type = 7;
    fx.level.world().par_value = 9;
    fx.level.world().time_bonus_limit = 10;
    fx.level.level_visuals().topx = 5;
    fx.level.level_visuals().topy = 6;
    fx.level.clear();

    ASSERT_TRUE(fx.level.world().title == "New Level");
    ASSERT_TRUE(fx.level.world().type == 0);
    ASSERT_TRUE(fx.level.world().par_value == 1);
    ASSERT_TRUE(fx.level.world().time_bonus_limit == 4000);
    ASSERT_TRUE(fx.level.level_visuals().topx == 0);
    ASSERT_TRUE(fx.level.level_visuals().topy == 0);

    walker dummy;
    ASSERT_TRUE(fx.level.remove_ob(&dummy) == 0);
}

// TYPE_GRASS_DARK's `around`-driven arms. None of these neighbourhoods holds
// trees or walls, so the early trees/wall arms are skipped and selection is by
// `around` plus the GENRE of the open side. The grass-vs-water twins are the
// load-bearing pairs: a swapped table entry cannot satisfy both halves.
// ConstantRandom{1} makes every draw 1.
TEST(CoverageMisc, coverage_r20_smooth_dark_grass_specific_branches)
{
    ConstantRandom rng1{1};
    GameContext gc;
    gc.rng = &rng1;
    push_test_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 3;
    const int y = 3;

    struct DarkCase
    {
        int mask;
        unsigned char other;
        int expect;
        const char* why;
    };
    const DarkCase cases[] = {
        {TO_UP | TO_DOWN | TO_LEFT, PIX_GRASS1, PIX_GRASS_DARK_R2,
         "right middle -> grass_dark_right[1]"},
        {TO_DOWN, PIX_GRASS1, PIX_GRASS_DARK_LL, "top alone, grass beside"},
        {TO_LEFT | TO_DOWN, PIX_GRASS1, PIX_GRASS_DARK_LL,
         "top right, grass to the right"},
        {TO_LEFT | TO_DOWN, PIX_WATER1, PIX_GRASS_DARK_B2,
         "top right, water to the right"},
        {TO_DOWN, PIX_WATER1, PIX_GRASS_DARK_B1, "top alone, water beside"},
        {TO_RIGHT | TO_UP, PIX_GRASS1, PIX_GRASS_DARK_UR,
         "bottom left, grass to the left"},
        {TO_RIGHT | TO_UP, PIX_WATER1, PIX_GRASS_DARK_B1,
         "bottom left, water to the left"},
        {TO_RIGHT, PIX_GRASS1, PIX_GRASS_DARK_UR, "left alone, grass left"},
        {TO_RIGHT, PIX_WATER1, PIX_GRASS_DARK_B1, "left alone, water left"},
        {TO_UP, PIX_GRASS1, PIX_GRASS_DARK_UR, "bottom alone, grass below"},
        {TO_UP | TO_DOWN, PIX_GRASS1, PIX_GRASS_DARK_R2,
         "center vertical -> grass_dark_right[1]"},
    };

    for (const DarkCase& c : cases)
    {
        set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1,
                           c.other, c.mask);
        ASSERT_EQ(1, s.smooth(x, y)) << c.why;
        EXPECT_EQ(c.expect, s.query_x_y(x, y)) << c.why;
    }

    pop_test_context();
}

TEST(CoverageMisc, coverage_r20_family_cleric_do_special_guard_conditions)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_CLERIC);
    living self;

    // The hook resolves og.tuning by the WALKER's own order/family, so the
    // walker must BE
    // a cleric, exactly as the sim guarantees when this hook dispatches.
    self.set_order_family(Order::Living, FAMILY_CLERIC);

    self.set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    self.set_user(0);

    self.set_current_special(1);
    self.set_shifter_down(1);
    self.set_busy(1.0f);
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_busy(0.0f);
    self.myguy->intelligence = 40;
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_current_special(2);
    self.set_shifter_down(1);
    self.set_busy(1.0f);
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_busy(0.0f);
    self.myguy->intelligence = 30;
    const float old_busy2 = self.busy();
    ASSERT_TRUE(!og::test::do_special(desc, &self));
    ASSERT_TRUE(self.busy() > old_busy2);

    self.set_current_special(3);
    self.set_shifter_down(1);
    self.set_busy(0.0f);
    self.myguy->intelligence = 30;
    ASSERT_TRUE(!og::test::do_special(desc, &self));
}

TEST(CoverageMisc, coverage_r20_family_mage_do_special_guard_conditions)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_MAGE);
    living self;

    self.set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    self.set_user(0);

    self.set_current_special(1);
    self.set_ani_type(ANI_TELE_OUT);
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_ani_type(ANI_WALK);
    self.set_shifter_down(1);
    self.set_busy(1.0f);
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_busy(0.0f);
    self.myguy->intelligence = 40;
    ASSERT_TRUE(!og::test::do_special(desc, &self));

    self.set_shifter_down(0);
    self.set_ani_type(ANI_WALK);
    ASSERT_TRUE(og::test::do_special(desc, &self));
    ASSERT_TRUE(self.ani_type() == ANI_TELE_OUT);
}

TEST(CoverageMisc, coverage_r20_family_druid_do_special_default_and_busy_guards)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_DRUID);
    R20Fixture fx;

    living* self = add_living(fx, FAMILY_DRUID, 0, 64, 64);
    ASSERT_TRUE(self != nullptr);

    self->set_current_special(4);
    self->set_busy(0.0f);
    ASSERT_TRUE(!og::test::do_special(desc, self));

    self->set_current_special(1);
    self->set_busy(1.0f);
    ASSERT_TRUE(!og::test::do_special(desc, self));

    self->set_current_special(2);
    self->set_busy(1.0f);
    ASSERT_TRUE(!og::test::do_special(desc, self));
}
} // namespace detail_coverage_r20

// --- From test_final_coverage_r16.cpp ---
namespace detail_final_coverage_r16 {
namespace {

struct SeqRandom final : IRandom {
    std::vector<std::uint32_t> vals;
    std::size_t i = 0;

    explicit SeqRandom(std::initializer_list<std::uint32_t> init)
        : vals(init)
    {}

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        if (vals.empty())
            return 0;
        const std::uint32_t v = vals[i % vals.size()] % max_exclusive;
        ++i;
        return v;
    }
};

struct FinalR16Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{1};
    ScopedGameplayContext gameplay;
    GameContext gc;

    FinalR16Fixture()
        : gameplay(level, save, events, cfg)
    {
        init_family_registry();
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;

        push_test_context(&gc);
    }

    ~FinalR16Fixture()
    {
        pop_test_context();
    }
};

living* add_living(FinalR16Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(2.0f);
    w->set_normal_stepsize(2.0f);
    w->set_lineofsight(8);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    living* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_fx(FinalR16Fixture& fx, char family, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::FX, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_team_num(0);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

struct MenuClient final : og::ui::IPickerClient {
    std::vector<const og::ui::PickerMenuItem*> main_items;
    std::vector<const og::ui::PickerMenuItem*> team_items;
    int main_i = 0;
    int team_i = 0;
    int handled = 0;

    bool prepared = true;
    bool load_ok = false;
    bool save_ok = false;
    std::string campaign = "";
    og::ui::PickerScreen after = og::ui::PickerScreen::MainMenu;

    int options_calls = 0;
    int help_calls = 0;
    int run_calls = 0;

    const og::ui::PickerMenuItem* present_menu(og::ui::PickerMenuId id) override
    {
        if (id == og::ui::PickerMenuId::Main) {
            if (main_i >= static_cast<int>(main_items.size()))
                return nullptr;
            return main_items[static_cast<std::size_t>(main_i++)];
        }
        if (team_i >= static_cast<int>(team_items.size()))
            return nullptr;
        return team_items[static_cast<std::size_t>(team_i++)];
    }

    void handle_menu_item(og::ui::PickerMenuId, const og::ui::PickerMenuItem&) override
    {
        ++handled;
    }

    bool prepare_new_game() override { return prepared; }
    std::string show_campaign_select() override { return campaign; }
    void show_options() override { ++options_calls; }
    void show_help() override { ++help_calls; }
    void run_game() override { ++run_calls; }
    bool load_game() override { return load_ok; }
    bool save_game() override { return save_ok; }
    og::ui::PickerScreen screen_after_game() const override { return after; }
};

} // namespace

TEST(CoverageMisc, final_r16_combat_math_edges)
{
    SeqRandom rng{3, 0, 17, 5, 9};

    ASSERT_TRUE(compute_base_damage(25.0f, static_cast<RandomU32>(nullptr)) >= 22.0f);
    ASSERT_TRUE(compute_damage_reduction(0.0f, 100.0f) == 0.0f);
    // 2002 roll expectation: 5 damage vs 99 armor lands (5+4+3+2+1)/99.
    ASSERT_NEAR(5.0f - 15.0f / 99.0f, compute_damage_reduction(5.0f, 99.0f), 1e-4f);
    ASSERT_NEAR(6.0f / 99.0f, compute_post_reduction_damage(3.0f, 99.0f), 1e-4f);

    ASSERT_TRUE(compute_freeze_duration(2, 9999, rng) == 0);
    ASSERT_TRUE(compute_freeze_duration(5, 0, rng) >= 0);

    const HealResult hr = compute_heal_amount(40, 3, rng);
    ASSERT_TRUE(hr.amount > hr.cost);
    ASSERT_TRUE(compute_charm_duration(-3, rng) == 25);

    const RegenTickResult r0 = compute_regen_tick(10.0f, 10.0f, 1.0f, 0, 2, false);
    ASSERT_TRUE(r0.new_value == 10.0f);

    const RegenTickResult r1 = compute_regen_tick(3.0f, 8.0f, 1.5f, 1, 2, false);
    ASSERT_TRUE(r1.new_delay == 0);
    ASSERT_TRUE(r1.new_value >= 4.5f);

    const HpRegenResult h0 = compute_hp_regen_tick(5.0f, 10.0f, 1.0f, 0, 4, 2, false);
    ASSERT_TRUE(h0.new_regen_delay == 1);

    const HpRegenResult h1 = compute_hp_regen_tick(5.0f, 10.0f, 1.0f, 3, 4, 0, false);
    ASSERT_TRUE(h1.new_hp >= 6.0f);

    ASSERT_TRUE(compute_xp_from_attack(10, 0.0f) == 0);
    ASSERT_TRUE(compute_xp_from_kill(0) >= 0);
    ASSERT_TRUE(compute_xp_from_action(ExpAction::TurnUndead, 4, 2, 7, rng) == 21);
    ASSERT_TRUE(compute_xp_from_action(ExpAction::RaiseSkeleton, 1, 1, 0, rng) == 45);
    ASSERT_TRUE(compute_xp_from_action(ExpAction::ResurrectPenalty, 2, 3, 0, rng) == 900);
}

TEST(CoverageMisc, final_r16_family_difficulty_levelup_and_ai_checks)
{
    FinalR16Fixture fx;
    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    const FamilyDescriptor* druid = get_family_descriptor(FAMILY_DRUID);
    const FamilyDescriptor* soldier = get_family_descriptor(FAMILY_SOLDIER);
    const FamilyDescriptor* thief = get_family_descriptor(FAMILY_THIEF);
    const FamilyDescriptor* cleric = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(mage && druid && soldier && thief && cleric);

    guy g_mage(FAMILY_MAGE);
    const short old_int = g_mage.intelligence;
    og::test::level_up(*mage, &g_mage, 2);
    ASSERT_TRUE(g_mage.intelligence > old_int);

    guy g_druid(FAMILY_DRUID);
    const short old_dex = g_druid.dexterity;
    og::test::level_up(*druid, &g_druid, 2);
    ASSERT_TRUE(g_druid.dexterity > old_dex);

    living self;
    const float hp0 = self.stats()->max_hitpoints();
    og::test::set_difficulty(*soldier, &self, 3);
    ASSERT_TRUE(self.stats()->max_hitpoints() > hp0);

    FinalR16Fixture fx2;
    living* caster = add_living(fx2, FAMILY_MAGE, 0, 64, 64);
    ASSERT_TRUE(caster != nullptr);

    caster->set_current_special(1);
    ASSERT_TRUE(og::test::check_special_ai(*mage, caster));

    add_living(fx2, FAMILY_ORC, 1, 74, 64);
    add_living(fx2, FAMILY_ORC, 1, 84, 64);
    ASSERT_TRUE(!og::test::check_special_ai(*mage, caster));

    add_living(fx2, FAMILY_ORC, 1, 94, 64);
    add_living(fx2, FAMILY_ORC, 1, 104, 64);
    ASSERT_TRUE(og::test::check_special_ai(*mage, caster));

    caster->set_foe(add_living(fx2, FAMILY_ORC, 1, 114, 64));
    ASSERT_TRUE(og::test::check_special_ai(*soldier, caster));
    caster->foe()->setxy(66, 64);
    ASSERT_TRUE(!og::test::check_special_ai(*soldier, caster));

    caster->set_current_special(1);
    caster->set_foe(nullptr);
    ASSERT_TRUE(og::test::check_special_ai(*thief, caster));

    // The charm/taunt (thief) and heal-range (cleric) gates read og.tuning,
    // which resolves by the CASTER's own family — a
    // family hook only ever runs on a walker of that family in the sim, so
    // these checks need family-matching casters (the special-1 thief gate
    // above is a code-constant path and stays family-agnostic on purpose).
    living* thief_caster = add_living(fx2, FAMILY_THIEF, 0, 70, 64);
    ASSERT_TRUE(thief_caster != nullptr);
    thief_caster->set_current_special(3);
    thief_caster->set_shifter_down(1);
    ASSERT_TRUE(og::test::check_special_ai(*thief, thief_caster));

    living* cleric_caster = add_living(fx2, FAMILY_CLERIC, 0, 64, 70);
    ASSERT_TRUE(cleric_caster != nullptr);
    cleric_caster->set_current_special(1);
    cleric_caster->stats()->set_magicpoints(
        cleric_caster->stats()->max_magicpoints());
    ASSERT_TRUE(og::test::check_special_ai(*cleric, cleric_caster));
}

TEST(CoverageMisc, final_r16_walker_specials_teleport_and_turn_undead)
{
    FinalR16Fixture fx;
    living* self = add_living(fx, FAMILY_MAGE, 0, 64, 64);
    living* undead = add_living(fx, FAMILY_SKELETON, 1, 120, 64);
    ASSERT_TRUE(self && undead);

    self->set_current_special(1);
    self->set_dead(1);
    ASSERT_TRUE(!self->special());

    self->set_dead(0);
    self->stats()->set_special_cost(1, 1);
    self->stats()->set_magicpoints(0);
    ASSERT_TRUE(!self->special());

    self->stats()->set_magicpoints(999.0f);
    fx.level.delete_grid();
    ASSERT_TRUE(!self->teleport());
    fx.level.create_new_grid();

    walker* marker = add_fx(fx, FAMILY_MARKER, 200, 200);
    marker->set_owner(self);
    marker->set_lifetime(1);
    ASSERT_TRUE(self->teleport());
    ASSERT_TRUE(marker->lifetime() <= 1);

    self->setxy(64, 64);
    undead->setxy(80, 64);
    const std::int32_t killed = self->turn_undead(120, 5);
    ASSERT_TRUE(killed >= 0);

    for (auto& uptr : fx.level.world().oblist)
        uptr->set_dead(1);
    ASSERT_TRUE(self->turn_undead(120, 5) == -1);
}

TEST(CoverageMisc, final_r16_sim_input_switch_special_and_yell)
{
    FinalR16Fixture fx;
    living* control_living = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    living* follower = add_living(fx, FAMILY_SOLDIER, 0, 96, 64);
    ASSERT_TRUE(control_living && follower);

    walker* control = control_living;

    control_living->set_act_type(ACT_CONTROL);
    control_living->set_user(0);
    follower->set_act_type(ACT_GUARD);
    follower->set_leader(nullptr);

    std::string special_names[NUM_FAMILIES][6];
    for (int i = 0; i < NUM_FAMILIES; ++i)
        for (int j = 0; j < 6; ++j)
            special_names[i][j] = "NONE";
    special_names[FAMILY_SOLDIER][1] = "CHARGE";

    SimInputDebounce debounce;
    PlayerInput pi{};

    control_living->set_current_special(4);
    pi.pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
    const SimInputResult s0 = sim_process_player_input(
        pi, control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(!s0.endgame_requested);
    ASSERT_TRUE(control_living->current_special() == 1);

    pi = {};
    pi.pressed[static_cast<int>(InputAction::Yell)] = true;
    const SimInputResult s1 = sim_process_player_input(
        pi, control, fx.level.world(), 0, 0, debounce, special_names, &fx.events);
    ASSERT_TRUE(s1.play_sound == SOUND_YO);
    ASSERT_TRUE(s1.notify_text == "Yo!");
    ASSERT_TRUE(control_living->yo_delay() == 30);
    ASSERT_TRUE(follower->leader() == control_living);
}

TEST(CoverageMisc, final_r16_smooth_targeted_grass_and_dark_variants)
{
    SeqRandom seq{1, 2, 0, 1, 0, 3};
    GameContext gc;
    gc.rng = &seq;
    push_test_context(&gc);

    smoother s;
    PixieData pd;
    pd.frames = 1;
    pd.w = 5;
    pd.h = 5;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int i = 0; i < 25; ++i)
        pd.data[static_cast<std::size_t>(i)] = PIX_GRASS1;
    s.set_target(pd);

    const int x = 2;
    const int y = 2;

    // Trigger TYPE_GRASS random cases 1 and 2.
    pd.data[static_cast<std::size_t>(x + y * pd.w)] = PIX_GRASS1;
    pd.data[static_cast<std::size_t>(x + (y - 1) * pd.w)] = PIX_GRASS1;
    pd.data[static_cast<std::size_t>(x + 1 + y * pd.w)] = PIX_GRASS1;
    pd.data[static_cast<std::size_t>(x + (y + 1) * pd.w)] = PIX_GRASS1;
    pd.data[static_cast<std::size_t>(x - 1 + y * pd.w)] = PIX_GRASS1;
    ASSERT_TRUE(s.smooth(x, y) == 1);
    ASSERT_TRUE(s.query_x_y(x, y) == PIX_GRASS2);
    ASSERT_TRUE(s.smooth(x, y) == 1);
    ASSERT_TRUE(s.query_x_y(x, y) == PIX_GRASS3);

    // Trigger explicit water-corner conversions.
    pd.data[static_cast<std::size_t>(x + y * pd.w)] = PIX_GRASS1;
    pd.data[static_cast<std::size_t>(x - 1 + (y - 1) * pd.w)] = PIX_WATER1;
    pd.data[static_cast<std::size_t>(x - 1 + (y + 1) * pd.w)] = PIX_WATER1;
    pd.data[static_cast<std::size_t>(x + (y - 1) * pd.w)] = PIX_WATER1;
    pd.data[static_cast<std::size_t>(x - 1 + y * pd.w)] = PIX_WATER1;
    pd.data[static_cast<std::size_t>(x + 1 + (y + 1) * pd.w)] = PIX_WATER1;
    ASSERT_TRUE(s.smooth(x, y) == 1);
    const Sint32 g = s.query_x_y(x, y);
    ASSERT_TRUE(g > 0);

    pd.data[static_cast<std::size_t>(x + y * pd.w)] = PIX_GRASS_DARK_1;
    pd.data[static_cast<std::size_t>(x + (y + 1) * pd.w)] = PIX_GRASS1;
    pd.data[static_cast<std::size_t>(x + (y - 1) * pd.w)] = PIX_WALL3;
    ASSERT_TRUE(s.smooth(x, y) == 1);
    const Sint32 d = s.query_x_y(x, y);
    ASSERT_TRUE(d > 0);

    pop_test_context();
}

TEST(CoverageMisc, final_r16_stats_walker_level_data_and_picker_state)
{
    FinalR16Fixture fx;
    living* a = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    living* b = add_living(fx, FAMILY_ORC, 1, 96, 64);
    ASSERT_TRUE(a && b);

    a->stats()->set_command(COMMAND_DIE, 1);
    a->stats()->set_command(COMMAND_WALK, 1);
    a->stats()->try_command(COMMAND_RANDOM_WALK, 1);
    ASSERT_TRUE(a->stats()->has_commands());

    a->set_curdir(FACE_UP);
    (void)a->stats()->right_blocked();
    a->set_curdir(FACE_DOWN_LEFT);
    (void)a->stats()->right_forward_blocked();
    a->set_curdir(FACE_RIGHT);
    (void)a->stats()->right_back_blocked();

    (void)a->walkstep(-1.0f, -1.0f);
    (void)a->walkstep(1.0f, 1.0f);

    ASSERT_TRUE(fx.level.find_near_foe(a) == b);
    std::int32_t howmany = 0;
    ASSERT_TRUE(!fx.level.find_foes_in_range(fx.level.world().oblist, 120, &howmany, a).empty());

    MenuClient client;
    static const og::ui::PickerMenuItem unknown{"u", "u", og::ui::PickerMenuCommand::SetDifficulty, 0};
    static const og::ui::PickerMenuItem start{"s", "s", og::ui::PickerMenuCommand::StartGame, 0};
    static const og::ui::PickerMenuItem quit{"q", "q", og::ui::PickerMenuCommand::Quit, 0};
    client.main_items = {&unknown, &quit};
    client.team_items = {&start};

    const og::ui::MainMenuAction action = client.show_main_menu();
    ASSERT_TRUE(action == og::ui::MainMenuAction::Quit);
    ASSERT_TRUE(client.handled == 1);

    client.main_i = 0;
    client.main_items = {&quit};
    client.after = og::ui::PickerScreen::Help;
    og::ui::run_picker(client);
}
} // namespace detail_final_coverage_r16
