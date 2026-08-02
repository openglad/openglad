/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// A Lua-declaring pack, mounted for real, through the whole installer.
//
// The two tests above this one in the stack use the declaration pass and
// the bind pass in isolation. This one mounts a directory containing
// packs/<id>/families/*.lua the way a campaign or a mod arrives, and pins
// the property the whole two-context design exists for:
//
//     DATA INSTALLS ONCE PER CONTENT CHANGE. BEHAVIOR BINDS PER VM.
//
// A declaration is a PROGRAM. Evaluating it is not free — it compiles a
// chunk, builds a VM and runs the author's loops — and a session builds a
// world VM every time it starts a level. If the declaration pass ran on
// each of those, a pack with a hundred families would pay for itself on
// every level load, and worse, the installer would be re-writing registry
// slots underneath a running game.
//
// So the declaration rides the same exact-bytes memo the YAML parse always
// had, widened to cover families/ and lib/ (a lib module is an input to a
// declaration through og.use, so a module edit really can change what a
// family declares). The tests here check both directions: unchanged bytes
// must not re-declare, and changed bytes must.

#include <gtest/gtest.h>

#include "family_registry_dump.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/packs.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr const char* kPackId = "v3inst";
constexpr const char* kMountPoint = "packs/v3inst/";
constexpr const char* kFamilyId = "v3inst:sparkle";

// An EFFECT family: it installs into a free mod slot, nothing in the picker
// or the wire protocol depends on the effect count, and a leak would be
// visible rather than load-bearing.
std::string sparkle_chunk(int lifetime_tuning)
{
    return "og.pack{ id = 'v3inst', version = '1.0', title = 'Install Test' }\n"
           "og.anims('sparkle', { rows = 2, frames = { {0,1,2}, false } })\n"
           "og.family('effect', {\n"
           "  id = '" + std::string(kFamilyId) + "',\n"
           "  wire_id = 'auto',\n"
           "  name = 'SPARKLE',\n"
           "  loops_animation = true,\n"
           "  animation = 'sparkle',\n"
           "  tuning = { lifetime = " + std::to_string(lifetime_tuning) +
           " },\n"
           "  on_act = function(self) return true end,\n"
           "})\n";
}

// The pack tree lives in the user directory (the only place a unit binary
// may write) and is mounted onto packs/ like any other pack source.
class SyntheticLuaPack {
public:
    explicit SyntheticLuaPack(const std::string& chunk)
        : root_(std::filesystem::path(get_user_path()) / "v3_lua_pack")
    {
        std::error_code ec;
        std::filesystem::create_directories(root_ / "families", ec);
        write_family(chunk);
        mounted_ = og::resources::mount(root_.string().c_str(), kMountPoint, 1);
        og::resources::refresh_pack_scripts();
    }

    ~SyntheticLuaPack()
    {
        if (mounted_)
            (void)og::resources::unmount(root_.string().c_str());
        og::resources::refresh_pack_scripts();
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    SyntheticLuaPack(const SyntheticLuaPack&) = delete;
    SyntheticLuaPack& operator=(const SyntheticLuaPack&) = delete;

    [[nodiscard]] bool ok() const { return mounted_; }

    // Rewrite the declaration and re-run the mount-time refresh, exactly as
    // a re-mount would.
    void rewrite(const std::string& chunk)
    {
        write_family(chunk);
        og::resources::refresh_pack_scripts();
    }

private:
    void write_family(const std::string& chunk) const
    {
        std::ofstream out(root_ / "families" / "sparkle.lua",
                          std::ios::binary | std::ios::trunc);
        out << chunk;
    }

    std::filesystem::path root_;
    bool mounted_ = false;
};

int installed_family()
{
    return og::families::resolve_family_string_id(Order::FX, kFamilyId);
}

}  // namespace

TEST(LuaPackInstall, a_lua_only_pack_installs_its_families)
{
    SyntheticLuaPack pack(sparkle_chunk(30));
    ASSERT_TRUE(pack.ok()) << "mount failed: "
                           << og::resources::filesystem_last_error();

    // No classpack.yaml anywhere in this pack: `docs.empty()` used to mean
    // "nothing to install", and a Lua-only tree is data-bearing now.
    const int family = installed_family();
    ASSERT_GE(family, 0) << "the declaration did not install";
    const EffectFamilyDescriptor* fd = get_effect_family_descriptor(family);
    ASSERT_NE(nullptr, fd);
    EXPECT_STREQ("SPARKLE", fd->name);
    EXPECT_TRUE(fd->loops_animation);
    EXPECT_STREQ(kFamilyId, fd->declared_id);

    // og.anims reached the descriptor, null row and all.
    ASSERT_NE(nullptr, fd->anim_table);
    EXPECT_EQ(2, fd->anim_row_count);
    ASSERT_NE(nullptr, fd->anim_table[0]);
    EXPECT_EQ(0, fd->anim_table[0][0]);
    EXPECT_EQ(2, fd->anim_table[0][2]);
    EXPECT_EQ(-1, fd->anim_table[0][3]) << "rows are -1 terminated";

    const og::script::TuningMap* tuning =
        og::script::family_tuning(Order::FX, family);
    ASSERT_NE(nullptr, tuning);
    ASSERT_EQ(1u, tuning->size());
    EXPECT_EQ("lifetime", (*tuning)[0].key);
    EXPECT_EQ(30, (*tuning)[0].value.integer);

    // The provenance the V3 rule keys off: this slot is a Lua declaration's.
    const std::string* owner =
        og::script::lua_declared_family_pack(Order::FX, family);
    ASSERT_NE(nullptr, owner);
    EXPECT_EQ(kPackId, *owner);
}

// The headline. Building world VMs must not re-run the declaration: the
// data half already happened, once, at mount.
TEST(LuaPackInstall, building_world_vms_does_not_reinstall)
{
    SyntheticLuaPack pack(sparkle_chunk(30));
    ASSERT_TRUE(pack.ok());
    const int family = installed_family();
    ASSERT_GE(family, 0);

    og::resources::reset_pack_install_stats();
    const unsigned store_before = og::resources::pack_install_stats().store_objects;

    {
        og::script::WorldScripts first;
        og::script::WorldScripts second;
        // Both VMs bound the same declaration's behavior...
        EXPECT_TRUE(first.host().errors().empty())
            << first.host().errors().front().message;
        EXPECT_TRUE(second.host().errors().empty())
            << second.host().errors().front().message;
        EXPECT_TRUE(first.has_hook(Order::FX, family,
                                   og::script::FamilyHook::EffectOnAct));
        EXPECT_TRUE(second.has_hook(Order::FX, family,
                                    og::script::FamilyHook::EffectOnAct));
    }

    const auto stats = og::resources::pack_install_stats();
    EXPECT_EQ(0u, stats.installs)
        << "building a world VM must not run the installer";
    EXPECT_EQ(0u, stats.pack_parses)
        << "building a world VM must not re-declare a pack's families";
    EXPECT_EQ(store_before, og::resources::pack_install_stats().store_objects)
        << "the process-lifetime pack store grew without a content change";
    EXPECT_EQ(family, installed_family())
        << "the family's wire id moved under the VMs";
}

// The memo, from the other side: a repeat mount-time refresh over unchanged
// bytes must be served from it. A campaign switch rescans packs/, so a pack
// that re-declared on every rescan would grow the process store without
// bound.
TEST(LuaPackInstall, an_unchanged_pack_is_served_from_the_parse_memo)
{
    SyntheticLuaPack pack(sparkle_chunk(30));
    ASSERT_TRUE(pack.ok());
    const int family = installed_family();
    ASSERT_GE(family, 0);

    og::resources::reset_pack_install_stats();
    const unsigned store_before =
        og::resources::pack_install_stats().store_objects;
    og::resources::refresh_pack_scripts();
    const auto stats = og::resources::pack_install_stats();

    EXPECT_EQ(0u, stats.pack_parses)
        << "unchanged bytes must not be parsed or re-declared";
    EXPECT_GT(stats.pack_parse_reuses, 0u) << "the memo did not answer";
    EXPECT_EQ(store_before, stats.store_objects)
        << "a memo hit must not add to the pack store";
    EXPECT_EQ(family, installed_family());
}

TEST(LuaPackInstall, changing_a_declaration_reinstalls_it)
{
    SyntheticLuaPack pack(sparkle_chunk(30));
    ASSERT_TRUE(pack.ok());
    ASSERT_GE(installed_family(), 0);

    og::resources::reset_pack_install_stats();
    pack.rewrite(sparkle_chunk(45));

    EXPECT_GT(og::resources::pack_install_stats().pack_parses, 0u)
        << "changed family bytes must re-run the declaration pass";
    const int family = installed_family();
    ASSERT_GE(family, 0);
    const og::script::TuningMap* tuning =
        og::script::family_tuning(Order::FX, family);
    ASSERT_NE(nullptr, tuning);
    ASSERT_EQ(1u, tuning->size());
    EXPECT_EQ(45, (*tuning)[0].value.integer)
        << "the edit did not reach the installed registry";
}

// A long-lived VM has to notice. The build generation folds the family
// chunks in beside the scripts and the lib modules, so a declaration-only
// edit rebuilds every host that caches one.
TEST(LuaPackInstall, a_declaration_edit_bumps_the_vm_build_generation)
{
    SyntheticLuaPack pack(sparkle_chunk(30));
    ASSERT_TRUE(pack.ok());

    og::script::WorldScripts before;
    EXPECT_EQ(og::script::pack_scripts_build_generation(),
              before.built_generation());

    pack.rewrite(sparkle_chunk(45));
    EXPECT_NE(og::script::pack_scripts_build_generation(),
              before.built_generation())
        << "a VM built before the edit must read as stale";
}

// One unusable family file rejects the whole pack — exactly what one
// unusable families/*.yaml always did. The important half is that the
// engine keeps running: a bad pack is refused, never obeyed and never
// fatal.
TEST(LuaPackInstall, a_pack_whose_declaration_fails_installs_nothing)
{
    SyntheticLuaPack pack(sparkle_chunk(30));
    ASSERT_TRUE(pack.ok());
    ASSERT_GE(installed_family(), 0);

    pack.rewrite("og.family('effect', { id = 'v3inst:sparkle',\n"
                 "                      nmae = 'SPARKLE' })\n");

    EXPECT_LT(installed_family(), 0)
        << "a rejected pack must install none of its families";
    EXPECT_TRUE(og::script::pack_declaration_failed(kPackId))
        << "the installer must name the casualty for the bind replay";

    // And the bind replay stays quiet about it, rather than repeating the
    // rejection in every VM the process builds from here on.
    og::script::WorldScripts ws;
    EXPECT_TRUE(ws.host().errors().empty())
        << ws.host().errors().front().message;

    // Fixing the file heals it, with no remount.
    pack.rewrite(sparkle_chunk(30));
    EXPECT_GE(installed_family(), 0);
    EXPECT_FALSE(og::script::pack_declaration_failed(kPackId));
}

// V7. The installer copies and patches: a second declaration of one id
// changes the fields it spells and leaves the rest alone. That is what
// makes a data-only override possible — a mod that wants a family cheaper
// should not have to restate its sprite, its animation and its tuning.
TEST(LuaPackInstall, a_later_declaration_patches_the_fields_it_spells)
{
    SyntheticLuaPack pack(sparkle_chunk(30));
    ASSERT_TRUE(pack.ok());
    const int slot = installed_family();
    ASSERT_GE(slot, 0);

    // The override names the SLOT, not just the id: `wire_id = "auto"`
    // means "give me a free one", so an override that repeated it would
    // install a second family beside the first rather than patch it. That
    // is the pre-existing rule and it is why the shipped families all pin
    // their wire ids.
    pack.rewrite(sparkle_chunk(30) +
                 "og.family('effect', { id = '" + std::string(kFamilyId) +
                 "', wire_id = " + std::to_string(slot) +
                 ", name = 'SPARKLE II' })\n");

    const int family = installed_family();
    ASSERT_EQ(slot, family);
    const EffectFamilyDescriptor* fd = get_effect_family_descriptor(family);
    ASSERT_NE(nullptr, fd);
    EXPECT_STREQ("SPARKLE II", fd->name) << "the override's field";
    EXPECT_TRUE(fd->loops_animation) << "a field the override did not spell";
    EXPECT_EQ(2, fd->anim_row_count) << "the frame table survived";
}

// The golden harness (tests/unit/family_registry_dump.h) is what C2's
// migration proof rests on, and one of the fields it has to render is the
// pack-shipped frame table — which no shipped core family has, because they
// all name a built-in animation. This pack does.
TEST(LuaPackInstall, the_dump_renders_a_pack_shipped_frame_table)
{
    SyntheticLuaPack pack(sparkle_chunk(30));
    ASSERT_TRUE(pack.ok());
    ASSERT_GE(installed_family(), 0);

    const std::string dump = og::testing::dump_installed_families();
    EXPECT_NE(std::string::npos, dump.find("  anim_row_count = 2\n"));
    EXPECT_NE(std::string::npos, dump.find("  anim_table[0] = 0 1 2\n"))
        << "frame rows render up to the -1 sentinel";
    EXPECT_NE(std::string::npos, dump.find("  anim_table[1] = ~\n"))
        << "a null row is a hole the pack punched on purpose";
    EXPECT_NE(std::string::npos, dump.find("  tuning[lifetime] = int 30\n"));
    EXPECT_NE(std::string::npos, dump.find("  declared_id = \"v3inst:sparkle\"\n"));
}

// Unmounting must take the family with it. The pack registries are rebuilt
// all-or-nothing from the mounted set, so a family that outlived its mount
// would be a slot nothing owns.
TEST(LuaPackInstall, unmounting_the_pack_removes_its_families)
{
    int family = -1;
    {
        SyntheticLuaPack pack(sparkle_chunk(30));
        ASSERT_TRUE(pack.ok());
        family = installed_family();
        ASSERT_GE(family, 0);
    }
    EXPECT_LT(installed_family(), 0)
        << "the family outlived the mount that declared it";
    EXPECT_EQ(nullptr, og::script::lua_declared_family_pack(Order::FX, family))
        << "the Lua-declared ledger must be rebuilt with the registries";
}
