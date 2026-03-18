#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <gtest/gtest.h>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
Sint32 do_save(Sint32 arg1);
Sint32 do_load(Sint32 arg1);
Sint32 delete_all();
void quit(Sint32 arg1);
Sint32 return_menu(Sint32 arg);
Sint32 name_guy(Sint32 arg);
Sint32 edit_guy(Sint32 arg1);
void picker_lobby_initialize_from_save();
void picker_lobby_sync_from_save();
void picker_reinitialize_lobby_after_game();
void picker_lobby_shutdown();
bool picker_lobby_request_start();
bool picker_lobby_start_request_pending();
bool picker_replace_lobby_client(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client,
    std::unique_ptr<og::ui::IPickerLobbyClient> next_client,
    const char* popup_title);
bool picker_join_game(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);
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

    std::optional<og::ui::PickerLobbyGameStartConfig> pending_config;
    int consume_calls = 0;
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

struct PickerLobbyClientTrace
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

    [[nodiscard]] std::vector<std::string> status_lines() const override
    {
        return status_lines_;
    }

    std::shared_ptr<PickerLobbyClientTrace> trace_;
    std::vector<std::string> status_lines_;
    bool throw_on_initialize = false;
};

} // namespace

#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>

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
        orig_list[i] = og::runtime::current_session->myscreen_->save_data.team_list[i].release();
        og::runtime::current_session->myscreen_->save_data.team_list[i].reset(nullptr);
    }

    Sint32 count = how_many(FAMILY_SOLDIER);
    ASSERT_EQ(0, (int)count) << "empty team should have 0 of any family";

    // Restore
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        og::runtime::current_session->myscreen_->save_data.team_list[i].reset(orig_list[i]);
    }
    og::runtime::current_session->myscreen_->save_data.team_size = orig_size;
}


TEST(PickerFuncs, how_many_with_team)
{
    // Save originals
    const unsigned char orig_size = og::runtime::current_session->myscreen_->save_data.team_size;
    guy* orig_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        orig_list[i] = og::runtime::current_session->myscreen_->save_data.team_list[i].release();
        og::runtime::current_session->myscreen_->save_data.team_list[i].reset(nullptr);
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
        og::runtime::current_session->myscreen_->save_data.team_list[i].reset(orig_list[i]);
    }
    og::runtime::current_session->myscreen_->save_data.team_size = orig_size;

    // Additional picker utility/state coverage without registering new tests.
    ASSERT_EQ(-1, get_scen_num_from_filename(nullptr)) << "null input should return -1";
    ASSERT_EQ(-1, get_scen_num_from_filename("scen")) << "no numeric suffix should return -1";
    ASSERT_EQ(123, get_scen_num_from_filename("scen123")) << "numeric suffix should parse";
    ASSERT_EQ(42, get_scen_num_from_filename("file42")) << "mixed prefix should parse trailing number";

    vbutton* old2 = og::runtime::current_session->allbuttons_[2];
    vbutton* old6 = og::runtime::current_session->allbuttons_[6];
    vbutton* old7 = og::runtime::current_session->allbuttons_[7];
    vbutton* old18 = og::runtime::current_session->allbuttons_[18];
    std::unique_ptr<guy> old_current = std::move(og::runtime::current_session->current_guy_);
    short old_team_num = og::runtime::current_session->current_team_num_;
    Sint32 old_diff = og::runtime::current_session->current_difficulty_;
    const short old_allied = og::runtime::current_session->myscreen_->save_data.allied_mode;

    og::runtime::current_session->allbuttons_[2] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b2", KEYSTATE_UNKNOWN);
    og::runtime::current_session->allbuttons_[6] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b6", KEYSTATE_UNKNOWN);
    og::runtime::current_session->allbuttons_[7] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b7", KEYSTATE_UNKNOWN);
    og::runtime::current_session->allbuttons_[18] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b18", KEYSTATE_UNKNOWN);

    og::runtime::current_session->current_guy_ = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->current_guy_->teamnum = 1;
    og::runtime::current_session->current_team_num_ = 0;
    og::runtime::current_session->current_difficulty_ = DIFFICULTY_SETTINGS - 1;
    og::runtime::current_session->myscreen_->save_data.allied_mode = static_cast<short>(0);

    ASSERT_EQ(4, (int)set_difficulty()) << "set_difficulty should return OK";
    ASSERT_TRUE(std::string(og::runtime::current_session->allbuttons_[2]->label).find("Difficulty: ") == 0 ||
        std::string(og::runtime::current_session->allbuttons_[6]->label).find("Difficulty: ") == 0) << "difficulty label should be updated";

    ASSERT_EQ(4, (int)change_teamnum(1)) << "change_teamnum should return OK";
    ASSERT_EQ(2, (int)og::runtime::current_session->current_guy_->teamnum) << "team should increment";
    ASSERT_TRUE(std::string(og::runtime::current_session->allbuttons_[18]->label).find("Playing on Team ") == 0) << "team label should be updated";

    og::runtime::current_session->current_team_num_ = 0;
    ASSERT_EQ(4, (int)change_hire_teamnum(1)) << "change_hire_teamnum should return OK";
    ASSERT_EQ(1, (int)og::runtime::current_session->current_team_num_) << "hire team num should increment";
    ASSERT_EQ(1, (int)og::runtime::current_session->current_guy_->teamnum) << "current guy team should mirror hire team";
    ASSERT_TRUE(std::string(og::runtime::current_session->allbuttons_[2]->label).find("Hiring for Team ") == 0) << "hire label should be updated";

    ASSERT_EQ(4, (int)change_allied()) << "change_allied should return OK";
    ASSERT_EQ(1, og::runtime::current_session->myscreen_->save_data.allied_mode) << "allied mode should toggle on";
    ASSERT_TRUE(og::runtime::current_session->allbuttons_[7]->label == "PVP: Ally") << "allied label should update";
    ASSERT_EQ(4, (int)change_allied()) << "change_allied second toggle should return OK";
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.allied_mode) << "allied mode should toggle off";
    ASSERT_TRUE(og::runtime::current_session->allbuttons_[7]->label == "PVP: Enemy") << "enemy label should update";

    // Directly exercise picker helpers that were still uncovered.
    const unsigned char saved_team_size = og::runtime::current_session->myscreen_->save_data.team_size;
    guy* saved_team_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        saved_team_list[i] = og::runtime::current_session->myscreen_->save_data.team_list[i].release();
        og::runtime::current_session->myscreen_->save_data.team_list[i].reset(nullptr);
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
    Sint32 save_ret = do_save(1);
    Sint32 load_ret = do_load(1);
    ASSERT_TRUE(save_ret == load_ret) << "do_save/do_load should return same menu status";

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

    delete og::runtime::current_session->allbuttons_[2];
    delete og::runtime::current_session->allbuttons_[6];
    delete og::runtime::current_session->allbuttons_[7];
    delete og::runtime::current_session->allbuttons_[18];
    if (old0 == nullptr) {
        delete og::runtime::current_session->allbuttons_[0];
    }
    og::runtime::current_session->allbuttons_[0] = old0;
    og::runtime::current_session->allbuttons_[2] = old2;
    og::runtime::current_session->allbuttons_[6] = old6;
    og::runtime::current_session->allbuttons_[7] = old7;
    og::runtime::current_session->allbuttons_[18] = old18;

    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        og::runtime::current_session->myscreen_->save_data.team_list[i].reset(saved_team_list[i]);
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
                .current_campaign = "org.openglad.gladiator",
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

TEST(PickerFuncs, picker_replace_lobby_client_is_transactional_on_initialize_failure)
{
    auto current_trace = std::make_shared<PickerLobbyClientTrace>();
    std::unique_ptr<og::ui::IPickerLobbyClient> current_client =
        std::make_unique<TraceablePickerLobbyClient>(
        current_trace);
    auto* const current_raw =
        static_cast<TraceablePickerLobbyClient*>(current_client.get());
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
    EXPECT_EQ(0, current_trace->shutdown_calls);
    EXPECT_EQ(1, next_trace->initialize_calls);
}

TEST(PickerFuncs, picker_replace_lobby_client_swaps_active_client_after_success)
{
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
        {"Relay: GLAD-XKCD"};
    auto* const next_raw =
        static_cast<TraceablePickerLobbyClient*>(next_client.get());

    ASSERT_TRUE(picker_replace_lobby_client(current_client,
                                            std::move(next_client),
                                            "HOST GAME"));
    EXPECT_EQ(next_raw, current_client.get());
    EXPECT_EQ(next_raw, og::ui::active_picker_lobby_client());
    EXPECT_EQ(1, current_trace->shutdown_calls);
    EXPECT_EQ(1, next_trace->initialize_calls);
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

TEST(PickerFuncs, lobby_sync_preserves_sparse_team_assignments)
{
    picker_lobby_shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    const unsigned char old_numplayers = save.numplayers;
    std::unique_ptr<guy> old_team[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        old_team[i] = std::move(save.team_list[i]);

    save.team_size = 2;
    save.numplayers = 2;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 1;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->teamnum = 3;

    picker_lobby_initialize_from_save();
    picker_lobby_sync_from_save();

    ASSERT_EQ(4, static_cast<int>(set_player_mode(2)));
    EXPECT_EQ(2, static_cast<int>(save.numplayers));
    ASSERT_TRUE(save.team_list[0] && save.team_list[1]);
    EXPECT_EQ(1, static_cast<int>(save.team_list[0]->teamnum));
    EXPECT_EQ(3, static_cast<int>(save.team_list[1]->teamnum));

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[i] = std::move(old_team[i]);
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
        old_team[i] = std::move(save.team_list[i]);

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
        save.team_list[i] = std::move(old_team[i]);
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
        old_team[i] = std::move(save.team_list[i]);

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
        save.team_list[i] = std::move(old_team[i]);
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
        old_team[i] = std::move(save.team_list[i]);

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
        save.team_list[i] = std::move(old_team[i]);
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
        old_team[i] = std::move(save.team_list[i]);

    save.current_campaign = "org.openglad.gladiator";
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
    EXPECT_EQ("org.openglad.gladiator", config->save_data.current_campaign);
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
        save.team_list[i] = std::move(old_team[i]);
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
        old_team[i] = std::move(save.team_list[i]);

    save.current_campaign = "org.openglad.gladiator";
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
    EXPECT_EQ("org.openglad.gladiator", config->save_data.current_campaign);
    EXPECT_EQ(1, config->save_data.scen_num);
    EXPECT_EQ(1, config->save_data.allied_mode);
    EXPECT_EQ(4, config->difficulty);
    ASSERT_EQ(1u, config->save_data.team_list.size());
    EXPECT_EQ("Spectator", config->save_data.team_list[0].character.name);
    EXPECT_EQ(0, config->save_data.team_list[0].character.teamnum);
    EXPECT_FALSE(picker_lobby_consume_game_start_config().has_value());

    picker_lobby_shutdown();
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[i] = std::move(old_team[i]);
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
        old_team[i] = std::move(save.team_list[i]);

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
        save.team_list[i] = std::move(old_team[i]);
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
        old_team[i] = std::move(save.team_list[i]);
    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
#ifdef TESTING
    const bool old_remove_exits = g_test_remove_exits;
    const std::int32_t old_tick_limit = og::sim::g_test_level_tick_limit_override;
#endif

    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
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
        save.team_list[i] = std::move(old_team[i]);
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
        old_team[i] = std::move(save.team_list[i]);
    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
#ifdef TESTING
    const bool old_remove_exits = g_test_remove_exits;
    const std::int32_t old_tick_limit = og::sim::g_test_level_tick_limit_override;
#endif

    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
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
        save.team_list[i] = std::move(old_team[i]);
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

TEST(PickerFuncs, train_team_change_persists_after_accept)
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
        old_team[i] = std::move(save.team_list[i]);

    std::unique_ptr<guy> old_current = std::move(og::runtime::current_session->current_guy_);
    const short old_team_num = og::runtime::current_session->current_team_num_;
    auto* old_team_button = og::runtime::current_session->allbuttons_[18];
    auto* old_train_session = pks().train_session;

    save.team_size = 1;
    save.numplayers = 2;
    save.m_totalcash[0] = 10000;
    save.m_totalcash[1] = 10000;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_list[0]->name = "Trainer";

    og::runtime::current_session->allbuttons_[18] =
        new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0,
                    "team", KEYSTATE_UNKNOWN);

    og::ui::TrainSession session(save);
    pks().train_session = &session;
    og::runtime::current_session->current_guy_ =
        std::make_unique<guy>(session.working_copy());
    og::runtime::current_session->current_team_num_ = session.working_copy().teamnum;

    ASSERT_EQ(4, static_cast<int>(change_teamnum(1)));
    EXPECT_EQ(1, static_cast<int>(session.working_copy().teamnum));
    EXPECT_EQ(1, static_cast<int>(og::runtime::current_session->current_guy_->teamnum));

    ASSERT_TRUE(session.accept(true));
    picker_lobby_sync_from_save();
    EXPECT_EQ(1, static_cast<int>(save.team_list[0]->teamnum));

    picker_lobby_shutdown();
    pks().train_session = old_train_session;
    delete og::runtime::current_session->allbuttons_[18];
    og::runtime::current_session->allbuttons_[18] = old_team_button;
    og::runtime::current_session->current_guy_ = std::move(old_current);
    og::runtime::current_session->current_team_num_ = old_team_num;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[i] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    for (int i = 0; i < 4; ++i)
        save.m_totalcash[i] = old_cash[i];
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
        old_team[i] = std::move(save.team_list[i]);

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
        save.team_list[i] = std::move(old_team[i]);
    save.team_size = old_team_size;
    save.numplayers = old_numplayers;
    for (int i = 0; i < 4; ++i)
        save.m_totalcash[i] = old_cash[i];
}
