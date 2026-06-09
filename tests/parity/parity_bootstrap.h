// Parity harness bootstrap — PhysFS + campaign + scenario search-path setup.
//
// SYNCHRONIZE WITH ../openglad-master/tools/parity_bootstrap.h
// Mirrored byte-for-byte at the master companion. Branch and master both
// initialise PhysFS, mount the default campaign archive, and add the project
// tree's `temp/scen/` and `scen/` directories to the search path so that
// `scen{id}.fss` resolves regardless of where the binary is launched.
//
// Compile-time `OG_PARITY_WORKSPACE_ROOT` is defined by each side's
// CMakeLists.txt to the absolute path of its own working tree (branch
// `/home/yans/code/openglad`; master `/home/yans/code/openglad-master`).
// The bootstrap reads `OG_PARITY_WORKSPACE_ROOT` from the environment first
// and falls back to that define so the test binaries can be relocated.

#pragma once

#include <string>

namespace og::parity {

// Initialise PhysFS, restore the bundled `org.openglad.gladiator` campaign,
// mount the project's `temp/scen/` and `scen/` directories at PhysFS
// mount-point `scen/`, and install `sdl_level_data_hooks()` so loaded
// walkers get their render-component / entity-services wiring.
//
// `argv0` is passed straight to PhysFS so it can find its own base dir.
// Safe to call repeatedly: a second invocation skips work that has already
// happened, but each instance still reference-counts so destruction is
// symmetrical.
class BootstrapScope
{
public:
    explicit BootstrapScope(const char* argv0 = "parity");
    ~BootstrapScope();

    BootstrapScope(const BootstrapScope&) = delete;
    BootstrapScope& operator=(const BootstrapScope&) = delete;
    BootstrapScope(BootstrapScope&&) = delete;
    BootstrapScope& operator=(BootstrapScope&&) = delete;

    // Was PhysFS owned (initialised) by this scope? If false, another
    // bootstrap or io_init() was already in effect when the scope started.
    bool owned_physfs() const noexcept { return owned_physfs_; }

    // The resolved workspace root used to find `temp/scen/` and `scen/`.
    const std::string& workspace_root() const noexcept { return workspace_root_; }

private:
    bool        owned_physfs_   = false;
    bool        mounted_temp_   = false;
    bool        mounted_scen_   = false;
    bool        mounted_builtin_ = false;
    bool        mounted_parity_write_ = false;
    std::string workspace_root_;
    std::string parity_write_path_;
    std::string temp_scen_path_;
    std::string scen_path_;
    std::string builtin_path_;
};

// Resolve the workspace root: env `OG_PARITY_WORKSPACE_ROOT` if set,
// otherwise the compile-time define of the same name.
std::string parity_workspace_root();

} // namespace og::parity
