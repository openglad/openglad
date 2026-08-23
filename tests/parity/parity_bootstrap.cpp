#include "parity_bootstrap.h"

#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/packs.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#ifndef OG_PARITY_WORKSPACE_ROOT
#define OG_PARITY_WORKSPACE_ROOT "."
#endif

// Defined in src/resources/platform_io.cpp (and the headless twin); resolves
// the directory the staged assets live in, next to the running binary.
std::string get_asset_path();

namespace og::parity {

std::string parity_workspace_root()
{
    if (const char* env = std::getenv("OG_PARITY_WORKSPACE_ROOT");
        env != nullptr && env[0] != '\0')
    {
        return std::string(env);
    }
    return std::string(OG_PARITY_WORKSPACE_ROOT);
}

namespace {

// PhysFS_init returns true (1) only when it transitioned from un-inited to
// inited; on a subsequent call against an already-live PhysFS state it
// returns false. We use that as our ownership signal.
bool try_init_physfs(const char* argv0)
{
    return og::resources::init(argv0);
}

// Create the parity-write scratch directory if missing. Returns the path.
std::string ensure_write_dir()
{
    std::filesystem::path p = std::filesystem::temp_directory_path() /
                              "openglad-parity-write";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

// Why a filesystem step failed, in words that are worth reading.
//
// PhysFS's error slot only speaks for PhysFS calls; `create_dir` can fail in
// the C library (a `campaigns` path that is already a regular file), and then
// PHYSFS_getLastErrorCode answers ERRNO_OK, whose message is the string "no
// error". A diagnostic that ends in ": no error" is worse than no diagnostic:
// it reads as a contradiction and sends the reader looking in the wrong place.
std::string describe_fs_failure()
{
    const std::string physfs = og::resources::filesystem_last_error();
    if (!physfs.empty() && physfs != "no error")
        return physfs;
    if (errno != 0)
        return std::strerror(errno);
    return "PhysFS reported no error";
}

bool install_parity_grid_fixture(const std::string& write_dir)
{
    std::vector<std::uint8_t> bytes =
        og::resources::read_file("pix/scen0001.png");
    if (bytes.empty())
        bytes = og::resources::read_file("pix/scen1.png");
    if (bytes.empty())
        return false;

    const std::filesystem::path pix_dir =
        std::filesystem::path(write_dir) / "pix";
    std::error_code ec;
    std::filesystem::create_directories(pix_dir, ec);
    if (ec)
        return false;

    std::ofstream out(pix_dir / "grid.png",
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

} // namespace

BootstrapScope::BootstrapScope(const char* argv0)
{
    workspace_root_  = parity_workspace_root();
    parity_write_path_ = ensure_write_dir();
    temp_scen_path_  = workspace_root_ + "/temp/scen";
    scen_path_       = workspace_root_ + "/scen";
    // The campaign archives are BUILD outputs staged next to the running
    // binary (composed from campaigns/ by og_builtin_campaigns); the
    // workspace no longer carries a builtin/ directory.
    builtin_path_    = get_asset_path() + "builtin";

    owned_physfs_ = try_init_physfs(argv0);

    if (owned_physfs_)
    {
        // First-time init: configure write dir + restore default campaign.
        og::resources::set_write_dir(parity_write_path_);
    }

    // The campaign archive lives next to the workspace; mount the directory
    // that contains it so `restore_default_campaigns` can find the .glad.
    if (og::resources::mount(builtin_path_.c_str(), "builtin/", 1))
        mounted_builtin_ = true;

    if (owned_physfs_)
    {
        // `restore_default_campaigns` copies builtin/*.glad with
        // std::filesystem::copy_file, which does NOT create the destination
        // directory. io_init() gets `campaigns/` from create_dataopenglad();
        // an owned bootstrap never runs that, so without this every copy
        // failed with ENOENT and the mount below found nothing — leaving
        // the whole scenario corpus loading empty-level stubs whose dumps
        // are still well-formed JSON.
        const std::string campaigns_dir = get_user_path() + "campaigns/";
        const std::string archive = campaigns_dir + "gladiator.glad";
        if (!create_dir(campaigns_dir))
        {
            // No destination directory means every copy below would fail with
            // ENOENT and print its own WARN line — eight of them, all of them
            // consequences, ahead of the one message that says what actually
            // went wrong. Skip the restore and report.
            failure_ = "cannot create campaign directory " + campaigns_dir +
                       ": " + describe_fs_failure();
        }
        else
        {
            restore_default_campaigns();

            if (!std::filesystem::exists(archive))
            {
                failure_ = "default campaign archive was not restored to " +
                           archive + ": " + describe_fs_failure();
            }
            else if (mount_campaign_package_with_error("gladiator") !=
                     CampaignPackageIoError::None)
            {
                failure_ = "cannot mount default campaign archive " + archive +
                           ": " + describe_fs_failure();
            }
        }
    }

    if (install_parity_grid_fixture(parity_write_path_) &&
        og::resources::mount(parity_write_path_.c_str(), nullptr, 0))
    {
        mounted_parity_write_ = true;
    }

    // Always add the scenario directories so smoke / temp scenarios resolve
    // regardless of whether the campaign archive contains them.
    if (og::resources::mount(temp_scen_path_.c_str(), "scen/", 1))
        mounted_temp_ = true;
    if (og::resources::mount(scen_path_.c_str(),      "scen/", 1))
        mounted_scen_ = true;

    // Class packs: all family behavior AND descriptor data live in pack
    // Lua, so a bootstrap that owns PhysFS (parity_runner_smoke) must
    // mount packs/ and install them or the registries stay empty and no
    // walker can spawn ("Order: 0, Family 0"). Same idempotent shape as
    // og::test::mount_core_pack(): inside og_test_parity, io_init already
    // installed the packs and this is a no-op.
    if (og::script::pack_scripts().empty())
    {
        (void)og::resources::mount((get_asset_path() + "packs/").c_str(),
                                   "packs/", 1);
        og::resources::refresh_pack_scripts();
    }

    // Install entity-service hooks so loaded walkers get their render /
    // entity-services wiring. `sdl_level_data_hooks()` is the SDL-side
    // capability table; `headless_level_data_hooks()` is the SDL-free table
    // used by openglad_text. We pick the SDL hooks on the branch (the test
    // binary already links SDL) and the headless hooks on master (mirrored
    // file picks the symbol available at link time — see master file).
    (void)sdl_level_data_hooks();
}

BootstrapScope::~BootstrapScope()
{
    if (mounted_parity_write_)
        og::resources::unmount(parity_write_path_.c_str());
    if (mounted_temp_)
        og::resources::unmount(temp_scen_path_.c_str());
    if (mounted_scen_)
        og::resources::unmount(scen_path_.c_str());
    if (mounted_builtin_)
        og::resources::unmount(builtin_path_.c_str());

    if (owned_physfs_)
        og::resources::deinit();
}

} // namespace og::parity
