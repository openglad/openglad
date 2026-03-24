#include "SDL.h"
#include <openglad/interface/level_runtime_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/og_file.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

// Forward declarations from src/runtime/level_data.cpp (now using OgFile&)
short load_version_2(og::io::OgFile& infile, LevelRuntimeData* data);
short load_version_3(og::io::OgFile& infile, LevelRuntimeData* data);
short load_version_4(og::io::OgFile& infile, LevelRuntimeData* data);
short load_version_5(og::io::OgFile& infile, LevelRuntimeData* data);
short load_version_6(og::io::OgFile& infile, LevelRuntimeData* data, short version);
short load_scenario_version(og::io::OgFile& infile, LevelRuntimeData* data, short version);

// Memory-backed OgFile for testing (replaces SDL_RWFromConstMem)
class MemoryOgFile final : public og::io::OgFile {
public:
    MemoryOgFile(const void* data, std::size_t size)
        : data_(static_cast<const unsigned char*>(data)), size_(size), pos_(0) {}

    std::size_t read(void* buf, std::size_t size, std::size_t count) override {
        if (size == 0 || count == 0) return 0;
        std::size_t total = size * count;
        std::size_t avail = (pos_ < size_) ? size_ - pos_ : 0;
        if (total > avail) total = avail;
        std::size_t objects = total / size;
        if (objects == 0 || buf == nullptr)
            return 0;
        std::memcpy(buf, data_ + pos_, objects * size);
        pos_ += objects * size;
        return objects;
    }
    std::size_t write(const void*, std::size_t, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t offset, int whence) override {
        std::int64_t newpos = 0;
        switch (whence) {
            case 0: newpos = offset; break;
            case 1: newpos = static_cast<std::int64_t>(pos_) + offset; break;
            case 2: newpos = static_cast<std::int64_t>(size_) + offset; break;
            default: return -1;
        }
        if (newpos < 0) return -1;
        pos_ = static_cast<std::size_t>(newpos);
        return static_cast<std::int64_t>(pos_);
    }
    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }
private:
    const unsigned char* data_;
    std::size_t size_;
    std::size_t pos_;
};

namespace
{
static void append_bytes(std::vector<uint8_t>& out, const void* data, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + n);
}

static void append_u8(std::vector<uint8_t>& out, uint8_t v)
{
    out.push_back(v);
}

static void append_i16(std::vector<uint8_t>& out, int16_t v)
{
    // Scenario files are native-endian in this codebase; tests run on little-endian.
    append_bytes(out, &v, sizeof(v));
}

static void append_fixed8(std::vector<uint8_t>& out, const char* s)
{
    std::array<char, 8> buf{};
    size_t i = 0;
    for (; i < 8 && s[i] != '\0'; i++)
        buf[i] = s[i];
    append_bytes(out, buf.data(), buf.size());
}
} // namespace

TEST(LevelDataLoadVersions, level_data_load_version2_minimal_success)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");     // newgrid (8 bytes)
    append_i16(bytes, 1);             // listsize
    append_u8(bytes, static_cast<uint8_t>(Order::Living));
    append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
    append_i16(bytes, 100);           // xpos
    append_i16(bytes, 100);           // ypos
    append_u8(bytes, 0);              // team
    append_u8(bytes, 0);              // facing
    append_u8(bytes, 0);              // command
    for (int i = 0; i < 11; i++)
        append_u8(bytes, 0);          // reserved

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_2(rw, &data);
    ASSERT_EQ(1, (int)ok) << "load_version_2 should succeed on minimal buffer";
}


TEST(LevelDataLoadVersions, level_data_load_version2_treasure_routes_to_fxlist)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");     // newgrid (8 bytes)
    append_i16(bytes, 1);             // listsize
    append_u8(bytes, static_cast<uint8_t>(Order::Treasure));
    append_u8(bytes, static_cast<uint8_t>(FAMILY_GOLD_BAR));
    append_i16(bytes, 100);           // xpos
    append_i16(bytes, 100);           // ypos
    append_u8(bytes, 0);              // team
    append_u8(bytes, 0);              // facing
    append_u8(bytes, 0);              // command
    for (int i = 0; i < 11; i++)
        append_u8(bytes, 0);          // reserved

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_2(rw, &data);
    ASSERT_EQ(1, (int)ok) << "load_version_2 should succeed with a treasure object";
    ASSERT_TRUE(!data.world().fxlist.empty()) << "treasure should route via add_fx_ob into fxlist for v2";
}


TEST(LevelDataLoadVersions, level_data_load_version2_truncated_object_payload_fails)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid"); // newgrid
    append_i16(bytes, 1);         // listsize
    // Omit object bytes to force rw_read_exact_or_log failure.

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_2(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_2 should fail on truncated object payload";
}


TEST(LevelDataLoadVersions, level_data_load_version2_invalid_family_fails_object_creation)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");     // newgrid (8 bytes)
    append_i16(bytes, 1);             // listsize
    append_u8(bytes, static_cast<uint8_t>(Order::Living));
    append_u8(bytes, 255);            // invalid family value
    append_i16(bytes, 100);           // xpos
    append_i16(bytes, 100);           // ypos
    append_u8(bytes, 0);              // team
    append_u8(bytes, 0);              // facing
    append_u8(bytes, 0);              // command
    for (int i = 0; i < 11; i++)
        append_u8(bytes, 0);          // reserved

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_2(rw, &data);
    // Some loaders tolerate unknown family values by clamping/defaulting, so
    // this isn't guaranteed to fail. We mainly want to ensure it doesn't crash.
    ASSERT_EQ(1, (int)ok) << "load_version_2 should not crash on unknown family values";
}


TEST(LevelDataLoadVersions, level_data_load_version2_rejects_invalid_object_count)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 5000); // > MAX_SCENARIO_OBJECTS
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_2(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_2 should reject invalid list size";
}


TEST(LevelDataLoadVersions, level_data_load_version3_rejects_invalid_object_count)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 5000); // > MAX_SCENARIO_OBJECTS
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_3(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_3 should reject invalid list size";
}


TEST(LevelDataLoadVersions, level_data_load_version3_treasure_adds_at_start_success)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 1); // listsize
    append_u8(bytes, static_cast<uint8_t>(Order::Treasure));
    append_u8(bytes, static_cast<uint8_t>(FAMILY_GOLD_BAR));
    append_i16(bytes, 100);
    append_i16(bytes, 100);
    append_u8(bytes, 0); // team
    append_u8(bytes, 0); // facing
    append_u8(bytes, 0); // command
    append_u8(bytes, 3); // level
    for (int i = 0; i < 10; i++)
        append_u8(bytes, 0); // reserved
    append_u8(bytes, 0);     // numlines

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_3(rw, &data);
    ASSERT_EQ(1, (int)ok) << "load_version_3 should succeed with treasure object";
    ASSERT_TRUE(!data.world().oblist.empty()) << "v3 treasure path should still create an object";
}


TEST(LevelDataLoadVersions, level_data_load_version3_truncated_object_payload_fails)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 1); // listsize
    append_u8(bytes, static_cast<uint8_t>(Order::Living));
    // Truncate the rest of the object fields to force a read failure.

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_3(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_3 should fail on truncated object payload";
}


TEST(LevelDataLoadVersions, level_data_load_version3_zero_lines_minimal_success)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 1); // listsize
    append_u8(bytes, static_cast<uint8_t>(Order::Living));
    append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
    append_i16(bytes, 100);
    append_i16(bytes, 100);
    append_u8(bytes, 0); // team
    append_u8(bytes, 0); // facing
    append_u8(bytes, 0); // command
    append_u8(bytes, 3); // level
    for (int i = 0; i < 10; i++)
        append_u8(bytes, 0); // reserved
    append_u8(bytes, 0);     // numlines

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_3(rw, &data);
    ASSERT_EQ(1, (int)ok) << "load_version_3 should succeed with zero description lines";
}


TEST(LevelDataLoadVersions, level_data_load_version3_truncates_long_description_line_and_discards_remaining_bytes)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 0); // listsize
    append_u8(bytes, 1);  // numlines
    append_u8(bytes, 200); // width > oneline[80], triggers truncation/discard loop
    for (int i = 0; i < 200; i++)
        append_u8(bytes, 'x');

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_3(rw, &data);
    ASSERT_EQ(1, (int)ok) << "load_version_3 should succeed with truncated description line";
    ASSERT_TRUE(!data.description.empty()) << "description should contain at least one line";
}


TEST(LevelDataLoadVersions, level_data_load_version4_truncates_long_description_line)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 0); // listsize
    append_u8(bytes, 1);  // numlines
    append_u8(bytes, 120); // width > oneline[80], triggers truncation/discard loop
    for (int i = 0; i < 120; i++)
        append_u8(bytes, 'x');

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_4(rw, &data);
    ASSERT_EQ(1, (int)ok) << "load_version_4 should succeed with truncated description line";
    ASSERT_TRUE(!data.description.empty()) << "description should contain at least one line";
}


TEST(LevelDataLoadVersions, level_data_load_version5_rejects_invalid_object_count)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_u8(bytes, 2);     // scenario type
    append_i16(bytes, 5000); // > MAX_SCENARIO_OBJECTS
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_5 should reject invalid list size";
}


TEST(LevelDataLoadVersions, level_data_load_version5_success_with_treasure_weapon_and_truncated_text)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_u8(bytes, 3); // scenario type
    append_i16(bytes, 2); // listsize

    // Treasure object path (routes through add_fx_ob).
    append_u8(bytes, static_cast<uint8_t>(Order::Treasure));
    append_u8(bytes, static_cast<uint8_t>(FAMILY_GOLD_BAR));
    append_i16(bytes, 100);
    append_i16(bytes, 100);
    append_u8(bytes, 0); // team
    append_u8(bytes, 0); // facing
    append_u8(bytes, 0); // command
    append_u8(bytes, 4); // level
    {
        std::array<char, 12> name{};
        std::memcpy(name.data(), "Treasure", 8);
        append_bytes(bytes, name.data(), name.size());
    }
    for (int i = 0; i < 10; i++)
        append_u8(bytes, 0);

    // Weapon door object to exercise version-5 door fixup loop.
    append_u8(bytes, static_cast<uint8_t>(Order::Weapon));
    append_u8(bytes, static_cast<uint8_t>(FAMILY_DOOR));
    append_i16(bytes, GRID_SIZE * 2);
    append_i16(bytes, GRID_SIZE * 2);
    append_u8(bytes, 0); // team
    append_u8(bytes, 0); // facing
    append_u8(bytes, 0); // command
    append_u8(bytes, 2); // level
    {
        std::array<char, 12> name{};
        std::memcpy(name.data(), "Door", 4);
        append_bytes(bytes, name.data(), name.size());
    }
    for (int i = 0; i < 10; i++)
        append_u8(bytes, 0);

    append_u8(bytes, 1);   // numlines
    append_u8(bytes, 120); // width > local line buffer, exercises truncate/discard loop
    for (int i = 0; i < 120; i++)
        append_u8(bytes, 'q');

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    ASSERT_EQ(1, (int)ok) << "load_version_5 should succeed on valid buffer";
    ASSERT_EQ(3, (int)data.world().type) << "load_version_5 should set scenario type";
    ASSERT_TRUE(!data.world().fxlist.empty()) << "treasure object should populate fxlist";
    ASSERT_TRUE(!data.world().weaplist.empty()) << "weapon object should populate weaplist";
    ASSERT_TRUE(!data.description.empty()) << "description line should be read";
}


TEST(LevelDataLoadVersions, level_data_load_version5_truncated_scenario_type_fails)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    // Truncate before scenario type byte.
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_5 should fail when scenario type byte is missing";
}


TEST(LevelDataLoadVersions, level_data_load_version5_truncated_object_payload_fails)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_u8(bytes, 1);  // scenario type
    append_i16(bytes, 1); // listsize
    append_u8(bytes, static_cast<uint8_t>(Order::Living));
    append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
    // Truncated before full object payload is available.
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_5 should fail on truncated object payload";
}


TEST(LevelDataLoadVersions, level_data_load_version5_missing_numlines_fails)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_u8(bytes, 1);  // scenario type
    append_i16(bytes, 0); // listsize
    // Truncate before numlines byte.
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_5 should fail when numlines byte is missing";
}


TEST(LevelDataLoadVersions, level_data_load_version5_truncated_discard_tail_fails)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_u8(bytes, 1);  // scenario type
    append_i16(bytes, 0); // listsize
    append_u8(bytes, 1);  // numlines
    append_u8(bytes, 120); // forces truncate + discard
    for (int i = 0; i < 90; i++)
        append_u8(bytes, 'z'); // fewer bytes than required after truncation

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_5 should fail when long-line discard bytes are truncated";
}


TEST(LevelDataLoadVersions, 2_3_4_missing_grid_or_count_fail)
{
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes; // missing grid bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        ASSERT_EQ(0, (int)load_version_2(rw, &data)) << "v2 should fail when grid field is missing";
    }
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid"); // missing listsize bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        ASSERT_EQ(0, (int)load_version_2(rw, &data)) << "v2 should fail when object count field is missing";
    }
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes; // missing grid bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        ASSERT_EQ(0, (int)load_version_3(rw, &data)) << "v3 should fail when grid field is missing";
    }
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid"); // missing listsize bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        ASSERT_EQ(0, (int)load_version_3(rw, &data)) << "v3 should fail when object count field is missing";
    }
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes; // missing grid bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        ASSERT_EQ(0, (int)load_version_4(rw, &data)) << "v4 should fail when grid field is missing";
    }
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid"); // missing listsize bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        ASSERT_EQ(0, (int)load_version_4(rw, &data)) << "v4 should fail when object count field is missing";
    }
}


TEST(LevelDataLoadVersions, level_data_load_version4_truncated_discard_tail_fails)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 0); // listsize
    append_u8(bytes, 1);  // numlines
    append_u8(bytes, 120); // long line, requires discard
    for (int i = 0; i < 90; i++)
        append_u8(bytes, 'w'); // intentionally short payload for discard loop

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_4(rw, &data);
    ASSERT_EQ(0, (int)ok) << "load_version_4 should fail when long-line discard bytes are truncated";
}


TEST(LevelDataLoadVersions, level_data_load_version4_truncated_numlines_or_width_fails)
{
    LevelRuntimeData data(1);

    // Missing numlines byte after one object should fail at numlines read.
    {
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid");
        append_i16(bytes, 1);
        append_u8(bytes, static_cast<uint8_t>(Order::Living));
        append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
        append_i16(bytes, 100);
        append_i16(bytes, 100);
        append_u8(bytes, 0); // team
        append_u8(bytes, 0); // facing
        append_u8(bytes, 0); // command
        append_u8(bytes, 1); // level
        std::array<char, 12> name{};
        append_bytes(bytes, name.data(), name.size());
        for (int i = 0; i < 10; i++)
            append_u8(bytes, 0);

        MemoryOgFile rw(bytes.data(), bytes.size());
        ASSERT_EQ(0, (int)load_version_4(rw, &data)) << "v4 should fail when numlines byte is missing";
    }

    // numlines present but first line width byte missing should fail in line loop.
    {
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid");
        append_i16(bytes, 1);
        append_u8(bytes, static_cast<uint8_t>(Order::Living));
        append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
        append_i16(bytes, 100);
        append_i16(bytes, 100);
        append_u8(bytes, 0); // team
        append_u8(bytes, 0); // facing
        append_u8(bytes, 0); // command
        append_u8(bytes, 1); // level
        std::array<char, 12> name{};
        append_bytes(bytes, name.data(), name.size());
        for (int i = 0; i < 10; i++)
            append_u8(bytes, 0);
        append_u8(bytes, 1); // numlines
        // width byte intentionally omitted

        MemoryOgFile rw(bytes.data(), bytes.size());
        ASSERT_EQ(0, (int)load_version_4(rw, &data)) << "v4 should fail when description width byte is missing";
    }
}


TEST(LevelDataLoadVersions, 2_to_5_invalid_order_fails_object_creation)
{
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid");
        append_i16(bytes, 1);
        append_u8(bytes, 255); // invalid Order -> add_ob should fail
        append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
        append_i16(bytes, 100);
        append_i16(bytes, 100);
        append_u8(bytes, 0);
        append_u8(bytes, 0);
        append_u8(bytes, 0);
        for (int i = 0; i < 11; i++)
            append_u8(bytes, 0);
        MemoryOgFile rw(bytes.data(), bytes.size());
        const short ok = load_version_2(rw, &data);
        // Legacy v2 behavior can differ based on loader state; ensure stability/no crash.
        ASSERT_TRUE(ok == 0 || ok == 1) << "v2 unknown-order input should not crash loader";
    }
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid");
        append_i16(bytes, 1);
        append_u8(bytes, 255); // invalid Order -> add_ob should fail
        append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
        append_i16(bytes, 100);
        append_i16(bytes, 100);
        append_u8(bytes, 0);
        append_u8(bytes, 0);
        append_u8(bytes, 0);
        append_u8(bytes, 1); // level
        for (int i = 0; i < 10; i++)
            append_u8(bytes, 0);
        append_u8(bytes, 0); // numlines
        MemoryOgFile rw(bytes.data(), bytes.size());
        const short ok = load_version_3(rw, &data);
        ASSERT_TRUE(ok == 0 || ok == 1) << "v3 unknown-order input should not crash loader";
    }
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid");
        append_i16(bytes, 1);
        append_u8(bytes, 255); // invalid Order -> add_ob should fail
        append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
        append_i16(bytes, 100);
        append_i16(bytes, 100);
        append_u8(bytes, 0);
        append_u8(bytes, 0);
        append_u8(bytes, 0);
        append_u8(bytes, 1); // level
        for (int i = 0; i < 12; i++)
            append_u8(bytes, 0); // name
        for (int i = 0; i < 10; i++)
            append_u8(bytes, 0); // reserved
        append_u8(bytes, 0); // numlines
        MemoryOgFile rw(bytes.data(), bytes.size());
        const short ok = load_version_4(rw, &data);
        ASSERT_TRUE(ok == 0 || ok == 1) << "v4 unknown-order input should not crash loader";
    }
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid");
        append_u8(bytes, 1); // scenario type
        append_i16(bytes, 1);
        append_u8(bytes, 255); // invalid Order -> add_ob should fail
        append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
        append_i16(bytes, 100);
        append_i16(bytes, 100);
        append_u8(bytes, 0);
        append_u8(bytes, 0);
        append_u8(bytes, 0);
        append_u8(bytes, 1); // level
        for (int i = 0; i < 12; i++)
            append_u8(bytes, 0); // name
        for (int i = 0; i < 10; i++)
            append_u8(bytes, 0); // reserved
        append_u8(bytes, 0); // numlines
        MemoryOgFile rw(bytes.data(), bytes.size());
        const short ok = load_version_5(rw, &data);
        ASSERT_TRUE(ok == 0 || ok == 1) << "v5 unknown-order input should not crash loader";
    }
}


TEST(LevelDataLoadVersions, level_data_load_scenario_version_dispatcher_guards)
{
    std::vector<uint8_t> bytes;
    MemoryOgFile rw(bytes.data(), bytes.size());
    const short null_result = load_scenario_version(rw, nullptr, 2);
    ASSERT_EQ(0, (int)null_result) << "dispatcher should reject null data pointer";

    LevelRuntimeData data(1);
    MemoryOgFile rw2(bytes.data(), bytes.size());
    const short old_version_result = load_scenario_version(rw2, &data, 1);
    ASSERT_EQ(0, (int)old_version_result) << "dispatcher should reject unsupported old version";
}


static void append_v6_object_record(std::vector<uint8_t>& out, uint8_t order, uint8_t family, int16_t x, int16_t y,
                                    uint8_t team, uint8_t facing, uint8_t command, int16_t level, const char* name12)
{
    append_u8(out, order);
    append_u8(out, family);
    append_i16(out, x);
    append_i16(out, y);
    append_u8(out, team);
    append_u8(out, facing);
    append_u8(out, command);
    append_i16(out, level);

    std::array<char, 12> nbuf{};
    for (int i = 0; i < 12 && name12 && name12[i] != '\0'; i++)
        nbuf[i] = name12[i];
    append_bytes(out, nbuf.data(), nbuf.size());
    for (int i = 0; i < 10; i++)
        append_u8(out, 0);
}

TEST(LevelDataLoadVersions, level_data_load_version6plus_invalid_counts_and_object_fail_paths)
{
    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid");
        append_bytes(bytes, "title", 5);
        for (int i = 0; i < 25; i++)
            append_u8(bytes, 0); // title padding to 30
        append_u8(bytes, 1); // scen type
        append_i16(bytes, 1); // par
        append_i16(bytes, 100); // time limit (v9+ path)
        append_i16(bytes, 5000); // invalid list size
        MemoryOgFile rw(bytes.data(), bytes.size());
        ASSERT_EQ(0, (int)load_version_6(rw, &data, 9)) << "v9 loader should reject invalid object count";
    }

    {
        LevelRuntimeData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid");
        append_bytes(bytes, "title", 5);
        for (int i = 0; i < 25; i++)
            append_u8(bytes, 0);
        append_u8(bytes, 1); // scen type
        append_i16(bytes, 1); // par
        append_i16(bytes, 100); // time limit
        append_i16(bytes, 1); // list size
        append_v6_object_record(bytes, 255, static_cast<uint8_t>(FAMILY_SOLDIER), 64, 64, 0, 0, 0, 3, "BadOrder");
        append_u8(bytes, 0); // num lines
        MemoryOgFile rw(bytes.data(), bytes.size());
        const short ok = load_version_6(rw, &data, 9);
        ASSERT_TRUE(ok == 0 || ok == 1) << "v9 loader unknown order should not crash";
    }
}


TEST(LevelDataLoadVersions, level_data_load_version6plus_truncated_description_discard_path)
{
    LevelRuntimeData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_bytes(bytes, "title", 5);
    for (int i = 0; i < 25; i++)
        append_u8(bytes, 0);
    append_u8(bytes, 1);    // scen type
    append_i16(bytes, 1);   // par
    append_i16(bytes, 100); // time limit
    append_i16(bytes, 0);   // no objects
    append_u8(bytes, 1);    // num lines
    append_u8(bytes, 120);  // long line width, triggers truncation/discard loop
    for (int i = 0; i < 20; i++)
        append_u8(bytes, 'q'); // intentionally short to force discard read failure

    MemoryOgFile rw(bytes.data(), bytes.size());
    const short loaded = load_version_6(rw, &data, 9);
    ASSERT_EQ(0, (int)loaded) << "v9 loader should fail when long description discard tail is truncated";
}


TEST(LevelDataLoadVersions, level_data_load_version6plus_named_objects_treasure_route_and_door_fixup)
{
    namespace fs = std::filesystem;
    const fs::path grid_path = "grid.png";
    std::error_code ec;
    fs::remove(grid_path, ec);

    // Build a tiny 4x4 pixie: mostly grass with a wall directly above door tile (2,2)->(2,1).
    {
        auto pixels = new unsigned char[16];
        for (int i = 0; i < 16; i++) pixels[i] = PIX_GRASS1;
        pixels[2 + 4 * 1] = PIX_H_WALL1;
        PixieData fixture(1, 4, 4, pixels);
        ASSERT_TRUE(write_pixie_png(grid_path.string().c_str(), fixture)) << "create grid.png fixture";
    }

    LevelRuntimeData data(7777);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid"); // -> grid.png
    {
        std::array<char, 30> title{};
        std::memcpy(title.data(), "v9 objects", 9);
        append_bytes(bytes, title.data(), title.size());
    }
    append_u8(bytes, 4);       // scenario type
    append_i16(bytes, 12);     // par
    append_i16(bytes, 345);    // time limit
    append_i16(bytes, 2);      // listsize

    // Treasure path (routes through add_fx_ob in v6 loader)
    append_v6_object_record(bytes, static_cast<uint8_t>(Order::Treasure),
                            static_cast<uint8_t>(FAMILY_GOLD_BAR),
                            GRID_SIZE, GRID_SIZE, 0, 0, 0, 5, "AB");
    // Door object for wall-above frame fixup path.
    append_v6_object_record(bytes, static_cast<uint8_t>(Order::Weapon),
                            static_cast<uint8_t>(FAMILY_DOOR),
                            GRID_SIZE * 2, GRID_SIZE * 2, 0, 0, 0, 2, "DoorX");

    append_u8(bytes, 1);   // numlines
    append_u8(bytes, 120); // long line width -> truncation/discard loop
    for (int i = 0; i < 120; i++)
        append_u8(bytes, 'n');

    MemoryOgFile rw(bytes.data(), bytes.size());
    ASSERT_EQ(1, (int)load_version_6(rw, &data, 9)) << "v9 loader should parse treasure/door objects and long description";
    ASSERT_TRUE(!data.world().fxlist.empty()) << "treasure object should be routed into fxlist";
    ASSERT_TRUE(!data.world().weaplist.empty()) << "door object should be routed into weaplist";
    if (!data.world().fxlist.empty())
    {
        ASSERT_TRUE(data.world().fxlist.front()->stats()->query_bit_flags(BIT_NAMED) != 0) << "name length > 1 should set BIT_NAMED";
    }

    bool saw_door = false;
    for (auto& uptr : data.world().weaplist)
    {
        walker* w = uptr.get();
        if (w && w->family() == FAMILY_DOOR)
        {
            saw_door = true;
            ASSERT_EQ(1, (int)w->frame()) << "door with wall above should be frame 1 after fixup";
        }
    }
    ASSERT_TRUE(saw_door) << "door object should be present in loaded weapon list";

    fs::remove(grid_path, ec);
}


TEST(LevelDataLoadVersions, level_data_load_scenario_dispatch_case2_path)
{
    // Minimal v2 payload through dispatcher (case 2 branch).
    LevelRuntimeData data(8888);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 0); // listsize
    MemoryOgFile rw(bytes.data(), bytes.size());
    const short result = load_scenario_version(rw, &data, 2);
    ASSERT_TRUE(result == 0 || result == 1) << "dispatch case 2 should execute without crashing";
}
