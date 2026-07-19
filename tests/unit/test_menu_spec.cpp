/* WP1 shared menu layer (menu-engine design §1.3/§1.9): binding-layer pins.
 *
 * Golden-equivalence oracles for og::ui::menu_item_label against the EXACT
 * strings the (deleted) private text/curses label switches produced, walked
 * across the full value cycles, plus the cancel-item, gate, guard-message,
 * mount-guard, and terminal-model contracts. These are hand-owned literals:
 * a drift in either the binding dispatch or the picker_common formatters
 * fails here, not in a self-referential comparison.
 */
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/interface/ui/terminal_menu_model.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using og::ui::GateBinding;
using og::ui::MenuGate;
using og::ui::MenuLabelContext;
using og::ui::PickerMenuCommand;
using og::ui::PickerMenuId;
using og::ui::PickerMenuItem;
using og::ui::RowState;
using og::ui::TerminalMenuModel;

// A campaign id that is guaranteed never mounted in this binary.
constexpr const char* kUnmountedCampaign = "org.example.never-mounted";

MenuLabelContext context_for(const SaveData& save, int difficulty = 0)
{
    MenuLabelContext context;
    context.save = &save;
    context.session_difficulty = difficulty;
    context.campaign = kUnmountedCampaign;
    context.level = 1;
    return context;
}

const PickerMenuItem* item_of(PickerMenuId menu, PickerMenuCommand command)
{
    return og::ui::find_picker_menu_item(menu, command);
}

// --- menu_item_label golden equivalence (full value cycles) ---------------

TEST(MenuSpec, difficulty_label_full_cycle_with_normalization)
{
    SaveData save;
    const PickerMenuItem* item =
        item_of(PickerMenuId::Difficulty, PickerMenuCommand::SetDifficulty);
    ASSERT_NE(nullptr, item);

    const std::pair<int, const char*> expected[] = {
        {0, "Difficulty: Skirmish"},
        {1, "Difficulty: Battle"},
        {2, "Difficulty: Slaughter"},
        {3, "Difficulty: Skirmish"},   // wraps
        {-1, "Difficulty: Slaughter"}, // negative normalization
    };
    for (const auto& [difficulty, label] : expected) {
        EXPECT_EQ(label,
            og::ui::menu_item_label(*item, context_for(save, difficulty)))
            << "difficulty=" << difficulty;
    }
}

TEST(MenuSpec, allied_mode_label_full_cycle)
{
    SaveData save;
    const PickerMenuItem* item =
        item_of(PickerMenuId::Main, PickerMenuCommand::ToggleAlliedMode);
    ASSERT_NE(nullptr, item);

    save.allied_mode = 0;
    EXPECT_EQ("PVP: Enemy", og::ui::menu_item_label(*item, context_for(save)));
    og::ui::toggle_allied_mode(save);
    EXPECT_EQ("PVP: Ally", og::ui::menu_item_label(*item, context_for(save)));
    og::ui::toggle_allied_mode(save);
    EXPECT_EQ("PVP: Enemy", og::ui::menu_item_label(*item, context_for(save)));
}

TEST(MenuSpec, ctf_setting_labels_full_cycles)
{
    SaveData save;
    const PickerMenuItem* teams =
        item_of(PickerMenuId::TeamBuild, PickerMenuCommand::CycleCtfTeamCount);
    const PickerMenuItem* caps = item_of(
        PickerMenuId::TeamBuild, PickerMenuCommand::CycleCtfCaptureLimit);
    const PickerMenuItem* troops = item_of(
        PickerMenuId::TeamBuild, PickerMenuCommand::ToggleCtfScenarioTroops);
    ASSERT_NE(nullptr, teams);
    ASSERT_NE(nullptr, caps);
    ASSERT_NE(nullptr, troops);

    save.ctf_team_count = 0;
    EXPECT_EQ("Teams: Auto", og::ui::menu_item_label(*teams, context_for(save)));
    const char* team_labels[] = {"Teams: 2", "Teams: 3", "Teams: 4",
                                 "Teams: Auto"};
    for (const char* label : team_labels) {
        og::ui::cycle_ctf_team_count(save);
        EXPECT_EQ(label, og::ui::menu_item_label(*teams, context_for(save)));
    }

    save.ctf_capture_limit = 0;
    EXPECT_EQ("Limit: Map", og::ui::menu_item_label(*caps, context_for(save)));
    const char* cap_labels[] = {"Limit: 1", "Limit: 3", "Limit: 5",
                                "Limit: 10", "Limit: Map"};
    for (const char* label : cap_labels) {
        og::ui::cycle_ctf_capture_limit(save);
        EXPECT_EQ(label, og::ui::menu_item_label(*caps, context_for(save)));
    }

    save.ctf_strip_scenario_troops = 0;
    EXPECT_EQ("Troops: Scen",
              og::ui::menu_item_label(*troops, context_for(save)));
    og::ui::toggle_ctf_scenario_troops(save);
    EXPECT_EQ("Troops: Own",
              og::ui::menu_item_label(*troops, context_for(save)));
    og::ui::toggle_ctf_scenario_troops(save);
    EXPECT_EQ("Troops: Scen",
              og::ui::menu_item_label(*troops, context_for(save)));
}

TEST(MenuSpec, match_rule_labels_full_cycles)
{
    SaveData save;
    const PickerMenuItem* respawns =
        item_of(PickerMenuId::Difficulty, PickerMenuCommand::CycleRespawnMode);
    const PickerMenuItem* delay =
        item_of(PickerMenuId::Difficulty, PickerMenuCommand::CycleRespawnDelay);
    const PickerMenuItem* permadeath =
        item_of(PickerMenuId::Difficulty, PickerMenuCommand::TogglePermadeath);
    const PickerMenuItem* generators =
        item_of(PickerMenuId::Difficulty, PickerMenuCommand::CycleGeneratorRate);
    ASSERT_NE(nullptr, respawns);
    ASSERT_NE(nullptr, delay);
    ASSERT_NE(nullptr, permadeath);
    ASSERT_NE(nullptr, generators);

    EXPECT_EQ("Respawns: Off",
              og::ui::menu_item_label(*respawns, context_for(save)));
    const char* respawn_labels[] = {"Respawns: Heroes", "Respawns: Everyone",
                                    "Respawns: Off"};
    for (const char* label : respawn_labels) {
        og::ui::cycle_respawn_mode(save);
        EXPECT_EQ(label, og::ui::menu_item_label(*respawns, context_for(save)));
    }

    EXPECT_EQ("Spawn Delay: Normal",
              og::ui::menu_item_label(*delay, context_for(save)));
    const char* delay_labels[] = {"Spawn Delay: Fast", "Spawn Delay: Slow",
                                  "Spawn Delay: Normal"};
    for (const char* label : delay_labels) {
        og::ui::cycle_respawn_delay(save);
        EXPECT_EQ(label, og::ui::menu_item_label(*delay, context_for(save)));
    }

    EXPECT_EQ("Permadeath: On",
              og::ui::menu_item_label(*permadeath, context_for(save)));
    og::ui::toggle_permadeath(save);
    EXPECT_EQ("Permadeath: Off",
              og::ui::menu_item_label(*permadeath, context_for(save)));
    og::ui::toggle_permadeath(save);
    EXPECT_EQ("Permadeath: On",
              og::ui::menu_item_label(*permadeath, context_for(save)));

    EXPECT_EQ("Generators: Normal",
              og::ui::menu_item_label(*generators, context_for(save)));
    const char* generator_labels[] = {"Generators: Calm", "Generators: Frenzy",
                                      "Generators: Normal"};
    for (const char* label : generator_labels) {
        og::ui::cycle_generator_rate(save);
        EXPECT_EQ(label,
                  og::ui::menu_item_label(*generators, context_for(save)));
    }
}

TEST(MenuSpec, level_and_campaign_labels_honor_the_mount_guard)
{
    SaveData save;
    const PickerMenuItem* set_level =
        item_of(PickerMenuId::Scenario, PickerMenuCommand::SetLevel);
    const PickerMenuItem* set_campaign =
        item_of(PickerMenuId::Scenario, PickerMenuCommand::SetCampaign);
    ASSERT_NE(nullptr, set_level);
    ASSERT_NE(nullptr, set_campaign);

    // Unmounted session campaign: bare level number, raw-id campaign title
    // fallback (campaign_display_title keeps the raw id for missing
    // packages).
    MenuLabelContext context = context_for(save);
    context.level = 7;
    EXPECT_EQ("Set Level (7)", og::ui::menu_item_label(*set_level, context));
    EXPECT_EQ(std::format("Set Campaign ({})", kUnmountedCampaign),
              og::ui::menu_item_label(*set_campaign, context));

    // The bare helper, both branches. When the context campaign IS the
    // mounted one (whatever this binary has mounted, possibly nothing ==
    // ""), the titled path must be taken.
    EXPECT_EQ("7", og::ui::level_display_guarded(kUnmountedCampaign, 7));
    EXPECT_EQ(og::data::scenario_display_name(5),
              og::ui::level_display_guarded(get_mounted_campaign(), 5));
}

TEST(MenuSpec, fixed_labels_pass_through_and_null_save_falls_back)
{
    SaveData save;
    const PickerMenuItem* view_team =
        item_of(PickerMenuId::TeamBuild, PickerMenuCommand::ViewTeam);
    const PickerMenuItem* go =
        item_of(PickerMenuId::TeamBuild, PickerMenuCommand::StartGame);
    const PickerMenuItem* allied =
        item_of(PickerMenuId::Main, PickerMenuCommand::ToggleAlliedMode);
    ASSERT_NE(nullptr, view_team);
    ASSERT_NE(nullptr, go);
    ASSERT_NE(nullptr, allied);

    EXPECT_EQ("View Team", og::ui::menu_item_label(*view_team, context_for(save)));
    EXPECT_EQ("GO!", og::ui::menu_item_label(*go, context_for(save)));

    // A save-backed binding without a save falls back to the fixed label.
    MenuLabelContext no_save;
    EXPECT_EQ("PVP Mode", og::ui::menu_item_label(*allied, no_save));

    // Spectator context does not alter any current label (documents Layer-E
    // behavior; Layer F adds spectator-aware bindings).
    save.allied_mode = 0;
    MenuLabelContext spectator = context_for(save);
    spectator.spectator = true;
    EXPECT_EQ("View Team", og::ui::menu_item_label(*view_team, spectator));
    EXPECT_EQ("PVP: Enemy", og::ui::menu_item_label(*allied, spectator));
}

// --- cancel semantics -----------------------------------------------------

TEST(MenuSpec, cancel_items_are_quit_on_main_and_back_elsewhere)
{
    // Native build: Main carries Quit (the web variant ships Help instead
    // and legitimately resolves to nullptr there).
    const PickerMenuItem* main_cancel =
        og::ui::menu_cancel_item(PickerMenuId::Main);
    ASSERT_NE(nullptr, main_cancel);
    EXPECT_EQ("quit", main_cancel->id);
    EXPECT_EQ(PickerMenuCommand::Quit, main_cancel->command);
    EXPECT_EQ(og::ui::find_picker_menu_item(PickerMenuId::Main,
                                            PickerMenuCommand::Quit),
              main_cancel);

    for (const PickerMenuId menu : {PickerMenuId::TeamBuild,
                                    PickerMenuId::Scenario,
                                    PickerMenuId::Difficulty}) {
        const PickerMenuItem* cancel = og::ui::menu_cancel_item(menu);
        ASSERT_NE(nullptr, cancel);
        EXPECT_EQ("back", cancel->id);
        EXPECT_EQ(PickerMenuCommand::Back, cancel->command);
        EXPECT_EQ(og::ui::find_picker_menu_item(menu, PickerMenuCommand::Back),
                  cancel);
    }
}

// --- gates ----------------------------------------------------------------

TEST(MenuSpec, gate_state_matrix)
{
    SaveData save;
    MenuLabelContext local = context_for(save);   // is_host=true, local
    MenuLabelContext networked_client = context_for(save);
    networked_client.is_networked = true;
    networked_client.is_host = false;

    EXPECT_EQ(RowState::Visible,
              og::ui::gate_state(GateBinding{MenuGate::Always, nullptr, {}}, local));

    EXPECT_EQ(RowState::Visible,
              og::ui::gate_state(GateBinding{MenuGate::HostOnly, nullptr, {}}, local));
    EXPECT_EQ(RowState::Hidden,
              og::ui::gate_state(GateBinding{MenuGate::HostOnly, nullptr, {}},
                                 networked_client));

    EXPECT_EQ(RowState::Hidden,
              og::ui::gate_state(GateBinding{MenuGate::NetworkedOnly, nullptr, {}}, local));
    EXPECT_EQ(RowState::Visible,
              og::ui::gate_state(GateBinding{MenuGate::NetworkedOnly, nullptr, {}},
                                 networked_client));

    EXPECT_EQ(RowState::Visible,
              og::ui::gate_state(GateBinding{MenuGate::LocalOnly, nullptr, {}}, local));
    EXPECT_EQ(RowState::Hidden,
              og::ui::gate_state(GateBinding{MenuGate::LocalOnly, nullptr, {}},
                                 networked_client));

    // CTF campaign gate: follows the save; a context without a save is
    // gated closed.
    save.current_campaign = "org.openglad.gladiator";
    EXPECT_EQ(RowState::Hidden,
              og::ui::gate_state(GateBinding{MenuGate::CtfCampaignOnly, nullptr, {}},
                                 local));
    save.current_campaign = std::string(og::kCtfCampaignId);
    EXPECT_EQ(RowState::Visible,
              og::ui::gate_state(GateBinding{MenuGate::CtfCampaignOnly, nullptr, {}},
                                 local));
    MenuLabelContext no_save;
    EXPECT_EQ(RowState::Hidden,
              og::ui::gate_state(GateBinding{MenuGate::CtfCampaignOnly, nullptr, {}},
                                 no_save));

    // Custom gate: follows the predicate; a null predicate reads Visible.
    const GateBinding custom_true{MenuGate::Custom,
        [](const MenuLabelContext&) { return true; }, {}};
    const GateBinding custom_false{MenuGate::Custom,
        [](const MenuLabelContext&) { return false; }, {}};
    EXPECT_EQ(RowState::Visible, og::ui::gate_state(custom_true, local));
    EXPECT_EQ(RowState::Hidden, og::ui::gate_state(custom_false, local));
    EXPECT_EQ(RowState::Visible,
              og::ui::gate_state(GateBinding{MenuGate::Custom, nullptr, {}}, local));
}

TEST(MenuSpec, terminal_gate_messages_guard_the_ctf_trio_verbatim)
{
    SaveData save;
    save.current_campaign = "org.openglad.gladiator";

    const PickerMenuCommand ctf_commands[] = {
        PickerMenuCommand::CycleCtfTeamCount,
        PickerMenuCommand::CycleCtfCaptureLimit,
        PickerMenuCommand::ToggleCtfScenarioTroops,
    };
    for (const PickerMenuCommand command : ctf_commands) {
        const PickerMenuItem* item = item_of(PickerMenuId::TeamBuild, command);
        ASSERT_NE(nullptr, item);
        EXPECT_EQ("CTF settings apply to CTF maps only.",
                  og::ui::terminal_gate_message(*item, context_for(save)));
    }

    // On the CTF campaign the gate passes: no guard message.
    save.current_campaign = std::string(og::kCtfCampaignId);
    for (const PickerMenuCommand command : ctf_commands) {
        const PickerMenuItem* item = item_of(PickerMenuId::TeamBuild, command);
        ASSERT_NE(nullptr, item);
        EXPECT_EQ("", og::ui::terminal_gate_message(*item, context_for(save)));
    }

    // Ungated items never produce a message on either campaign.
    const PickerMenuItem* view_team =
        item_of(PickerMenuId::TeamBuild, PickerMenuCommand::ViewTeam);
    ASSERT_NE(nullptr, view_team);
    EXPECT_EQ("", og::ui::terminal_gate_message(*view_team, context_for(save)));
    save.current_campaign = "org.openglad.gladiator";
    EXPECT_EQ("", og::ui::terminal_gate_message(*view_team, context_for(save)));
}

// --- terminal model -------------------------------------------------------

TEST(MenuSpec, terminal_model_preserves_the_full_item_list)
{
    SaveData save;
    for (const PickerMenuId menu_id : {PickerMenuId::Main,
                                       PickerMenuId::TeamBuild,
                                       PickerMenuId::Scenario,
                                       PickerMenuId::Difficulty}) {
        const auto& definition = og::ui::picker_menu_definition(menu_id);
        const TerminalMenuModel model =
            og::ui::build_terminal_menu_model(menu_id, context_for(save));

        EXPECT_EQ(std::string(definition.title), model.title);
        ASSERT_EQ(definition.items.size(), model.entries.size())
            << "the 1-based index contract requires the FULL, unfiltered list";
        for (std::size_t i = 0; i < definition.items.size(); ++i) {
            EXPECT_EQ(&definition.items[i], model.entries[i].item)
                << "entry " << i << " must alias the definition item";
            EXPECT_TRUE(model.entries[i].selectable);
        }
        EXPECT_EQ(og::ui::menu_cancel_item(menu_id), model.cancel_item);
        if (menu_id != PickerMenuId::TeamBuild) {
            EXPECT_TRUE(model.context_lines.empty());
        }
    }
}

TEST(MenuSpec, team_build_context_lines_pin_the_header_strings)
{
    SaveData save;
    save.m_totalcash[0] = 1234;

    // Empty roster.
    TerminalMenuModel model = og::ui::build_terminal_menu_model(
        PickerMenuId::TeamBuild, context_for(save));
    const std::vector<std::string> expected_empty = {"Team: (empty)",
                                                     "Gold: 1234"};
    EXPECT_EQ(expected_empty, model.context_lines);

    // Two members: "name (family)" joined with ", " after the "Team: " tag.
    std::unique_ptr<guy> first =
        og::ui::create_recruit(FAMILY_SOLDIER, 0, save);
    ASSERT_NE(nullptr, first);
    first->name = "ALPHA";
    ASSERT_LE(0, og::ui::add_recruit_to_team(save, std::move(first), 0));
    std::unique_ptr<guy> second = og::ui::create_recruit(FAMILY_MAGE, 0, save);
    ASSERT_NE(nullptr, second);
    second->name = "BETA";
    ASSERT_LE(0, og::ui::add_recruit_to_team(save, std::move(second), 0));

    model = og::ui::build_terminal_menu_model(PickerMenuId::TeamBuild,
                                              context_for(save));
    const std::vector<std::string> expected_roster = {
        std::format("Team: ALPHA ({}), BETA ({})",
                    og::ui::family_display_name(FAMILY_SOLDIER),
                    og::ui::family_display_name(FAMILY_MAGE)),
        "Gold: 1234",
    };
    EXPECT_EQ(expected_roster, model.context_lines);

    // A save-less context produces no context lines.
    MenuLabelContext no_save;
    model = og::ui::build_terminal_menu_model(PickerMenuId::TeamBuild, no_save);
    EXPECT_TRUE(model.context_lines.empty());
}

TEST(MenuSpec, difficulty_menu_model_labels_pin_the_default_screen)
{
    SaveData save;
    const TerminalMenuModel model = og::ui::build_terminal_menu_model(
        PickerMenuId::Difficulty, context_for(save, /*difficulty=*/1));

    std::vector<std::string> labels;
    for (const og::ui::TerminalMenuEntry& entry : model.entries)
        labels.push_back(entry.label);
    const std::vector<std::string> expected = {
        "Difficulty: Battle",
        "Respawns: Off",
        "Spawn Delay: Normal",
        "Permadeath: On",
        "Generators: Normal",
        "Back",
    };
    EXPECT_EQ(expected, labels);
    EXPECT_EQ("Difficulty", model.title);
}

// --- show_submenu (picker_state) ------------------------------------------

// Minimal scripted client: present_menu feeds a fixed item sequence and
// handle_menu_item counts dispatches; verifies the shared nested-loop
// semantics (stop on Back or null, dispatch everything else).
class ScriptedSubmenuClient final : public og::ui::IPickerClient
{
public:
    explicit ScriptedSubmenuClient(std::vector<const PickerMenuItem*> script)
        : script_(std::move(script))
    {
    }

    const PickerMenuItem* present_menu(PickerMenuId menu_id) override
    {
        presented_menus.push_back(menu_id);
        if (next_ >= script_.size())
            return nullptr;
        return script_[next_++];
    }

    void handle_menu_item(PickerMenuId menu_id,
                          const PickerMenuItem& item) override
    {
        handled.emplace_back(menu_id, &item);
    }

    // Unused pure-virtual surface.
    std::string show_campaign_select() override { return {}; }
    void show_options() override {}
    void show_help() override {}
    void run_game() override {}
    bool load_game() override { return false; }
    bool save_game() override { return false; }

    std::vector<PickerMenuId> presented_menus;
    std::vector<std::pair<PickerMenuId, const PickerMenuItem*>> handled;

private:
    std::vector<const PickerMenuItem*> script_;
    std::size_t next_ = 0;
};

TEST(MenuSpec, show_submenu_dispatches_until_back_or_null)
{
    const PickerMenuItem* cycle =
        item_of(PickerMenuId::Difficulty, PickerMenuCommand::CycleRespawnMode);
    const PickerMenuItem* back =
        item_of(PickerMenuId::Difficulty, PickerMenuCommand::Back);
    ASSERT_NE(nullptr, cycle);
    ASSERT_NE(nullptr, back);

    // Two actionable picks, then Back: exactly two dispatches, all on the
    // requested menu id.
    ScriptedSubmenuClient client({cycle, cycle, back});
    client.show_submenu(PickerMenuId::Difficulty);
    ASSERT_EQ(2u, client.handled.size());
    for (const auto& [menu_id, item] : client.handled) {
        EXPECT_EQ(PickerMenuId::Difficulty, menu_id);
        EXPECT_EQ(cycle, item);
    }
    ASSERT_EQ(3u, client.presented_menus.size());
    for (const PickerMenuId menu_id : client.presented_menus)
        EXPECT_EQ(PickerMenuId::Difficulty, menu_id);

    // A null present_menu result (exhausted input) exits without dispatch.
    ScriptedSubmenuClient cancelled({});
    cancelled.show_submenu(PickerMenuId::Scenario);
    EXPECT_TRUE(cancelled.handled.empty());
    ASSERT_EQ(1u, cancelled.presented_menus.size());
    EXPECT_EQ(PickerMenuId::Scenario, cancelled.presented_menus.front());
}

} // namespace
