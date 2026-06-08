#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input_state.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/picker.h>
#include <openglad/interface/ui/text_protocol.h>
#include <openglad/interface/walker_render.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/pixie_data.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <unistd.h>

void headless_clear_stale_view_controls(LevelRuntimeData*);
void clear_keyboard();
void headless_level_data_draw(LevelRuntimeData*, screen*);
std::unique_ptr<LevelRender> headless_create_level_render(PixieData[]);
EntityFactory headless_create_entity_factory();
loader* headless_entity_loader();
void wire_world_with_loader(GameWorld* world, loader* game_loader);
void headless_wire_world_entity_services(GameWorld* world, LevelRuntimeData* level);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
int get_input_events();
walker* find_follow_leader();
extern short end_of_file;
int toInt(const std::string& s);
void input_state_from_sdl(InputState& out);
void emit_headless_unsupported_warnings_probe();
std::string get_asset_path();
bool save_settings();
bool load_settings();
void io_init(int argc, char* argv[]);
void io_exit();

namespace {

class EnvGuard {
public:
    explicit EnvGuard(const char* name)
        : name_(name)
    {
        if (const char* value = std::getenv(name)) {
            had_value_ = true;
            old_value_ = value;
        }
    }

    ~EnvGuard()
    {
        if (had_value_)
            setenv(name_, old_value_.c_str(), 1);
        else
            unsetenv(name_);
    }

private:
    const char* name_;
    bool had_value_ = false;
    std::string old_value_;
};

class MemoryOgFile final : public og::io::OgFile {
public:
    explicit MemoryOgFile(std::string data)
        : data_(std::move(data))
    {
    }

    std::size_t read(void* buf, std::size_t size, std::size_t count) override
    {
        const std::size_t requested = size * count;
        const std::size_t available = pos_ < data_.size() ? data_.size() - pos_ : 0u;
        const std::size_t bytes = requested < available ? requested : available;
        if (bytes > 0)
            std::memcpy(buf, data_.data() + pos_, bytes);
        pos_ += bytes;
        return size == 0 ? 0u : bytes / size;
    }

    std::size_t write(const void*, std::size_t, std::size_t) override { return 0; }

    std::int64_t seek(std::int64_t offset, int whence) override
    {
        if (whence == 0)
            pos_ = static_cast<std::size_t>(offset);
        else if (whence == 1)
            pos_ += static_cast<std::size_t>(offset);
        else if (whence == 2)
            pos_ = data_.size() + static_cast<std::size_t>(offset);
        return static_cast<std::int64_t>(pos_);
    }

    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }

private:
    std::string data_;
    std::size_t pos_ = 0;
};

class StdinRedirect {
public:
    explicit StdinRedirect(const std::string& input)
        : input_(input)
        , old_(std::cin.rdbuf(input_.rdbuf()))
    {
    }

    ~StdinRedirect()
    {
        std::cin.rdbuf(old_);
    }

private:
    std::istringstream input_;
    std::streambuf* old_;
};

class CoutRedirect {
public:
    CoutRedirect()
        : old_(std::cout.rdbuf(output_.rdbuf()))
    {
    }

    ~CoutRedirect()
    {
        std::cout.rdbuf(old_);
    }

    std::string str() const { return output_.str(); }

private:
    std::ostringstream output_;
    std::streambuf* old_;
};

class StdoutSilencer {
public:
    StdoutSilencer()
        : saved_(dup(STDOUT_FILENO))
        , null_(std::fopen("/dev/null", "w"))
    {
        if (saved_ >= 0 && null_ != nullptr)
            dup2(fileno(null_), STDOUT_FILENO);
    }

    ~StdoutSilencer()
    {
        std::fflush(stdout);
        if (saved_ >= 0) {
            dup2(saved_, STDOUT_FILENO);
            close(saved_);
        }
        if (null_ != nullptr)
            std::fclose(null_);
    }

private:
    int saved_;
    FILE* null_;
};

PixieData make_pixie(unsigned char frames = 3, unsigned char w = 2, unsigned char h = 2)
{
    const std::size_t count = static_cast<std::size_t>(frames) * w * h;
    auto* data = new unsigned char[count];
    for (std::size_t i = 0; i < count; ++i)
        data[i] = static_cast<unsigned char>(i + 1);
    return PixieData(frames, w, h, data);
}

} // namespace

namespace og::ui {
int text_picker_testing_exercise_internal_paths();
std::string text_protocol_testing_format_event_text(std::string_view text);
}

TEST(PlatformHeadless, user_and_asset_paths_cover_normalization)
{
    EnvGuard config_guard("OPENGLAD_CONFIG_DIR");
    EnvGuard home_guard("HOME");

    setenv("OPENGLAD_CONFIG_DIR", "/tmp/openglad-headless///", 1);
    EXPECT_EQ("/tmp/openglad-headless/", get_user_path());

    setenv("OPENGLAD_CONFIG_DIR", "", 1);
    setenv("HOME", "/tmp/openglad-home", 1);
    EXPECT_EQ("/tmp/openglad-home/.openglad/", get_user_path());

    unsetenv("OPENGLAD_CONFIG_DIR");
    unsetenv("HOME");
    EXPECT_EQ("./", get_user_path());

    const std::string asset_path = get_asset_path();
    ASSERT_FALSE(asset_path.empty());
    EXPECT_EQ('/', asset_path.back());
}

TEST(PlatformHeadless, hooks_and_entity_services_are_wired)
{
    const LevelDataHooks& hooks = headless_level_data_hooks();
    ASSERT_NE(nullptr, hooks.clear_stale_view_controls);
    ASSERT_NE(nullptr, hooks.draw);
    ASSERT_NE(nullptr, hooks.create_level_render);
    ASSERT_NE(nullptr, hooks.create_entity_factory);
    ASSERT_NE(nullptr, hooks.wire_world_entity_services);

    hooks.clear_stale_view_controls(nullptr);
    headless_clear_stale_view_controls(nullptr);
    clear_keyboard();
    hooks.draw(nullptr, nullptr);
    headless_level_data_draw(nullptr, nullptr);
    EXPECT_EQ(nullptr, hooks.create_level_render(nullptr));
    EXPECT_EQ(nullptr, headless_create_level_render(nullptr));

    EntityFactory factory = hooks.create_entity_factory();
    EXPECT_FALSE(static_cast<bool>(factory.attach_render));
    ASSERT_TRUE(static_cast<bool>(factory.report_error));
    factory.report_error("headless factory test error");

    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));

    GameWorld world(0);
    wire_world_with_loader(nullptr, headless_entity_loader());
    wire_world_with_loader(&world, nullptr);
    hooks.wire_world_entity_services(&world, nullptr);
    ASSERT_TRUE(static_cast<bool>(world.entity_factory));
    ASSERT_TRUE(static_cast<bool>(world.entity_configurator));
    ASSERT_TRUE(static_cast<bool>(world.entity_derived_stats));

    std::unique_ptr<walker> entity =
        world.entity_factory(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, entity);
    EXPECT_FALSE(entity->has_render());
    EXPECT_NE(nullptr, world.entity_configurator(*entity, Order::Living, FAMILY_SOLDIER));
    world.entity_derived_stats(entity.get(), Order::Living, FAMILY_SOLDIER);
    world.entity_derived_stats(nullptr, Order::Living, FAMILY_SOLDIER);

    GameWorld world_two(0);
    headless_wire_world_entity_services(&world_two, nullptr);
    EXPECT_TRUE(static_cast<bool>(world_two.entity_factory));

    LevelRender render;
    render.init_tiles(nullptr);
    render.reset_tiles(nullptr);
    render.draw_tile(0, 1, 2, nullptr);
    og::runtime::VButtonDeleter{}(nullptr);
}

TEST(PlatformHeadless, io_init_installs_callable_headless_platform_bridge)
{
    EnvGuard config_guard("OPENGLAD_CONFIG_DIR");
    const std::filesystem::path config_dir =
        std::filesystem::temp_directory_path() / "openglad-headless-bridge";
    std::error_code ec;
    std::filesystem::remove_all(config_dir, ec);
    std::filesystem::create_directories(config_dir, ec);
    setenv("OPENGLAD_CONFIG_DIR", config_dir.string().c_str(), 1);

    char arg0[] = "og_unit_headless_platform";
    char* argv[] = {arg0, nullptr};
    io_init(1, argv);

    const PlatformBridge& bridge = platform_bridge();
    ASSERT_TRUE(static_cast<bool>(bridge.present_frame));
    ASSERT_TRUE(static_cast<bool>(bridge.play_sound));
    ASSERT_TRUE(static_cast<bool>(bridge.play_music));
    ASSERT_TRUE(static_cast<bool>(bridge.stop_music));
    ASSERT_TRUE(static_cast<bool>(bridge.create_surface));
    ASSERT_TRUE(static_cast<bool>(bridge.list_relay_rooms));

    bridge.present_frame();
    bridge.play_sound(7);
    bridge.play_music("theme.ogg");
    bridge.stop_music();
    EXPECT_EQ(nullptr, bridge.create_surface(320, 200));
    EXPECT_TRUE(bridge.list_relay_rooms("http://relay", "campaign").empty());

    (void)load_settings();
    (void)save_settings();

    std::filesystem::remove_all(config_dir, ec);
}

TEST(PlatformHeadless, unsupported_platform_functions_return_documented_defaults)
{
    EXPECT_TRUE(yes_or_no_prompt("headless", "default true", true));
    EXPECT_FALSE(yes_or_no_prompt("headless", "default false", false));
    EXPECT_EQ(0, get_input_events());
    EXPECT_EQ(nullptr, find_follow_leader());

    EXPECT_EQ(NewFileIoError::OpenWriteFailed,
              create_new_map_pix_with_error("headless-map.png", 3, 4));
    EXPECT_EQ(NewFileIoError::OpenWriteFailed,
              create_new_pix_with_error("headless-pix.png", 3, 4, 7));
    EXPECT_EQ(NewFileIoError::OpenWriteFailed,
              create_new_campaign_descriptor_with_error("headless-campaign.yaml"));
    EXPECT_EQ(NewFileIoError::OpenWriteFailed,
              create_new_scen_file_with_error("headless-scen", "scen0001"));

    PixieData pixie = make_pixie(1, 1, 1);
    load_map_data(&pixie);

    EXPECT_EQ(42, toInt("42"));
    EXPECT_EQ(-7, toInt("-7"));
    EXPECT_EQ(0, toInt("not-a-number"));

    InputState input;
    input.timer_wait_request = 12;
    input_state_from_sdl(input);
    EXPECT_EQ(kNoTimerWaitRequest, input.timer_wait_request);

    emit_headless_unsupported_warnings_probe();
}

TEST(PlatformHeadless, help_reader_stops_on_newline_carriage_return_limit_and_eof)
{
    MemoryOgFile file("alpha\rbravo\ncharlie");
    end_of_file = 0;
    EXPECT_EQ("alpha", read_one_line(file, HELP_WIDTH));
    EXPECT_EQ("bravo", read_one_line(file, HELP_WIDTH));
    EXPECT_EQ("charlie", read_one_line(file, HELP_WIDTH));
    EXPECT_EQ("", read_one_line(file, HELP_WIDTH));
    EXPECT_EQ(1, end_of_file);

    MemoryOgFile limited("abcdef");
    end_of_file = 0;
    EXPECT_EQ("abc", read_one_line(limited, 3));
    EXPECT_EQ(0, end_of_file);

    MemoryOgFile many_lines("one\ntwo\nthree\n");
    char help[HELP_WIDTH][MAX_LINES];
    std::memset(help, 0, sizeof(help));
    end_of_file = 0;
    const short count = fill_help_array(help, many_lines);
    EXPECT_GE(count, 3);
    EXPECT_STREQ("one", help[0]);
    EXPECT_STREQ("two", help[1]);
    EXPECT_STREQ("three", help[2]);
}

TEST(PlatformHeadless, walker_render_stubs_keep_sim_state_without_render_component)
{
    PixieData pixie = make_pixie();
    WalkerRender render(pixie);
    EXPECT_EQ(nullptr, render.bmp_data());
    render.set_frame(1);
    render.set_data(pixie);

    walker entity(pixie);
    EXPECT_FALSE(entity.has_render());
    EXPECT_EQ(2, entity.sizex());
    EXPECT_EQ(2, entity.sizey());
    EXPECT_EQ(nullptr, entity.bmp_data());
    EXPECT_EQ(1, entity.set_frame(2));
    EXPECT_EQ(2, entity.frame());
    EXPECT_EQ(0, entity.set_frame(-1));
    EXPECT_EQ(0, entity.set_frame(3));
    entity.set_direct_frame(5);
    EXPECT_EQ(5, entity.frame());

    PixieData replacement = make_pixie(2, 4, 5);
    entity.set_data(replacement);
    EXPECT_EQ(4, entity.sizex());
    EXPECT_EQ(5, entity.sizey());
    EXPECT_EQ(1, entity.set_frame(1));
    EXPECT_EQ(0, entity.set_frame(2));
}

TEST(PlatformHeadless, text_protocol_session_covers_commands_and_load_failure)
{
    {
        StdinRedirect input("tick 0\ntick -5\nevents\nstate\nunknown\nquit\n");
        CoutRedirect output;

        og::ui::TextProtocolArgs args;
        args.level = 1;
        args.team_families = {FAMILY_SOLDIER, FAMILY_ELF};
        args.seed = 99;
        EXPECT_EQ(0, og::ui::run_text_protocol_session(args));

        const std::string text = output.str();
        EXPECT_NE(std::string::npos, text.find("\"status\":\"ready\""));
        EXPECT_NE(std::string::npos, text.find("\"cmd\":\"tick\",\"count\":1"));
        EXPECT_NE(std::string::npos, text.find("\"cmd\":\"events\""));
        EXPECT_NE(std::string::npos, text.find("\"cmd\":\"state\""));
        EXPECT_NE(std::string::npos, text.find("unknown command: unknown"));
        EXPECT_NE(std::string::npos, text.find("\"cmd\":\"quit\",\"status\":\"ok\""));
    }

    {
        StdinRedirect input("quit\n");
        CoutRedirect output;

        og::ui::TextProtocolArgs args;
        args.level = 9999;
        EXPECT_EQ(1, og::ui::run_text_protocol_session(args));
    }
}

TEST(PlatformHeadless, text_protocol_event_text_is_valid_json_escaped)
{
    const std::string encoded = og::ui::text_protocol_testing_format_event_text(
        "quote\" slash\\ newline\n tab\t carriage\r ctrl\x01");

    EXPECT_NE(std::string::npos, encoded.find("\"tick\":7"));
    EXPECT_NE(std::string::npos, encoded.find("\"kind\":8"));
    EXPECT_NE(std::string::npos,
              encoded.find("\"text\":\"quote\\\" slash\\\\ newline\\n tab\\t carriage\\r ctrl\\u0001\""));
    EXPECT_EQ(std::string::npos, encoded.find('\n'))
        << "event JSON must not contain raw newlines inside strings";
    EXPECT_EQ(std::string::npos, encoded.find('\r'))
        << "event JSON must not contain raw carriage returns inside strings";
}

TEST(PlatformHeadless, text_picker_drives_menu_options_team_and_campaign_paths)
{
    const std::string input =
        "bad\n"
        "7\n"
        "3\n"
        "4\n"
        "5\n"
        "6\n"
        "8\n"
        "9\n"
        "10\n"
        "newslot\n"
        "not-a-seed\n"
        "10\n"
        "textslot\n"
        "123\n"
        "2\n"
        "1\n"
        "\n"
        "2\n"
        "n\n"
        "p\n"
        "1\n"
        "-1\n"
        "6\n"
        "a\n"
        "b\n"
        "3\n"
        "p\n"
        "n\n"
        "h\n"
        "b\n"
        "5\n"
        "4\n"
        "8\n"
        "9\n"
        "0\n"
        "9\n"
        "2\n"
        "10\n"
        "11\n"
        "999\n"
        "11\n"
        "\n"
        "7\n"
        "11\n";

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutSilencer stdout_silencer;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER, FAMILY_MAGE};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("textslot", config.save_name);
    EXPECT_EQ(2, config.level);
    ASSERT_GE(config.team_families.size(), 2u);
}

TEST(PlatformHeadless, text_picker_internal_help_and_error_paths)
{
    StdoutSilencer stdout_silencer;
    constexpr int kExpectedInternalHelperChecks = 23;
    EXPECT_EQ(kExpectedInternalHelperChecks,
              og::ui::text_picker_testing_exercise_internal_paths());
}
