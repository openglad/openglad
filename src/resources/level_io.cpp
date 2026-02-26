#include <openglad/resources/level_io.h>

#include <openglad/core/constants.h>
#include <openglad/core/stats.h>
#include <openglad/core/util.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/resources/level_visuals.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/resources/og_file.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/gameplay/game_world.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace {

using og::data::LevelFileIoError;
using og::data::LevelFileMetadata;

constexpr char kScenarioVersion = 9;
constexpr short kMaxScenarioObjects = 4096;

bool rw_read_exact_or_log(og::io::OgFile& file, void* dst, size_t size, size_t count)
{
    const size_t got = file.read(dst, size, count);
    if (got != count)
    {
        Log("Read error: expected {} items, got {}\n", count, got);
        return false;
    }
    return true;
}

bool rw_read_checked_or_log(og::io::OgFile& file,
                            void* dst,
                            size_t dst_bytes,
                            size_t read_size,
                            size_t count)
{
    if (dst == nullptr)
    {
        LogError("Read error: destination buffer is null.\n");
        return false;
    }
    if (read_size == 0 || count == 0)
        return true;
    if (read_size > dst_bytes || count > (dst_bytes / read_size))
    {
        LogError("Read error: destination buffer too small (dst_bytes={}, read_size={}, count={}).\n",
            dst_bytes, read_size, count);
        return false;
    }
    return rw_read_exact_or_log(file, dst, read_size, count);
}

unsigned char sanitize_loaded_team_num(unsigned char team_num)
{
    if (team_num <= MAX_TEAM)
        return team_num;
    LogWarn("Scenario object uses invalid team id {}. Clamping to team 0.\n", static_cast<int>(team_num));
    return 0;
}

void fill_fixed_field(char* dst, size_t fixed_len, std::string_view src, const char* field_name)
{
    if (dst == nullptr || fixed_len == 0)
        return;

    memset(dst, 0, fixed_len);
    const size_t to_copy = std::min(src.size(), fixed_len);
    memcpy(dst, src.data(), to_copy);
    if (src.size() > fixed_len)
        LogWarn("Truncating {} to {} bytes for scenario serialization.\n", field_name, fixed_len);
}

std::string ensure_pix_extension(std::string_view name)
{
    std::string s(name);
    if (s.size() >= 4 && s.compare(s.size() - 4, 4, ".pix") == 0)
        return s;
    return s + ".pix";
}

bool read_level_body(og::io::OgFile& infile,
                     short version,
                     og::gameplay::GameWorld& world,
                     LevelFileMetadata& metadata,
                     LevelFileIoError& err)
{
    short currentx = 0;
    short currenty = 0;
    unsigned char temporder = 0;
    unsigned char tempfamily = 0;
    unsigned char tempteam = 0;
    char tempfacing = 0;
    char tempcommand = 0;
    char templevel = 0;
    short shortlevel = 0;
    short listsize = 0;
    char newgrid[9] = "grid";
    char new_scen_type = 0;
    std::array<char, 80> oneline{};
    char numlines = 0;
    char tempwidth = 0;
    std::array<char, 13> tempname{};
    std::array<char, 31> scentitle{};
    short temp_par = static_cast<short>(world.id);
    short temp_time_limit = 4000;

#define READ_OR_FAIL(ptr, size, n)                                              \
    do {                                                                         \
        if (!rw_read_checked_or_log(infile, (ptr), sizeof(*(ptr)), (size), (n))) { \
            err = LevelFileIoError::ParseFailed;                                 \
            return false;                                                        \
        }                                                                        \
    } while (0)

#define READ_BUF_OR_FAIL(ptr, dst_bytes, size, n)                               \
    do {                                                                         \
        if (!rw_read_checked_or_log(infile, (ptr), (dst_bytes), (size), (n))) { \
            err = LevelFileIoError::ParseFailed;                                 \
            return false;                                                        \
        }                                                                        \
    } while (0)

    READ_BUF_OR_FAIL(newgrid, sizeof(newgrid), 8, 1);
    newgrid[8] = '\0';
    lowercase(newgrid);
    metadata.grid_file = newgrid;

    if (version >= 6)
    {
        READ_BUF_OR_FAIL(scentitle.data(), scentitle.size(), 30, 1);
        world.title = std::string(scentitle.data(), strnlen(scentitle.data(), 30));
    }
    else
    {
        world.title = "New Level";
    }

    if (version >= 5)
    {
        READ_OR_FAIL(&new_scen_type, 1, 1);
        world.type = new_scen_type;
    }
    else
    {
        world.type = 0;
    }

    if (version >= 8)
        READ_OR_FAIL(&temp_par, 2, 1);
    world.par_value = temp_par;

    if (version >= 9)
        READ_OR_FAIL(&temp_time_limit, 2, 1);
    world.time_bonus_limit = temp_time_limit;

    READ_OR_FAIL(&listsize, 2, 1);
    if (listsize < 0 || listsize > kMaxScenarioObjects)
    {
        Log("Invalid scenario object count: {}\n", listsize);
        err = LevelFileIoError::ParseFailed;
        return false;
    }

    for (short i = 0; i < listsize; ++i)
    {
        READ_OR_FAIL(&temporder, 1, 1);
        READ_OR_FAIL(&tempfamily, 1, 1);
        READ_OR_FAIL(&currentx, 2, 1);
        READ_OR_FAIL(&currenty, 2, 1);
        READ_OR_FAIL(&tempteam, 1, 1);
        READ_OR_FAIL(&tempfacing, 1, 1);
        READ_OR_FAIL(&tempcommand, 1, 1);

        if (version >= 7)
            READ_OR_FAIL(&shortlevel, 2, 1);
        else if (version >= 3)
            READ_OR_FAIL(&templevel, 1, 1);

        std::string obj_name;
        if (version >= 4)
        {
            READ_BUF_OR_FAIL(tempname.data(), tempname.size(), 12, 1);
            tempname[12] = '\0';
            obj_name = std::string(tempname.data(), strnlen(tempname.data(), 12));
        }

        const int reserved_width = (version == 2) ? 11 : 10;
        std::array<char, 20> reserved{};
        READ_BUF_OR_FAIL(reserved.data(), reserved.size(), reserved_width, 1);

        walker* new_guy = nullptr;
        if (static_cast<Order>(temporder) == Order::Treasure)
        {
            if (version == 3)
                new_guy = world.add_ob(static_cast<Order>(temporder), tempfamily, true);
            else
                new_guy = world.add_fx_ob(static_cast<Order>(temporder), tempfamily);
        }
        else
        {
            new_guy = world.add_ob(static_cast<Order>(temporder), tempfamily);
        }

        if (!new_guy)
        {
            Log("Error creating object when loading.\n");
            err = LevelFileIoError::ParseFailed;
            return false;
        }

        new_guy->setxy(currentx, currenty);
        new_guy->team_num = sanitize_loaded_team_num(tempteam);

        if (version >= 7)
            new_guy->stats()->level = shortlevel;
        else if (version >= 3)
            new_guy->stats()->level = templevel;

        if (version >= 4)
        {
            new_guy->stats()->name = obj_name;
            if (new_guy->stats()->name.size() > 1)
                new_guy->stats()->set_bit_flags(BIT_NAMED, 1);
        }
    }

    metadata.description.clear();
    if (version >= 3)
    {
        READ_OR_FAIL(&numlines, 1, 1);
        for (short i = 0; i < numlines; ++i)
        {
            READ_OR_FAIL(&tempwidth, 1, 1);
            const int original_width = static_cast<unsigned char>(tempwidth);
            int width = original_width;
            if (width >= static_cast<int>(oneline.size()))
                width = static_cast<int>(oneline.size()) - 1;

            if (width > 0)
            {
                READ_BUF_OR_FAIL(oneline.data(), oneline.size(), static_cast<size_t>(width), 1);
                oneline[static_cast<size_t>(width)] = 0;

                if (original_width > width)
                {
                    std::array<char, 256> discard{};
                    int remaining = original_width - width;
                    while (remaining > 0)
                    {
                        const int chunk = std::min(remaining, static_cast<int>(discard.size()));
                        READ_BUF_OR_FAIL(discard.data(), discard.size(), static_cast<size_t>(chunk), 1);
                        remaining -= chunk;
                    }
                }
            }
            else
            {
                oneline[0] = 0;
            }

            metadata.description.emplace_back(oneline.data());
        }
    }

    const std::string gridpix = ensure_pix_extension(newgrid);
    world.grid = read_pixie_file(gridpix.c_str());
    world.pixmaxx = world.grid.w * GRID_SIZE;
    world.pixmaxy = world.grid.h * GRID_SIZE;

    if (version >= 5)
    {
        world.mysmoother.set_target(world.grid);

        for (auto& uptr : world.weaplist)
        {
            walker* w = uptr.get();
            if (w && w->family == FAMILY_DOOR)
            {
                if (world.mysmoother.query_genre_x_y(w->xpos / GRID_SIZE,
                                                     (w->ypos / GRID_SIZE) - 1) == TYPE_WALL)
                {
                    w->set_frame(1);
                }
            }
        }
    }

#undef READ_OR_FAIL
#undef READ_BUF_OR_FAIL
    return true;
}

bool save_grid_file_internal(const char* gridname, const PixieData& grid)
{
    constexpr std::size_t kMaxGridPixels = 10'000'000;

    const auto safe_mul = [](std::size_t a, std::size_t b, std::size_t& out) -> bool {
        if (a == 0 || b == 0)
        {
            out = 0;
            return true;
        }
        if (a > (std::numeric_limits<std::size_t>::max() / b))
            return false;
        out = a * b;
        return true;
    };

    const std::size_t width = static_cast<std::size_t>(grid.w);
    const std::size_t height = static_cast<std::size_t>(grid.h);
    if (width == 0 || height == 0)
    {
        LogError("Refusing to save grid with invalid dimensions: {} (w={}, h={})\n",
            gridname, width, height);
        return false;
    }
    if (!grid.data)
    {
        LogError("Refusing to save grid with null tile data: {} (w={}, h={})\n",
            gridname, width, height);
        return false;
    }

    std::size_t tile_count = 0;
    if (!safe_mul(width, height, tile_count) || tile_count > kMaxGridPixels)
    {
        LogError("Refusing to save grid with invalid tile count: {} (w={}, h={}, tiles={}, max={})\n",
            gridname, width, height, tile_count, kMaxGridPixels);
        return false;
    }

    unsigned char numframes = 1;
    unsigned char x = grid.w;
    unsigned char y = grid.h;
    std::string fullpath(gridname);
    fullpath += ".pix";
    lowercase(fullpath);

    auto outfile = og::io::og_open_write("temp/pix/", fullpath.c_str());
    if (!outfile)
    {
        Log("Failed to save map file: temp/pix/{}\n", fullpath);
        return false;
    }

    outfile->write(&numframes, 1, 1);
    outfile->write(&x, 1, 1);
    outfile->write(&y, 1, 1);
    outfile->write(grid.data.get(), 1, tile_count);
    return true;
}

void set_error(LevelFileIoError* out_error, LevelFileIoError err)
{
    if (out_error)
        *out_error = err;
}

} // namespace

namespace og::data {

bool load_level(const std::string& path,
                og::gameplay::GameWorld& world,
                LevelVisuals& visuals,
                LevelFileMetadata& metadata,
                LevelFileIoError* out_error)
{
    set_error(out_error, LevelFileIoError::None);

    auto infile = og::io::og_open_read("scen/", path.c_str());
    if (!infile)
    {
        LogError("Cannot open level file for reading: {}\n", path);
        set_error(out_error, LevelFileIoError::OpenReadFailed);
        return false;
    }

    char header[4] = {};
    char versionnumber = 0;
    if (!rw_read_checked_or_log(*infile, header, sizeof(header), 1, 3))
    {
        set_error(out_error, LevelFileIoError::ParseFailed);
        return false;
    }

    if (std::string(header, 3) != "FSS")
    {
        LogError("File {} is not a valid scenario!\n", path);
        set_error(out_error, LevelFileIoError::InvalidHeader);
        return false;
    }

    if (!rw_read_checked_or_log(*infile, &versionnumber, sizeof(versionnumber), 1, 1))
    {
        set_error(out_error, LevelFileIoError::ParseFailed);
        return false;
    }

    if (versionnumber < 2 || versionnumber > kScenarioVersion)
    {
        Log("Scenario {} is version-level {}, and cannot be read.\n",
            world.id, static_cast<int>(versionnumber));
        set_error(out_error, LevelFileIoError::UnsupportedVersion);
        return false;
    }

    world.clear();
    metadata.description.clear();

    LevelFileIoError io_err = LevelFileIoError::None;
    if (!read_level_body(*infile, versionnumber, world, metadata, io_err))
    {
        set_error(out_error, io_err);
        return false;
    }

    visuals.topx = 0;
    visuals.topy = 0;

    set_error(out_error, LevelFileIoError::None);
    return true;
}

bool load_level(const std::string& path,
                og::gameplay::GameWorld& world,
                LevelVisuals& visuals,
                std::string& grid_file,
                std::list<std::string>& description,
                const std::function<void()>& prepare_for_load,
                LevelFileIoError* out_error)
{
    LevelFileMetadata metadata;
    metadata.grid_file = grid_file;
    metadata.description = description;

    if (prepare_for_load)
        prepare_for_load();

    if (!load_level(path, world, visuals, metadata, out_error))
        return false;

    grid_file = std::move(metadata.grid_file);
    description = std::move(metadata.description);
    return true;
}

bool save_level(og::gameplay::GameWorld& world,
                LevelVisuals& visuals,
                const std::string& path,
                const LevelFileMetadata& metadata,
                LevelFileIoError* out_error)
{
    (void)visuals;
    set_error(out_error, LevelFileIoError::None);

    auto outfile = og::io::og_open_write("temp/scen/", path.c_str());
    if (!outfile)
    {
        Log("Could not open file for writing: temp/scen/{}\n", path);
        set_error(out_error, LevelFileIoError::OpenWriteFailed);
        return false;
    }

#define WRITE_FIELD(src, size, count)                                                                 \
    do {                                                                                               \
        if (outfile->write((src), (size), (count)) != static_cast<std::size_t>(count)) {             \
            Log("Failed to write scenario file: temp/scen/{}\n", path);                             \
            set_error(out_error, LevelFileIoError::SerializeFailed);                                  \
            return false;                                                                              \
        }                                                                                              \
    } while (0)

    const char header[3] = {'F', 'S', 'S'};
    char temp_version = kScenarioVersion;
    char temp_grid[20] = {};
    char scentitle[30] = {};
    char filler[20] = "MSTRMSTRMSTRMSTR";

    WRITE_FIELD(header, 3, 1);
    WRITE_FIELD(&temp_version, 1, 1);

    fill_fixed_field(temp_grid, 8, metadata.grid_file, "grid_file");
    WRITE_FIELD(temp_grid, 8, 1);

    fill_fixed_field(scentitle, 30, world.title, "title");
    WRITE_FIELD(scentitle, 30, 1);

    char temp_scen_type = world.type;
    WRITE_FIELD(&temp_scen_type, 1, 1);

    short temp_par = world.par_value;
    WRITE_FIELD(&temp_par, 2, 1);

    short temp_time_limit = world.time_bonus_limit;
    WRITE_FIELD(&temp_time_limit, 2, 1);

    const size_t total_objects = world.oblist.size() + world.fxlist.size() + world.weaplist.size();
    const size_t serialized_objects = std::min(total_objects, static_cast<size_t>(kMaxScenarioObjects));
    if (serialized_objects != total_objects)
        Log("Scenario object count {} exceeds {}, truncating on save.\n", total_objects, kMaxScenarioObjects);

    short listsize = static_cast<short>(serialized_objects);
    WRITE_FIELD(&listsize, 2, 1);

    size_t remaining_objects = serialized_objects;
    auto write_object_list = [&](std::list<std::unique_ptr<walker>>& list, const char* null_label) -> bool {
        for (auto& uptr : list)
        {
            if (remaining_objects == 0)
                break;

            walker* ob = uptr.get();
            if (!ob)
            {
                Log("Unexpected nullptr {} object.\n", null_label);
                set_error(out_error, LevelFileIoError::SerializeFailed);
                return false;
            }

            unsigned char temporder = static_cast<unsigned char>(ob->query_order());
            char tempfamily = ob->family;
            const std::int32_t raw_x = ob->xpos;
            const std::int32_t raw_y = ob->ypos;
            std::int16_t currentx = 0;
            std::int16_t currenty = 0;
            if (raw_x < std::numeric_limits<std::int16_t>::min())
            {
                currentx = std::numeric_limits<std::int16_t>::min();
                LogWarn("Scenario object x {} below int16 min; clamping to {}.\n",
                    raw_x, static_cast<int>(currentx));
            }
            else if (raw_x > std::numeric_limits<std::int16_t>::max())
            {
                currentx = std::numeric_limits<std::int16_t>::max();
                LogWarn("Scenario object x {} above int16 max; clamping to {}.\n",
                    raw_x, static_cast<int>(currentx));
            }
            else
            {
                currentx = static_cast<std::int16_t>(raw_x);
            }

            if (raw_y < std::numeric_limits<std::int16_t>::min())
            {
                currenty = std::numeric_limits<std::int16_t>::min();
                LogWarn("Scenario object y {} below int16 min; clamping to {}.\n",
                    raw_y, static_cast<int>(currenty));
            }
            else if (raw_y > std::numeric_limits<std::int16_t>::max())
            {
                currenty = std::numeric_limits<std::int16_t>::max();
                LogWarn("Scenario object y {} above int16 max; clamping to {}.\n",
                    raw_y, static_cast<int>(currenty));
            }
            else
            {
                currenty = static_cast<std::int16_t>(raw_y);
            }
            char tempteam = static_cast<char>(ob->team_num);
            char tempfacing = ob->curdir;
            char tempcommand = static_cast<char>(ob->act_type);
            short shortlevel = static_cast<short>(ob->stats()->level);
            char tempname[12] = {};
            snprintf(tempname, sizeof(tempname), "%s", ob->stats()->name.c_str());

            WRITE_FIELD(&temporder, 1, 1);
            WRITE_FIELD(&tempfamily, 1, 1);
            WRITE_FIELD(&currentx, 2, 1);
            WRITE_FIELD(&currenty, 2, 1);
            WRITE_FIELD(&tempteam, 1, 1);
            WRITE_FIELD(&tempfacing, 1, 1);
            WRITE_FIELD(&tempcommand, 1, 1);
            WRITE_FIELD(&shortlevel, 2, 1);
            WRITE_FIELD(tempname, 12, 1);
            WRITE_FIELD(filler, 10, 1);
            --remaining_objects;
        }
        return true;
    };

    if (!write_object_list(world.oblist, "regular") ||
        !write_object_list(world.fxlist, "fx") ||
        !write_object_list(world.weaplist, "weap"))
    {
        return false;
    }

    const std::uint8_t numlines = static_cast<std::uint8_t>(metadata.description.size());
    WRITE_FIELD(&numlines, 1, 1);
    for (const auto& line : metadata.description)
    {
        const size_t serialized_width = std::min<size_t>(line.size(), 0xffu);
        const std::uint8_t tempwidth = static_cast<std::uint8_t>(serialized_width);
        WRITE_FIELD(&tempwidth, 1, 1);
        if (serialized_width > 0)
            WRITE_FIELD(line.data(), serialized_width, 1);
    }

    if (!save_grid_file_internal(metadata.grid_file.c_str(), world.grid))
    {
        set_error(out_error, LevelFileIoError::OpenWriteFailed);
        return false;
    }

    Log("Scenario saved.\n");

#undef WRITE_FIELD
    set_error(out_error, LevelFileIoError::None);
    return true;
}

bool create_new_scenario(const std::string& scenfile,
                         const std::string& gridname,
                         LevelFileIoError* out_error)
{
    set_error(out_error, LevelFileIoError::None);

    auto outfile = og::io::og_open_write(scenfile.c_str());
    if (!outfile)
    {
        set_error(out_error, LevelFileIoError::OpenWriteFailed);
        return false;
    }

#define WRITE_OR_FAIL(src, size, count)                                    \
    do {                                                                    \
        if (!og::io::og_write_exact(*outfile, (src), (size), (count))) {   \
            set_error(out_error, LevelFileIoError::SerializeFailed);        \
            return false;                                                   \
        }                                                                   \
    } while (0)

    const char header[3] = {'F', 'S', 'S'};
    char version = kScenarioVersion;
    char grid_field[8] = {};
    char title_field[30] = {};
    char scen_type = 1;
    short par_value = 1;
    short time_limit = 0;
    short num_objects = 0;
    std::uint8_t num_lines = 1;
    const char* desc = "A new scenario.";
    std::uint8_t desc_len = static_cast<std::uint8_t>(std::strlen(desc));

    fill_fixed_field(grid_field, 8, gridname, "grid_file");
    fill_fixed_field(title_field, 30, "New Level", "title");

    WRITE_OR_FAIL(header, 3, 1);
    WRITE_OR_FAIL(&version, 1, 1);
    WRITE_OR_FAIL(grid_field, 8, 1);
    WRITE_OR_FAIL(title_field, 30, 1);
    WRITE_OR_FAIL(&scen_type, 1, 1);
    WRITE_OR_FAIL(&par_value, 2, 1);
    WRITE_OR_FAIL(&time_limit, 2, 1);
    WRITE_OR_FAIL(&num_objects, 2, 1);
    WRITE_OR_FAIL(&num_lines, 1, 1);
    WRITE_OR_FAIL(&desc_len, 1, 1);
    WRITE_OR_FAIL(desc, desc_len, 1);

#undef WRITE_OR_FAIL
    return true;
}

std::string load_scenario_title(const char* filename)
{
    if (filename == nullptr || filename[0] == '\0')
        return "none";

    std::string tempfile = std::string(filename) + ".fss";
    auto infile = og::io::og_open_read("scen/", tempfile.c_str());
    if (!infile)
        return "none";

    char header[4] = {};
    char versionnumber = 0;
    char gridname[8] = {};
    char buffer[31] = {};

    if (!rw_read_checked_or_log(*infile, header, sizeof(header), 1, 3) || std::string(header, 3) != "FSS")
        return "none";
    if (!rw_read_checked_or_log(*infile, &versionnumber, sizeof(versionnumber), 1, 1) || versionnumber < 6)
        return "none";
    if (!rw_read_checked_or_log(*infile, gridname, sizeof(gridname), 1, 8))
        return "none";
    if (!rw_read_checked_or_log(*infile, buffer, sizeof(buffer), 1, 30))
        return "none";
    return std::string(buffer);
}

} // namespace og::data

namespace {

short load_version_bridge(og::io::OgFile& infile, og::gameplay::GameWorld* world,
                          og::data::LevelFileMetadata* metadata, short version)
{
    if (!world || !metadata)
        return 0;

    world->delete_objects();
    world->delete_grid();

    LevelFileIoError err = LevelFileIoError::None;
    if (!read_level_body(infile, version, *world, *metadata, err))
        return 0;

    return 1;
}

} // namespace

short load_version_2(og::io::OgFile& infile, og::gameplay::GameWorld* world, og::data::LevelFileMetadata* metadata)
{
    return load_version_bridge(infile, world, metadata, 2);
}

short load_version_3(og::io::OgFile& infile, og::gameplay::GameWorld* world, og::data::LevelFileMetadata* metadata)
{
    return load_version_bridge(infile, world, metadata, 3);
}

short load_version_4(og::io::OgFile& infile, og::gameplay::GameWorld* world, og::data::LevelFileMetadata* metadata)
{
    return load_version_bridge(infile, world, metadata, 4);
}

short load_version_5(og::io::OgFile& infile, og::gameplay::GameWorld* world, og::data::LevelFileMetadata* metadata)
{
    return load_version_bridge(infile, world, metadata, 5);
}

short load_version_6(og::io::OgFile& infile, og::gameplay::GameWorld* world, og::data::LevelFileMetadata* metadata, short version)
{
    return load_version_bridge(infile, world, metadata, version);
}

short load_scenario_version(og::io::OgFile& infile, og::gameplay::GameWorld* world,
                            og::data::LevelFileMetadata* metadata, short version)
{
    if (world == nullptr)
        return 0;

    switch (version)
    {
        case 2:
            return load_version_2(infile, world, metadata);
        case 3:
            return load_version_3(infile, world, metadata);
        case 4:
            return load_version_4(infile, world, metadata);
        case 5:
            return load_version_5(infile, world, metadata);
        case 6:
        case 7:
        case 8:
        case 9:
            return load_version_6(infile, world, metadata, version);
        default:
            Log("Scenario {} is version-level {}, and cannot be read.\n",
                world->id, version);
            return 0;
    }
}

bool save_grid_file(const char* gridname, const PixieData& grid)
{
    return save_grid_file_internal(gridname, grid);
}
