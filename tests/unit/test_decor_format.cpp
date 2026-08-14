/* .fss v11 — decor plane serialization (BASE + DECOR tile layering).
 *
 * Pins the decor-plane format contract end-to-end through the production
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
#include <openglad/resources/our_palette.h>

#include "lodepng.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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
    d0.data[static_cast<std::size_t>(3 + d0.w * 4)] = DECOR_TORCH1;
    d0.data[static_cast<std::size_t>(5 + d0.w * 5)] = DECOR_SHRUB;
    PixieData& d2 = alloc_decor(w, 2);
    d2.data[static_cast<std::size_t>(7 + d2.w * 8)] = DECOR_BOULDER_2;

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
    EXPECT_EQ(DECOR_TORCH1, l0.data[static_cast<std::size_t>(3 + l0.w * 4)]);
    EXPECT_EQ(DECOR_SHRUB, l0.data[static_cast<std::size_t>(5 + l0.w * 5)]);
    EXPECT_EQ(DECOR_NONE, l0.data[0]);
    const PixieData& l2 = loaded.decor_for_floor(2);
    EXPECT_EQ(DECOR_BOULDER_2, l2.data[static_cast<std::size_t>(7 + l2.w * 8)]);

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
    d0.data[static_cast<std::size_t>(2 + d0.w * 2)] = DECOR_PEBBLES;
    d0.data[static_cast<std::size_t>(3 + d0.w * 3)] = 200; // hostile byte, >= DECOR_MAX
    ASSERT_EQ(11, save_level_and_read_version(w, "dfmt_cl.fss", "dfmtcl"));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());
    GameWorld loaded(0);
    ASSERT_TRUE(load_saved_level("dfmt_cl.fss", loaded));
    ASSERT_TRUE(loaded.decor_for_floor(0).valid());
    const PixieData& l0 = loaded.decor_for_floor(0);
    EXPECT_EQ(DECOR_PEBBLES, l0.data[static_cast<std::size_t>(2 + l0.w * 2)]) << "valid byte survives";
    EXPECT_EQ(DECOR_NONE, l0.data[static_cast<std::size_t>(3 + l0.w * 3)])
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

// ---------------------------------------------------------------------------
// Sprite footprint scaling — read_pixie_file + the optional sidecar
// "footprint": {"w": W, "h": H} extension. A PNG whose pixel dims differ from
// the declared world footprint is nearest-neighbor resampled IN INDEX SPACE
// at load time, so everything downstream (sim sizex/sizey, walkputbuffer,
// parity, wire) sees footprint-sized data. The integer mapping is pinned
// here: sx = x*src_w/dst_w, sy = y*src_h/dst_h. Footprint absent (or equal
// to the frame dims) must be byte-identical to today's loader.
// ---------------------------------------------------------------------------

namespace {

// Minimal Aseprite "Hash"-format sidecar text (the exact subset the loader's
// JSON reader accepts), with an optional extra top-level member appended
// (e.g. "\"footprint\": {\"w\": 16, \"h\": 16}").
std::string sprite_sidecar_json(int frames, int frame_w, int frame_h,
                                const std::string& extra_member)
{
    std::string s = "{\n \"frames\": {\n";
    for (int f = 0; f < frames; ++f)
    {
        s += "  \"fp " + std::to_string(f) + ".aseprite\": { \"frame\": "
             "{ \"x\": 0, \"y\": " + std::to_string(f * frame_h)
           + ", \"w\": " + std::to_string(frame_w)
           + ", \"h\": " + std::to_string(frame_h) + " } }";
        s += (f + 1 < frames) ? ",\n" : "\n";
    }
    s += " },\n \"meta\": { \"size\": { \"w\": " + std::to_string(frame_w)
       + ", \"h\": " + std::to_string(frame_h * frames) + " } }";
    if (!extra_member.empty())
        s += ",\n " + extra_member;
    s += "\n}\n";
    return s;
}

// Write pix/<name>.png (+ sidecar unless empty) into the write-dir temp
// tree. `pixel(frame, y, x)` supplies each SOURCE pixel's palette index.
template <typename PixelFn>
void write_sprite_asset(const std::string& name, int frames, int w, int h,
                        PixelFn pixel, const std::string& sidecar_text)
{
    create_dir(get_user_path() + "temp");
    create_dir(get_user_path() + "temp/pix");

    const std::size_t n = static_cast<std::size_t>(w)
        * static_cast<std::size_t>(h) * static_cast<std::size_t>(frames);
    auto* buf = new unsigned char[n];
    for (int f = 0; f < frames; ++f)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                buf[(static_cast<std::size_t>(f) * static_cast<std::size_t>(h)
                     + static_cast<std::size_t>(y))
                        * static_cast<std::size_t>(w)
                    + static_cast<std::size_t>(x)] = pixel(f, y, x);
    PixieData p(static_cast<unsigned char>(frames),
                static_cast<unsigned char>(w),
                static_cast<unsigned char>(h), buf);
    ASSERT_TRUE(write_pixie_png(
        (get_user_path() + "temp/pix/" + name + ".png").c_str(), p))
        << name;
    if (!sidecar_text.empty())
    {
        ASSERT_TRUE(og::resources::write_file(
            ("temp/pix/" + name + ".json").c_str(), sidecar_text.data(),
            sidecar_text.size()))
            << name;
    }
}

// A source pattern that encodes its own (frame, y, x) coordinates in the
// index byte so the resample mapping can be asserted pixel-by-pixel. Stays
// in 1..247: never index 0 (transparency) and never the >=248 recolor band.
unsigned char coord_pixel(int f, int y, int x)
{
    return static_cast<unsigned char>(1 + ((f * 89 + y * 13 + x) % 246));
}

using SpriteFootprintTest = DecorFormatTest;

} // namespace

// (a) Identity pin: footprint == frame dims loads byte-identical to the same
// PNG with a no-footprint sidecar (which is itself today's exact path).
TEST_F(SpriteFootprintTest, footprint_equal_to_dims_is_byte_identical)
{
    ASSERT_NO_FATAL_FAILURE(write_sprite_asset(
        "fp_ident_none", 2, 16, 12, coord_pixel,
        sprite_sidecar_json(2, 16, 12, "")));
    ASSERT_NO_FATAL_FAILURE(write_sprite_asset(
        "fp_ident_same", 2, 16, 12, coord_pixel,
        sprite_sidecar_json(2, 16, 12,
                            "\"footprint\": { \"w\": 16, \"h\": 12 }")));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());

    PixieData plain = read_pixie_file("fp_ident_none.png");
    PixieData footed = read_pixie_file("fp_ident_same.png");
    ASSERT_TRUE(plain.valid());
    ASSERT_TRUE(footed.valid());
    ASSERT_EQ(2, static_cast<int>(plain.frames));
    EXPECT_EQ(static_cast<int>(plain.frames), static_cast<int>(footed.frames));
    EXPECT_EQ(static_cast<int>(plain.w), static_cast<int>(footed.w));
    EXPECT_EQ(static_cast<int>(plain.h), static_cast<int>(footed.h));
    const std::size_t total = static_cast<std::size_t>(16) * 12 * 2;
    EXPECT_EQ(0, std::memcmp(plain.data.get(), footed.data.get(), total))
        << "identity footprint must not disturb one byte";
}

// (b) Downsample pin: 32x32 source, 16x16 footprint. The mapping is EXACTLY
// sx = x*32/16 = 2x, sy = 2y — pinned pixel-by-pixel plus literal spots.
TEST_F(SpriteFootprintTest, downsample_32_to_16_uses_the_pinned_mapping)
{
    ASSERT_NO_FATAL_FAILURE(write_sprite_asset(
        "fp_down", 1, 32, 32, coord_pixel,
        sprite_sidecar_json(1, 32, 32,
                            "\"footprint\": { \"w\": 16, \"h\": 16 }")));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());

    PixieData p = read_pixie_file("fp_down.png");
    ASSERT_TRUE(p.valid());
    ASSERT_EQ(1, static_cast<int>(p.frames));
    ASSERT_EQ(16, static_cast<int>(p.w));
    ASSERT_EQ(16, static_cast<int>(p.h));
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            ASSERT_EQ(coord_pixel(0, 2 * y, 2 * x),
                      p.data[static_cast<std::size_t>(y * 16 + x)])
                << "dst(" << x << "," << y << ") must sample src(2x,2y)";
    // Literal spot checks of the pinned mapping.
    EXPECT_EQ(coord_pixel(0, 0, 0), p.data[0]);
    EXPECT_EQ(coord_pixel(0, 14, 6), p.data[7 * 16 + 3]);
    EXPECT_EQ(coord_pixel(0, 30, 30), p.data[15 * 16 + 15]);
}

// (c) Upsample: 8x8 source, 16x16 footprint — each source pixel becomes a
// 2x2 block (sx = x*8/16 = x/2).
TEST_F(SpriteFootprintTest, upsample_8_to_16_replicates_each_pixel_2x2)
{
    ASSERT_NO_FATAL_FAILURE(write_sprite_asset(
        "fp_up", 1, 8, 8, coord_pixel,
        sprite_sidecar_json(1, 8, 8,
                            "\"footprint\": { \"w\": 16, \"h\": 16 }")));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());

    PixieData p = read_pixie_file("fp_up.png");
    ASSERT_TRUE(p.valid());
    ASSERT_EQ(16, static_cast<int>(p.w));
    ASSERT_EQ(16, static_cast<int>(p.h));
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            ASSERT_EQ(coord_pixel(0, y / 2, x / 2), p.data[static_cast<std::size_t>(y * 16 + x)])
                << "dst(" << x << "," << y
                << ") must replicate src(x/2,y/2)";
}

// (d) Multi-frame assets resample per-frame with correct frame addressing:
// frame f's destination block must sample only frame f's source pixels.
TEST_F(SpriteFootprintTest, multiframe_resamples_each_frame_independently)
{
    ASSERT_NO_FATAL_FAILURE(write_sprite_asset(
        "fp_frames", 3, 32, 32, coord_pixel,
        sprite_sidecar_json(3, 32, 32,
                            "\"footprint\": { \"w\": 16, \"h\": 16 }")));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());

    PixieData p = read_pixie_file("fp_frames.png");
    ASSERT_TRUE(p.valid());
    ASSERT_EQ(3, static_cast<int>(p.frames));
    ASSERT_EQ(16, static_cast<int>(p.w));
    ASSERT_EQ(16, static_cast<int>(p.h));
    for (int f = 0; f < 3; ++f)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                ASSERT_EQ(coord_pixel(f, 2 * y, 2 * x),
                          p.data[static_cast<std::size_t>((f * 16 + y) * 16 + x)])
                    << "frame " << f << " dst(" << x << "," << y << ")";
}

// (e) Rejections: footprint dims of 0 or >255 and malformed footprint values
// must fail the LOAD (no silent single-frame fallback — that would ship the
// sprite at the wrong world size).
TEST_F(SpriteFootprintTest, invalid_footprints_reject_the_sprite)
{
    const struct
    {
        const char* name;
        const char* member;
    } bad[] = {
        {"fp_bad_zero_w", "\"footprint\": { \"w\": 0, \"h\": 16 }"},
        {"fp_bad_zero_h", "\"footprint\": { \"w\": 16, \"h\": 0 }"},
        {"fp_bad_huge", "\"footprint\": { \"w\": 16, \"h\": 300 }"},
        {"fp_bad_missing_h", "\"footprint\": { \"w\": 16 }"},
        {"fp_bad_not_object", "\"footprint\": \"16x16\""},
        {"fp_bad_string_dim", "\"footprint\": { \"w\": \"16\", \"h\": 16 }"},
        {"fp_bad_empty", "\"footprint\": { }"},
        {"fp_bad_unknown_key",
         "\"footprint\": { \"w\": 16, \"h\": 16, \"d\": 16 }"},
    };
    for (const auto& row : bad)
        ASSERT_NO_FATAL_FAILURE(write_sprite_asset(
            row.name, 1, 32, 32, coord_pixel,
            sprite_sidecar_json(1, 32, 32, row.member)))
            << row.name;

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());

    for (const auto& row : bad)
    {
        PixieData p = read_pixie_file((std::string(row.name) + ".png").c_str());
        EXPECT_FALSE(p.valid())
            << row.name << ": invalid footprint must reject the sprite, "
                           "not fall back to single-frame";
    }

    // Control: the same PNG with a well-formed footprint loads fine.
    ASSERT_NO_FATAL_FAILURE(write_sprite_asset(
        "fp_good_control", 1, 32, 32, coord_pixel,
        sprite_sidecar_json(1, 32, 32,
                            "\"footprint\": { \"w\": 255, \"h\": 1 }")));
    PixieData ok = read_pixie_file("fp_good_control.png");
    ASSERT_TRUE(ok.valid());
    EXPECT_EQ(255, static_cast<int>(ok.w));
    EXPECT_EQ(1, static_cast<int>(ok.h));
}

// ---------------------------------------------------------------------------
// FFA synthesized team ramps (docs/ffa-design.md section 4, D5/D6). Palette
// entries 168-191 — the old 24-slot flat-grey hole — now carry three 8-shade
// ramps (TEAL/GOLD/SLATE) baked into our_pal_lookup, while shipped sprite
// PNGs still embed the pre-ramp grey PLTE bytes there. The loader exempts
// exactly that band from PLTE verification so both vintages load.
// ---------------------------------------------------------------------------

namespace {

inline constexpr int kSynthRampFirst = 168;
inline constexpr int kSynthRampLast = 191;

// The exact 6-bit triples from docs/ffa-design.md section 4 (brightest at
// +0, descending like RED at 40): TEAL 168-175, GOLD 176-183, SLATE 184-191.
inline constexpr unsigned char kFfaSynthRamps[24][3] = {
    {0, 57, 50},  {0, 50, 44},  {0, 43, 38},  {0, 36, 32},
    {0, 29, 26},  {0, 22, 20},  {0, 15, 14},  {0, 9, 8},
    {57, 45, 10}, {51, 40, 8},  {45, 35, 6},  {39, 30, 4},
    {33, 25, 3},  {27, 20, 2},  {21, 15, 1},  {15, 10, 0},
    {44, 48, 57}, {39, 43, 51}, {34, 38, 45}, {29, 33, 39},
    {24, 28, 33}, {19, 23, 27}, {14, 18, 21}, {9, 13, 15},
};

// The loader's 6-bit -> 8-bit PLTE scaling (og_file.cpp).
unsigned char pal8(unsigned v6)
{
    return static_cast<unsigned char>((v6 * 255u) / 63u);
}

// Encode a tiny indexed PNG with an arbitrary caller-supplied 256-entry
// palette (write_pixie_png always embeds our_pal_lookup, so hand-rolled
// PLTE vintages need lodepng directly) and drop it in the temp pix tree.
// `pal(i, c)` supplies the 8-bit PLTE byte for entry i channel c.
template <typename PalFn>
void write_plte_fixture(const std::string& name, PalFn pal)
{
    create_dir(get_user_path() + "temp");
    create_dir(get_user_path() + "temp/pix");

    lodepng::State state;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8;
    state.encoder.auto_convert = 0;
    for (unsigned i = 0; i < 256; ++i)
    {
        const auto a8 = static_cast<unsigned char>((i == 0) ? 0u : 255u);
        ASSERT_EQ(0u, lodepng_palette_add(&state.info_raw, pal(i, 0),
                                          pal(i, 1), pal(i, 2), a8));
        ASSERT_EQ(0u, lodepng_palette_add(&state.info_png.color, pal(i, 0),
                                          pal(i, 1), pal(i, 2), a8));
    }

    std::vector<unsigned char> pixels(16 * 16);
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<unsigned char>(1 + (i % 246));

    std::vector<unsigned char> encoded;
    ASSERT_EQ(0u, lodepng::encode(encoded, pixels, 16, 16, state)) << name;
    ASSERT_TRUE(og::resources::write_file(("temp/pix/" + name + ".png").c_str(),
                                          encoded.data(), encoded.size()))
        << name;
}

unsigned char engine_pal8(unsigned i, unsigned c)
{
    return pal8(our_pal_lookup(static_cast<int>(i * 3 + c)));
}

using FfaPaletteTest = DecorFormatTest;

} // namespace

// Pin the baked ramp values: our_pal_lookup entries 168*3 .. 191*3+2 must
// carry exactly the spec table, channel by channel.
TEST(FfaPalette, our_pal_lookup_carries_the_exact_synth_ramps)
{
    for (int e = 0; e < 24; ++e)
        for (int c = 0; c < 3; ++c)
            ASSERT_EQ(static_cast<int>(kFfaSynthRamps[e][c]),
                      static_cast<int>(our_pal_lookup(
                          (kSynthRampFirst + e) * 3 + c)))
                << "palette entry " << (kSynthRampFirst + e) << " channel "
                << c << " drifted from docs/ffa-design.md section 4";
}

// Old-vintage sprite: PLTE matches our_pal_lookup everywhere EXCEPT 168-191,
// which still holds the pre-ramp flat grey (6-bit 17,17,17 -> 8-bit 68). The
// shipped 616 PNGs keep those bytes, so this must load (the D6 exemption).
TEST_F(FfaPaletteTest, loader_accepts_old_flat_grey_plte_in_the_synth_band)
{
    ASSERT_NO_FATAL_FAILURE(write_plte_fixture(
        "ffa_plte_old_grey", [](unsigned i, unsigned c) -> unsigned char {
            if (i >= kSynthRampFirst && i <= kSynthRampLast)
                return pal8(17);
            return engine_pal8(i, c);
        }));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());
    PixieData p = read_pixie_file("ffa_plte_old_grey.png");
    ASSERT_TRUE(p.valid())
        << "pre-ramp grey PLTE bytes in 168-191 must stay loadable";
    ASSERT_EQ(16, static_cast<int>(p.w));
    ASSERT_EQ(16, static_cast<int>(p.h));
    ASSERT_EQ(1, static_cast<int>(p.frames));
}

// New-vintage sprite: PLTE equals our_pal_lookup exactly, ramps included.
TEST_F(FfaPaletteTest, loader_accepts_new_ramp_plte)
{
    ASSERT_NO_FATAL_FAILURE(
        write_plte_fixture("ffa_plte_new_ramps", engine_pal8));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());
    PixieData p = read_pixie_file("ffa_plte_new_ramps.png");
    ASSERT_TRUE(p.valid());
    ASSERT_EQ(16, static_cast<int>(p.w));
    ASSERT_EQ(16, static_cast<int>(p.h));
    ASSERT_EQ(1, static_cast<int>(p.frames));
}

// The exemption is exactly 168-191: a mismatch at a non-exempt entry (RED
// ramp base 40) must still reject the sprite.
TEST_F(FfaPaletteTest, loader_still_rejects_mismatch_outside_the_synth_band)
{
    ASSERT_NO_FATAL_FAILURE(write_plte_fixture(
        "ffa_plte_bad_entry40", [](unsigned i, unsigned c) -> unsigned char {
            const unsigned char v = engine_pal8(i, c);
            if (i == 40 && c == 0)
                return static_cast<unsigned char>(v - 5);
            return v;
        }));

    ScopedTempMount mount;
    ASSERT_TRUE(mount.ok());
    PixieData p = read_pixie_file("ffa_plte_bad_entry40.png");
    EXPECT_FALSE(p.valid())
        << "PLTE verification outside 168-191 must stay strict";
}
