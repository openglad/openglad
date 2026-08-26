#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/cloud_save_client.h>
#include <openglad/interface/ui/picker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/terminal_menu_model.h>
#include <openglad/interface/ui/text_protocol.h>
#include <openglad/interface/walker_render.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/company.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/og_file.h>
#include <openglad/gameplay/pixie_data.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

// The headless hooks table members are file-local to
// src/server/headless_level_hooks.cpp now (the SDL platform keeps a
// wire_world_with_loader twin, so the names must not leak) — reach them
// through headless_level_data_hooks() / headless_entity_loader() only.
void clear_keyboard();
loader* headless_entity_loader();
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
void popup_dialog(const char* title, const char* message);
std::uint32_t random(std::uint32_t x);

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

// Captures fd-level stdout (std::printf) into a temp file; restore() puts
// the original stdout back and returns everything captured so far.
class StdoutCapture {
public:
    StdoutCapture()
        : saved_(dup(STDOUT_FILENO))
        , file_(std::tmpfile())
    {
        if (saved_ >= 0 && file_ != nullptr)
            dup2(fileno(file_), STDOUT_FILENO);
    }

    ~StdoutCapture()
    {
        (void)restore();
        if (file_ != nullptr)
            std::fclose(file_);
    }

    std::string restore()
    {
        std::fflush(stdout);
        if (saved_ >= 0) {
            dup2(saved_, STDOUT_FILENO);
            close(saved_);
            saved_ = -1;
        }
        if (file_ == nullptr)
            return {};
        std::string text;
        std::rewind(file_);
        char buffer[4096];
        std::size_t bytes = 0;
        while ((bytes = std::fread(buffer, 1, sizeof(buffer), file_)) > 0)
            text.append(buffer, bytes);
        return text;
    }

private:
    int saved_;
    std::FILE* file_;
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
int text_picker_testing_staged_view_scenario();
int text_picker_testing_launch_seed_matches_the_preview();
int text_picker_testing_launch_census_matches_the_preview();
std::string text_protocol_testing_format_event_text(std::string_view text);
std::string text_protocol_testing_json_mode(const GameWorld& world);
}

TEST(PlatformHeadless, production_platform_globals_preserve_headless_contracts)
{
    testing::internal::CaptureStderr();
    popup_dialog("Network", "Connection lost");
    EXPECT_EQ("[Network] Connection lost\n",
              testing::internal::GetCapturedStderr());

    EXPECT_EQ(0u, random(0));
    EXPECT_EQ(0u, random(1));
    for (int sample = 0; sample < 128; ++sample)
        EXPECT_LT(random(17), 17u);
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

    // Call each warn-stub twice through the table: the std::call_once-backed
    // warnings must emit only once per process.
    hooks.clear_stale_view_controls(nullptr);
    hooks.clear_stale_view_controls(nullptr);
    clear_keyboard();
    hooks.draw(nullptr, nullptr);
    hooks.draw(nullptr, nullptr);
    EXPECT_EQ(nullptr, hooks.create_level_render(nullptr));
    EXPECT_EQ(nullptr, hooks.create_level_render(nullptr));

    EntityFactory factory = hooks.create_entity_factory();
    EXPECT_FALSE(static_cast<bool>(factory.attach_render));
    ASSERT_TRUE(static_cast<bool>(factory.report_error));
    factory.report_error("headless factory test error");

    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    GameWorld world(0);
    // A null world flows through the wiring guard untouched.
    hooks.wire_world_entity_services(nullptr, nullptr);
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
    hooks.wire_world_entity_services(&world_two, nullptr);
    EXPECT_TRUE(static_cast<bool>(world_two.entity_factory));

    LevelRender render;
    render.init_tiles(nullptr);
    render.reset_tiles(nullptr);
    render.draw_tile(0, 1, 2, nullptr);
    render.init_decor(nullptr);
    render.draw_decor(0, 1, 2, nullptr);
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

namespace {

// io_init runs its body only on a FRESH PhysFS: PHYSFS_init returns 0 when
// PhysFS is already up, and io_init bails out on that. The unit main
// initialises one filesystem for the whole binary, so a test that wants the
// real io_init path has to hand it a clean slate — and put the harness's
// filesystem, mounted-campaign bookkeeping and pack-script registry back
// exactly as they were, or every later test in the binary inherits the
// wreckage.
class FreshFilesystemForIoInit {
public:
    FreshFilesystemForIoInit()
        : user_path_(get_user_path())
        , campaign_(get_mounted_campaign())
        , scripts_(og::script::pack_scripts())
        , chunks_(og::script::pack_family_chunks())
        , modules_(og::script::pack_lib_modules())
    {
        (void)og::resources::deinit();
        set_mounted_campaign_for_testing("");
        // All three registries, because all three are what a mount fills:
        // core's behavior is families/ + lib/ and nothing under scripts/.
        og::script::clear_pack_scripts();
        og::script::clear_pack_family_chunks();
        og::script::clear_pack_lib_modules();
    }

    // Runs AFTER the caller's EnvGuard has restored OPENGLAD_CONFIG_DIR
    // (reverse declaration order), so get_user_path() is the harness path
    // again by the time the campaign is remounted.
    ~FreshFilesystemForIoInit()
    {
        (void)og::resources::deinit();
        set_mounted_campaign_for_testing("");
        (void)og::resources::init("og_unit_headless_platform");
        (void)og::resources::set_write_dir(user_path_);
        (void)og::resources::mount(user_path_.c_str(), nullptr, 1);
        if (!campaign_.empty())
            (void)mount_campaign_package_with_error(campaign_);
        og::script::clear_pack_scripts();
        for (const og::script::PackScript& script : scripts_)
            og::script::register_pack_script(script);
        og::script::clear_pack_family_chunks();
        for (const og::script::PackScript& chunk : chunks_)
            og::script::register_pack_family_chunk(chunk);
        og::script::clear_pack_lib_modules();
        for (const og::script::PackLibModule& module : modules_)
            og::script::register_pack_lib_module(module);
    }

private:
    std::string user_path_;
    std::string campaign_;
    std::vector<og::script::PackScript> scripts_;
    std::vector<og::script::PackScript> chunks_;
    std::vector<og::script::PackLibModule> modules_;
};

}  // namespace

// Headless clients run the same deterministic sim as the SDL client, so
// io_init must leave the process with the class-pack BEHAVIOR registered —
// not just the descriptor data. The moment a family's behavior lives only in
// pack Lua, openglad_text / openglad_server / openglad_curses would
// otherwise silently run with no behavior at all. In v3 that behavior
// reaches the VM through two registries: the families/*.lua chunks (which
// carry both the declaration and its hooks) and the lib/*.lua modules they
// og.use. Core ships nothing under scripts/ any more, so an empty script
// registry is no longer evidence of anything.
TEST(PlatformHeadless, io_init_registers_class_pack_scripts)
{
    FreshFilesystemForIoInit filesystem_guard;
    ASSERT_TRUE(og::script::pack_family_chunks().empty());

    EnvGuard config_guard("OPENGLAD_CONFIG_DIR");
    const std::filesystem::path config_dir =
        std::filesystem::temp_directory_path() / "openglad-headless-packs";
    std::error_code ec;
    std::filesystem::remove_all(config_dir, ec);
    std::filesystem::create_directories(config_dir, ec);
    setenv("OPENGLAD_CONFIG_DIR", config_dir.string().c_str(), 1);

    char arg0[] = "og_unit_headless_platform";
    char* argv[] = {arg0, nullptr};
    io_init(1, argv);

    const std::vector<og::script::PackScript>& chunks =
        og::script::pack_family_chunks();
    EXPECT_FALSE(chunks.empty())
        << "headless io_init must register the mounted packs' declarations";

    // The soldier's declaration and behavior are one file under families/,
    // and the shared behavior the other families reference is under lib/, so
    // the mount has to fill both registries to have filled either.
    bool has_core_pack = false;
    bool has_core_soldier = false;
    bool has_core_lib = false;
    for (const og::script::PackScript& chunk : chunks) {
        if (chunk.pack_id != "core")
            continue;
        has_core_pack = true;
        EXPECT_FALSE(chunk.source.empty())
            << "registered family chunk has no source: " << chunk.chunk_name;
        if (chunk.chunk_name == "packs/core/families/living-00-soldier.lua")
            has_core_soldier = true;
    }
    for (const og::script::PackLibModule& module :
         og::script::pack_lib_modules()) {
        if (module.pack_id != "core")
            continue;
        EXPECT_FALSE(module.source.empty())
            << "registered lib module has no source: " << module.chunk_name;
        if (module.name == "weapon_projectiles")
            has_core_lib = true;
    }
    EXPECT_TRUE(has_core_pack)
        << "the shipped core pack must be registered by headless io_init";
    EXPECT_TRUE(has_core_soldier)
        << "core pack declarations must be readable through the headless "
           "packs mount";
    EXPECT_TRUE(has_core_lib)
        << "the shared behavior a declaration og.uses must be mounted too";

    // refresh_pack_scripts also does what the old install_classpacks() call
    // did; the descriptor registries must still come out populated.
    const FamilyDescriptor* soldier = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);
    EXPECT_STREQ("SOLDIER", soldier->name)
        << "headless io_init must still install pack descriptor data";

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
    // Order-independent: install the packages AND mount the one whose level 1
    // the session loads — the protocol session reads the mounted package and
    // does not mount anything itself.
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    {
        StdinRedirect input(
            "path 0\n"       // missing entity diagnostic
            "path 1\n"       // known entity before AI selects a foe
            "tick 0\n"
            "tick -5\n"
            "events\n"
            "state\r\n"
            "path 1\n"       // same entity after AI selects a foe
            "grid 0 -1 -1 1 1\n" // clips negative cells from the query
            "obat 0 0 0\n"   // valid, empty collision-map cell
            "obat 0 24 58\n" // player start cell contains an entity
            "unknown\n"
            "quit\n");
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
        EXPECT_NE(std::string::npos,
                  text.find("\"cmd\":\"path\",\"eid\":0,\"error\":"))
            << "path must report a missing entity as a protocol error";
        const std::size_t known_path =
            text.find("\"cmd\":\"path\",\"eid\":1,");
        ASSERT_NE(std::string::npos, known_path)
            << "path must inspect a known entity without mutating it";
        EXPECT_NE(std::string::npos, text.find("\"foe\":", known_path))
            << "a known entity path report must include its target state";
        EXPECT_NE(std::string::npos, text.find("\"foe\":null"))
            << "the diagnostic must represent an entity with no target";
        EXPECT_NE(std::string::npos, text.find("\"foe\":{\"eid\":"))
            << "the diagnostic must represent an entity's selected target";
        EXPECT_NE(std::string::npos, text.find("\"path_len\":"))
            << "a selected target must produce a pathfinding report";
        EXPECT_NE(std::string::npos,
                  text.find("\"cmd\":\"grid\",\"floor\":0,\"rows\":["));
        EXPECT_NE(std::string::npos, text.find("\"y\":0,\"cells\":[[0,"))
            << "negative grid bounds must be clipped";
        EXPECT_NE(std::string::npos,
                  text.find("\"cmd\":\"obat\",\"floor\":0,\"cx\":0,"
                            "\"cy\":0,\"entries\":[]"));
        EXPECT_NE(std::string::npos,
                  text.find("\"cmd\":\"obat\",\"floor\":0,\"cx\":24,"
                            "\"cy\":58,\"entries\":[{"))
            << "the deployed player must be registered in the collision map";
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

    {
        StdinRedirect input("quit\n");
        CoutRedirect output;

        og::ui::TextProtocolArgs args;
        args.campaign = "missing-protocol-campaign";
        args.level = 1;
        EXPECT_EQ(1, og::ui::run_text_protocol_session(args))
            << "a missing campaign package must fail before level loading";
    }
}

TEST(PlatformHeadless, text_protocol_census_reports_teams_named_heroes_and_crew)
{
    // Westlands level 2 (The Forest Road) carries a PLACED named hero
    // ("The Bearer", the entity SAVE_ALL actually watches) plus hostile
    // delayed spawns, so one level exercises team_counts, dormant_counts,
    // named, and crew in a single census. --team-level=5 must show up as
    // the crew guys' level.
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("westlands"));

    {
        StdinRedirect input("census\ntick 3\ncensus\nquit\n");
        CoutRedirect output;

        og::ui::TextProtocolArgs args;
        args.campaign = "westlands";
        args.level = 2;
        args.team_families = {FAMILY_SOLDIER, FAMILY_ELF};
        args.seed = 7;
        args.team_level = 5;
        EXPECT_EQ(0, og::ui::run_text_protocol_session(args));

        const std::string text = output.str();
        EXPECT_NE(std::string::npos, text.find("\"cmd\":\"census\""));
        EXPECT_NE(std::string::npos, text.find("\"team_counts\":["));
        EXPECT_NE(std::string::npos, text.find("\"dormant_counts\":["));
        EXPECT_NE(std::string::npos, text.find("\"named\":["))
            << "census must list placed named heroes";
        EXPECT_NE(std::string::npos, text.find("\"name\":\"The Bearer\""))
            << "the placed SAVE_ALL hero must appear in the census";
        EXPECT_NE(std::string::npos, text.find("\"crew\":["));
        EXPECT_NE(std::string::npos, text.find("\"name\":\"Player1\""));
        EXPECT_NE(std::string::npos, text.find("\"level\":5"))
            << "--team-level must upgrade the spawned crew";
        EXPECT_NE(std::string::npos, text.find("\"dead\":false"));
    }

    {
        // team_level=0 keeps the legacy loader-default guy (level 1).
        StdinRedirect input("census\nquit\n");
        CoutRedirect output;

        og::ui::TextProtocolArgs args;
        args.campaign = "westlands";
        args.level = 2;
        args.team_families = {FAMILY_SOLDIER};
        args.seed = 7;
        EXPECT_EQ(0, og::ui::run_text_protocol_session(args));

        const std::string text = output.str();
        EXPECT_NE(std::string::npos, text.find("\"name\":\"Player1\""));
        EXPECT_NE(std::string::npos, text.find("\"level\":1"));
        EXPECT_EQ(std::string::npos, text.find("\"level\":5"));
    }

    // Leave the default campaign mounted for order-independence.
    ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("gladiator"));
}

TEST(PlatformHeadless, text_protocol_serializes_shipped_mode_state)
{
    restore_default_campaigns();
    struct RestoreDefaultCampaignMount
    {
        ~RestoreDefaultCampaignMount()
        {
            (void)mount_campaign_package_with_error("gladiator");
        }
    } restore_default_campaign_mount;

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    StdinRedirect input("tick 2\nstate\nquit\n");
    CoutRedirect output;

    og::ui::TextProtocolArgs args;
    args.campaign = "modes";
    args.level = 500;
    args.team_families = {FAMILY_SOLDIER};
    args.seed = 7;
    EXPECT_EQ(0, og::ui::run_text_protocol_session(args));

    const std::string text = output.str();
    EXPECT_NE(std::string::npos,
              text.find("\"status\":\"ready\",\"level\":500,"
                        "\"title\":\"CTF: FIRST BLOOD\""));
    EXPECT_NE(std::string::npos,
              text.find("\"cmd\":\"tick\",\"count\":2"));
    EXPECT_NE(std::string::npos, text.find("\"cmd\":\"state\""));

    // The shipped CTF Lua activates on the first scripted tick and names
    // itself through og.set_mode_name; its scoreboard HUD line carries the
    // 0:0 caps readout. The mode block is the ONLY match-state channel.
    EXPECT_NE(std::string::npos,
              text.find("\"mode\":{\"active\":true,\"name\":\"CTF\","
                        "\"winner_team\":-1"))
        << "the shipped scen500 must activate the Lua CTF mode";
    EXPECT_NE(std::string::npos, text.find("\"hud\":["))
        << "the mode block carries the HUD channel";
    EXPECT_EQ(std::string::npos, text.find("\"ctf\":"))
        << "the retired CTF JSON block must not be emitted";
    EXPECT_NE(std::string::npos,
              text.find("\"cmd\":\"quit\",\"status\":\"ok\""));

    EXPECT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// The scripted-mode observability block, byte-pinned (the json_ctf literal
// pin's twin). The emitter reads only ModeState, so a hand-built world pins
// the exact JSON shape all headless harnesses will parse.
TEST(PlatformHeadless, text_protocol_json_mode_literal_shape)
{
    GameWorld world(997);
    world.type |= SCEN_TYPE_SCRIPTED;
    world.mode.active = true;
    std::strncpy(world.mode.name.data(), "CTF", world.mode.name.size() - 1);
    world.mode.vars[0] = 5;
    world.mode.vars[63] = -2;
    world.mode.hud[1].team = 0;
    std::strncpy(world.mode.hud[1].text.data(), "2H",
                 world.mode.hud[1].text.size() - 1);
    world.mode.beacons[2].entity_id = 123;
    world.mode.beacons[2].team = 2;

    std::string vars = "5";
    for (int i = 1; i < og::sim::kModeVarCount - 1; ++i)
        vars += ",0";
    vars += ",-2";
    const std::string expected =
        "{\"active\":true,\"name\":\"CTF\",\"winner_team\":-1,"
        "\"vars\":[" + vars + "],"
        "\"hud\":[{\"team\":0,\"text\":\"2H\"}],"
        "\"beacons\":[{\"id\":123,\"team\":2}]}";
    EXPECT_EQ(expected, og::ui::text_protocol_testing_json_mode(world));

    // The pre-activation shape: everything default, empty hud/beacons.
    GameWorld blank(998);
    std::string zeros = "0";
    for (int i = 1; i < og::sim::kModeVarCount; ++i)
        zeros += ",0";
    EXPECT_EQ("{\"active\":false,\"name\":\"\",\"winner_team\":-1,"
              "\"vars\":[" + zeros + "],\"hud\":[],\"beacons\":[]}",
              og::ui::text_protocol_testing_json_mode(blank));
}

// End-to-end: a level authored with SCEN_TYPE_SCRIPTED makes cmd_state emit
// the "mode" key (active:false without a registered on_mode_init — the
// activation itself is observable).
TEST(PlatformHeadless, text_protocol_emits_mode_block_for_scripted_levels)
{
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path fss = fs::path(get_user_path()) / "scen" / "scen997.fss";
    const fs::path grid_png =
        fs::path(get_user_path()) / "pix" / "modep997.png";
    fs::remove(fss, ec);
    fs::remove(grid_png, ec);

    {
        // The tower user-dir pipeline writes the .fss + grid png exactly
        // where the og_file fallback chain loads them from.
        GameWorld world(997);
        world.create_new_grid();
        world.type = SCEN_TYPE_SCRIPTED;
        world.title = "MODE PROBE";
        og::data::LevelFileMetadata metadata;
        metadata.grid_file = "modep997";
        og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
        ASSERT_TRUE(og::data::save_level_to_user_dir(world, 997, metadata,
                                                     &err))
            << "authoring the scripted probe level should succeed";
    }

    {
        StdinRedirect input("state\nquit\n");
        CoutRedirect output;

        og::ui::TextProtocolArgs args;
        args.campaign = "gladiator";
        args.level = 997;
        args.team_families = {FAMILY_SOLDIER};
        args.seed = 7;
        EXPECT_EQ(0, og::ui::run_text_protocol_session(args));

        const std::string text = output.str();
        EXPECT_NE(std::string::npos, text.find("\"cmd\":\"state\""));
        EXPECT_NE(std::string::npos,
                  text.find("\"mode\":{\"active\":false,\"name\":\"\","
                            "\"winner_team\":-1"))
            << "scripted levels must carry the mode block";
        EXPECT_EQ(std::string::npos, text.find("\"ctf\":"))
            << "the retired CTF JSON block must not be emitted";
    }

    fs::remove(fss, ec);
    fs::remove(grid_png, ec);
}

TEST(PlatformHeadless, text_protocol_campaign_state_seed_validates_keys)
{
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    {
        StdinRedirect input("quit\n");
        CoutRedirect output;
        og::ui::TextProtocolArgs args;
        args.level = 1;
        args.team_families = {FAMILY_SOLDIER};
        args.campaign_state = {{"delve_counted", 1}, {"watch_paid", 900}};
        EXPECT_EQ(0, og::ui::run_text_protocol_session(args))
            << "safe-charset keys must seed and start the session";
    }
    {
        StdinRedirect input("quit\n");
        CoutRedirect output;
        og::ui::TextProtocolArgs args;
        args.level = 1;
        args.team_families = {FAMILY_SOLDIER};
        args.campaign_state = {{"BadKey", 1}};
        EXPECT_EQ(1, og::ui::run_text_protocol_session(args))
            << "a key outside [a-z0-9_] must reject the session";
    }
}

TEST(PlatformHeadless, text_protocol_event_text_is_valid_json_escaped)
{
    const std::string encoded = og::ui::text_protocol_testing_format_event_text(
        "quote\" slash\\ backspace\b formfeed\f newline\n tab\t carriage\r ctrl\x01");

    EXPECT_NE(std::string::npos, encoded.find("\"tick\":7"));
    EXPECT_NE(std::string::npos, encoded.find("\"kind\":8"));
    // #230: the per-seat addressee is reported (the text driver runs one
    // world and never filters, so the key is informational).
    EXPECT_NE(std::string::npos, encoded.find("\"target\":3"));
    EXPECT_NE(std::string::npos,
              encoded.find("\"text\":\"quote\\\" slash\\\\ backspace\\b formfeed\\f newline\\n tab\\t carriage\\r ctrl\\u0001\""));
    EXPECT_EQ(std::string::npos, encoded.find('\n'))
        << "event JSON must not contain raw newlines inside strings";
    EXPECT_EQ(std::string::npos, encoded.find('\r'))
        << "event JSON must not contain raw carriage returns inside strings";
}

TEST(PlatformHeadless, text_picker_drives_menu_options_team_and_campaign_paths)
{
    // Team Build is 11 items (§2.5 in-place substitution: 1=roster,
    // 4=deploy, 5=ready, 6=GO!; #206 inserted 7=camp before Back, shifting
    // 8=back, 9=networking, 10=Scenario; the flat CTF trio left for the
    // camp's MATCH SETUP page and 11=difficulty was appended in its place —
    // docs/camp-controls-design.md); the scenario-shaped commands nest
    // under the Scenario submenu (1=set_campaign, 2=set_level,
    // 3=view_scenario, 4=matchup, 5=progress, 6=troops, 7=replay level
    // (#207), 8=back — the missions door retired into the camp). Team
    // Build 11=difficulty opens the DIFFICULTY submenu, and 12=lineup the
    // LINEUP page (§8 — appended below difficulty, so nothing above moved)
    // (1=difficulty, 2=respawns, 3=respawn delay, 4=permadeath,
    // 5=generators, 6=infinite gold, 7=back). Main is 8 items now:
    // 1=begin, 2=continue, 3=level edit, 4=options, 5=help, 6=quit,
    // 7=load company, 8=cloud.
    const std::string input =
        "bad\r\n"   // main: invalid choice; terminal CR is trimmed
        "3\n"       // main: level edit (unavailable)
        "4\n"       // main: options
        "newslot\n"
        "not-a-seed\n"
        "4\n"       // main: options again
        "textslot\n"
        "123\n"
        "2\n"       // main: continue -> team build (base camp)
        "1\n"       // base camp: roster (deploy flags + sub-prompt)
        "deploy 1\n" //   roster: bench display row 1
        "deploy 1\n" //   roster: re-deploy it
        "train 1\n"  //   roster: train row 1 directly
        "b\n"        //   train: back to the roster
        "\n"         //   roster: blank exits
        "2\n"       // base camp: train
        "n\n"
        "p\n"
        "1\n"
        "-1\n"
        "6\n"
        "a\n"
        "b\n"
        "3\n"       // base camp: hire
        "p\n"
        "n\n"
        "h\n"
        "b\n"
        "5\n"       // base camp: ready (guarded outside networked lobbies)
        "4\n"       // base camp: deploy prompt
        "2\n"       //   toggle display row 2
        "10\n"      // team build: Scenario submenu
        "5\n"       // scenario: progress
        "2\n"       // scenario: set level (invalid value)
        "0\n"
        "2\n"       // scenario: set level -> 2 (unearned: the gate refuses)
        "2\n"
        "1\n"       // scenario: set campaign (invalid selection)
        "999\n"
        "1\n"       // scenario: set campaign (blank keeps current)
        "\n"
        "6\n"       // scenario: cycle scenario troops (ALL -> OWN)
        "6\n"       // scenario: cycle scenario troops (OWN -> ALL)
        "8\n"       // scenario: back -> team build (#207: Replay Level joined at 7)
        "11\n"      // team build: difficulty -> DIFFICULTY submenu
        "1\n"       // difficulty: cycle difficulty
        "2\n"       // difficulty: cycle respawns
        "3\n"       // difficulty: cycle respawn delay
        "4\n"       // difficulty: toggle permadeath
        "5\n"       // difficulty: cycle generators
        "6\n"       // difficulty: toggle infinite gold
        "6\n"       // difficulty: toggle infinite gold back off
        "7\n"       // difficulty: back -> team build
        // LINEUP (docs/lineup-design.md §8) appended at 12, so every ordinal
        // above it is untouched. Host rows: 1..8 the four teams' bot/level
        // knobs, 9 Fighters, 10 Split even, 11 Split fair, 12 Unite, 13 Back.
        "12\n"      // team build: LINEUP
        "1\n"       // lineup: TEAM 1 bots (classic: refused)
        "2\n"       // lineup: TEAM 1 level (classic: refused)
        "99\n"      // lineup: out of range -> refused, page reprints
        "13\n"      // lineup: back -> team build
        "9\n"       // team build: networking (unavailable)
        "8\n"       // team build: back -> main
        "6\n";      // main: quit

    restore_default_campaigns(); // order-independent: install the packages

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutSilencer stdout_silencer;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER, FAMILY_MAGE};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("textslot", config.save_name);
    EXPECT_EQ(1, config.level)
        << "the earned-roads gate must refuse the unearned forward jump";
    ASSERT_GE(config.team_families.size(), 2u);
}

// The raw Set Level prompt under the earned-roads gate: a fresh gladiator
// company's free-typed forward jump refuses in the campaign's voice and the
// cursor stays put. (The versus exemption on the same predicate is driven
// through the curses prompt and the terminal camp tests.)
TEST(PlatformHeadless, text_picker_set_level_prompt_rides_the_gate)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    {
        const std::string input =
            "2\n"    // main: continue -> team build
            "10\n"   // team build: Scenario submenu
            "2\n"    // scenario: set level -> 15 (unearned: refused)
            "15\n"
            "8\n"    // scenario: back -> team build (#207: Replay Level joined at 7)
            "8\n"    // team build: back -> main
            "6\n";   // main: quit
        StdinRedirect stdin_redirect(input);
        CoutRedirect cout_redirect;
        StdoutCapture stdout_capture;

        og::ui::TextPickerConfig config;
        config.team_families = {FAMILY_SOLDIER};
        og::ui::TextPickerError error;
        og::ui::run_text_picker(config, &error);

        const std::string out = stdout_capture.restore();
        EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
        EXPECT_EQ(1, config.level)
            << "the free-typed forward jump must not move the cursor";
        EXPECT_NE(std::string::npos,
                  out.find(std::string(og::ui::kCampaignLevelClosedMessage)))
            << "the prompt refuses in the campaign's voice";
    }
}

TEST(PlatformHeadless, text_picker_drives_the_cloud_submenu)
{
    // #155: Main 8=cloud (last, after load_company); the submenu is
    // 1=passphrase, 2=upload, 3=download, 4=back.
    // The headless bridge installs no cloud HTTP callbacks, so upload and
    // download degrade with the D8 unavailable line.
    cfg.data.erase("cloud");
    const std::string input =
        "8\n"                      // main: cloud -> CLOUD submenu
        "1\n"                      // cloud: passphrase
        "\n"                       //   blank cancels (unchanged)
        "1\n"                      // cloud: passphrase
        "short12\n"                //   7 chars -> rejected by the length gate
        "1\n"                      // cloud: passphrase (valid this time)
        "correct horse battery\n"
        "2\n"                      // cloud: upload -> unavailable
        "3\n"                      // cloud: download -> unavailable
        "4\n"                      // cloud: back -> main
        "6\n";                     // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string output = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_NE(std::string::npos, output.find("Passphrase unchanged."));
    EXPECT_NE(std::string::npos, output.find("Passphrase set."));
    EXPECT_NE(std::string::npos,
              output.find("Cloud sync is not available"))
        << "the headless bridge has no HTTP: the flows must degrade (D8)";
    EXPECT_EQ("73270125791ba273", cfg.get_setting("cloud", "key"))
        << "the DERIVED key persists (D9), pinned to the D2 vector";
    EXPECT_EQ("0", cfg.get_setting("cloud", "revision"));
    cfg.data.erase("cloud");
}

TEST(PlatformHeadless, text_picker_internal_help_and_error_paths)
{
    // Order-independent: install the packages and mount the default one the
    // internal paths read scenarios from.
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    StdoutSilencer stdout_silencer;
    EXPECT_EQ(0,
              og::ui::text_picker_testing_exercise_internal_paths());

    StdinRedirect empty_input("");
    og::ui::TextPickerConfig default_config;
    og::ui::run_text_picker(default_config, nullptr);
    ASSERT_EQ(1u, default_config.team_families.size());
    EXPECT_EQ(FAMILY_SOLDIER, default_config.team_families.front())
        << "an empty text-client team must receive the documented default";
}

// Staged VIEW LEVEL (#218, C10): the text census is the locally staged
// world — exact staged lines, and byte-identical on a reopen with the same
// session seed (the exerciser returns the negated index of the first
// failed check; 0 = every check passed).
TEST(PlatformHeadless, text_picker_staged_view_scenario_census)
{
    restore_default_campaigns();
    StdoutSilencer stdout_silencer;
    EXPECT_EQ(0, og::ui::text_picker_testing_staged_view_scenario());
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// #218 §1.3: the text session owns ONE match seed — VIEW LEVEL stages the
// preview with it and the GO path runs the protocol session with it. The
// launch's ready line reports the seed it ran with, so a GO that started
// drawing its own would show a seed the preview never staged.
TEST(PlatformHeadless, text_picker_launch_seed_matches_the_preview)
{
    restore_default_campaigns();
    StdoutSilencer stdout_silencer;
    EXPECT_EQ(0, og::ui::text_picker_testing_launch_seed_matches_the_preview());
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// #247: the text picker's GO launches the world VIEW LEVEL promised. The
// launch used to build its own world from a fresh default save, so the match
// knobs — TROOPS above all — never reached it: the preview showed matched
// squads and the launch fielded the legacy five-strong ones. Both halves now
// stage through the one pipeline on the one session seed.
TEST(PlatformHeadless, text_picker_launch_census_matches_the_preview)
{
    restore_default_campaigns();
    StdoutSilencer stdout_silencer;
    EXPECT_EQ(0,
              og::ui::text_picker_testing_launch_census_matches_the_preview());
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

TEST(PlatformHeadless, text_picker_reports_protocol_start_failure)
{
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    StdinRedirect input(
        "2\n"  // main: continue -> team build
        "6\n"  // team build: GO! attempts the invalid level
        "8\n"  // failed session returns to team build: back
        "6\n"); // main: quit
    CoutRedirect output;
    StdoutSilencer stdout_silencer;

    og::ui::TextPickerConfig config;
    config.level = 9999;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    EXPECT_EQ(og::ui::TextPickerErrorCode::Unsupported, error.code);
    EXPECT_NE(std::string::npos,
              error.detail.find("protocol session failed with code 1"));
}

// Begin New Game must drop a previously selected campaign back to the
// default — in the session config AND in the mounted package. The blank
// answer at the forced campaign-select keeps "current", which after the
// reset has to be the default campaign, not the stale one.
TEST(PlatformHeadless, text_picker_new_game_resets_campaign_and_mount)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "the modes package ships with the game and should mount";

    const std::string input =
        "1\n"       // main: begin new game -> §2.2 name entry
        "\n"        //   name entry: blank accepts the generated company name
        "\n"        //   campaign select: blank keeps current (= default reset)
        "8\n"       // team build: back -> main
        "6\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutSilencer stdout_silencer;

    og::ui::TextPickerConfig config;
    config.campaign = "modes"; // stale campaign from a prior session
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("gladiator", config.campaign)
        << "a new game must reset the session campaign to the default";
    EXPECT_EQ("gladiator", get_mounted_campaign())
        << "a new game must remount the default campaign package";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// --- §2.3 Company List: text-client flow ----------------------------------

namespace {

// Sandboxes <user>/save so the company list is fully deterministic (renames
// it aside, restores in the dtor). Also re-asserts the canonical unit
// filesystem: the binary-shared PhysFS state can be redirected by earlier
// tests under --gtest_shuffle.
class HeadlessSaveDirSandbox
{
public:
    HeadlessSaveDirSandbox()
        : save_dir_(std::filesystem::path(get_user_path()) / "save")
        , stash_dir_(std::filesystem::path(get_user_path()) /
                     "save_sandbox_stash_hl")
    {
        if (og::resources::is_initialized()) {
            const std::string user_path = get_user_path();
            (void)og::resources::set_write_dir(user_path);
            (void)og::resources::mount(user_path.c_str(), nullptr, 1);
        }
        std::error_code ec;
        std::filesystem::remove_all(stash_dir_, ec);
        if (std::filesystem::exists(save_dir_, ec))
            std::filesystem::rename(save_dir_, stash_dir_, ec);
        std::filesystem::create_directories(save_dir_, ec);
    }

    ~HeadlessSaveDirSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(save_dir_, ec);
        if (std::filesystem::exists(stash_dir_, ec))
            std::filesystem::rename(stash_dir_, save_dir_, ec);
        else
            std::filesystem::create_directories(save_dir_, ec);
    }

private:
    std::filesystem::path save_dir_;
    std::filesystem::path stash_dir_;
};

bool seed_headless_company(const std::string& slot, const std::string& name,
                           std::int64_t last_played)
{
    SaveData sd;
    sd.reset();
    sd.save_name = name;
    sd.current_campaign = "gladiator";
    sd.last_played_unix_s = last_played;
    return sd.save_with_error(slot) == SaveDataIoError::None;
}

} // namespace

// The main-menu LOAD door (position 8) presents the company list: open by
// row number ([SAVE-R2]: the terminal slot follows the load), the corrupt
// row refuses to switch, deleting the ACTIVE company refuses with
// switch-first, deleting another company needs the explicit "y" (NO-first),
// and the §2.4 backups chrome backs out of a company with no snapshots yet.
TEST(PlatformHeadless, text_picker_company_list_open_delete_and_guards)
{
    restore_default_campaigns(); // order-independent: install the packages
    HeadlessSaveDirSandbox sandbox;
    ASSERT_TRUE(seed_headless_company("wp3hlb", "BRAVO BAND", 6000));
    ASSERT_TRUE(seed_headless_company("wp3hla", "ALPHA BAND", 5000));
    {
        // Bad magic: lists as a CORRUPT row (sorts last, ts reads 0).
        std::ofstream corrupt(std::filesystem::path(get_user_path()) /
                              "save" / "wp3hlc.gtl");
        corrupt << "not a save file";
        ASSERT_TRUE(corrupt.good());
    }

    // Rows (most-recent-first): 1 = wp3hlb, 2 = wp3hla, 3 = wp3hlc (corrupt).
    const std::string input =
        "7\n"       // main: load company -> the company list
        "1\n"       //   list: open company...
        "3\n"       //     #3 = corrupt -> damaged message, never switches
        "1\n"       //   list: open company...
        "1\n"       //     #1 = wp3hlb -> loads, slot follows -> team build
        "8\n"       // team build: back -> main
        "7\n"       // main: load company again (active is now wp3hlb)
        "3\n"       //   list: delete company...
        "1\n"       //     #1 = wp3hlb = ACTIVE -> refused (switch first)
        "3\n"       //   list: delete company...
        "2\n"       //     #2 = wp3hla...
        "\n"        //     blank confirm = NO (NO-first) -> kept
        "3\n"       //   list: delete company...
        "2\n"       //     #2 = wp3hla...
        "y\n"       //     explicit yes -> deleted
        "2\n"       //   list: backups...
        "1\n"       //     #1 = wp3hlb: no snapshots yet -> backs out (§2.4)
        "4\n"       //   list: back -> main
        "6\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutSilencer stdout_silencer;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("wp3hlb", config.save_name)
        << "[SAVE-R2] opening a company repoints the terminal slot";
    EXPECT_TRUE(user_file_exists("save/wp3hlb.gtl"))
        << "the active company must survive its refused delete";
    EXPECT_FALSE(user_file_exists("save/wp3hla.gtl"))
        << "the explicit yes must delete the other company";
    EXPECT_TRUE(user_file_exists("save/wp3hlc.gtl"))
        << "the corrupt company is never opened OR deleted here";
}

// #155 cloud saves, text projection: the DOWNLOAD flow all the way down.
// text_picker_drives_the_cloud_submenu stops at the D8 unavailable line
// (the headless bridge installs no HTTP), so this one hands the bridge a
// canned vault reply and walks the rest: the stdin y/N confirm over an
// existing company, install_company_bytes, and the §2.3 open that repoints
// the terminal slot ([SAVE-R2]).
TEST(PlatformHeadless, text_picker_cloud_download_confirms_installs_and_opens)
{
    restore_default_campaigns(); // order-independent: install the packages
    HeadlessSaveDirSandbox sandbox;
    cfg.data.erase("cloud");

    // The company the cloud holds: real writer bytes (so install accepts them
    // and the open path can load them), staged through a scratch slot.
    ASSERT_TRUE(seed_headless_company("hlcloudr", "CLOUD BAND", 7000));
    std::string remote_bytes;
    {
        std::ifstream in(std::filesystem::path(get_user_path()) / "save" /
                             "hlcloudr.gtl",
                         std::ios::binary);
        remote_bytes.assign((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    }
    ASSERT_FALSE(remote_bytes.empty());
    ASSERT_TRUE(remove_user_file("save/hlcloudr.gtl"));

    // ...and the DIFFERENT company already occupying the target slot, so the
    // NO-first confirm actually fires.
    ASSERT_TRUE(seed_headless_company("hlcloudl", "LOCAL BAND", 6000));

    const std::vector<std::uint8_t> remote_raw(remote_bytes.begin(),
                                               remote_bytes.end());
    const std::string get_body =
        std::string(
            R"({"revision":5,"uploaded_at":1754200000000,"slot":"hlcloudl",)"
            R"("save_name":"CLOUD BAND","scen_num":1,"last_played":7000,)"
            R"("data_hex":")") +
        og::ui::cloud::hex_encode(remote_raw) + R"("})";

    std::vector<std::string> get_urls;
    // Restore on every exit path: an ASSERT inside the flow must not leak the
    // faked HTTP into the rest of the binary.
    struct BridgeRestore {
        PlatformBridge saved;
        ~BridgeRestore() { set_platform_bridge(saved); }
    } bridge_restore{platform_bridge()};

    PlatformBridge faked = bridge_restore.saved;
    faked.cloud_http_get = [&](const std::string& url) {
        get_urls.push_back(url);
        og::ui::cloud::CloudHttpResult result;
        result.status = 200;
        result.body = get_body;
        return result;
    };
    faked.cloud_http_post = [](const std::string&, const std::string&) {
        // Present only so hooks_available() passes; this flow never posts.
        og::ui::cloud::CloudHttpResult result;
        result.status = 500;
        return result;
    };
    set_platform_bridge(faked);

    const std::string input =
        "8\n"                      // main: cloud -> CLOUD submenu
        "1\n"                      // cloud: passphrase
        "correct horse battery\n"
        "3\n"                      // cloud: download
        "y\n"                      //   NO-first overwrite confirm -> yes
        "4\n"                      // cloud: back -> main
        "6\n";                     // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string output = stdout_capture.restore();

    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    ASSERT_EQ(1u, get_urls.size());
    EXPECT_NE(std::string::npos,
              get_urls[0].find("/api/save/73270125791ba273"))
        << "the GET addresses the derived-key route";
    EXPECT_NE(std::string::npos, output.find("OVERWRITE COMPANY?"));
    EXPECT_NE(std::string::npos, output.find("[y/N]"))
        << "the terminal confirm hook must have run (NO-first)";
    EXPECT_NE(std::string::npos, output.find("Loaded 'hlcloudl'"))
        << "the download runs the 2.3 open sequence on the installed company";
    EXPECT_NE(std::string::npos, output.find("Downloaded 'CLOUD BAND'."));
    EXPECT_EQ("hlcloudl", config.save_name)
        << "[SAVE-R2] the open repoints the terminal slot";
    EXPECT_EQ("CLOUD BAND", og::data::read_company_header("hlcloudl")
                                .value_or(og::data::CompanyInfo{})
                                .display_name)
        << "the slot holds the downloaded company";
    EXPECT_EQ("5", cfg.get_setting("cloud", "revision"))
        << "the server revision persists for the next optimistic upload";
    cfg.data.erase("cloud");
}

// The §2.4 Backups sub-view (text projection): snapshots list newest-first,
// a corrupt snapshot refuses restore up front, delete and restore are both
// NO-first ("y" only), and a successful restore rewinds the company,
// repoints the terminal slot ([SAVE-R2]), re-stamps last-played, and
// proceeds to team build (base camp).
TEST(PlatformHeadless, text_picker_backups_delete_and_restore_round_trip)
{
    restore_default_campaigns(); // order-independent: install the packages
    HeadlessSaveDirSandbox sandbox;
    // OLD (seq 1) and MID (seq 2) snapshots under a NEW current state, plus
    // a corrupt snapshot at seq 9 (bad magic, sorts first: seq desc).
    ASSERT_TRUE(seed_headless_company("wp3hlr", "OLD BAND", 5000));
    ASSERT_TRUE(og::data::backup_company_now("wp3hlr"));
    ASSERT_TRUE(seed_headless_company("wp3hlr", "MID BAND", 5500));
    ASSERT_TRUE(og::data::backup_company_now("wp3hlr"));
    ASSERT_TRUE(seed_headless_company("wp3hlr", "NEW BAND", 6000));
    {
        const std::filesystem::path backups_dir =
            std::filesystem::path(get_user_path()) / "save" / "backups";
        std::error_code ec;
        std::filesystem::create_directories(backups_dir, ec);
        std::ofstream corrupt(backups_dir / "wp3hlr.009.gtl",
                              std::ios::binary | std::ios::trunc);
        corrupt << "not a backup";
        ASSERT_TRUE(corrupt.good());
    }
    og::data::set_company_clock_for_tests(444444);

    // Snapshot rows (seq desc): 1 = seq 9 (corrupt), 2 = seq 2 (MID),
    // 3 = seq 1 (OLD).
    const std::string input =
        "7\n"       // main: load company -> the company list
        "2\n"       //   list: backups...
        "1\n"       //     #1 = wp3hlr (the only company)
        "1\n"       //     backups: restore...
        "1\n"       //       #1 = seq 9 corrupt -> damaged, no confirm
        "2\n"       //     backups: delete...
        "2\n"       //       #2 = seq 2 (MID)...
        "\n"        //       blank confirm = NO (NO-first) -> kept
        "2\n"       //     backups: delete...
        "2\n"       //       #2 = seq 2 again...
        "y\n"       //       explicit yes -> deleted
        "1\n"       //     backups: restore... (rows now: seq 9, seq 1)
        "2\n"       //       #2 = seq 1 (OLD)...
        "\n"        //       blank confirm = NO -> not restored
        "1\n"       //     backups: restore...
        "2\n"       //       #2 = seq 1 (OLD)...
        "y\n"       //       explicit yes -> rewound -> team build
        "8\n"       // team build: back -> main
        "6\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutSilencer stdout_silencer;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);
    og::data::set_company_clock_for_tests(std::nullopt);

    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("wp3hlr", config.save_name)
        << "[SAVE-R2] a restore repoints the terminal slot";

    // The slot file holds the rewound OLD state, re-stamped by the pinned
    // clock (§3.7 step 4).
    const std::optional<og::data::CompanyInfo> header =
        og::data::read_company_header("wp3hlr");
    ASSERT_TRUE(header && header->valid);
    EXPECT_EQ("OLD BAND", header->display_name);
    EXPECT_EQ(444444, header->last_played_unix_s);

    // Snapshots after the round trip: the pre-restore NEW state became the
    // newest (seq max+1 = 10), the corrupt seq 9 survives untouched, MID
    // (seq 2) was explicitly deleted, OLD (seq 1) survives its own restore.
    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("wp3hlr");
    ASSERT_EQ(3u, backups.size());
    EXPECT_EQ(10, backups[0].seq);
    EXPECT_EQ("NEW BAND", backups[0].header.display_name)
        << "the pre-restore state must be snapshotted first (§3.7 step 1)";
    EXPECT_EQ(9, backups[1].seq);
    EXPECT_FALSE(backups[1].header.valid);
    EXPECT_EQ(1, backups[2].seq);
}

// Packs a one-file .glad (campaign.yaml with the given title) into the user
// campaigns dir so label formatting can see a real package.
static bool install_titled_package(const std::string& id,
                                   const std::string& title)
{
    namespace fs = std::filesystem;
    const fs::path staging =
        fs::path(get_user_path()) / "dup_title_staging" / id;
    std::error_code ec;
    fs::create_directories(staging, ec);
    if (ec)
        return false;
    {
        std::ofstream out(staging / "campaign.yaml");
        out << "format_version: 1\ntitle: " << title << "\nversion: 1\n";
        if (!out)
            return false;
    }
    const fs::path archive =
        fs::path(get_user_path()) / "campaigns" / (id + ".glad");
    return zip_contents_with_error(staging.string(), archive.string()) ==
        ArchiveIoError::None;
}

// Two installed packages sharing a title must stay distinguishable in the
// campaign-select labels: duplicates carry the raw id as a suffix, unique
// titles stay clean.
TEST(PlatformHeadless, campaign_select_labels_disambiguate_duplicate_titles)
{
    restore_default_campaigns(); // order-independent: install the packages

    const std::string id_a = "test_dup_a";
    const std::string id_b = "test_dup_b";
    ASSERT_TRUE(install_titled_package(id_a, "Twin Peaks"));
    ASSERT_TRUE(install_titled_package(id_b, "Twin Peaks"));
    og::data::clear_campaign_metadata_cache(); // drop stale negative entries

    const std::vector<std::string> labels =
        og::ui::format_campaign_select_labels(
            {id_a, id_b, "gladiator"});
    ASSERT_EQ(3u, labels.size());
    EXPECT_EQ("Twin Peaks [test_dup_a]", labels[0]);
    EXPECT_EQ("Twin Peaks [test_dup_b]", labels[1]);
    EXPECT_EQ("Gladiator", labels[2]);

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove(fs::path(get_user_path()) / "campaigns" / (id_a + ".glad"), ec);
    fs::remove(fs::path(get_user_path()) / "campaigns" / (id_b + ".glad"), ec);
    fs::remove_all(fs::path(get_user_path()) / "dup_title_staging", ec);
    og::data::clear_campaign_metadata_cache();
}

// Selecting a campaign must mount its package: the text GO path loads levels
// straight from the mounted package, so a selection that only updated strings
// would silently keep playing the previously mounted campaign.
TEST(PlatformHeadless, text_picker_campaign_select_mounts_selection)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "the modes package ships with the game and should mount";

    const std::string input =
        "2\n"       // main: continue -> team build
        "10\n"      // team build: Scenario submenu
        "1\n"       // scenario: set campaign
        "1\n"       //   entry 1 is always the default campaign
        "8\n"       // scenario: back -> team build (#207: Replay Level joined at 7)
        "8\n"       // team build: back -> main
        "6\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutSilencer stdout_silencer;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("gladiator", config.campaign);
    EXPECT_EQ("gladiator", get_mounted_campaign())
        << "campaign selection must re-point the mount, not just strings";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// Users must see human titles, never raw campaign ids: "Gladiator" from the
// package's campaign.yaml and "1. SOUTH OF TALWOOD (BEGINNING)" from the
// scen1.fss header. The selection state keeps the raw id.
TEST(PlatformHeadless, text_picker_shows_display_titles_when_campaign_mounted)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    const std::string input =
        "2\n"       // main: continue -> team build
        "10\n"      // team build: Scenario submenu (labels print)
        "5\n"       // scenario: progress
        "2\n"       // scenario: set level (blank keeps current)
        "\n"
        "1\n"       // scenario: set campaign (blank keeps current)
        "\n"
        "6\n"       // scenario: cycle scenario troops (ALL -> OWN)
        "6\n"       // scenario: cycle scenario troops (OWN -> ALL)
        "8\n"       // scenario: back -> team build (#207: Replay Level joined at 7)
        "8\n"       // team build: back -> main
        "6\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("gladiator", config.campaign)
        << "display titles must not leak into the selection state";
    EXPECT_EQ("gladiator", get_mounted_campaign());

    EXPECT_NE(std::string::npos, out.find("Set Campaign (Gladiator)"));
    EXPECT_NE(std::string::npos,
              out.find("Set Level (1. SOUTH OF TALWOOD (BEGINNING))"));
    EXPECT_NE(std::string::npos,
              out.find("Current campaign progress: campaign=Gladiator "
                       "level=1. SOUTH OF TALWOOD (BEGINNING).\n"));
    EXPECT_NE(std::string::npos,
              out.find("Set level (current 1. SOUTH OF TALWOOD (BEGINNING)): "));
    EXPECT_NE(std::string::npos, out.find("  1. Gladiator\n"))
        << "campaign select must list titles, default campaign first";
    EXPECT_NE(std::string::npos, out.find("Multiplayer Game Modes"))
        << "campaign select must list every package by title";
    EXPECT_EQ(std::string::npos, out.find("org.openglad."))
        << "raw campaign ids must never reach the display";
}

// Scenario titles are read from the MOUNTED package; when the session
// campaign diverges from the mount the level must fall back to the bare
// number instead of showing a title from the wrong campaign.
TEST(PlatformHeadless, text_picker_level_display_falls_back_when_mount_differs)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "the modes package ships with the game and should mount";

    const std::string input =
        "2\n"       // main: continue -> team build
        "10\n"      // team build: Scenario submenu (labels print)
        "5\n"       // scenario: progress
        "8\n"       // scenario: back -> team build (#207: Replay Level joined at 7)
        "8\n"       // team build: back -> main
        "6\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.campaign = "gladiator"; // diverges from the ctf mount
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_NE(std::string::npos, out.find("Set Level (1)\n"))
        << "an unmounted campaign's level must display as a bare number";
    EXPECT_NE(std::string::npos, out.find("Set Campaign (Gladiator)"))
        << "campaign titles resolve via a private mount even when unmounted";
    EXPECT_NE(std::string::npos,
              out.find("Current campaign progress: campaign=Gladiator level=1.\n"));
    EXPECT_EQ(std::string::npos, out.find("SOUTH OF TALWOOD"))
        << "titles must never be read from a package other than the mount";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// §2.5 per-row TRAIN (text shape): 'train 2' opens the train flow SEEDED on
// roster row 2 — the banner names the second member's family and the first
// member's train screen never appears.
TEST(PlatformHeadless, text_picker_roster_train_row_opens_seeded_member)
{
    restore_default_campaigns(); // order-independent: install the packages

    const std::string input =
        "2\n"        // main: continue -> team build (base camp)
        "1\n"        // base camp: roster
        "train 2\n"  //   roster: train row 2 directly (the mage)
        "1\n"        //   train: +1 STR on the SEEDED member
        "a\n"        //   train: accept (autosaves the active slot)
        "b\n"        //   train: back to the roster
        "\n"         //   roster: blank exits
        "8\n"        // base camp: back -> main
        "6\n";       // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER, FAMILY_MAGE};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_NE(std::string::npos, out.find("(MAGE) ---"))
        << "'train 2' must seed the session on roster row 2 (the mage)";
    EXPECT_EQ(std::string::npos, out.find("(SOLDIER) ---"))
        << "the train screen must never open on the first member";
    EXPECT_NE(std::string::npos, out.find("Training accepted."))
        << "the seeded member's +1 STR should be affordable and accepted";

    // Hygiene: the train accept autosaved the text client's slot; reap it so
    // company-listing tests stay order-independent.
    (void)remove_user_file("save/" + config.save_name + ".gtl");
    og::data::set_active_company_slot("save0");
}

// #207: the SCENARIO submenu's Replay Level prompt on the text client —
// the invalid-id, earned-roads and cleared refusals, then the cleared arm:
// the cursor moves onto the level, the answer speaks in the replay voice,
// and PROGRESS confirms the moved cursor.
TEST(PlatformHeadless, text_picker_replay_level_prompt_gates_and_arms)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    HeadlessSaveDirSandbox sandbox;
    {
        SaveData seed;
        seed.reset();
        seed.save_name = "REPLAY BAND";
        seed.current_campaign = "gladiator";
        seed.scen_num = 3;
        seed.add_level_completed("gladiator", 1);
        seed.add_level_completed("gladiator", 2);
        seed.last_played_unix_s = 9000;
        ASSERT_EQ(SaveDataIoError::None, seed.save_with_error("replaytc"));
    }

    const std::string input =
        "7\n"       // main: load company -> the company list
        "1\n"       //   list: open company...
        "1\n"       //     row 1 = REPLAY BAND -> team build
        "10\n"      // team build: Scenario submenu
        "7\n"       // scenario: replay level (invalid value)
        "abc\n"
        "7\n"       // scenario: replay level (unearned forward id)
        "15\n"
        "7\n"       // scenario: replay level (in frontier but uncleared)
        "3\n"
        "7\n"       // scenario: replay level -> 1 (cleared: ARMS)
        "1\n"
        "5\n"       // scenario: progress (prints the moved cursor)
        "8\n"       // scenario: back -> team build
        "8\n"       // team build: back -> main
        "6\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_NE(std::string::npos, out.find("Replay level (must be cleared): "));
    EXPECT_NE(std::string::npos, out.find("Invalid level.\n"));
    EXPECT_NE(std::string::npos, out.find("That road is not open yet.\n"));
    EXPECT_NE(std::string::npos, out.find("That level is not cleared yet.\n"));
    EXPECT_NE(std::string::npos,
              out.find("Replaying SOUTH OF TALWOOD (BEGINNING). "
                       "GO when ready.\n"))
        << "the cleared arm answers in the replay voice";
    EXPECT_NE(std::string::npos,
              out.find("Current campaign progress: campaign=Gladiator "
                       "level=1. SOUTH OF TALWOOD (BEGINNING).\n"))
        << "arming moved the cursor onto the level";
    EXPECT_EQ(1, config.level);

    (void)remove_user_file("save/replaytc.gtl");
    og::data::set_active_company_slot("save0");
}

// #207 arm lifecycle on the text client: a campaign SWITCH after Replay
// Level abandons the excursion (the shared rule is pinned behaviorally on
// the curses twin and at apply_campaign_selection; the text tail has its
// own clear because its select writes the save directly). This drives the
// text chain end-to-end — arm, then switch to a NON-default campaign — so
// the switch-side clear executes on this client, with the switch itself
// asserted through the selection and mount state.
TEST(PlatformHeadless, text_picker_campaign_switch_after_replay_arm)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    HeadlessSaveDirSandbox sandbox;
    {
        SaveData seed;
        seed.reset();
        seed.save_name = "SWITCH BAND";
        seed.current_campaign = "gladiator";
        seed.scen_num = 3;
        seed.add_level_completed("gladiator", 1);
        seed.add_level_completed("gladiator", 2);
        seed.last_played_unix_s = 9000;
        ASSERT_EQ(SaveDataIoError::None, seed.save_with_error("switchtc"));
    }

    const std::string input =
        "7\n"       // main: load company -> the company list
        "1\n"       //   list: open company...
        "1\n"       //     row 1 = SWITCH BAND -> team build
        "10\n"      // team build: Scenario submenu
        "7\n"       // scenario: replay level -> 1 (cleared: ARMS)
        "1\n"
        "1\n"       // scenario: set campaign
        "2\n"       //   entry 2 = the first NON-default campaign (SWITCH)
        "8\n"       // scenario: back -> team build
        "8\n"       // team build: back -> main
        "6\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_NE(std::string::npos, out.find("Replaying"))
        << "the excursion armed before the switch";
    EXPECT_NE("gladiator", config.campaign)
        << "entry 2 must be a real switch off the default campaign";
    EXPECT_EQ(config.campaign, get_mounted_campaign())
        << "the switch re-points the mount";

    (void)remove_user_file("save/switchtc.gtl");
    og::data::set_active_company_slot("save0");
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

// §2.5 MATCHUP sub-prompt: the historical 'play N' spelling changes
// preferred-team metadata only, while 'move SLOT N' moves a roster slot
// across teams (the §3.8 move autosave rides the valid move). The former TEAMS
// screen previously had no test surface at all (WP7 coverage pass); the
// roster/deploy legs bundle their invalid-row rejections for the same reason.
TEST(PlatformHeadless, text_picker_matchup_screen_play_and_move_commands)
{
    restore_default_campaigns(); // order-independent: install the packages

    const std::string input =
        "2\n"           // main: continue -> team build (base camp)
        "1\n"           // base camp: roster
        "deploy 99\n"   //   roster: deploy row out of range
        "train 99\n"    //   roster: train row out of range
        "gibberish\n"   //   roster: unrecognized command
        "\n"            //   roster: blank exits
        "4\n"           // base camp: deploy prompt
        "abc\n"         //   non-numeric row rejected
        "10\n"          // base camp: Scenario submenu
        "4\n"           // scenario: matchup -> matchup sub-prompt
        "play 9\n"      //   team out of range
        "play 4\n"      //   in range but no heroes on team 4
        "play 1\n"      //   valid: preferred-team metadata only
        "play 2 1\n"    //   playable-seat form is explicitly unavailable
        "move 0 1\n"    //   slot out of range
        "move 1 9\n"    //   team out of range
        "move 1 2\n"    //   valid: slot 1 -> team 2 (§3.8 autosave)
        "move 1 1\n"    //   valid: back to team 1
        "gibberish\n"   //   unrecognized command
        "\n"            //   blank exits matchup
        "8\n"           // scenario: back -> team build (#207: Replay Level joined at 7)
        "8\n"           // base camp: back -> main
        "6\n";          // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER, FAMILY_MAGE};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_NE(std::string::npos, out.find("--- Matchup ---\n"));
    EXPECT_EQ(std::string::npos, out.find("--- Teams ---\n"))
        << "the retired TEAMS title must not remain user-visible";
    EXPECT_EQ(std::string::npos, out.find("4 Player\n"))
        << "the terminal Main menu must not expose retired player-count rows";
    EXPECT_NE(std::string::npos,
              out.find("Matchup (simulation has no player controls):"))
        << "the Matchup prompt must not imply controllable seats";
    EXPECT_NE(std::string::npos, out.find("No heroes on team 9.\n"))
        << "an out-of-range 'play' team must be rejected";
    EXPECT_NE(std::string::npos, out.find("No heroes on team 4.\n"))
        << "'play' onto an unmanned team must be rejected";
    EXPECT_NE(std::string::npos,
              out.find("Preferred-team metadata is now RED; "
                       "the text simulator has no player controls.\n"))
        << "'play 1' must be labeled as metadata, not a controllable seat";
    EXPECT_NE(std::string::npos,
              out.find("Player-seat assignments are unavailable in "
                       "the text simulator.\n"))
        << "the removed exact-seat form must be rejected clearly";
    EXPECT_EQ(std::string::npos, out.find(" TEAM (P"))
        << "text Matchup rows must never claim P# seat ownership";
    EXPECT_NE(std::string::npos, out.find("Invalid slot or team.\n"))
        << "out-of-range 'move' inputs must be rejected";
    EXPECT_NE(std::string::npos, out.find("Moved slot 1 to "))
        << "a valid 'move' must land and report the target team";
    EXPECT_NE(std::string::npos, out.find("Invalid roster row.\n"))
        << "out-of-range roster deploy/train rows must be rejected";

    // Hygiene: the valid move autosaved the text client's slot; reap it so
    // company-listing tests stay order-independent.
    (void)remove_user_file("save/" + config.save_name + ".gtl");
    og::data::set_active_company_slot("save0");
}

// --- #206 CAMP: the scripted Base Camp zone, text projection ---------------

namespace {

// Registers a throwaway scripted campaign for one test and restores the
// pack-script registry (and the gameplay context campaign dispatch resolves)
// afterwards — the test_campaign_picker_session fixture approach. The chunk
// name deliberately does NOT start with `packs/`: that prefix declares the
// bytes to the pack-Lua coverage inventory, and this throwaway chunk exists
// nowhere in the repository (the test_classpack_lua_decl discipline).
class ScopedSyntheticCampaignPicker
{
public:
    explicit ScopedSyntheticCampaignPicker(const std::string& source)
        : previous_game_(current_game)
        , scripts_(og::script::pack_scripts())
    {
        current_game = nullptr;  // dispatch resolves the shared UI VM
        og::script::register_pack_script(
            {"test.textcamp", "textcamp/scripts/c.lua", source});
    }

    ~ScopedSyntheticCampaignPicker()
    {
        og::script::clear_pack_scripts();
        for (const og::script::PackScript& script : scripts_)
            og::script::register_pack_script(script);
        current_game = previous_game_;
    }

private:
    GameplayContext* previous_game_;
    std::vector<og::script::PackScript> scripts_;
};

} // namespace

// The Team Build CAMP row (ordinal 7) opens the scripted Base Camp zone: the
// composition renders through the shared driver (byte-identical with curses)
// — the hoisted readout as the panel heading, a text widget CLIPPED to its
// declared band, the docket in the shared row vocabulary, and the roster
// block with the deploy padlock and its reason inline. og.campaign_gold
// proves the providers point at THIS picker's live save; an action
// debits/toasts/autosaves, a page row opens the book rooted at that page, a
// level row runs the text set-level tail, and the oath row cycles a hero's
// campaign_tag with the full-word toast. 0 at the camp closes it.
TEST(PlatformHeadless, text_picker_camp_drive_runs_the_scripted_zone)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    // The earned-roads gate closes unearned docket rows, so the company this
    // drive opens has already cleared THE PIT (level 9) — the level row
    // under test is a REPLAY, the state the camp's set-level tail serves.
    HeadlessSaveDirSandbox sandbox;
    {
        SaveData sd;
        sd.reset();
        sd.save_name = "CAMP BAND";
        sd.current_campaign = "gladiator";
        sd.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
        sd.team_list[0]->name = "Arthur";
        sd.team_list[0]->teamnum = 0;
        sd.team_list[0]->deployed = true;
        sd.team_size = 1;
        sd.scen_num = 1;
        sd.add_level_completed("gladiator", 9);
        ASSERT_EQ(SaveDataIoError::None, sd.save_with_error("campgate"));
    }
    ScopedSyntheticCampaignPicker picker(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "readout", items = {
            { label = "PURSE", value = og.campaign_gold() .. "g" },
            { label = "ROAD", value = "OPEN" },
          } },
        { kind = "text", weight = 1, lines = {
            "The company waits at the fire.",
            "CLIPPED: a one-unit band holds one line.",
          } },
        { kind = "actions", entries = {
            { id = "300", label = "THE CIRCLE", kind = "level", level = 300, note = "4 teams" },
            { id = "9", label = "THE PIT", kind = "level", level = 9 },
            { id = "kit", label = "FIELD KIT", kind = "action", cost = 100 },
            { id = "stores", label = "KETTLE'S STORES", kind = "page" },
            { id = "gone", kind = "level", level = 4242 },
          } },
        { kind = "roster",
          locks = { { unset = true, reason = "Swear first." } },
          assign = { key = "muster", labels = { "WAR", "BURDEN" } } },
      },
    }
  end,
  picker_menu = function(page_id)
    return {
      title = "KETTLE'S STORES",
      lines = { "Purse " .. og.campaign_gold() .. "g." },
      entries = {
        { id = "bread", label = "BREAD", kind = "action", cost = 10 },
      },
    }
  end,
  picker_action = function(entry_id)
    og.campaign_state_set("kit", 1)
    return { message = "Stowed for the road." }
  end,
}))LUA");

    const std::string input =
        "7\n"          // main: load company -> the company list
        "1\n"          //   list: open company...
        "1\n"          //     #1 = campgate (the cleared-9 band) -> team build
        "1\n"          // base camp: roster
        "deploy 1\n"   //   bench the soldier: benching is never refused
        "deploy 1\n"   //   deploying him back is: the camp's lock binds the
                       //   client's OWN roster row, not just the camp screen
        "\n"           //   roster: blank exits
        "7\n"          // team build: camp -> the scripted zone
        "99\n"         //   camp: out-of-range row -> invalid notice
        "1\n"          //   camp: a LABELLED road this campaign does not
                       //   carry -> refused (never "Level set to")
        "5\n"          //   camp: an unlabelled missing road -> refused too
        "3\n"          //   camp: FIELD KIT -> debit + toast + autosave
        "4\n"          //   camp: KETTLE'S STORES -> the book rooted at the page
        "1\n"          //     page: BREAD -> debit + toast
        "0\n"          //     page: back at the door's root -> the camp
        "6\n"          //   camp: SWEAR MUSTER -> the swear prompt
        "9\n"          //     swear: out-of-range roster row -> invalid notice
        "1\n"          //     swear: cycle row 1 (unset -> WAR)
        "1\n"          //     swear: cycle row 1 again (WAR -> BURDEN)
        "0\n"          //     swear: done -> the camp
        "2\n"          //   camp: THE PIT (level 9) -> the text set-level tail
        "2\n"          //   camp: THE PIT again -> the cursor is already
                       //   there, so the click is refused in the same words
                       //   the SDL camp uses (never a second confirmation)
        "0\n"          //   camp: back -> team build
        "8\n"          // team build: back -> main
        "6\n";         // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ(9, config.level)
        << "a camp level row must run the text set-level tail";

    EXPECT_NE(std::string::npos, out.find("--- Camp ---"));
    // The readout is ONE line of label/value cells, hoisted to the top
    // because the roster does not lead the composition.
    EXPECT_NE(std::string::npos, out.find("PURSE 5000g | ROAD OPEN\n"))
        << "og.campaign_gold must read the picker client's live save";
    EXPECT_NE(std::string::npos, out.find("The company waits at the fire.\n"));
    EXPECT_EQ(std::string::npos, out.find("CLIPPED:"))
        << "a widget weighed smaller than its lines CLIPS on the terminal "
           "exactly as it does on the panel";
    // The docket composes through the shared campaign_picker_row_text helper:
    // page rows wear the door marker, costed rows quote their price, and a
    // road the campaign does not carry reads CLOSED before the click —
    // whether the BOOK named the row (row 1) or the engine had to (row 5).
    EXPECT_NE(std::string::npos,
              out.find("   1. THE CIRCLE - 4 teams  [CLOSED]\n"))
        << "a labelled row must still declare a road the campaign lacks";
    EXPECT_NE(std::string::npos, out.find("   3. FIELD KIT  100g\n"));
    EXPECT_NE(std::string::npos, out.find("   4. KETTLE'S STORES  >\n"));
    EXPECT_NE(std::string::npos, out.find("   5. SCEN 4242  [CLOSED]\n"));
    EXPECT_NE(std::string::npos, out.find("Camp # [1-6] (0 = back): "));
    EXPECT_NE(std::string::npos, out.find("Invalid camp row."));
    EXPECT_EQ(std::string::npos, out.find("Level set to THE CIRCLE."))
        << "a CLOSED road must never answer with a confirmation";
    EXPECT_EQ(std::string::npos, out.find("scen4242"))
        << "the loader's own diagnostics never reach the campaign's voice";
    EXPECT_NE(std::string::npos,
              out.find(std::string(og::ui::kCampaignLevelClosedMessage)))
        << "a CLOSED road refuses in the campaign's voice, not the loader's";
    // The action debited the acting wallet (visible in the refetched
    // readout) and its toast printed.
    EXPECT_NE(std::string::npos, out.find("PURSE 4900g | ROAD OPEN\n"))
        << "the 100g action debit must land before the zone refetch";
    EXPECT_NE(std::string::npos, out.find("Stowed for the road."));
    // The page row opened the BOOK rooted at that page, and its own action
    // debited the same wallet; closing it refetched the camp.
    EXPECT_NE(std::string::npos, out.find("--- KETTLE'S STORES ---"));
    EXPECT_NE(std::string::npos, out.find("Camp # [1-1] (0 = back): "))
        << "the book page asks for a camp row: the word 'mission' is retired";
    EXPECT_EQ(std::string::npos, out.find("Mission #"))
        << "no player-visible terminal surface keeps the v1 noun";
    EXPECT_NE(std::string::npos, out.find("PURSE 4890g | ROAD OPEN\n"))
        << "the camp refetches when a page door closes";
    // The C++-owned header strip: the purse is on the camp screen AND on
    // the shop page that quotes prices, never only on Team Build. The oath
    // heading is padded out to sit OVER the oath cell (column 49), not
    // parked beside the summary facts.
    EXPECT_NE(std::string::npos,
              out.find("COMPANY  DEP 0/1  GOLD 5000"
                       "                       MUSTER\n"))
        << "the camp header carries the purse and heads the oath column";
    EXPECT_NE(std::string::npos, out.find("COMPANY  DEP 0/1  GOLD 4900\n"))
        << "the book page carries the same strip while it quotes prices";
    // The roster block: the shared camp row composer, the padlock in the
    // deploy cell and the lock's reason inline (a terminal cannot draw a
    // glyph in a cell nobody clicks).
    EXPECT_NE(std::string::npos, out.find("      [L] "))
        << "an unsworn hero wears the deploy padlock";
    EXPECT_NE(std::string::npos, out.find("  Swear first.\n"))
        << "the lock's reason reads inline, before any refusal";
    EXPECT_EQ(std::string::npos, out.find("- - Swear first."))
        << "the unsworn oath cell must not stutter into the reason";
    // The lock is not decoration: the Team Build roster's own 'deploy N'
    // asks the camp first and prints the campaign's reason (the SDL panel
    // refuses the same toggle with the same words).
    EXPECT_NE(std::string::npos, out.find("blank line exits: Swear first.\n"))
        << "a locked deploy must be refused on the client's own roster row, "
           "answering that row's prompt in the campaign's words";
    EXPECT_EQ(std::string::npos, out.find(" deployed.\n"))
        << "the refused toggle must never report a deploy";
    // The oath door and its prompt.
    EXPECT_NE(std::string::npos, out.find("   6. SWEAR MUSTER  >\n"));
    EXPECT_NE(std::string::npos, out.find("Swear # [1-1] (0 = done): "));
    EXPECT_NE(std::string::npos, out.find("Invalid roster row."));
    EXPECT_NE(std::string::npos, out.find("Sworn to WAR."));
    EXPECT_NE(std::string::npos, out.find("Sworn to BURDEN."))
        << "the cycle runs unset -> WAR -> BURDEN, never back to unset";
    EXPECT_NE(std::string::npos, out.find("   1. [ ] "))
        << "the sworn hero's row lost the padlock (the unset lock stopped "
           "matching) and the swear prompt numbers the roster";
    // The set-level tail re-derives the CURRENT marker on the refetch, and
    // the row that now reads [CURRENT] answers the next click with the
    // refusal instead of confirming a move that never happens.
    EXPECT_NE(std::string::npos, out.find("Level set to THE PIT."));
    EXPECT_NE(std::string::npos, out.find("   2. THE PIT  [CURRENT]\n"));
    EXPECT_NE(std::string::npos,
              out.find(std::string(og::ui::kCampaignLevelUnchangedMessage)))
        << "a click on the row the cursor is parked on must be refused, in "
           "the words the SDL camp uses for the same click";

    // Hygiene: the Acted arm autosaved the text client's slot; reap it so
    // company-listing tests stay order-independent.
    (void)remove_user_file("save/" + config.save_name + ".gtl");
    og::data::set_active_company_slot("save0");
}

// --- LINEUP, the text twin (docs/lineup-design.md §8) -------------------

namespace {

// A company with two seated teams: three fighters on team 0 (my_team, so
// the derivation seats it) and one on team 1. Levels descend by slot so the
// power-less SPLIT FAIR order (level desc, tie slot) is fully determined.
bool seed_lineup_company(const std::string& slot)
{
    SaveData sd;
    sd.reset();
    sd.save_name = "LINEUP BAND";
    // §2.3: the eight bot knobs are read by VERSUS campaigns' modes only, so
    // the knob half of this drive needs one.
    sd.current_campaign = "modes";
    sd.my_team = 0;
    for (int i = 0; i < 4; ++i) {
        sd.team_list[static_cast<std::size_t>(i)] =
            std::make_unique<guy>(FAMILY_SOLDIER);
        guy& member = *sd.team_list[static_cast<std::size_t>(i)];
        member.name = std::string("F") + static_cast<char>('1' + i);
        member.teamnum = i == 3 ? 1 : 0;
        member.deployed = true;
        member.level = static_cast<short>(4 - i);
    }
    sd.team_size = 4;
    sd.scen_num = 1;
    return sd.save_with_error(slot) == SaveDataIoError::None;
}

} // namespace

// The host drive: the bands print through the shared formatters, a bot knob
// cycles and LANDS IN THE SAVE (the .gtl round-trip is the proof — a label
// that changed without a write would be a lie the next launch tells), and
// SPLIT FAIR snake-drafts the company across the two seated teams.
TEST(PlatformHeadless, text_picker_lineup_cycles_a_knob_and_splits_fair)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    HeadlessSaveDirSandbox sandbox;
    ASSERT_TRUE(seed_lineup_company("lineupd"));

    const std::string input =
        "7\n"        // main: load company -> the company list
        "1\n"        //   list: open company...
        "1\n"        //     #1 = lineupd -> team build
        "12\n"       // team build: LINEUP
        // A2: TEAM 1 has this machine's seat, so the wheel's OFF is
        // refused and the step lands on NONE.
        "1\n"        //   lineup: TEAM 1 bots -> (OFF refused) NONE
        "2\n"        //   lineup: TEAM 1 level -> LV +1
        "9\n"        //   lineup: Fighters
        "team 4 1\n" //     fighters: F4 GREEN -> RED
        "team 9 1\n" //     fighters: slot out of range -> refused
        "team 4 9\n" //     fighters: team out of range -> refused
        "bench 4\n"  //     fighters: bench F4
        "bench 9\n"  //     fighters: slot out of range -> refused
        "wobble\n"   //     fighters: not a command -> refused
        "bench 4\n"  //     fighters: deploy F4 again
        "team 4 2\n" //     fighters: F4 back to GREEN (state restored)
        "\n"         //     fighters: blank exits
        "11\n"       //   lineup: SPLIT FAIR
        "99\n"       //   lineup: out of range -> refused, page reprints
        "13\n"       //   lineup: back -> team build
        "8\n"        // team build: back -> main
        "6\n";       // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);

    // The bands: header (colour + POWER + seats) over the knob/census line.
    EXPECT_NE(std::string::npos, out.find("--- Lineup ---"))
        << out;
    EXPECT_NE(std::string::npos, out.find("TEAM 1 RED  POWER "))
        << "the band names the colour, the price and the seats:\n" << out;
    EXPECT_NE(std::string::npos, out.find("[BOTS: AUTO] [LV: AUTO]  3 FIGHTERS"))
        << "the shared census formatter spells the fighter count:\n" << out;
    EXPECT_NE(std::string::npos, out.find("TEAM 3 BLUE  POWER --   NO SEAT"))
        << "an unoccupied team is honestly unpriced, and still says NO SEAT:\n"
        << out;
    EXPECT_NE(std::string::npos, out.find("[BOTS: AUTO] [LV: AUTO]  NO FIGHTERS"))
        << out;
    // The two cycles answer with the SAME formatter the row label uses.
    EXPECT_NE(std::string::npos, out.find("BOTS: NONE"))
        << "cycling the squad knob must answer with the shared label:\n" << out;
    EXPECT_NE(std::string::npos, out.find("LV +1"))
        << "A6: the level knob is an OFFSET, so the sign is the label:\n" << out;
    EXPECT_NE(std::string::npos, out.find("TEAM 1 HAS PLAYERS"))
        << "A2: OFF is refused on a seated team, in words, and the wheel "
           "steps over it instead of stranding:\n" << out;
    // The fighter list: team, deploy state and price on one row.
    EXPECT_NE(std::string::npos, out.find("--- Fighters ---")) << out;
    EXPECT_NE(std::string::npos,
              out.find("1. F1 (SOLDIER) LV 4  RED  DEPLOYED  POWER "))
        << out;
    EXPECT_NE(std::string::npos, out.find("'team SLOT TEAM' | 'bench SLOT'"))
        << out;
    // The command grammar, and its three refusals.
    EXPECT_NE(std::string::npos, out.find("Moved slot 4 to RED.")) << out;
    EXPECT_NE(std::string::npos, out.find("Moved slot 4 to GREEN.")) << out;
    EXPECT_NE(std::string::npos, out.find("F4 benched.")) << out;
    EXPECT_NE(std::string::npos, out.find("F4 deployed.")) << out;
    EXPECT_NE(std::string::npos, out.find("Invalid slot or team.")) << out;
    EXPECT_NE(std::string::npos, out.find("Invalid slot.")) << out;
    EXPECT_NE(std::string::npos, out.find("Unrecognized command.")) << out;
    EXPECT_NE(std::string::npos, out.find("Invalid selection."))
        << "the page refuses an out-of-range row and reprints:\n" << out;

    // M3 + §5: a text client is a ONE-SEAT machine (a company file always
    // loads with numplayers 1), and its seat picture is the launch's own —
    // so there is one seated team and split_company's single-seat rule makes
    // SPLIT FAIR an ALL TO 1. Only F4, parked on GREEN, moves.
    EXPECT_NE(std::string::npos, out.find("Moved 1 fighter.")) << out;

    SaveData reloaded;
    ASSERT_EQ(SaveDataIoError::None, reloaded.load_with_error("lineupd"));
    EXPECT_EQ(og::sim::kBotSquadNone, reloaded.bot_squad[0])
        << "the squad cycle must reach the company file (AUTO -> OFF "
           "refused -> NONE)";
    EXPECT_EQ(1, reloaded.bot_level[0])
        << "the level cycle must reach the company file (AUTO -> +1)";
    EXPECT_EQ(0, reloaded.bot_squad[1]) << "only the cycled team moved";
    ASSERT_TRUE(reloaded.team_list[0] && reloaded.team_list[1] &&
                reloaded.team_list[2] && reloaded.team_list[3]);
    for (int slot = 0; slot < 4; ++slot) {
        EXPECT_EQ(0, reloaded.team_list[static_cast<std::size_t>(slot)]->teamnum)
            << "slot " << slot;
    }

    (void)remove_user_file("save/lineupd.gtl");
    og::data::set_active_company_slot("save0");
}

// The same two rows on a CLASSIC campaign: the map's own levels decide the
// bots there, so the write is refused in words and never reaches the company
// file — the terminal spelling of change_lineup_bots returning without
// cycling. Hardcoding the knobs live let a terminal write a value the same
// campaign's SDL screen refuses.
TEST(PlatformHeadless, text_picker_lineup_knob_refuses_on_a_classic_campaign)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    HeadlessSaveDirSandbox sandbox;
    {
        SaveData sd;
        sd.reset();
        sd.save_name = "CLASSIC BAND";
        sd.current_campaign = "gladiator";
        sd.my_team = 0;
        sd.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
        sd.team_list[0]->name = "F1";
        sd.team_list[0]->teamnum = 0;
        sd.team_list[0]->deployed = true;
        sd.team_size = 1;
        sd.scen_num = 1;
        ASSERT_EQ(SaveDataIoError::None, sd.save_with_error("classicd"));
    }

    const std::string input =
        "7\n"   // main: load company -> the company list
        "1\n"   //   list: open company...
        "1\n"   //     #1 = classicd -> team build
        "12\n"  // team build: LINEUP
        "1\n"   //   lineup: TEAM 1 bots -> refused
        "2\n"   //   lineup: TEAM 1 level -> refused
        "13\n"  //   lineup: back -> team build
        "8\n"   // team build: back -> main
        "6\n";  // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_NE(std::string::npos,
              out.find(std::string(og::ui::kTerminalLineupMapRulesRefusal)))
        << "the row says who decides instead of cycling:\n" << out;
    EXPECT_NE(std::string::npos, out.find("TEAM 1  BOTS: AUTO  (MAP RULES)"))
        << "and the row itself carries the mark:\n" << out;
    EXPECT_EQ(std::string::npos, out.find("BOTS: NONE"))
        << "nothing cycled:\n" << out;

    SaveData reloaded;
    ASSERT_EQ(SaveDataIoError::None, reloaded.load_with_error("classicd"));
    EXPECT_EQ(0, reloaded.bot_squad[0]) << "no write reached the .gtl";
    EXPECT_EQ(0, reloaded.bot_level[0]);

    (void)remove_user_file("save/classicd.gtl");
    og::data::set_active_company_slot("save0");
}

// The joiner projection of the same page: §2.3 hides the eight bot knobs
// (they are the host's, and a joiner writing them would be a silent
// divergence) while the bands and every roster action stay.
TEST(PlatformHeadless, lineup_terminal_model_hides_the_knobs_from_a_joiner)
{
    using og::ui::TerminalLineupItem;

    SaveData save;
    save.reset();
    save.my_team = 0;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_list[0]->deployed = true;
    save.team_size = 1;

    const std::vector<og::sim::LobbyPlayer> seats =
        og::ui::synthesize_local_lobby_players(save);
    const std::vector<std::string> presets = {"BRUTES"};

    og::ui::TerminalLineupInputs inputs;
    inputs.save = &save;
    inputs.players = seats;
    inputs.preset_names = presets;

    inputs.is_host = true;
    const og::ui::TerminalLineupModel host =
        og::ui::build_terminal_lineup_model(inputs);
    inputs.is_host = false;
    const og::ui::TerminalLineupModel joiner =
        og::ui::build_terminal_lineup_model(inputs);

    EXPECT_EQ(8u, host.lines.size())
        << "four bands of two lines each, whether or not a team is on";
    EXPECT_EQ(joiner.lines, host.lines)
        << "a joiner sees exactly the census the host sees";
    EXPECT_EQ(13u, host.items.size())
        << "eight knobs + Fighters + the three SPLIT rows + Back";
    EXPECT_EQ(5u, joiner.items.size())
        << "the eight host-only knob rows are gone, nothing else is";
    for (const TerminalLineupItem& item : joiner.items) {
        EXPECT_NE(TerminalLineupItem::Kind::BotSquad, item.kind);
        EXPECT_NE(TerminalLineupItem::Kind::BotLevel, item.kind);
    }
    EXPECT_EQ(TerminalLineupItem::Kind::Fighters, joiner.items[0].kind);
    EXPECT_EQ("Fighters", joiner.items[0].label);
    // §5 over one seated team is UNITE by arithmetic, and a terminal client
    // is single-seat by construction — so the two SPLIT rows keep their
    // ordinals and say what they will actually do.
    EXPECT_EQ("Split even  (one seat: same as Unite)", joiner.items[1].label);
    EXPECT_EQ("Split fair  (one seat: same as Unite)", joiner.items[2].label);
    EXPECT_EQ("Unite", joiner.items[3].label);
    EXPECT_EQ(TerminalLineupItem::Kind::Back, joiner.items.back().kind);
    EXPECT_EQ("Back", joiner.items.back().label);
    // The knob rows quote the shared label VERBATIM behind the team ordinal.
    // This save names no campaign, so it is CLASSIC: the rows stay (dropping
    // them would renumber the page under the two 1-based consumers) but they
    // carry the MAP RULES mark and the band censuses MAP RULES, the terminal
    // spelling of the SDL screen's dimmed faces.
    EXPECT_EQ("TEAM 1  BOTS: AUTO  (MAP RULES)", host.items[0].label);
    EXPECT_EQ("TEAM 1  LV: AUTO  (MAP RULES)", host.items[1].label);
    EXPECT_NE(std::string::npos, host.lines[1].find("MAP RULES"))
        << "the classic census names who decides: " << host.lines[1];
    EXPECT_EQ(std::string::npos, host.lines[1].find("FIGHTER"))
        << "MAP RULES replaces the census, it does not join it: "
        << host.lines[1];
    // A preset ordinal the campaign DID register reads by name; the joiner
    // clamp path (an ordinal with no name) reads as its number, never AUTO.
    save.bot_squad[0] = og::sim::kBotSquadPresetBase;      // BRUTES
    save.bot_squad[1] = static_cast<short>(
        og::sim::kBotSquadPresetBase + 4);                 // no such name
    inputs.is_host = true;
    const og::ui::TerminalLineupModel named =
        og::ui::build_terminal_lineup_model(inputs);
    EXPECT_EQ("TEAM 1  BOTS: BRUTES  (MAP RULES)", named.items[0].label);
    EXPECT_EQ("TEAM 2  BOTS: #5  (MAP RULES)", named.items[2].label);

    // On a VERSUS campaign the knobs really are live: no mark, and the band
    // censuses the fighters again.
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    save.current_campaign = "modes";
    save.bot_squad[0] = 0;
    save.bot_squad[1] = 0;
    const og::ui::TerminalLineupModel versus =
        og::ui::build_terminal_lineup_model(inputs);
    EXPECT_EQ("TEAM 1  BOTS: AUTO", versus.items[0].label);
    EXPECT_EQ("TEAM 1  LV: AUTO", versus.items[1].label);
    EXPECT_NE(std::string::npos, versus.lines[1].find("FIGHTER"))
        << versus.lines[1];
    EXPECT_EQ(std::string::npos, versus.lines[1].find("MAP RULES"))
        << versus.lines[1];
    (void)unmount_campaign_package_with_error("modes");
    (void)mount_campaign_package_with_error("gladiator");
}

// Amendment A2: OFF drops an authored team out of the match, so a team that
// has a seat or a deployed fighter may not take it — and a refusal that just
// held the wheel in place would strand it, because OFF sits between AUTO and
// the presets. The step goes over it and says why.
TEST(PlatformHeadless, lineup_terminal_bots_wheel_steps_over_a_refused_off)
{
    SaveData save;
    save.reset();
    save.my_team = 0;
    // Team 1 (index 0) carries the seat; team 2 carries a deployed fighter
    // with no seat; teams 3 and 4 are empty.
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 0;
    save.team_list[0]->deployed = true;
    save.team_list[1] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[1]->teamnum = 1;
    save.team_list[1]->deployed = true;
    save.team_size = 2;
    save.numplayers = 1;

    og::ui::TerminalLineupInputs inputs;
    const std::vector<og::sim::LobbyPlayer> seats =
        og::ui::synthesize_local_lobby_players(save);
    inputs.save = &save;
    inputs.players = seats;
    const og::ui::TerminalLineupModel model =
        og::ui::build_terminal_lineup_model(inputs);

    EXPECT_EQ("TEAM 1 HAS PLAYERS", model.off_refusal[0])
        << "a seat outranks the fighters as the reason";
    EXPECT_EQ("TEAM 2 HAS FIGHTERS", model.off_refusal[1]);
    EXPECT_EQ("", model.off_refusal[2])
        << "an empty team may be switched OFF";
    EXPECT_EQ("", model.off_refusal[3]);

    // The wheel on the empty team is the plain one: AUTO -> OFF -> NONE.
    og::ui::TerminalLineupBotsStep step = og::ui::terminal_lineup_bots_step(
        og::sim::kBotSquadAuto, 1, 1, model.off_refusal[2]);
    EXPECT_EQ(og::sim::kBotSquadOff, step.value);
    EXPECT_EQ("", step.refusal);

    // On the seated team the same press lands on NONE and names the reason.
    step = og::ui::terminal_lineup_bots_step(og::sim::kBotSquadAuto, 1, 1,
                                             model.off_refusal[0]);
    EXPECT_EQ(og::sim::kBotSquadNone, step.value);
    EXPECT_EQ("TEAM 1 HAS PLAYERS", step.refusal);

    // Backwards over the same gap: NONE -> (OFF refused) -> AUTO. The wheel
    // stays reachable in both directions, which is the whole point of
    // stepping over rather than refusing in place.
    step = og::ui::terminal_lineup_bots_step(og::sim::kBotSquadNone, 1, -1,
                                             model.off_refusal[0]);
    EXPECT_EQ(og::sim::kBotSquadAuto, step.value);
    EXPECT_EQ("TEAM 1 HAS PLAYERS", step.refusal);

    // And the preset beyond it is still reachable in one more press.
    step = og::ui::terminal_lineup_bots_step(og::sim::kBotSquadNone, 1, 1,
                                             model.off_refusal[0]);
    EXPECT_EQ(og::sim::kBotSquadPresetBase, step.value);
    EXPECT_EQ("", step.refusal);
}

// M3: a seat picture has ONE derivation. The SDL screens and the launch both
// read derive_local_gameplay_seat_teams (through synthesize_local_lobby_
// players): my_team first, then the deployed teams, PADDED and truncated to
// numplayers. The terminal bands and their SPLIT used the unpadded
// derive_local_seat_teams instead, so on this two-player company — every
// character on team 0, which is what a fresh save looks like — the terminals
// saw ONE seat where the SDL sees two, painted NO SEAT on TEAM 2, and turned
// every SPLIT into ALL TO 1 while the SDL dealt the company across the pair.
TEST(PlatformHeadless, terminal_seat_picture_matches_the_sdl_derivation)
{
    SaveData save;
    save.reset();
    save.save_name = "ONE SEAT";
    save.my_team = 0;
    save.numplayers = 2;
    save.allied_mode = 0;
    for (int i = 0; i < 4; ++i) {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = std::string("F") + static_cast<char>('1' + i);
        member->deployed = true;
        member->teamnum = 0;  // the fresh-save shape: everyone on one colour
        save.team_list[static_cast<std::size_t>(i)] = std::move(member);
    }
    save.team_size = 4;

    // The picture the SDL screens read.
    const std::vector<og::sim::LobbyPlayer> seats =
        og::ui::synthesize_local_lobby_players(save);
    ASSERT_EQ(2u, seats.size()) << "numplayers=2 means two seats";
    EXPECT_EQ(0, seats[0].team);
    EXPECT_EQ(1, seats[1].team) << "the second seat is padded onto a free team";

    og::ui::TerminalLineupInputs inputs;
    inputs.save = &save;
    inputs.players = seats;
    const og::ui::TerminalLineupModel model =
        og::ui::build_terminal_lineup_model(inputs);
    ASSERT_EQ(8u, model.lines.size());
    EXPECT_EQ("TEAM 1 RED  POWER --   P1 ONE", model.lines[0]);
    EXPECT_EQ("TEAM 2 GREEN  POWER --   P2 ONE", model.lines[2])
        << "the padded seat must not read NO SEAT";

    // ...and the SPLIT plans over the same pair, not over one team. These are
    // the exact moves LineupUi.split_even_and_unite_direct pins on the SDL
    // screen for the same roster: one derivation, one answer.
    std::vector<std::string> report;
    EXPECT_EQ(2, og::ui::terminal_apply_lineup_split(
                     save, og::ui::LineupSplit::Even, report));
    EXPECT_EQ(0, save.team_list[0]->teamnum);
    EXPECT_EQ(1, save.team_list[1]->teamnum);
    EXPECT_EQ(0, save.team_list[2]->teamnum);
    EXPECT_EQ(1, save.team_list[3]->teamnum);
}

// §5 + §2.2: a SPLIT is a bulk team assignment, so it obeys the campaign's
// own can_team rule — the same rule that gates one fighter row at a time.
// The terminal clients used to hardcode zone_can_team=true, which made the
// three SPLIT rows a way around a composition that had taken the team chip
// away. Both terminals now run this one helper.
TEST(PlatformHeadless, terminal_split_obeys_the_campaign_can_team_rule)
{
    const auto seed = [](SaveData& save) {
        save.reset();
        save.my_team = 0;
        save.numplayers = 2;
        save.allied_mode = 0;
        for (int i = 0; i < 4; ++i) {
            auto member = std::make_unique<guy>(FAMILY_SOLDIER);
            member->name = "S" + std::to_string(i);
            member->deployed = true;
            // Seats derive from the deployed teams: {0, 1}.
            member->teamnum = static_cast<short>(i < 2 ? 0 : 1);
            save.team_list[static_cast<std::size_t>(i)] = std::move(member);
        }
        save.team_size = 4;
    };

    // A composition that ALLOWS team changes: the split lands.
    {
        ScopedSyntheticCampaignPicker zone(R"LUA(
og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster", can_team = true } } }
  end,
})
)LUA");
        SaveData save;
        seed(save);
        std::vector<std::string> report;
        const int moved = og::ui::terminal_apply_lineup_split(
            save, og::ui::LineupSplit::AllToFirst, report);
        EXPECT_EQ(2, moved) << "the two team-1 fighters march to team 0";
        EXPECT_EQ("Moved 2 fighters.", report.front());
        for (int i = 0; i < 4; ++i) {
            EXPECT_EQ(0, save.team_list[static_cast<std::size_t>(i)]->teamnum)
                << "slot " << i;
        }
    }

    // The same company under a composition that CLEARS can_team: every slot
    // is locked, nothing moves, and the refusal is in words.
    {
        ScopedSyntheticCampaignPicker zone(R"LUA(
og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster", can_team = false } } }
  end,
})
)LUA");
        SaveData save;
        seed(save);
        const std::array<short, 4> before = {
            save.team_list[0]->teamnum, save.team_list[1]->teamnum,
            save.team_list[2]->teamnum, save.team_list[3]->teamnum};

        for (const og::ui::LineupSplit mode :
             {og::ui::LineupSplit::Even, og::ui::LineupSplit::Fair,
              og::ui::LineupSplit::AllToFirst}) {
            std::vector<std::string> report;
            const int moved =
                og::ui::terminal_apply_lineup_split(save, mode, report);
            EXPECT_EQ(0, moved) << "no bulk write past a cleared can_team";
            ASSERT_EQ(2u, report.size());
            EXPECT_EQ("Moved 0 fighters.", report[0]);
            EXPECT_EQ("4 fighters are locked and stayed put.", report[1]);
            for (int i = 0; i < 4; ++i) {
                EXPECT_EQ(before[static_cast<std::size_t>(i)],
                          save.team_list[static_cast<std::size_t>(i)]->teamnum)
                    << "slot " << i;
            }
        }
    }
}

// With no og.register_campaign_hooks anywhere the camp door refuses with the
// shared guard line — pinned verbatim here, exactly once. The DEFAULT
// composition is a full-capability roster, and both terminals already carry
// that on their own Team Build rows, so the door says so instead of opening
// a second copy of the roster.
TEST(PlatformHeadless, text_picker_camp_without_a_zone_prints_the_guard)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    const std::string input =
        "2\n"       // main: continue -> team build
        "7\n"       // team build: camp -> guard line, straight back
        "8\n"       // team build: back -> main
        "6\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_NE(std::string::npos,
              out.find("This campaign keeps no camp.\n"))
        << "a campaign that composed no camp must print the guard line";
    EXPECT_EQ(std::string::npos, out.find("Camp # "))
        << "the guard path must never open the camp prompt";
}
