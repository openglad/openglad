#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/io/og_file.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "unit/unit.h"

short load_scenario_version(og::io::OgFile& infile, LevelData* data, short version);

namespace {

class MemoryOgFile final : public og::io::OgFile {
public:
    MemoryOgFile(const std::vector<unsigned char>& data)
        : data_(data), pos_(0) {}

    std::size_t read(void* buf, std::size_t size, std::size_t count) override
    {
        if (size == 0 || count == 0)
            return 0;
        std::size_t total = size * count;
        std::size_t avail = (pos_ < data_.size()) ? data_.size() - pos_ : 0;
        if (total > avail)
            total = avail;
        const std::size_t objects = total / size;
        if (objects > 0)
            std::memcpy(buf, data_.data() + pos_, objects * size);
        pos_ += objects * size;
        return objects;
    }

    std::size_t write(const void*, std::size_t, std::size_t) override { return 0; }

    std::int64_t seek(std::int64_t offset, int whence) override
    {
        std::int64_t newpos = 0;
        switch (whence)
        {
            case 0: newpos = offset; break;
            case 1: newpos = static_cast<std::int64_t>(pos_) + offset; break;
            case 2: newpos = static_cast<std::int64_t>(data_.size()) + offset; break;
            default: return -1;
        }
        if (newpos < 0)
            return -1;
        pos_ = static_cast<std::size_t>(newpos);
        return static_cast<std::int64_t>(pos_);
    }

    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }

private:
    const std::vector<unsigned char>& data_;
    std::size_t pos_;
};

void append_bytes(std::vector<unsigned char>& out, const void* p, std::size_t n)
{
    const auto* b = static_cast<const unsigned char*>(p);
    out.insert(out.end(), b, b + n);
}

template <typename T>
void append_pod(std::vector<unsigned char>& out, const T& v)
{
    append_bytes(out, &v, sizeof(T));
}

void append_fixed_string(std::vector<unsigned char>& out, const std::string& s, std::size_t n)
{
    std::string t = s;
    t.resize(n, '\0');
    append_bytes(out, t.data(), n);
}

std::vector<unsigned char> make_payload_v2(const std::string& grid8)
{
    std::vector<unsigned char> v;
    append_fixed_string(v, grid8, 8);
    short listsize = 0;
    append_pod(v, listsize);
    return v;
}

std::vector<unsigned char> make_payload_v3(const std::string& grid8, unsigned char line_width)
{
    std::vector<unsigned char> v;
    append_fixed_string(v, grid8, 8);
    short listsize = 0;
    append_pod(v, listsize);
    char numlines = 1;
    append_pod(v, numlines);
    char width = static_cast<char>(line_width);
    append_pod(v, width);
    for (unsigned int i = 0; i < line_width; ++i)
        v.push_back(static_cast<unsigned char>('A' + (i % 26)));
    return v;
}

std::vector<unsigned char> make_payload_v5(const std::string& grid8, char scen_type)
{
    std::vector<unsigned char> v;
    append_fixed_string(v, grid8, 8);
    append_pod(v, scen_type);
    short listsize = 0;
    append_pod(v, listsize);
    char numlines = 0;
    append_pod(v, numlines);
    return v;
}

std::vector<unsigned char> make_payload_v9(const std::string& grid8, const std::string& title,
                                           char scen_type, short par, short time_limit,
                                           unsigned char line_width)
{
    std::vector<unsigned char> v;
    append_fixed_string(v, grid8, 8);
    append_fixed_string(v, title, 30);
    append_pod(v, scen_type);
    append_pod(v, par);
    append_pod(v, time_limit);
    short listsize = 0;
    append_pod(v, listsize);
    char numlines = 1;
    append_pod(v, numlines);
    char width = static_cast<char>(line_width);
    append_pod(v, width);
    for (unsigned int i = 0; i < line_width; ++i)
        v.push_back(static_cast<unsigned char>('a' + (i % 26)));
    return v;
}

} // namespace

OG_UNIT_TEST(test_level_data_r14_lines_705_770_786_912_1069_1206_load_versions_success_matrix)
{
    // v2, grid without .pix extension path.
    {
        std::vector<unsigned char> bytes = make_payload_v2("grid");
        MemoryOgFile mem(bytes);
        LevelData data(1, true);
        OG_ASSERT(load_scenario_version(mem, &data, 2) == 1);
        OG_ASSERT(data.grid_file == "grid");
    }

    // v3, grid with .pix extension path + long line width skip/discard path.
    {
        std::vector<unsigned char> bytes = make_payload_v3("ab.pix", 95);
        MemoryOgFile mem(bytes);
        LevelData data(2, true);
        OG_ASSERT(load_scenario_version(mem, &data, 3) == 1);
        OG_ASSERT(data.grid_file == "ab.pix");
        OG_ASSERT(!data.description.empty());
    }

    // v5 scenario type path.
    {
        std::vector<unsigned char> bytes = make_payload_v5("gr5", static_cast<char>(7));
        MemoryOgFile mem(bytes);
        LevelData data(3, true);
        OG_ASSERT(load_scenario_version(mem, &data, 5) == 1);
        OG_ASSERT(data.type == 7);
    }

    // v9 title/par/time-limit + long line width skip path.
    {
        std::vector<unsigned char> bytes = make_payload_v9("gr9", "R14 Title", static_cast<char>(5),
                                                           static_cast<short>(77), static_cast<short>(1234),
                                                           110);
        MemoryOgFile mem(bytes);
        LevelData data(4, true);
        OG_ASSERT(load_scenario_version(mem, &data, 9) == 1);
        OG_ASSERT(data.title == "R14 Title");
        OG_ASSERT(data.par_value == 77);
        OG_ASSERT(data.time_bonus_limit == 1234);
        OG_ASSERT(data.type == 5);
    }
}

OG_UNIT_TEST(test_level_data_r14_lines_725_733_735_746_873_1018_1112_1315_loader_failures_and_bounds)
{
    // Truncated v2 after grid field.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        MemoryOgFile mem(bytes);
        LevelData data(11, true);
        OG_ASSERT(load_scenario_version(mem, &data, 2) == 0);
    }

    // Invalid object count v2.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        short bad_listsize = static_cast<short>(-1);
        append_pod(bytes, bad_listsize);
        MemoryOgFile mem(bytes);
        LevelData data(12, true);
        OG_ASSERT(load_scenario_version(mem, &data, 2) == 0);
    }

    // Truncated v3 while reading text lines.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        short listsize = 0;
        append_pod(bytes, listsize);
        char numlines = 1;
        append_pod(bytes, numlines);
        char width = 10;
        append_pod(bytes, width);
        bytes.push_back('x');
        MemoryOgFile mem(bytes);
        LevelData data(13, true);
        OG_ASSERT(load_scenario_version(mem, &data, 3) == 0);
    }

    // Truncated v5 before scenario type read.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        MemoryOgFile mem(bytes);
        LevelData data(14, true);
        OG_ASSERT(load_scenario_version(mem, &data, 5) == 0);
    }

    // Invalid object count v8.
    {
        std::vector<unsigned char> bytes;
        append_fixed_string(bytes, "grid", 8);
        append_fixed_string(bytes, "Bad Count", 30);
        char scen_type = 0;
        append_pod(bytes, scen_type);
        short par = 1;
        append_pod(bytes, par);
        short bad_listsize = 5000;
        append_pod(bytes, bad_listsize);
        MemoryOgFile mem(bytes);
        LevelData data(15, true);
        OG_ASSERT(load_scenario_version(mem, &data, 8) == 0);
    }
}

OG_UNIT_TEST(test_level_data_r14_lines_95_99_353_371_378_campaign_description_accessors)
{
    CampaignData c("r14_campaign");
    OG_ASSERT(c.getDescriptionLine(0) == "No description.");
    OG_ASSERT(c.get_description_line(0) == "No description.");
    OG_ASSERT(c.get_description_line(5).empty());

    // Out-of-range from load/save wrappers should remain deterministic without I/O setup.
    OG_ASSERT(c.load_with_error() == c.last_io_error());
    OG_ASSERT(c.save_with_error() == c.last_io_error());
}
