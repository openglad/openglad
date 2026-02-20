#include <openglad/ui/picker_state.h>
#include <openglad/ui/menu_model.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "unit/unit.h"

const FamilyDescriptor& describe_family_cleric();

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
