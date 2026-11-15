#pragma once

// ScopedSettingsFile: the settings file around a test that leaves a terminal
// Game Settings page (#525), which now writes cfg/openglad.yaml.
//
// On entry it points the PhysFS write directory at the test's private
// OPENGLAD_CONFIG_DIR (the unit and curses test mains set one; a predecessor
// that tore PhysFS down may have left no write directory, and a write with
// none falls back to the working directory). It refuses (ready() false) when
// OPENGLAD_CONFIG_DIR is unset, and then it neither reads nor copies any
// file, so a test can never reach the player's own settings. On exit it puts
// the global cfg's persisted settings and overrides and the file on disk
// back exactly as they were. The write directory stays where the helper
// pointed it (the private directory, the one the test mains choose). The
// cached copy of New Ability Select is not its business: a test that changes
// that setting also takes ScopedAbilitySelect.
//
//     og::test::ScopedSettingsFile settings_file;
//     ASSERT_TRUE(settings_file.ready());
//     ASSERT_TRUE(settings_file.write("gameplay:\n  new_specials: on\n"));
//     ... leave a Game Settings page ...
//     EXPECT_EQ("off", settings_file.load_fresh().get_setting(...));

#include "test_save_state_guard.h"

#include <openglad/resources/filesystem.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace og::test
{

class ScopedSettingsFile
{
public:
    ScopedSettingsFile()
        : data_(cfg.data)
        , overrides_(cfg.overrides)
        , private_dir_(point_write_dir_at_private_config())
    {
        // The file is backed up only inside the private directory.
        if (private_dir_)
            file_.emplace(path());
    }

    ~ScopedSettingsFile()
    {
        cfg.data = data_;
        cfg.overrides = overrides_;
    }

    ScopedSettingsFile(const ScopedSettingsFile&) = delete;
    ScopedSettingsFile& operator=(const ScopedSettingsFile&) = delete;

    [[nodiscard]] bool ready() const { return private_dir_ && file_ && file_->ready(); }

    // The settings file a page writes: cfg/openglad.yaml under the private
    // config directory.
    [[nodiscard]] static std::filesystem::path path()
    {
        return std::filesystem::path(get_user_path()) / "cfg" / "openglad.yaml";
    }

    // Puts `text` on disk as the settings file.
    [[nodiscard]] bool write(std::string_view text) const
    {
        std::ofstream out(path(), std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(out);
    }

    // The bytes on disk; empty when there is no file.
    [[nodiscard]] static std::string bytes()
    {
        std::ifstream in(path(), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }

    // What the next start reads: a store of its own, loaded from the file,
    // never the running game's memory.
    [[nodiscard]] static cfg_store load_fresh()
    {
        cfg_store fresh;
        (void)fresh.load_settings();
        return fresh;
    }

private:
    static bool point_write_dir_at_private_config()
    {
        const char* config_dir = std::getenv("OPENGLAD_CONFIG_DIR");
        if (config_dir == nullptr || config_dir[0] == '\0')
            return false;
        std::error_code error;
        std::filesystem::create_directories(path().parent_path(), error);
        if (error)
            return false;
        const std::string user_path = get_user_path();
        if (!og::resources::set_write_dir(user_path))
            return false;
        // Fails harmlessly when the directory is mounted already.
        (void)og::resources::mount(user_path.c_str(), nullptr, 1);
        return true;
    }

    std::map<std::string, std::map<std::string, std::string>> data_;
    std::map<std::string, std::map<std::string, std::string>> overrides_;
    bool private_dir_;
    std::optional<ScopedPhysicalFileState> file_;
};

} // namespace og::test
