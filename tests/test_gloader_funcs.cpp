#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/gameplay/guy.h>
#include <openglad/resources/gloader.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/filesystem.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_context.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

bool apply_sprite_sheet_setting();

#include <algorithm>
#include <cstddef>

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// create_walker for all order+family combos
// exercises gloader's create_walker (the biggest function)
// ---------------------------------------------------------------------------

TEST(GloaderFuncs, gloader_create_living_all)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = l->create_walker_owned(Order::Living, families[i]);
        ASSERT_TRUE(w != nullptr) << "create_walker should succeed for all living families";
        ASSERT_TRUE(w->stats() != nullptr) << "stats should exist";
        ASSERT_TRUE(w->query_order() == Order::Living) << "order should be Living";
        ASSERT_EQ((int)families[i], (int)w->family()) << "family should match";
    }
}


TEST(GloaderFuncs, gloader_create_weapon_families)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    short weap_families[] = { FAMILY_KNIFE, FAMILY_ROCK, FAMILY_ARROW,
                              FAMILY_FIREBALL, FAMILY_LIGHTNING, FAMILY_METEOR };
    for (int i = 0; i < 6; i++) {
        auto w = l->create_walker_owned(Order::Weapon, weap_families[i]);
        if (w) {
            ASSERT_TRUE(w->query_order() == Order::Weapon) << "order should be Weapon";
        }
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto ww = l->create_walker_owned(Order::Weapon, static_cast<char>(fam));
        if (ww) {
            total_created++;
            ASSERT_TRUE(ww->query_order() == Order::Weapon) << "sweep: order should be Weapon";
        }
    }
    ASSERT_TRUE(total_created > 0) << "weapon sweep should create at least one object";
}


TEST(GloaderFuncs, gloader_create_treasure)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    auto w = l->create_walker_owned(Order::Treasure, FAMILY_STAIN);
    if (w) {
        ASSERT_TRUE(w->query_order() == Order::Treasure) << "order should be Treasure";
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto wt = l->create_walker_owned(Order::Treasure, static_cast<char>(fam));
        if (wt) {
            total_created++;
            ASSERT_TRUE(wt->query_order() == Order::Treasure) << "sweep: order should be Treasure";
        }
    }
    ASSERT_TRUE(total_created > 0) << "treasure sweep should create at least one object";
}


TEST(GloaderFuncs, gloader_create_effect)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    auto w = l->create_walker_owned(Order::FX, FAMILY_EXPLOSION);
    if (w) {
        ASSERT_TRUE(w->query_order() == Order::FX) << "order should be FX";
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto wf = l->create_walker_owned(Order::FX, static_cast<char>(fam));
        if (wf) {
            total_created++;
            ASSERT_TRUE(wf->query_order() == Order::FX) << "sweep: order should be FX";
        }
    }
    ASSERT_TRUE(total_created > 0) << "fx sweep should create at least one object";
}


TEST(GloaderFuncs, gloader_create_generator)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    auto w = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    if (w) {
        ASSERT_TRUE(w->query_order() == Order::Generator) << "order should be Generator";
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto wg = l->create_walker_owned(Order::Generator, static_cast<char>(fam));
        if (wg) {
            total_created++;
            ASSERT_TRUE(wg->query_order() == Order::Generator) << "sweep: order should be Generator";
        }
    }
    ASSERT_TRUE(total_created > 0) << "generator sweep should create at least one object";
}


// ---------------------------------------------------------------------------
// set_derived_stats
// ---------------------------------------------------------------------------

TEST(GloaderFuncs, gloader_set_derived_stats_all)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = l->create_walker_owned(Order::Living, families[i]);
        if (w) {
            l->set_derived_stats(w.get(), Order::Living, families[i]);
            ASSERT_TRUE(w->stats()->max_hitpoints() > 0) << "HP should be set";
        }
    }
}


// ---------------------------------------------------------------------------
// set_walker (sets order/family on existing walker)
// ---------------------------------------------------------------------------

TEST(GloaderFuncs, gloader_set_walker)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    walker* wp = w.get();

    og::runtime::current_session->myscreen_->set_walker(wp, Order::Living, FAMILY_MAGE);
    ASSERT_EQ((int)FAMILY_MAGE, (int)wp->family()) << "family should change to mage";

    const Order orders[] = {Order::Living, Order::Weapon, Order::Treasure, Order::FX, Order::Generator, Order::Special};
    for (Order o : orders) {
        for (int fam = 0; fam < NUM_FAMILIES; fam++) {
            if (!l->graphics[PIX(o, fam)].valid()) {
                continue;
            }
            walker* changed = l->set_walker(wp, o, static_cast<char>(fam));
            ASSERT_TRUE(changed != nullptr) << "set_walker should return object";
            ASSERT_TRUE(changed->stats() != nullptr) << "set_walker should leave stats valid";
        }
    }

    int pixie_created = 0;
    for (Order o : orders) {
        for (int fam = 0; fam < NUM_FAMILIES; fam++) {
            auto p = l->create_pixieN_owned(o, static_cast<char>(fam));
            if (p) {
                pixie_created++;
            }
        }
    }
    ASSERT_TRUE(pixie_created > 0) << "create_pixieN sweep should create objects";
}


TEST(GloaderFuncs, gloader_invalid_family_clamp_paths)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";
    if (!l)
        return;

    auto living_w = l->create_walker_owned(Order::Living, NUM_FAMILIES + 5);
    ASSERT_TRUE(living_w != nullptr) << "invalid living family should fall back to soldier";
    if (!living_w)
        return;
    ASSERT_EQ((int)FAMILY_SOLDIER, (int)living_w->family()) << "invalid living family should clamp to soldier";

    auto weapon_w = l->create_walker_owned(Order::Weapon, NUM_FAMILIES + 5);
    ASSERT_TRUE(weapon_w != nullptr) << "invalid weapon family should clamp to 0 and still construct";
    if (weapon_w)
    {
        ASSERT_EQ(0, (int)weapon_w->family()) << "invalid non-living family should clamp to family 0";
    }

    l->set_derived_stats(living_w.get(), Order::Living, NUM_FAMILIES + 9);
    ASSERT_TRUE(living_w->normal_stepsize() >= 0.0f) << "set_derived_stats should clamp invalid family safely";

    walker* changed = l->set_walker(living_w.get(), Order::Living, NUM_FAMILIES + 9);
    ASSERT_TRUE(changed != nullptr) << "set_walker should clamp invalid family and return object";
    ASSERT_EQ(0, (int)living_w->family()) << "set_walker invalid family should clamp to 0";
}


TEST(GloaderFuncs, gloader_order_special_and_invalid_graphics_paths)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";
    if (!l)
        return;

    auto special = l->create_walker_owned(Order::Special, FAMILY_RESERVED_TEAM);
    ASSERT_TRUE(special != nullptr) << "special order should build generic walker when graphics exist";
    if (special)
    {
        ASSERT_TRUE(special->query_order() == Order::Special) << "special walker should keep special order";
    }

    // Cover the "invalid graphics -> popup + nullptr" branch deterministically.
    const int idx = PIX(Order::Special, FAMILY_RESERVED_TEAM);
    PixieData saved = std::move(l->graphics[idx]);
    l->graphics[idx] = PixieData{};
    auto missing = l->create_walker_owned(Order::Special, FAMILY_RESERVED_TEAM);
    ASSERT_TRUE(missing == nullptr) << "missing graphics should return nullptr";
    l->graphics[idx] = std::move(saved);

    auto neg_living = l->create_walker_owned(Order::Living, -4);
    ASSERT_TRUE(neg_living != nullptr) << "negative living family should clamp to soldier";
    if (neg_living)
    {
        ASSERT_EQ((int)FAMILY_SOLDIER, (int)neg_living->family()) << "negative living family should map to soldier";
    }
}


TEST(GloaderFuncs, gloader_set_walker_descriptor_flag_and_default_paths)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";
    if (!l)
        return;

    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "base walker created";
    if (!w)
        return;

    // Weapon descriptor flags + lifetime + ani type paths.
    l->set_walker(w.get(), Order::Weapon, FAMILY_WAVE3);
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_IMMORTAL) != 0) << "wave3 should set BIT_IMMORTAL";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_NO_COLLIDE) != 0) << "wave3 should set BIT_NO_COLLIDE";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_PHANTOM) != 0) << "wave3 should set BIT_PHANTOM";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_FLYING) != 0) << "wave3 should set BIT_FLYING";

    l->set_walker(w.get(), Order::Weapon, FAMILY_GLOW);
    ASSERT_EQ(350, (int)w->lifetime()) << "glow should set init lifetime";

    l->set_walker(w.get(), Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_EQ(5, (int)w->ani_type()) << "circle protection should set init ani_type";

    // Treasure descriptor init_ignore + init_frame paths.
    l->set_walker(w.get(), Order::Treasure, FAMILY_STAIN);
    ASSERT_EQ(1, (int)w->ignore()) << "stain treasure should set ignore";

    l->set_walker(w.get(), Order::Treasure, FAMILY_GOLD_BAR);
    ASSERT_EQ(0, (int)w->frame()) << "gold bar should set direct frame 0";

    // Generator descriptor missing path: family >= NUM_GENERATOR_FAMILIES falls back to skeleton.
    l->set_walker(w.get(), Order::Generator, 6);
    ASSERT_EQ(0, (int)w->stats()->weapon_cost()) << "generators should set weapon_cost=0";
    ASSERT_EQ((int)FAMILY_SKELETON, (int)w->default_weapon()) << "missing generator descriptor should default weapon to skeleton";

    // FX descriptor bit flags.
    l->set_walker(w.get(), Order::FX, FAMILY_CLOUD);
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_NO_COLLIDE) != 0) << "cloud effect should set BIT_NO_COLLIDE";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_FLYING) != 0) << "cloud effect should set BIT_FLYING";

    l->set_walker(w.get(), Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_PHANTOM) != 0) << "magic shield effect should set BIT_PHANTOM";
}


TEST(GloaderFuncs, gloader_respects_optional_attach_render_callback)
{
    EntityFactory factory;
    factory.report_error = [](const std::string&) {};

    loader custom_loader(factory);
    auto w = custom_loader.create_walker_owned(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker should still be created when attach_render callback is empty";
    ASSERT_TRUE(!w->has_render()) << "empty attach_render callback must not attach render components";
}


TEST(GloaderFuncs, gloader_active_config_branch_with_ctx_and_global_cfg)
{
    const std::string prev_global_gore = cfg.get_setting("effects", "gore");

    // Exercise both gore branches via the global cfg.
    cfg.apply_setting("effects", "gore", "on");
    loader global_gore_on;
    cfg.apply_setting("effects", "gore", "off");
    loader global_gore_off;

    const int blood_idx = PIX(Order::Weapon, FAMILY_BLOOD);
    const PixieData& gore_on = global_gore_on.graphics[blood_idx];
    const PixieData& gore_off = global_gore_off.graphics[blood_idx];
    ASSERT_TRUE(gore_on.valid()) << "gore=on should load blood graphics";
    ASSERT_TRUE(gore_off.valid()) << "gore=off should load friendly blood graphics";
    ASSERT_EQ(gore_on.frames, gore_off.frames);
    ASSERT_EQ(gore_on.w, gore_off.w);
    ASSERT_EQ(gore_on.h, gore_off.h);

    const std::size_t pixel_count =
        static_cast<std::size_t>(gore_on.frames) * gore_on.w * gore_on.h;
    ASSERT_FALSE(std::equal(gore_on.data.get(),
                            gore_on.data.get() + pixel_count,
                            gore_off.data.get()))
        << "gore toggle should select distinct blood sprite data";

    cfg.apply_setting("effects", "gore", prev_global_gore);
}


// ---------------------------------------------------------------------------
// Custom spritesheet PhysFS override
// ---------------------------------------------------------------------------

namespace {

struct PixieSnapshot {
    unsigned char frames = 0;
    unsigned char w = 0;
    unsigned char h = 0;
    std::vector<unsigned char> pixels;
};

PixieSnapshot snapshot_pixie(const PixieData* pix)
{
    PixieSnapshot snap;
    if (pix == nullptr || !pix->valid())
        return snap;
    snap.frames = pix->frames;
    snap.w = pix->w;
    snap.h = pix->h;
    const std::size_t len =
        static_cast<std::size_t>(pix->frames) * pix->w * pix->h;
    snap.pixels.assign(pix->data.get(), pix->data.get() + len);
    return snap;
}

bool operator==(const PixieSnapshot& lhs, const PixieSnapshot& rhs)
{
    return lhs.frames == rhs.frames && lhs.w == rhs.w && lhs.h == rhs.h &&
        lhs.pixels == rhs.pixels;
}

bool operator!=(const PixieSnapshot& lhs, const PixieSnapshot& rhs)
{
    return !(lhs == rhs);
}

void write_file_bytes(const std::filesystem::path& path,
                      const std::vector<std::uint8_t>& bytes)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST(GloaderFuncs, custom_spritesheet_overrides_default_pix)
{
    const char* config_dir = std::getenv("OPENGLAD_CONFIG_DIR");
    ASSERT_TRUE(config_dir != nullptr) << "OPENGLAD_CONFIG_DIR must be set by the test harness";

    // Read the standard footman.png so we have a baseline to compare against.
    const std::vector<std::uint8_t> standard_bytes = og::resources::read_file("pix/footman.png");
    ASSERT_FALSE(standard_bytes.empty()) << "standard footman.png must be present";

    // Write a custom footman.png with distinct content into a temporary pack directory.
    const std::filesystem::path pack_dir =
        std::filesystem::path(config_dir) / "extra_pix" / "test_pack";
    std::filesystem::create_directories(pack_dir);
    const std::vector<std::uint8_t> custom_bytes = {0x00, 0x01, 0x02, 0x03, 0x04};
    write_file_bytes(pack_dir / "footman.png", custom_bytes);

    const std::string orig = cfg.get_setting("graphics", "sprite_sheet");

    // Start from a clean (unmounted) state.
    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_TRUE(apply_sprite_sheet_setting());

    // Mount the custom pack — footman.png must now resolve to the custom version.
    cfg.apply_setting("graphics", "sprite_sheet", "test_pack");
    ASSERT_TRUE(apply_sprite_sheet_setting());
    ASSERT_EQ(custom_bytes, og::resources::read_file("pix/footman.png"))
        << "custom pack must shadow the standard footman.png";

    // Unmount — standard footman.png must be visible again.
    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_TRUE(apply_sprite_sheet_setting());
    ASSERT_EQ(standard_bytes, og::resources::read_file("pix/footman.png"))
        << "standard footman.png must be restored after unmounting the custom pack";

    // Restore original setting and clean up.
    cfg.apply_setting("graphics", "sprite_sheet", orig);
    ASSERT_TRUE(apply_sprite_sheet_setting());
    std::filesystem::remove_all(pack_dir);
}


TEST(GloaderFuncs, custom_spritesheet_failed_mount_can_be_retried)
{
    const char* config_dir = std::getenv("OPENGLAD_CONFIG_DIR");
    ASSERT_TRUE(config_dir != nullptr) << "OPENGLAD_CONFIG_DIR must be set by the test harness";

    const std::vector<std::uint8_t> standard_bytes = og::resources::read_file("pix/footman.png");
    ASSERT_FALSE(standard_bytes.empty()) << "standard footman.png must be present";

    const std::filesystem::path pack_dir =
        std::filesystem::path(config_dir) / "extra_pix" / "retry_pack";
    std::filesystem::remove_all(pack_dir);

    const std::string orig = cfg.get_setting("graphics", "sprite_sheet");
    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_TRUE(apply_sprite_sheet_setting());

    cfg.apply_setting("graphics", "sprite_sheet", "retry_pack");
    ASSERT_FALSE(apply_sprite_sheet_setting())
        << "missing pack directory should fail to mount";
    ASSERT_EQ(standard_bytes, og::resources::read_file("pix/footman.png"))
        << "failed mount must leave standard assets visible";

    std::filesystem::create_directories(pack_dir);
    const std::vector<std::uint8_t> custom_bytes = {0x05, 0x04, 0x03, 0x02, 0x01};
    write_file_bytes(pack_dir / "footman.png", custom_bytes);

    ASSERT_TRUE(apply_sprite_sheet_setting())
        << "same pack name should be retried after a mount failure";
    ASSERT_EQ(custom_bytes, og::resources::read_file("pix/footman.png"))
        << "successful retry must mount the custom pack";

    cfg.apply_setting("graphics", "sprite_sheet", orig);
    ASSERT_TRUE(apply_sprite_sheet_setting());
    std::filesystem::remove_all(pack_dir);
}


TEST(GloaderFuncs, custom_spritesheet_reload_reapplies_ctf_entries)
{
    const char* config_dir = std::getenv("OPENGLAD_CONFIG_DIR");
    ASSERT_TRUE(config_dir != nullptr) << "OPENGLAD_CONFIG_DIR must be set by the test harness";

    loader* l = og::runtime::current_session->myscreen_->myloader;
    ASSERT_TRUE(l != nullptr) << "loader exists";

    const PixieSnapshot standard_flag =
        snapshot_pixie(l->graphics_for(Order::Treasure, og::FAMILY_FLAG));
    const PixieSnapshot key_sprite =
        snapshot_pixie(l->graphics_for(Order::Treasure, FAMILY_KEY));
    ASSERT_FALSE(standard_flag.pixels.empty()) << "standard flag sprite must be present";
    ASSERT_FALSE(key_sprite.pixels.empty()) << "key sprite must be present";
    ASSERT_NE(standard_flag, key_sprite)
        << "test fixture expects key.png to be visually distinct from flag.png";

    const std::vector<std::uint8_t> key_png = og::resources::read_file("pix/key.png");
    ASSERT_FALSE(key_png.empty()) << "key.png must be present";

    const std::filesystem::path pack_dir =
        std::filesystem::path(config_dir) / "extra_pix" / "ctf_pack";
    std::filesystem::create_directories(pack_dir);
    write_file_bytes(pack_dir / "flag.png", key_png);
    {
        std::ofstream sidecar(pack_dir / "flag.json", std::ios::binary);
        sidecar << R"({"frames":{"flag 0.aseprite":{"frame":{"x":0,"y":0,"w":10,"h":10}}},"meta":{"size":{"w":10,"h":10}}})";
    }

    const std::string orig = cfg.get_setting("graphics", "sprite_sheet");
    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_TRUE(apply_sprite_sheet_setting());

    cfg.apply_setting("graphics", "sprite_sheet", "ctf_pack");
    ASSERT_TRUE(apply_sprite_sheet_setting());
    l->reload_graphics();
    EXPECT_EQ(key_sprite, snapshot_pixie(l->graphics_for(Order::Treasure, og::FAMILY_FLAG)))
        << "hot reload must apply custom CTF flag graphics";

    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_TRUE(apply_sprite_sheet_setting());
    l->reload_graphics();
    EXPECT_EQ(standard_flag, snapshot_pixie(l->graphics_for(Order::Treasure, og::FAMILY_FLAG)))
        << "hot reload must restore standard CTF flag graphics after unmount";

    cfg.apply_setting("graphics", "sprite_sheet", orig);
    ASSERT_TRUE(apply_sprite_sheet_setting());
    l->reload_graphics();
    std::filesystem::remove_all(pack_dir);
}


// A pack name with a path separator or ".." component must be rejected so a
// hand-edited config can't mount an arbitrary host directory over pix/.
TEST(GloaderFuncs, custom_spritesheet_rejects_path_traversal)
{
    const std::vector<std::uint8_t> standard_bytes = og::resources::read_file("pix/footman.png");
    ASSERT_FALSE(standard_bytes.empty()) << "standard footman.png must be present";

    const std::string orig = cfg.get_setting("graphics", "sprite_sheet");
    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_TRUE(apply_sprite_sheet_setting());

    for (const char* bad : {"..", "../evil", "../../etc", "sub/dir", "a\\b", "."}) {
        cfg.apply_setting("graphics", "sprite_sheet", bad);
        ASSERT_TRUE(apply_sprite_sheet_setting());
        ASSERT_EQ(standard_bytes, og::resources::read_file("pix/footman.png"))
            << "unsafe pack name '" << bad << "' must not mount anything over pix/";
    }

    cfg.apply_setting("graphics", "sprite_sheet", orig);
    ASSERT_TRUE(apply_sprite_sheet_setting());
}
