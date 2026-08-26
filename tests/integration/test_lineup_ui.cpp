// LINEUP screen flows (docs/lineup-design.md §2): the SCENARIO door, the
// four team bands with their per-team bot knobs, the SPLIT actions, and the
// FIGHTERS list. Injector-driven through the real picker (interact by
// button id, never coordinates), with UxShots-pattern frame captures that
// double as visual smoke tests when UXSHOTS_DIR is unset.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Picker entry points for the injector-driven flows.
void picker_main(Sint32 argc, char** argv);
Sint32 create_team_menu(Sint32 arg1);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
extern std::atomic_bool g_test_present_pause_requested;
extern std::atomic_bool g_test_present_paused;

namespace {

PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[static_cast<std::size_t>(i)].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

// --- UxShots capture machinery (test_uxshots_probe.cpp pattern) ------------

constexpr Uint64 kFramePauseTimeoutMs = 30000;

class PresentedFramePause {
public:
    PresentedFramePause()
    {
        bool expected = false;
        if (!g_test_present_pause_requested.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel))
        {
            fprintf(stderr,
                    "  [lineup] FAILED: another frame capture pending\n");
            fflush(stderr);
            abort();
        }
        const Uint64 deadline = SDL_GetTicks() + kFramePauseTimeoutMs;
        while (!g_test_present_paused.load(std::memory_order_acquire)) {
            if (SDL_GetTicks() >= deadline) {
                fprintf(stderr,
                        "  [lineup] FAILED: no presented frame in time\n");
                fflush(stderr);
                abort();
            }
            SDL_Delay(1);
        }
        acquired_ = true;
    }

    ~PresentedFramePause()
    {
        if (!acquired_)
            return;
        g_test_present_pause_requested.store(false,
                                             std::memory_order_release);
        const Uint64 deadline = SDL_GetTicks() + kFramePauseTimeoutMs;
        while (g_test_present_paused.load(std::memory_order_acquire)) {
            if (SDL_GetTicks() >= deadline) {
                fprintf(stderr, "  [lineup] presenter did not resume\n");
                fflush(stderr);
                abort();
            }
            SDL_Delay(1);
        }
    }

    PresentedFramePause(const PresentedFramePause&) = delete;
    PresentedFramePause& operator=(const PresentedFramePause&) = delete;

    [[nodiscard]] bool acquired() const { return acquired_; }

private:
    bool acquired_ = false;
};

// Verify the current canvas and optionally dump it as a binary PPM (runs on
// the injector thread; the presenter handshake freezes one complete frame).
bool capture_frame(const char* name)
{
    screen* scr = og::runtime::current_session->myscreen_;
    const char* output_dir = std::getenv("UXSHOTS_DIR");
    std::string path;
    std::vector<Uint8> rgb;
    if (output_dir != nullptr && output_dir[0] != '\0') {
        std::error_code error;
        std::filesystem::create_directories(output_dir, error);
        if (error) {
            fprintf(stderr, "  [lineup] FAILED to create %s: %s\n",
                    output_dir, error.message().c_str());
            return false;
        }
        path = std::string(output_dir) + "/" + name + ".ppm";
        rgb.reserve(320 * 200 * 3);
    }

    std::size_t nonblack_pixels = 0;
    {
        PresentedFramePause frame_pause;
        if (!frame_pause.acquired())
            return false;
        for (int y = 0; y < 200; ++y) {
            for (int x = 0; x < 320; ++x) {
                Uint8 r = 0, g = 0, b = 0;
                scr->get_pixel(x, y, &r, &g, &b);
                if (r != 0 || g != 0 || b != 0)
                    ++nonblack_pixels;
                if (!path.empty()) {
                    rgb.push_back(r);
                    rgb.push_back(g);
                    rgb.push_back(b);
                }
            }
        }
    }

    if (!path.empty()) {
        FILE* f = fopen(path.c_str(), "wb");
        if (f == nullptr) {
            fprintf(stderr, "  [lineup] FAILED to open %s\n", path.c_str());
            return false;
        }
        fprintf(f, "P6\n320 200\n255\n");
        fwrite(rgb.data(), sizeof(Uint8), rgb.size(), f);
        fclose(f);
        fprintf(stderr, "  [lineup] wrote %s\n", path.c_str());
    }
    fprintf(stderr, "  [lineup] %s: %zu nonblack pixels\n", name,
            nonblack_pixels);
    return nonblack_pixels >= 1000;
}

// --- interact helpers (test_ctf_ui.cpp idioms) ------------------------------

bool wait_for_interactable_label(const std::string& id,
                                 const std::string& want, int timeout_ms)
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
    fprintf(stderr, "  [lineup] TIMEOUT waiting for '%s' label '%s'\n",
            id.c_str(), want.c_str());
    return false;
}

// Click `id` until its label reads `want` (bounded retries: a press and
// release landing in one stretched frame are swallowed whole under load).
bool click_until_label(const std::string& id, const std::string& want,
                       int attempts = 3, int wait_ms = 2500)
{
    for (int i = 0; i < attempts; ++i) {
        interact(id);
        if (wait_for_interactable_label(id, want, wait_ms))
            return true;
        fprintf(stderr, "  [lineup] retry %d: '%s' not yet '%s'\n", i + 1,
                id.c_str(), want.c_str());
    }
    return false;
}

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
    fprintf(stderr, "  [lineup] TIMEOUT waiting for '%s' at (%d,%d)\n",
            id.c_str(), x, y);
    return false;
}

bool interactable_visible(const std::string& id)
{
    for (const Interactable& item : get_interactables()) {
        if (item.id == id && !item.hidden)
            return true;
    }
    return false;
}

bool wait_for_team_menu(int timeout_ms = 20000)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (has_interactable("hire_troops") && has_interactable("networking"))
            return true;
        SDL_Delay(50);
        elapsed += 50;
    }
    return false;
}

// --- save fixtures ----------------------------------------------------------

// Stash/restore the picker save across an injector flow (the test_ctf_ui
// guard, extended with the LINEUP bot knobs).
struct SavedPickerSave
{
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team_list;
    SaveData snapshot_fields;

    SavedPickerSave()
    {
        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            team_list[static_cast<std::size_t>(i)] =
                std::move(save.team_list[static_cast<std::size_t>(i)]);
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
        snapshot_fields.bot_squad = save.bot_squad;
        snapshot_fields.bot_level = save.bot_level;
    }

    ~SavedPickerSave()
    {
        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[static_cast<std::size_t>(i)] =
                std::move(team_list[static_cast<std::size_t>(i)]);
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
        save.bot_squad = snapshot_fields.bot_squad;
        save.bot_level = snapshot_fields.bot_level;
    }
};

struct FighterSeed
{
    const char* name;
    short level;
    bool deployed;
    short team;
};

void write_save0_with_fighters(const std::string& campaign, short scen_num,
                               int numplayers,
                               const std::vector<FighterSeed>& roster)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    for (std::size_t i = 0; i < roster.size(); ++i)
    {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = roster[i].name;
        member->upgrade_to_level(roster[i].level, true);
        member->deployed = roster[i].deployed;
        member->teamnum = roster[i].team;
        save.team_list[i] = std::move(member);
    }
    save.team_size = static_cast<unsigned char>(roster.size());
    save.my_team = 0;
    save.numplayers = static_cast<unsigned char>(numplayers);
    save.allied_mode = 0;
    save.scen_num = scen_num;
    save.current_campaign = campaign;
    save.current_levels.clear();
    save.current_levels[campaign] = scen_num;
    save.ctf_team_count = 0;
    save.ctf_capture_limit = 0;
    save.ctf_strip_scenario_troops = 0;
    save.bot_squad = {};
    save.bot_level = {};
    ASSERT_TRUE(save.save("save0"));
}

void restore_gladiator_mount()
{
    (void)unmount_campaign_package_with_error(get_mounted_campaign());
    (void)mount_campaign_package_with_error("gladiator");
}

// Base Camp -> SCENARIO -> LINEUP, shared by the flows below. Returns false
// (and abandons the flow) on any missed step.
bool injector_open_lineup()
{
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");
    SDL_Delay(500);
    if (!wait_for_interactable("scenario", 15000))
        return false;
    SDL_Delay(750);
    interact("scenario");
    if (!wait_for_interactable("lineup", 10000))
        return false;
    SDL_Delay(300);
    return true;
}

// SCENARIO -> Base Camp -> main menu -> quit (every flow's unwind).
void injector_unwind_from_scenario()
{
    if (wait_for_interactable_at("back", 30, 170, 5000)) {
        SDL_Delay(300);
        interact("back");
    }
    if (wait_for_team_menu(10000)) {
        SDL_Delay(300);
        interact("back");
    }
    if (wait_for_interactable("begin_new_game", 10000)) {
        SDL_Delay(750);
        interact("quit");
    }
}

// --- fake networked lobby (test_uxshots_probe.cpp pattern) ------------------

class FakeNetLobbyClient final : public og::ui::IPickerLobbyClient {
public:
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        return std::nullopt;
    }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return host_view;
    }
    [[nodiscard]] bool is_save_slot_editable(
        std::size_t) const noexcept override
    {
        return slots_editable;
    }
    [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players()
        const override
    {
        return players;
    }
    [[nodiscard]] std::vector<std::uint8_t> local_player_indices()
        const override
    {
        return local_indices;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }
    // §6: "am I actually in a session", carried directly so a test can pose a
    // pending or a dead join without a transport.
    [[nodiscard]] bool session_established() const noexcept override
    {
        return established;
    }
    [[nodiscard]] std::string session_room_code() const override
    {
        return room_code;
    }

    bool host_view = true;
    bool established = true;
    bool slots_editable = true;
    std::vector<og::sim::LobbyPlayer> players;
    std::vector<std::uint8_t> local_indices;
    std::string room_code = "GLAD-7Q2F";
};

struct ActiveLobbyGuard
{
    og::ui::IPickerLobbyClient* saved;
    explicit ActiveLobbyGuard(og::ui::IPickerLobbyClient* client)
        : saved(og::ui::active_picker_lobby_client())
    {
        og::ui::install_active_picker_lobby_client(client);
    }
    ~ActiveLobbyGuard()
    {
        og::ui::install_active_picker_lobby_client(saved);
    }
};

og::sim::LobbyPlayer make_probe_seat(std::uint8_t index, const char* name,
                                     const char* company, bool is_host,
                                     short team,
                                     const std::vector<FighterSeed>& roster)
{
    og::sim::LobbyPlayer player;
    player.player_index = index;
    player.seat_id = static_cast<og::sim::LobbySeatId>(index) + 1;
    player.machine_id = static_cast<og::sim::LobbyMachineId>(index + 1);
    player.name = name;
    player.company = company;
    player.team = team;
    player.is_host = is_host;
    player.ready = false;
    std::uint8_t i = 0;
    for (const FighterSeed& seed : roster) {
        og::sim::LobbyCharacterSlot slot;
        slot.slot_index = i++;
        slot.deployed = seed.deployed;
        slot.character.name = seed.name;
        slot.character.family = static_cast<std::int8_t>(FAMILY_SOLDIER);
        slot.character.level = seed.level;
        slot.character.teamnum = seed.team;
        player.character_slots.push_back(std::move(slot));
    }
    return player;
}

void seed_session_save_for_net(const std::string& campaign, short scen_num)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.save_name = "IRON KETTLE BAND";
    save.current_campaign = campaign;
    save.scen_num = scen_num;
    save.numplayers = 1;
    const std::vector<FighterSeed> own = {
        {"GORT", 3, true, 0},
        {"HALDOR", 2, true, 0},
        {"SYLVA", 4, true, 0},
        {"FLINT", 2, false, 0},
    };
    int i = 0;
    for (const FighterSeed& seed : own) {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = seed.name;
        member->upgrade_to_level(seed.level, true);
        member->deployed = seed.deployed;
        member->teamnum = seed.team;
        save.team_list[static_cast<std::size_t>(i++)] = std::move(member);
    }
    save.team_size = static_cast<unsigned char>(i);
}

// ---------------------------------------------------------------------------
// Flow 1: SCENARIO carries the LINEUP door; the page opens; the host's
// per-team knobs cycle with save-value pins; BOTS: NONE lands on an occupied
// team exactly as it does on an empty one (§2.3, ruling 2026-08-26 — one
// write rule on all three clients, no refusal, no toast).

struct LineupKnobsFlowState
{
    bool finished = false;
    bool door_seen = false;
    bool page_opened = false;
    bool level_cycled = false;
    bool bots_none_on_empty_team = false;
    bool bots_none_on_occupied_team = false;
    int captures = 0;
};

int lineup_knobs_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<LineupKnobsFlowState*>(data);
    if (!injector_open_lineup()) {
        state->finished = true;
        return 0;
    }
    state->door_seen = true;
    SDL_Delay(750);
    state->captures += capture_frame("scenario_with_lineup_door");
    SDL_Delay(200);
    interact("lineup");

    // The page: the action strip's BACK sits at its own (8,176) geometry.
    state->page_opened =
        wait_for_interactable_at("back", 8, 176, 10000);
    if (state->page_opened) {
        SDL_Delay(750);

        // LV knob on band 1: AUTO -> 1.
        state->level_cycled =
            click_until_label("lineup_level_1", "LV 1");
        SDL_Delay(300);
        // BOTS on the unoccupied team 2 may reach NONE (no seat, no
        // fighters there in this fixture).
        state->bots_none_on_empty_team =
            click_until_label("lineup_bots_1", "BOTS: NONE");
        SDL_Delay(300);
        // BOTS on team 1 (the seat + every fighter): NONE is just as legal
        // there — one cycle off AUTO lands on it and it sticks.
        state->bots_none_on_occupied_team =
            click_until_label("lineup_bots_0", "BOTS: NONE");
        SDL_Delay(300);

        interact("back");  // LINEUP -> SCENARIO
    }
    injector_unwind_from_scenario();
    state->finished = true;
    return 0;
}

// ---------------------------------------------------------------------------

} // namespace

TEST(LineupUi, scenario_door_knob_cycles_and_none_everywhere)
{
    trace_clear();
    SavedPickerSave save_guard;
    write_save0_with_fighters("modes", 500, 1,
                              {{"Alpha", 3, true, 0}, {"Beta", 2, true, 0}});

    LineupKnobsFlowState state;
    SDL_Thread* thread = SDL_CreateThread(lineup_knobs_flow_injector,
                                          "lineup_knobs", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.door_seen) << "SCENARIO should carry the LINEUP door";
    EXPECT_TRUE(state.page_opened) << "the LINEUP page should open";
    EXPECT_TRUE(state.level_cycled) << "LV knob should cycle AUTO -> 1";
    EXPECT_TRUE(state.bots_none_on_empty_team)
        << "BOTS on an empty team reaches NONE";
    EXPECT_TRUE(state.bots_none_on_occupied_team)
        << "BOTS on the occupied team reaches NONE too";
    EXPECT_EQ(1, static_cast<int>(save.bot_level[1]))
        << "the LV cycle lands in the save knob";
    EXPECT_EQ(1, static_cast<int>(save.bot_squad[1]))
        << "the empty team's NONE lands in the save knob";
    // Both knob writes survived every later per-frame picker_lobby_poll(),
    // which copies the lobby settings back over the save: the value is only
    // still 1 because change_lineup_bots pushed it into the lobby first.
    EXPECT_EQ(1, static_cast<int>(save.bot_squad[0]))
        << "the occupied team's NONE lands and stays synced";
    EXPECT_FALSE(trace_contains("lineup", "bots_none_refused"))
        << "NONE is never refused";
    EXPECT_FALSE(trace_contains("lineup", "toast TEAM "))
        << "NONE raises no toast";
    EXPECT_EQ(1, state.captures) << "the SCENARIO capture should land";

    restore_gladiator_mount();
}

namespace {

// ---------------------------------------------------------------------------
// Flow 1b (WP-C x WP-E): the knobs reach the campaign's Lua and come back as
// labels. On the modes campaign — the one campaign that registers the
// `lineup` hook — a preset lands on the BOTS cycler by NAME, the TEAM 1 band
// prices the deployed company through the hook's `power`, and VIEW LEVEL's
// staged census reports the squad the preset+level actually spawned. Wave 2
// built the LINEUP screen against a tree where the hook did not exist yet,
// so this is the first end-to-end proof of the whole chain.

struct LineupPresetFlowState
{
    bool finished = false;
    bool page_opened = false;
    bool preset_labelled = false;
    bool level_labelled = false;
    bool hook_registered = false;
    std::optional<long long> team1_power;
    bool viewer_opened = false;
    bool squad_line_seen = false;
    bool level_suffix_seen = false;
    int captures = 0;
};

// Poll a trace category for a substring (the wait_for_picker_trace idiom).
bool wait_for_trace(const char* category, const char* substring,
                    int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 50) {
        if (trace_contains(category, substring))
            return true;
        SDL_Delay(50);
    }
    fprintf(stderr, "  [lineup] TIMEOUT waiting for trace '%s'\n", substring);
    return false;
}

int lineup_preset_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<LineupPresetFlowState*>(data);
    if (!injector_open_lineup()) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(300);
    interact("lineup");
    state->page_opened = wait_for_interactable_at("back", 8, 176, 10000);
    if (!state->page_opened) {
        injector_unwind_from_scenario();
        state->finished = true;
        return 0;
    }
    SDL_Delay(750);

    // TEAM 2's BOTS knob: AUTO -> NONE -> BALANC (ordinal 2 — the first
    // entry of the campaign's own BOT_PRESETS, named by the lineup hook,
    // never by the engine).
    state->preset_labelled =
        click_until_label("lineup_bots_1", "BOTS: BALANC", 5);
    SDL_Delay(300);
    // ...and its level: AUTO -> LV 1 -> LV 2 -> LV 3.
    state->level_labelled = click_until_label("lineup_level_1", "LV 3", 6);
    SDL_Delay(300);

    // The TEAM 1 band's POWER, priced exactly as the screen prices it: the
    // hook is registered, so build_lineup_bands runs with the real
    // lineup_power_for_guy (menu_screen_specs' lineup_active_power_fn).
    (void)run_on_main_thread([state] {
        state->hook_registered =
            og::script::hooks::campaign_lineup_registered();
        const SaveData& save =
            og::runtime::current_session->myscreen_->save_data;
        const LineupSeatView view = picker_lineup_seat_view();
        const std::array<og::ui::LineupTeamBand, 4> bands =
            og::ui::build_lineup_bands(save, view.players,
                                       view.local_indices,
                                       picker_lobby_is_networked(),
                                       &og::ui::lineup_power_for_guy);
        state->team1_power = bands[0].power;
    });

    state->captures += capture_frame("lineup_modes_preset_power");
    SDL_Delay(300);

    interact("back");  // LINEUP -> SCENARIO
    if (!wait_for_interactable("view_scenario", 10000)) {
        injector_unwind_from_scenario();
        state->finished = true;
        return 0;
    }
    SDL_Delay(750);
    interact("view_scenario");
    state->viewer_opened = wait_for_interactable_at("back", 10, 170, 10000);
    if (state->viewer_opened) {
        // The staged census names the preset the spawn seam banked, and the
        // explicit level rides the same line with no inner space (§3.4).
        state->squad_line_seen = wait_for_trace(
            "picker", "view_scenario line   GREEN TEAM  ACTIVE - BOT SQUAD "
                      "BALANC",
            10000);
        state->level_suffix_seen =
            trace_contains("picker", "BOT SQUAD BALANC (5) LV3");
        state->captures += capture_frame("lineup_view_level_preset_squad");
        SDL_Delay(300);
        interact("back");
        SDL_Delay(300);
        (void)wait_for_interactable("progress", 10000);
        SDL_Delay(300);
    }
    injector_unwind_from_scenario();
    state->finished = true;
    return 0;
}

} // namespace

TEST(LineupUi, modes_preset_knobs_reach_the_labels_and_the_staged_world)
{
    trace_clear();
    SavedPickerSave save_guard;
    write_save0_with_fighters("modes", 500, 1,
                              {{"ROWAN", 4, true, 0}, {"MIRA", 3, true, 0}});

    LineupPresetFlowState state;
    SDL_Thread* thread = SDL_CreateThread(lineup_preset_flow_injector,
                                          "lineup_preset", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    const SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.page_opened) << "the LINEUP page should open";
    EXPECT_TRUE(state.hook_registered)
        << "the modes campaign must register the lineup hook";
    EXPECT_TRUE(state.preset_labelled)
        << "the BOTS cycler must reach the campaign preset BALANC";
    EXPECT_TRUE(state.level_labelled) << "the LV cycler must reach LV 3";
    EXPECT_EQ(2, static_cast<int>(save.bot_squad[1]))
        << "BALANC is ordinal 2 in the save knob";
    EXPECT_EQ(3, static_cast<int>(save.bot_level[1]))
        << "the explicit level lands in the save knob";
    ASSERT_TRUE(state.team1_power.has_value())
        << "the registered hook prices the deployed company: POWER n, not --";
    EXPECT_GT(*state.team1_power, 0)
        << "two deployed fighters are worth something";
    EXPECT_TRUE(state.viewer_opened) << "VIEW LEVEL should open its frame";
    EXPECT_TRUE(state.squad_line_seen)
        << "the staged census must name the preset squad on the green team";
    EXPECT_TRUE(state.level_suffix_seen)
        << "the explicit level rides the same line as ' LVk'";
    EXPECT_EQ(2, state.captures) << "both captures should land";

    restore_gladiator_mount();
}

namespace {

// ---------------------------------------------------------------------------
// Flow 2: SPLIT FAIR on a six-fighter, two-seat company — the snake draft
// lands the exact (slot, team) assignment and the derived seat rail follows.

struct LineupSplitFlowState
{
    bool finished = false;
    bool page_opened = false;
    bool splits_visible = false;
    int captures = 0;
};

int lineup_split_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<LineupSplitFlowState*>(data);
    if (!injector_open_lineup()) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(300);
    interact("lineup");
    state->page_opened = wait_for_interactable_at("back", 8, 176, 10000);
    if (state->page_opened) {
        SDL_Delay(750);
        state->splits_visible = interactable_visible("lineup_split_fair");
        state->captures += capture_frame("lineup_solo_two_seats");
        SDL_Delay(300);
        // Bounded-retry click (the click_until_label idiom, keyed on the
        // action's own trace): a press and release landing inside one
        // stretched frame — the capture pause above stretches one — are
        // swallowed whole.
        for (int attempt = 0;
             attempt < 3 && !trace_contains("lineup", "split mode=1");
             ++attempt)
        {
            interact("lineup_split_fair");
            SDL_Delay(750);
        }
        interact("back");
    }
    injector_unwind_from_scenario();
    state->finished = true;
    return 0;
}

} // namespace

TEST(LineupUi, split_fair_two_seats_six_fighters_snake_draft)
{
    trace_clear();
    SavedPickerSave save_guard;
    // Six deployed fighters, levels 5/4/3/2/2/1, all on team 0; two local
    // seats. No campaign power hook is registered in this build, so FAIR
    // drafts by level descending (tie: slot order) — the documented
    // fallback.
    write_save0_with_fighters("modes", 500, 2,
                              {{"AXEL", 5, true, 0},
                               {"BORIS", 4, true, 0},
                               {"CYRA", 3, true, 0},
                               {"DORN", 2, true, 0},
                               {"EDDA", 2, true, 0},
                               {"FEN", 1, true, 0}});

    LineupSplitFlowState state;
    SDL_Thread* thread = SDL_CreateThread(lineup_split_flow_injector,
                                          "lineup_split", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.page_opened);
    EXPECT_TRUE(state.splits_visible)
        << "two local seats show SPLIT EVEN / SPLIT FAIR";
    EXPECT_EQ(1, state.captures);

    // Snake draft over seat teams {0,1}: picks 0,1,1,0,0,1 by level order.
    const std::array<short, 6> expected = {0, 1, 1, 0, 0, 1};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        ASSERT_NE(nullptr, save.team_list[i]) << "slot " << i;
        EXPECT_EQ(expected[i], save.team_list[i]->teamnum)
            << "slot " << i << " (" << save.team_list[i]->name << ")";
    }
    EXPECT_TRUE(trace_contains("lineup", "split mode=1 moved="))
        << "the split must trace its outcome";

    // The seat rail follows: the derived local seat teams now cover both
    // colours, preferred team first (M3).
    const std::vector<short> seat_teams =
        og::ui::derive_local_seat_teams(save);
    ASSERT_EQ(2u, seat_teams.size());
    EXPECT_EQ(0, seat_teams[0]);
    EXPECT_EQ(1, seat_teams[1]);

    restore_gladiator_mount();
}

namespace {

// ---------------------------------------------------------------------------
// Flow 3: the FIGHTERS list — a row click cycles the fighter's team, the
// deploy box benches, and both land in the save through the mutation tail.

struct LineupFightersFlowState
{
    bool finished = false;
    bool list_opened = false;
    int captures = 0;
};

int lineup_fighters_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<LineupFightersFlowState*>(data);
    if (!injector_open_lineup()) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(300);
    interact("lineup");
    if (wait_for_interactable("lineup_fighters", 10000)) {
        SDL_Delay(750);
        interact("lineup_fighters");
        state->list_opened =
            wait_for_interactable_at("back", 10, 169, 10000);
        if (state->list_opened) {
            SDL_Delay(750);
            state->captures += capture_frame("fighters_list");
            SDL_Delay(200);
            interact("fighter_row_0");  // Alpha: team 0 -> 1
            SDL_Delay(750);
            interact("fighter_dep_1");  // Beta: bench
            SDL_Delay(750);
            interact("back");  // FIGHTERS -> LINEUP
        }
        if (wait_for_interactable_at("back", 8, 176, 5000)) {
            SDL_Delay(300);
            interact("back");  // LINEUP -> SCENARIO
        }
    }
    injector_unwind_from_scenario();
    state->finished = true;
    return 0;
}

} // namespace

TEST(LineupUi, fighters_row_cycles_team_and_deploy_toggles)
{
    trace_clear();
    SavedPickerSave save_guard;
    write_save0_with_fighters("modes", 500, 1,
                              {{"Alpha", 3, true, 0}, {"Beta", 2, true, 0}});

    LineupFightersFlowState state;
    SDL_Thread* thread = SDL_CreateThread(lineup_fighters_flow_injector,
                                          "lineup_fighters", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.list_opened) << "FIGHTERS should open from LINEUP";
    EXPECT_EQ(1, state.captures);
    ASSERT_NE(nullptr, save.team_list[0]);
    ASSERT_NE(nullptr, save.team_list[1]);
    EXPECT_EQ(1, save.team_list[0]->teamnum)
        << "the row click cycles Alpha's team through cycle_guy_team";
    EXPECT_FALSE(save.team_list[1]->deployed)
        << "the deploy box benches Beta";
    EXPECT_TRUE(trace_contains("lineup", "fighter_team slot=0 team=1"));
    EXPECT_TRUE(trace_contains("lineup", "deploy slot=1 off"));

    restore_gladiator_mount();
}

namespace {

// ---------------------------------------------------------------------------
// Flows 4a/4b: networked joiner and host over the direct create_team_menu
// fixture (the UxShots FakeNetLobbyClient pattern — no sockets).

struct LineupNetFlowState
{
    bool finished = false;
    bool page_opened = false;
    bool knobs_visible = false;
    int captures = 0;
    const char* capture_name = "lineup_net";
};

int lineup_net_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<LineupNetFlowState*>(data);
    if (wait_for_team_menu()) {
        SDL_Delay(1000);
        interact("scenario");
        if (wait_for_interactable("lineup", 10000)) {
            SDL_Delay(750);
            interact("lineup");
            state->page_opened =
                wait_for_interactable_at("back", 8, 176, 10000);
            if (state->page_opened) {
                SDL_Delay(1000);
                state->knobs_visible =
                    interactable_visible("lineup_bots_0");
                state->captures += capture_frame(state->capture_name);
                SDL_Delay(200);
                interact("back");  // LINEUP -> SCENARIO
            }
        }
        if (wait_for_interactable_at("back", 30, 170, 5000)) {
            SDL_Delay(300);
            interact("back");  // SCENARIO -> Base Camp
        }
        if (wait_for_team_menu(5000)) {
            SDL_Delay(300);
            interact("back");
        }
    }
    state->finished = true;
    return 0;
}

void run_lineup_net_flow(bool host_view, const char* capture_name,
                         LineupNetFlowState& state)
{
    trace_clear();
    // Mount the versus campaign so the level-reload guard finds scen 500
    // (the fixture drives create_team_menu directly; no save0 load mounts
    // it for us).
    (void)unmount_campaign_package_with_error(get_mounted_campaign());
    (void)mount_campaign_package_with_error("modes");
    seed_session_save_for_net("modes", 500);
    FakeNetLobbyClient client;
    client.host_view = host_view;
    client.players = {
        make_probe_seat(0, "net-host", "IRON KETTLE BAND", true, 0,
                        host_view ? std::vector<FighterSeed>{}
                                  : std::vector<FighterSeed>{
                                        {"WREN", 2, true, 0},
                                        {"ASHA", 3, true, 0}}),
        make_probe_seat(1, "net-join", "JOIN RIVER BAND", false, 1,
                        host_view ? std::vector<FighterSeed>{
                                        {"WREN", 2, true, 1},
                                        {"ASHA", 3, true, 1}}
                                  : std::vector<FighterSeed>{}),
    };
    client.local_indices = {host_view ? std::uint8_t{0} : std::uint8_t{1}};
    ActiveLobbyGuard guard(&client);
    state.capture_name = capture_name;
    SDL_Thread* thread =
        SDL_CreateThread(lineup_net_flow_injector, "lineup_net", &state);
    ASSERT_NE(nullptr, thread);
    picker_load_menu_backdrops();
    create_team_menu(0);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    restore_gladiator_mount();
}

} // namespace

TEST(LineupUi, networked_host_sees_knobs)
{
    SavedPickerSave save_guard;
    LineupNetFlowState state;
    run_lineup_net_flow(true, "lineup_host_networked", state);
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.page_opened);
    EXPECT_TRUE(state.knobs_visible)
        << "the host keeps the per-team bot knobs";
    EXPECT_EQ(1, state.captures);
}

TEST(LineupUi, networked_joiner_sees_no_knobs)
{
    SavedPickerSave save_guard;
    LineupNetFlowState state;
    run_lineup_net_flow(false, "lineup_joiner", state);
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.page_opened)
        << "the LINEUP door is not host-gated: joiners open it read-only";
    EXPECT_FALSE(state.knobs_visible)
        << "joiners never see the bot knobs (§2.3)";
    EXPECT_EQ(1, state.captures);
}

namespace {

// ---------------------------------------------------------------------------
// Flow 5: a classic campaign shows the knobs dimmed and inert — the click
// is engine-dead and the save knob never moves.

struct LineupClassicFlowState
{
    bool finished = false;
    bool page_opened = false;
    bool knobs_visible = false;
    int captures = 0;
};

int lineup_classic_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<LineupClassicFlowState*>(data);
    if (!injector_open_lineup()) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(300);
    interact("lineup");
    state->page_opened = wait_for_interactable_at("back", 8, 176, 10000);
    if (state->page_opened) {
        SDL_Delay(750);
        state->knobs_visible = interactable_visible("lineup_bots_0");
        state->captures += capture_frame("lineup_classic_campaign");
        SDL_Delay(200);
        // Disabled rows are engine-inert: this click must change nothing.
        interact("lineup_bots_0");
        SDL_Delay(500);
        interact("back");
    }
    injector_unwind_from_scenario();
    state->finished = true;
    return 0;
}

} // namespace

TEST(LineupUi, classic_campaign_dims_knobs_inert)
{
    trace_clear();
    SavedPickerSave save_guard;
    write_save0_with_fighters("gladiator", 1, 1,
                              {{"Alpha", 3, true, 0}, {"Beta", 2, true, 0}});

    LineupClassicFlowState state;
    SDL_Thread* thread = SDL_CreateThread(lineup_classic_flow_injector,
                                          "lineup_classic", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.page_opened);
    EXPECT_TRUE(state.knobs_visible)
        << "classic campaigns keep the knobs visible (dimmed), not hidden";
    EXPECT_EQ(0, static_cast<int>(save.bot_squad[0]))
        << "a dimmed knob's click is inert";
    EXPECT_EQ(1, state.captures);
}

// ---------------------------------------------------------------------------
// Direct dispatch coverage (no injector): the knob callbacks' gate branches,
// the SPLIT actions' EVEN / UNITE arms, and the FIGHTERS spec-row pager.

TEST(LineupUi, knob_callbacks_gate_branches)
{
    trace_clear();
    SavedPickerSave save_guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.current_campaign = "modes";
    save.bot_squad = {};
    save.bot_level = {};

    // Out-of-range team: inert.
    EXPECT_EQ(MENU_OK, change_lineup_bots(-1));
    EXPECT_EQ(MENU_OK, change_lineup_level(4));

    {
        // Joiner: §2.7-style denial — popup, TRACE, no local cycle.
        FakeNetLobbyClient joiner;
        joiner.host_view = false;
        ActiveLobbyGuard guard(&joiner);
        EXPECT_EQ(MENU_OK, change_lineup_bots(0));
        EXPECT_EQ(MENU_OK, change_lineup_level(0));
        EXPECT_TRUE(trace_contains("lineup", "bots_denied"));
        EXPECT_TRUE(trace_contains("lineup", "level_denied"));
        EXPECT_EQ(0, static_cast<int>(save.bot_squad[0]));
        EXPECT_EQ(0, static_cast<int>(save.bot_level[0]));
    }

    // Classic campaign: the belt behind the engine's Disabled grammar.
    save.current_campaign = "gladiator";
    EXPECT_EQ(MENU_OK, change_lineup_bots(0));
    EXPECT_EQ(MENU_OK, change_lineup_level(0));
    EXPECT_EQ(0, static_cast<int>(save.bot_squad[0]));
    EXPECT_EQ(0, static_cast<int>(save.bot_level[0]));

    // Host + versus: the LV wheel walks AUTO -> 1..9 -> AUTO on an empty
    // band's knob, and the save value rides every step through the clamp.
    save.current_campaign = "modes";
    for (int expected = 1; expected <= 9; ++expected)
    {
        EXPECT_EQ(MENU_OK, change_lineup_level(3));
        EXPECT_EQ(expected, static_cast<int>(save.bot_level[3]));
    }
    EXPECT_EQ(MENU_OK, change_lineup_level(3));
    EXPECT_EQ(0, static_cast<int>(save.bot_level[3])) << "9 wraps to AUTO";

    // A toast with no LINEUP state installed still traces (never crashes).
    og::ui::lineup_show_toast("NOWHERE TO LAND");
    EXPECT_TRUE(trace_contains("lineup", "toast NOWHERE TO LAND"));

    // The settings sync may have re-mounted the versus campaign.
    restore_gladiator_mount();
}

// §6: a joiner that is only connecting — or whose link died with the last
// roster still cached — is not in a session, so LINEUP must show it the LOCAL
// picture: its own deployed company under synthesized seats. Reading the
// unestablished client instead painted NO SEAT over every band and NO FIGHTERS
// over a company sitting right there in the save.
TEST(LineupUi, unestablished_joiner_reads_the_local_seat_picture)
{
    trace_clear();
    SavedPickerSave save_guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    const std::vector<FighterSeed> roster = {
        {"HOME", 3, true, 0}, {"AWAY", 3, true, 1},
    };
    for (std::size_t i = 0; i < roster.size(); ++i)
    {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = roster[i].name;
        member->upgrade_to_level(roster[i].level, true);
        member->deployed = roster[i].deployed;
        member->teamnum = roster[i].team;
        save.team_list[i] = std::move(member);
    }
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 2;
    save.allied_mode = 0;

    FakeNetLobbyClient client;
    client.host_view = false;      // a joiner
    client.established = false;    // ...with no session behind it
    // The retained roster of the session that just died: two foreign seats,
    // and an ownership grant this machine can no longer act on.
    client.players = {
        make_probe_seat(0, "net-host", "IRON KETTLE BAND", true, 0, roster),
        make_probe_seat(1, "net-self", "RIVER BAND", false, 1, roster),
    };
    client.local_indices = {1};
    ActiveLobbyGuard guard(&client);

    const LineupSeatView pending = picker_lineup_seat_view();
    ASSERT_EQ(2u, pending.players.size())
        << "the local save's two seats, synthesized";
    EXPECT_EQ(save.save_name, pending.players[0].company)
        << "the retained foreign roster is not this machine's picture";
    EXPECT_EQ((std::vector<std::uint8_t>{0, 1}), pending.local_indices)
        << "every seat in the local picture is this machine's";

    const std::array<og::ui::LineupTeamBand, 4> bands =
        og::ui::build_lineup_bands(save, pending.players,
                                   pending.local_indices,
                                   picker_lobby_session_established(),
                                   og::ui::LineupPowerFn{});
    EXPECT_TRUE(bands[0].has_seat) << "TEAM 1 must not read NO SEAT";
    EXPECT_TRUE(bands[1].has_seat) << "TEAM 2 must not read NO SEAT";
    EXPECT_EQ(1, bands[0].fighter_count) << "HOME fights for TEAM 1";
    EXPECT_EQ(1, bands[1].fighter_count) << "AWAY fights for TEAM 2";

    // The session lands: the lobby picture takes over, ownership included.
    client.established = true;
    const LineupSeatView live = picker_lineup_seat_view();
    ASSERT_EQ(2u, live.players.size());
    EXPECT_EQ("IRON KETTLE BAND", live.players[0].company)
        << "an established session reads the replicated roster";
    EXPECT_EQ((std::vector<std::uint8_t>{1}), live.local_indices)
        << "and the server's ownership grant";
}

TEST(LineupUi, split_even_and_unite_direct)
{
    trace_clear();
    SavedPickerSave save_guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    const std::vector<FighterSeed> roster = {
        {"A", 3, true, 0}, {"B", 3, true, 0}, {"C", 3, true, 0},
        {"D", 3, true, 0}, {"E", 3, false, 0},  // benched: never drafted
    };
    for (std::size_t i = 0; i < roster.size(); ++i)
    {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = roster[i].name;
        member->upgrade_to_level(roster[i].level, true);
        member->deployed = roster[i].deployed;
        member->teamnum = roster[i].team;
        save.team_list[i] = std::move(member);
    }
    save.team_size = 5;
    save.my_team = 0;
    save.numplayers = 2;
    // A legacy Together save (allied_mode) would seed BOTH seats onto team
    // 0 and turn every split into ALL TO 1.
    save.allied_mode = 0;
    save.current_campaign = "modes";
    // The live local lobby derives its seats when the count changes, not
    // when the save field is poked: declare the second seat properly.
    picker_lobby_set_player_mode(2);

    // EVEN deals round-robin over the derived local seat teams {0,1}.
    EXPECT_EQ(MENU_OK, lineup_split_action(0));
    EXPECT_EQ(0, save.team_list[0]->teamnum);
    EXPECT_EQ(1, save.team_list[1]->teamnum);
    EXPECT_EQ(0, save.team_list[2]->teamnum);
    EXPECT_EQ(1, save.team_list[3]->teamnum);
    EXPECT_EQ(0, save.team_list[4]->teamnum) << "the bench does not move";
    EXPECT_TRUE(trace_contains("lineup", "split mode=0 moved=2 locked=0"));

    // UNITE marches everyone to the lowest local seated team, with the
    // §2.2 toast.
    EXPECT_EQ(MENU_OK, lineup_split_action(2));
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(0, save.team_list[static_cast<std::size_t>(i)]->teamnum)
            << "slot " << i;
    EXPECT_TRUE(trace_contains("lineup", "split mode=2"));
    EXPECT_TRUE(trace_contains("lineup", "toast ALL FIGHTERS TO TEAM 1"));

    // Hand back a one-seat lobby and the default mount (the shared-sweep
    // restore discipline).
    picker_lobby_set_player_mode(1);
    restore_gladiator_mount();
}

namespace {

// Save/restore the pack-script registry around a synthetic base_camp zone
// (the test_campaign_zone_ui guard). The chunk name deliberately does NOT
// start with `packs/` — that prefix declares bytes to the pack-Lua coverage
// inventory, and this chunk exists nowhere in the repository.
class SyntheticZoneScriptGuard
{
public:
    explicit SyntheticZoneScriptGuard(const char* source)
        : previous_game_(current_game), saved_(og::script::pack_scripts())
    {
        current_game = nullptr;  // dispatch resolves the shared UI VM
        og::script::register_pack_script(
            {"test.lineupzone", "lineupzonetest/scripts/c.lua", source});
    }

    ~SyntheticZoneScriptGuard()
    {
        og::script::clear_pack_scripts();
        for (const og::script::PackScript& script : saved_)
            og::script::register_pack_script(script);
        current_game = previous_game_;
    }

private:
    GameplayContext* previous_game_;
    std::vector<og::script::PackScript> saved_;
};

} // namespace

// §5 + §2.2: the three SPLIT actions are bulk team assignments, so they obey
// the campaign's own can_team rule — the rule that already gates one FIGHTERS
// row at a time and the Base Camp chip. lineup_split_action used to plan
// against a NULL zone, which made the strip a way around a composition that
// had taken the team chip away.
TEST(LineupUi, split_actions_obey_the_campaign_can_team_rule)
{
    trace_clear();
    SavedPickerSave save_guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    const std::vector<FighterSeed> roster = {
        {"A", 3, true, 0}, {"B", 3, true, 0},
        {"C", 3, true, 1}, {"D", 3, true, 1},
    };
    for (std::size_t i = 0; i < roster.size(); ++i)
    {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = roster[i].name;
        member->upgrade_to_level(roster[i].level, true);
        member->deployed = roster[i].deployed;
        member->teamnum = roster[i].team;
        save.team_list[i] = std::move(member);
    }
    save.team_size = 4;
    save.my_team = 0;
    save.numplayers = 2;
    save.allied_mode = 0;
    picker_lobby_set_player_mode(2);

    // Settle the campaign LAST: the lobby's seat declaration rewrites the
    // save's campaign cursor, and a mount mismatch would remount mid-test —
    // a remount clears the pack-script registry the synthetic zone lives in.
    save.current_campaign = "gladiator";
    restore_gladiator_mount();
    SyntheticZoneScriptGuard zone(R"LUA(
og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster", can_team = false } } }
  end,
})
)LUA");

    ASSERT_FALSE(og::ui::lineup_zone_can_team(save))
        << "the synthetic composition must be the live one";
    const std::array<short, 4> before = {
        save.team_list[0]->teamnum, save.team_list[1]->teamnum,
        save.team_list[2]->teamnum, save.team_list[3]->teamnum};

    for (int mode = 0; mode < 3; ++mode)
    {
        trace_clear();
        EXPECT_EQ(MENU_OK, lineup_split_action(mode));
        const std::string expected =
            std::format("split mode={} moved=0 locked=4", mode);
        EXPECT_TRUE(trace_contains("lineup", expected.c_str()))
            << "mode " << mode << ": every slot is locked, nothing moves";
        EXPECT_TRUE(trace_contains("lineup", "toast 4 LOCKED SLOTS KEPT"))
            << "mode " << mode << ": the refusal is on the toast, and it "
                                  "does not claim a march that never happened";
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_EQ(before[static_cast<std::size_t>(i)],
                      save.team_list[static_cast<std::size_t>(i)]->teamnum)
                << "mode " << mode << " slot " << i;
        }
    }

    picker_lobby_set_player_mode(1);
    restore_gladiator_mount();
}

// §2.2: the FIGHTERS screen's capabilities come from the campaign zone, and
// the zone is a function of campaign state the level cursor is part of. A
// host changing the level under a parked joiner therefore has to move this
// screen's capabilities with it — base_camp_frame_tick refetches on exactly
// this guard. Without the refetch the screen kept the previous level's
// composition: deploy boxes still live on a level that forbids deploying.
TEST(LineupUi, fighters_refetches_the_zone_after_a_level_change)
{
    trace_clear();
    SavedPickerSave save_guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = "PARKED";
    member->deployed = true;
    save.team_list[0] = std::move(member);
    save.team_size = 1;
    save.scen_num = 1;
    save.current_campaign = "gladiator";
    restore_gladiator_mount();

    // The composition reads the LEVEL: deploying is forbidden on level 2.
    SyntheticZoneScriptGuard zone_script(R"LUA(
og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster",
                           can_deploy = og.campaign_current_level() ~= 2 } } }
  end,
})
)LUA");

    og::ui::CampaignZoneSession zone(save);
    zone.fetch();
    ASSERT_TRUE(zone.roster().can_deploy) << "level 1 deploys";

    og::ui::LineupFightersScreenState state;
    state.last_level_id = save.scen_num;
    state.zone = &zone;
    og::ui::lineup_fighters_refresh_rows(state);
    og::ui::install_lineup_fighters_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::LineupFighters).spec;
    ASSERT_NE(nullptr, spec.frame_tick);

    // A quiet frame changes nothing.
    EXPECT_TRUE(spec.frame_tick(&state, 0));
    EXPECT_TRUE(zone.roster().can_deploy);

    // The host moves the lobby to level 2 under the open screen.
    save.scen_num = 2;
    EXPECT_TRUE(spec.frame_tick(&state, 0));
    EXPECT_FALSE(zone.roster().can_deploy)
        << "the zone must be refetched with the new level cursor";
    EXPECT_TRUE(trace_contains("zone", "refetch"));

    // ...and the deploy boxes really do go inert on the next rewire.
    button* const buttons = picker_lineup_fighters_buttons();
    const int count = picker_lineup_fighters_button_count();
    std::vector<button> table(buttons, buttons + count);
    int highlighted = kLineupFightersBackIndex;
    og::ui::install_lineup_fighters_state_for_screen(&state);
    spec.nav.rewire(table.data(), count, highlighted);
    EXPECT_TRUE(table[kLineupFightersDeployBase].hidden)
        << "row 0's deploy box is gone with the capability";

    og::ui::install_lineup_fighters_state_for_screen(nullptr);
    save.scen_num = 1;
    restore_gladiator_mount();
}

TEST(LineupUi, fighters_spec_row_pager_and_guards)
{
    trace_clear();
    SavedPickerSave save_guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    for (int i = 0; i < 10; ++i)
    {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = "S" + std::to_string(i);
        member->deployed = true;
        save.team_list[static_cast<std::size_t>(i)] = std::move(member);
    }
    save.team_size = 10;

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::LineupFighters).spec;
    ASSERT_NE(nullptr, spec.on_spec_row);

    // Null state: every row is inert.
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kLineupFightersBodyBase, nullptr));

    og::ui::LineupFightersScreenState state;
    og::ui::lineup_fighters_refresh_rows(state);
    ASSERT_TRUE(state.page.multi_page());
    og::ui::install_lineup_fighters_state_for_screen(&state);

    // Pager: NEXT flips to page 2 (rows 8..9), PREV flips back.
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kLineupFightersNextIndex, &state));
    EXPECT_EQ(1, state.page.page);
    EXPECT_TRUE(trace_contains("lineup", "fighters_page 1"));
    // A row past the window on the second page: inert.
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kLineupFightersBodyBase + 5, &state));
    // Row 0 of page 2 = save slot 8: the body click cycles ITS team.
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kLineupFightersBodyBase, &state));
    EXPECT_EQ(1, save.team_list[8]->teamnum);
    // The deploy box benches the same slot.
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kLineupFightersDeployBase, &state));
    EXPECT_FALSE(save.team_list[8]->deployed);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kLineupFightersPrevIndex, &state));
    EXPECT_EQ(0, state.page.page);
    // An unknown ordinal: inert.
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kLineupFightersButtonCount + 3, &state));

    og::ui::install_lineup_fighters_state_for_screen(nullptr);
}

// A locked (foreign-owned) slot refuses BOTH row actions with a TOAST —
// never a modal, which would strand a networked joiner mid-GO (§2.3) —
// and mutates nothing.
TEST(LineupUi, fighters_locked_slot_refuses_with_toast)
{
    trace_clear();
    SavedPickerSave save_guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = "HELD";
    member->deployed = true;
    member->teamnum = 0;
    save.team_list[0] = std::move(member);
    save.team_size = 1;

    FakeNetLobbyClient client;
    client.slots_editable = false;
    ActiveLobbyGuard guard(&client);

    og::ui::LineupFightersScreenState state;
    og::ui::lineup_fighters_refresh_rows(state);
    og::ui::install_lineup_fighters_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::LineupFighters).spec;

    EXPECT_EQ(MENU_OK, spec.on_spec_row(kLineupFightersBodyBase, &state));
    EXPECT_EQ(0, save.team_list[0]->teamnum) << "a locked row never cycles";
    EXPECT_TRUE(trace_contains("lineup", "team_slot_locked slot=0"));
    EXPECT_TRUE(trace_contains("lineup", "toast SLOT LOCKED"));

    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kLineupFightersDeployBase, &state));
    EXPECT_TRUE(save.team_list[0]->deployed)
        << "a locked row never benches";
    EXPECT_TRUE(trace_contains("lineup", "deploy_slot_locked slot=0"));
    EXPECT_FALSE(state.toast.empty());

    og::ui::install_lineup_fighters_state_for_screen(nullptr);
}

// The TRAIN team cycler's relabel (docs/lineup-design.md §1): "Team N", not
// "Playing on Team N" — the phrase implied a seat where the control writes
// a character's fighting colour. The spec table carries the static shape.
TEST(LineupUi, train_team_cycler_relabelled)
{
    button* buttons = picker_trainmenu_buttons();
    ASSERT_GT(picker_trainmenu_button_count(), kTrainMenuChangeTeamIndex);
    EXPECT_EQ("Team X", buttons[kTrainMenuChangeTeamIndex].label);
    EXPECT_LE(static_cast<int>(
                  buttons[kTrainMenuChangeTeamIndex].label.size()) * 6,
              buttons[kTrainMenuChangeTeamIndex].sizex);
}
