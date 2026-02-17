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
#include <openglad/data/level_render.h>
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
walker* find_follow_leader() { return nullptr; }

// Stubs for base.h helpers that use OgFile
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

// --- Platform I/O stubs (headless: no campaign/archive support) ---
#include <openglad/platform/io_common.h>
#include <openglad/data/save_data.h>
#include <openglad/entities/guy.h>
#include <openglad/data/pixie_data.h>
#include <openglad/input/input_state.h>

std::list<std::string> list_files(const std::string&) { return {}; }
std::list<std::string> explode(const std::string& str, char delimiter)
{
    std::list<std::string> result;
    std::string token;
    for (char c : str) {
        if (c == delimiter) { result.push_back(token); token.clear(); }
        else token += c;
    }
    if (!token.empty()) result.push_back(token);
    return result;
}

std::string get_mounted_campaign() { return "org.openglad.gladiator"; }
bool mount_campaign_package(const std::string&) { return false; }
bool unmount_campaign_package(const std::string&) { return false; }
bool remount_campaign_package() { return false; }
CampaignPackageIoError mount_campaign_package_with_error(const std::string&) { return CampaignPackageIoError::MountFailed; }
CampaignPackageIoError unmount_campaign_package_with_error(const std::string&) { return CampaignPackageIoError::UnmountFailed; }
CampaignPackageIoError remount_campaign_package_with_error() { return CampaignPackageIoError::MountFailed; }
std::list<std::string> list_campaigns() { return {}; }
std::list<int> list_levels() { return {}; }
std::vector<int> list_levels_v() { return {}; }

void restore_default_campaigns() {}
void restore_default_settings() {}
bool save_settings() { return false; }
bool load_settings() { return false; }
void delete_level(int) {}
void delete_campaign(const std::string&) {}

ArchiveIoError zip_contents_with_error(const std::string&, const std::string&) { return ArchiveIoError::OpenArchiveFailed; }
ArchiveIoError unzip_into_with_error(const std::string&, const std::string&) { return ArchiveIoError::OpenArchiveFailed; }
bool zip_contents(const std::string&, const std::string&) { return false; }
bool unzip_into(const std::string&, const std::string&) { return false; }

bool unpack_campaign(const std::string&) { return false; }
bool repack_campaign(const std::string&) { return false; }
void cleanup_unpacked_campaign() {}

NewFileIoError create_new_map_pix_with_error(const std::string&, int, int) { return NewFileIoError::OpenWriteFailed; }
NewFileIoError create_new_pix_with_error(const std::string&, int, int, unsigned char) { return NewFileIoError::OpenWriteFailed; }
NewFileIoError create_new_campaign_descriptor_with_error(const std::string&) { return NewFileIoError::OpenWriteFailed; }
NewFileIoError create_new_scen_file_with_error(const std::string&, const std::string&) { return NewFileIoError::OpenWriteFailed; }
bool create_new_map_pix(const std::string&, int, int) { return false; }
bool create_new_pix(const std::string&, int, int, unsigned char) { return false; }
bool create_new_campaign_descriptor(const std::string&) { return false; }
bool create_new_scen_file(const std::string&, const std::string&) { return false; }

void load_map_data(PixieData*) {}
bool create_dir(const std::string&) { return false; }
void io_init(int, char*[]) {}
void io_exit() {}
void sync_filesystem() {}

int toInt(const std::string& s)
{
    try { return std::stoi(s); }
    catch (...) { return 0; }
}

void input_state_from_sdl(InputState&) {}

// --- SaveData stubs (headless: no save/load) ---
SaveData::SaveData()
    : scen_num(1), score(0), totalcash(0), totalscore(0), team_size(0), numplayers(1), allied_mode(0)
{
    std::fill(std::begin(m_score), std::end(m_score), 0);
    std::fill(std::begin(m_totalcash), std::end(m_totalcash), 0);
    std::fill(std::begin(m_totalscore), std::end(m_totalscore), 0);
}
SaveData::~SaveData() = default;
bool SaveData::load(const std::string&) { return false; }
bool SaveData::save(const std::string&) { return false; }
bool SaveData::is_level_completed(int) const { return false; }

// popup_dialog is defined in main.cpp for the text client
