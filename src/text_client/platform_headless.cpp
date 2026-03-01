/* SDL-free platform layer for headless clients.
 *
 * Provides get_user_path(), get_asset_path(), and link-time dispatch
 * implementations for functions that the game engine expects but that have
 * no SDL dependency in headless mode.
 *
 * Categorisation follows the headless platform completion matrix (section 6.3
 * of docs/over-engineering-audit.md):
 *   A) Implemented via shared helpers in src/io/platform_io_common.cpp
 *   B) Explicit unsupported — one-time warning via std::call_once
 *   C) Safe no-ops with documentation
 *   D) Deferred — returns typed failure with log message
 */

#include <openglad/core/util.h>
#include <openglad/core/constants.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unistd.h>

// current_difficulty lives in GameSession — the text client's headless_session_buf
// in main.cpp provides zero-initialized storage. text_picker sets it at runtime.
// difficulty_level[] moved to src/ui/picker_common.cpp (shared with SDL client)

#ifdef _WIN32
#include <direct.h>
#include <shlobj.h>
#include <windows.h>
#endif

std::string get_user_path()
{
    auto normalize_dir = [](std::string path) {
        while (path.size() > 1 && path.back() == '/') {
            path.pop_back();
        }
        if (path.empty()) {
            return std::string("./");
        }
        if (path.back() != '/') {
            path.push_back('/');
        }
        return path;
    };

    if (const char* config_dir = std::getenv("OPENGLAD_CONFIG_DIR")) {
        if (config_dir[0] != '\0') {
            return normalize_dir(config_dir);
        }
    }

#ifdef _WIN32
    char path[MAX_PATH];
    HRESULT hr = SHGetFolderPath(0, CSIDL_LOCAL_APPDATA, 0, 0, path);
    if (SUCCEEDED(hr)) {
        std::string s = path;
        size_t pos = 0;
        do {
            pos = s.find_first_of('\\', pos);
            if (pos != std::string::npos)
                s[pos] = '/';
        } while (pos != std::string::npos);
        return normalize_dir(s + "/.openglad");
    }
    return "";
#else
    const char* home = std::getenv("HOME");
    if (!home) return "./";
    return normalize_dir(std::string(home) + "/.openglad");
#endif
}

std::string get_asset_path()
{
#ifdef _WIN32
    return "";
#else
    constexpr size_t maxPathSize = 512;
    char path[maxPathSize];
    std::fill_n(path, maxPathSize, '\0');

    const ssize_t read_len = readlink("/proc/self/exe", path, maxPathSize - 1);
    if (read_len < 0) {
        LogError("get_asset_path: readlink(/proc/self/exe) failed\n");
        return "./";
    }
    path[static_cast<size_t>(read_len)] = '\0';

    std::string s = path;
    size_t slash = s.find_last_of('/');
    if (slash != std::string::npos)
        s = s.substr(0, slash);
    s += '/';
    return s;
#endif
}

// ---------------------------------------------------------------------------
// Category C: Safe no-ops (documented)
// ---------------------------------------------------------------------------

#include <openglad/runtime/level_runtime_data.h>
#include <openglad/data/level_data_hooks.h>
#include <openglad/data/gloader.h>

// Safe no-op: view controls are an SDL render concern; headless has no views.
void headless_clear_stale_view_controls(LevelRuntimeData*) {}

// Safe no-op: keyboard buffer is an SDL input concern.
void clear_keyboard() {}

// ---------------------------------------------------------------------------
// Category B: Explicit unsupported (one-time warning)
// ---------------------------------------------------------------------------

namespace {
std::once_flag warn_draw_impl;
std::once_flag warn_yes_or_no;
std::once_flag warn_input_events;
std::once_flag warn_input_state;
std::once_flag warn_find_follow;
} // namespace

void headless_level_data_draw(LevelRuntimeData*, screen*)
{
    std::call_once(warn_draw_impl, [] {
        LogWarn("level_data_draw_impl: not supported in headless mode\n");
    });
}

// LevelRender stubs for headless mode.
// renderer_ is always nullptr in headless, but the linker needs these symbols.
struct LevelRender::Impl {};
LevelRender::LevelRender() {}
LevelRender::~LevelRender() = default;
void LevelRender::init_tiles(PixieData[]) {}
void LevelRender::reset_tiles(PixieData[]) {}
void LevelRender::draw_tile(int, int, int, viewscreen*) {}

std::unique_ptr<LevelRender> headless_create_level_render(PixieData[])
{
    static std::once_flag warn_flag;
    std::call_once(warn_flag, []() { Log("Warning: create_level_render not supported in headless mode\n"); });
    return nullptr;
}

EntityFactory headless_create_entity_factory()
{
    EntityFactory factory;
    // Headless mode intentionally leaves attach_render empty.
    factory.report_error = [](const std::string& message) {
        LogError("{}\n", message);
    };
    return factory;
}

loader* headless_entity_loader()
{
    static auto game_loader = std::make_unique<loader>(headless_create_entity_factory());
    return game_loader.get();
}

void wire_world_with_loader(GameWorld* world, loader* game_loader)
{
    if (world == nullptr || game_loader == nullptr)
        return;

    world->entity_factory = [game_loader](Order order, std::int32_t family) -> std::unique_ptr<walker> {
        return game_loader->create_walker_owned(order, family);
    };

    world->entity_configurator = [game_loader](walker& entity, Order order, std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(), entity.family);
    };

    world->entity_derived_stats = [game_loader](walker* entity, Order order, std::int32_t family) {
        if (entity == nullptr)
            return;
        game_loader->set_derived_stats(entity, order, family);
    };
}

void headless_wire_world_entity_services(GameWorld* world, LevelRuntimeData* level)
{
    (void)level;
    wire_world_with_loader(world, headless_entity_loader());
}

const LevelDataHooks kHeadlessLevelDataHooks{
    .clear_stale_view_controls = headless_clear_stale_view_controls,
    .draw = headless_level_data_draw,
    .create_level_render = headless_create_level_render,
    .create_entity_factory = headless_create_entity_factory,
    .wire_world_entity_services = headless_wire_world_entity_services,
};

bool yes_or_no_prompt(const char* /*title*/, const char* /*message*/, bool default_value)
{
    std::call_once(warn_yes_or_no, [] {
        LogWarn("yes_or_no_prompt: not supported in headless mode, returning default\n");
    });
    return default_value;
}

int get_input_events()
{
    std::call_once(warn_input_events, [] {
        LogWarn("get_input_events: not supported in headless mode\n");
    });
    return 0;
}

// Stub for stats.cpp — headless has no follow-leader concept
walker* find_follow_leader()
{
    std::call_once(warn_find_follow, [] {
        LogWarn("find_follow_leader: not supported in headless mode\n");
    });
    return nullptr;
}

// ---------------------------------------------------------------------------
// Stubs for base.h helpers that use OgFile
// ---------------------------------------------------------------------------
#include <openglad/io/og_file.h>
#include <openglad/legacy/base.h>

short end_of_file = 0;

std::string read_one_line(og::io::OgFile& infile, short length)
{
    char temp;
    std::string newline;
    newline.reserve(static_cast<size_t>(length));
    for (short i = 0; i < length; i++) {
        size_t n = infile.read(&temp, 1, 1);
        if (n != 1) { end_of_file = 1; return newline; }
        if (temp == '\n' || temp == '\r') return newline;
        newline.push_back(temp);
    }
    return newline;
}

short fill_help_array(char somearray[HELP_WIDTH][MAX_LINES], og::io::OgFile& infile)
{
    short i;
    for (i = 0; i < MAX_LINES; i++) {
        std::string someline = read_one_line(infile, HELP_WIDTH);
        snprintf(somearray[i], HELP_WIDTH, "%s", someline.c_str());
        if (end_of_file) return i;
    }
    return MAX_LINES;
}

// ---------------------------------------------------------------------------
// Platform I/O
// Campaign/list/archive/fs helpers are now in src/io/platform_io_common.cpp
// (shared by both SDL and headless builds).
// ---------------------------------------------------------------------------

#include <openglad/platform/io_common.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/guy.h>
#include <openglad/data/pixie_data.h>
#include <openglad/input/input_state.h>

bool save_settings() { return cfg.save_settings(); }
bool load_settings() { return cfg.load_settings(); }

// ---------------------------------------------------------------------------
// Category D: Editor-adjacent stubs (deferred — typed failure + log)
// ---------------------------------------------------------------------------

namespace {
std::once_flag warn_editor_create;
std::once_flag warn_load_map;
} // namespace

NewFileIoError create_new_map_pix_with_error(const std::string& filename, int, int)
{
    std::call_once(warn_editor_create, [] {
        LogWarn("Editor file creation not supported in headless mode\n");
    });
    LogError("create_new_map_pix_with_error: unsupported in headless mode (file={})\n", filename);
    return NewFileIoError::OpenWriteFailed;
}

NewFileIoError create_new_pix_with_error(const std::string& filename, int, int, unsigned char)
{
    LogError("create_new_pix_with_error: unsupported in headless mode (file={})\n", filename);
    return NewFileIoError::OpenWriteFailed;
}

NewFileIoError create_new_campaign_descriptor_with_error(const std::string& filename)
{
    LogError("create_new_campaign_descriptor_with_error: unsupported in headless mode (file={})\n", filename);
    return NewFileIoError::OpenWriteFailed;
}

NewFileIoError create_new_scen_file_with_error(const std::string& scenfile, const std::string&)
{
    LogError("create_new_scen_file_with_error: unsupported in headless mode (file={})\n", scenfile);
    return NewFileIoError::OpenWriteFailed;
}


void load_map_data(PixieData*)
{
    std::call_once(warn_load_map, [] {
        LogWarn("load_map_data: not supported in headless mode\n");
    });
}

// ---------------------------------------------------------------------------
// Headless lifecycle (mirrors SDL io_init/io_exit/sync_filesystem)
// ---------------------------------------------------------------------------
#include <openglad/io/physfs_api.h>

void io_init(int argc, char* argv[])
{
    (void)argc;

    // Create user directory tree
    std::string user_path = get_user_path();
    create_dir(user_path);
    create_dir(user_path + "campaigns/");
    create_dir(user_path + "save/");
    create_dir(user_path + "cfg/");

    // Initialize PhysFS
    if (!og::io::physfs_init(argv[0])) {
        LogError("io_init(headless): physfs_init failed\n");
        return;
    }
    if (!og::io::physfs_set_write_dir(user_path)) {
        LogError("io_init(headless): Failed to set write dir: {}\n", user_path);
        return;
    }

    if (!og::io::physfs_mount(user_path, nullptr, 1)) {
        LogError("io_init(headless): Failed to mount user path: {}\n", user_path);
    }

    // Copy default campaign from asset directory to user directory
    restore_default_campaigns();

    // Mount default campaign
    if (mount_campaign_package_with_error("org.openglad.gladiator") != CampaignPackageIoError::None) {
        LogError("io_init(headless): Failed to mount default campaign\n");
    }

    // Mount asset directories
    std::string asset_path = get_asset_path();
    og::io::physfs_mount(asset_path + "pix/", "pix/", 1);
    og::io::physfs_mount(asset_path + "sound/", "sound/", 1);
    og::io::physfs_mount(asset_path + "cfg/", "cfg/", 1);

    Log("io_init(headless): done\n");
}

void io_exit()
{
    og::io::physfs_deinit();
}

// No-op on non-web platforms (they use real filesystem)
void sync_filesystem() {}

int toInt(const std::string& s)
{
    try { return std::stoi(s); }
    catch (...) { return 0; }
}

// Category B: SDL input sampling not available in headless mode
void input_state_from_sdl(InputState&)
{
    std::call_once(warn_input_state, [] {
        LogWarn("input_state_from_sdl: not supported in headless mode\n");
    });
}

void emit_headless_unsupported_warnings_probe()
{
    const LevelDataHooks& hooks = headless_level_data_hooks();
    // Intentionally call each unsupported API twice; std::call_once-backed warnings
    // must still emit only once per process.
    hooks.draw(nullptr, nullptr);
    hooks.draw(nullptr, nullptr);

    (void)hooks.create_level_render(nullptr);
    (void)hooks.create_level_render(nullptr);

    (void)yes_or_no_prompt("probe", "probe", true);
    (void)yes_or_no_prompt("probe", "probe", true);

    (void)get_input_events();
    (void)get_input_events();

    (void)find_follow_leader();
    (void)find_follow_leader();

    load_map_data(nullptr);
    load_map_data(nullptr);

    InputState input{};
    input_state_from_sdl(input);
    input_state_from_sdl(input);
}

const LevelDataHooks& headless_level_data_hooks()
{
    return kHeadlessLevelDataHooks;
}

// SaveData is now provided by the real src/runtime/save_data.cpp
// (linked into openglad_text via HEADLESS_SOURCES).

// popup_dialog is defined in main.cpp for the text client
