#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input_state.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/picker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/text_protocol.h>
#include <openglad/interface/walker_render.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/company.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/pixie_data.h>

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
std::string text_protocol_testing_format_event_text(std::string_view text);
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
              mount_campaign_package_with_error("org.openglad.gladiator"));

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
        args.campaign = "org.openglad.missing-protocol-campaign";
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
              mount_campaign_package_with_error("org.openglad.westlands"));

    {
        StdinRedirect input("census\ntick 3\ncensus\nquit\n");
        CoutRedirect output;

        og::ui::TextProtocolArgs args;
        args.campaign = "org.openglad.westlands";
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
        args.campaign = "org.openglad.westlands";
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
                  mount_campaign_package_with_error("org.openglad.gladiator"));
}

TEST(PlatformHeadless, text_protocol_serializes_shipped_ctf_state)
{
    restore_default_campaigns();
    struct RestoreDefaultCampaignMount
    {
        ~RestoreDefaultCampaignMount()
        {
            (void)mount_campaign_package_with_error("org.openglad.gladiator");
        }
    } restore_default_campaign_mount;

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.ctf"));

    StdinRedirect input("tick 1\nstate\nquit\n");
    CoutRedirect output;

    og::ui::TextProtocolArgs args;
    args.campaign = "org.openglad.ctf";
    args.level = 500;
    args.team_families = {FAMILY_SOLDIER};
    args.seed = 7;
    EXPECT_EQ(0, og::ui::run_text_protocol_session(args));

    const std::string text = output.str();
    EXPECT_NE(std::string::npos,
              text.find("\"status\":\"ready\",\"level\":500,"
                        "\"title\":\"CTF: FIRST BLOOD\""));
    EXPECT_NE(std::string::npos,
              text.find("\"cmd\":\"tick\",\"count\":1"));
    EXPECT_NE(std::string::npos, text.find("\"cmd\":\"state\""));

    const std::string expected_ctf =
        "\"ctf\":{\"active\":true,\"team_count\":2,"
        "\"capture_limit\":3,\"winner_team\":-1,"
        "\"captures\":[0,0,0,0],"
        "\"flags\":[{\"team\":0,\"state\":0,\"carrier\":0,"
        "\"x\":80,\"y\":240},{\"team\":1,\"state\":0,"
        "\"carrier\":0,\"x\":544,\"y\":240}],"
        "\"cps\":[{\"owner\":-1,\"x\":320,\"y\":240}]}";
    EXPECT_NE(std::string::npos, text.find(expected_ctf))
        << "the shipped two-flag/one-control-point arena must retain the "
           "protocol's complete CTF JSON shape";
    EXPECT_NE(std::string::npos,
              text.find("\"cmd\":\"quit\",\"status\":\"ok\""));

    EXPECT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));
}

TEST(PlatformHeadless, text_protocol_event_text_is_valid_json_escaped)
{
    const std::string encoded = og::ui::text_protocol_testing_format_event_text(
        "quote\" slash\\ backspace\b formfeed\f newline\n tab\t carriage\r ctrl\x01");

    EXPECT_NE(std::string::npos, encoded.find("\"tick\":7"));
    EXPECT_NE(std::string::npos, encoded.find("\"kind\":8"));
    EXPECT_NE(std::string::npos,
              encoded.find("\"text\":\"quote\\\" slash\\\\ backspace\\b formfeed\\f newline\\n tab\\t carriage\\r ctrl\\u0001\""));
    EXPECT_EQ(std::string::npos, encoded.find('\n'))
        << "event JSON must not contain raw newlines inside strings";
    EXPECT_EQ(std::string::npos, encoded.find('\r'))
        << "event JSON must not contain raw carriage returns inside strings";
}

TEST(PlatformHeadless, text_picker_drives_menu_options_team_and_campaign_paths)
{
    // Team Build is 12 items (§2.5 in-place substitution: 1=roster,
    // 4=deploy, 5=ready; 7=back, 8=networking, 9=Scenario); the
    // scenario-shaped commands nest under the Scenario submenu
    // (1=set_campaign, 2=set_level, 3=view_scenario, 4=matchup, 5=progress,
    // 6=back). Main 3=difficulty opens the DIFFICULTY submenu
    // (1=difficulty, 2=respawns, 3=respawn delay, 4=permadeath,
    // 5=generators, 6=infinite gold, 7=back).
    const std::string input =
        "bad\r\n"   // main: invalid choice; terminal CR is trimmed
        "3\n"       // main: difficulty -> DIFFICULTY submenu
        "1\n"       // difficulty: cycle difficulty
        "2\n"       // difficulty: cycle respawns
        "3\n"       // difficulty: cycle respawn delay
        "4\n"       // difficulty: toggle permadeath
        "5\n"       // difficulty: cycle generators
        "6\n"       // difficulty: toggle infinite gold
        "6\n"       // difficulty: toggle infinite gold back off
        "7\n"       // difficulty: back -> main
        "4\n"       // main: level edit (unavailable)
        "5\n"       // main: options
        "newslot\n"
        "not-a-seed\n"
        "5\n"       // main: options again
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
        "9\n"       // team build: Scenario submenu
        "5\n"       // scenario: progress
        "2\n"       // scenario: set level (invalid value)
        "0\n"
        "2\n"       // scenario: set level -> 2
        "2\n"
        "1\n"       // scenario: set campaign (invalid selection)
        "999\n"
        "1\n"       // scenario: set campaign (blank keeps current)
        "\n"
        "6\n"       // scenario: back -> team build
        "8\n"       // team build: networking (unavailable)
        "7\n"       // team build: back -> main
        "7\n";      // main: quit

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
    EXPECT_EQ(2, config.level);
    ASSERT_GE(config.team_families.size(), 2u);
}

TEST(PlatformHeadless, text_picker_internal_help_and_error_paths)
{
    // Order-independent: install the packages and mount the default one the
    // internal paths read scenarios from.
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));

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

TEST(PlatformHeadless, text_picker_reports_protocol_start_failure)
{
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));

    StdinRedirect input(
        "2\n"  // main: continue -> team build
        "6\n"  // team build: GO! attempts the invalid level
        "7\n"  // failed session returns to team build: back
        "7\n"); // main: quit
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
              mount_campaign_package_with_error("org.openglad.ctf"))
        << "the ctf package ships with the game and should mount";

    const std::string input =
        "1\n"       // main: begin new game -> §2.2 name entry
        "\n"        //   name entry: blank accepts the generated company name
        "\n"        //   campaign select: blank keeps current (= default reset)
        "7\n"       // team build: back -> main
        "7\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutSilencer stdout_silencer;

    og::ui::TextPickerConfig config;
    config.campaign = "org.openglad.ctf"; // stale campaign from a prior session
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("org.openglad.gladiator", config.campaign)
        << "a new game must reset the session campaign to the default";
    EXPECT_EQ("org.openglad.gladiator", get_mounted_campaign())
        << "a new game must remount the default campaign package";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));
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
    sd.current_campaign = "org.openglad.gladiator";
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
        "8\n"       // main: load company -> the company list
        "1\n"       //   list: open company...
        "3\n"       //     #3 = corrupt -> damaged message, never switches
        "1\n"       //   list: open company...
        "1\n"       //     #1 = wp3hlb -> loads, slot follows -> team build
        "7\n"       // team build: back -> main
        "8\n"       // main: load company again (active is now wp3hlb)
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
        "7\n";      // main: quit

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
        "8\n"       // main: load company -> the company list
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
        "7\n"       // team build: back -> main
        "7\n";      // main: quit

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

    const std::string id_a = "org.openglad.test_dup_a";
    const std::string id_b = "org.openglad.test_dup_b";
    ASSERT_TRUE(install_titled_package(id_a, "Twin Peaks"));
    ASSERT_TRUE(install_titled_package(id_b, "Twin Peaks"));
    og::data::clear_campaign_metadata_cache(); // drop stale negative entries

    const std::vector<std::string> labels =
        og::ui::format_campaign_select_labels(
            {id_a, id_b, "org.openglad.gladiator"});
    ASSERT_EQ(3u, labels.size());
    EXPECT_EQ("Twin Peaks [org.openglad.test_dup_a]", labels[0]);
    EXPECT_EQ("Twin Peaks [org.openglad.test_dup_b]", labels[1]);
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
              mount_campaign_package_with_error("org.openglad.ctf"))
        << "the ctf package ships with the game and should mount";

    const std::string input =
        "2\n"       // main: continue -> team build
        "9\n"       // team build: Scenario submenu
        "1\n"       // scenario: set campaign
        "1\n"       //   entry 1 is always the default campaign
        "6\n"       // scenario: back -> team build
        "7\n"       // team build: back -> main
        "7\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutSilencer stdout_silencer;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("org.openglad.gladiator", config.campaign);
    EXPECT_EQ("org.openglad.gladiator", get_mounted_campaign())
        << "campaign selection must re-point the mount, not just strings";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));
}

// Users must see human titles, never raw campaign ids: "Gladiator" from the
// package's campaign.yaml and "1. SOUTH OF TALWOOD (BEGINNING)" from the
// scen1.fss header. The selection state keeps the raw id.
TEST(PlatformHeadless, text_picker_shows_display_titles_when_campaign_mounted)
{
    restore_default_campaigns(); // order-independent: install the packages
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));

    const std::string input =
        "2\n"       // main: continue -> team build
        "9\n"       // team build: Scenario submenu (labels print)
        "5\n"       // scenario: progress
        "2\n"       // scenario: set level (blank keeps current)
        "\n"
        "1\n"       // scenario: set campaign (blank keeps current)
        "\n"
        "6\n"       // scenario: back -> team build
        "7\n"       // team build: back -> main
        "7\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.team_families = {FAMILY_SOLDIER};
    og::ui::TextPickerError error;
    og::ui::run_text_picker(config, &error);

    const std::string out = stdout_capture.restore();
    EXPECT_EQ(og::ui::TextPickerErrorCode::None, error.code);
    EXPECT_EQ("org.openglad.gladiator", config.campaign)
        << "display titles must not leak into the selection state";
    EXPECT_EQ("org.openglad.gladiator", get_mounted_campaign());

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
    EXPECT_NE(std::string::npos, out.find("Capture the Flag"))
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
              mount_campaign_package_with_error("org.openglad.ctf"))
        << "the ctf package ships with the game and should mount";

    const std::string input =
        "2\n"       // main: continue -> team build
        "9\n"       // team build: Scenario submenu (labels print)
        "5\n"       // scenario: progress
        "6\n"       // scenario: back -> team build
        "7\n"       // team build: back -> main
        "7\n";      // main: quit

    StdinRedirect stdin_redirect(input);
    CoutRedirect cout_redirect;
    StdoutCapture stdout_capture;

    og::ui::TextPickerConfig config;
    config.campaign = "org.openglad.gladiator"; // diverges from the ctf mount
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
              mount_campaign_package_with_error("org.openglad.gladiator"));
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
        "7\n"        // base camp: back -> main
        "7\n";       // main: quit

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
        "9\n"           // base camp: Scenario submenu
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
        "6\n"           // scenario: back -> team build
        "7\n"           // base camp: back -> main
        "7\n";          // main: quit

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
