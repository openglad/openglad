// SDL-side CTF presentation: the score panel's CTF block (captures, carrier
// indicator, respawn countdown), radar blips for flags/control points, the
// results-screen formatting helpers, and the level-editor labels. The CTF
// worlds are built in-test (scripted frame + respawn entries), plus the
// lazy init) — no CTF campaign is loaded.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/view_sizes.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/game_context.h>
#include <openglad/resources/gloader.h>
#include <openglad/interface/button.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/io_common.h>
#include "../src/interface/ui/picker_sdl_defs.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Picker entry points for the injector-driven flows.
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
// TESTING hook (picker_team_build.cpp): render one MATCHUP frame in
// the real draw order so pixel probes can run without the blocking loop.
void picker_test_render_teams_menu_frame();

loader* sdl_entity_loader();
short new_score_panel(screen* s, short do_it);
std::string get_editor_family_label(Order order, Sint32 family, std::span<const std::string> livings,
                                    const char* treasures[], const char* weapons[]);
std::string get_editor_level_label(Order order, Sint32 family, Sint32 level);

namespace {

screen* test_screen()
{
    return og::runtime::current_session->myscreen_;
}

// The CTF HUD probes below capture a fixed 320x200 frame and assert classic
// pixel coordinates.  Keep that historical geometry scoped to those tests;
// production zoom 1.0 and unrelated CTF tests continue using the live canvas.
class ClassicCtfHudCanvasGuard
{
public:
    ClassicCtfHudCanvasGuard()
        : game_(test_screen()), saved_target_(game_->active_canvas())
    {
        game_->set_world_canvas_pinned_classic(true);
        game_->relayout_views();
        game_->set_active_canvas(CanvasTarget::World);
    }

    ~ClassicCtfHudCanvasGuard()
    {
        game_->set_active_canvas(CanvasTarget::UI);
        game_->set_world_canvas_pinned_classic(false);
        game_->relayout_views();
        game_->set_active_canvas(saved_target_);
    }

    ClassicCtfHudCanvasGuard(const ClassicCtfHudCanvasGuard&) = delete;
    ClassicCtfHudCanvasGuard& operator=(const ClassicCtfHudCanvasGuard&) = delete;

private:
    screen* game_;
    CanvasTarget saved_target_;
};

std::unique_ptr<walker> make_control(unsigned char team)
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, test_screen());
    if (!w)
        return nullptr;
    w->set_team_num(team);
    w->set_dead(0);
    w->set_user(0);
    w->setxy(100, 100);
    return w;
}

std::array<unsigned char, 64000> capture_rendered_frame(screen& scr)
{
    std::array<unsigned char, 64000> frame{};
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            int color_index = 0;
            scr.get_pixel(x, y, &color_index);
            frame[static_cast<std::size_t>(y * 320 + x)] =
                static_cast<unsigned char>(color_index);
        }
    }
    return frame;
}

bool box_has_pixels(const std::array<unsigned char, 64000>& frame,
                    int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                return true;
    return false;
}

// Quiet HUD baseline: only the CTF block should paint into the probed boxes.
void silence_hud_prefs(viewscreen* v)
{
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    v->prefs[PREF_FOES] = PREF_FOES_OFF;
}


} // namespace

TEST(CtfUi, classic_respawn_shows_only_the_shared_countdown)
{
    ClassicCtfHudCanvasGuard classic_canvas;
    screen* const s = test_screen();
    if (get_mounted_campaign() != "org.openglad.gladiator") {
        (void)unmount_campaign_package_with_error(get_mounted_campaign());
        (void)mount_campaign_package_with_error("org.openglad.gladiator");
    }
    s->world().id = 1;
    ASSERT_TRUE(s->load_level()) << "level 1 should load";
    GameWorld& world = s->world();

    auto control = make_control(0);
    ASSERT_NE(nullptr, control);
    control->set_dead(1);
    viewscreen* const v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    walker* const old_control = v->control;
    v->control = control.get();
    silence_hud_prefs(v);

    // Leave conspicuous mode values in the shared state with the mode
    // INACTIVE on a classic world. The classic path may render the respawn
    // timer but must not leak the mode scoreboard lines.
    world.type &= static_cast<char>(~GameWorld::TYPE_SCRIPTED);
    world.mode = og::sim::ModeState{};
    std::strncpy(world.mode.hud[0].text.data(), "LEAK 8:7",
                 world.mode.hud[0].text.size() - 1);
    world.respawn_mode = og::sim::kRespawnModeHeroes;

    og::sim::RespawnEntry entry;
    entry.kind = 0;
    entry.team = 0;
    entry.ticks_left = 60;
    entry.walker_entity_id = control->entity_id();
    world.respawn.respawn_queue.clear();
    world.respawn.respawn_queue.push_back(entry);

    const int lm = v->xloc;
    const int tm = v->yloc;
    const int rm = v->endx;
    s->clearbuffer();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(s, 1)));
    const auto frame = capture_rendered_frame(*s);
    EXPECT_TRUE(box_has_pixels(frame,
                               lm + 4, tm + 11, lm + 90, tm + 20))
        << "classic retained control should see RESPAWN IN <s>";
    EXPECT_FALSE(box_has_pixels(frame,
                                rm - 92, tm + 3, rm - 59, tm + 12))
        << "classic mode must not paint mode scoreboard lines";

    world.respawn_mode = 0;
    v->control = old_control;
}

TEST(CtfUi, team_build_row_and_matchup_settings_cycle)
{
    screen* s = test_screen();
    SaveData& save = s->save_data;
    const std::string old_campaign = save.current_campaign;
    const short old_teams = save.ctf_team_count;
    const short old_caps = save.ctf_capture_limit;
    const short old_troops = save.ctf_strip_scenario_troops;

    // The SCENARIO entry never depends on the campaign.
    for (const char* campaign :
         {"org.openglad.gladiator", "org.openglad.modes"})
    {
        save.current_campaign = campaign;
        button* row = picker_createmenu_buttons();
        ASSERT_EQ(kCreateMenuButtonCount, picker_createmenu_button_count());
        EXPECT_EQ("scenario", row[kCreateMenuScenarioIndex].id) << campaign;
        EXPECT_FALSE(row[kCreateMenuScenarioIndex].hidden) << campaign;
        // MATCHUP and VIEW LEVEL live in the SCENARIO subscreen.
        button* scenario = picker_scenariomenu_buttons();
        ASSERT_EQ(kScenarioMenuButtonCount,
                  picker_scenariomenu_button_count());
        EXPECT_EQ("matchup", scenario[kScenarioMenuTeamsIndex].id)
            << campaign;
        EXPECT_EQ("MATCHUP", scenario[kScenarioMenuTeamsIndex].label)
            << campaign;
        EXPECT_EQ("view_scenario",
                  scenario[kScenarioMenuViewScenarioIndex].id) << campaign;
        EXPECT_FALSE(scenario[kScenarioMenuTeamsIndex].hidden) << campaign;
        EXPECT_FALSE(scenario[kScenarioMenuViewScenarioIndex].hidden)
            << campaign;
    }

    // Match Teams and Score Limit live in MATCHUP; their handlers cycle the
    // save fields and refresh the subscreen's descriptor labels. The
    // scenario-troops control moved to SCENARIO, so its MATCHUP row is
    // dormant: the ordinal still resolves, but the row never shows and
    // change_ctf_troops writes the SCENARIO descriptor instead.
    save.current_campaign = "org.openglad.modes";
    save.ctf_team_count = 2;
    save.ctf_capture_limit = 0;
    save.ctf_strip_scenario_troops = 0;
    button* teams_menu = picker_teamsmenu_buttons();
    ASSERT_EQ(kTeamsMenuButtonCount, picker_teamsmenu_button_count());
    EXPECT_EQ("ctf_teams", teams_menu[kTeamsMenuCtfTeamsIndex].id);
    EXPECT_EQ("ctf_caps", teams_menu[kTeamsMenuCtfCapsIndex].id);
    EXPECT_EQ("ctf_troops", teams_menu[kTeamsMenuCtfTroopsIndex].id);

    button* scenario_rows = picker_scenariomenu_buttons();
    EXPECT_EQ("troops", scenario_rows[kScenarioMenuTroopsIndex].id);

    (void)change_ctf_teams();
    EXPECT_EQ(3, (int)save.ctf_team_count);
    (void)change_ctf_caps();
    EXPECT_EQ(1, (int)save.ctf_capture_limit);
    (void)change_ctf_troops();
    EXPECT_EQ(2, (int)save.ctf_strip_scenario_troops);

    const auto& live_rows = og::runtime::current_session->picker_->teamsmenu_buttons;
    ASSERT_EQ(static_cast<std::size_t>(kTeamsMenuButtonCount), live_rows.size());
    EXPECT_EQ("Teams: 3", live_rows[kTeamsMenuCtfTeamsIndex].label);
    EXPECT_EQ("Limit: 1", live_rows[kTeamsMenuCtfCapsIndex].label);

    const auto& live_scenario =
        og::runtime::current_session->picker_->scenariomenu_buttons;
    ASSERT_EQ(static_cast<std::size_t>(kScenarioMenuButtonCount),
              live_scenario.size());
    EXPECT_EQ("TROOPS: OWN", live_scenario[kScenarioMenuTroopsIndex].label);

    (void)change_ctf_troops();
    EXPECT_EQ(0, (int)save.ctf_strip_scenario_troops);
    EXPECT_EQ("TROOPS: ALL", live_scenario[kScenarioMenuTroopsIndex].label);

    // Classic campaign: the same two states, no campaign gate.
    save.current_campaign = "org.openglad.gladiator";
    (void)change_ctf_troops();
    EXPECT_EQ(2, (int)save.ctf_strip_scenario_troops);
    EXPECT_EQ("TROOPS: OWN", live_scenario[kScenarioMenuTroopsIndex].label);
    (void)change_ctf_troops();
    EXPECT_EQ(0, (int)save.ctf_strip_scenario_troops);

    // A save carrying the retired middle state shows OWN and cycles to ALL.
    save.ctf_strip_scenario_troops = 1;
    EXPECT_EQ("TROOPS: OWN", og::ui::format_ctf_troops_label(save));
    (void)change_ctf_troops();
    EXPECT_EQ(0, (int)save.ctf_strip_scenario_troops);

    save.current_campaign = old_campaign;
    save.ctf_team_count = old_teams;
    save.ctf_capture_limit = old_caps;
    save.ctf_strip_scenario_troops = old_troops;
}

TEST(CtfUi, results_helpers_format_winner_banner)
{
    // Color names, matching the sim's match-end notification wording.
    ASSERT_EQ("BLUE TEAM WINS!", format_winner_banner(2));
    ASSERT_EQ("RED TEAM WINS!", format_winner_banner(0));
    ASSERT_EQ("YELLOW TEAM WINS!", format_winner_banner(3));

}

// ---------------------------------------------------------------------------
// Injector-driven picker flows for MATCHUP and the VIEW LEVEL
// viewer (test_back_to_mainmenu's continue_game pattern: save0 is written
// first so CONTINUE lands straight in team build without prompts).
// ---------------------------------------------------------------------------

namespace {

PickerState& picker_state()
{
    return *og::runtime::current_session->picker_;
}

void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        picker_state().backdrops[i].reset();
        picker_state().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    picker_state().main_columns_pix.reset();
    picker_state().main_columns_data.free();
    picker_state().main_title_logo_pix.reset();
    picker_state().main_title_logo_data.free();
}

// Wait until the (visible) interactable `id` shows label `want`.
bool wait_for_interactable_label(const std::string& id, const std::string& want,
                                 int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == id && !item.hidden && item.label == want)
                return true;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' label '%s'\n",
            id.c_str(), want.c_str());
    return false;
}

// Wait until a (visible) interactable `id` exists at game coords (x, y) —
// disambiguates the per-screen "back" buttons by their geometry.
bool wait_for_interactable_at(const std::string& id, int x, int y,
                              int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == id && !item.hidden && item.x == x && item.y == y)
                return true;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' at (%d,%d)\n",
            id.c_str(), x, y);
    return false;
}

// Stash/restore the picker save across an injector flow.
struct SavedPickerSave
{
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team_list;
    SaveData snapshot_fields;

    SavedPickerSave()
    {
        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            team_list[i] = std::move(save.team_list[i]);
        snapshot_fields.team_size = save.team_size;
        snapshot_fields.my_team = save.my_team;
        snapshot_fields.numplayers = save.numplayers;
        snapshot_fields.allied_mode = save.allied_mode;
        snapshot_fields.scen_num = save.scen_num;
        snapshot_fields.current_campaign = save.current_campaign;
        snapshot_fields.ctf_team_count = save.ctf_team_count;
        snapshot_fields.ctf_capture_limit = save.ctf_capture_limit;
        snapshot_fields.ctf_respawn_ticks = save.ctf_respawn_ticks;
        snapshot_fields.ctf_strip_scenario_troops =
            save.ctf_strip_scenario_troops;
    }

    ~SavedPickerSave()
    {
        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[i] = std::move(team_list[i]);
        save.team_size = snapshot_fields.team_size;
        save.my_team = snapshot_fields.my_team;
        save.numplayers = snapshot_fields.numplayers;
        save.allied_mode = snapshot_fields.allied_mode;
        save.scen_num = snapshot_fields.scen_num;
        save.current_campaign = snapshot_fields.current_campaign;
        save.ctf_team_count = snapshot_fields.ctf_team_count;
        save.ctf_capture_limit = snapshot_fields.ctf_capture_limit;
        save.ctf_respawn_ticks = snapshot_fields.ctf_respawn_ticks;
        save.ctf_strip_scenario_troops =
            snapshot_fields.ctf_strip_scenario_troops;
    }
};

void write_save0_with_soldiers(const std::string& campaign, short scen_num,
                               const std::vector<std::string>& names)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[i]->name = names[i];
        save.team_list[i]->teamnum = 0;
    }
    save.team_size = static_cast<unsigned char>(names.size());
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;
    save.scen_num = scen_num;
    save.current_campaign = campaign;
    save.current_levels.clear();
    save.current_levels[campaign] = scen_num;
    save.ctf_team_count = 0;
    save.ctf_capture_limit = 0;
    save.ctf_strip_scenario_troops = 0;
    ASSERT_TRUE(save.save("save0"));
}

void write_save0_with_two_soldiers(const std::string& campaign, short scen_num)
{
    write_save0_with_soldiers(campaign, scen_num, {"Alpha", "Beta"});
}

struct TeamsFlowState
{
    bool started = false;
    bool finished = false;
    bool subscreen_opened = false;
    bool ctf_buttons_hidden = false;
    bool retired_controls_hidden = false;
    bool you_on_team_0 = false;
    bool you_on_team_1 = false;
    bool viewer_opened = false;
    bool viewer_back_seen = false;
    // TROOPS lives on the SCENARIO screen; one flag per state it flips to.
    bool troops_row_seen = false;
    bool troops_own_seen = false;
    bool troops_all_seen = false;
};

int teams_local_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TeamsFlowState* state = static_cast<TeamsFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // Base Camp's seat-rail label opens MATCHUP directly.
    SDL_Delay(500);
    wait_for_interactable("seats", 10000);
    SDL_Delay(750);
    interact("seats");

    // Classic MATCHUP is overview-only: no JOIN, hero cycler, duplicate
    // READY, or CTF settings.
    state->subscreen_opened =
        wait_for_interactable_at("back", 10, 170, 10000);
    SDL_Delay(300);
    state->ctf_buttons_hidden = !has_interactable("ctf_teams") &&
        !has_interactable("ctf_caps") && !has_interactable("ctf_troops");
    state->retired_controls_hidden =
        !has_interactable("join_team_0") &&
        !has_interactable("guy_prev") &&
        !has_interactable("guy_next") &&
        !has_interactable("guy_team") &&
        !has_interactable("ready");

    // MATCHUP back returns directly to Base Camp.
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("scenario", 10000);
    interact("scenario");
    SDL_Delay(300);
    wait_for_interactable("view_scenario", 10000);
    SDL_Delay(300);

    // VIEW LEVEL: framed report over a scratch load; BACK returns.
    interact("view_scenario");
    state->viewer_opened = wait_for_interactable_at("back", 10, 170, 10000);
    SDL_Delay(300);
    interact("back");

    // Viewer back -> SCENARIO submenu; its back (30,170) -> team build.
    SDL_Delay(300);
    wait_for_interactable("matchup", 10000);
    SDL_Delay(300);
    interact("back");

    SDL_Delay(300);
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

int teams_ctf_settings_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TeamsFlowState* state = static_cast<TeamsFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // Team build -> SCENARIO submenu -> MATCHUP.
    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);
    interact("scenario");
    wait_for_interactable("matchup", 10000);
    SDL_Delay(300);
    interact("matchup");

    // CTF campaign + local host: the match-settings trio lives here.
    state->subscreen_opened = wait_for_interactable("ctf_teams", 10000);
    SDL_Delay(300);

    // Each label can flip while the previous click's press is still held;
    // settle after every wait so the next down-transition isn't swallowed.
    interact("ctf_teams");
    state->you_on_team_0 =
        wait_for_interactable_label("ctf_teams", "Teams: 2", 5000);
    SDL_Delay(300);
    interact("ctf_caps");
    state->you_on_team_1 =
        wait_for_interactable_label("ctf_caps", "Limit: 1", 5000);
    SDL_Delay(300);

    // MATCHUP back returns to the SCENARIO submenu.
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("view_scenario", 10000);
    SDL_Delay(300);

    // TROOPS is a SCENARIO row now, host-gated like SET CAMPAIGN. The cycle
    // flips ALL <-> OWN on every campaign.
    state->troops_row_seen =
        wait_for_interactable_label("troops", "TROOPS: ALL", 5000);
    SDL_Delay(300);
    interact("troops");
    state->troops_own_seen =
        wait_for_interactable_label("troops", "TROOPS: OWN", 5000);
    SDL_Delay(300);
    interact("troops");
    state->troops_all_seen =
        wait_for_interactable_label("troops", "TROOPS: ALL", 5000);
    SDL_Delay(300);

    // VIEW LEVEL on the loaded CTF map (the save0 load mounted the CTF
    // campaign): the framed CTF report renders, BACK returns.
    interact("view_scenario");
    state->viewer_back_seen = wait_for_interactable_at("back", 10, 170, 10000);
    SDL_Delay(300);
    interact("back");

    // Viewer back -> SCENARIO submenu; its back (30,170) -> team build.
    SDL_Delay(300);
    wait_for_interactable("matchup", 10000);
    SDL_Delay(300);
    interact("back");

    SDL_Delay(300);
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

// Count picker traces whose message carries the given substring (trace_count
// is per-category only; the page-flip assertions need the page number).
int count_picker_trace_containing(const char* substring)
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int count = 0;
    for (const TraceEntry& entry : g_trace_buffer)
    {
        if (entry.category == "picker" &&
            entry.message.find(substring) != std::string::npos)
        {
            ++count;
        }
    }
    return count;
}

struct PagerFlowState
{
    bool started = false;
    bool finished = false;
    bool viewer_opened = false;
    bool pager_visible = false;
};

// handle_menu_nav reads SDL_GetKeyboardState (not the event queue), so the
// injector pokes the state array directly — the same approach as
// KeyStateGuard in test_picker_menu_nav.cpp.
void hold_nav_key(SDL_Keycode key, bool down)
{
    int numkeys = 0;
    const bool* ro = SDL_GetKeyboardState(&numkeys);
    bool* keys = const_cast<bool*>(ro);
    const SDL_Scancode sc = SDL_GetScancodeFromKey(key, nullptr);
    if (keys != nullptr && sc >= 0 && sc < numkeys)
        keys[sc] = down;
}

void press_nav_key(SDL_Keycode key, int hold_ms)
{
    hold_nav_key(key, true);
    SDL_Delay(hold_ms);
    hold_nav_key(key, false);
}

int view_scenario_pager_injector(void* data)
{
    og::runtime::ensure_thread_session();
    PagerFlowState* state = static_cast<PagerFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // Team build -> SCENARIO submenu -> VIEW LEVEL.
    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);
    interact("scenario");
    wait_for_interactable("view_scenario", 10000);
    SDL_Delay(300);
    interact("view_scenario");

    state->viewer_opened = wait_for_interactable_at("back", 10, 170, 10000);
    SDL_Delay(300);
    state->pager_visible =
        has_interactable("page_prev") && has_interactable("page_next");

    // Mouse path: click NEXT (page 0 -> 1).
    interact("page_next");
    SDL_Delay(500);

    // Keyboard path: RIGHT moves the highlight BACK -> PREV (and enables
    // menu nav); FIRE activates PREV through its ButtonAction (page 1 -> 0).
    press_nav_key(SDLK_RIGHT, 120);
    SDL_Delay(300);
    press_nav_key(SDLK_SPACE, 120);
    SDL_Delay(500);

    // Viewer back -> SCENARIO submenu; its back (30,170) -> team build.
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("matchup", 10000);
    SDL_Delay(300);
    interact("back");

    SDL_Delay(300);
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

// Wait until at least `count` picker traces carry the given substring.
bool wait_for_picker_trace(const char* substring, int count, int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (count_picker_trace_containing(substring) >= count)
            return true;
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for %d picker trace(s) '%s'\n",
            count, substring);
    return false;
}

struct TeamsPagerFlowState
{
    bool started = false;
    bool finished = false;
    bool subscreen_opened = false;
    bool pager_team0_visible = false;
    bool pager_team1_hidden = false;
    bool page2_seen = false;
    bool page3_seen = false;
};

// MATCHUP per-team pager: an 8-hero RED team overflows the detail line, so
// team_page_0 shows and cycles the slice; empty GREEN has no pager.
int teams_pager_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TeamsPagerFlowState* state = static_cast<TeamsPagerFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);
    interact("scenario");
    wait_for_interactable("matchup", 10000);
    SDL_Delay(300);
    interact("matchup");

    state->subscreen_opened =
        wait_for_interactable_at("back", 10, 170, 10000);
    SDL_Delay(300);
    state->pager_team0_visible = wait_for_interactable("team_page_0", 5000);
    state->pager_team1_hidden = !has_interactable("team_page_1");
    SDL_Delay(300);

    // Flip twice: 1/3 -> 2/3 -> 3/3. Each flip is confirmed through the
    // page trace before the next click (the label itself never changes).
    interact("team_page_0");
    state->page2_seen =
        wait_for_picker_trace("teams_detail t=0 page=2/", 1, 5000);
    SDL_Delay(300);
    interact("team_page_0");
    state->page3_seen =
        wait_for_picker_trace("teams_detail t=0 page=3/", 1, 5000);
    SDL_Delay(300);

    // Unwind: MATCHUP -> SCENARIO -> team build -> main menu.
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("view_scenario", 10000);
    SDL_Delay(300);
    interact("back");
    SDL_Delay(300);
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

} // namespace

TEST(CtfUi, matchup_local_overview_and_viewer_flow)
{
    trace_clear();
    SavedPickerSave save_guard;
    write_save0_with_two_soldiers("org.openglad.gladiator", 1);

    TeamsFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        teams_local_flow_injector, "teams_local_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.subscreen_opened) << "MATCHUP should open from SEATS";
    EXPECT_TRUE(state.ctf_buttons_hidden)
        << "classic campaigns hide the CTF settings trio";
    EXPECT_TRUE(state.retired_controls_hidden)
        << "MATCHUP must not expose JOIN, hero cycling, or duplicate READY";
    EXPECT_TRUE(state.viewer_opened) << "VIEW LEVEL should open its frame";

    EXPECT_EQ(0, save.my_team)
        << "the overview must not mutate the player's assigned team";
    const guy* alpha = nullptr;
    for (const auto& member : save.team_list)
    {
        if (member && member->name == "Alpha")
            alpha = member.get();
    }
    ASSERT_NE(nullptr, alpha);
    EXPECT_EQ(0, alpha->teamnum)
        << "the overview must not mutate roster allegiance";

    EXPECT_FALSE(trace_contains("popup", "NO HEROES"))
        << "retired JOIN actions must not run";
    EXPECT_TRUE(trace_contains("picker", "view_scenario lines="))
        << "the viewer should trace its report";
}

TEST(CtfUi, matchup_ctf_settings_flow)
{
    trace_clear();
    SavedPickerSave save_guard;
    // The save0 load mounts the CTF campaign; its levels start at scen 500.
    write_save0_with_two_soldiers("org.openglad.modes", 500);

    TeamsFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        teams_ctf_settings_flow_injector, "teams_ctf_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.subscreen_opened)
        << "CTF campaign + host shows the settings in the subscreen";
    EXPECT_TRUE(state.you_on_team_0) << "Teams cycle should relabel";
    EXPECT_TRUE(state.you_on_team_1) << "Limit cycle should relabel";
    EXPECT_TRUE(state.troops_row_seen)
        << "TROOPS should be visible to the host on SCENARIO";
    EXPECT_TRUE(state.troops_own_seen) << "Troops cycle should relabel to OWN";
    EXPECT_TRUE(state.troops_all_seen)
        << "Troops cycle should flip back to ALL";

    EXPECT_EQ(2, (int)save.ctf_team_count);
    EXPECT_EQ(1, (int)save.ctf_capture_limit);
    EXPECT_EQ(0, (int)save.ctf_strip_scenario_troops);

    EXPECT_TRUE(state.viewer_back_seen)
        << "VIEW LEVEL should open on the mounted CTF map";
    EXPECT_TRUE(trace_contains("picker", "view_scenario lines="))
        << "the viewer should trace its CTF report";

    // The save0 load remounted the CTF campaign; restore the default mount
    // so later (or shuffled) tests load classic levels again.
    (void)unmount_campaign_package_with_error(get_mounted_campaign());
    (void)mount_campaign_package_with_error("org.openglad.gladiator");
}

TEST(CtfUi, view_scenario_pager_flips_by_mouse_and_keyboard)
{
    trace_clear();
    SavedPickerSave save_guard;
    // Gladiator scen 15 (BATTLE OF THE SLIME) formats to well over one page
    // at 23 rows/page: a stable shipped multi-page fixture.
    write_save0_with_two_soldiers("org.openglad.gladiator", 15);

    // Deterministic keyboard nav: bind player-0 menu keys explicitly and
    // keep any configured joystick from shadowing them.
    const int old_up = og::runtime::current_session->player_keys_[0][KEY_UP];
    const int old_down = og::runtime::current_session->player_keys_[0][KEY_DOWN];
    const int old_left = og::runtime::current_session->player_keys_[0][KEY_LEFT];
    const int old_right = og::runtime::current_session->player_keys_[0][KEY_RIGHT];
    const int old_fire = og::runtime::current_session->player_keys_[0][KEY_FIRE];
    const int old_joy_index = player_joy[0].index;
    set_player_key_binding(0, KEY_UP, SDLK_UP);
    set_player_key_binding(0, KEY_DOWN, SDLK_DOWN);
    set_player_key_binding(0, KEY_LEFT, SDLK_LEFT);
    set_player_key_binding(0, KEY_RIGHT, SDLK_RIGHT);
    set_player_key_binding(0, KEY_FIRE, SDLK_SPACE);
    disablePlayerJoystick(0);

    PagerFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        view_scenario_pager_injector, "view_pager_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    set_player_key_binding(0, KEY_UP, old_up);
    set_player_key_binding(0, KEY_DOWN, old_down);
    set_player_key_binding(0, KEY_LEFT, old_left);
    set_player_key_binding(0, KEY_RIGHT, old_right);
    set_player_key_binding(0, KEY_FIRE, old_fire);
    player_joy[0].index = old_joy_index;

    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.viewer_opened) << "VIEW LEVEL should open its frame";
    EXPECT_TRUE(state.pager_visible)
        << "scen 15's report must span multiple pages (PREV/NEXT visible)";
    EXPECT_GE(count_picker_trace_containing("page=1"), 1)
        << "mouse click on NEXT must flip to page 1";
    EXPECT_GE(count_picker_trace_containing("page=0"), 2)
        << "keyboard FIRE on PREV must flip back (entry trace + flip trace)";
}

// MATCHUP per-team detail paging: a seat and eight heroes overflow the
// 39-character paged detail budget, so the row gets a '>' pager plus p/N,
// and the slices rotate through every member. An empty team has no pager.
TEST(CtfUi, matchup_member_pager_cycles_slices)
{
    trace_clear();
    SavedPickerSave save_guard;
    // The seat identity and HEROES prefix consume part of page one; the
    // widened row then packs the remaining names into three visible slices.
    write_save0_with_soldiers(
        "org.openglad.gladiator", 1,
        {"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf",
         "Hotel"});

    TeamsPagerFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        teams_pager_flow_injector, "teams_pager_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.subscreen_opened) << "MATCHUP should open";
    EXPECT_TRUE(state.pager_team0_visible)
        << "the overflowing RED team must show its '>' pager";
    EXPECT_TRUE(state.pager_team1_hidden)
        << "the empty GREEN team must not show a pager";
    EXPECT_TRUE(state.page2_seen) << "first flip should reach page 2/3";
    EXPECT_TRUE(state.page3_seen) << "second flip should reach page 3/3";

    // The traces carry the rendered indicator (p/N) and the slice text:
    // each page shows a different run of member names.
    EXPECT_GE(count_picker_trace_containing(
                  "teams_detail t=0 page=1/3 SEATS: P1 YOU, HEROES: Alpha, Bravo"),
              1)
        << "entry slice should identify the seat and first heroes";
    EXPECT_GE(count_picker_trace_containing(
                  "teams_detail t=0 page=2/3 Charlie, Delta, Echo, Foxtrot, Golf"),
              1)
        << "page 2 should rotate to the middle members";
    EXPECT_GE(count_picker_trace_containing(
                  "teams_detail t=0 page=3/3 Hotel"), 1)
        << "page 3 should show the overflow tail";
}

// Draw-order regression: the per-team '>' pager is the only MATCHUP button
// whose face sits inside a row readability bar (8..312 x row band). The bars
// must render BENEATH the buttons — when they were painted after
// draw_buttons, every visible pager was dimmed to ~41% brightness (PURE_BLACK
// alpha 150) unlike every other button. Renders one real frame via the
// TESTING hook and compares the pager's face pixels against the same
// BUTTON_FACING face style on buttons outside the bars.
TEST(CtfUi, matchup_pager_face_not_dimmed_by_row_bar)
{
    SavedPickerSave save_guard;
    // Same 8-name roster as the pager flow: team 0's detail line overflows,
    // so team_page_0 is visible.
    write_save0_with_soldiers(
        "org.openglad.gladiator", 1,
        {"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf",
         "Hotel"});

    picker_test_render_teams_menu_frame();

    screen* s = test_screen();
    // Face-interior probes: inside each button, clear of the 1px borders and
    // of the centered 1-char '>' label (glyph starts at x=(x+xend)/2).
    // team_page_0: (297,39,14x12), with the glyph centered near x=304.
    int pager_face = -1;
    s->get_pixel(299, 44, &pager_face);
    // BACK is a visible button outside every row bar and uses the same
    // BUTTON_FACING face style.
    int reference_face = -1;
    s->get_pixel(13, 174, &reference_face);

    EXPECT_EQ(reference_face, pager_face)
        << "pager face inside the row bar must match the undimmed button "
           "face style (bar painted over the button?)";

    // And the bar itself still dims the backdrop around the button: a bar
    // pixel just below the pager (row band is y 30..51; the pager ends at
    // y=50) must not be the button-face color.
    int bar_pixel = -1;
    s->get_pixel(299, 51, &bar_pixel);
    EXPECT_NE(bar_pixel, pager_face)
        << "row readability bar should still darken non-button pixels";

    cleanup_picker_state();
}
