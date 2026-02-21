#include <openglad/ui/picker_state.h>
#include <openglad/ui/menu_model.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/data/smooth.h>
#include <openglad/data/pixie_data.h>
#include <openglad/data/level_data_hooks.h>
#include <openglad/runtime/game_context.h>
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

void test_clear_stale_view_controls(LevelData*)
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

    fx.level.oblist.push_back(std::unique_ptr<walker>{});
    fx.level.fxlist.push_back(std::unique_ptr<walker>{});
    fx.level.weaplist.push_back(std::unique_ptr<walker>{});

    fx.level.resize_grid(3, 3);
    for (auto& uptr : fx.level.oblist)
    {
        OG_ASSERT(uptr != nullptr);
        OG_ASSERT(uptr.get() != off_map);
    }
    for (auto& uptr : fx.level.fxlist)
        OG_ASSERT(uptr != nullptr);
    for (auto& uptr : fx.level.weaplist)
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
