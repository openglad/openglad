#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/interface/button.h>
#include "../../src/interface/ui/picker_sdl_defs.h"
#include <openglad/core/test_trace.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/filesystem.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Forward declaration from platform_io.cpp
bool apply_sprite_sheet_setting();

// Forward declarations from picker.cpp
std::string get_class_description(unsigned char family);
const char* family_name_copy(short family);
const char* get_family_string(Sint32 family);
const char* get_training_cost_rating(unsigned char family, int stat);
Sint32 how_many(Sint32 whatfamily);
int get_scen_num_from_filename(const char* name);
Sint32 set_difficulty();
Sint32 set_player_mode(Sint32 howmany);
Sint32 change_teamnum(Sint32 arg);
Sint32 change_hire_teamnum(Sint32 arg);
Sint32 change_allied();
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
Sint32 add_guy(guy* newguy);
std::string get_saved_name(const char* filename);
Sint32 delete_all();
void quit(Sint32 arg1);
Sint32 return_menu(Sint32 arg);
Sint32 name_guy(Sint32 arg);
Sint32 edit_guy(Sint32 arg1);
Sint32 create_train_menu(Sint32 arg1);
Sint32 do_pick_spritesheet(Sint32 arg);
void picker_prepare_async_team_build_start_request();
void picker_lobby_initialize_from_save();
void picker_lobby_sync_from_save();
void picker_reinitialize_lobby_after_game();
void picker_lobby_shutdown();
bool picker_lobby_request_start();
bool picker_lobby_start_request_pending();
bool picker_replace_lobby_client(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client,
    std::unique_ptr<og::ui::IPickerLobbyClient> next_client,
    const char* popup_title,
    bool show_success_popup = true,
    // LINEUP §6: false for callers that already tore the previous session
    // down (DISCONNECT, the kicked revert) — restoring it would reconnect.
    bool restore_previous_on_failure = true);
bool picker_try_intercept_button_action(
    Sint32 whatfunc, Sint32 call_arg, Sint32& retvalue);
bool picker_join_game(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client);
std::unique_ptr<og::ui::IPickerClient> picker_testing_create_sdl_client();
int picker_testing_exercise_sdl_client_internal_paths();
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
int picker_testing_yes_or_no_queue_remaining();
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);
std::vector<std::string>& level_editor_testing_prompt_queue_ref();
extern bool g_start_game_requested;
#ifdef TESTING
extern bool g_test_remove_exits;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
namespace og::sim { extern std::int32_t g_test_level_tick_limit_override; }
#endif

static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

// myscreen is now a macro defined in base.h (via game_session.h)

namespace {

class ContractPickerLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override
    {
        ++request_start_calls;
        return request_start_result;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        std::optional<og::ui::PickerLobbyGameStartConfig> config =
            std::move(pending_config);
        pending_config.reset();
        ++consume_calls;
        return config;
    }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool has_game_start_config() const noexcept override
    {
        return pending_config.has_value();
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return host_controls_visible_result;
    }
    [[nodiscard]] og::sim::StartDenialReason last_start_denial()
        const noexcept override
    {
        return denial_result;
    }
    [[nodiscard]] std::vector<og::sim::LobbyPlayer>
    lobby_players() const override
    {
        return players;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return networked_result;
    }
    [[nodiscard]] bool is_save_slot_editable(
        std::size_t slot_index) const noexcept override
    {
        if (!restrict_editable_slots)
            return true;
        return slot_index < editable_slots.size() && editable_slots[slot_index];
    }

    std::optional<og::ui::PickerLobbyGameStartConfig> pending_config;
    int consume_calls = 0;
    int request_start_calls = 0;
    bool request_start_result = false;
    bool host_controls_visible_result = true;
    bool networked_result = false;
    og::sim::StartDenialReason denial_result =
        og::sim::StartDenialReason::None;
    std::vector<og::sim::LobbyPlayer> players;
    bool restrict_editable_slots = false;
    std::array<bool, MAX_TEAM_SIZE> editable_slots{};

    // LINEUP §6 seam (kick / disconnect / kicked receipt).
    bool kick_machine(og::sim::LobbyMachineId machine_id) override
    {
        kicked_machines.push_back(machine_id);
        return kick_result;
    }
    bool disconnect_session() override
    {
        ++disconnect_calls;
        return disconnect_result;
    }
    [[nodiscard]] bool was_kicked() const noexcept override
    {
        return was_kicked_result;
    }

    std::vector<og::sim::LobbyMachineId> kicked_machines;
    bool kick_result = true;
    int disconnect_calls = 0;
    bool disconnect_result = true;
    bool was_kicked_result = false;
};

// A client that overrides NOTHING past the pure virtuals, so the
// IPickerLobbyClient defaults for the LINEUP §6 seam are exercised as the
// contract every non-networked client inherits.
class DefaultSeamPickerLobbyClient final : public og::ui::IPickerLobbyClient
{
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
};

struct ActivePickerLobbyClientGuard
{
    og::ui::IPickerLobbyClient* saved = nullptr;

    explicit ActivePickerLobbyClientGuard(og::ui::IPickerLobbyClient* client)
        : saved(og::ui::active_picker_lobby_client())
    {
        og::ui::install_active_picker_lobby_client(client);
    }

    ~ActivePickerLobbyClientGuard()
    {
        og::ui::install_active_picker_lobby_client(saved);
    }
};

struct PlatformBridgeGuard
{
    PlatformBridge saved = platform_bridge();

    ~PlatformBridgeGuard()
    {
        set_platform_bridge(std::move(saved));
    }
};

void inject_mouse_click(int x, int y, int delay_ms = 50)
{
    // UI-canvas-pinned map — raw viewport math mismaps in non-16:10
    // windows (see test_interact.h).
    const auto [mapped_x, mapped_y] = ui_canvas_to_window(
        static_cast<float>(x), static_cast<float>(y));
    const int sx = static_cast<int>(mapped_x);
    const int sy = static_cast<int>(mapped_y);

    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    event.button.clicks = 1;
    event.button.x = static_cast<float>(sx);
    event.button.y = static_cast<float>(sy);
    SDL_PushEvent(&event);

    SDL_Delay(static_cast<Uint32>(delay_ms));

    std::memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = false;
    event.button.clicks = 1;
    event.button.x = static_cast<float>(sx);
    event.button.y = static_cast<float>(sy);
    SDL_PushEvent(&event);
}

void prepare_picker_mouse()
{
    clear_events();
    auto& input_hw = input_hardware_state();
    input_hw.mouse = {};
    input_hw.picker_was_left_down = false;
    input_hw.picker_was_right_down = false;
}

struct PickerLobbyClientTrace
{
    int initialize_calls = 0;
    int shutdown_calls = 0;
    int sync_from_save_calls = 0;
    int sync_roster_calls = 0;
    int sync_settings_calls = 0;
    int poll_calls = 0;
    int set_player_mode_calls = 0;
    int last_player_mode = -1;
};

struct ExclusiveLobbyBinding
{
    bool in_use = false;
};

struct ExclusiveResourcePickerLobbyStats
{
    int initialize_calls = 0;
    int shutdown_calls = 0;
};

class ScopedEnvVar
{
public:
    explicit ScopedEnvVar(const char* name)
        : name_(name)
    {
        const char* const current = std::getenv(name_);
        if (current != nullptr)
        {
            had_value_ = true;
            old_value_ = current;
        }
    }

    ~ScopedEnvVar()
    {
        if (had_value_)
        {
#ifdef _WIN32
            _putenv_s(name_, old_value_.c_str());
#else
            setenv(name_, old_value_.c_str(), 1);
#endif
        }
        else
        {
#ifdef _WIN32
            _putenv_s(name_, "");
#else
            unsetenv(name_);
#endif
        }
    }

private:
    const char* name_;
    bool had_value_ = false;
    std::string old_value_;
};

class ScopedTraceBuffer
{
public:
    ScopedTraceBuffer() { trace_clear(); }
    ~ScopedTraceBuffer() { trace_clear(); }

    ScopedTraceBuffer(const ScopedTraceBuffer&) = delete;
    ScopedTraceBuffer& operator=(const ScopedTraceBuffer&) = delete;
};

class TraceablePickerLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    explicit TraceablePickerLobbyClient(std::shared_ptr<PickerLobbyClientTrace> trace)
        : trace_(std::move(trace))
    {
    }

    void initialize_from_save() override
    {
        ++trace_->initialize_calls;
        if (throw_on_initialize)
            throw std::runtime_error("simulated init failure");
    }

    void shutdown() override
    {
        ++trace_->shutdown_calls;
    }

    void sync_from_save() override
    {
        ++trace_->sync_from_save_calls;
    }
    void sync_roster_from_save() override
    {
        ++trace_->sync_roster_calls;
    }
    void sync_settings_from_save() override
    {
        ++trace_->sync_settings_calls;
    }
    void poll_and_apply() override
    {
        ++trace_->poll_calls;
    }
    void set_player_mode(int player_count) override
    {
        ++trace_->set_player_mode_calls;
        trace_->last_player_mode = player_count;
    }
    bool request_start_game() override
    {
        return false;
    }

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

    [[nodiscard]] std::vector<std::string> status_lines() const override
    {
        return status_lines_;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return networked_;
    }

    std::shared_ptr<PickerLobbyClientTrace> trace_;
    std::vector<std::string> status_lines_;
    bool throw_on_initialize = false;
    bool networked_ = false;
};

class ExclusiveResourcePickerLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    ExclusiveResourcePickerLobbyClient(
        ExclusiveLobbyBinding& binding,
        std::shared_ptr<ExclusiveResourcePickerLobbyStats> stats)
        : binding_(binding)
        , stats_(std::move(stats))
    {
    }

    void initialize_from_save() override
    {
        ++stats_->initialize_calls;
        if (binding_.in_use)
            throw std::runtime_error("resource already in use");
        binding_.in_use = true;
        owns_binding_ = true;
    }

    void shutdown() override
    {
        ++stats_->shutdown_calls;
        if (owns_binding_)
        {
            binding_.in_use = false;
            owns_binding_ = false;
        }
    }

    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override
    {
        return false;
    }

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

    ExclusiveLobbyBinding& binding_;
    std::shared_ptr<ExclusiveResourcePickerLobbyStats> stats_;
    bool owns_binding_ = false;
};

} // namespace

#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>

// Button stat constants from picker.cpp
#define BUT_STR 0
#define BUT_DEX 1
#define BUT_CON 2
#define BUT_INT 3
#define BUT_ARMOR 4
#define BUT_LEVEL 5

// ---------------------------------------------------------------------------
// get_class_description tests
// ---------------------------------------------------------------------------

TEST(PickerFuncs, get_class_description_soldier)
{
    std::string desc = get_class_description(FAMILY_SOLDIER);
    ASSERT_TRUE(!desc.empty()) << "soldier description should not be empty";
    ASSERT_TRUE(desc.find("Soldier") != std::string::npos || desc.find("soldier") != std::string::npos || desc.find("fighter") != std::string::npos || desc.size() > 10) << "soldier description should contain useful text";
}


TEST(PickerFuncs, get_class_description_mage)
{
    std::string desc = get_class_description(FAMILY_MAGE);
    ASSERT_TRUE(!desc.empty()) << "mage description should not be empty";
}


TEST(PickerFuncs, get_class_description_all_families)
{
    unsigned char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN,
                        FAMILY_ARCHMAGE, FAMILY_BIG_ORC };
    for (int i = 0; i < 16; i++) {
        std::string desc = get_class_description(families[i]);
        ASSERT_TRUE(!desc.empty()) << "every family should have a description";
    }
}


// ---------------------------------------------------------------------------
// family_name_copy tests
// ---------------------------------------------------------------------------

TEST(PickerFuncs, family_name_copy_soldier)
{
    const char* name = family_name_copy(FAMILY_SOLDIER);
    ASSERT_TRUE(name != nullptr) << "soldier name should not be null";
    ASSERT_TRUE(strlen(name) > 0) << "soldier name should not be empty";
}


TEST(PickerFuncs, family_name_copy_all_families)
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        const char* name = family_name_copy(families[i]);
        ASSERT_TRUE(name != nullptr) << "family name should not be null";
        ASSERT_TRUE(strlen(name) > 0) << "family name should not be empty";
    }

    ASSERT_TRUE(std::string(family_name_copy(FAMILY_BIG_ORC)) == "ORC CAP.") << "big orc label from registry";
    ASSERT_TRUE(std::string(get_family_string(FAMILY_BIG_ORC)) == "ORC CAPTAIN") << "big orc full label";
    ASSERT_TRUE(std::string(get_family_string(255)) == "BEAST") << "unknown family full label fallback";
}


// ---------------------------------------------------------------------------
// get_training_cost_rating tests
// ---------------------------------------------------------------------------

TEST(PickerFuncs, get_training_cost_rating_returns_stars)
{
    const char* rating = get_training_cost_rating(FAMILY_SOLDIER, BUT_STR);
    ASSERT_TRUE(rating != nullptr) << "rating should not be null";
    // Rating is 0-5 asterisks
    size_t len = strlen(rating);
    ASSERT_TRUE(len <= 5) << "rating should be at most 5 characters";
    for (size_t i = 0; i < len; i++) {
        ASSERT_TRUE(rating[i] == '*') << "rating should only contain asterisks";
    }
}


TEST(PickerFuncs, get_training_cost_rating_varies_by_stat)
{
    // Soldier STR cost is 6 (cheap), INT cost is 25 (expensive)
    const char* str_rating = get_training_cost_rating(FAMILY_SOLDIER, BUT_STR);
    const char* int_rating = get_training_cost_rating(FAMILY_SOLDIER, BUT_INT);
    // Cheaper stats should have more stars
    ASSERT_TRUE(strlen(str_rating) >= strlen(int_rating)) << "cheaper stat should have >= stars";
}


TEST(PickerFuncs, get_training_cost_rating_all_families)
{
    for (int fam = 0; fam <= FAMILY_ARCHMAGE; fam++) {
        for (int stat = 0; stat < 5; stat++) {
            const char* rating = get_training_cost_rating(static_cast<unsigned char>(fam), stat);
            ASSERT_TRUE(rating != nullptr) << "rating should not be null";
            ASSERT_TRUE(strlen(rating) <= 5) << "rating should be at most 5 chars";
        }
    }
}

TEST(PickerFuncs, player_control_summary_rejects_bad_slots_and_clips_long_key_names)
{
    EXPECT_EQ("--", player_control_key_display_name(-1, KEY_UP));
    EXPECT_EQ("--", player_control_key_display_name(0, NUM_KEYS));
    EXPECT_EQ((std::array<std::string, 2>{"", ""}),
              build_player_control_summary_lines(-1, false));
    EXPECT_EQ((std::array<std::string, 2>{"", ""}),
              build_player_control_summary_lines(4, true));

    int& key = og::runtime::current_session->player_keys_[0][KEY_FIRE];
    const int saved_key = key;
    struct KeyRestore
    {
        int& target;
        int saved;
        ~KeyRestore() { target = saved; }
    } restore{key, saved_key};

    key = SDLK_PRINTSCREEN;
    EXPECT_EQ("PrintScre", player_control_key_display_name(0, KEY_FIRE))
        << "the 11-character SDL key name must use the nine-character face";
}


// ---------------------------------------------------------------------------
// get_random_name tests
// ---------------------------------------------------------------------------

TEST(PickerFuncs, get_random_name_returns_nonempty)
{
    srand(42);
    const char* name = og::ui::get_random_name(FAMILY_SOLDIER);
    ASSERT_TRUE(name != nullptr) << "random name should not be null";
    ASSERT_TRUE(strlen(name) > 0) << "random name should not be empty";
}


TEST(PickerFuncs, get_random_name_all_families)
{
    srand(42);
    unsigned char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        const char* name = og::ui::get_random_name(families[i]);
        ASSERT_TRUE(name != nullptr) << "random name should not be null for any family";
        ASSERT_TRUE(strlen(name) > 0) << "random name should not be empty for any family";
    }

    std::string unique = og::ui::get_unique_name(FAMILY_SOLDIER, og::runtime::current_session->myscreen_->save_data);
    ASSERT_TRUE(!unique.empty()) << "unique name should not be empty";
}


// ---------------------------------------------------------------------------
// has_name_in_team tests
// ---------------------------------------------------------------------------

TEST(PickerFuncs, has_name_in_team_empty)
{
    // Save and clear team
    const unsigned char orig_size = og::runtime::current_session->myscreen_->save_data.team_size;
    og::runtime::current_session->myscreen_->save_data.team_size = static_cast<unsigned char>(0);

    // Use get_unique_name to verify name dedup works on empty team
    std::string name1 = og::ui::get_unique_name(FAMILY_SOLDIER, og::runtime::current_session->myscreen_->save_data);
    ASSERT_TRUE(!name1.empty()) << "unique name should not be empty on empty team";

    guy* g = new guy(FAMILY_SOLDIER);
    g->name = "TestName";
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(g);
    og::runtime::current_session->myscreen_->save_data.team_size = static_cast<unsigned char>(1);

    // get_unique_name should return a name different from existing "TestName"
    // (though it may not collide anyway since random names vary)
    std::string name2 = og::ui::get_unique_name(FAMILY_SOLDIER, og::runtime::current_session->myscreen_->save_data);
    ASSERT_TRUE(!name2.empty()) << "unique name should not be empty";

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(nullptr);
    og::runtime::current_session->myscreen_->save_data.team_size = orig_size;
}


// ---------------------------------------------------------------------------
// how_many tests
// ---------------------------------------------------------------------------

TEST(PickerFuncs, how_many_empty_team)
{
    const unsigned char orig_size = og::runtime::current_session->myscreen_->save_data.team_size;
    guy* orig_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        orig_list[i] = og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].release();
        og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].reset(nullptr);
    }

    Sint32 count = how_many(FAMILY_SOLDIER);
    ASSERT_EQ(0, (int)count) << "empty team should have 0 of any family";

    // Restore
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].reset(orig_list[i]);
    }
    og::runtime::current_session->myscreen_->save_data.team_size = orig_size;
}


TEST(PickerFuncs, how_many_with_team)
{
    // Save originals
    const unsigned char orig_size = og::runtime::current_session->myscreen_->save_data.team_size;
    guy* orig_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        orig_list[i] = og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].release();
        og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].reset(nullptr);
    }

    // Add some guys
    guy* g1 = new guy(FAMILY_SOLDIER);
    guy* g2 = new guy(FAMILY_SOLDIER);
    guy* g3 = new guy(FAMILY_MAGE);
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(g1);
    og::runtime::current_session->myscreen_->save_data.team_list[1].reset(g2);
    og::runtime::current_session->myscreen_->save_data.team_list[2].reset(g3);
    og::runtime::current_session->myscreen_->save_data.team_size = static_cast<unsigned char>(3);

    ASSERT_EQ(2, (int)how_many(FAMILY_SOLDIER)) << "should count 2 soldiers";
    ASSERT_EQ(1, (int)how_many(FAMILY_MAGE)) << "should count 1 mage";
    ASSERT_EQ(0, (int)how_many(FAMILY_ARCHER)) << "should count 0 archers";

    // Cleanup
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].reset(orig_list[i]);
    }
    og::runtime::current_session->myscreen_->save_data.team_size = orig_size;

    // Additional picker utility/state coverage without registering new tests.
    ASSERT_EQ(-1, get_scen_num_from_filename(nullptr)) << "null input should return -1";
    ASSERT_EQ(-1, get_scen_num_from_filename("scen")) << "no numeric suffix should return -1";
    ASSERT_EQ(123, get_scen_num_from_filename("scen123")) << "numeric suffix should parse";
    ASSERT_EQ(42, get_scen_num_from_filename("file42")) << "mixed prefix should parse trailing number";

    vbutton* old1 = og::runtime::current_session->allbuttons_[1];
    vbutton* old2 = og::runtime::current_session->allbuttons_[2];
    vbutton* old6 = og::runtime::current_session->allbuttons_[6];
    vbutton* old7 = og::runtime::current_session->allbuttons_[7];
    vbutton* old18 = og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex];
    std::unique_ptr<guy> old_current = std::move(og::runtime::current_session->current_guy_);
    short old_team_num = og::runtime::current_session->current_team_num_;
    Sint32 old_diff = og::runtime::current_session->current_difficulty_;
    const short old_allied = og::runtime::current_session->myscreen_->save_data.allied_mode;

    og::runtime::current_session->allbuttons_[1] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b1", KEYSTATE_UNKNOWN);
    og::runtime::current_session->allbuttons_[2] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b2", KEYSTATE_UNKNOWN);
    og::runtime::current_session->allbuttons_[6] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b6", KEYSTATE_UNKNOWN);
    og::runtime::current_session->allbuttons_[7] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b7", KEYSTATE_UNKNOWN);
    og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b18", KEYSTATE_UNKNOWN);

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->current_guy_->teamnum = 1;
    og::runtime::current_session->current_team_num_ = 0;
    og::runtime::current_session->current_difficulty_ = DIFFICULTY_SETTINGS - 1;
    og::runtime::current_session->myscreen_->save_data.allied_mode = static_cast<short>(0);
    vbutton dispatcher;
    dispatcher.arg = 1;

    ASSERT_EQ(4, (int)set_difficulty()) << "set_difficulty should return OK";
    // set_difficulty writes the DIFFICULTY subscreen's cycling row (index 1),
    // not the main-menu door (which keeps its static label).
    ASSERT_TRUE(std::string(og::runtime::current_session->allbuttons_[1]->label).find("Difficulty: ") == 0)
        << "difficulty label should be updated";

    ASSERT_EQ(4, (int)dispatcher.do_call(
        button_action_id(ButtonAction::ChangeTeam), 1))
        << "the button dispatcher should route team changes";
    ASSERT_EQ(2, (int)og::runtime::current_session->current_guy_->teamnum) << "team should increment";
    ASSERT_TRUE(std::string(og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]->label).find("Team ") == 0) << "team label should be updated";

    og::runtime::current_session->current_team_num_ = 0;
    ASSERT_EQ(4, (int)dispatcher.do_call(
        button_action_id(ButtonAction::ChangeHireTeam), 1))
        << "the button dispatcher should route hire-team changes";
    ASSERT_EQ(1, (int)og::runtime::current_session->current_team_num_) << "hire team num should increment";
    ASSERT_EQ(1, (int)og::runtime::current_session->current_guy_->teamnum) << "current guy team should mirror hire team";
    ASSERT_TRUE(std::string(og::runtime::current_session->allbuttons_[2]->label).find("Hiring for Team ") == 0) << "hire label should be updated";

    // change_allied remains a save/replay compatibility hook after the visible
    // seat-mode row moved out; it no longer writes either button surface.
    ASSERT_EQ(4, (int)dispatcher.do_call(
        button_action_id(ButtonAction::AlliedMode), 0))
        << "the button dispatcher should route the compatibility toggle";
    ASSERT_EQ(1, og::runtime::current_session->myscreen_->save_data.allied_mode) << "allied mode should toggle on";
    ASSERT_EQ(4, (int)dispatcher.do_call(
        button_action_id(ButtonAction::AlliedMode), 0))
        << "a second dispatched toggle should restore the original mode";
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.allied_mode) << "allied mode should toggle off";

    // Directly exercise picker helpers that were still uncovered.
    const unsigned char saved_team_size = og::runtime::current_session->myscreen_->save_data.team_size;
    guy* saved_team_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        saved_team_list[i] = og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].release();
        og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].reset(nullptr);
    }
    og::runtime::current_session->myscreen_->save_data.team_size = static_cast<unsigned char>(0);

    guy* recruited = new guy(FAMILY_SOLDIER);
    Sint32 slot = add_guy(recruited);
    ASSERT_TRUE(slot >= 0) << "add_guy(guy*) should place recruit in a slot";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_size == static_cast<unsigned char>(1)) << "team size should increment after add_guy(guy*)";

    ASSERT_EQ(1, (int)delete_all()) << "delete_all should report number of removed members";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_size == static_cast<unsigned char>(0)) << "delete_all should clear team size";

    vbutton* old0 = og::runtime::current_session->allbuttons_[0];
    if (og::runtime::current_session->allbuttons_[0] == nullptr) {
        og::runtime::current_session->allbuttons_[0] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b0", KEYSTATE_UNKNOWN);
    }
    og::runtime::current_session->allbuttons_[0]->label = "UNIT_TEST_SAVE";
    // (do_save/do_load retired with the slot menus — §3.8: saving is
    // automatic; loading is the Company List.)

    ASSERT_EQ(1234, (int)return_menu(1234)) << "return_menu should echo its argument";
    quit(0); // test mode: should not exit
    std::unique_ptr<guy> tmp_current = std::move(og::runtime::current_session->current_guy_);
    og::runtime::current_session->current_guy_ = nullptr;
    ASSERT_EQ(2, (int)name_guy(0)) << "name_guy with no current_guy should return REDRAW";
    ASSERT_EQ(-1, (int)edit_guy(0)) << "edit_guy with no current_guy should fail";
    og::runtime::current_session->current_guy_ = std::move(tmp_current);


    og::runtime::current_session->current_guy_ = std::move(old_current);
    og::runtime::current_session->current_team_num_ = old_team_num;
    og::runtime::current_session->current_difficulty_ = old_diff;
    og::runtime::current_session->myscreen_->save_data.allied_mode = old_allied;

    delete og::runtime::current_session->allbuttons_[1];
    delete og::runtime::current_session->allbuttons_[2];
    delete og::runtime::current_session->allbuttons_[6];
    delete og::runtime::current_session->allbuttons_[7];
    delete og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex];
    if (old0 == nullptr) {
        delete og::runtime::current_session->allbuttons_[0];
    }
    og::runtime::current_session->allbuttons_[0] = old0;
    og::runtime::current_session->allbuttons_[1] = old1;
    og::runtime::current_session->allbuttons_[2] = old2;
    og::runtime::current_session->allbuttons_[6] = old6;
    og::runtime::current_session->allbuttons_[7] = old7;
    og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex] = old18;

    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].reset(saved_team_list[i]);
    }
    og::runtime::current_session->myscreen_->save_data.team_size = saved_team_size;
}

TEST(PickerFuncs, picker_lobby_consume_game_start_config_uses_active_client_boundary)
{
    picker_lobby_shutdown();

    ContractPickerLobbyClient client;
    client.pending_config = og::ui::PickerLobbyGameStartConfig{
        .save_data =
            og::sim::LobbySaveDataEquivalent{
                .current_campaign = "gladiator",
                .scen_num = 1,
                .numplayers = 2,
                .allied_mode = 0,
                .team_list = {},
            },
        .difficulty = 4,
    };

    {
        ActivePickerLobbyClientGuard guard(&client);
        const std::optional<og::ui::PickerLobbyGameStartConfig> config =
            picker_lobby_consume_game_start_config();
        ASSERT_TRUE(config.has_value());
        EXPECT_EQ(2u, config->save_data.numplayers);
        EXPECT_EQ(4, config->difficulty);
        EXPECT_EQ(1, client.consume_calls);
        EXPECT_FALSE(picker_lobby_consume_game_start_config().has_value());
        EXPECT_EQ(2, client.consume_calls);
    }

    picker_lobby_shutdown();
}

TEST(PickerFuncs, picker_lobby_host_controls_visible_follows_active_client_boundary)
{
    picker_lobby_shutdown();
    EXPECT_TRUE(picker_lobby_host_controls_visible());

    ContractPickerLobbyClient client;
    client.host_controls_visible_result = false;
    {
        ActivePickerLobbyClientGuard guard(&client);
        EXPECT_FALSE(picker_lobby_host_controls_visible());
    }

    EXPECT_TRUE(picker_lobby_host_controls_visible());
    picker_lobby_shutdown();
}

// LINEUP §6: the three free functions are the UI's only handle on the kick /
// disconnect seam, and each must forward to the ACTIVE client and answer
// false with no client installed (the local/offline case WP-F swaps in).
TEST(PickerFuncs, picker_lobby_kick_and_disconnect_forward_to_the_active_client)
{
    picker_lobby_shutdown();
    EXPECT_FALSE(picker_lobby_kick_machine(7u))
        << "no client installed: nothing to kick";
    EXPECT_FALSE(picker_lobby_disconnect_session());
    EXPECT_FALSE(picker_lobby_was_kicked());

    ContractPickerLobbyClient client;
    client.was_kicked_result = true;
    {
        ActivePickerLobbyClientGuard guard(&client);
        EXPECT_TRUE(picker_lobby_kick_machine(0x1234u));
        ASSERT_EQ(1u, client.kicked_machines.size());
        EXPECT_EQ(0x1234u, client.kicked_machines.front())
            << "the machine id must reach the client unchanged";
        EXPECT_TRUE(picker_lobby_disconnect_session());
        EXPECT_EQ(1, client.disconnect_calls);
        EXPECT_TRUE(picker_lobby_was_kicked());

        // A refusal propagates as a refusal, not as a silent success.
        client.kick_result = false;
        client.disconnect_result = false;
        EXPECT_FALSE(picker_lobby_kick_machine(0x99u));
        EXPECT_FALSE(picker_lobby_disconnect_session());
        EXPECT_EQ(2u, client.kicked_machines.size());
        EXPECT_EQ(2, client.disconnect_calls);
    }

    // The base-class defaults: a client with no networking behind it refuses
    // both requests and has never been kicked.
    DefaultSeamPickerLobbyClient plain;
    {
        ActivePickerLobbyClientGuard guard(&plain);
        EXPECT_FALSE(picker_lobby_kick_machine(1u));
        EXPECT_FALSE(picker_lobby_disconnect_session());
        EXPECT_FALSE(picker_lobby_was_kicked());
    }

    picker_lobby_shutdown();
    EXPECT_FALSE(picker_lobby_kick_machine(7u));
    EXPECT_FALSE(picker_lobby_disconnect_session());
    EXPECT_FALSE(picker_lobby_was_kicked());
}

TEST(PickerFuncs, picker_replace_lobby_client_is_transactional_on_initialize_failure)
{
    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(
        current_trace);
    auto* const current_raw =
        static_cast<TraceablePickerLobbyClient*>(current_client.get());
    current_raw->initialize_from_save();
    ActivePickerLobbyClientGuard guard(current_raw);

    auto next_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> next_client =
        std::make_unique<TraceablePickerLobbyClient>(
        next_trace);
    static_cast<TraceablePickerLobbyClient*>(next_client.get())
        ->throw_on_initialize = true;

    EXPECT_THROW(
        picker_replace_lobby_client(current_client,
                                    std::move(next_client),
                                    "HOST GAME"),
        std::runtime_error);
    EXPECT_EQ(current_raw, current_client.get());
    EXPECT_EQ(current_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(2, current_trace->initialize_calls);
    EXPECT_EQ(1, current_trace->shutdown_calls);
    EXPECT_EQ(1, next_trace->initialize_calls);
}

TEST(PickerFuncs, picker_replace_lobby_client_swaps_active_client_after_success)
{
    trace_clear();
    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(
        current_trace);
    ActivePickerLobbyClientGuard guard(current_client.get());

    auto next_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> next_client =
        std::make_unique<TraceablePickerLobbyClient>(
        next_trace);
    static_cast<TraceablePickerLobbyClient*>(next_client.get())->status_lines_ =
        {"", "Room: GLAD-XKCD", "Lobby: 2 players"};
    auto* const next_raw =
        static_cast<TraceablePickerLobbyClient*>(next_client.get());

    ASSERT_TRUE(picker_replace_lobby_client(current_client,
                                            std::move(next_client),
                                            "HOST GAME"));
    EXPECT_EQ(next_raw, current_client.get());
    EXPECT_EQ(next_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(1, current_trace->shutdown_calls);
    EXPECT_EQ(1, next_trace->initialize_calls);
    EXPECT_TRUE(trace_contains(
        "popup", "Room: GLAD-XKCD\nLobby: 2 players"))
        << "empty status rows must be skipped and visible rows joined once";
}

TEST(PickerFuncs, picker_replace_lobby_client_rejects_null_and_unpreserved_network_client)
{
    ScopedTraceBuffer trace_guard;
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client;
    EXPECT_FALSE(picker_replace_lobby_client(
        current_client, nullptr, "HOST GAME"));
    EXPECT_EQ(nullptr, current_client);

    auto next_trace = std::make_shared<PickerLobbyClientTrace>();
    auto next_client = std::make_unique<TraceablePickerLobbyClient>(next_trace);
    next_client->networked_ = true;

    screen* const saved_screen = og::runtime::current_session->myscreen_;
    struct ScreenRestore
    {
        screen*& target;
        screen* saved;
        ~ScreenRestore() { target = saved; }
    } restore{og::runtime::current_session->myscreen_, saved_screen};
    og::runtime::current_session->myscreen_ = nullptr;

    trace_clear();
    EXPECT_FALSE(picker_replace_lobby_client(
        current_client, std::move(next_client), "HOST GAME"));
    EXPECT_EQ(nullptr, current_client);
    EXPECT_EQ(0, next_trace->initialize_calls)
        << "network initialization must not begin without the baseline save";
    EXPECT_TRUE(trace_contains(
        "popup", "Could not preserve your local team save."));
}

TEST(PickerFuncs, picker_main_menu_help_intercept_selects_the_help_command)
{
    struct PickerInterceptRestore
    {
        int scope = pks().intercept_scope;
        const og::ui::PickerMenuItem* selected = pks().selected_menu_item;
        ~PickerInterceptRestore()
        {
            pks().intercept_scope = scope;
            pks().selected_menu_item = selected;
        }
    } restore;

    pks().intercept_scope = 1; // PickerInterceptScope::MainMenu
    pks().selected_menu_item = nullptr;
    Sint32 retvalue = 0;

    ASSERT_TRUE(picker_try_intercept_button_action(
        button_action_id(ButtonAction::ShowHelp), 0, retvalue));
    ASSERT_NE(nullptr, pks().selected_menu_item);
    EXPECT_EQ(og::ui::PickerMenuCommand::Help,
              pks().selected_menu_item->command);
    EXPECT_EQ(MENU_EXIT, retvalue);
}

TEST(PickerFuncs, picker_replace_lobby_client_can_skip_success_popup)
{
    trace_clear();
    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(
        current_trace);
    ActivePickerLobbyClientGuard guard(current_client.get());

    auto next_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> next_client =
        std::make_unique<TraceablePickerLobbyClient>(
        next_trace);
    static_cast<TraceablePickerLobbyClient*>(next_client.get())->status_lines_ =
        {"LAN: 192.168.1.5:12345", "Lobby: 1 player"};
    auto* const next_raw =
        static_cast<TraceablePickerLobbyClient*>(next_client.get());

    ASSERT_TRUE(picker_replace_lobby_client(current_client,
                                            std::move(next_client),
                                            "HOST GAME",
                                            false));
    EXPECT_EQ(next_raw, current_client.get());
    EXPECT_EQ(next_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(1, current_trace->shutdown_calls);
    EXPECT_EQ(1, next_trace->initialize_calls);
    EXPECT_EQ(0, trace_count("popup"));
}

TEST(PickerFuncs, picker_replace_lobby_client_releases_old_host_before_new_init)
{
    ExclusiveLobbyBinding binding;
    auto current_stats = std::make_shared<ExclusiveResourcePickerLobbyStats>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<ExclusiveResourcePickerLobbyClient>(
            binding,
            current_stats);
    auto* const current_raw =
        static_cast<ExclusiveResourcePickerLobbyClient*>(current_client.get());
    current_raw->initialize_from_save();
    ASSERT_TRUE(binding.in_use);
    ActivePickerLobbyClientGuard guard(current_raw);

    auto next_stats = std::make_shared<ExclusiveResourcePickerLobbyStats>();
    std::unique_ptr<og::ui::IPickerLobbyClient> next_client =
        std::make_unique<ExclusiveResourcePickerLobbyClient>(
            binding,
            next_stats);
    auto* const next_raw =
        static_cast<ExclusiveResourcePickerLobbyClient*>(next_client.get());

    ASSERT_TRUE(picker_replace_lobby_client(current_client,
                                            std::move(next_client),
                                            "HOST GAME"));
    EXPECT_EQ(next_raw, current_client.get());
    EXPECT_EQ(next_raw, og::ui::active_picker_lobby_client());
    EXPECT_TRUE(binding.in_use);
    EXPECT_EQ(1, current_stats->shutdown_calls);
    EXPECT_EQ(1, next_stats->initialize_calls);
}

TEST(PickerFuncs, picker_join_game_direct_prompt_catches_factory_error)
{
    trace_clear();
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    bridge.create_join_picker_lobby_client =
        [](const og::ui::PickerJoinGameOptions&)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        throw std::runtime_error("simulated join failure");
    };
    set_platform_bridge(std::move(bridge));

    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(current_trace);
    auto* const current_raw =
        static_cast<TraceablePickerLobbyClient*>(current_client.get());
    ActivePickerLobbyClientGuard guard(current_raw);

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("127.0.0.1:24567");

    EXPECT_FALSE(picker_join_game(current_client));
    EXPECT_EQ(current_raw, current_client.get());
    EXPECT_EQ(current_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(0, current_trace->shutdown_calls);
    EXPECT_TRUE(trace_contains("popup", "simulated join failure"));

    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();
}

TEST(PickerFuncs, relay_listing_fallbacks_are_immediate_and_non_throwing)
{
    PlatformBridgeGuard bridge_guard;

    set_platform_bridge({});
    EXPECT_THROW(
        og::ui::create_host_picker_lobby_client({}),
        std::runtime_error);
    EXPECT_THROW(
        og::ui::create_join_picker_lobby_client({}),
        std::runtime_error);
    EXPECT_TRUE(
        og::ui::list_relay_rooms("https://relay.invalid").empty());

    auto unavailable =
        og::ui::begin_list_relay_rooms("https://relay.invalid");
    ASSERT_NE(nullptr, unavailable);
    const auto unavailable_result = unavailable->poll();
    ASSERT_TRUE(unavailable_result.has_value());
    EXPECT_TRUE(unavailable_result->rooms.empty());
    EXPECT_TRUE(unavailable_result->error.empty());
    EXPECT_FALSE(unavailable->poll().has_value());

    std::string normalized_url;
    PlatformBridge synchronous;
    synchronous.begin_list_relay_rooms =
        [](const std::string&, const std::string&) {
            return std::unique_ptr<
                og::ui::IPickerRelayRoomListRequest>{};
        };
    synchronous.list_relay_rooms =
        [&](const std::string& base_url, const std::string&) {
            normalized_url = base_url;
            return std::vector<og::ui::PickerRelayRoomInfo>{
                {.code = "GLAD-FALLBACK",
                 .campaign_hash = {},
                 .campaign_name = {},
                 .host_name = {},
                 .player_count = 0,
                 .created_at_ms = 0}};
        };
    set_platform_bridge(std::move(synchronous));

    auto fallback = og::ui::begin_list_relay_rooms(
        " https://relay.invalid/// ", "campaign");
    ASSERT_NE(nullptr, fallback);
    const auto fallback_result = fallback->poll();
    ASSERT_TRUE(fallback_result.has_value());
    ASSERT_EQ(1u, fallback_result->rooms.size());
    EXPECT_EQ("GLAD-FALLBACK", fallback_result->rooms.front().code);
    EXPECT_EQ("https://relay.invalid", normalized_url);
    EXPECT_FALSE(fallback->poll().has_value());

    PlatformBridge standard_failure;
    standard_failure.list_relay_rooms =
        [](const std::string&, const std::string&)
            -> std::vector<og::ui::PickerRelayRoomInfo> {
            throw std::runtime_error("relay unavailable");
        };
    set_platform_bridge(std::move(standard_failure));
    auto failed =
        og::ui::begin_list_relay_rooms("https://relay.invalid");
    ASSERT_NE(nullptr, failed);
    const auto failed_result = failed->poll();
    ASSERT_TRUE(failed_result.has_value());
    EXPECT_EQ("relay unavailable", failed_result->error);

    PlatformBridge unknown_failure;
    unknown_failure.list_relay_rooms =
        [](const std::string&, const std::string&)
            -> std::vector<og::ui::PickerRelayRoomInfo> {
            throw 7;
        };
    set_platform_bridge(std::move(unknown_failure));
    auto unknown =
        og::ui::begin_list_relay_rooms("https://relay.invalid");
    ASSERT_NE(nullptr, unknown);
    const auto unknown_result = unknown->poll();
    ASSERT_TRUE(unknown_result.has_value());
    EXPECT_EQ("Relay room listing failed.", unknown_result->error);
}

TEST(PickerFuncs, picker_join_game_direct_prompt_replaces_client)
{
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    std::optional<og::ui::PickerJoinGameOptions> captured_options;
    auto next_trace = std::make_shared<PickerLobbyClientTrace>();
    auto* next_raw = static_cast<TraceablePickerLobbyClient*>(nullptr);
    bridge.create_join_picker_lobby_client =
        [&](const og::ui::PickerJoinGameOptions& options)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        captured_options = options;
        auto next_client =
            std::make_unique<TraceablePickerLobbyClient>(next_trace);
        next_raw = next_client.get();
        return next_client;
    };
    set_platform_bridge(std::move(bridge));

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("198.51.100.24:24567");

    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(current_trace);
    auto* const current_raw =
        static_cast<TraceablePickerLobbyClient*>(current_client.get());
    ActivePickerLobbyClientGuard guard(current_raw);

    EXPECT_TRUE(picker_join_game(current_client));
    ASSERT_TRUE(captured_options.has_value());
    EXPECT_EQ(og::ui::PickerJoinMode::Direct, captured_options->mode);
    EXPECT_EQ("198.51.100.24:24567", captured_options->direct_endpoint);
    EXPECT_EQ("", captured_options->room_code);
    EXPECT_EQ(next_raw, current_client.get());
    EXPECT_EQ(next_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(1, current_trace->shutdown_calls);
    EXPECT_EQ(1, next_trace->initialize_calls);

    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();
}

// Joining must NOT pop a blocking "connected" modal: the join client's live
// status (e.g. "Status: connected", "Lobby: N players") is shown on top of the
// same lobby menu instead, exactly like the host. (The host already passes
// show_success_popup=false; the join must match it.)
TEST(PickerFuncs, picker_join_game_does_not_pop_a_success_modal)
{
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    bridge.create_join_picker_lobby_client =
        [&](const og::ui::PickerJoinGameOptions&)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        auto next_client = std::make_unique<TraceablePickerLobbyClient>(
            std::make_shared<PickerLobbyClientTrace>());
        next_client->status_lines_ = {"Status: connected", "Lobby: 2 players"};
        return next_client;
    };
    set_platform_bridge(std::move(bridge));

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true); // "Direct connect?" = yes
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("198.51.100.24:24567");

    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(current_trace);
    ActivePickerLobbyClientGuard guard(
        static_cast<TraceablePickerLobbyClient*>(current_client.get()));

    trace_clear();
    EXPECT_TRUE(picker_join_game(current_client));
    EXPECT_EQ(0, trace_count("popup"))
        << "joining must not show a blocking success modal; its status renders "
           "in the lobby menu like the host's";

    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();
}

TEST(PickerFuncs, picker_join_game_relay_prompt_replaces_client)
{
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    std::optional<og::ui::PickerJoinGameOptions> captured_options;
    std::string listed_base_url;
    std::string listed_campaign_tag;
    auto next_trace = std::make_shared<PickerLobbyClientTrace>();
    auto* next_raw = static_cast<TraceablePickerLobbyClient*>(nullptr);
    bridge.list_relay_rooms =
        [&](const std::string& base_url, const std::string& campaign_tag) {
        listed_base_url = base_url;
        listed_campaign_tag = campaign_tag;
        og::ui::PickerRelayRoomInfo alpha;
        alpha.code = "GLAD-ALPHA";
        alpha.host_name = "Alpha";
        alpha.player_count = 2;

        og::ui::PickerRelayRoomInfo beta;
        beta.code = "GLAD-BETA";
        beta.host_name = "Beta";
        beta.player_count = 1;

        return std::vector<og::ui::PickerRelayRoomInfo>{alpha, beta};
    };
    bridge.create_join_picker_lobby_client =
        [&](const og::ui::PickerJoinGameOptions& options)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        captured_options = options;
        auto next_client =
            std::make_unique<TraceablePickerLobbyClient>(next_trace);
        next_raw = next_client.get();
        return next_client;
    };
    set_platform_bridge(std::move(bridge));

    auto& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_campaign = save.current_campaign;
    save.current_campaign = "gladiator";

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("GLAD-BETA");

    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(current_trace);
    auto* const current_raw =
        static_cast<TraceablePickerLobbyClient*>(current_client.get());
    ActivePickerLobbyClientGuard guard(current_raw);

    EXPECT_TRUE(picker_join_game(current_client));
    ASSERT_TRUE(captured_options.has_value());
    EXPECT_EQ(og::ui::PickerJoinMode::Relay, captured_options->mode);
    EXPECT_EQ("GLAD-BETA", captured_options->room_code);
    EXPECT_FALSE(captured_options->relay_base_url.empty());
    EXPECT_EQ(captured_options->relay_base_url, listed_base_url);
    EXPECT_EQ("gladiator", listed_campaign_tag);
    EXPECT_EQ(next_raw, current_client.get());
    EXPECT_EQ(next_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(1, current_trace->shutdown_calls);
    EXPECT_EQ(1, next_trace->initialize_calls);

    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();
    save.current_campaign = old_campaign;
}

TEST(PickerFuncs, picker_join_game_relay_prompt_uses_room_list_error_message)
{
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    std::optional<og::ui::PickerJoinGameOptions> captured_options;
    auto next_trace = std::make_shared<PickerLobbyClientTrace>();
    auto* next_raw = static_cast<TraceablePickerLobbyClient*>(nullptr);
    bridge.list_relay_rooms =
        [](const std::string&, const std::string&)
            -> std::vector<og::ui::PickerRelayRoomInfo> {
        throw std::runtime_error("simulated room list failure");
    };
    bridge.create_join_picker_lobby_client =
        [&](const og::ui::PickerJoinGameOptions& options)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        captured_options = options;
        auto next_client =
            std::make_unique<TraceablePickerLobbyClient>(next_trace);
        next_raw = next_client.get();
        return next_client;
    };
    set_platform_bridge(std::move(bridge));

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("GLAD-ROOM");

    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(current_trace);
    auto* const current_raw =
        static_cast<TraceablePickerLobbyClient*>(current_client.get());
    ActivePickerLobbyClientGuard guard(current_raw);

    EXPECT_TRUE(picker_join_game(current_client));
    ASSERT_TRUE(captured_options.has_value());
    EXPECT_EQ(og::ui::PickerJoinMode::Relay, captured_options->mode);
    EXPECT_EQ("GLAD-ROOM", captured_options->room_code);
    EXPECT_EQ(next_raw, current_client.get());
    EXPECT_EQ(next_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(1, current_trace->shutdown_calls);
    EXPECT_EQ(1, next_trace->initialize_calls);

    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();
}

TEST(PickerFuncs, picker_join_game_catches_invalid_relay_base_url)
{
    ScopedEnvVar relay_env("OPENGLAD_RELAY_BASE_URL");
#ifdef _WIN32
    _putenv_s("OPENGLAD_RELAY_BASE_URL", "ftp://relay.invalid");
#else
    setenv("OPENGLAD_RELAY_BASE_URL", "ftp://relay.invalid", 1);
#endif

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("GLAD-XKCD");

    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(current_trace);
    auto* const current_raw =
        static_cast<TraceablePickerLobbyClient*>(current_client.get());
    ActivePickerLobbyClientGuard guard(current_raw);

    EXPECT_FALSE(picker_join_game(current_client));
    EXPECT_EQ(current_raw, current_client.get());
    EXPECT_EQ(current_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(0, current_trace->shutdown_calls);

    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();
}

TEST(PickerFuncs, concrete_sdl_client_delegates_host_join_load_and_save)
{
    ScopedTraceBuffer trace_guard;
    struct PromptQueueRestore
    {
        PromptQueueRestore()
        {
            picker_testing_yes_or_no_queue_clear();
            level_editor_testing_prompt_queue_clear();
        }
        ~PromptQueueRestore()
        {
            picker_testing_yes_or_no_queue_clear();
            level_editor_testing_prompt_queue_clear();
        }
    } prompt_queue_restore;
    picker_lobby_shutdown();
    std::unique_ptr<og::ui::IPickerClient> client =
        picker_testing_create_sdl_client();
    ASSERT_NE(nullptr, client);

    // The concrete host adapter owns port validation, before any transport is
    // created.  A rejected port must leave the session local and explain why.
    trace_clear();
    level_editor_testing_prompt_queue_push("0");
    EXPECT_FALSE(client->host_game());
    EXPECT_TRUE(trace_contains("popup", "1 to 65535"));
    EXPECT_TRUE(level_editor_testing_prompt_queue_ref().empty());

    // The concrete join adapter must delegate to the same checked factory as
    // the public join flow, preserving a failed connection as a clean false.
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    std::vector<og::ui::PickerHostGameOptions> captured_host_options;
    std::optional<og::ui::PickerJoinGameOptions> captured_join_options;
    auto hosted_trace = std::make_shared<PickerLobbyClientTrace>();
    auto* hosted_raw = static_cast<TraceablePickerLobbyClient*>(nullptr);
    bridge.create_host_picker_lobby_client =
        [&](const og::ui::PickerHostGameOptions& options)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        captured_host_options.push_back(options);
        auto hosted_client =
            std::make_unique<TraceablePickerLobbyClient>(hosted_trace);
        hosted_raw = hosted_client.get();
        return hosted_client;
    };
    bridge.create_join_picker_lobby_client =
        [&](const og::ui::PickerJoinGameOptions& options)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        captured_join_options = options;
        throw std::runtime_error("adapter join failure");
    };
    set_platform_bridge(std::move(bridge));

    // A valid host request must preserve the exact parsed transport options,
    // replace the live lobby, and avoid a blocking success modal.
    picker_testing_yes_or_no_queue_push(false);
    level_editor_testing_prompt_queue_push("24567");
    trace_clear();
    EXPECT_TRUE(client->host_game());
    ASSERT_EQ(1u, captured_host_options.size());
    EXPECT_EQ(24567, captured_host_options.front().port);
    EXPECT_FALSE(captured_host_options.front().enable_relay);
    EXPECT_TRUE(captured_host_options.front().relay_base_url.empty());
    EXPECT_EQ(1, hosted_trace->initialize_calls);
    EXPECT_EQ(hosted_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(0, trace_count("popup"));
    EXPECT_EQ(0, picker_testing_yes_or_no_queue_remaining());
    EXPECT_TRUE(level_editor_testing_prompt_queue_ref().empty());

    // Enabling relay hosting validates the configured URL before replacing the
    // working lobby. An invalid scheme must be surfaced and leave it active.
    {
        ScopedEnvVar relay_env("OPENGLAD_RELAY_BASE_URL");
#ifdef _WIN32
        _putenv_s("OPENGLAD_RELAY_BASE_URL", "ftp://relay.invalid");
#else
        setenv("OPENGLAD_RELAY_BASE_URL", "ftp://relay.invalid", 1);
#endif
        picker_testing_yes_or_no_queue_push(true);
        level_editor_testing_prompt_queue_push("24568");
        trace_clear();
        EXPECT_FALSE(client->host_game());
        EXPECT_EQ(1u, captured_host_options.size());
        EXPECT_EQ(hosted_raw, og::ui::active_picker_lobby_client());
        EXPECT_TRUE(trace_contains(
            "popup", "Relay base URL must use http://"));
        EXPECT_EQ(0, picker_testing_yes_or_no_queue_remaining())
            << "relay validation must consume the host-mode decision";
        EXPECT_TRUE(level_editor_testing_prompt_queue_ref().empty())
            << "relay validation must consume the requested port";
    }

    ASSERT_EQ(0, picker_testing_yes_or_no_queue_remaining());
    ASSERT_TRUE(level_editor_testing_prompt_queue_ref().empty());
    picker_testing_yes_or_no_queue_push(true);
    level_editor_testing_prompt_queue_push("127.0.0.1:24567");
    trace_clear();
    EXPECT_FALSE(client->join_game());
    EXPECT_TRUE(trace_contains("popup", "adapter join failure"));
    ASSERT_TRUE(captured_join_options.has_value());
    EXPECT_EQ(og::ui::PickerJoinMode::Direct,
              captured_join_options->mode);
    EXPECT_EQ("127.0.0.1:24567",
              captured_join_options->direct_endpoint);
    EXPECT_TRUE(captured_join_options->room_code.empty());
    EXPECT_TRUE(captured_join_options->relay_base_url.empty());
    EXPECT_EQ(0, picker_testing_yes_or_no_queue_remaining());
    EXPECT_TRUE(level_editor_testing_prompt_queue_ref().empty());

    // Loading with no display is a supported cancellation edge in the real
    // Company List wrapper; the adapter must propagate it without mutation.
    screen* const saved_screen = og::runtime::current_session->myscreen_;
    og::runtime::current_session->myscreen_ = nullptr;
    EXPECT_FALSE(client->load_game());
    og::runtime::current_session->myscreen_ = saved_screen;

    // The retired manual-save transition is a real autosave of the active
    // company, and reports the write result to the picker state machine.
    EXPECT_TRUE(client->save_game());

    client.reset();
    picker_lobby_shutdown();
}

TEST(PickerFuncs, concrete_sdl_client_relay_state_machine_paths)
{
    EXPECT_EQ(0, picker_testing_exercise_sdl_client_internal_paths());
}

TEST(PickerFuncs, local_lobby_client_without_screen_has_exact_empty_contract)
{
    auto client = og::ui::create_local_picker_lobby_client();
    ASSERT_NE(nullptr, client);

    screen* const saved_screen = og::runtime::current_session->myscreen_;
    struct ScreenRestore
    {
        screen*& target;
        screen* saved;
        ~ScreenRestore() { target = saved; }
    } restore{og::runtime::current_session->myscreen_, saved_screen};
    og::runtime::current_session->myscreen_ = nullptr;

    client->poll_and_apply();
    client->set_player_mode(2);
    EXPECT_FALSE(client->add_local_seat());
    EXPECT_FALSE(client->request_start_game());
    EXPECT_FALSE(client->request_seat_team_change(0, 1));
    EXPECT_FALSE(client->build_game_start_config().has_value());
    EXPECT_TRUE(client->lobby_players().empty());
    EXPECT_FALSE(client->authoritative_team_mask().has_value());
    EXPECT_EQ(0u, client->local_seat_count());
    EXPECT_FALSE(client->start_request_pending());
    EXPECT_FALSE(client->has_game_start_config());
}

TEST(PickerFuncs, no_active_lobby_client_wrappers_return_documented_defaults)
{
    ActivePickerLobbyClientGuard active_guard(nullptr);
    picker_lobby_shutdown();

    EXPECT_FALSE(picker_lobby_start_request_pending());
    EXPECT_FALSE(picker_lobby_has_game_start_config());
    EXPECT_TRUE(picker_lobby_status_lines().empty());
    EXPECT_TRUE(picker_lobby_session_room_code().empty());
    EXPECT_FALSE(picker_lobby_set_ready(true));
    EXPECT_EQ(og::sim::StartDenialReason::None,
              picker_lobby_last_start_denial());
    EXPECT_FALSE(picker_lobby_authoritative_team_mask().has_value());
    EXPECT_TRUE(picker_lobby_local_player_indices().empty());
}

TEST(PickerFuncs, lobby_poll_is_suppressed_while_gameplay_owns_the_transport)
{
    auto trace = std::make_shared<PickerLobbyClientTrace>();
    TraceablePickerLobbyClient client(trace);
    ActivePickerLobbyClientGuard active_guard(&client);

    const bool saved_gameplay_active =
        og::runtime::current_session->gameplay_active_;
    struct GameplayRestore
    {
        bool& target;
        bool saved;
        ~GameplayRestore() { target = saved; }
    } restore{og::runtime::current_session->gameplay_active_,
              saved_gameplay_active};

    og::runtime::current_session->gameplay_active_ = true;
    picker_lobby_poll();
    EXPECT_EQ(0, trace->poll_calls);

    og::runtime::current_session->gameplay_active_ = false;
    picker_lobby_poll();
    EXPECT_EQ(1, trace->poll_calls);
}

TEST(PickerFuncs, right_click_player_mode_only_deselects_the_active_count)
{
    auto trace = std::make_shared<PickerLobbyClientTrace>();
    TraceablePickerLobbyClient client(trace);
    ActivePickerLobbyClientGuard active_guard(&client);
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char saved_numplayers = save.numplayers;
    struct PlayerCountRestore
    {
        SaveData& save;
        unsigned char count;
        ~PlayerCountRestore() { save.numplayers = count; }
    } restore{save, saved_numplayers};

    vbutton dispatcher;
    dispatcher.arg = 2;
    save.numplayers = 2;
    EXPECT_EQ(MENU_OK, dispatcher.do_call_right(
        button_action_id(ButtonAction::SetPlayerMode), 2));
    EXPECT_EQ(1, trace->set_player_mode_calls);
    EXPECT_EQ(0, trace->last_player_mode)
        << "right-clicking the selected count must request spectator mode";

    save.numplayers = 1;
    EXPECT_EQ(MENU_OK, dispatcher.do_call_right(
        button_action_id(ButtonAction::SetPlayerMode), 2));
    EXPECT_EQ(1, trace->set_player_mode_calls)
        << "right-clicking an unselected count is a strict no-op";
}

TEST(PickerFuncs, go_menu_surfaces_each_authoritative_start_denial)
{
    ScopedTraceBuffer trace_guard;
    ContractPickerLobbyClient client;
    client.networked_result = true;
    client.host_controls_visible_result = false;
    client.request_start_result = false;

    og::sim::LobbyPlayer player;
    player.player_index = 0;
    player.seat_id = 1;
    player.name = "Ready Host";
    player.company = "Ready Company";
    player.is_host = true;
    player.ready = true;
    player.character_slots.push_back(og::sim::LobbyCharacterSlot{
        .slot_index = 0,
        .character = og::sim::LobbyCharacterData{
            .name = "Deployed Hero",
            .family = FAMILY_SOLDIER,
            .teamnum = 0,
        },
        .deployed = true,
    });
    client.players = {player};

    ActivePickerLobbyClientGuard active_guard(&client);
    const bool saved_start_requested = g_start_game_requested;
    struct StartRequestRestore
    {
        bool saved;
        ~StartRequestRestore() { g_start_game_requested = saved; }
    } restore{saved_start_requested};

    client.denial_result = og::sim::StartDenialReason::MachinesNotReady;
    g_start_game_requested = false;
    trace_clear();
    EXPECT_EQ(MENU_REDRAW, go_menu(0));
    EXPECT_TRUE(trace_contains(
        "popup", "Waiting for other\nmachines to ready"))
        << "a late readiness denial with no stale blocker rows needs fallback text";

    client.denial_result =
        og::sim::StartDenialReason::NoDeployedCharacters;
    g_start_game_requested = false;
    trace_clear();
    EXPECT_EQ(MENU_REDRAW, go_menu(0));
    EXPECT_TRUE(trace_contains(
        "popup", "Deploy at least\none character\nbefore starting"))
        << "a late authoritative roster denial must identify deployment";
    EXPECT_EQ(2, client.request_start_calls);
}

// (do_load retired with the slot menus; the lobby sync-on-load behavior is
// exercised by the Company List open path — open_company_slot -> load ->
// picker_lobby_sync_from_save — in the og_test_basecamp flows.)

TEST(PickerFuncs, lobby_sync_preserves_sparse_team_assignments)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    save.team_size = 2;
    save.numplayers = 2;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 1;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->teamnum = 3;

    picker_lobby_initialize_from_save();
    picker_lobby_sync_from_save();

    vbutton dispatcher;
    ASSERT_EQ(4, static_cast<int>(dispatcher.do_call(
        button_action_id(ButtonAction::SetPlayerMode), 2)));
    EXPECT_EQ(2, static_cast<int>(save.numplayers));
    ASSERT_TRUE(save.team_list[0] && save.team_list[1]);
    EXPECT_EQ(1, static_cast<int>(save.team_list[0]->teamnum));
    EXPECT_EQ(3, static_cast<int>(save.team_list[1]->teamnum));

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
}

TEST(PickerFuncs, lobby_sync_preserves_single_player_team_four_assignment)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    save.team_size = 1;
    save.numplayers = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 3;

    picker_lobby_initialize_from_save();
    picker_lobby_sync_from_save();

    EXPECT_EQ(1, static_cast<int>(save.numplayers));
    ASSERT_TRUE(save.team_list[0]);
    EXPECT_EQ(3, static_cast<int>(save.team_list[0]->teamnum));

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
}

TEST(PickerFuncs, lobby_set_player_mode_honors_requested_positive_count)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    save.team_size = 1;
    save.numplayers = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 3;

    picker_lobby_initialize_from_save();

    ASSERT_EQ(4, static_cast<int>(set_player_mode(2)));
    EXPECT_EQ(2, static_cast<int>(save.numplayers));
    ASSERT_TRUE(save.team_list[0]);
    EXPECT_EQ(3, static_cast<int>(save.team_list[0]->teamnum));

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
}

TEST(PickerFuncs, lobby_set_player_mode_allows_return_to_single_player_without_dropping_other_teams)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    save.team_size = 4;
    save.numplayers = 4;
    for (int i = 0; i < 4; ++i)
    {
        save.team_list[static_cast<std::size_t>(i)] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[static_cast<std::size_t>(i)]->name = std::format("Team {}", i + 1);
        save.team_list[static_cast<std::size_t>(i)]->teamnum = static_cast<short>(i);
    }

    picker_lobby_initialize_from_save();

    ASSERT_EQ(4, static_cast<int>(set_player_mode(1)));
    EXPECT_EQ(1, static_cast<int>(save.numplayers));
    EXPECT_EQ(4, static_cast<int>(save.team_size));
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(save.team_list[static_cast<std::size_t>(i)]) << "team slot " << i << " should be preserved";
        EXPECT_EQ(static_cast<int>(i), static_cast<int>(save.team_list[static_cast<std::size_t>(i)]->teamnum));
        EXPECT_EQ(std::format("Team {}", i + 1), save.team_list[static_cast<std::size_t>(i)]->name);
    }

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
}

TEST(PickerFuncs, lobby_start_request_sets_start_flag_after_confirmation)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    save.team_size = 1;
    save.numplayers = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;

    g_start_game_requested = false;
    picker_lobby_initialize_from_save();

    EXPECT_FALSE(picker_lobby_start_request_pending());
    EXPECT_TRUE(picker_lobby_request_start());
    EXPECT_TRUE(g_start_game_requested);
    EXPECT_FALSE(picker_lobby_start_request_pending());

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    g_start_game_requested = false;
}

TEST(PickerFuncs, lobby_start_request_captures_game_start_config)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_campaign = save.current_campaign;
    const short old_scen_num = save.scen_num;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    const short old_allied_mode = save.allied_mode;
    const std::int32_t old_difficulty =
        og::runtime::current_session->current_difficulty_;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.team_size = 2;
    save.numplayers = 2;
    save.allied_mode = 0;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Leader";
    save.team_list[0]->teamnum = 0;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->name = "Support";
    save.team_list[1]->teamnum = 1;
    og::runtime::current_session->current_difficulty_ = 3;

    g_start_game_requested = false;
    picker_lobby_initialize_from_save();

    ASSERT_TRUE(picker_lobby_request_start());
    ASSERT_TRUE(g_start_game_requested);

    const std::optional<og::ui::PickerLobbyGameStartConfig> config =
        picker_lobby_consume_game_start_config();
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(2u, config->save_data.numplayers);
    EXPECT_EQ("gladiator", config->save_data.current_campaign);
    EXPECT_EQ(1, config->save_data.scen_num);
    EXPECT_EQ(0, config->save_data.allied_mode);
    EXPECT_EQ(3, config->difficulty);
    ASSERT_EQ(2u, config->save_data.team_list.size());
    EXPECT_EQ("Leader", config->save_data.team_list[0].character.name);
    EXPECT_EQ(0, config->save_data.team_list[0].character.teamnum);
    EXPECT_EQ("Support", config->save_data.team_list[1].character.name);
    EXPECT_EQ(1, config->save_data.team_list[1].character.teamnum);
    EXPECT_FALSE(picker_lobby_consume_game_start_config().has_value());

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.current_campaign = old_campaign;
    save.scen_num = old_scen_num;
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    save.allied_mode = old_allied_mode;
    og::runtime::current_session->current_difficulty_ = old_difficulty;
    g_start_game_requested = false;
}

TEST(PickerFuncs, lobby_start_request_preserves_spectator_game_start_config)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_campaign = save.current_campaign;
    const short old_scen_num = save.scen_num;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    const short old_allied_mode = save.allied_mode;
    const std::int32_t old_difficulty =
        og::runtime::current_session->current_difficulty_;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.team_size = 1;
    save.numplayers = 0;
    save.allied_mode = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Spectator";
    save.team_list[0]->teamnum = 0;
    og::runtime::current_session->current_difficulty_ = 4;

    g_start_game_requested = false;
    picker_lobby_initialize_from_save();

    ASSERT_TRUE(picker_lobby_request_start());
    ASSERT_TRUE(g_start_game_requested);

    const std::optional<og::ui::PickerLobbyGameStartConfig> config =
        picker_lobby_consume_game_start_config();
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(0u, config->save_data.numplayers);
    EXPECT_EQ("gladiator", config->save_data.current_campaign);
    EXPECT_EQ(1, config->save_data.scen_num);
    EXPECT_EQ(1, config->save_data.allied_mode);
    EXPECT_EQ(4, config->difficulty);
    ASSERT_EQ(1u, config->save_data.team_list.size());
    EXPECT_EQ("Spectator", config->save_data.team_list[0].character.name);
    EXPECT_EQ(0, config->save_data.team_list[0].character.teamnum);
    EXPECT_FALSE(picker_lobby_consume_game_start_config().has_value());

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.current_campaign = old_campaign;
    save.scen_num = old_scen_num;
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    save.allied_mode = old_allied_mode;
    og::runtime::current_session->current_difficulty_ = old_difficulty;
    g_start_game_requested = false;
}

TEST(PickerFuncs, lobby_reinitialize_after_game_allows_second_confirmed_start)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    save.team_size = 1;
    save.numplayers = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;

    g_start_game_requested = false;
    picker_lobby_initialize_from_save();

    EXPECT_TRUE(picker_lobby_request_start());
    EXPECT_TRUE(g_start_game_requested);
    EXPECT_FALSE(picker_lobby_start_request_pending());

    picker_reinitialize_lobby_after_game();
    EXPECT_FALSE(g_start_game_requested);
    EXPECT_FALSE(picker_lobby_start_request_pending());
    EXPECT_FALSE(picker_lobby_consume_game_start_config().has_value());

    EXPECT_TRUE(picker_lobby_request_start());
    EXPECT_TRUE(g_start_game_requested);
    EXPECT_FALSE(picker_lobby_start_request_pending());

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    g_start_game_requested = false;
}

TEST(PickerFuncs, go_menu_starts_via_lobby_confirmation_and_reinitializes_for_replay)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_save_name = save.save_name;
    const std::string old_campaign = save.current_campaign;
    const short old_scen_num = save.scen_num;
    const auto old_completed_levels = save.completed_levels;
    const auto old_current_levels = save.current_levels;
    const std::uint32_t old_score = save.score;
    const std::uint32_t old_totalcash = save.totalcash;
    const std::uint32_t old_totalscore = save.totalscore;
    const short old_my_team = save.my_team;
    std::uint32_t old_m_score[4];
    std::uint32_t old_m_totalcash[4];
    std::uint32_t old_m_totalscore[4];
    for (int i = 0; i < 4; ++i)
    {
        old_m_score[i] = save.m_score[i];
        old_m_totalcash[i] = save.m_totalcash[i];
        old_m_totalscore[i] = save.m_totalscore[i];
    }
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    const short old_allied_mode = save.allied_mode;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
#ifdef TESTING
    const bool old_remove_exits = g_test_remove_exits;
    const std::int32_t old_tick_limit = og::sim::g_test_level_tick_limit_override;
#endif

    save.reset();
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    save.scen_num = 1;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    soldier->teamnum = 0;
    soldier->strength = 200;
    soldier->dexterity = 200;
    soldier->constitution = 200;
    soldier->intelligence = 200;
    soldier->armor = 200;
    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    archer->teamnum = 0;
    archer->strength = 200;
    archer->dexterity = 200;
    archer->constitution = 200;
    archer->intelligence = 200;
    archer->armor = 200;
    save.team_list[0] = std::move(soldier);
    save.team_list[1] = std::move(archer);
    save.team_size = 2;

    g_start_game_requested = false;
#ifdef TESTING
    g_test_remove_exits = true;
    og::sim::g_test_level_tick_limit_override = 15;
#endif
    set_game_speed(0.0f);

    const int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    ASSERT_EQ(button_action_id(ButtonAction::CreateTeamMenu), go_menu(0));
    EXPECT_GT(g_test_game_epoch.load(std::memory_order_acquire), epoch_before);
    EXPECT_FALSE(g_test_in_game.load(std::memory_order_acquire));
    EXPECT_FALSE(g_start_game_requested);
    EXPECT_FALSE(picker_lobby_start_request_pending());

    const int replay_epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    ASSERT_EQ(button_action_id(ButtonAction::CreateTeamMenu), go_menu(0));
    EXPECT_GT(g_test_game_epoch.load(std::memory_order_acquire), replay_epoch_before);
    EXPECT_FALSE(g_test_in_game.load(std::memory_order_acquire));
    EXPECT_FALSE(g_start_game_requested);
    EXPECT_FALSE(picker_lobby_start_request_pending());

    picker_lobby_shutdown();
    save.reset();
    save.save_name = old_save_name;
    save.current_campaign = old_campaign;
    save.scen_num = old_scen_num;
    save.completed_levels = old_completed_levels;
    save.current_levels = old_current_levels;
    save.score = old_score;
    save.totalcash = old_totalcash;
    save.totalscore = old_totalscore;
    save.my_team = old_my_team;
    for (int i = 0; i < 4; ++i)
    {
        save.m_score[i] = old_m_score[i];
        save.m_totalcash[i] = old_m_totalcash[i];
        save.m_totalscore[i] = old_m_totalscore[i];
    }
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    save.allied_mode = old_allied_mode;
#ifdef TESTING
    g_test_remove_exits = old_remove_exits;
    og::sim::g_test_level_tick_limit_override = old_tick_limit;
#endif
    set_game_speed(old_speed);
    g_start_game_requested = false;
}

TEST(PickerFuncs, async_team_build_start_preserves_preexisting_remote_start_request)
{
    picker_lobby_shutdown();

    ContractPickerLobbyClient remote_client;
    remote_client.pending_config = og::ui::PickerLobbyGameStartConfig{};
    ActivePickerLobbyClientGuard active_guard(&remote_client);
    g_start_game_requested = true;

    picker_prepare_async_team_build_start_request();

    EXPECT_TRUE(g_start_game_requested);
    EXPECT_EQ(0, remote_client.request_start_calls);
    EXPECT_TRUE(remote_client.pending_config.has_value());

    picker_lobby_shutdown();
    g_start_game_requested = false;
}

TEST(PickerFuncs, go_menu_honors_preexisting_remote_start_request)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_save_name = save.save_name;
    const std::string old_campaign = save.current_campaign;
    const short old_scen_num = save.scen_num;
    const auto old_completed_levels = save.completed_levels;
    const auto old_current_levels = save.current_levels;
    const std::uint32_t old_score = save.score;
    const std::uint32_t old_totalcash = save.totalcash;
    const std::uint32_t old_totalscore = save.totalscore;
    const short old_my_team = save.my_team;
    std::uint32_t old_m_score[4];
    std::uint32_t old_m_totalcash[4];
    std::uint32_t old_m_totalscore[4];
    for (int i = 0; i < 4; ++i)
    {
        old_m_score[i] = save.m_score[i];
        old_m_totalcash[i] = save.m_totalcash[i];
        old_m_totalscore[i] = save.m_totalscore[i];
    }
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    const short old_allied_mode = save.allied_mode;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
#ifdef TESTING
    const bool old_remove_exits = g_test_remove_exits;
    const std::int32_t old_tick_limit = og::sim::g_test_level_tick_limit_override;
#endif

    save.reset();
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    save.scen_num = 1;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    soldier->teamnum = 0;
    soldier->strength = 200;
    soldier->dexterity = 200;
    soldier->constitution = 200;
    soldier->intelligence = 200;
    soldier->armor = 200;
    save.team_list[0] = std::move(soldier);
    save.team_size = 1;

    ContractPickerLobbyClient remote_client;
    remote_client.pending_config = og::ui::PickerLobbyGameStartConfig{};
    remote_client.pending_config->save_data.current_campaign =
        save.current_campaign;
    remote_client.pending_config->save_data.scen_num =
        static_cast<std::int16_t>(save.scen_num);
    remote_client.pending_config->save_data.numplayers = 1;
    remote_client.pending_config->save_data.team_list.push_back(
        og::sim::LobbyCharacterSlot{
            .slot_index = 0,
            .character = og::sim::LobbyCharacterData{
                .guy_id = save.team_list[0]->id,
                .name = save.team_list[0]->name,
                .family = static_cast<std::int8_t>(save.team_list[0]->family),
                .strength = save.team_list[0]->strength,
                .dexterity = save.team_list[0]->dexterity,
                .constitution = save.team_list[0]->constitution,
                .intelligence = save.team_list[0]->intelligence,
                .armor = save.team_list[0]->armor,
                .exp = save.team_list[0]->exp,
                .kills = save.team_list[0]->kills,
                .level_kills = save.team_list[0]->level_kills,
                .total_damage = save.team_list[0]->total_damage,
                .total_hits = save.team_list[0]->total_hits,
                .total_shots = save.team_list[0]->total_shots,
                .teamnum = save.team_list[0]->teamnum,
                .scen_damage = save.team_list[0]->scen_damage,
                .scen_kills = save.team_list[0]->scen_kills,
                .scen_damage_taken = save.team_list[0]->scen_damage_taken,
                .scen_min_hp = save.team_list[0]->scen_min_hp,
                .scen_shots = save.team_list[0]->scen_shots,
                .scen_hits = save.team_list[0]->scen_hits,
                .level = save.team_list[0]->level,
            },
        });
    ActivePickerLobbyClientGuard active_guard(&remote_client);
    g_start_game_requested = true;
#ifdef TESTING
    g_test_remove_exits = true;
    og::sim::g_test_level_tick_limit_override = 15;
#endif
    set_game_speed(0.0f);

    const int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    ASSERT_EQ(button_action_id(ButtonAction::CreateTeamMenu), go_menu(0));
    EXPECT_GT(g_test_game_epoch.load(std::memory_order_acquire), epoch_before);
    EXPECT_EQ(1, remote_client.consume_calls);
    EXPECT_FALSE(g_test_in_game.load(std::memory_order_acquire));
    EXPECT_FALSE(g_start_game_requested);

    picker_lobby_shutdown();
    save.reset();
    save.save_name = old_save_name;
    save.current_campaign = old_campaign;
    save.scen_num = old_scen_num;
    save.completed_levels = old_completed_levels;
    save.current_levels = old_current_levels;
    save.score = old_score;
    save.totalcash = old_totalcash;
    save.totalscore = old_totalscore;
    save.my_team = old_my_team;
    for (int i = 0; i < 4; ++i)
    {
        save.m_score[i] = old_m_score[i];
        save.m_totalcash[i] = old_m_totalcash[i];
        save.m_totalscore[i] = old_m_totalscore[i];
    }
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    save.allied_mode = old_allied_mode;
#ifdef TESTING
    g_test_remove_exits = old_remove_exits;
    og::sim::g_test_level_tick_limit_override = old_tick_limit;
#endif
    set_game_speed(old_speed);
    g_start_game_requested = false;
}

TEST(PickerFuncs, train_team_change_immediately_syncs_saved_roster)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::uint32_t old_cash[4];
    for (int i = 0; i < 4; ++i)
        old_cash[i] = save.m_totalcash[i];
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    std::unique_ptr<guy> old_current = std::move(og::runtime::current_session->current_guy_);
    const short old_team_num = og::runtime::current_session->current_team_num_;
    auto* old_team_button = og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex];
    auto* old_train_session = pks().train_session;

    save.team_size = 1;
    save.numplayers = 2;
    save.m_totalcash[0] = 10000;
    save.m_totalcash[1] = 10000;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 2;
    save.team_list[0]->name = "Trainer";

    og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex] =
        new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0,
                    "team", KEYSTATE_UNKNOWN);

    og::ui::TrainSession session(save);
    pks().train_session = &session;
    EXPECT_EQ(2, static_cast<int>(session.working_copy().teamnum))
        << "TRAIN must start from Base Camp's saved TEAM value";
    og::runtime::current_session->current_guy_ =
        std::make_unique<guy>(session.working_copy());
    og::runtime::current_session->current_team_num_ = session.working_copy().teamnum;

    vbutton stat_dispatcher;
    stat_dispatcher.arg = BUT_STR;
    const short original_strength = session.working_copy().strength;
    ASSERT_EQ(MENU_OK, stat_dispatcher.do_call(
        button_action_id(ButtonAction::IncreaseStat), BUT_STR));
    EXPECT_EQ(original_strength + 1, session.working_copy().strength);
    ASSERT_EQ(MENU_OK, stat_dispatcher.do_call(
        button_action_id(ButtonAction::DecreaseStat), BUT_STR));
    EXPECT_EQ(original_strength, session.working_copy().strength);
    ASSERT_EQ(MENU_OK, stat_dispatcher.do_call_right(
        button_action_id(ButtonAction::IncreaseStat), BUT_STR));
    EXPECT_EQ(original_strength + 5, session.working_copy().strength);
    ASSERT_EQ(MENU_OK, stat_dispatcher.do_call_right(
        button_action_id(ButtonAction::DecreaseStat), BUT_STR));
    EXPECT_EQ(original_strength, session.working_copy().strength);

    ASSERT_EQ(4, static_cast<int>(change_teamnum(1)));
    EXPECT_EQ(3, static_cast<int>(session.working_copy().teamnum));
    EXPECT_EQ(3, static_cast<int>(og::runtime::current_session->current_guy_->teamnum));
    EXPECT_EQ(3, static_cast<int>(save.team_list[0]->teamnum))
        << "Base Camp's TEAM column must see the TRAIN choice immediately";
    EXPECT_TRUE(trace_contains("train", "team slot=0 team=3"));

    // ACCEPT remains harmless for the already-synchronized team setting;
    // stat changes still use the TrainSession's transactional path.
    ASSERT_TRUE(session.accept(true));
    picker_lobby_sync_from_save();
    EXPECT_EQ(3, static_cast<int>(save.team_list[0]->teamnum));

    picker_lobby_shutdown();
    pks().train_session = old_train_session;
    delete og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex];
    og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex] = old_team_button;
    og::runtime::current_session->current_guy_ = std::move(old_current);
    og::runtime::current_session->current_team_num_ = old_team_num;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    for (int i = 0; i < 4; ++i)
        save.m_totalcash[i] = old_cash[i];
}

TEST(PickerFuncs, create_train_menu_rejects_team_without_editable_local_slots)
{
    trace_clear();
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    std::unique_ptr<guy> old_current =
        std::move(og::runtime::current_session->current_guy_);
    auto* old_train_session = pks().train_session;

    save.team_size = 1;
    save.numplayers = 2;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_list[0]->name = "Remote Host";

    ContractPickerLobbyClient client;
    client.restrict_editable_slots = true;
    client.editable_slots.fill(false);
    ActivePickerLobbyClientGuard guard(&client);

    EXPECT_EQ(static_cast<Sint32>(4), create_train_menu(0));
    EXPECT_TRUE(trace_contains("popup", "NEED A TEAM"));
    EXPECT_EQ(old_train_session, pks().train_session);

    picker_lobby_shutdown();
    og::runtime::current_session->current_guy_ = std::move(old_current);
    pks().train_session = old_train_session;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
}

TEST(PickerFuncs, change_teamnum_ignores_non_editable_lobby_slot)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    std::unique_ptr<guy> old_current =
        std::move(og::runtime::current_session->current_guy_);
    const short old_team_num = og::runtime::current_session->current_team_num_;
    const int old_editguy = og::runtime::current_session->editguy_;
    auto* old_team_button = og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex];

    save.team_size = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    og::runtime::current_session->current_guy_ =
        std::make_unique<guy>(*save.team_list[0]);
    og::runtime::current_session->current_team_num_ = 0;
    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex] =
        new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0,
                    "team", KEYSTATE_UNKNOWN);

    ContractPickerLobbyClient client;
    client.restrict_editable_slots = true;
    client.editable_slots.fill(true);
    client.editable_slots[0] = false;
    ActivePickerLobbyClientGuard guard(&client);

    ASSERT_EQ(0, static_cast<int>(change_teamnum(1)));
    EXPECT_EQ(0, static_cast<int>(og::runtime::current_session->current_guy_->teamnum));

    delete og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex];
    og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex] = old_team_button;
    og::runtime::current_session->current_guy_ = std::move(old_current);
    og::runtime::current_session->current_team_num_ = old_team_num;
    og::runtime::current_session->editguy_ = old_editguy;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
}

TEST(PickerFuncs, train_session_survives_team_slot_replacement_after_accept)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::uint32_t old_cash[4];
    for (int i = 0; i < 4; ++i)
        old_cash[i] = save.m_totalcash[i];
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[static_cast<std::size_t>(i)]);

    save.team_size = 1;
    save.numplayers = 1;
    save.m_totalcash[0] = 10000;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_list[0]->strength = 10;
    save.team_list[0]->name = "Trainer";

    og::ui::TrainSession session(save);
    session.increase_stat(og::ui::TrainSession::Stat::Strength, 1);
    ASSERT_TRUE(session.accept(true));

    auto replacement = std::make_unique<guy>(save.team_list[0]->family);
    replacement->id = save.team_list[0]->id;
    og::ui::statscopy(replacement.get(), save.team_list[0].get());
    save.team_list[0] = std::move(replacement);

    EXPECT_EQ(save.team_list[0].get(), &session.original());
    EXPECT_EQ(0u, session.current_cost());
    EXPECT_FALSE(session.level_increased());
    EXPECT_FALSE(session.stats_increased());
    EXPECT_EQ(save.team_list[0]->strength, session.working_copy().strength);

    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    for (int i = 0; i < 4; ++i)
        save.m_totalcash[i] = old_cash[i];
}

TEST(PickerFuncs, get_saved_name_handles_legacy_truncated_and_named_slots)
{
    create_dir(get_user_path() + "save");

    // [SAVE-R5] Raw-slot seeding must not leave stray companies behind: with
    // startup selection scanning save/ (design §3.5), leaked fixture slots
    // would become phantom "most recent company" candidates for other tests.
    std::vector<std::string> seeded_slots;
    struct SlotCleanup
    {
        std::vector<std::string>& slots;
        ~SlotCleanup()
        {
            for (const std::string& slot : slots)
                (void)remove_user_file("save/" + slot + ".gtl");
        }
    } cleanup{seeded_slots};

    auto write_slot = [&seeded_slots](const std::string& slot,
                                      const std::string& bytes) {
        const std::string path = get_user_path() + "save/" + slot + ".gtl";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good()) << "failed to open " << path;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(out.good()) << "failed to write " << path;
        seeded_slots.push_back(slot);
    };

    EXPECT_EQ("EMPTY SLOT", get_saved_name("__picker_missing_slot__"));

    write_slot("__picker_bad_header__", "BAD");
    EXPECT_EQ("EMPTY SLOT", get_saved_name("__picker_bad_header__"));

    write_slot("__picker_missing_version__", "GTL");
    EXPECT_EQ("EMPTY SLOT", get_saved_name("__picker_missing_version__"));

    write_slot("__picker_version_1__", std::string("GTL", 3) + std::string(1, '\x01'));
    EXPECT_EQ("SAVED GAME", get_saved_name("__picker_version_1__"));

    write_slot("__picker_version_0__", std::string("GTL", 3) + std::string(1, '\0'));
    EXPECT_EQ("SAVED GAME", get_saved_name("__picker_version_0__"));

    write_slot("__picker_version_2_truncated__", std::string("GTL", 3) + std::string(1, '\x02'));
    EXPECT_EQ("SAVED GAME", get_saved_name("__picker_version_2_truncated__"));

    write_slot("__picker_version_7_missing_registered__",
               std::string("GTL", 3) + std::string(1, '\x07'));
    EXPECT_EQ("SAVED GAME", get_saved_name("__picker_version_7_missing_registered__"));

    std::string named = std::string("GTL", 3) + std::string(1, '\x07');
    named.push_back('\x01');
    named.push_back('\0');
    std::array<char, 40> stored_name{};
    std::memcpy(stored_name.data(), "Named Slot", 10);
    named.append(stored_name.data(), stored_name.size());
    write_slot("__picker_version_7_named__", named);
    EXPECT_EQ("Named Slot", get_saved_name("__picker_version_7_named__"));
}

// --- Local lobby my_team hoist ---

TEST(PickerFuncs, local_lobby_hoists_my_team_to_player_one)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    // Stash what this test mutates.
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> orig_list;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        orig_list[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char orig_size = save.team_size;
    const short orig_my_team = save.my_team;
    const unsigned char orig_numplayers = save.numplayers;
    const short orig_allied = save.allied_mode;

    // Roster: slot 0 on team 2, slot 1 on team 1. my_team = 1 must be hoisted
    // to the Player 1 seat (game.cpp's view_teams derivation), so the local
    // lobby's first peer — and the gameplay start team — is team 1, not the
    // first-slot team 2.
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "SlotZero";
    save.team_list[0]->teamnum = 2;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->name = "SlotOne";
    save.team_list[1]->teamnum = 1;
    save.team_size = 2;
    save.my_team = 1;
    save.numplayers = 2;
    save.allied_mode = 0;

    {
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        const auto config = client->build_game_start_config();
        ASSERT_TRUE(config.has_value());
        EXPECT_EQ(1, config->my_team)
            << "P1 must play my_team, not the first slot's team";
        client->shutdown();
    }

    // Restore.
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(orig_list[static_cast<std::size_t>(i)]);
    save.team_size = orig_size;
    save.my_team = orig_my_team;
    save.numplayers = orig_numplayers;
    save.allied_mode = orig_allied;
}

// Staged lobby (#218): the LOCAL client's honest preview-health arms — a
// solo stage refused at the wire cap (oversize completed-levels ledger)
// reports Failed with no staged world and no retained pair through the
// same IPickerLobbyClient surface the preview pane reads, then recovers to
// Staged when the ledger shrinks (the host-save digest moves the change
// key both ways).
TEST(PickerFuncs, local_lobby_stage_failure_reports_honest_preview_health)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    // Stash what this test mutates.
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> orig_list;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        orig_list[static_cast<std::size_t>(i)] =
            std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char orig_size = save.team_size;
    const short orig_my_team = save.my_team;
    const unsigned char orig_numplayers = save.numplayers;
    const short orig_allied = save.allied_mode;
    const std::string orig_campaign = save.current_campaign;
    const short orig_scen = save.scen_num;
    const auto orig_completed = save.completed_levels;

    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Solo";
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;
    save.current_campaign = "gladiator";
    save.scen_num = 1;

    {
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        using Health = og::ui::IPickerLobbyClient::StagedPreviewHealth;
        const auto poll_until = [&](Health wanted) {
            for (int i = 0; i < 200; ++i)
            {
                client->poll_and_apply();
                if (client->staged_preview_health() == wanted)
                    return true;
                SDL_Delay(20);
            }
            return false;
        };
        ASSERT_TRUE(poll_until(Health::Staged))
            << "the solo stage never staged";
        EXPECT_NE(nullptr, client->staged_world());
        EXPECT_NE(0u, client->stage_generation());

        std::set<int>& ledger = save.completed_levels["gladiator"];
        for (int level = 100'000; level < 117'000; ++level)
            ledger.insert(level);
        ASSERT_TRUE(poll_until(Health::Failed))
            << "the oversize restage never landed as Failed";
        EXPECT_EQ(nullptr, client->staged_world())
            << "a Failed stage presents no world to the pane";
        std::uint32_t pair_generation = 0;
        const std::vector<std::uint8_t>* setup_bytes = nullptr;
        const std::vector<std::uint8_t>* keyframe_bytes = nullptr;
        EXPECT_FALSE(client->staged_keyframe_bytes(
            pair_generation, setup_bytes, keyframe_bytes))
            << "a Failed stage retains no wire pair";

        ledger.clear();
        ASSERT_TRUE(poll_until(Health::Staged))
            << "the stage never recovered after the ledger shrank";
        EXPECT_NE(nullptr, client->staged_world());
        client->shutdown();
    }

    // Restore.
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] =
            std::move(orig_list[static_cast<std::size_t>(i)]);
    save.team_size = orig_size;
    save.my_team = orig_my_team;
    save.numplayers = orig_numplayers;
    save.allied_mode = orig_allied;
    save.current_campaign = orig_campaign;
    save.scen_num = orig_scen;
    save.completed_levels = orig_completed;
}

TEST(PickerFuncs,
     local_lobby_targets_exact_seats_and_preserves_explicit_teams)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    // Stash what this test mutates.
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> orig_list;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        orig_list[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char orig_size = save.team_size;
    const short orig_my_team = save.my_team;
    const unsigned char orig_numplayers = save.numplayers;
    const short orig_allied = save.allied_mode;
    const bool orig_start_requested = g_start_game_requested;

    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Red One";
    save.team_list[0]->teamnum = 0;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->name = "Red Two";
    save.team_list[1]->teamnum = 0;
    save.team_list[2] = std::make_unique<guy>(FAMILY_ARCHER);
    save.team_list[2]->name = "Blue One";
    save.team_list[2]->teamnum = 2;
    save.team_size = 3;
    save.my_team = 0;
    save.numplayers = 3;
    save.allied_mode = 0;
    g_start_game_requested = false;

    {
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        ActivePickerLobbyClientGuard active_client(client.get());
        EXPECT_FALSE(client->is_networked_session())
            << "local sessions are not networked";
        EXPECT_FALSE(client->local_ready());

        const std::optional<std::uint8_t> authoritative_mask =
            client->authoritative_team_mask();
        ASSERT_TRUE(authoritative_mask.has_value());
        EXPECT_EQ(authoritative_mask,
                  picker_lobby_authoritative_team_mask());
        EXPECT_TRUE(picker_lobby_status_lines().empty())
            << "the local lobby intentionally has no network status banner";

        // A request against this machine's FIRST seat updates
        // SaveData::my_team, and only after the authoritative echo lands.
        std::vector<og::sim::LobbyPlayer> players = client->lobby_players();
        const auto by_player_index = [](const auto& lhs, const auto& rhs) {
            return lhs.player_index < rhs.player_index;
        };
        std::sort(players.begin(), players.end(), by_player_index);
        ASSERT_EQ(3u, players.size());
        const std::uint8_t first_seat = players[0].player_index;
        EXPECT_TRUE(picker_lobby_request_seat_team_change(first_seat, 2));
        players = client->lobby_players();
        std::sort(players.begin(), players.end(), by_player_index);
        ASSERT_EQ(3u, players.size());
        EXPECT_EQ(2, players[0].team);
        EXPECT_EQ(2, save.my_team);
        EXPECT_TRUE(picker_lobby_request_seat_team_change(first_seat, 0));

        players = client->lobby_players();
        std::sort(players.begin(), players.end(),
                  [](const auto& lhs, const auto& rhs) {
                      return lhs.player_index < rhs.player_index;
                  });
        EXPECT_EQ(3u, players.size());
        if (players.size() == 3u)
        {
            const std::uint8_t p1 = players[0].player_index;
            const std::uint8_t p2 = players[1].player_index;

            // A seat assignment is independent of roster colors: Player 2 can
            // choose empty team 3, and changing it must not reseat Player 1 or
            // rewrite the compatibility my_team field.
            EXPECT_TRUE(picker_lobby_request_seat_team_change(p2, 3));
            players = client->lobby_players();
            std::sort(players.begin(), players.end(),
                      [](const auto& lhs, const auto& rhs) {
                          return lhs.player_index < rhs.player_index;
                      });
            EXPECT_EQ(0, players[0].team);
            EXPECT_EQ(3, players[1].team);
            EXPECT_EQ(0, save.my_team);

            const auto empty_team_config =
                client->build_game_start_config();
            ASSERT_TRUE(empty_team_config.has_value());
            EXPECT_EQ((std::vector<short>{0, 3, 1}),
                      empty_team_config->local_seat_teams)
                << "game-start configuration uses authored seat teams even "
                   "when one currently has no hero";

            const std::vector<short> before_invalid = {
                static_cast<short>(players[0].team),
                static_cast<short>(players[1].team),
                static_cast<short>(players[2].team),
            };
            EXPECT_FALSE(client->request_seat_team_change(0xffu, 2))
                << "an unknown global player id must not fall back to seat 0";
            EXPECT_EQ(before_invalid,
                      [&] {
                          std::vector<short> teams;
                          for (const auto& player : client->lobby_players())
                              teams.push_back(player.team);
                          return teams;
                      }());

            // Duplicate assignments are intentional: both local players may
            // control team 0. The second-seat request remains seat-targeted.
            EXPECT_TRUE(client->request_seat_team_change(p2, 0));
            players = client->lobby_players();
            std::sort(players.begin(), players.end(),
                      [](const auto& lhs, const auto& rhs) {
                          return lhs.player_index < rhs.player_index;
                      });
            EXPECT_EQ(0, players[0].team);
            EXPECT_EQ(0, players[1].team);
            EXPECT_EQ(1, players[2].team);
            EXPECT_EQ(p1, players[0].player_index);

            // Returning from a level re-declares the local seats and roster.
            // Authored duplicate teams survive, while every private save slot
            // is advertised exactly once (the first matching seat owns it).
            client->resume_after_level();
            players = client->lobby_players();
            std::sort(players.begin(), players.end(),
                      [](const auto& lhs, const auto& rhs) {
                          return lhs.player_index < rhs.player_index;
                      });
            ASSERT_EQ(3u, players.size());
            EXPECT_EQ(0, players[0].team);
            EXPECT_EQ(0, players[1].team);
            EXPECT_EQ(1, players[2].team);

            std::array<int, MAX_TEAM_SIZE> slot_counts{};
            std::size_t advertised_slots = 0;
            for (const auto& player : players)
            {
                for (const auto& slot : player.character_slots)
                {
                    ++advertised_slots;
                    if (slot.slot_index < slot_counts.size())
                        ++slot_counts[slot.slot_index];
                }
            }
            EXPECT_EQ(3u, advertised_slots);
            EXPECT_EQ(1, slot_counts[0]);
            EXPECT_EQ(1, slot_counts[1]);
            EXPECT_EQ(1, slot_counts[2]);
            EXPECT_TRUE(players[1].character_slots.empty())
                << "a duplicate matching seat must not duplicate team-0 "
                   "fighters";

            const auto resumed_config = client->build_game_start_config();
            ASSERT_TRUE(resumed_config.has_value());
            EXPECT_EQ((std::vector<short>{0, 0, 1}),
                      resumed_config->local_seat_teams);
            EXPECT_EQ(0, resumed_config->my_team);
            ASSERT_EQ(3u, resumed_config->save_data.team_list.size());
            std::array<int, MAX_TEAM_SIZE> config_slot_counts{};
            for (const auto& slot : resumed_config->save_data.team_list)
            {
                if (slot.slot_index < config_slot_counts.size())
                    ++config_slot_counts[slot.slot_index];
            }
            EXPECT_EQ(1, config_slot_counts[0]);
            EXPECT_EQ(1, config_slot_counts[1]);
            EXPECT_EQ(1, config_slot_counts[2]);
        }

        client->shutdown();
    }

    // Restore.
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(orig_list[static_cast<std::size_t>(i)]);
    save.team_size = orig_size;
    save.my_team = orig_my_team;
    save.numplayers = orig_numplayers;
    save.allied_mode = orig_allied;
    g_start_game_requested = orig_start_requested;
}

TEST(PickerFuncs,
     local_lobby_adds_and_removes_exact_seats_with_offline_last_seat_guard)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> original_roster;
    for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
        original_roster[static_cast<std::size_t>(slot)] = std::move(save.team_list[static_cast<std::size_t>(slot)]);
    const unsigned char original_team_size = save.team_size;
    const short original_my_team = save.my_team;
    const unsigned char original_numplayers = save.numplayers;
    const short original_allied_mode = save.allied_mode;

    for (auto& member : save.team_list)
        member.reset();
    for (int slot = 0; slot < 3; ++slot)
    {
        save.team_list[static_cast<std::size_t>(slot)] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[static_cast<std::size_t>(slot)]->name = std::format("Seat {}", slot + 1);
        save.team_list[static_cast<std::size_t>(slot)]->teamnum = static_cast<short>(slot);
    }
    save.team_size = 3;
    save.my_team = 0;
    save.numplayers = 3;
    save.allied_mode = 0;

    {
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        EXPECT_EQ(3u, client->local_seat_count());

        std::vector<og::sim::LobbyPlayer> players = client->lobby_players();
        ASSERT_EQ(3u, players.size());
        const og::sim::LobbyPlayer first = players[0];
        const og::sim::LobbyPlayer removed = players[1];
        const og::sim::LobbyPlayer third = players[2];

        EXPECT_TRUE(client->remove_local_seat(
            0xffu, removed.seat_id))
            << "the stable token, not a stale dense P#, targets the seat";
        EXPECT_EQ(2u, client->local_seat_count());
        players = client->lobby_players();
        ASSERT_EQ(2u, players.size());
        EXPECT_EQ(first.seat_id, players[0].seat_id);
        EXPECT_EQ(first.team, players[0].team);
        EXPECT_EQ(third.seat_id, players[1].seat_id);
        EXPECT_EQ(third.team, players[1].team);
        EXPECT_EQ(1u, players[1].player_index)
            << "the surviving third seat gets the dense P2 display index";
        EXPECT_FALSE(client->remove_local_seat(
            removed.player_index, removed.seat_id))
            << "the removed stable token must never alias the new P2";

        EXPECT_TRUE(client->add_local_seat());
        EXPECT_EQ(3u, client->local_seat_count());
        players = client->lobby_players();
        ASSERT_EQ(3u, players.size());
        EXPECT_EQ(first.seat_id, players[0].seat_id);
        EXPECT_EQ(third.seat_id, players[1].seat_id);
        EXPECT_NE(removed.seat_id, players[2].seat_id);
        EXPECT_TRUE(client->add_local_seat());
        EXPECT_EQ(4u, client->local_seat_count());
        EXPECT_FALSE(client->add_local_seat())
            << "one machine cannot exceed MAX_PLAYERS local controls";

        client->set_player_mode(1);
        players = client->lobby_players();
        ASSERT_EQ(1u, players.size());
        EXPECT_FALSE(client->remove_local_seat(
            players.front().player_index, players.front().seat_id))
            << "offline play keeps one active seat";
        EXPECT_EQ(1u, client->local_seat_count());

        // The legacy spectator mode still exists. [+] activates its existing
        // placeholder instead of creating a second lobby player.
        client->set_player_mode(0);
        EXPECT_EQ(0u, client->local_seat_count());
        ASSERT_EQ(1u, client->lobby_players().size());
        EXPECT_TRUE(client->add_local_seat());
        EXPECT_EQ(1u, client->local_seat_count());
        EXPECT_EQ(1u, client->lobby_players().size());
        client->shutdown();
    }

    for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
        save.team_list[static_cast<std::size_t>(slot)] = std::move(original_roster[static_cast<std::size_t>(slot)]);
    save.team_size = original_team_size;
    save.my_team = original_my_team;
    save.numplayers = original_numplayers;
    save.allied_mode = original_allied_mode;
}

TEST(PickerFuncs, local_lobby_fill_leaves_seats_and_sparse_domains_reconcile)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> original_roster;
    for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
        original_roster[static_cast<std::size_t>(slot)] = std::move(save.team_list[static_cast<std::size_t>(slot)]);
    const unsigned char original_team_size = save.team_size;
    const short original_my_team = save.my_team;
    const unsigned char original_numplayers = save.numplayers;
    const short original_allied_mode = save.allied_mode;
    const std::string original_campaign = save.current_campaign;
    const short original_scenario = save.scen_num;
    const std::array<short, 4> original_bot_squad = save.fill;
    const std::string original_mount = get_mounted_campaign();
    const int original_world_id = world.id;
    const char original_world_type = world.type;
    const std::size_t original_ob_size = world.oblist.size();
    std::vector<std::pair<walker*, unsigned char>> original_marker_teams;
    for (const auto& ob : world.oblist)
    {
        if (ob != nullptr &&
            ob->query_order() == Order::Special &&
            ob->family() == FAMILY_RESERVED_TEAM)
        {
            original_marker_teams.emplace_back(ob.get(), ob->team_num());
            ob->set_team_num(SCORE_TEAM_COUNT);
        }
    }

    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Host";
    save.team_list[0]->teamnum = 0;
    save.team_list[1] = std::make_unique<guy>(FAMILY_ARCHER);
    save.team_list[1]->name = "Guest";
    save.team_list[1]->teamnum = 3;
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 2;
    save.allied_mode = 0;
    save.current_campaign = "modes";
    save.scen_num = 7;
    save.fill = {};
    // A REAL mount: the versus predicate reads the campaign's matchup: yaml
    // key through the mounted package (a faked mount id would read another
    // campaign's yaml).
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(save.current_campaign));
    world.id = save.scen_num;
    world.type |= GameWorld::TYPE_SCRIPTED;
    for (const short team : {short{0}, short{2}, short{3}})
    {
        walker* marker = world.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        ASSERT_NE(nullptr, marker);
        marker->set_team_num(static_cast<unsigned char>(team));
    }

    {
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        std::vector<og::sim::LobbyPlayer> players = client->lobby_players();
        ASSERT_EQ(2u, players.size());
        EXPECT_EQ(3, players[1].team);

        // B8: nothing the band holds narrows the seat domain. The host sets
        // FILL: NONE on team 4 — the very team the second seat is sitting
        // on — and the settings echo re-resolves every seat against the
        // UNCHANGED authored mask {0,2,3}: the seat stays where it sits.
        // (The A2-era OFF value, which DID narrow the domain, is gone.)
        save.fill[3] = og::sim::kFillNone;
        client->sync_settings_from_save();
        players = client->lobby_players();
        ASSERT_EQ(2u, players.size());
        EXPECT_EQ(3, players[1].team);
        // Per-seat reassignment follows the authored mask exactly: 2 is
        // authored, 1 is not, and the NONE team stays perfectly seatable.
        EXPECT_TRUE(client->request_seat_team_change(
            players[1].player_index, 2));
        EXPECT_FALSE(client->request_seat_team_change(
            players[1].player_index, 1));
        EXPECT_TRUE(client->request_seat_team_change(
            players[1].player_index, 3));
        players = client->lobby_players();
        ASSERT_EQ(2u, players.size());
        EXPECT_EQ(3, players[1].team);

        // A next level with sparse authored {1,3} invalidates the host's
        // team-0 assignment; the client adopts the server's deterministic
        // team 1 normalization without recoloring the saved heroes.
        while (world.oblist.size() > original_ob_size)
            world.oblist.pop_back();
        walker* marker1 = world.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        walker* marker3 = world.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        ASSERT_NE(nullptr, marker1);
        ASSERT_NE(nullptr, marker3);
        marker1->set_team_num(1);
        marker3->set_team_num(3);
        save.fill[3] = og::sim::kFillFair;
        client->sync_settings_from_save();
        players = client->lobby_players();
        ASSERT_EQ(2u, players.size());
        EXPECT_EQ(1, players[0].team);
        EXPECT_EQ(3, players[1].team)
            << "team 4 is still authored, so that seat never moved";
        EXPECT_EQ(0, save.team_list[0]->teamnum);
        EXPECT_EQ(3, save.team_list[1]->teamnum);
        client->shutdown();
    }

    for (const auto& [marker, team] : original_marker_teams)
        marker->set_team_num(team);
    while (world.oblist.size() > original_ob_size)
        world.oblist.pop_back();
    world.id = original_world_id;
    world.type = original_world_type;
    if (!original_mount.empty())
        (void)mount_campaign_package_with_error(original_mount);
    for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
        save.team_list[static_cast<std::size_t>(slot)] = std::move(original_roster[static_cast<std::size_t>(slot)]);
    save.team_size = original_team_size;
    save.my_team = original_my_team;
    save.numplayers = original_numplayers;
    save.allied_mode = original_allied_mode;
    save.current_campaign = original_campaign;
    save.scen_num = original_scenario;
    save.fill = original_bot_squad;
}

namespace {

// RAII capture of the long-lived session state the local-lobby seat tests
// mutate: the real roster (moved OUT of the save — losing it would delete
// the session roster for every later test), the save's lobby fields, the
// campaign mount, the world identity and the pre-existing team markers
// (parked outside the score range for the test's duration). Restoration
// runs on EVERY exit path, so a fatal ASSERT mid-test cannot leak any of
// it into the rest of the binary. The oblist restore pops entries past the
// captured size, which is sound because GameWorld::add_ob APPENDS
// (add_to_list with atstart = false) and the tests only add, never remove.
struct LocalLobbySessionGuard
{
    SaveData& save;
    GameWorld& world;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> roster;
    unsigned char team_size;
    short my_team;
    unsigned char numplayers;
    short allied_mode;
    std::string campaign;
    short scen_num;
    short ctf_team_count;
    std::string mount;
    int world_id;
    char world_type;
    std::size_t ob_size;
    std::vector<std::pair<walker*, unsigned char>> marker_teams;

    LocalLobbySessionGuard(SaveData& s, GameWorld& w)
        : save(s), world(w), team_size(s.team_size), my_team(s.my_team),
          numplayers(s.numplayers), allied_mode(s.allied_mode),
          campaign(s.current_campaign), scen_num(s.scen_num),
          ctf_team_count(s.ctf_team_count), mount(get_mounted_campaign()),
          world_id(w.id), world_type(w.type), ob_size(w.oblist.size())
    {
        for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
            roster[static_cast<std::size_t>(slot)] =
                std::move(save.team_list[static_cast<std::size_t>(slot)]);
        for (const auto& ob : world.oblist)
        {
            if (ob != nullptr && ob->query_order() == Order::Special &&
                ob->family() == FAMILY_RESERVED_TEAM)
            {
                marker_teams.emplace_back(ob.get(), ob->team_num());
                ob->set_team_num(SCORE_TEAM_COUNT);
            }
        }
    }

    ~LocalLobbySessionGuard()
    {
        for (const auto& [marker, team] : marker_teams)
            marker->set_team_num(team);
        while (world.oblist.size() > ob_size)
            world.oblist.pop_back();
        world.id = world_id;
        world.type = world_type;
        if (!mount.empty())
            (void)mount_campaign_package_with_error(mount);
        for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
            save.team_list[static_cast<std::size_t>(slot)] =
                std::move(roster[static_cast<std::size_t>(slot)]);
        save.team_size = team_size;
        save.my_team = my_team;
        save.numplayers = numplayers;
        save.allied_mode = allied_mode;
        save.current_campaign = campaign;
        save.scen_num = scen_num;
        save.ctf_team_count = ctf_team_count;
    }

    LocalLobbySessionGuard(const LocalLobbySessionGuard&) = delete;
    LocalLobbySessionGuard& operator=(const LocalLobbySessionGuard&) = delete;
};

}  // namespace

// TROOPS is inert since amendment B5: a legacy save carrying the retired
// kTroopsMatched sentinel heals to 0 in the lobby's sanitize (the D30
// one-time migration), the field feeds no mask anywhere, so the settings
// echo re-resolves every seat against an UNCHANGED domain — no seat moves
// — and per-seat reassignment obeys the same authored-mask rule as before.
TEST(PickerFuncs, local_lobby_retired_troops_value_heals_and_moves_no_seats)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    LocalLobbySessionGuard guard(save, world);

    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Host";
    save.team_list[0]->teamnum = 0;
    save.team_list[1] = std::make_unique<guy>(FAMILY_ARCHER);
    save.team_list[1]->name = "Guest";
    save.team_list[1]->teamnum = 3;
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 2;
    save.allied_mode = 0;
    save.current_campaign = "modes";
    save.scen_num = 7;
    save.ctf_team_count = 0;
    // A REAL mount: the versus predicate reads the campaign's matchup: yaml
    // key through the mounted package.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(save.current_campaign));
    world.id = save.scen_num;
    world.type |= GameWorld::TYPE_SCRIPTED;
    for (const short team : {short{0}, short{2}, short{3}})
    {
        walker* marker = world.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        ASSERT_NE(nullptr, marker);
        marker->set_team_num(static_cast<unsigned char>(team));
    }

    {
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        // Auto over authored {0,2,3}: both saved seats are valid.
        std::vector<og::sim::LobbyPlayer> players = client->lobby_players();
        ASSERT_EQ(2u, players.size());
        EXPECT_EQ(0, players[0].team);
        EXPECT_EQ(3, players[1].team);

        // The retired sentinel rides in: the field feeds no mask, so the
        // settings echo moves no seats — and the sanitize heals the value
        // to 0 (B5: TROOPS is inert on disk and on the wire).
        save.ctf_strip_scenario_troops = og::sim::kTroopsMatched;
        client->sync_settings_from_save();
        players = client->lobby_players();
        ASSERT_EQ(2u, players.size());
        EXPECT_EQ(0, players[0].team);
        EXPECT_EQ(3, players[1].team);
        EXPECT_EQ(0, save.ctf_strip_scenario_troops)
            << "the one-time heal: every retired TROOPS value reads 0 back";

        // Seat changes follow the authored mask exactly as before: 2 is
        // authored, 1 is not.
        EXPECT_TRUE(client->request_seat_team_change(
            players[1].player_index, 2));
        EXPECT_FALSE(client->request_seat_team_change(
            players[1].player_index, 1));
        players = client->lobby_players();
        ASSERT_EQ(2u, players.size());
        EXPECT_EQ(2, players[1].team);

        // A second sync at the healed value: same domain again, so still
        // no seat movement.
        save.ctf_strip_scenario_troops = 0;
        client->sync_settings_from_save();
        players = client->lobby_players();
        ASSERT_EQ(2u, players.size());
        EXPECT_EQ(0, players[0].team);
        EXPECT_EQ(2, players[1].team);
        client->shutdown();
    }
    // Session state restores via the guard on every exit path.
}

TEST(PickerFuncs, local_lobby_start_preserves_all_team_colors_in_both_modes)
{
    picker_lobby_shutdown();
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> orig_list;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        orig_list[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char orig_size = save.team_size;
    const short orig_my_team = save.my_team;
    const unsigned char orig_numplayers = save.numplayers;
    const short orig_allied = save.allied_mode;

    for (const short allied_mode : {short{0}, short{1}})
    {
        for (short team = 0; team < 4; ++team)
        {
            for (auto& member : save.team_list)
                member.reset();
            save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
            save.team_list[0]->name = "Color Guard";
            save.team_list[0]->teamnum = team;
            save.team_size = 1;
            save.my_team = team;
            save.numplayers = 1;
            save.allied_mode = allied_mode;

            auto client = og::ui::create_local_picker_lobby_client();
            client->initialize_from_save();
            const auto config = client->build_game_start_config();
            ASSERT_TRUE(config.has_value());
            ASSERT_EQ(1u, config->save_data.team_list.size());
            EXPECT_FALSE(config->is_networked);
            EXPECT_EQ(team, config->my_team)
                << "allied=" << allied_mode << " team=" << team;
            EXPECT_EQ(team, config->save_data.team_list[0].character.teamnum)
                << "allied=" << allied_mode << " team=" << team;
            ASSERT_EQ(1u, config->local_seat_teams.size());
            EXPECT_EQ(team, config->local_seat_teams[0])
                << "allied=" << allied_mode << " team=" << team;
            client->shutdown();
        }
    }

    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(orig_list[static_cast<std::size_t>(i)]);
    save.team_size = orig_size;
    save.my_team = orig_my_team;
    save.numplayers = orig_numplayers;
    save.allied_mode = orig_allied;
}

TEST(PickerFuncs, local_lobby_rejects_every_uncontrollable_seat_shape)
{
    picker_lobby_shutdown();
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> orig_list;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        orig_list[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char orig_size = save.team_size;
    const short orig_my_team = save.my_team;
    const unsigned char orig_numplayers = save.numplayers;
    const short orig_allied = save.allied_mode;
    const bool orig_start_requested = g_start_game_requested;

    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Red One";
    save.team_list[0]->teamnum = 0;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->name = "Second";
    save.team_list[1]->teamnum = 0;
    save.team_list[1]->deployed = false;
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 2;
    save.allied_mode = 1;

    {
        g_start_game_requested = false;
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        EXPECT_FALSE(client->request_start_game())
            << "two Together seats require two deployed fighters on their "
               "shared control team";
        save.team_list[1]->deployed = true;
        client->sync_roster_from_save();
        EXPECT_TRUE(client->request_start_game());
        client->shutdown();
    }

    // Split seats require a deployed fighter for each derived team. A yellow
    // reserve must not count merely because it still exists in the company.
    save.allied_mode = 0;
    save.team_list[1]->teamnum = 1;
    save.team_list[1]->deployed = false;
    {
        g_start_game_requested = false;
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        EXPECT_FALSE(client->request_start_game());
        save.team_list[1]->deployed = true;
        client->sync_roster_from_save();
        EXPECT_TRUE(client->request_start_game());
        client->shutdown();
    }

    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(orig_list[static_cast<std::size_t>(i)]);
    save.team_size = orig_size;
    save.my_team = orig_my_team;
    save.numplayers = orig_numplayers;
    save.allied_mode = orig_allied;
    g_start_game_requested = orig_start_requested;
}

// GTL v16: campaign_tag has no LobbyCharacterData field, so the local lobby
// round-trip must re-stamp it from the private save slot it overwrites, the
// way it already re-stamps `deployed` from the slot. This is a SOLO path —
// the standalone local client exists as soon as Base Camp syncs — and the
// Base Camp mutation tail autosaves right after the sync, so a lost tag is
// lost on disk too.
TEST(PickerFuncs, local_lobby_round_trip_preserves_campaign_tags)
{
    picker_lobby_shutdown();
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> orig_list;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        orig_list[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char orig_size = save.team_size;
    const short orig_my_team = save.my_team;
    const unsigned char orig_numplayers = save.numplayers;
    const short orig_allied = save.allied_mode;

    for (auto& member : save.team_list)
        member.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Sworn";
    save.team_list[0]->teamnum = 0;
    save.team_list[0]->deployed = true;
    save.team_list[0]->campaign_tag = 1;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->name = "Benched";
    save.team_list[1]->teamnum = 0;
    save.team_list[1]->deployed = false;
    save.team_list[1]->campaign_tag = 3;
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 1;

    // The Base Camp roster-mutation tail: sync the lobby, then autosave.
    picker_lobby_sync_roster_from_save();

    ASSERT_TRUE(save.team_list[0] != nullptr);
    ASSERT_TRUE(save.team_list[1] != nullptr);
    EXPECT_EQ(1, static_cast<int>(save.team_list[0]->campaign_tag))
        << "a deployed hero must keep its assignment through the sync";
    EXPECT_EQ(3, static_cast<int>(save.team_list[1]->campaign_tag))
        << "a benched hero must keep its assignment through the sync";
    // Pinned alongside the tag: the two re-stamps share one mechanism, so a
    // rebuild that drops either is the same bug.
    EXPECT_TRUE(save.team_list[0]->deployed);
    EXPECT_FALSE(save.team_list[1]->deployed);

    // The assign-chip shape: the provider writes the tag into the live
    // roster, then an ordinary menu frame polls the lobby.
    save.team_list[0]->campaign_tag = 5;
    picker_lobby_poll();
    EXPECT_EQ(5, static_cast<int>(save.team_list[0]->campaign_tag))
        << "a just-written assignment must survive the next menu frame";
    EXPECT_EQ(3, static_cast<int>(save.team_list[1]->campaign_tag));

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(orig_list[static_cast<std::size_t>(i)]);
    save.team_size = orig_size;
    save.my_team = orig_my_team;
    save.numplayers = orig_numplayers;
    save.allied_mode = orig_allied;
}

// Player seats are control assignments, not roster filters. A one-player
// local mission must carry deployed and benched members from every company
// color into its isolated start seed, preserving their original private slots.
TEST(PickerFuncs, local_lobby_one_player_start_carries_the_full_mixed_team_roster)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> orig_list;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        orig_list[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char orig_size = save.team_size;
    const short orig_my_team = save.my_team;
    const unsigned char orig_numplayers = save.numplayers;
    const short orig_allied = save.allied_mode;

    constexpr std::array<short, 4> teams = {0, 1, 2, 3};
    for (std::size_t index = 0; index < teams.size(); ++index)
    {
        save.team_list[index] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[index]->name = std::format("Color {}", index + 1);
        save.team_list[index]->teamnum = teams[index];
        save.team_list[index]->deployed = index != 2;
    }
    save.team_size = static_cast<unsigned char>(teams.size());
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;

    {
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        const auto config = client->build_game_start_config();
        ASSERT_TRUE(config.has_value());
        ASSERT_EQ(teams.size(), config->save_data.team_list.size());
        for (std::size_t index = 0; index < teams.size(); ++index)
        {
            const auto& slot = config->save_data.team_list[index];
            EXPECT_EQ(index, slot.slot_index);
            EXPECT_EQ(teams[index], slot.character.teamnum);
            EXPECT_EQ(index != 2, slot.deployed);
            EXPECT_EQ(0u, slot.owner_player_index);
            EXPECT_EQ(index, slot.owner_save_slot);
        }
        client->shutdown();
    }

    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(orig_list[static_cast<std::size_t>(i)]);
    save.team_size = orig_size;
    save.my_team = orig_my_team;
    save.numplayers = orig_numplayers;
    save.allied_mode = orig_allied;
}

// §4.2: the LOCAL lobby round-trips the machine's own roster through the
// lobby state on every sync. A benched member must survive that round-trip
// (flag intact, never force-redeployed, never dropped) and must still ride
// the game-start equivalent — the local start path seeds the real company
// save from it, so dropping benched members would be save data loss. The
// local benched member is kept OUT of the level at spawn time instead.
TEST(PickerFuncs, local_lobby_roundtrip_preserves_benched_members)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    // Stash what this test mutates.
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> orig_list;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        orig_list[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char orig_size = save.team_size;
    const short orig_my_team = save.my_team;
    const unsigned char orig_numplayers = save.numplayers;
    const short orig_allied = save.allied_mode;

    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Front";
    save.team_list[0]->teamnum = 0;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->name = "Reserve";
    save.team_list[1]->teamnum = 0;
    save.team_list[1]->deployed = false;
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;

    {
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        // Drive a second sync (what the picker loop does every frame): the
        // round-trip rebuild must keep the benched member benched.
        client->sync_roster_from_save();

        ASSERT_NE(nullptr, save.team_list[0]);
        ASSERT_NE(nullptr, save.team_list[1]);
        EXPECT_TRUE(save.team_list[0]->deployed);
        EXPECT_FALSE(save.team_list[1]->deployed)
            << "the lobby echo must not force-redeploy a benched member";
        EXPECT_EQ("Reserve", save.team_list[1]->name);

        const auto config = client->build_game_start_config();
        ASSERT_TRUE(config.has_value());
        ASSERT_EQ(2u, config->save_data.team_list.size())
            << "the LOCAL equivalent keeps benched slots (save-seed safety)";
        EXPECT_TRUE(config->save_data.team_list[0].deployed);
        EXPECT_FALSE(config->save_data.team_list[1].deployed);
        client->shutdown();
    }

    // Restore.
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(orig_list[static_cast<std::size_t>(i)]);
    save.team_size = orig_size;
    save.my_team = orig_my_team;
    save.numplayers = orig_numplayers;
    save.allied_mode = orig_allied;
}

// ---------------------------------------------------------------------------
// Sprite sheet picker tests
// ---------------------------------------------------------------------------

class SpriteSheetPicker : public ::testing::Test {
protected:
    std::string orig_setting_;

    void SetUp() override {
        orig_setting_ = cfg.get_setting("graphics", "sprite_sheet");
        cfg.apply_setting("graphics", "sprite_sheet", "");
        apply_sprite_sheet_setting();
    }

    void TearDown() override {
        cfg.apply_setting("graphics", "sprite_sheet", orig_setting_);
        apply_sprite_sheet_setting();
    }
};

namespace {

static int picker_spritesheet_select_first_pack_injector(void*)
{
    og::runtime::ensure_thread_session();
    SDL_Delay(200);

    // Row 0 is "Standard"; row 1 is the first sorted pack.
    inject_mouse_click(80, 56, 60);
    SDL_Delay(120);
    inject_mouse_click(25, 185, 60);
    return 0;
}

} // namespace


TEST_F(SpriteSheetPicker, config_round_trip)
{
    cfg.apply_setting("graphics", "sprite_sheet", "my-pack");
    ASSERT_EQ("my-pack", cfg.get_setting("graphics", "sprite_sheet"));

    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_EQ("", cfg.get_setting("graphics", "sprite_sheet"));
}


TEST_F(SpriteSheetPicker, do_pick_spritesheet_selects_visible_pack)
{
    const char* config_dir = std::getenv("OPENGLAD_CONFIG_DIR");
    ASSERT_TRUE(config_dir != nullptr) << "OPENGLAD_CONFIG_DIR must be set by the test harness";

    namespace fs = std::filesystem;
    const fs::path pack_dir =
        fs::path(config_dir) / "extra_pix" / "000_picker_pack";
    std::error_code ec;
    fs::remove_all(pack_dir, ec);
    fs::create_directories(pack_dir, ec);

    prepare_picker_mouse();
    SDL_Thread* th = SDL_CreateThread(
        picker_spritesheet_select_first_pack_injector,
        "picker_sprite_sheet",
        nullptr);
    ASSERT_TRUE(th != nullptr) << "injector thread started";

    vbutton dispatcher;
    const Sint32 result = dispatcher.do_call(
        button_action_id(ButtonAction::PickSpriteSheet), 0);

    int code = 0;
    if (th)
        SDL_WaitThread(th, &code);
    clear_events();

    ASSERT_EQ(2, result) << "sprite sheet picker should redraw after selection";
    ASSERT_EQ("000_picker_pack", cfg.get_setting("graphics", "sprite_sheet"))
        << "clicking the first visible pack should apply that pack";

    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_TRUE(apply_sprite_sheet_setting());
    fs::remove_all(pack_dir, ec);
}
