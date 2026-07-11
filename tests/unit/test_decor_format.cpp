/* .fss v11 — decor plane serialization (BASE + DECOR tile layering).
 *
 * Pins the Stage 0 format contract end-to-end through the production
 * writer/reader (og::data::save_level / load_level):
 *   - writer downgrade cascade: decor-free levels keep emitting v9 (or v10
 *     when multifloor/NPC-extras), BYTE-identical even when an all-zero decor
 *     plane is allocated — the parity/editor-round-trip protection;
 *   - v11 round-trip: presence flags + "{grid}_dN.png" planes per flagged
 *     floor (including floor 0), empty floors write nothing;
 *   - v<=10 files load with invalid (absent) decor planes;
 *   - hostile-file hardening: dim-mismatched planes are dropped, bytes
 *     >= DECOR_MAX clamp to DECOR_NONE;
 *   - the version gate: v12 refuses cleanly (the same refusal an OLD engine
 *     gives a v11 file — the whole point of the version tick).
 */
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/og_file.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// SDL-free indexed PNG writer (resources IO; same symbol save_grid_file uses).
bool write_pixie_png(const char* filepath, const PixieData& data);

namespace {

using og::data::LevelFileIoError;
using og::data::LevelFileMetadata;

// Allocate (zero-filled) the decor plane for `floor`, sized to that floor's
// grid — the editor brush's lazy-allocation shape.
PixieData& alloc_decor(GameWorld& w, int floor)
{
    const PixieData& fg = w.grid_for_floor(floor);
    PixieData& dp = w.decor_for_floor(floor);
    dp.frames = 1;
    dp.w = fg.w;
    dp.h = fg.h;
    dp.data = std::make_unique<unsigned char[]>(
        static_cast<std::size_t>(fg.w) * static_cast<std::size_t>(fg.h));
    return dp;
}

// Give stacked floor `floor` a grass grid matching floor 0's footprint
// (mirrors the editor's add_floor allocation).
void alloc_floor_grid(GameWorld& w, int floor)
{
    PixieData& fg = w.grid_for_floor(floor);
    fg.frames = 1;
    fg.w = w.grid.w;
    fg.h = w.grid.h;
    const std::size_t n =
        static_cast<std::size_t>(fg.w) * static_cast<std::size_t>(fg.h);
    fg.data = std::make_unique<unsigned char[]>(n);
    for (std::size_t i = 0; i < n; ++i)
        fg.data[i] = PIX_GRASS1;
    w.smoother_for_floor(floor).set_target(fg);
}

// Save through the FULL writer (payload + grid/decor PNGs into the PhysFS
// write dir's temp/ tree) and return the version byte the writer chose.
int save_level_and_read_version(GameWorld& world, const std::string& filename,
                                const std::string& grid_file)
{
    // save_level writes via PHYSFS_openWrite, which does not create parent
    // directories; make the write-dir temp tree exist first (never fall back
    // to a cwd write — the repo checkout is the cwd).
    create_dir(get_user_path() + "temp/scen");
    create_dir(get_user_path() + "temp/pix");

    LevelFileMetadata metadata;
    metadata.grid_file = grid_file;
    LevelFileIoError err = LevelFileIoError::None;
    if (!og::data::save_level(world, filename, metadata, &err))
        return -1;

    auto infile = og::io::og_open_read(("temp/scen/" + filename).c_str());
    if (!infile)
        return -2;
    char header[3] = {};
    char version = 0;
    if (infile->read(header, 1, 3) != 3 || infile->read(&version, 1, 1) != 1)
        return -3;
    if (header[0] != 'F' || header[1] != 'S' || header[2] != 'S')
        return -4;
    return static_cast<int>(version);
}

std::vector<std::uint8_t> read_saved_scenario_bytes(const std::string& filename)
{
    return og::resources::read_file(("temp/scen/" + filename).c_str());
}

// Mounts the write dir's temp/ tree so the production loader resolves
// "scen/*.fss" and "pix/*.png" exactly like a mounted campaign package.
class ScopedTempMount
{
public:
    ScopedTempMount() : path_(get_user_path() + "temp")
    {
        mounted_ = og::resources::mount(path_.c_str(), nullptr, 1);
    }
    ~ScopedTempMount()
    {
        if (mounted_)
            (void)og::resources::unmount(path_.c_str());
    }
    bool ok() const { return mounted_; }

private:
    std::string path_;
    bool mounted_ = false;
};

bool load_saved_level(const std::string& filename, GameWorld& world)
{
    LevelFileMetadata metadata;
    LevelFileIoError err = LevelFileIoError::None;
    return og::data::load_level(filename, world, metadata, &err);
}

// Earlier suites in this binary deliberately sabotage PhysFS state
// (test_physfs_wrappers simulates a fatal-assert bail: mounts destroyed,
// write dir redirected). Re-establish the unit_main contract — write dir at
// the user path, user path mounted — so every write below lands in the
// per-run config dir instead of silently falling back to a cwd (repo) write.
class DecorFormatTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::string user_path = get_user_path();
        ASSERT_TRUE(og::resources::set_write_dir(user_path));
        // Fails harmlessly when the user dir is still mounted.
        (void)og::resources::mount(user_path.c_str(), nullptr, 1);
    }
};

} // namespace

// The parity pin: decor-free levels keep today's exact bytes. An ALLOCATED
// but all-zero plane counts as "no decor" — erasing decor in the editor
// downgrades the format again (v9 single-floor, v10 multifloor).
TEST_F(DecorFormatTest, writer_downgrade_cascade_and_byte_identity_when_decor_empty)
{
    // Single-floor, no decor -> v9.
    TestGameWorld tw;
    GameWorld& w = tw.world();
    ASSERT_EQ(9, save_level_and_read_version(w, "dfmt_v9a.fss", "dfmt9a"));
    const std::vector<std::uint8_t> v9_bytes =
        read_saved_scenario_bytes("dfmt_v9a.fss");
    ASSERT_FALSE(v9_bytes.empty());

    // All-zero plane: still v9, byte-identical payload.
    alloc_decor(w, 0);
    ASSERT_EQ(9, save_level_and_read_version(w, "dfmt_v9b.fss", "dfmt9a"));
    EXPECT_EQ(v9_bytes, read_saved_scenario_bytes("dfmt_v9b.fss"))
        << "an allocated all-zero decor plane must not change one byte";

    // Multifloor, empty decor: v10, and again byte-identical to pre-decor.
    w.set_floor_count(2);
    alloc_floor_grid(w, 1);
    ASSERT_EQ(10, save_level_and_read_version(w, "dfmt_v10a.fss", "dfmt10a"));
    const std::vector<std::uint8_t> v10_bytes =
        read_saved_scenario_bytes("dfmt_v10a.fss");
    alloc_decor(w, 1);
    ASSERT_EQ(10, save_level_and_read_version(w, "dfmt_v10b.fss", "dfmt10a"));
    EXPECT_EQ(v10_bytes, read_saved_scenario_bytes("dfmt_v10b.fss"));

    // One nonzero decor byte anywhere -> v11; zeroing it -> v10 again.
    w.decor_for_floor(1).data[7] = DECOR_TORCH1;
    ASSERT_EQ(11, save_level_and_read_version(w, "dfmt_v11.fss", "dfmt11a"));
    w.decor_for_floor(1).data[7] = DECOR_NONE;
    ASSERT_EQ(10, save_level_and_read_version(w, "dfmt_v10c.fss", "dfmt10a"));
}

// Full v11 round-trip through the production writer + reader: three floors,
// decor on floors 0 and 2, nothing on floor 1. Presence flags steer exactly
// which "_dN" planes exist on disk and which planes come back valid.
TEST_F(DecorFormatTest, v11_round_trip_multifloor_planes_and_presence_flags)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(3);
    alloc_floor_grid(w, 1);
    alloc_floor_grid(w, 2);

    PixieData& d0 = alloc_decor(w, 0);
    d0.data[3 + d0.w * 4] = DECOR_TORCH1;
    d0.data[5 + d0.w * 5] = DECOR_SHRUB;
    PixieData& d2 = alloc_decor(w, 2);
    d2.data[7 + d2.w * 8] = DECOR_BOULDER_2;

    ASSERT_EQ(11, save_level_and_read_version(w, "dfmt_rt.fss", "dfmtrt"));

    EXPECT_TRUE(og::resources::exists("temp/pix/dfmtrt_d0.png"));
    EXPECT_FALSE(og::resources::exists("temp/pix/dfmtrt_d1.png"))
        << "empty-plane floors write nothing";
    EXPECT_TRUE(og::resources::exists("temp/pix/dfmtrt_d2.png"));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());
    GameWorld loaded(0);
    ASSERT_TRUE(load_saved_level("dfmt_rt.fss", loaded));

    ASSERT_EQ(3, loaded.floor_count());
    ASSERT_TRUE(loaded.decor_for_floor(0).valid());
    EXPECT_FALSE(loaded.decor_for_floor(1).valid())
        << "unflagged floors stay decor-free";
    ASSERT_TRUE(loaded.decor_for_floor(2).valid());

    const PixieData& l0 = loaded.decor_for_floor(0);
    EXPECT_EQ(DECOR_TORCH1, l0.data[3 + l0.w * 4]);
    EXPECT_EQ(DECOR_SHRUB, l0.data[5 + l0.w * 5]);
    EXPECT_EQ(DECOR_NONE, l0.data[0]);
    const PixieData& l2 = loaded.decor_for_floor(2);
    EXPECT_EQ(DECOR_BOULDER_2, l2.data[7 + l2.w * 8]);

    EXPECT_EQ(static_cast<int>(loaded.grid.w), static_cast<int>(l0.w));
    EXPECT_EQ(static_cast<int>(loaded.grid.h), static_cast<int>(l0.h));
}

// v<=10 files never reach the decor arm: planes stay invalid after load.
TEST_F(DecorFormatTest, v10_and_older_files_load_with_absent_decor)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    ASSERT_EQ(9, save_level_and_read_version(w, "dfmt_old9.fss", "dfmto9"));
    w.set_floor_count(2);
    alloc_floor_grid(w, 1);
    ASSERT_EQ(10, save_level_and_read_version(w, "dfmt_old10.fss", "dfmto10"));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());

    GameWorld loaded9(0);
    ASSERT_TRUE(load_saved_level("dfmt_old9.fss", loaded9));
    EXPECT_FALSE(loaded9.decor_for_floor(0).valid());

    GameWorld loaded10(0);
    ASSERT_TRUE(load_saved_level("dfmt_old10.fss", loaded10));
    ASSERT_EQ(2, loaded10.floor_count());
    EXPECT_FALSE(loaded10.decor_for_floor(0).valid());
    EXPECT_FALSE(loaded10.decor_for_floor(1).valid());
}

// Hostile-file hardening, part 1: a plane whose dims mismatch the floor's
// grid is dropped (level still loads, cell reads stay in-bounds forever).
TEST_F(DecorFormatTest, dim_mismatched_decor_plane_is_dropped_on_load)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    PixieData& d0 = alloc_decor(w, 0);
    d0.data[11] = DECOR_PEBBLES;
    ASSERT_EQ(11, save_level_and_read_version(w, "dfmt_mm.fss", "dfmtmm"));

    // Overwrite the shipped plane with an 8x8 impostor.
    PixieData small;
    small.frames = 1;
    small.w = 8;
    small.h = 8;
    small.data = std::make_unique<unsigned char[]>(64);
    small.data[0] = DECOR_TORCH1;
    ASSERT_TRUE(write_pixie_png(
        (get_user_path() + "temp/pix/dfmtmm_d0.png").c_str(), small));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());
    GameWorld loaded(0);
    ASSERT_TRUE(load_saved_level("dfmt_mm.fss", loaded))
        << "a bad decor plane must not take the level down with it";
    EXPECT_FALSE(loaded.decor_for_floor(0).valid())
        << "mismatched plane is dropped, not adopted";
}

// Hostile-file hardening, part 2: out-of-registry bytes clamp to DECOR_NONE
// on load (mirrors the editor's out-of-range grid clamp), while valid
// neighbors survive.
TEST_F(DecorFormatTest, decor_bytes_beyond_registry_clamp_to_none)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    PixieData& d0 = alloc_decor(w, 0);
    d0.data[2 + d0.w * 2] = DECOR_PEBBLES;
    d0.data[3 + d0.w * 3] = 200; // hostile byte, >= DECOR_MAX
    ASSERT_EQ(11, save_level_and_read_version(w, "dfmt_cl.fss", "dfmtcl"));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());
    GameWorld loaded(0);
    ASSERT_TRUE(load_saved_level("dfmt_cl.fss", loaded));
    ASSERT_TRUE(loaded.decor_for_floor(0).valid());
    const PixieData& l0 = loaded.decor_for_floor(0);
    EXPECT_EQ(DECOR_PEBBLES, l0.data[2 + l0.w * 2]) << "valid byte survives";
    EXPECT_EQ(DECOR_NONE, l0.data[3 + l0.w * 3])
        << "byte >= DECOR_MAX must clamp to DECOR_NONE (registry index "
           "would be OOB)";
}

// The version gate. kScenarioVersion is now 11: a v11 header passes the gate
// (round-trip test above proves it), and a v12 file refuses cleanly with
// UnsupportedVersion — the exact refusal a v10-era engine gives our v11
// files, which is why decor levels tick the version instead of silently
// losing their blocking decor on old engines.
TEST_F(DecorFormatTest, version_12_refused_cleanly_version_11_is_current)
{
    create_dir(get_user_path() + "scen");
    const unsigned char v12[] = {'F', 'S', 'S', 12};
    ASSERT_TRUE(og::resources::write_file("scen/dfmt_v12.fss", v12,
                                          sizeof(v12)));

    GameWorld loaded(0);
    LevelFileMetadata metadata;
    LevelFileIoError err = LevelFileIoError::None;
    EXPECT_FALSE(og::data::load_level("dfmt_v12.fss", loaded, metadata, &err));
    EXPECT_EQ(LevelFileIoError::UnsupportedVersion, err)
        << "one version past current must refuse with the clean log path";
}
