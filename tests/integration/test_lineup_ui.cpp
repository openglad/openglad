// LINEUP screen flows (docs/lineup-design.md §2, amendment B1-B9): the
// SCENARIO door over the re-gridded SCORE row, the four team bands with
// their FILL wheel + MAP UNITS box, the SPLIT actions, and the Base Camp
// roster chip that inherited the FIGHTERS list's networked team cycler
// (B6). Injector-driven through the real picker (interact by button id,
// never coordinates), with UxShots-pattern frame captures that double as
// visual smoke tests when UXSHOTS_DIR is unset.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
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
        snapshot_fields.fill = save.fill;
        snapshot_fields.map_units = save.map_units;
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
        save.fill = snapshot_fields.fill;
        save.map_units = snapshot_fields.map_units;
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
    save.fill = {};
    save.map_units = {};
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
    void sync_roster_from_save() override { ++roster_syncs; }
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
    int roster_syncs = 0;
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
// Flow 1: SCENARIO carries the LINEUP door over the re-gridded knob row
// (B5: SCORE alone at (30,140)); the page opens; the host's FILL wheel
// walks all five labels in display order (B2: NONE, WEAK, FAIR, STRONG,
// BRUTAL — entered from FAIR, so the clicks read STRONG, BRUTAL, NONE,
// WEAK, FAIR) with the save value pinned at every stop; the MAP UNITS box
// (B4) flips only where the map ships units (scen 501 ships 5 on RED,
// nothing anywhere else). End-to-end (B7): FILL: STRONG on RED stages a
// solved squad and VIEW LEVEL names it with its fill word, while RED's map
// units switched OFF drop its MAP TROOPS row from the same census.

struct LineupFillFlowState
{
    bool finished = false;
    bool door_seen = false;
    bool score_cell_seen = false;
    bool page_opened = false;
    bool wheel_walked = false;
    std::array<short, 5> wheel_values = {-1, -1, -1, -1, -1};
    bool fill_red_strong = false;
    bool fill_blue_weak = false;
    bool map_units_red_off = false;
    bool viewer_opened_before = false;
    bool troops_line_before = false;
    bool viewer_opened_after = false;
    bool troops_line_after = true;
    std::string red_line_after;
    int captures = 0;
};

// Walk a knob through consecutive labels, one click per step (each step
// waits for its own label, so a swallowed click is retried, never skipped;
// the 300ms settle after every label flip is the menus-skill rule — the
// press is still held when the label flips, and a second press without a
// release is silently dropped).
bool click_through_labels(const std::string& id,
                          const std::vector<std::string>& labels)
{
    for (const std::string& label : labels) {
        if (!click_until_label(id, label, 3, 2000))
            return false;
        SDL_Delay(300);
    }
    return true;
}

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

std::string first_picker_trace_line_containing(const char* needle)
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    for (const TraceEntry& entry : g_trace_buffer) {
        if (entry.category == "picker" &&
            entry.message.find(needle) != std::string::npos)
        {
            return entry.message;
        }
    }
    return std::string();
}

int lineup_fill_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<LineupFillFlowState*>(data);
    if (!injector_open_lineup()) {
        state->finished = true;
        return 0;
    }
    state->door_seen = true;
    // The re-gridded knob row (B5): SCORE alone at (30,140), reading MAP.
    state->score_cell_seen =
        wait_for_interactable_at("ctf_caps", 30, 140, 5000) &&
        wait_for_interactable_label("ctf_caps", "SCORE: MAP", 5000);
    SDL_Delay(750);
    state->captures += capture_frame("scenario_score_only");
    SDL_Delay(200);

    // VIEW LEVEL first: with every knob at its default the staged census
    // fields RED's five authored map units.
    interact("view_scenario");
    state->viewer_opened_before =
        wait_for_interactable_at("back", 10, 170, 10000);
    if (state->viewer_opened_before) {
        state->troops_line_before = wait_for_trace(
            "picker", "view_scenario line   RED TEAM  ACTIVE - MAP TROOPS (5)",
            10000);
        SDL_Delay(300);
        interact("back");
        SDL_Delay(300);
        (void)wait_for_interactable("progress", 10000);
        SDL_Delay(300);
    }

    interact("lineup");
    // The page: the action strip's BACK sits at its own (8,176) geometry.
    state->page_opened = wait_for_interactable_at("back", 8, 176, 10000);
    if (state->page_opened) {
        SDL_Delay(750);
        // The pristine page: three bands' boxes dimmed with the
        // NO MAP UNITS hint (only RED ships units on scen 501).
        state->captures += capture_frame("lineup_no_map_units_dimmed");
        SDL_Delay(300);

        // The FILL wheel on RED, all five labels with the save pinned at
        // every stop (the wheel is a display order, not the storage order).
        const std::array<const char*, 5> wheel_labels = {
            "FILL: STRONG", "FILL: BRUTAL", "FILL: NONE", "FILL: WEAK",
            "FILL: FAIR"};
        state->wheel_walked = true;
        for (std::size_t step = 0; step < wheel_labels.size(); ++step) {
            if (!click_until_label("lineup_fill_0", wheel_labels[step], 3,
                                   2000))
            {
                state->wheel_walked = false;
                break;
            }
            SDL_Delay(300);
            state->wheel_values[step] =
                og::runtime::current_session->myscreen_->save_data.fill[0];
        }
        // Land RED on STRONG for the end-to-end staged read.
        state->fill_red_strong =
            click_until_label("lineup_fill_0", "FILL: STRONG");
        SDL_Delay(300);
        // BLUE takes WEAK (the capture shows two different fill words).
        state->fill_blue_weak = click_through_labels(
            "lineup_fill_1",
            {"FILL: STRONG", "FILL: BRUTAL", "FILL: NONE", "FILL: WEAK"});
        SDL_Delay(300);

        // The MAP UNITS box: RED's is live (5 authored units) and flips
        // OFF; BLUE's is dimmed-inert (the map ships none there), so its
        // click must move nothing.
        interact("lineup_map_units_0");
        state->map_units_red_off =
            wait_for_trace("lineup", "map_units team=0 value=1", 5000);
        SDL_Delay(300);
        interact("lineup_map_units_1");
        SDL_Delay(500);

        state->captures += capture_frame("lineup_fill_wheel");
        SDL_Delay(300);
        interact("back");  // LINEUP -> SCENARIO
    }

    // VIEW LEVEL again: the restaged census drops RED's map troops (the
    // box turned them off) and fields the STRONG squad instead. Clear the
    // trace ledger so the assertions read THIS visit, not the first one.
    if (wait_for_interactable("view_scenario", 10000)) {
        SDL_Delay(750);
        trace_clear();
        interact("view_scenario");
        state->viewer_opened_after =
            wait_for_interactable_at("back", 10, 170, 10000);
        if (state->viewer_opened_after) {
            (void)wait_for_trace(
                "picker", "view_scenario line   RED TEAM  ACTIVE - BOT SQUAD",
                10000);
            (void)wait_for_trace("picker", "view_scenario lines=", 5000);
            state->troops_line_after =
                trace_contains("picker", "MAP TROOPS (5)");
            state->red_line_after =
                first_picker_trace_line_containing("RED TEAM  ACTIVE");
            SDL_Delay(300);
            state->captures += capture_frame("view_level_fill_labels");
            SDL_Delay(300);
            interact("back");
            SDL_Delay(300);
            (void)wait_for_interactable("progress", 10000);
            SDL_Delay(300);
        }
    }
    injector_unwind_from_scenario();
    state->finished = true;
    return 0;
}

// ---------------------------------------------------------------------------

} // namespace

TEST(LineupUi, fill_wheel_map_units_box_and_staged_labels_end_to_end)
{
    trace_clear();
    SavedPickerSave save_guard;
    // CTF: A BORDER FORT (scen 501) is the one modes level that ships
    // authored map units: five livings on RED, none anywhere else. The
    // company holds BLUE with two seats, so RED's census is purely the
    // map's own.
    write_save0_with_fighters("modes", 501, 2,
                              {{"Alpha", 3, true, 1}, {"Beta", 2, true, 1}});

    LineupFillFlowState state;
    SDL_Thread* thread = SDL_CreateThread(lineup_fill_flow_injector,
                                          "lineup_fill", &state);
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
    EXPECT_TRUE(state.score_cell_seen)
        << "SCORE: MAP alone at (30,140) (B5)";
    EXPECT_TRUE(state.viewer_opened_before);
    EXPECT_TRUE(state.troops_line_before)
        << "with MAP UNITS on, RED fields its five authored units";
    EXPECT_TRUE(state.page_opened) << "the LINEUP page should open";
    EXPECT_TRUE(state.wheel_walked)
        << "the FILL wheel walks STRONG, BRUTAL, NONE, WEAK, FAIR from "
           "FAIR (B2 display order)";
    // The save value at every stop of the wheel — each write survived every
    // later per-frame picker_lobby_poll() only because change_lineup_fill
    // pushed it into the lobby first.
    EXPECT_EQ(og::sim::kFillStrong, state.wheel_values[0]);
    EXPECT_EQ(og::sim::kFillBrutal, state.wheel_values[1]);
    EXPECT_EQ(og::sim::kFillNone, state.wheel_values[2]);
    EXPECT_EQ(og::sim::kFillWeak, state.wheel_values[3]);
    EXPECT_EQ(og::sim::kFillFair, state.wheel_values[4]);
    EXPECT_TRUE(state.fill_red_strong);
    EXPECT_TRUE(state.fill_blue_weak);
    EXPECT_EQ(og::sim::kFillStrong, save.fill[0]);
    EXPECT_EQ(og::sim::kFillWeak, save.fill[1]);
    EXPECT_TRUE(state.map_units_red_off)
        << "RED's live MAP UNITS box flips OFF";
    EXPECT_EQ(og::sim::kMapUnitsOff, save.map_units[0]);
    EXPECT_EQ(og::sim::kMapUnitsOn, save.map_units[1])
        << "a box on a team the map ships nothing for is inert (B4)";
    EXPECT_FALSE(trace_contains("lineup", "map_units team=1"))
        << "the inert box never reaches the callback's write";
    EXPECT_TRUE(state.viewer_opened_after);
    EXPECT_FALSE(state.troops_line_after)
        << "MAP UNITS: OFF strips RED's authored units from the stage (B4)";
    // MATCHED BOTS, not BOT SQUAD: the two labels are the report's honest
    // record of which solver ran, and the company on BLUE is human power, so
    // B3's reference exists and RED's squad is solved against it. BOT SQUAD
    // is the legacy-formula label, reachable only with no human power
    // anywhere — which this flow, with two seats on the board, is not.
    EXPECT_NE(std::string::npos,
              state.red_line_after.find("MATCHED BOTS"))
        << "FILL: STRONG fields a solved squad on RED: '"
        << state.red_line_after << "'";
    const std::string strong_tail = "STRONG";
    ASSERT_GE(state.red_line_after.size(), strong_tail.size());
    EXPECT_EQ(strong_tail,
              state.red_line_after.substr(state.red_line_after.size() -
                                          strong_tail.size()))
        << "the row closes with its fill word (B7): '"
        << state.red_line_after << "'";
    EXPECT_EQ(4, state.captures) << "all four captures should land";

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
// Flows 4a/4b: networked joiner and host over the direct create_team_menu
// fixture (the UxShots FakeNetLobbyClient pattern — no sockets).

struct LineupNetFlowState
{
    bool finished = false;
    bool page_opened = false;
    bool knobs_visible = false;
    bool boxes_visible = false;
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
                    interactable_visible("lineup_fill_0");
                state->boxes_visible =
                    interactable_visible("lineup_map_units_0");
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
        << "the host keeps the per-team FILL knobs";
    EXPECT_TRUE(state.boxes_visible)
        << "...and the MAP UNITS boxes beside them";
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
        << "joiners never see the band knobs (§2.3)";
    EXPECT_FALSE(state.boxes_visible)
        << "the MAP UNITS box is a knob too: hidden for joiners";
    EXPECT_EQ(1, state.captures);
}

namespace {

// ---------------------------------------------------------------------------
// Flow 4c (B6): the Base Camp roster chip is the networked home of the
// per-fighter team cycler now that the FIGHTERS list retired. An OWN row's
// chip is visible and cycles the fighting colour through the ONE shared
// predicate, the mutation tail re-syncs the lobby roster, and foreign rows
// never grow a chip.

struct BasecampChipFlowState
{
    bool finished = false;
    bool own_chip_visible = false;
    bool foreign_chip_inert = false;
    bool chip_cycled = false;
    int captures = 0;
};

int basecamp_chip_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<BasecampChipFlowState*>(data);
    if (wait_for_team_menu()) {
        SDL_Delay(1000);
        state->own_chip_visible = wait_for_interactable("roster_team_0", 10000);
        // Display rows 4..5 are the foreign machine's replicated slots:
        // their chip ordinals stay hidden (inert) on every frame.
        state->foreign_chip_inert =
            !interactable_visible("roster_team_4") &&
            !interactable_visible("roster_team_5");
        SDL_Delay(300);
        state->captures += capture_frame("basecamp_networked_chip");
        SDL_Delay(300);
        interact("roster_team_0");
        state->chip_cycled =
            wait_for_trace("basecamp", "team slot=0 team=1", 5000);
        SDL_Delay(300);
        interact("back");
    }
    state->finished = true;
    return 0;
}

} // namespace

TEST(LineupUi, basecamp_chip_cycles_own_row_networked_and_resyncs)
{
    trace_clear();
    SavedPickerSave save_guard;
    (void)unmount_campaign_package_with_error(get_mounted_campaign());
    (void)mount_campaign_package_with_error("modes");
    seed_session_save_for_net("modes", 500);
    FakeNetLobbyClient client;
    client.host_view = true;
    client.players = {
        make_probe_seat(0, "net-host", "IRON KETTLE BAND", true, 0, {}),
        make_probe_seat(1, "net-join", "JOIN RIVER BAND", false, 1,
                        {{"WREN", 2, true, 1}, {"ASHA", 3, true, 1}}),
    };
    client.local_indices = {0};
    ActiveLobbyGuard guard(&client);

    BasecampChipFlowState state;
    SDL_Thread* thread = SDL_CreateThread(basecamp_chip_flow_injector,
                                          "basecamp_chip", &state);
    ASSERT_NE(nullptr, thread);
    picker_load_menu_backdrops();
    const int syncs_before = client.roster_syncs;
    create_team_menu(0);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    restore_gladiator_mount();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.own_chip_visible)
        << "B6: the own-row chip shows in a networked session";
    EXPECT_TRUE(state.foreign_chip_inert)
        << "foreign rows never grow a chip";
    EXPECT_TRUE(state.chip_cycled) << "the chip click cycles and traces";
    ASSERT_NE(nullptr, save.team_list[0]);
    EXPECT_EQ(1, save.team_list[0]->teamnum)
        << "GORT's fighting colour cycled 1 -> 2";
    EXPECT_GT(client.roster_syncs, syncs_before)
        << "the mutation tail re-syncs the lobby roster (B6)";
    EXPECT_EQ(1, state.captures);
}

namespace {

// ---------------------------------------------------------------------------
// Flow 5 (amendment C5): a classic campaign's knobs are LIVE — the classic
// dim retired when the lineup stage moved to packs/core, so the FILL wheel
// turns on gladiator exactly as it does on a versus campaign, and no band
// censuses MAP RULES any more.

struct LineupClassicFlowState
{
    bool finished = false;
    bool page_opened = false;
    bool knobs_visible = false;
    bool fill_cycled = false;
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
        state->knobs_visible = interactable_visible("lineup_fill_0");
        // C5: the click cycles the wheel — FAIR steps to STRONG (B2
        // display order) on a classic campaign too.
        state->fill_cycled =
            click_until_label("lineup_fill_0", "FILL: STRONG");
        SDL_Delay(500);
        state->captures += capture_frame("lineup_gladiator_live_knobs");
        SDL_Delay(200);
        interact("back");
    }
    injector_unwind_from_scenario();
    state->finished = true;
    return 0;
}

} // namespace

TEST(LineupUi, classic_campaign_knobs_are_live)
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
        << "classic campaigns keep the knobs visible AND live (C5)";
    EXPECT_TRUE(state.fill_cycled)
        << "the FILL wheel turns on a classic campaign (C5)";
    EXPECT_EQ(og::sim::kFillStrong, save.fill[0])
        << "the classic click writes the save knob";
    EXPECT_EQ(1, state.captures);

    // C5's POWER half, CLOSED (docs/lineup-design.md, "As built: W6-D"):
    // packs/core registers the default `lineup.power` through
    // og.register_default_lineup — its own per-VM slot, because the
    // campaign-book registrar is one-book-first-wins and a second call
    // poisons the book — so gladiator's bands price the deployed company
    // with the core lib's stat_power instead of reading `POWER --`.
    EXPECT_TRUE(og::script::hooks::campaign_lineup_registered())
        << "the shipped default prices a classic campaign's bands (C5)";
    og::ui::lineup_power_cache_clear();
    const LineupSeatView seats = picker_lineup_seat_view();
    const std::array<og::ui::LineupTeamBand, 4> bands =
        og::ui::build_lineup_bands(save, seats.players, seats.local_indices,
                                   picker_lobby_session_established(),
                                   &og::ui::lineup_power_for_guy);
    ASSERT_TRUE(bands[0].power.has_value())
        << "the deployed company's band reads a NUMBER, not POWER --";
    EXPECT_GT(*bands[0].power, 0);
    EXPECT_NE("POWER --", og::ui::format_lineup_power(bands[0].power));
    og::ui::lineup_power_cache_clear();
}

// C5, the other half of the same ruling: the default is the core lib's own
// stat_power, which is exactly what the modes book already registers — so
// the modes campaign's POWER numbers do NOT move when the default lands.
// The row and its answer are the pin test_modes_book carries
// (ED = (10 * (2 + 3)) / 4 = 12; RATE = 120 / 6 = 20; OFF = 12 * 20 + 15 =
// 255; EHP = 100 + 16 + 10 = 126; f = 126 * 315 / 60 = 661), asserted here
// on BOTH mounts: the book's answer and the default's are one number.
TEST(LineupUi, modes_power_pin_survives_the_shipped_default)
{
    trace_clear();
    SavedPickerSave save_guard;

    og::script::hooks::LineupPowerRow row;
    row.family = "SOLDIER";
    row.level = 2;
    row.hp = 100;
    row.mp = 20;
    row.armor = 4;
    row.damage = 10;
    row.stepsize = 3;
    row.fire_frequency = 6;

    (void)unmount_campaign_package_with_error(get_mounted_campaign());
    (void)mount_campaign_package_with_error("modes");
    ASSERT_TRUE(og::script::hooks::campaign_lineup_registered());
    long long modes_power = 0;
    ASSERT_TRUE(og::script::hooks::campaign_fighter_power(row, modes_power));
    EXPECT_EQ(661, modes_power)
        << "the modes book still prices with core stat_power";

    restore_gladiator_mount();
    ASSERT_TRUE(og::script::hooks::campaign_lineup_registered())
        << "gladiator prices through the shipped default (C5)";
    long long classic_power = 0;
    ASSERT_TRUE(og::script::hooks::campaign_fighter_power(row, classic_power));
    EXPECT_EQ(661, classic_power)
        << "one metric, one currency: the default IS the modes' pricing";
}

// ---------------------------------------------------------------------------
// Flow 6 (amendment C5): VIEW LEVEL's classic arm renders the same per-team
// fills census the staged (mode) arm does, end to end on gladiator scen 1.
// Three viewer visits: (a) all-default — the company on RED, the twelve
// authored map troops on GREEN; (b) GREEN's MAP UNITS box OFF with FILL:
// STRONG — the classic trade rule retires the troops and fields a solved
// squad wearing its fill word; (c) FILL: NONE with the box still OFF —
// nothing stands on GREEN and its line drops entirely (C4: fewer enemies,
// never a refusal). Gladiator authors no marker-only side teams (every
// start marker is RED's), so the box-trade is the one classic path to a
// squad — recorded in the design doc's W6-C section.

namespace {

struct LineupClassicViewerState
{
    bool finished = false;
    bool viewer_opened = false;
    bool company_line_seen = false;
    bool troops_line_seen = false;
    bool page_opened = false;
    bool fill_green_strong = false;
    bool map_units_green_off = false;
    bool viewer_fill_opened = false;
    bool troops_line_after_trade = true;
    std::string green_line_after_trade;
    bool fill_green_none = false;
    bool viewer_stripped_opened = false;
    bool green_line_when_none = true;
    bool company_line_still_there = false;
    int captures = 0;
};

int lineup_classic_viewer_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<LineupClassicViewerState*>(data);
    if (!injector_open_lineup()) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(300);

    // Visit (a): the all-default census block.
    interact("view_scenario");
    state->viewer_opened = wait_for_interactable_at("back", 10, 170, 10000);
    if (state->viewer_opened) {
        state->company_line_seen = wait_for_trace(
            "picker", "view_scenario line   RED TEAM  ACTIVE - COMPANY (2)",
            10000);
        state->troops_line_seen = wait_for_trace(
            "picker",
            "view_scenario line   GREEN TEAM  ACTIVE - MAP TROOPS (12)",
            10000);
        SDL_Delay(300);
        interact("back");
        SDL_Delay(300);
        (void)wait_for_interactable("progress", 10000);
        SDL_Delay(300);
    }

    // LINEUP: FILL: STRONG on GREEN and its MAP UNITS box OFF (the trade).
    interact("lineup");
    state->page_opened = wait_for_interactable_at("back", 8, 176, 10000);
    if (!state->page_opened) {
        injector_unwind_from_scenario();
        state->finished = true;
        return 0;
    }
    SDL_Delay(750);
    state->fill_green_strong =
        click_until_label("lineup_fill_1", "FILL: STRONG");
    SDL_Delay(300);
    interact("lineup_map_units_1");
    state->map_units_green_off =
        wait_for_trace("lineup", "map_units team=1 value=1", 5000);
    SDL_Delay(300);
    interact("back");  // LINEUP -> SCENARIO
    SDL_Delay(300);

    // Visit (b): the traded squad wears its fill word; the troops are gone.
    if (wait_for_interactable("view_scenario", 10000)) {
        SDL_Delay(750);
        trace_clear();
        interact("view_scenario");
        state->viewer_fill_opened =
            wait_for_interactable_at("back", 10, 170, 10000);
        if (state->viewer_fill_opened) {
            (void)wait_for_trace(
                "picker", "view_scenario line   GREEN TEAM  ACTIVE", 10000);
            (void)wait_for_trace("picker", "view_scenario lines=", 5000);
            state->troops_line_after_trade =
                trace_contains("picker", "MAP TROOPS (12)");
            state->green_line_after_trade =
                first_picker_trace_line_containing("GREEN TEAM  ACTIVE");
            SDL_Delay(300);
            state->captures += capture_frame("view_level_gladiator_fill");
            SDL_Delay(300);
            interact("back");
            SDL_Delay(300);
            (void)wait_for_interactable("progress", 10000);
            SDL_Delay(300);
        }
    }

    // LINEUP again: GREEN's wheel to NONE (STRONG -> BRUTAL -> NONE).
    interact("lineup");
    if (wait_for_interactable_at("back", 8, 176, 10000)) {
        SDL_Delay(750);
        state->fill_green_none = click_through_labels(
            "lineup_fill_1", {"FILL: BRUTAL", "FILL: NONE"});
        SDL_Delay(300);
        interact("back");
        SDL_Delay(300);
    }

    // Visit (c): nothing stands on GREEN — its line drops entirely.
    if (wait_for_interactable("view_scenario", 10000)) {
        SDL_Delay(750);
        trace_clear();
        interact("view_scenario");
        state->viewer_stripped_opened =
            wait_for_interactable_at("back", 10, 170, 10000);
        if (state->viewer_stripped_opened) {
            state->company_line_still_there = wait_for_trace(
                "picker",
                "view_scenario line   RED TEAM  ACTIVE - COMPANY (2)",
                10000);
            (void)wait_for_trace("picker", "view_scenario lines=", 5000);
            state->green_line_when_none =
                trace_contains("picker", "GREEN TEAM");
            SDL_Delay(300);
            state->captures +=
                capture_frame("view_level_gladiator_stripped");
            SDL_Delay(300);
            interact("back");
            SDL_Delay(300);
            (void)wait_for_interactable("progress", 10000);
            SDL_Delay(300);
        }
    }

    injector_unwind_from_scenario();
    state->finished = true;
    return 0;
}

} // namespace

TEST(LineupUi, classic_view_level_censuses_the_staged_world)
{
    trace_clear();
    SavedPickerSave save_guard;
    write_save0_with_fighters("gladiator", 1, 1,
                              {{"Alpha", 3, true, 0}, {"Beta", 2, true, 0}});

    LineupClassicViewerState state;
    SDL_Thread* thread = SDL_CreateThread(lineup_classic_viewer_injector,
                                          "lineup_classic_view", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.viewer_opened) << "VIEW LEVEL should open";
    EXPECT_TRUE(state.company_line_seen)
        << "the classic census labels the company COMPANY with its count";
    EXPECT_TRUE(state.troops_line_seen)
        << "the classic census labels the authored enemies MAP TROOPS";
    EXPECT_TRUE(state.page_opened) << "the LINEUP page should open";
    EXPECT_TRUE(state.fill_green_strong);
    EXPECT_TRUE(state.map_units_green_off)
        << "GREEN's box is live on gladiator (12 authored units)";
    EXPECT_TRUE(state.viewer_fill_opened);
    EXPECT_FALSE(state.troops_line_after_trade)
        << "the box OFF retires GREEN's authored troops from the stage";
    // BOT SQUAD, not MATCHED BOTS: the row's noun tracks the MATCHED.SIZE
    // latch, which only the match-mode activation fold banks — MATCHED is
    // match-mode vocabulary (W6-A: announces and the matched latch stay
    // out of classic), so a classic solved squad is the full stock table
    // wearing its fill word, and the plan/facts still bank underneath.
    EXPECT_EQ("view_scenario line   GREEN TEAM  ACTIVE - BOT SQUAD (5) "
              "STRONG",
              state.green_line_after_trade)
        << "the trade fields a solved squad closing with its fill word "
           "(B7)";
    EXPECT_TRUE(state.fill_green_none);
    EXPECT_EQ(og::sim::kFillNone, save.fill[1]);
    EXPECT_TRUE(state.viewer_stripped_opened);
    EXPECT_TRUE(state.company_line_still_there)
        << "RED's company census is untouched by GREEN's knobs";
    EXPECT_FALSE(state.green_line_when_none)
        << "NONE with the box off leaves nothing on GREEN: no census line, "
           "no roster rows (C4: fewer enemies, never a refusal)";
    EXPECT_EQ(2, state.captures) << "both viewer captures should land";
}

// ---------------------------------------------------------------------------
// Direct dispatch coverage (no injector): the knob callbacks' gate
// branches and the SPLIT actions' EVEN / UNITE arms.

TEST(LineupUi, knob_callbacks_gate_branches)
{
    trace_clear();
    SavedPickerSave save_guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    save.current_campaign = "modes";
    save.fill = {};
    save.map_units = {};

    // The MAP UNITS belt reads the loaded picker world's census. Whatever
    // level a previous test left loaded, make the picture deterministic:
    // park every existing living outside the score range (the team-marker
    // parking pattern), give team 4 two authored livings of our own, and
    // restore both on every exit path (add_ob APPENDS, so popping past the
    // captured size removes exactly ours).
    const std::size_t original_ob_size = world.oblist.size();
    std::vector<std::pair<walker*, unsigned char>> parked_livings;
    for (const auto& ob : world.oblist)
    {
        if (ob != nullptr && !ob->dead() &&
            ob->query_order() == Order::Living)
        {
            parked_livings.emplace_back(ob.get(), ob->team_num());
            ob->set_team_num(SCORE_TEAM_COUNT);
        }
    }
    struct WorldUnitsGuard
    {
        GameWorld& world;
        std::size_t size;
        std::vector<std::pair<walker*, unsigned char>>& parked;
        ~WorldUnitsGuard()
        {
            while (world.oblist.size() > size)
                world.oblist.pop_back();
            for (const auto& [living, team] : parked)
                living->set_team_num(team);
        }
    } units_guard{world, original_ob_size, parked_livings};
    for (int i = 0; i < 2; ++i)
    {
        walker* unit = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, unit);
        unit->set_team_num(3);
    }
    EXPECT_EQ((std::array<int, 4>{0, 0, 0, 2}),
              picker_lineup_map_unit_counts())
        << "the census counts authored livings per team";

    // Out-of-range team: inert.
    EXPECT_EQ(MENU_OK, change_lineup_fill(-1));
    EXPECT_EQ(MENU_OK, change_lineup_map_units(4));

    {
        // Joiner: §2.7-style denial — popup, TRACE, no local cycle.
        FakeNetLobbyClient joiner;
        joiner.host_view = false;
        ActiveLobbyGuard guard(&joiner);
        EXPECT_EQ(MENU_OK, change_lineup_fill(0));
        EXPECT_EQ(MENU_OK, change_lineup_map_units(3));
        EXPECT_TRUE(trace_contains("lineup", "fill_denied"));
        EXPECT_TRUE(trace_contains("lineup", "map_units_denied"));
        EXPECT_EQ(0, static_cast<int>(save.fill[0]));
        EXPECT_EQ(0, static_cast<int>(save.map_units[3]));
    }

    // Classic campaign (C5): there is no classic belt any more — the
    // packs/core lineup stage applies the knobs on every campaign, so the
    // host's write goes through on gladiator exactly as on modes.
    save.current_campaign = "gladiator";
    EXPECT_EQ(MENU_OK, change_lineup_fill(0));
    EXPECT_EQ(og::sim::kFillStrong, save.fill[0])
        << "a classic campaign's FILL wheel turns (C5)";
    EXPECT_EQ(MENU_OK, change_lineup_map_units(3));
    EXPECT_EQ(og::sim::kMapUnitsOff, save.map_units[3])
        << "a classic campaign's MAP UNITS box flips (C5)";
    // Park both knobs back at their defaults so the versus wheel walk below
    // starts from FAIR / ON.
    save.fill[0] = og::sim::kFillFair;
    save.map_units[3] = og::sim::kMapUnitsOn;

    // Host + versus: the FILL wheel walks the DISPLAY order from FAIR —
    // STRONG, BRUTAL, NONE, WEAK, FAIR (B2) — and the save value rides
    // every step through the clamp. No value is ever refused (B8).
    save.current_campaign = "modes";
    trace_clear();
    const short wheel[] = {og::sim::kFillStrong, og::sim::kFillBrutal,
                           og::sim::kFillNone, og::sim::kFillWeak,
                           og::sim::kFillFair};
    for (const short expected : wheel)
    {
        EXPECT_EQ(MENU_OK, change_lineup_fill(0));
        EXPECT_EQ(expected, save.fill[0]);
    }
    EXPECT_FALSE(trace_contains("lineup", "toast"))
        << "B8: nothing on the wheel refuses";

    // The MAP UNITS box flips where the map ships units, and junk heals ON.
    EXPECT_EQ(MENU_OK, change_lineup_map_units(3));
    EXPECT_EQ(og::sim::kMapUnitsOff, save.map_units[3]);
    EXPECT_EQ(MENU_OK, change_lineup_map_units(3));
    EXPECT_EQ(og::sim::kMapUnitsOn, save.map_units[3]);
    // ...and the belt keeps a stale dispatch off a unit-less team's knob.
    save.map_units[0] = og::sim::kMapUnitsOn;
    EXPECT_EQ(MENU_OK, change_lineup_map_units(0));
    EXPECT_EQ(og::sim::kMapUnitsOn, save.map_units[0]);
    EXPECT_TRUE(trace_contains("lineup", "map_units_no_units team=0"))
        << "the belt names the refused team";

    // A toast with no LINEUP state installed still traces (never crashes).
    og::ui::lineup_show_toast("NOWHERE TO LAND");
    EXPECT_TRUE(trace_contains("lineup", "toast NOWHERE TO LAND"));

    // With the screen state installed the toast also stamps its 2.5 s
    // deadline (the lineup_now_ms path the draw's title-band check reads).
    og::ui::LineupScreenState toast_state;
    og::ui::install_lineup_state_for_screen(&toast_state);
    og::ui::lineup_show_toast("UNITE");
    EXPECT_EQ("UNITE", toast_state.toast);
    EXPECT_GT(toast_state.toast_until_ms, 0)
        << "an installed state gets a wall-clock deadline";
    og::ui::install_lineup_state_for_screen(nullptr);

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
// §4: the campaign price is memoized on the ROW handed to the hook, which is
// the hook's whole input — so a fighter that changed is a different key and
// gets a fresh answer, while an idle menu frame costs no Lua at all. The
// memo cannot see the HOOK change, so the page clears it wherever the
// registration could have moved: this pins both halves.
TEST(LineupUi, power_memo_is_keyed_on_the_fighter_and_cleared_with_the_page)
{
    trace_clear();
    SavedPickerSave save_guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.current_campaign = "gladiator";
    restore_gladiator_mount();

    guy fighter(FAMILY_SOLDIER);
    fighter.upgrade_to_level(3, true);

    {
        SyntheticZoneScriptGuard hook(R"LUA(
og.register_campaign_hooks({
  lineup = { power = function(row) return row.level * 10 end },
})
)LUA");
        og::ui::lineup_power_cache_clear();
        ASSERT_TRUE(og::script::hooks::campaign_lineup_registered());
        const std::optional<long long> first =
            og::ui::lineup_power_for_guy(fighter);
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(30, *first);
        // The memoized answer for the SAME row.
        EXPECT_EQ(first, og::ui::lineup_power_for_guy(fighter));
        // A changed fighter is a different key, so it is priced afresh — a
        // memo that keyed on anything coarser would hand back 30 here.
        fighter.upgrade_to_level(5, true);
        const std::optional<long long> second =
            og::ui::lineup_power_for_guy(fighter);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(50, *second);
    }

    // A different registration with the same row: the page's clear is what
    // keeps the old campaign's price from surviving into the new one.
    {
        SyntheticZoneScriptGuard hook(R"LUA(
og.register_campaign_hooks({
  lineup = { power = function(row) return row.level * 100 end },
})
)LUA");
        og::ui::lineup_power_cache_clear();
        const std::optional<long long> repriced =
            og::ui::lineup_power_for_guy(fighter);
        ASSERT_TRUE(repriced.has_value());
        EXPECT_EQ(500, *repriced);
    }

    og::ui::lineup_power_cache_clear();
    restore_gladiator_mount();
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

// ---------------------------------------------------------------------------
// A FILL cycle repaints the knob it changed and nothing else. The page's
// live vbuttons carry the dim the gate pass published (three of the four
// MAP UNITS boxes are Disabled on scen 501 — only RED ships map units), and
// re-creating the whole live array on the dispatch dropped it for the frame
// that click composed: every dimmed knob on the page flashed bright for one
// presented frame. init_buttons is the teardown, so its own TRACE is the
// pin.
namespace {

struct LineupBlinkState
{
    bool page_opened = false;
    bool cycled = false;
    bool rebuilt_buttons = true;
};

int lineup_no_rebuild_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<LineupBlinkState*>(data);
    if (!injector_open_lineup())
        return 0;
    interact("lineup");
    state->page_opened = wait_for_interactable_at("back", 8, 176, 10000);
    if (state->page_opened) {
        SDL_Delay(750);
        SaveData& save = og::runtime::current_session->myscreen_->save_data;
        // One cycle of RED's wheel, retried on a swallowed click (the
        // ledger is cleared for each attempt so a retry cannot smuggle a
        // rebuild past the assertion).
        for (int attempt = 0; attempt < 3 && !state->cycled; ++attempt) {
            const short before = save.fill[0];
            trace_clear();
            interact("lineup_fill_0");
            for (int waited = 0; waited < 2500; waited += 50) {
                if (save.fill[0] != before) {
                    state->cycled = true;
                    break;
                }
                SDL_Delay(50);
            }
        }
        SDL_Delay(300);
        state->rebuilt_buttons = trace_contains("menu", "init_buttons");
        interact("back");
        SDL_Delay(300);
    }
    injector_unwind_from_scenario();
    return 0;
}

} // namespace

TEST(LineupUi, fill_cycle_never_rebuilds_the_live_buttons)
{
    trace_clear();
    SavedPickerSave save_guard;
    write_save0_with_fighters("modes", 501, 2,
                              {{"Alpha", 3, true, 1}, {"Beta", 2, true, 1}});

    LineupBlinkState state;
    SDL_Thread* thread = SDL_CreateThread(lineup_no_rebuild_injector,
                                          "lineup_blink", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    restore_gladiator_mount();

    EXPECT_TRUE(state.page_opened) << "the LINEUP page should open";
    EXPECT_TRUE(state.cycled) << "the FILL knob should have stepped";
    EXPECT_FALSE(state.rebuilt_buttons)
        << "cycling FILL re-created every live vbutton: the other bands' "
           "knobs lose the dim the gate pass published and flash bright "
           "for the frame the click composes";
}

