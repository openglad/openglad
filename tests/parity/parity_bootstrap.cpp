#include "parity_bootstrap.h"

#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#ifndef OG_PARITY_WORKSPACE_ROOT
#define OG_PARITY_WORKSPACE_ROOT "."
#endif

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
    builtin_path_    = workspace_root_ + "/builtin";

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
        restore_default_campaigns();
        // Best-effort: ignore the error here; later mounts make scen99.fss
        // resolvable even when the campaign mount fails (e.g. archive
        // already present from a sibling init).
        (void)mount_campaign_package_with_error("org.openglad.gladiator");
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
