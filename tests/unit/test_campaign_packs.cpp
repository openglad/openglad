/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/packs.h>
#include <openglad/resources/zip_api.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

std::string get_asset_path();

// Campaign-embedded class packs: a .glad carrying packs/<id>/scripts/*.lua
// merges into the virtual packs/ tree on mount, and the pack-script registry
// follows mounts (refresh on mount/unmount).

namespace {

constexpr const char* kPackCampaignId = "org.test.packcarrier";
constexpr const char* kEmbeddedScript = "og.log('embedded pack loaded')\n";

bool script_registered(const char* pack_id)
{
    for (const auto& s : og::script::pack_scripts())
        if (s.pack_id == pack_id)
            return true;
    return false;
}

class CampaignPacksTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        // Unit binaries skip io_init's asset mounts; the shipped packs dir
        // must be reachable for the core-pack assertions (append mount is
        // idempotent).
        (void)og::resources::mount((get_asset_path() + "packs/").c_str(),
                                   "packs/", 1);
        og::resources::refresh_pack_scripts();
        previous_ = get_mounted_campaign();
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error(get_mounted_campaign());
        remove_fake_package();
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

    static bool install_pack_campaign()
    {
        namespace fs = std::filesystem;
        const fs::path staging =
            fs::path(get_user_path()) / "pack_test_staging" / kPackCampaignId;
        std::error_code ec;
        fs::create_directories(staging / "packs" / "embeddedtest" / "scripts",
                               ec);
        if (ec)
            return false;
        {
            std::ofstream out(staging / "campaign.yaml");
            out << "format: 1\ntitle: Pack Carrier\nfirst_level: 1\n";
            if (!out)
                return false;
        }
        {
            std::ofstream out(staging / "packs" / "embeddedtest" / "scripts" /
                              "hello.lua");
            out << kEmbeddedScript;
            if (!out)
                return false;
        }
        const fs::path archive = fs::path(get_user_path()) / "campaigns" /
                                 (std::string(kPackCampaignId) + ".glad");
        return zip_contents_with_error(staging.string(), archive.string()) ==
               ArchiveIoError::None;
    }

    static void remove_fake_package()
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove(fs::path(get_user_path()) / "campaigns" /
                       (std::string(kPackCampaignId) + ".glad"),
                   ec);
        fs::remove_all(fs::path(get_user_path()) / "pack_test_staging", ec);
    }

private:
    std::string previous_;
};

}  // namespace

TEST_F(CampaignPacksTest, embedded_pack_scripts_follow_campaign_mounts)
{
    ASSERT_TRUE(install_pack_campaign());
    EXPECT_FALSE(script_registered("embeddedtest"));

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kPackCampaignId));
    EXPECT_TRUE(script_registered("embeddedtest"))
        << "campaign mount must pull embedded pack scripts into the registry";

    ASSERT_EQ(CampaignPackageIoError::None,
              unmount_campaign_package_with_error(kPackCampaignId));
    EXPECT_FALSE(script_registered("embeddedtest"))
        << "campaign unmount must drop its embedded pack scripts";
}

TEST_F(CampaignPacksTest, core_pack_survives_campaign_cycling)
{
    ASSERT_TRUE(install_pack_campaign());
    const unsigned gen_before = og::script::pack_scripts_generation();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kPackCampaignId));
    EXPECT_TRUE(script_registered("core"))
        << "the shipped core pack stays registered across campaign mounts";
    EXPECT_NE(gen_before, og::script::pack_scripts_generation())
        << "mount must bump the generation so world VMs rebuild";
}

// ---------------------------------------------------------------------------
// Pack-shipped graphics and animations, end to end
// ---------------------------------------------------------------------------
// docs/modding/examples/emberwisp is a complete mod pack that borrows nothing
// from the core families: its own indexed sprite sheet, its own `anims:` set,
// its own descriptor data and its own Lua hooks. It lives under docs/ rather
// than packs/ precisely so it is NOT auto-mounted — a new living family in the
// shipped build would move the picker and the auto-assigned wire ids.

namespace {

constexpr const char* kExamplePackDir = "docs/modding/examples/emberwisp";
constexpr const char* kExamplePackMount = "packs/emberwisp/";
constexpr const char* kExampleSprite = "packs/emberwisp/sprites/emberwisp.png";

// Mounts the example pack, refreshes the pack registries, and puts both back
// on the way out (the unmount refresh frees the mod slot the pack claimed).
class ExamplePackMount
{
public:
    ExamplePackMount()
        : mounted_(og::resources::mount(kExamplePackDir, kExamplePackMount, 1))
    {
        og::resources::refresh_pack_scripts();
    }

    ~ExamplePackMount()
    {
        if (mounted_)
            (void)og::resources::unmount(kExamplePackDir);
        og::resources::refresh_pack_scripts();
    }

    ExamplePackMount(const ExamplePackMount&) = delete;
    ExamplePackMount& operator=(const ExamplePackMount&) = delete;

    [[nodiscard]] bool ok() const { return mounted_; }

private:
    bool mounted_;
};

}  // namespace

TEST(ExampleClassPack, ships_its_own_sprite_and_animation_table)
{
    ExamplePackMount pack;
    ASSERT_TRUE(pack.ok()) << "mount " << kExamplePackDir;
    EXPECT_TRUE(script_registered("emberwisp"))
        << "the mount point names the pack id the scripts register under";

    const int family = og::families::resolve_family_string_id(
        Order::Living, "example:emberwisp");
    ASSERT_GE(family, NUM_FAMILIES)
        << "wire_id: auto must land in a mod slot above the core pins";

    // --- the descriptor carries the pack's own table, with its row count ---
    const FamilyDescriptor* fd = get_family_descriptor(family);
    ASSERT_NE(fd, nullptr);
    ASSERT_NE(fd->anim_table, nullptr)
        << "the pack's anims: set must reach the descriptor";
    ASSERT_EQ(fd->anim_row_count, 16)
        << "16 rows = 2 ani_types x 8 facings, as classpack.yaml declares";
    ASSERT_NE(fd->anim_table[0], nullptr);
    const signed char walk_row[] = {0, 1, 2, 3, 2, 1, -1};
    for (int i = 0; i < 7; i++)
        EXPECT_EQ(fd->anim_table[0][i], walk_row[i]) << "walk row frame " << i;
    ASSERT_NE(fd->anim_table[8], nullptr);
    const signed char attack_row[] = {4, 5, 6, 7, -1};
    for (int i = 0; i < 5; i++)
        EXPECT_EQ(fd->anim_table[8][i], attack_row[i])
            << "attack row (ani_type 1) frame " << i;

    // --- the loader honours both the sprite path and the table -------------
    loader l{EntityFactory{}};

    const PixieData* pix = l.graphics_for(Order::Living, family);
    ASSERT_NE(pix, nullptr)
        << "a pack-relative sprite path must load through read_pixie_file";
    EXPECT_EQ(static_cast<int>(pix->frames), 8)
        << "the pack's Aseprite sidecar declares eight frames; a sidecar that "
           "failed to resolve would silently degrade to one";
    EXPECT_EQ(static_cast<int>(pix->w), 16);
    EXPECT_EQ(static_cast<int>(pix->h), 16);

    std::unique_ptr<walker> w = l.create_walker_owned(Order::Living, family);
    ASSERT_NE(w, nullptr) << "a pack family must be constructible";
    EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(w->family())),
              family);
    EXPECT_EQ(w->ani, fd->anim_table);
    EXPECT_EQ(w->ani_count, fd->anim_row_count)
        << "ani_count must carry the pack's explicit row count: 0 means "
           "'legacy direct indexing' and turns the animate() bounds checks "
           "off for a snapshot-controlled ani_type/curdir";
    EXPECT_GT(w->ani_count, 0);

    // --- and the pack's Lua compiles and binds to that same family ---------
    // An unknown hook name or an unresolvable family id rejects the whole
    // chunk at load, so this is what keeps the example honest.
    og::script::WorldScripts scripts;
    for (const og::script::ScriptError& e : scripts.host().errors())
        ADD_FAILURE() << "pack script error in " << e.where << ": "
                      << e.message;
    EXPECT_TRUE(scripts.has_hook(Order::Living, family,
                                 og::script::FamilyHook::OnCreate));
    EXPECT_TRUE(scripts.has_hook(Order::Living, family,
                                 og::script::FamilyHook::OnFireWeapon));
}

// The frame sidecar is resolved beside the PNG that actually opened. This is
// not hypothetical: pix/ takes a user-chosen sprite pack (graphics.sprite_sheet),
// so a file mounted there at a pack sprite's path would otherwise supply that
// sprite's frame metadata — and a two-frame answer for eight-frame art fails
// the total-height cross-check, rejecting the sprite outright.
TEST(ExampleClassPack, a_pix_sidecar_cannot_shadow_a_pack_sprite)
{
    namespace fs = std::filesystem;
    ExamplePackMount pack;
    ASSERT_TRUE(pack.ok()) << "mount " << kExamplePackDir;

    const fs::path shadow = fs::path(get_user_path()) / "sidecar_shadow";
    const fs::path shadow_dir = shadow / "packs" / "emberwisp" / "sprites";
    std::error_code ec;
    fs::remove_all(shadow, ec);
    fs::create_directories(shadow_dir, ec);
    ASSERT_FALSE(ec) << ec.message();
    {
        std::ofstream out(shadow_dir / "emberwisp.json", std::ios::binary);
        out << R"({"frames":{"a":{"frame":{"w":16,"h":16}},)"
               R"("b":{"frame":{"w":16,"h":16}}},)"
               R"("meta":{"size":{"w":16,"h":32}}})";
        ASSERT_TRUE(out.good());
    }
    ASSERT_TRUE(og::resources::mount(shadow.string().c_str(), "pix/", 1));
    ASSERT_FALSE(og::resources::read_file(
                     "pix/packs/emberwisp/sprites/emberwisp.json")
                     .empty())
        << "the decoy must really be reachable under pix/, or this test "
           "proves nothing about which sidecar wins";

    const PixieData pix = read_pixie_file(kExampleSprite);
    EXPECT_TRUE(pix.valid())
        << "the pack's own sidecar must win, so the height cross-check passes";
    EXPECT_EQ(static_cast<int>(pix.frames), 8);

    (void)og::resources::unmount(shadow.string().c_str());
    fs::remove_all(shadow, ec);
}

// The example pack's two hooks, driven through the doors the sim uses.
//
// Registering a hook is not running it: the coverage gate counts a Lua
// function as covered only when a line of its own body executes, and until
// this existed both of the example pack's hooks were shipped, documented,
// registered on every mount — and never once entered by the suite. A modding
// example whose behaviour nothing exercises is a promise nobody checked.
TEST(ExampleClassPack, its_hooks_run_when_the_engine_dispatches_them)
{
    ExamplePackMount pack;
    ASSERT_TRUE(pack.ok()) << "mount " << kExamplePackDir;

    const int family = og::families::resolve_family_string_id(
        Order::Living, "example:emberwisp");
    ASSERT_GE(family, NUM_FAMILIES);
    const FamilyDescriptor* fd = get_family_descriptor(family);
    ASSERT_NE(nullptr, fd);

    TestGameWorld fixture;
    GameWorld& world = fixture.world();
    walker* wisp = world.add_ob(Order::Living, family);
    ASSERT_NE(nullptr, wisp) << "a pack family must spawn into a world";
    wisp->stats()->set_max_magicpoints(100.0f);
    wisp->stats()->set_magicpoints(100.0f);
    wisp->set_ani_type(static_cast<char>(ANI_ATTACK));

    // on_create: "each wisp starts with a random slice of its pool spent"
    // (guy.cpp dispatches this the moment a walker is built).
    ASSERT_TRUE(og::script::hooks::on_create(fd, wisp))
        << "the pack's on_create must be the one that answers";
    EXPECT_LT(wisp->stats()->magicpoints(), 100.0f)
        << "some ember is always spent";
    EXPECT_GE(wisp->stats()->magicpoints(), 100.0f - 16.0f)
        << "og.rand(16) bounds how much";
    EXPECT_EQ(ANI_WALK, static_cast<int>(wisp->ani_type())) << "on_create settles it to walking";

    // on_fire_weapon, refusing: below the flare cost the shot is blocked.
    wisp->stats()->set_magicpoints(2.0f);
    wisp->set_ani_type(static_cast<char>(ANI_WALK));
    std::optional<bool> blocked =
        og::script::hooks::on_fire_weapon(fd, wisp, nullptr);
    ASSERT_TRUE(blocked.has_value()) << "the hook must answer";
    EXPECT_FALSE(*blocked) << "a wisp nearly out of ember cannot fire";
    EXPECT_EQ(ANI_WALK, static_cast<int>(wisp->ani_type()))
        << "a blocked shot must not flare the attack animation";

    // ...and allowing: the wisp flares into its own attack rows.
    wisp->stats()->set_magicpoints(60.0f);
    wisp->set_cycle(static_cast<signed char>(4));
    std::optional<bool> allowed =
        og::script::hooks::on_fire_weapon(fd, wisp, nullptr);
    ASSERT_TRUE(allowed.has_value());
    EXPECT_TRUE(*allowed) << "with ember to spare the shot goes through";
    EXPECT_EQ(ANI_ATTACK, static_cast<int>(wisp->ani_type()))
        << "ani_type 1 is the attack half of the pack's own table";
    EXPECT_EQ(0, static_cast<int>(wisp->cycle())) << "the flare restarts the animation";

    for (const og::script::ScriptError& e :
         world.scripts().host().errors())
        ADD_FAILURE() << "pack script error in " << e.where << ": "
                      << e.message;
}
