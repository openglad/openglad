/* Deterministic error-path coverage for the production scenario reader and
 * writer.  Every physical artifact is restored after each scope so these
 * tests remain order-independent under repeated shuffled runs. */
#include "../test_save_state_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/og_file.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#endif

bool write_pixie_png(const char* filepath, const PixieData& data);

namespace {

namespace fs = std::filesystem;

using og::data::LevelFileIoError;
using og::data::LevelFileMetadata;

template <typename T>
void append_native(std::vector<std::uint8_t>& out, T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    const std::size_t old_size = out.size();
    out.resize(old_size + sizeof(value));
    std::memcpy(out.data() + old_size, &value, sizeof(value));
}

void append_fixed(std::vector<std::uint8_t>& out, std::string_view value,
                  std::size_t width)
{
    const std::size_t old_size = out.size();
    out.resize(old_size + width, 0);
    const std::size_t copied = std::min(width, value.size());
    std::memcpy(out.data() + old_size, value.data(), copied);
}

std::vector<std::uint8_t> level_body(std::uint8_t version,
                                     std::string_view grid,
                                     std::uint8_t floor_count = 1,
                                     bool include_object = false)
{
    std::vector<std::uint8_t> bytes;
    append_fixed(bytes, grid, 8);
    append_fixed(bytes, "Object failure", 30);
    append_native(bytes, std::uint8_t{0}); // scenario type
    append_native(bytes, std::int16_t{7}); // par
    append_native(bytes, std::int16_t{321}); // time limit
    append_native(bytes, static_cast<std::int16_t>(include_object ? 1 : 0));

    if (include_object)
    {
        append_native(bytes, static_cast<std::uint8_t>(Order::Living));
        append_native(bytes, std::uint8_t{0}); // family
        append_native(bytes, std::int16_t{64});
        append_native(bytes, std::int16_t{96});
        append_native(bytes, std::uint8_t{1}); // team
        append_native(bytes, std::uint8_t{0}); // facing
        append_native(bytes, std::uint8_t{0}); // command
        append_native(bytes, std::int16_t{2}); // level
        append_fixed(bytes, "unbuildable", 12);
        bytes.resize(bytes.size() + 10, 0); // reserved
    }

    append_native(bytes, std::uint8_t{0}); // description line count
    if (version >= 10)
        append_native(bytes, floor_count);
    return bytes;
}

std::vector<std::uint8_t> full_level(std::uint8_t version,
                                     std::string_view grid,
                                     bool include_object)
{
    std::vector<std::uint8_t> bytes = {'F', 'S', 'S', version};
    std::vector<std::uint8_t> body =
        level_body(version, grid, 1, include_object);
    bytes.insert(bytes.end(), body.begin(), body.end());
    return bytes;
}

std::vector<std::uint8_t> tower_v10_header()
{
    std::vector<std::uint8_t> bytes = {'F', 'S', 'S', 10};
    // grid 8 + title 30 + type 1 + par 2 + time limit 2
    bytes.resize(bytes.size() + 43, 0);
    return bytes;
}

bool write_bytes(const fs::path& path,
                 const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    if (!bytes.empty())
    {
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(out);
}

std::vector<std::uint8_t> read_bytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

#if defined(__linux__)
class ScopedFileSizeLimit
{
public:
    explicit ScopedFileSizeLimit(rlim_t limit)
    {
        if (::getrlimit(RLIMIT_FSIZE, &old_limit_) != 0 ||
            old_limit_.rlim_max < limit)
        {
            return;
        }

        old_handler_ = std::signal(SIGXFSZ, SIG_IGN);
        if (old_handler_ == SIG_ERR)
            return;
        handler_changed_ = true;

        struct rlimit limited = old_limit_;
        limited.rlim_cur = limit;
        if (::setrlimit(RLIMIT_FSIZE, &limited) != 0)
            return;
        ready_ = true;
    }

    ~ScopedFileSizeLimit()
    {
        if (ready_ && ::setrlimit(RLIMIT_FSIZE, &old_limit_) != 0)
            ADD_FAILURE() << "failed to restore RLIMIT_FSIZE";
        if (handler_changed_ && std::signal(SIGXFSZ, old_handler_) == SIG_ERR)
            ADD_FAILURE() << "failed to restore SIGXFSZ disposition";
    }

    ScopedFileSizeLimit(const ScopedFileSizeLimit&) = delete;
    ScopedFileSizeLimit& operator=(const ScopedFileSizeLimit&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }

private:
    struct rlimit old_limit_{};
    using SignalHandler = void (*)(int);
    SignalHandler old_handler_ = SIG_DFL;
    bool handler_changed_ = false;
    bool ready_ = false;
};
#endif

bool file_nonempty(const fs::path& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec &&
           fs::file_size(path, ec) > 0 && !ec;
}

PixieData& allocate_decor_plane(GameWorld& world, unsigned char value)
{
    const PixieData& grid = world.grid_for_floor(0);
    PixieData& decor = world.decor_for_floor(0);
    decor.frames = 1;
    decor.w = grid.w;
    decor.h = grid.h;
    const std::size_t cells =
        static_cast<std::size_t>(grid.w) * static_cast<std::size_t>(grid.h);
    decor.data = std::make_unique<unsigned char[]>(cells);
    std::fill_n(decor.data.get(), cells, value);
    return decor;
}

// Snapshot the named files, then force them absent for the test scope.  This
// lets failure tests assert exactly which partial outputs were produced while
// preserving any pre-existing developer files byte-for-byte.
class ScopedFilesAbsent
{
public:
    ScopedFilesAbsent(std::initializer_list<fs::path> paths)
        : paths_(paths)
    {
        for (const fs::path& path : paths_)
        {
            guards_.push_back(
                std::make_unique<og::test::ScopedPhysicalFileState>(path));
            if (!guards_.back()->ready())
            {
                ADD_FAILURE() << "could not snapshot " << path << ": "
                              << guards_.back()->error().message();
                ready_ = false;
            }
        }

        if (!ready_)
            return;
        for (const fs::path& path : paths_)
        {
            std::error_code ec;
            (void)fs::remove(path, ec);
            if (ec)
            {
                ADD_FAILURE() << "could not clear " << path << ": "
                              << ec.message();
                ready_ = false;
            }
        }
    }

    [[nodiscard]] bool ready() const noexcept { return ready_; }

private:
    std::vector<fs::path> paths_;
    std::vector<std::unique_ptr<og::test::ScopedPhysicalFileState>> guards_;
    bool ready_ = true;
};

class LevelFileIoCoverage : public ::testing::Test
{
protected:
    void SetUp() override
    {
        user_ = fs::path(get_user_path());
        ASSERT_TRUE(og::resources::set_write_dir(user_.string()));
        // Already-mounted is harmless; this also repairs state if an earlier
        // test deliberately tore down PhysFS.
        (void)og::resources::mount(user_.string().c_str(), nullptr, 1);

        for (const char* relative : {"scen", "pix", "temp/scen", "temp/pix"})
        {
            std::error_code ec;
            fs::create_directories(user_ / relative, ec);
            ASSERT_FALSE(ec) << relative << ": " << ec.message();
        }
    }

    fs::path user_;
};

TEST_F(LevelFileIoCoverage,
       unsafe_names_and_short_headers_fail_without_mutating_state)
{
    GameWorld world(17);
    world.create_new_grid();
    world.title = "preserve me";
    const unsigned char* const grid_data = world.grid.data.get();
    const int grid_width = world.grid.w;

    LevelFileMetadata metadata;
    metadata.grid_file = "unchanged";
    metadata.description = {"keep this"};
    LevelFileIoError err = LevelFileIoError::SerializeFailed;

    EXPECT_FALSE(og::data::load_level("../escape.fss", world, metadata, &err));
    EXPECT_EQ(LevelFileIoError::OpenReadFailed, err);
    EXPECT_EQ("preserve me", world.title);
    EXPECT_EQ(grid_data, world.grid.data.get());
    EXPECT_EQ(grid_width, world.grid.w);
    EXPECT_EQ("unchanged", metadata.grid_file);
    ASSERT_EQ(1u, metadata.description.size());
    EXPECT_EQ("keep this", metadata.description.front());

    err = LevelFileIoError::SerializeFailed;
    EXPECT_FALSE(og::data::save_level(world, "../escape.fss", metadata, &err));
    EXPECT_EQ(LevelFileIoError::OpenWriteFailed, err);

    const fs::path short_file = user_ / "scen/lfio_short_header.fss";
    ScopedFilesAbsent files({short_file});
    ASSERT_TRUE(files.ready());
    ASSERT_TRUE(write_bytes(short_file, {'F', 'S'}));

    err = LevelFileIoError::None;
    EXPECT_FALSE(
        og::data::load_level("lfio_short_header.fss", world, metadata, &err));
    EXPECT_EQ(LevelFileIoError::ParseFailed, err);
    EXPECT_EQ("preserve me", world.title);
    EXPECT_EQ(grid_data, world.grid.data.get());
    EXPECT_EQ("unchanged", metadata.grid_file);
    ASSERT_EQ(1u, metadata.description.size());
    EXPECT_EQ("keep this", metadata.description.front());
}

TEST_F(LevelFileIoCoverage,
       compatibility_dispatch_rejects_nulls_and_preserves_failure_state)
{
    const fs::path body = user_ / "scen/lfio_dispatch_body.bin";
    const fs::path missing = user_ / "scen/lfio_compat_missing.fss";
    ScopedFilesAbsent files({body, missing});
    ASSERT_TRUE(files.ready());
    ASSERT_TRUE(write_bytes(body, level_body(9, "unused")));

    GameWorld world(18);
    world.create_new_grid();
    world.title = "dispatch state";
    const unsigned char* const grid_data = world.grid.data.get();
    const int grid_width = world.grid.w;

    LevelFileMetadata metadata;
    metadata.grid_file = "metadata state";
    metadata.description = {"metadata description"};
    auto infile = og::io::og_open_read("scen/lfio_dispatch_body.bin");
    ASSERT_TRUE(infile);

    EXPECT_EQ(0, og::data::load_scenario_version(*infile, &world, nullptr, 9));
    EXPECT_EQ("dispatch state", world.title);
    EXPECT_EQ(grid_data, world.grid.data.get());
    EXPECT_EQ(grid_width, world.grid.w);

    EXPECT_EQ(
        0, og::data::load_scenario_version(*infile, nullptr, &metadata, 9));
    EXPECT_EQ("metadata state", metadata.grid_file);
    ASSERT_EQ(1u, metadata.description.size());
    EXPECT_EQ("metadata description", metadata.description.front());

    std::string grid_file = "compat grid";
    std::list<std::string> description = {"compat description"};
    int prepare_calls = 0;
    LevelFileIoError err = LevelFileIoError::SerializeFailed;
    EXPECT_FALSE(og::data::load_level(
        "lfio_compat_missing.fss", world, grid_file, description,
        [&] {
            ++prepare_calls;
            world.title = "prepared exactly once";
            world.type = 37;
        },
        &err));
    EXPECT_EQ(LevelFileIoError::OpenReadFailed, err);
    EXPECT_EQ(1, prepare_calls);
    EXPECT_EQ("prepared exactly once", world.title);
    EXPECT_EQ(37, world.type);
    EXPECT_EQ(grid_data, world.grid.data.get());
    EXPECT_EQ(grid_width, world.grid.w);
    EXPECT_EQ("compat grid", grid_file);
    ASSERT_EQ(1u, description.size());
    EXPECT_EQ("compat description", description.front());
}

TEST_F(LevelFileIoCoverage,
       title_reader_rejects_unsafe_name_and_two_byte_safe_fixture)
{
    std::string title = "caller value";
    EXPECT_EQ(LevelFileIoError::OpenReadFailed,
              og::data::load_scenario_title_with_error("../escape", title));
    EXPECT_EQ("none", title);
    EXPECT_EQ("none", og::data::load_scenario_title("../escape"));

    const fs::path short_title = user_ / "scen/lfio_title_short.fss";
    ScopedFilesAbsent files({short_title});
    ASSERT_TRUE(files.ready());
    ASSERT_TRUE(write_bytes(short_title, {'F', 'S'}));

    title = "caller value";
    EXPECT_EQ(
        LevelFileIoError::ParseFailed,
        og::data::load_scenario_title_with_error("lfio_title_short", title));
    EXPECT_EQ("none", title);
    EXPECT_EQ("none", og::data::load_scenario_title("lfio_title_short"));
}

TEST_F(LevelFileIoCoverage,
       legacy_pix_and_missing_upper_floor_use_the_editor_bridge_contract)
{
    const fs::path legacy_body = user_ / "scen/lfio_legacy_body.bin";
    const fs::path legacy_png = user_ / "pix/lcy.png";
    const fs::path floors_body = user_ / "scen/lfio_floors_body.bin";
    const fs::path floors_png = user_ / "pix/lfbase.png";
    const fs::path missing_upper = user_ / "pix/lfbase_f1.png";
    ScopedFilesAbsent files(
        {legacy_body, legacy_png, floors_body, floors_png, missing_upper});
    ASSERT_TRUE(files.ready());

    GameWorld world(23);
    world.create_new_grid();
    const int expected_w = world.grid.w;
    const int expected_h = world.grid.h;
    const unsigned char expected_first = world.grid.data[0];
    ASSERT_TRUE(write_pixie_png(legacy_png.string().c_str(), world.grid));
    ASSERT_TRUE(write_bytes(legacy_body, level_body(9, "lcy.pix")));

    LevelFileMetadata metadata;
    metadata.description = {"replaced"};
    {
        auto infile = og::io::og_open_read("scen/lfio_legacy_body.bin");
        ASSERT_TRUE(infile);
        ASSERT_EQ(1, og::data::load_scenario_version(*infile, &world,
                                                     &metadata, 9));
    }
    EXPECT_EQ("lcy.pix", metadata.grid_file);
    EXPECT_TRUE(metadata.description.empty());
    ASSERT_TRUE(world.grid.valid())
        << "the legacy .pix field must resolve the existing lcy.png";
    EXPECT_EQ(expected_w, world.grid.w);
    EXPECT_EQ(expected_h, world.grid.h);
    EXPECT_EQ(expected_first, world.grid.data[0]);

    ASSERT_TRUE(write_pixie_png(floors_png.string().c_str(), world.grid));
    ASSERT_TRUE(write_bytes(floors_body, level_body(10, "lfbase", 2)));
    ASSERT_FALSE(fs::exists(missing_upper));
    {
        auto infile = og::io::og_open_read("scen/lfio_floors_body.bin");
        ASSERT_TRUE(infile);
        ASSERT_EQ(1, og::data::load_scenario_version(*infile, &world,
                                                     &metadata, 10));
    }
    EXPECT_EQ("lfbase", metadata.grid_file);
    ASSERT_TRUE(world.grid.valid());
    ASSERT_EQ(2, world.floor_count());
    EXPECT_FALSE(world.grid_for_floor(1).valid())
        << "the editor bridge keeps a declared-but-missing floor repairable";
}

TEST_F(LevelFileIoCoverage, object_factory_failure_is_an_exact_parse_failure)
{
    const fs::path scenario = user_ / "scen/lfio_object_failure.fss";
    ScopedFilesAbsent files({scenario});
    ASSERT_TRUE(files.ready());
    ASSERT_TRUE(
        write_bytes(scenario, full_level(9, "badgrid", true)));

    // A plain GameWorld intentionally has no entity factory.  The file itself
    // contains a complete, structurally valid object record, so failure occurs
    // specifically when the loader asks the world to construct it.
    GameWorld world(31);
    world.create_new_grid();
    world.title = "cleared before parsing";
    LevelFileMetadata metadata;
    metadata.description = {"not reached"};
    LevelFileIoError err = LevelFileIoError::None;

    EXPECT_FALSE(og::data::load_level("lfio_object_failure.fss", world,
                                     metadata, &err));
    EXPECT_EQ(LevelFileIoError::ParseFailed, err);
    EXPECT_EQ("Object failure", world.title);
    EXPECT_EQ("badgrid", metadata.grid_file);
    EXPECT_TRUE(metadata.description.empty())
        << "a valid header commits the load and clears old descriptions";
    EXPECT_TRUE(world.oblist.empty());
    EXPECT_TRUE(world.fxlist.empty());
    EXPECT_TRUE(world.weaplist.empty());
    EXPECT_FALSE(world.grid.valid());
}

TEST_F(LevelFileIoCoverage, tower_skim_rejects_every_torn_record_shape)
{
    int next_id = 9201;
    const auto expect_bytes = [&](std::vector<std::uint8_t> bytes,
                                  bool expected) {
        const int id = next_id++;
        SCOPED_TRACE("tower id " + std::to_string(id));
        const std::string stem = "scen" + std::to_string(id);
        const fs::path fss = user_ / "scen" / (stem + ".fss");
        const fs::path base = user_ / "pix" / (stem + ".png");
        ScopedFilesAbsent files({fss, base});
        ASSERT_TRUE(files.ready());
        ASSERT_TRUE(write_bytes(fss, bytes));
        ASSERT_TRUE(write_bytes(base, {0x89}));
        EXPECT_EQ(expected, og::data::tower_floor_files_exist(id));
    };

    expect_bytes({'B', 'A', 'D', 10}, false); // bad header
    expect_bytes({'F', 'S', 'S'}, false); // missing version
    expect_bytes({'F', 'S', 'S', 1}, false); // too old
    expect_bytes({'F', 'S', 'S', 12}, false); // too new
    expect_bytes({'F', 'S', 'S', 9}, true); // pre-v10 means one floor

    std::vector<std::uint8_t> bytes = {'F', 'S', 'S', 10};
    bytes.resize(bytes.size() + 42, 0);
    expect_bytes(bytes, false); // truncated fixed header

    bytes = tower_v10_header();
    bytes.push_back(0);
    expect_bytes(bytes, false); // half of the object count

    bytes = tower_v10_header();
    append_native(bytes, std::int16_t{-1});
    expect_bytes(bytes, false); // negative object count

    bytes = tower_v10_header();
    append_native(bytes, std::int16_t{4097});
    expect_bytes(bytes, false); // oversized object count

    bytes = tower_v10_header();
    append_native(bytes, std::int16_t{1});
    bytes.resize(bytes.size() + 32, 0);
    expect_bytes(bytes, false); // truncated 33-byte object record

    bytes = tower_v10_header();
    append_native(bytes, std::int16_t{0});
    expect_bytes(bytes, false); // missing description count

    bytes.push_back(1);
    expect_bytes(bytes, false); // missing description width

    bytes.push_back(2);
    bytes.push_back('x');
    expect_bytes(bytes, false); // short description payload

    bytes = tower_v10_header();
    append_native(bytes, std::int16_t{0});
    bytes.push_back(0);
    expect_bytes(bytes, false); // missing floor count

    bytes.push_back(0);
    expect_bytes(bytes, true); // floor count zero clamps to one

    // A complete two-floor record is absent until its exact derived plane is
    // non-empty, then becomes complete without changing the scenario bytes.
    bytes.back() = 2;
    const int two_floor_id = next_id++;
    const std::string stem = "scen" + std::to_string(two_floor_id);
    const fs::path fss = user_ / "scen" / (stem + ".fss");
    const fs::path base = user_ / "pix" / (stem + ".png");
    const fs::path upper = user_ / "pix" / (stem + "_f1.png");
    ScopedFilesAbsent files({fss, base, upper});
    ASSERT_TRUE(files.ready());
    ASSERT_TRUE(write_bytes(fss, bytes));
    ASSERT_TRUE(write_bytes(base, {0x89}));
    EXPECT_FALSE(og::data::tower_floor_files_exist(two_floor_id));
    ASSERT_TRUE(write_bytes(upper, {0x89}));
    EXPECT_TRUE(og::data::tower_floor_files_exist(two_floor_id));
}

TEST_F(LevelFileIoCoverage, writer_failures_report_the_exact_partial_file_set)
{
    {
        constexpr int id = 9401;
        const fs::path fss = user_ / "scen/scen9401.fss";
        const fs::path png = user_ / "pix/scen9401.png";
        ScopedFilesAbsent files({fss, png});
        ASSERT_TRUE(files.ready());

        GameWorld invalid_grid(41);
        LevelFileMetadata metadata; // empty grid name selects scen9401
        LevelFileIoError err = LevelFileIoError::None;
        EXPECT_FALSE(og::data::save_level_to_user_dir(
            invalid_grid, id, metadata, &err));
        EXPECT_EQ(LevelFileIoError::OpenWriteFailed, err);
        EXPECT_TRUE(file_nonempty(fss));
        EXPECT_FALSE(fs::exists(png));
    }

    {
        constexpr int id = 9402;
        const fs::path fss = user_ / "scen/scen9402.fss";
        const fs::path base = user_ / "pix/scen9402.png";
        const fs::path upper = user_ / "pix/scen9402_f1.png";
        ScopedFilesAbsent files({fss, base, upper});
        ASSERT_TRUE(files.ready());

        GameWorld missing_upper_grid(42);
        missing_upper_grid.create_new_grid();
        missing_upper_grid.set_floor_count(2);
        LevelFileMetadata metadata;
        LevelFileIoError err = LevelFileIoError::None;
        EXPECT_FALSE(og::data::save_level_to_user_dir(
            missing_upper_grid, id, metadata, &err));
        EXPECT_EQ(LevelFileIoError::OpenWriteFailed, err);
        EXPECT_TRUE(file_nonempty(fss));
        EXPECT_TRUE(file_nonempty(base));
        EXPECT_FALSE(fs::exists(upper));
    }

    {
        const fs::path fss = user_ / "temp/scen/lfio_missing_floor.fss";
        const fs::path base = user_ / "temp/pix/lfiotmp.png";
        const fs::path upper = user_ / "temp/pix/lfiotmp_f1.png";
        ScopedFilesAbsent files({fss, base, upper});
        ASSERT_TRUE(files.ready());

        GameWorld missing_upper_grid(43);
        missing_upper_grid.create_new_grid();
        missing_upper_grid.set_floor_count(2);
        LevelFileMetadata metadata;
        metadata.grid_file = "lfiotmp";
        LevelFileIoError err = LevelFileIoError::None;
        EXPECT_FALSE(og::data::save_level(missing_upper_grid,
                                         "lfio_missing_floor.fss", metadata,
                                         &err));
        EXPECT_EQ(LevelFileIoError::OpenWriteFailed, err);
        EXPECT_TRUE(file_nonempty(fss));
        EXPECT_TRUE(file_nonempty(base));
        EXPECT_FALSE(fs::exists(upper));
    }
}

TEST_F(LevelFileIoCoverage,
       user_dir_save_treats_allocated_zero_decor_as_absent)
{
    constexpr int id = 9403;
    const fs::path fss = user_ / "scen/scen9403.fss";
    const fs::path base = user_ / "pix/scen9403.png";
    const fs::path decor = user_ / "pix/scen9403_d0.png";
    ScopedFilesAbsent files({fss, base, decor});
    ASSERT_TRUE(files.ready());

    GameWorld world(44);
    world.create_new_grid();
    PixieData& zero_decor = allocate_decor_plane(world, 0);
    ASSERT_TRUE(zero_decor.valid());

    LevelFileMetadata metadata;
    LevelFileIoError err = LevelFileIoError::SerializeFailed;
    EXPECT_TRUE(
        og::data::save_level_to_user_dir(world, id, metadata, &err));
    EXPECT_EQ(LevelFileIoError::None, err);
    EXPECT_TRUE(file_nonempty(fss));
    EXPECT_TRUE(file_nonempty(base));
    EXPECT_FALSE(fs::exists(decor))
        << "an allocated all-zero plane must not emit a decor artifact";
}

TEST_F(LevelFileIoCoverage,
       nonempty_decor_reports_blocked_user_and_temp_outputs_exactly)
{
    {
        constexpr int id = 9404;
        const fs::path fss = user_ / "scen/scen9404.fss";
        const fs::path base = user_ / "pix/scen9404.png";
        const fs::path decor = user_ / "pix/scen9404_d0.png";
        ScopedFilesAbsent files({fss, base, decor});
        ASSERT_TRUE(files.ready());
        std::error_code ec;
        ASSERT_TRUE(fs::create_directory(decor, ec)) << ec.message();

        GameWorld world(45);
        world.create_new_grid();
        PixieData& nonempty_decor = allocate_decor_plane(world, 0);
        ASSERT_GT(nonempty_decor.w, 0);
        ASSERT_GT(nonempty_decor.h, 0);
        nonempty_decor.data[0] = 1;

        LevelFileMetadata metadata;
        LevelFileIoError err = LevelFileIoError::None;
        EXPECT_FALSE(
            og::data::save_level_to_user_dir(world, id, metadata, &err));
        EXPECT_EQ(LevelFileIoError::OpenWriteFailed, err);
        EXPECT_TRUE(file_nonempty(fss));
        EXPECT_TRUE(file_nonempty(base));
        EXPECT_TRUE(fs::is_directory(decor));
    }

    {
        const fs::path fss = user_ / "temp/scen/lfio_decor_blocked.fss";
        const fs::path base = user_ / "temp/pix/lfiodecor.png";
        const fs::path decor = user_ / "temp/pix/lfiodecor_d0.png";
        // OgFile deliberately falls back to stdio when PhysFS rejects an
        // open. Block that relative fallback too, so this test exercises a
        // real failed open independent of the test runner's working dir.
        const fs::path fallback_decor =
            fs::current_path() / "temp/pix/lfiodecor_d0.png";
        ScopedFilesAbsent files({fss, base, decor, fallback_decor});
        ASSERT_TRUE(files.ready());
        std::error_code ec;
        ASSERT_TRUE(fs::create_directory(decor, ec)) << ec.message();
        ec.clear();
        ASSERT_TRUE(fs::create_directory(fallback_decor, ec)) << ec.message();

        GameWorld world(46);
        world.create_new_grid();
        PixieData& nonempty_decor = allocate_decor_plane(world, 1);
        ASSERT_TRUE(nonempty_decor.valid());
        ASSERT_EQ(1, nonempty_decor.data[0]);

        LevelFileMetadata metadata;
        metadata.grid_file = "lfiodecor";
        LevelFileIoError err = LevelFileIoError::None;
        EXPECT_FALSE(og::data::save_level(
            world, "lfio_decor_blocked.fss", metadata, &err));
        EXPECT_EQ(LevelFileIoError::OpenWriteFailed, err);
        EXPECT_TRUE(file_nonempty(fss));
        EXPECT_TRUE(file_nonempty(base));
        EXPECT_TRUE(fs::is_directory(decor));
        EXPECT_TRUE(fs::is_directory(fallback_decor));
    }
}

#if defined(__linux__)
TEST_F(LevelFileIoCoverage,
       scenario_writer_reports_each_quota_boundary_with_exact_prefix)
{
    enum class PayloadShape
    {
        Plain,
        Object,
        Description,
        Multifloor,
        Decor,
    };
    struct FailurePoint
    {
        rlim_t byte_limit;
        PayloadShape shape;
        const char* field;
    };

    // These offsets are the boundaries immediately before each field in the
    // production FSS layout. A real kernel file-size quota makes the next
    // fwrite fail while preserving every preceding byte.
    const FailurePoint cases[] = {
        {4, PayloadShape::Plain, "grid name"},
        {12, PayloadShape::Plain, "title"},
        {42, PayloadShape::Plain, "scenario type"},
        {43, PayloadShape::Plain, "par value"},
        {45, PayloadShape::Plain, "time limit"},
        {47, PayloadShape::Plain, "object count"},
        {49, PayloadShape::Object, "first object"},
        {49, PayloadShape::Plain, "description count"},
        {50, PayloadShape::Description, "description width"},
        {51, PayloadShape::Description, "description bytes"},
        {50, PayloadShape::Multifloor, "floor count"},
        {51, PayloadShape::Decor, "decor-present byte"},
    };

    for (std::size_t i = 0; i < std::size(cases); ++i)
    {
        const FailurePoint& row = cases[i];
        SCOPED_TRACE(row.field);

        GameWorld world(static_cast<int>(70 + i));
        world.create_new_grid();
        world.title = "quota-boundary";
        LevelFileMetadata metadata;
        metadata.grid_file = "quota";

        switch (row.shape)
        {
            case PayloadShape::Plain:
                break;
            case PayloadShape::Object:
                world.oblist.push_back(std::make_unique<walker>());
                break;
            case PayloadShape::Description:
                metadata.description = {"abc"};
                break;
            case PayloadShape::Multifloor:
                world.set_floor_count(2);
                break;
            case PayloadShape::Decor:
                (void)allocate_decor_plane(world, 1);
                break;
        }

        const std::string suffix =
            std::to_string(::getpid()) + "_" + std::to_string(i) + ".fss";
        const fs::path canonical_path =
            fs::temp_directory_path() / ("openglad_lfio_full_" + suffix);
        const fs::path limited_path =
            fs::temp_directory_path() / ("openglad_lfio_limited_" + suffix);
        ScopedFilesAbsent files({canonical_path, limited_path});
        ASSERT_TRUE(files.ready());

        LevelFileIoError error = LevelFileIoError::SerializeFailed;
        ASSERT_TRUE(og::data::save_level_scenario_file(
            world, canonical_path.string(), metadata, &error));
        ASSERT_EQ(LevelFileIoError::None, error);
        const std::vector<std::uint8_t> canonical =
            read_bytes(canonical_path);
        ASSERT_GT(canonical.size(),
                  static_cast<std::size_t>(row.byte_limit));

        bool saved = true;
        {
            ScopedFileSizeLimit limit(row.byte_limit);
            ASSERT_TRUE(limit.ready())
                << "the host must support a reversible RLIMIT_FSIZE";
            error = LevelFileIoError::None;
            saved = og::data::save_level_scenario_file(
                world, limited_path.string(), metadata, &error);
        }

        EXPECT_FALSE(saved);
        EXPECT_EQ(LevelFileIoError::SerializeFailed, error);
        const std::vector<std::uint8_t> partial = read_bytes(limited_path);
        ASSERT_EQ(static_cast<std::size_t>(row.byte_limit), partial.size());
        EXPECT_TRUE(std::equal(partial.begin(), partial.end(),
                               canonical.begin()))
            << "a failed field may not corrupt any committed prefix byte";
    }
}
#endif

// The SCEN_TYPE_GENERATED provenance mark: metadata-side only. The writer
// ORs it into the on-disk type byte (offset 42: 'FSS' + version + 8-byte
// grid name + 30-byte title), the loader strips it back into
// LevelFileMetadata::generated, and GameWorld::type never carries it — so
// snapshots, parity dumps and the wire are untouched. A classic byte (no
// mark) defaults to generated == false.
TEST_F(LevelFileIoCoverage,
       generated_mark_round_trips_and_classic_type_bytes_default_clear)
{
    constexpr std::size_t kTypeByteOffset = 42;

    constexpr int marked_id = 9405;
    const fs::path marked_fss = user_ / "scen/scen9405.fss";
    const fs::path marked_pix = user_ / "pix/scen9405.png";
    constexpr int classic_id = 9406;
    const fs::path classic_fss = user_ / "scen/scen9406.fss";
    const fs::path classic_pix = user_ / "pix/scen9406.png";
    ScopedFilesAbsent files(
        {marked_fss, marked_pix, classic_fss, classic_pix});
    ASSERT_TRUE(files.ready());

    GameWorld world(47);
    world.create_new_grid();
    world.type = SCEN_TYPE_CAN_EXIT;

    LevelFileMetadata metadata;
    metadata.generated = true;
    LevelFileIoError err = LevelFileIoError::SerializeFailed;
    ASSERT_TRUE(
        og::data::save_level_to_user_dir(world, marked_id, metadata, &err));
    EXPECT_EQ(LevelFileIoError::None, err);

    const std::vector<std::uint8_t> marked_bytes = read_bytes(marked_fss);
    ASSERT_GT(marked_bytes.size(), kTypeByteOffset);
    EXPECT_EQ(static_cast<unsigned char>(SCEN_TYPE_CAN_EXIT) |
                  static_cast<unsigned char>(SCEN_TYPE_GENERATED),
              marked_bytes[kTypeByteOffset]);

    GameWorld reloaded(47);
    LevelFileMetadata reloaded_metadata;
    err = LevelFileIoError::SerializeFailed;
    ASSERT_TRUE(og::data::load_level("scen9405.fss", reloaded,
                                     reloaded_metadata, &err));
    EXPECT_TRUE(reloaded_metadata.generated)
        << "the loader must surface the provenance mark in metadata";
    EXPECT_EQ(SCEN_TYPE_CAN_EXIT, reloaded.type)
        << "the sim-side type must never carry the provenance bit";

    // Round-trip through an unmarking save (metadata cleared): the byte
    // reverts to the classic value and the loader answers false — hand
    // campaigns stay byte-identical.
    metadata.generated = false;
    err = LevelFileIoError::SerializeFailed;
    ASSERT_TRUE(
        og::data::save_level_to_user_dir(world, classic_id, metadata, &err));
    const std::vector<std::uint8_t> classic_bytes = read_bytes(classic_fss);
    ASSERT_GT(classic_bytes.size(), kTypeByteOffset);
    EXPECT_EQ(static_cast<unsigned char>(SCEN_TYPE_CAN_EXIT),
              classic_bytes[kTypeByteOffset]);

    GameWorld classic(47);
    LevelFileMetadata classic_metadata;
    classic_metadata.generated = true; // stale caller value must be reset
    err = LevelFileIoError::SerializeFailed;
    ASSERT_TRUE(og::data::load_level("scen9406.fss", classic,
                                     classic_metadata, &err));
    EXPECT_FALSE(classic_metadata.generated)
        << "a classic type byte must load as generated == false";
    EXPECT_EQ(SCEN_TYPE_CAN_EXIT, classic.type);
}

} // namespace
