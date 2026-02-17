/* SDL-free path helpers for headless clients.
 * Provides get_user_path() and get_asset_path() without SDL dependency.
 * These are normally defined in platform_io.cpp (which includes SDL.h).
 */

#include <openglad/core/util.h>
#include <openglad/core/constants.h>

#include <algorithm>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unistd.h>

// Difficulty globals — normally defined in picker.cpp (UI layer).
// Headless client provides its own definitions.
std::int32_t current_difficulty = 1; // 'normal'
std::int32_t difficulty_level[DIFFICULTY_SETTINGS] = { 50, 100, 200 };

#ifdef _WIN32
#include <direct.h>
#include <shlobj.h>
#include <windows.h>
#endif

std::string get_user_path()
{
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
        return s + "/.openglad/";
    }
    return "";
#else
    const char* home = std::getenv("HOME");
    if (!home) return "./";
    return std::string(home) + "/.openglad/";
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

// Link-time dispatch stubs for level_data.cpp (headless has no SDL render layer)
class LevelData;
class walker;
class screen;
class ILevelRender;
class PixieData;

void clear_stale_view_controls(LevelData*) {}
void level_data_wire_entity_from_screen(walker*) {}
void level_data_draw_impl(LevelData*, screen*) {}
std::unique_ptr<ILevelRender> create_level_render(PixieData[]) { return nullptr; }

// Stubs for treasure_family_navigation.cpp
bool yes_or_no_prompt(const char*, const char*, bool) { return false; }
void clear_keyboard() {}
int get_input_events() { return 0; }

// Stub for stats.cpp
walker* find_follow_leader(walker*) { return nullptr; }

// Stubs for base.h helpers that use OgFile
namespace og { namespace io { class OgFile; } }
short fill_help_array(const char*, og::io::OgFile&, short) { return 0; }
void read_one_line(og::io::OgFile&, short, char*) {}
