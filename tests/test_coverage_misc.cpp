#include <openglad/core/constants.h>
#include <openglad/core/stats.h>
#include <openglad/data/gparser.h>
#include <openglad/data/level_data.h>
#include <openglad/data/pixie_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/smooth.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/irandom.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "unit/unit.h"
#include <openglad/ui/picker_state.h>
#include <openglad/ui/menu_model.h>
#include <openglad/data/level_data_hooks.h>
#include <string>
#include <openglad/core/combat_math.h>
#include <openglad/input/input_action.h>
#include <openglad/input/input_state.h>
#include <openglad/sim/sim_input_handler.h>

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
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    R17Fixture()
    {
        init_family_registry();
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;

        set_global_context(&gc);
    }

    ~R17Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(R17Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->normal_stepsize = 1.0f;
    w->lineofsight = 8;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.game_world().oblist.push_back(std::move(w));
    return out;
}

walker* add_fx(R17Fixture& fx, char family, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::FX, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->team_num = 0;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.game_world().oblist.push_back(std::move(w));
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

PixieData make_grid(unsigned char fill, int w = 9, int h = 9)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = static_cast<unsigned char>(w);
    pd.h = static_cast<unsigned char>(h);
    pd.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        pd.data[i] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[x + y * pd.w] = v;
}

} // namespace

OG_UNIT_TEST(test_coverage_r17_family_mage_specials_and_reactions)
{
    R17Fixture fx;
    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    OG_ASSERT(mage != nullptr);

    living* self = add_living(fx, FAMILY_MAGE, 0, 64, 64);
    OG_ASSERT(self != nullptr);
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    self->myguy->name = "R17 Mage";
    self->myguy->intelligence = 70;
    self->user = 0;
    self->stats()->level = 8;
    self->stats()->magicpoints = 300.0f;

    self->current_special = 1;
    self->shifter_down = 1;
    OG_ASSERT(!mage->do_special(self));

    self->myguy->intelligence = 90;
    walker* marker = add_fx(fx, FAMILY_MARKER, 66, 64);
    OG_ASSERT(marker != nullptr);
    marker->owner = self;
    marker->dead = 0;
    self->busy = 0;
    self->current_special = 1;
    self->shifter_down = 1;
    OG_ASSERT(mage->do_special(self));

    self->busy = 0;
    self->current_special = 2;
    self->shifter_down = 0;
    self->lastx = 1.0f;
    self->lasty = 0.0f;
    OG_ASSERT(mage->do_special(self));

    living* foe = add_living(fx, FAMILY_ORC, 1, 96, 64);
    OG_ASSERT(foe != nullptr);
    self->stats()->hitpoints = self->stats()->max_hitpoints;
    self->stats()->level = 5;
    self->foe = nullptr;
    foe->foe = nullptr;
    mage->hit_response(self->stats(), foe);
    OG_ASSERT(self->foe == foe);
    OG_ASSERT(foe->foe == self);

    add_living(fx, FAMILY_ORC, 1, 100, 64);
    add_living(fx, FAMILY_ORC, 1, 104, 64);
    add_living(fx, FAMILY_ORC, 1, 108, 64);
    self->current_special = 1;
    OG_ASSERT(mage->check_special_ai(self));
}

OG_UNIT_TEST(test_coverage_r17_family_druid_protection_tree_and_faerie)
{
    R17Fixture fx;
    const FamilyDescriptor* druid = get_family_descriptor(FAMILY_DRUID);
    OG_ASSERT(druid != nullptr);

    living* self = add_living(fx, FAMILY_DRUID, 0, 80, 80);
    living* ally1 = add_living(fx, FAMILY_SOLDIER, 0, 84, 80);
    living* ally2 = add_living(fx, FAMILY_SOLDIER, 0, 88, 80);
    OG_ASSERT(self && ally1 && ally2);

    self->set_owned_myguy(std::make_unique<guy>(FAMILY_DRUID));
    self->stats()->level = 7;
    self->stats()->magicpoints = 300.0f;
    self->busy = 0;

    self->current_special = 1;
    self->lastx = 1.0f;
    self->lasty = 0.0f;
    OG_ASSERT(druid->do_special(self));

    walker* existing = fx.level.add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    OG_ASSERT(existing != nullptr);
    existing->owner = ally2;
    existing->team_num = ally2->team_num;
    existing->setxy(ally2->xpos, ally2->ypos);

    self->busy = 0;
    self->current_special = 4;
    OG_ASSERT(druid->do_special(self));

    self->busy = 0;
    self->current_special = 2;
    self->setxy(0, 0);
    self->lastx = -1.0f;
    self->lasty = 0.0f;
    (void)druid->do_special(self);
}

OG_UNIT_TEST(test_coverage_r17_walker_movement_and_act_cleanup)
{
    R17Fixture fx;

    walker* actor = fx.level.add_ob(Order::FX, FAMILY_EXPLOSION);
    OG_ASSERT(actor != nullptr);
    actor->setxy(static_cast<short>(64), static_cast<short>(64));
    actor->sizex = 16;
    actor->sizey = 16;
    actor->stepsize = 1.0f;
    actor->team_num = 0;

    living* dead_foe = add_living(fx, FAMILY_ORC, 1, 80, 64);
    living* dead_leader = add_living(fx, FAMILY_ORC, 1, 84, 64);
    living* dead_owner = add_living(fx, FAMILY_ORC, 1, 88, 64);
    OG_ASSERT(actor && dead_foe && dead_leader && dead_owner);
    dead_foe->dead = 1;
    dead_leader->dead = 1;
    dead_owner->dead = 1;

    actor->foe = dead_foe;
    actor->leader = dead_leader;
    actor->owner = dead_owner;
    actor->ani_type = ANI_WALK;
    actor->attack_lunge = 0.2f;
    actor->hit_recoil = 0.3f;
    actor->stats()->clear_command();
    actor->stats()->frozen_delay = 0;
    actor->set_act_type(ACT_CONTROL);
    (void)actor->act();

    assign_basic_ani(actor);
    actor->user = -1;
    actor->setxy(static_cast<short>(0), static_cast<short>(fx.level.pixmaxy - 1));
    actor->curdir = FACE_DOWN_LEFT;
    (void)actor->walkstep(-1.0f, 1.0f);

    actor->user = 0;
    actor->setxy(static_cast<short>(0), static_cast<short>(fx.level.pixmaxy - 1));
    actor->curdir = FACE_DOWN_LEFT;
    (void)actor->walkstep(-1.0f, 1.0f);

    actor->curdir = 127;
    actor->stepsize = 2.0f;
    (void)actor->turn(FACE_RIGHT);
}

OG_UNIT_TEST(test_coverage_r17_smooth_grass_water_and_dark_variants)
{
    SeqRandom rng{1, 2, 0, 0, 1, 0};
    GameContext gc;
    gc.rng = &rng;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 4;
    const int y = 4;

    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x - 1, y - 1, PIX_WATER1);
    set_at(pd, x + 1, y - 1, PIX_WATER1);
    set_at(pd, x - 1, y + 1, PIX_WATER1);
    set_at(pd, x, y - 1, PIX_WATER1);
    set_at(pd, x - 1, y, PIX_WATER1);
    s.smooth(x, y);

    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x + 1, y - 1, PIX_WATER1);
    set_at(pd, x + 1, y + 1, PIX_WATER1);
    set_at(pd, x - 1, y + 1, PIX_WATER1);
    set_at(pd, x + 1, y, PIX_WATER1);
    set_at(pd, x, y + 1, PIX_WATER1);
    s.smooth(x, y);

    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x - 1, y - 1, PIX_GRASS1);
    set_at(pd, x + 1, y - 1, PIX_GRASS1);
    set_at(pd, x - 1, y + 1, PIX_GRASS1);
    set_at(pd, x + 1, y + 1, PIX_GRASS1);
    set_at(pd, x, y - 1, PIX_GRASS1);
    set_at(pd, x, y + 1, PIX_GRASS1);
    set_at(pd, x - 1, y, PIX_GRASS1);
    set_at(pd, x + 1, y, PIX_GRASS1);
    s.smooth(x, y);
    s.smooth(x, y);

    set_at(pd, x, y, PIX_GRASS_DARK_1);
    set_at(pd, x - 1, y - 1, PIX_TREE_B1);
    set_at(pd, x, y - 1, PIX_TREE_B1);
    set_at(pd, x, y + 1, PIX_FLOOR1);
    s.smooth(x, y);

    set_global_context(nullptr);
}

OG_UNIT_TEST(test_coverage_r17_save_data_reset_defaults)
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

    OG_ASSERT(save.current_campaign == "org.openglad.gladiator");
    OG_ASSERT(save.team_size == 0);
    OG_ASSERT(save.scen_num == 1);
    OG_ASSERT(save.my_team == 0);
    OG_ASSERT(save.current_levels["org.openglad.gladiator"] == 1);
}
} // namespace detail_coverage_r17

// --- From test_coverage_r18.cpp ---
const FamilyDescriptor& describe_family_cleric();

namespace detail_coverage_r18 {
namespace {

class MenuOnlyClient final : public og::ui::IPickerClient
{
public:
    std::vector<const og::ui::PickerMenuItem*> scripted;
    int present_calls = 0;
    int handle_calls = 0;

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
    bool load_ok = true;
    bool save_ok = true;
    og::ui::PickerScreen after_game = og::ui::PickerScreen::MainMenu;

    int show_main_menu_calls = 0;
    int show_team_build_calls = 0;
    int show_campaign_calls = 0;
    int options_calls = 0;
    int help_calls = 0;
    int run_game_calls = 0;
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
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    MovementFixture()
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
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(x, y);
    walker* out = w.get();
    fx.level.game_world().oblist.push_back(std::move(w));
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

void assign_resetting_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[i][0] = 0;
        seqs[i][1] = -1;
        seqs[i][2] = -1;
        seqs[i][3] = -1;
        rows[i] = seqs[i].data();
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
            seqs[i][j] = static_cast<signed char>(j % 4);
        rows[i] = seqs[i].data();
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
        pd.data[i] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[x + y * pd.w] = v;
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

void test_clear_stale_view_controls(og::gameplay::GameWorld*)
{
    ++g_clear_stale_view_controls_calls;
}

} // namespace

OG_UNIT_TEST(test_coverage_r18_picker_show_main_and_team_build_mappings)
{
    MenuOnlyClient client;
    static const og::ui::PickerMenuItem unknown{"noop", "noop", og::ui::PickerMenuCommand::SetDifficulty, 0};
    static const og::ui::PickerMenuItem new_game{"new", "new", og::ui::PickerMenuCommand::BeginNewGame, 0};
    static const og::ui::PickerMenuItem cont{"continue", "continue", og::ui::PickerMenuCommand::ContinueGame, 0};
    static const og::ui::PickerMenuItem options{"options", "options", og::ui::PickerMenuCommand::Options, 0};
    static const og::ui::PickerMenuItem help{"help", "help", og::ui::PickerMenuCommand::Help, 0};
    static const og::ui::PickerMenuItem quit{"quit", "quit", og::ui::PickerMenuCommand::Quit, 0};
    static const og::ui::PickerMenuItem go{"go", "go", og::ui::PickerMenuCommand::StartGame, 0};
    static const og::ui::PickerMenuItem back{"back", "back", og::ui::PickerMenuCommand::Back, 0};

    client.scripted = {&unknown, &new_game};
    client.present_calls = 0;
    OG_ASSERT(client.show_main_menu() == og::ui::MainMenuAction::NewGame);
    OG_ASSERT(client.handle_calls == 1);

    client.scripted = {&cont};
    client.present_calls = 0;
    OG_ASSERT(client.show_main_menu() == og::ui::MainMenuAction::ViewTeam);

    client.scripted = {&options};
    client.present_calls = 0;
    OG_ASSERT(client.show_main_menu() == og::ui::MainMenuAction::Options);

    client.scripted = {&help};
    client.present_calls = 0;
    OG_ASSERT(client.show_main_menu() == og::ui::MainMenuAction::Help);

    client.scripted = {&quit};
    client.present_calls = 0;
    OG_ASSERT(client.show_main_menu() == og::ui::MainMenuAction::Quit);

    client.scripted = {nullptr};
    client.present_calls = 0;
    OG_ASSERT(client.show_main_menu() == og::ui::MainMenuAction::Quit);

    client.scripted = {&unknown, &go};
    client.present_calls = 0;
    const int old_handles = client.handle_calls;
    OG_ASSERT(client.show_team_build() == og::ui::TeamBuildAction::PlayGame);
    OG_ASSERT(client.handle_calls == old_handles + 1);

    client.scripted = {&back};
    client.present_calls = 0;
    OG_ASSERT(client.show_team_build() == og::ui::TeamBuildAction::BackToMainMenu);

    client.scripted = {nullptr};
    client.present_calls = 0;
    OG_ASSERT(client.show_team_build() == og::ui::TeamBuildAction::BackToMainMenu);
}

OG_UNIT_TEST(test_coverage_r18_picker_run_picker_state_switches)
{
    ScriptedPickerClient a;
    a.main_actions = {
        og::ui::MainMenuAction::NewGame,
        og::ui::MainMenuAction::LoadGame,
        og::ui::MainMenuAction::SaveGame,
        og::ui::MainMenuAction::Options,
        og::ui::MainMenuAction::Help,
        og::ui::MainMenuAction::Quit,
    };
    a.campaign_result = "campaign";
    a.team_build_action = og::ui::TeamBuildAction::BackToMainMenu;
    og::ui::run_picker(a);
    OG_ASSERT(a.prep_calls == 1);
    OG_ASSERT(a.show_campaign_calls == 1);
    OG_ASSERT(a.load_calls == 1);
    OG_ASSERT(a.save_calls == 1);
    OG_ASSERT(a.options_calls == 1);
    OG_ASSERT(a.help_calls == 1);
    OG_ASSERT(a.show_team_build_calls == 1);
    OG_ASSERT(a.run_game_calls == 0);

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
    OG_ASSERT(b.prep_calls == 1);
    OG_ASSERT(b.show_campaign_calls == 0);
    OG_ASSERT(b.load_calls == 1);
    OG_ASSERT(b.save_calls == 1);
    OG_ASSERT(b.show_team_build_calls == 5);
    OG_ASSERT(b.run_game_calls == 0);

    ScriptedPickerClient c;
    c.main_actions = {og::ui::MainMenuAction::ViewTeam};
    c.team_build_action = og::ui::TeamBuildAction::PlayGame;
    c.after_game = og::ui::PickerScreen::Quit;
    og::ui::run_picker(c);
    OG_ASSERT(c.show_team_build_calls == 1);
    OG_ASSERT(c.run_game_calls == 1);

    ScriptedPickerClient d;
    d.main_actions = {og::ui::MainMenuAction::ContinueGame};
    d.after_game = static_cast<og::ui::PickerScreen>(99);
    og::ui::run_picker(d);
    OG_ASSERT(d.run_game_calls == 1);
    OG_ASSERT(d.show_main_menu_calls == 1);

    ScriptedPickerClient e;
    e.main_actions = {og::ui::MainMenuAction::ContinueGame};
    e.after_game = og::ui::PickerScreen::Quit;
    og::ui::run_picker(e);
    OG_ASSERT(e.run_game_calls == 1);
}

OG_UNIT_TEST(test_coverage_r18_family_cleric_check_special_default_false)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    MovementFixture fx;
    living self;
    self.set_order_family(Order::Living, FAMILY_CLERIC);
    fx.level.wire_entity(&self);
    self.current_special = 1;
    self.stats()->max_magicpoints = 100.0f;
    self.stats()->magicpoints = 1.0f;
    OG_ASSERT(!desc.check_special_ai(&self));
}

OG_UNIT_TEST(test_coverage_r18_walker_movement_blocked_user_paths)
{
    MovementFixture fx;
    walker* user = add_living(fx, static_cast<short>(fx.level.pixmaxx - 1), static_cast<short>(fx.level.pixmaxy - 1));
    OG_ASSERT(user != nullptr);
    assign_basic_ani(user);

    user->user = 0;
    user->curdir = FACE_DOWN_RIGHT;
    OG_ASSERT(!user->walkstep(1.0f, 1.0f));

    user->setxy(0, 0);
    user->curdir = FACE_UP;
    OG_ASSERT(!user->walkstep(0.0f, -1.0f));

    // Hit user-slide branches where only one axis can move.
    user->setxy(static_cast<short>(fx.level.pixmaxx - 1), static_cast<short>(10));
    user->curdir = FACE_DOWN_RIGHT;
    (void)user->walkstep(1.0f, 1.0f);

    user->setxy(static_cast<short>(10), static_cast<short>(fx.level.pixmaxy - 1));
    user->curdir = FACE_DOWN_LEFT;
    (void)user->walkstep(-1.0f, 1.0f);

    user->setxy(static_cast<short>(fx.level.pixmaxx - 1), static_cast<short>(10));
    user->curdir = FACE_UP_RIGHT;
    (void)user->walkstep(1.0f, -1.0f);

    // Stationary family short-circuit in walk().
    walker* tower = add_living(fx, 32, 32);
    OG_ASSERT(tower != nullptr);
    tower->set_order_family(Order::Living, FAMILY_TOWER1);
    OG_ASSERT(tower->walk(1.0f, 0.0f));

    // BIT_ANIMATE invalid-move branch (blocked by terrain, not map edge).
    user->setxy(20, 20);
    user->stats()->set_bit_flags(BIT_ANIMATE, 1);
    assign_resetting_ani(user);
    const int blocked_x = (user->xpos - 1) / GRID_SIZE;
    const int blocked_y = user->ypos / GRID_SIZE;
    const int blocked_index = blocked_x + blocked_y * fx.level.grid.w;
    fx.level.grid.data[blocked_index] = PIX_H_WALL1;
    user->curdir = FACE_LEFT;
    OG_ASSERT(!user->walk(-1.0f, 0.0f));
}

OG_UNIT_TEST(test_coverage_r18_walker_act_decay_cleanup_and_guard_branches)
{
    MovementFixture fx;
    walker* self = add_living(fx, 64, 64);
    walker* foe = add_living(fx, 80, 64);
    walker* leader = add_living(fx, 60, 64);
    walker* owner = add_living(fx, 64, 80);
    OG_ASSERT(self && foe && leader && owner);

    self->foe = foe;
    self->leader = leader;
    self->owner = owner;
    foe->dead = 1;
    leader->dead = 1;
    owner->dead = 1;

    self->attack_lunge = 0.2f;
    self->hit_recoil = 0.2f;
    self->set_act_type(ACT_CONTROL);
    OG_ASSERT(self->act());
    OG_ASSERT(self->foe == nullptr);
    OG_ASSERT(self->leader == nullptr);
    OG_ASSERT(self->owner == nullptr);
    OG_ASSERT(self->attack_lunge == 0.0f);
    OG_ASSERT(self->hit_recoil == 0.0f);

    self->team_num = 0;
    foe->dead = 0;
    foe->team_num = 1;
    self->setxy(64, 64);
    foe->setxy(72, 64);
    self->set_act_type(ACT_GUARD);
    (void)self->act();
}

OG_UNIT_TEST(test_coverage_r18_walker_animate_invalid_sequence_guard)
{
    MovementFixture fx;
    walker* self = add_living(fx, 64, 64);
    OG_ASSERT(self != nullptr);

    assign_long_ani_no_sentinel(self);
    self->ani_type = ANI_ATTACK;
    self->cycle = 0;

    OG_ASSERT(self->animate() == 0);
    OG_ASSERT(self->ani_type == ANI_WALK);
    OG_ASSERT(self->cycle == 0);
}

OG_UNIT_TEST(test_coverage_r18_level_data_resize_and_delete_cleanup_branches)
{
    MovementFixture fx;
    walker* keep = add_living(fx, 10, 10);
    walker* off_map = add_living(fx, 400, 400);
    OG_ASSERT(keep && off_map);

    fx.level.game_world().oblist.push_back(std::unique_ptr<walker>{});
    fx.level.game_world().fxlist.push_back(std::unique_ptr<walker>{});
    fx.level.game_world().weaplist.push_back(std::unique_ptr<walker>{});

    fx.level.resize_grid(3, 3);
    for (auto& uptr : fx.level.game_world().oblist)
    {
        OG_ASSERT(uptr != nullptr);
        OG_ASSERT(uptr.get() != off_map);
    }
    for (auto& uptr : fx.level.game_world().fxlist)
        OG_ASSERT(uptr != nullptr);
    for (auto& uptr : fx.level.game_world().weaplist)
        OG_ASSERT(uptr != nullptr);

    LevelDataHooks hooks{};
    hooks.clear_stale_view_controls = test_clear_stale_view_controls;
    g_clear_stale_view_controls_calls = 0;
    LevelData with_hooks(1, true, &hooks);
    with_hooks.create_new_grid();
    with_hooks.myobmap->walker_to_pos[reinterpret_cast<walker*>(0x1)] = {};
    with_hooks.delete_objects();
    OG_ASSERT(g_clear_stale_view_controls_calls == 1);
    OG_ASSERT(with_hooks.myobmap->walker_to_pos.empty());
    OG_ASSERT(with_hooks.myobmap->pos_to_walker.empty());
}

OG_UNIT_TEST(test_coverage_r18_smooth_targeted_mask_branches)
{
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);
    const int x = 4;
    const int y = 4;

    // TYPE_GRASS water corner path (lines 232-233).
    set_at(pd, x, y, PIX_GRASS1);
    set_at(pd, x + 1, y - 1, PIX_WATER1);
    set_at(pd, x + 1, y + 1, PIX_WATER1);
    set_at(pd, x - 1, y + 1, PIX_WATER1);
    set_at(pd, x + 1, y, PIX_WATER1);
    set_at(pd, x, y + 1, PIX_WATER1);
    s.smooth(x, y);

    // Wall around masks: 3, 9, and default (2).
    set_neighbors_mask(pd, x, y, PIX_WALL2, PIX_WALL2, PIX_GRASS1, TO_UP | TO_RIGHT);
    s.smooth(x, y);
    set_neighbors_mask(pd, x, y, PIX_WALL2, PIX_WALL2, PIX_GRASS1, TO_UP | TO_LEFT);
    s.smooth(x, y);
    set_neighbors_mask(pd, x, y, PIX_WALL2, PIX_WALL2, PIX_GRASS1, TO_RIGHT);
    s.smooth(x, y);

    // Water single-neighbor branches that depend on rng(2).
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_UP);
    s.smooth(x, y);
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_DOWN);
    s.smooth(x, y);
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_LEFT);
    s.smooth(x, y);
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_RIGHT);
    s.smooth(x, y);

    // Trees TO_AROUND rng switch and dark dirt TO_AROUND switch.
    set_neighbors_mask(pd, x, y, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1, TO_AROUND);
    set_at(pd, x - 1, y - 1, PIX_TREE_B1);
    set_at(pd, x + 1, y - 1, PIX_TREE_B1);
    set_at(pd, x - 1, y + 1, PIX_TREE_B1);
    set_at(pd, x + 1, y + 1, PIX_TREE_B1);
    s.smooth(x, y);
    s.smooth(x, y);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, TO_AROUND);
    s.smooth(x, y);

    // set_x_y() no-grid guard (line 903).
    smoother empty;
    (void)empty.smooth(0, 0);

    set_global_context(nullptr);
}

OG_UNIT_TEST(test_coverage_r18_gparser_more_commandline_switches)
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
    OG_ASSERT(local_cfg.get_setting("sound", "sound") == "on");
    OG_ASSERT(local_cfg.get_setting("graphics", "render") == "sai");
    OG_ASSERT(local_cfg.get_setting("graphics", "fullscreen") == "on");
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
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    R19Fixture()
    {
        init_family_registry();
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;

        set_global_context(&gc);
    }

    ~R19Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(R19Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->normal_stepsize = 1.0f;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.game_world().oblist.push_back(std::move(w));
    return out;
}

void assign_short_ani(walker* w)
{
    static std::array<std::array<signed char, 3>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[i][0] = 0;
        seqs[i][1] = -1;
        seqs[i][2] = -1;
        rows[i] = seqs[i].data();
    }
    w->ani = rows.data();
}

} // namespace

OG_UNIT_TEST(test_coverage_r19_family_cleric_check_special_true_paths)
{
    R19Fixture fx;
    const FamilyDescriptor& desc = describe_family_cleric();

    living* self = add_living(fx, FAMILY_CLERIC, 0, 64, 64);
    living* ally = add_living(fx, FAMILY_SOLDIER, 0, 70, 64);
    OG_ASSERT(self && ally);

    self->current_special = 1;
    self->stats()->max_magicpoints = 100.0f;
    self->stats()->magicpoints = 80.0f;
    self->shifter_down = 1;
    OG_ASSERT(desc.check_special_ai(self));
    OG_ASSERT(self->shifter_down == 0);

    ally->setxy(300, 300);
    self->shifter_down = 0;
    OG_ASSERT(desc.check_special_ai(self));
    OG_ASSERT(self->shifter_down == 1);
}

OG_UNIT_TEST(test_coverage_r19_walker_init_fire_busy_and_fire_fail_paths)
{
    R19Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 48, 48);
    OG_ASSERT(self != nullptr);

    const short fire_dir = self->facing(1, 0);
    self->curdir = static_cast<char>(fire_dir);
    self->enddir = static_cast<char>(fire_dir);
    self->busy = 1.0f;
    OG_ASSERT(!self->init_fire(1, 0));

    self->busy = 0.0f;
    self->ani_type = ANI_ATTACK;
    self->stats()->weapon_cost = 1;
    self->stats()->magicpoints = 0.0f;
    OG_ASSERT(!self->init_fire(1, 0));
}

OG_UNIT_TEST(test_coverage_r19_walker_animate_attack_completion_branch)
{
    R19Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    OG_ASSERT(self != nullptr);

    assign_short_ani(self);
    self->ani_type = ANI_ATTACK;
    self->cycle = 0;
    self->stats()->weapon_cost = 1;
    self->stats()->magicpoints = 0.0f;

    OG_ASSERT(self->animate());
    OG_ASSERT(self->ani_type == ANI_WALK);
    OG_ASSERT(self->cycle == 0);
}

OG_UNIT_TEST(test_coverage_r19_walker_act_random_paths)
{
    R19Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    living* foe = add_living(fx, FAMILY_ORC, 1, 120, 64);
    OG_ASSERT(self && foe);

    self->lineofsight = 1;
    self->foe = nullptr;
    self->set_act_type(ACT_RANDOM);
    SeqRandom rng_find_and_move{0, 1, 0};
    (void)self->act();

    foe->dead = 1;
    self->foe = nullptr;
    SeqRandom rng_find_none{0, 1, 0};
    (void)self->act();
}
} // namespace detail_coverage_r19

// --- From test_coverage_r20.cpp ---
const FamilyDescriptor& describe_family_mage();
const FamilyDescriptor& describe_family_druid();

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
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    ConstantRandom rng{1};
    GameContext gc;

    R20Fixture()
    {
        init_family_registry();
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);

        gc.rng = &rng;

        set_global_context(&gc);
    }

    ~R20Fixture()
    {
        set_global_context(nullptr);
    }
};

walker* add_walker(R20Fixture& fx, Order order, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(order, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->normal_stepsize = 1.0f;
    w->lineofsight = 4;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    if (order == Order::Weapon)
        fx.level.game_world().weaplist.push_back(std::move(w));
    else
        fx.level.game_world().oblist.push_back(std::move(w));
    return out;
}

living* add_living(R20Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->normal_stepsize = 1.0f;
    w->lineofsight = 4;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.game_world().oblist.push_back(std::move(w));
    return out;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[x + y * pd.w] = v;
}

PixieData make_grid(unsigned char fill, int w = 7, int h = 7)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = static_cast<unsigned char>(w);
    pd.h = static_cast<unsigned char>(h);
    pd.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        pd.data[i] = fill;
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

OG_UNIT_TEST(test_coverage_r20_walker_act_random_no_foe_and_chase_paths)
{
    R20Fixture fx;

    walker* self = add_walker(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    OG_ASSERT(self != nullptr);

    SequenceRandom rng_no_foe{0, 1, 1};
    self->foe = nullptr;
    self->lineofsight = 1;
    self->set_act_type(ACT_RANDOM);
    (void)self->act();

    walker* foe = add_walker(fx, Order::Living, FAMILY_ORC, 1, 220, 64);
    OG_ASSERT(foe != nullptr);
    self->stats()->clear_command();
    SequenceRandom rng_chase{0, 1, 1};
    self->foe = foe;
    self->lineofsight = 1;
    self->collide_ob = reinterpret_cast<walker*>(0x1);
    (void)self->act();
    OG_ASSERT(self->collide_ob == nullptr);
}

OG_UNIT_TEST(test_coverage_r20_walker_movement_stationary_walkstep_walk_turn)
{
    R20Fixture fx;

    walker* tower = add_walker(fx, Order::Living, FAMILY_TOWER1, 0, 80, 80);
    OG_ASSERT(tower != nullptr);

    tower->stepsize = 2.0f;
    const float old_lastx = tower->lastx;
    const float old_lasty = tower->lasty;

    OG_ASSERT(tower->walkstep(1.0f, 0.0f));
    OG_ASSERT(tower->lastx == 1.0f);
    OG_ASSERT(tower->lasty == 0.0f);

    OG_ASSERT(tower->walk(0.0f, -1.0f));

    tower->curdir = FACE_UP;
    tower->turn(FACE_LEFT);
    OG_ASSERT(tower->lastx == 1.0f);
    OG_ASSERT(tower->lasty == 0.0f);
    OG_ASSERT(tower->curdir != FACE_UP);
    OG_ASSERT(old_lastx != tower->lastx || old_lasty != tower->lasty);
}

OG_UNIT_TEST(test_coverage_r20_level_data_add_paths_and_clear_reset)
{
    R20Fixture fx;

    walker* as_weapon = fx.level.add_ob(Order::Weapon, FAMILY_ARROW);
    OG_ASSERT(as_weapon != nullptr);

    walker* fx_ob = fx.level.add_fx_ob(Order::FX, FAMILY_HIT);
    OG_ASSERT(fx_ob != nullptr);

    walker* weap = fx.level.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    OG_ASSERT(weap != nullptr);

    fx.level.title = "changed";
    fx.level.type = 7;
    fx.level.par_value = 9;
    fx.level.time_bonus_limit = 10;
    fx.level.clear();

    OG_ASSERT(fx.level.title == "New Level");
    OG_ASSERT(fx.level.type == 0);
    OG_ASSERT(fx.level.par_value == 1);
    OG_ASSERT(fx.level.time_bonus_limit == 4000);
    walker dummy;
    OG_ASSERT(fx.level.remove_ob(&dummy) == 0);
}

OG_UNIT_TEST(test_coverage_r20_smooth_dark_grass_specific_branches)
{
    ConstantRandom rng1{1};
    GameContext gc;
    gc.rng = &rng1;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 3;
    const int y = 3;

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_UP | TO_DOWN | TO_LEFT);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_DOWN);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_LEFT | TO_DOWN);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_WATER1,
                       TO_LEFT | TO_DOWN);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_WATER1,
                       TO_DOWN);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_RIGHT | TO_UP);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_WATER1,
                       TO_RIGHT | TO_UP);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_RIGHT);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_WATER1,
                       TO_RIGHT);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_UP);
    s.smooth(x, y);

    set_neighbors_mask(pd, x, y, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                       TO_UP | TO_DOWN);
    s.smooth(x, y);

    set_global_context(nullptr);
}

OG_UNIT_TEST(test_coverage_r20_family_cleric_do_special_guard_conditions)
{
    const FamilyDescriptor& desc = describe_family_cleric();
    living self;

    self.set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    self.user = 0;

    self.current_special = 1;
    self.shifter_down = 1;
    self.busy = 1.0f;
    OG_ASSERT(!desc.do_special(&self));

    self.busy = 0.0f;
    self.myguy->intelligence = 40;
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 2;
    self.shifter_down = 1;
    self.busy = 1.0f;
    OG_ASSERT(!desc.do_special(&self));

    self.busy = 0.0f;
    self.myguy->intelligence = 30;
    const float old_busy2 = self.busy;
    OG_ASSERT(!desc.do_special(&self));
    OG_ASSERT(self.busy > old_busy2);

    self.current_special = 3;
    self.shifter_down = 1;
    self.busy = 0.0f;
    self.myguy->intelligence = 30;
    OG_ASSERT(!desc.do_special(&self));
}

OG_UNIT_TEST(test_coverage_r20_family_mage_do_special_guard_conditions)
{
    const FamilyDescriptor& desc = describe_family_mage();
    living self;

    self.set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    self.user = 0;

    self.current_special = 1;
    self.ani_type = ANI_TELE_OUT;
    OG_ASSERT(!desc.do_special(&self));

    self.ani_type = ANI_WALK;
    self.shifter_down = 1;
    self.busy = 1.0f;
    OG_ASSERT(!desc.do_special(&self));

    self.busy = 0.0f;
    self.myguy->intelligence = 40;
    OG_ASSERT(!desc.do_special(&self));

    self.shifter_down = 0;
    self.ani_type = ANI_WALK;
    OG_ASSERT(desc.do_special(&self));
    OG_ASSERT(self.ani_type == ANI_TELE_OUT);
}

OG_UNIT_TEST(test_coverage_r20_family_druid_do_special_default_and_busy_guards)
{
    const FamilyDescriptor& desc = describe_family_druid();
    R20Fixture fx;

    living* self = add_living(fx, FAMILY_DRUID, 0, 64, 64);
    OG_ASSERT(self != nullptr);

    self->current_special = 4;
    self->busy = 0.0f;
    OG_ASSERT(!desc.do_special(self));

    self->current_special = 1;
    self->busy = 1.0f;
    OG_ASSERT(!desc.do_special(self));

    self->current_special = 2;
    self->busy = 1.0f;
    OG_ASSERT(!desc.do_special(self));
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
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{1};
    GameContext gc;

    FinalR16Fixture()
    {
        init_family_registry();
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;

        set_global_context(&gc);
    }

    ~FinalR16Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(FinalR16Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 2.0f;
    w->normal_stepsize = 2.0f;
    w->lineofsight = 8;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.game_world().oblist.push_back(std::move(w));
    return out;
}

walker* add_fx(FinalR16Fixture& fx, char family, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::FX, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->team_num = 0;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.game_world().oblist.push_back(std::move(w));
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

OG_UNIT_TEST(test_final_r16_combat_math_edges)
{
    SeqRandom rng{3, 0, 17, 5, 9};

    OG_ASSERT(compute_base_damage(25.0f, static_cast<RandomU32>(nullptr)) >= 22.0f);
    OG_ASSERT(compute_damage_reduction(0.0f, 100.0f) == 0.0f);
    OG_ASSERT(compute_damage_reduction(5.0f, 99.0f) == 4.0f);
    OG_ASSERT(compute_post_reduction_damage(3.0f, 99.0f) == 1.0f);

    OG_ASSERT(compute_freeze_duration(2, 9999, rng) == 0);
    OG_ASSERT(compute_freeze_duration(5, 0, rng) >= 0);

    const HealResult hr = compute_heal_amount(40, 3, rng);
    OG_ASSERT(hr.amount > hr.cost);
    OG_ASSERT(compute_charm_duration(-3, rng) == 25);

    const RegenTickResult r0 = compute_regen_tick(10.0f, 10.0f, 1.0f, 0, 2, false);
    OG_ASSERT(r0.new_value == 10.0f);

    const RegenTickResult r1 = compute_regen_tick(3.0f, 8.0f, 1.5f, 1, 2, false);
    OG_ASSERT(r1.new_delay == 0);
    OG_ASSERT(r1.new_value >= 4.5f);

    const HpRegenResult h0 = compute_hp_regen_tick(5.0f, 10.0f, 1.0f, 0, 4, 2, false);
    OG_ASSERT(h0.new_regen_delay == 1);

    const HpRegenResult h1 = compute_hp_regen_tick(5.0f, 10.0f, 1.0f, 3, 4, 0, false);
    OG_ASSERT(h1.new_hp >= 6.0f);

    OG_ASSERT(compute_xp_from_attack(10, 0.0f) == 0);
    OG_ASSERT(compute_xp_from_kill(0) >= 0);
    OG_ASSERT(compute_xp_from_action(ExpAction::TurnUndead, 4, 2, 7, rng) == 21);
    OG_ASSERT(compute_xp_from_action(ExpAction::RaiseSkeleton, 1, 1, 0, rng) == 45);
    OG_ASSERT(compute_xp_from_action(ExpAction::ResurrectPenalty, 2, 3, 0, rng) == 900);
}

OG_UNIT_TEST(test_final_r16_family_difficulty_levelup_and_ai_checks)
{
    FinalR16Fixture fx;
    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    const FamilyDescriptor* druid = get_family_descriptor(FAMILY_DRUID);
    const FamilyDescriptor* soldier = get_family_descriptor(FAMILY_SOLDIER);
    const FamilyDescriptor* thief = get_family_descriptor(FAMILY_THIEF);
    const FamilyDescriptor* cleric = get_family_descriptor(FAMILY_CLERIC);
    OG_ASSERT(mage && druid && soldier && thief && cleric);

    guy g_mage(FAMILY_MAGE);
    const short old_int = g_mage.intelligence;
    mage->level_up(&g_mage, 2);
    OG_ASSERT(g_mage.intelligence > old_int);

    guy g_druid(FAMILY_DRUID);
    const short old_dex = g_druid.dexterity;
    druid->level_up(&g_druid, 2);
    OG_ASSERT(g_druid.dexterity > old_dex);

    living self;
    const float hp0 = self.stats()->max_hitpoints;
    soldier->set_difficulty(&self, 3);
    OG_ASSERT(self.stats()->max_hitpoints > hp0);

    FinalR16Fixture fx2;
    living* caster = add_living(fx2, FAMILY_MAGE, 0, 64, 64);
    OG_ASSERT(caster != nullptr);

    caster->current_special = 1;
    OG_ASSERT(mage->check_special_ai(caster));

    add_living(fx2, FAMILY_ORC, 1, 74, 64);
    add_living(fx2, FAMILY_ORC, 1, 84, 64);
    (void)mage->check_special_ai(caster);

    add_living(fx2, FAMILY_ORC, 1, 94, 64);
    add_living(fx2, FAMILY_ORC, 1, 104, 64);
    OG_ASSERT(mage->check_special_ai(caster));

    caster->foe = add_living(fx2, FAMILY_ORC, 1, 114, 64);
    OG_ASSERT(soldier->check_special_ai(caster));
    caster->foe->setxy(66, 64);
    OG_ASSERT(!soldier->check_special_ai(caster));

    caster->current_special = 1;
    caster->foe = nullptr;
    (void)thief->check_special_ai(caster);

    caster->current_special = 3;
    caster->shifter_down = 1;
    (void)thief->check_special_ai(caster);

    caster->current_special = 1;
    caster->stats()->magicpoints = caster->stats()->max_magicpoints;
    OG_ASSERT(cleric->check_special_ai(caster));
}

OG_UNIT_TEST(test_final_r16_walker_specials_teleport_and_turn_undead)
{
    FinalR16Fixture fx;
    living* self = add_living(fx, FAMILY_MAGE, 0, 64, 64);
    living* undead = add_living(fx, FAMILY_SKELETON, 1, 120, 64);
    OG_ASSERT(self && undead);

    self->current_special = 1;
    self->dead = 1;
    OG_ASSERT(!self->special());

    self->dead = 0;
    self->stats()->special_cost[1] = 1;
    self->stats()->magicpoints = 0;
    OG_ASSERT(!self->special());

    self->stats()->magicpoints = 999.0f;
    fx.level.delete_grid();
    OG_ASSERT(!self->teleport());
    fx.level.create_new_grid();

    walker* marker = add_fx(fx, FAMILY_MARKER, 200, 200);
    marker->owner = self;
    marker->lifetime = 1;
    OG_ASSERT(self->teleport());
    OG_ASSERT(marker->lifetime <= 1);

    self->setxy(64, 64);
    undead->setxy(80, 64);
    const std::int32_t killed = self->turn_undead(120, 5);
    OG_ASSERT(killed >= 0);

    for (auto& uptr : fx.level.game_world().oblist)
        uptr->dead = 1;
    OG_ASSERT(self->turn_undead(120, 5) == -1);
}

OG_UNIT_TEST(test_final_r16_sim_input_switch_special_and_yell)
{
    FinalR16Fixture fx;
    living* control_living = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    living* follower = add_living(fx, FAMILY_SOLDIER, 0, 96, 64);
    OG_ASSERT(control_living && follower);

    walker* control = control_living;

    control_living->set_act_type(ACT_CONTROL);
    control_living->user = 0;
    follower->set_act_type(ACT_GUARD);
    follower->leader = nullptr;

    std::string special_names[NUM_FAMILIES][6];
    for (int i = 0; i < NUM_FAMILIES; ++i)
        for (int j = 0; j < 6; ++j)
            special_names[i][j] = "NONE";
    special_names[FAMILY_SOLDIER][1] = "CHARGE";

    SimInputDebounce debounce;
    PlayerInput pi{};

    control_living->current_special = 4;
    pi.pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
    const SimInputResult s0 = sim_process_player_input(
        pi, control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(!s0.endgame_requested);
    OG_ASSERT(control_living->current_special == 1);

    pi = {};
    pi.pressed[static_cast<int>(InputAction::Yell)] = true;
    const SimInputResult s1 = sim_process_player_input(
        pi, control, fx.level, 0, 0, debounce, special_names, &fx.events);
    OG_ASSERT(s1.play_sound == SOUND_YO);
    OG_ASSERT(s1.notify_text == "Yo!");
    OG_ASSERT(control_living->yo_delay == 30);
    OG_ASSERT(follower->leader == control_living);
}

OG_UNIT_TEST(test_final_r16_smooth_targeted_grass_and_dark_variants)
{
    SeqRandom seq{1, 2, 0, 1, 0, 3};
    GameContext gc;
    gc.rng = &seq;
    set_global_context(&gc);

    smoother s;
    PixieData pd;
    pd.frames = 1;
    pd.w = 5;
    pd.h = 5;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int i = 0; i < 25; ++i)
        pd.data[i] = PIX_GRASS1;
    s.set_target(pd);

    const int x = 2;
    const int y = 2;

    // Trigger TYPE_GRASS random cases 1 and 2.
    pd.data[x + y * pd.w] = PIX_GRASS1;
    pd.data[x + (y - 1) * pd.w] = PIX_GRASS1;
    pd.data[x + 1 + y * pd.w] = PIX_GRASS1;
    pd.data[x + (y + 1) * pd.w] = PIX_GRASS1;
    pd.data[x - 1 + y * pd.w] = PIX_GRASS1;
    OG_ASSERT(s.smooth(x, y) == 1);
    OG_ASSERT(s.query_x_y(x, y) == PIX_GRASS2);
    OG_ASSERT(s.smooth(x, y) == 1);
    OG_ASSERT(s.query_x_y(x, y) == PIX_GRASS3);

    // Trigger explicit water-corner conversions.
    pd.data[x + y * pd.w] = PIX_GRASS1;
    pd.data[x - 1 + (y - 1) * pd.w] = PIX_WATER1;
    pd.data[x - 1 + (y + 1) * pd.w] = PIX_WATER1;
    pd.data[x + (y - 1) * pd.w] = PIX_WATER1;
    pd.data[x - 1 + y * pd.w] = PIX_WATER1;
    pd.data[x + 1 + (y + 1) * pd.w] = PIX_WATER1;
    OG_ASSERT(s.smooth(x, y) == 1);
    const Sint32 g = s.query_x_y(x, y);
    OG_ASSERT(g > 0);

    pd.data[x + y * pd.w] = PIX_GRASS_DARK_1;
    pd.data[x + (y + 1) * pd.w] = PIX_GRASS1;
    pd.data[x + (y - 1) * pd.w] = PIX_WALL3;
    OG_ASSERT(s.smooth(x, y) == 1);
    const Sint32 d = s.query_x_y(x, y);
    OG_ASSERT(d > 0);

    set_global_context(nullptr);
}

OG_UNIT_TEST(test_final_r16_stats_walker_level_data_and_picker_state)
{
    FinalR16Fixture fx;
    living* a = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    living* b = add_living(fx, FAMILY_ORC, 1, 96, 64);
    OG_ASSERT(a && b);

    a->stats()->set_command(COMMAND_DIE, 1);
    a->stats()->set_command(COMMAND_WALK, 1);
    a->stats()->try_command(COMMAND_RANDOM_WALK, 1);
    OG_ASSERT(a->stats()->has_commands());

    a->curdir = FACE_UP;
    (void)a->stats()->right_blocked();
    a->curdir = FACE_DOWN_LEFT;
    (void)a->stats()->right_forward_blocked();
    a->curdir = FACE_RIGHT;
    (void)a->stats()->right_back_blocked();

    (void)a->walkstep(-1.0f, -1.0f);
    (void)a->walkstep(1.0f, 1.0f);

    OG_ASSERT(fx.level.find_near_foe(a) == b);
    std::int32_t howmany = 0;
    OG_ASSERT(!fx.level.find_foes_in_range(fx.level.game_world().oblist, 120, &howmany, a).empty());

    MenuClient client;
    static const og::ui::PickerMenuItem unknown{"u", "u", og::ui::PickerMenuCommand::SetDifficulty, 0};
    static const og::ui::PickerMenuItem start{"s", "s", og::ui::PickerMenuCommand::StartGame, 0};
    static const og::ui::PickerMenuItem quit{"q", "q", og::ui::PickerMenuCommand::Quit, 0};
    client.main_items = {&unknown, &quit};
    client.team_items = {&start};

    const og::ui::MainMenuAction action = client.show_main_menu();
    OG_ASSERT(action == og::ui::MainMenuAction::Quit);
    OG_ASSERT(client.handled == 1);

    client.main_i = 0;
    client.main_items = {&quit};
    client.after = og::ui::PickerScreen::Help;
    og::ui::run_picker(client);
}
} // namespace detail_final_coverage_r16
