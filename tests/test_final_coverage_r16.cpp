#include <openglad/core/combat_math.h>
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
#include <openglad/input/input_action.h>
#include <openglad/input/input_state.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/irandom.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/sim_input_handler.h>
#include <openglad/ui/picker_state.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "unit/unit.h"

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
        gc.config = &cfg;
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
    fx.level.oblist.push_back(std::move(w));
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
    fx.level.oblist.push_back(std::move(w));
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
    OG_ASSERT(!mage->check_special_ai(caster));

    add_living(fx2, FAMILY_ORC, 1, 94, 64);
    add_living(fx2, FAMILY_ORC, 1, 104, 64);
    OG_ASSERT(mage->check_special_ai(caster));

    caster->foe = add_living(fx2, FAMILY_ORC, 1, 114, 64);
    OG_ASSERT(soldier->check_special_ai(caster));
    caster->foe->setxy(66, 64);
    OG_ASSERT(!soldier->check_special_ai(caster));

    caster->current_special = 1;
    caster->foe = nullptr;
    OG_ASSERT(thief->check_special_ai(caster));

    caster->current_special = 3;
    caster->shifter_down = 1;
    OG_ASSERT(thief->check_special_ai(caster));

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

    for (auto& uptr : fx.level.oblist)
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
    OG_ASSERT(!fx.level.find_foes_in_range(fx.level.oblist, 120, &howmany, a).empty());

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
