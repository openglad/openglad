/* GoogleTest entry point for the ncurses client test suite.
 *
 * Provides the process globals the SDL-free engine expects (mirroring
 * src/server/server_main.cpp), initializes the real headless filesystem so
 * integration tests can load campaigns/levels/sprites, and isolates writes to a
 * temporary OPENGLAD_CONFIG_DIR. All curses tests use HeadlessTerminal +
 * FakeClock, so the suite needs no TTY and runs in CI.
 */
#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include <openglad/core/irandom.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/interface/input.h> // load_player_control_settings_from_cfg, InputHardwareState
#include <openglad/interface/session_state.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>

void io_init(int argc, char* argv[]);
void io_exit();

namespace og::curses {
int curses_terminal_testing_fatal_signal_probe();
}

namespace {

// Restores the headless session's gameplay context between tests so a test that
// repoints current_game / current_session cannot corrupt later tests.
class CursesSessionListener final : public ::testing::EmptyTestEventListener
{
public:
    CursesSessionListener(og::runtime::SessionState& session,
                          GameWorld& fallback_world, SaveData& fallback_save)
        : session_(session), fallback_world_(fallback_world), fallback_save_(fallback_save)
    {
    }

    void OnTestEnd(const ::testing::TestInfo&) override
    {
        fallback_world_.delete_objects();
        og::runtime::current_session = &session_;
        og::runtime::primary_session.store(&session_, std::memory_order_release);
        current_game = &session_.game_;
        og::runtime::primary_game.store(&session_.game_, std::memory_order_release);
        session_.game_.world = &fallback_world_;
        session_.game_.save = &fallback_save_;
        session_.game_.sim_events = session_.ctx_.sim_events.get();
        session_.game_.config = &cfg;
        session_.game_.session_rng_ref = &session_.ctx_.rng;
        session_.game_.gameplay_active_ref = &session_.gameplay_active_;
        session_.gameplay_active_ = false;
        // [SAVE-R8] Structural active-company reset: CursesPickerClient's
        // constructor asserts terminal slot authority, so every picker test
        // repoints the process-wide slot; restore the default between tests.
        (void)og::data::set_active_company_slot("save0");
    }

private:
    og::runtime::SessionState& session_;
    GameWorld& fallback_world_;
    SaveData& fallback_save_;
};

} // namespace

int main(int argc, char** argv)
{
    if (std::getenv("OPENGLAD_CURSES_FATAL_SIGNAL_PROBE") != nullptr)
        return og::curses::curses_terminal_testing_fatal_signal_probe();

    std::error_code executable_error;
    const std::filesystem::path executable =
        std::filesystem::weakly_canonical(argv[0], executable_error);
    if (!executable_error)
        (void)setenv("OPENGLAD_CURSES_TEST_EXECUTABLE",
                     executable.c_str(), 1);

    ::testing::InitGoogleTest(&argc, argv);

    const auto test_config_dir = std::filesystem::temp_directory_path() /
        ("openglad_curses_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_config_dir);
    setenv("OPENGLAD_CONFIG_DIR", test_config_dir.c_str(), 1);

    init_logging();
    io_init(argc, argv);
    cfg.load_settings();
    init_all_registries();

    static FixedRandom test_rng{0};
    static GameWorld fallback_world(0);
    static SaveData fallback_save;
    og::runtime::SessionState& session = *og::runtime::current_session;
    session.ctx_.rng = &test_rng;
    session.game_.world = &fallback_world;
    session.game_.save = &fallback_save;
    session.game_.sim_events = session.ctx_.sim_events.get();
    session.game_.config = &cfg;
    session.game_.session_rng_ref = &session.ctx_.rng;
    session.game_.gameplay_active_ref = &session.gameplay_active_;
    current_game = &session.game_;

    // Mirror the app: allocate the input hardware state and load the player's
    // keybindings + 4/8-direction mode from the (test-isolated) config. This
    // populates current_session->player_keys_[0] — the table CursesInput reads — so
    // input tests exercise the real bindings. Tests that need a specific direction
    // mode set it themselves (with a restore guard) so the suite stays
    // order-independent under --gtest_shuffle.
    session.input_hw_ = std::make_unique<InputHardwareState>();
    load_player_control_settings_from_cfg(cfg);

    ::testing::UnitTest::GetInstance()->listeners().Append(
        new CursesSessionListener(session, fallback_world, fallback_save));

    const int result = RUN_ALL_TESTS();

    io_exit();
    std::error_code ec;
    std::filesystem::remove_all(test_config_dir, ec);
    return result;
}
