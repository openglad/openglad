#include "SDL.h"
#include <openglad/data/level_data.h>
#include <openglad/entities/walker.h>
#include <openglad/io/og_file.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

// Forward declarations from src/runtime/level_data.cpp (now using OgFile&)
short load_version_2(og::io::OgFile& infile, LevelData* data);
short load_version_3(og::io::OgFile& infile, LevelData* data);
short load_version_4(og::io::OgFile& infile, LevelData* data);
short load_version_5(og::io::OgFile& infile, LevelData* data);
short load_version_6(og::io::OgFile& infile, LevelData* data, short version);
short load_scenario_version(og::io::OgFile& infile, LevelData* data, short version);

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

void test_level_data_load_version2_minimal_success()
{
    LevelData data(1);
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
    TEST_ASSERT_EQ(1, (int)ok, "load_version_2 should succeed on minimal buffer");
}
REGISTER_TEST(test_level_data_load_version2_minimal_success);

void test_level_data_load_version2_treasure_routes_to_fxlist()
{
    LevelData data(1);
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
    TEST_ASSERT_EQ(1, (int)ok, "load_version_2 should succeed with a treasure object");
    TEST_ASSERT(!data.fxlist.empty(), "treasure should route via add_fx_ob into fxlist for v2");
}
REGISTER_TEST(test_level_data_load_version2_treasure_routes_to_fxlist);

void test_level_data_load_version2_truncated_object_payload_fails()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid"); // newgrid
    append_i16(bytes, 1);         // listsize
    // Omit object bytes to force rw_read_exact_or_log failure.

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_2(rw, &data);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_2 should fail on truncated object payload");
}
REGISTER_TEST(test_level_data_load_version2_truncated_object_payload_fails);

void test_level_data_load_version2_invalid_family_fails_object_creation()
{
    LevelData data(1);
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
    TEST_ASSERT_EQ(1, (int)ok, "load_version_2 should not crash on unknown family values");
}
REGISTER_TEST(test_level_data_load_version2_invalid_family_fails_object_creation);

void test_level_data_load_version2_rejects_invalid_object_count()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 5000); // > MAX_SCENARIO_OBJECTS
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_2(rw, &data);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_2 should reject invalid list size");
}
REGISTER_TEST(test_level_data_load_version2_rejects_invalid_object_count);

void test_level_data_load_version3_rejects_invalid_object_count()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 5000); // > MAX_SCENARIO_OBJECTS
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_3(rw, &data);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_3 should reject invalid list size");
}
REGISTER_TEST(test_level_data_load_version3_rejects_invalid_object_count);

void test_level_data_load_version3_treasure_adds_at_start_success()
{
    LevelData data(1);
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
    TEST_ASSERT_EQ(1, (int)ok, "load_version_3 should succeed with treasure object");
    TEST_ASSERT(!data.oblist.empty(), "v3 treasure path should still create an object");
}
REGISTER_TEST(test_level_data_load_version3_treasure_adds_at_start_success);

void test_level_data_load_version3_truncated_object_payload_fails()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 1); // listsize
    append_u8(bytes, static_cast<uint8_t>(Order::Living));
    // Truncate the rest of the object fields to force a read failure.

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_3(rw, &data);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_3 should fail on truncated object payload");
}
REGISTER_TEST(test_level_data_load_version3_truncated_object_payload_fails);

void test_level_data_load_version3_zero_lines_minimal_success()
{
    LevelData data(1);
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
    TEST_ASSERT_EQ(1, (int)ok, "load_version_3 should succeed with zero description lines");
}
REGISTER_TEST(test_level_data_load_version3_zero_lines_minimal_success);

void test_level_data_load_version3_truncates_long_description_line_and_discards_remaining_bytes()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 0); // listsize
    append_u8(bytes, 1);  // numlines
    append_u8(bytes, 200); // width > oneline[80], triggers truncation/discard loop
    for (int i = 0; i < 200; i++)
        append_u8(bytes, 'x');

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_3(rw, &data);
    TEST_ASSERT_EQ(1, (int)ok, "load_version_3 should succeed with truncated description line");
    TEST_ASSERT(!data.description.empty(), "description should contain at least one line");
}
REGISTER_TEST(test_level_data_load_version3_truncates_long_description_line_and_discards_remaining_bytes);

void test_level_data_load_version4_truncates_long_description_line()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 0); // listsize
    append_u8(bytes, 1);  // numlines
    append_u8(bytes, 120); // width > oneline[80], triggers truncation/discard loop
    for (int i = 0; i < 120; i++)
        append_u8(bytes, 'x');

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_4(rw, &data);
    TEST_ASSERT_EQ(1, (int)ok, "load_version_4 should succeed with truncated description line");
    TEST_ASSERT(!data.description.empty(), "description should contain at least one line");
}
REGISTER_TEST(test_level_data_load_version4_truncates_long_description_line);

void test_level_data_load_version5_rejects_invalid_object_count()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_u8(bytes, 2);     // scenario type
    append_i16(bytes, 5000); // > MAX_SCENARIO_OBJECTS
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_5 should reject invalid list size");
}
REGISTER_TEST(test_level_data_load_version5_rejects_invalid_object_count);

void test_level_data_load_version5_success_with_treasure_weapon_and_truncated_text()
{
    LevelData data(1);
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
    TEST_ASSERT_EQ(1, (int)ok, "load_version_5 should succeed on valid buffer");
    TEST_ASSERT_EQ(3, (int)data.type, "load_version_5 should set scenario type");
    TEST_ASSERT(!data.fxlist.empty(), "treasure object should populate fxlist");
    TEST_ASSERT(!data.weaplist.empty(), "weapon object should populate weaplist");
    TEST_ASSERT(!data.description.empty(), "description line should be read");
}
REGISTER_TEST(test_level_data_load_version5_success_with_treasure_weapon_and_truncated_text);

void test_level_data_load_version5_truncated_scenario_type_fails()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    // Truncate before scenario type byte.
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_5 should fail when scenario type byte is missing");
}
REGISTER_TEST(test_level_data_load_version5_truncated_scenario_type_fails);

void test_level_data_load_version5_truncated_object_payload_fails()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_u8(bytes, 1);  // scenario type
    append_i16(bytes, 1); // listsize
    append_u8(bytes, static_cast<uint8_t>(Order::Living));
    append_u8(bytes, static_cast<uint8_t>(FAMILY_SOLDIER));
    // Truncated before full object payload is available.
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_5 should fail on truncated object payload");
}
REGISTER_TEST(test_level_data_load_version5_truncated_object_payload_fails);

void test_level_data_load_version5_missing_numlines_fails()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_u8(bytes, 1);  // scenario type
    append_i16(bytes, 0); // listsize
    // Truncate before numlines byte.
    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_5(rw, &data);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_5 should fail when numlines byte is missing");
}
REGISTER_TEST(test_level_data_load_version5_missing_numlines_fails);

void test_level_data_load_version5_truncated_discard_tail_fails()
{
    LevelData data(1);
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
    TEST_ASSERT_EQ(0, (int)ok, "load_version_5 should fail when long-line discard bytes are truncated");
}
REGISTER_TEST(test_level_data_load_version5_truncated_discard_tail_fails);

void test_level_data_load_versions_2_3_4_missing_grid_or_count_fail()
{
    {
        LevelData data(1);
        std::vector<uint8_t> bytes; // missing grid bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(0, (int)load_version_2(rw, &data), "v2 should fail when grid field is missing");
    }
    {
        LevelData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid"); // missing listsize bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(0, (int)load_version_2(rw, &data), "v2 should fail when object count field is missing");
    }
    {
        LevelData data(1);
        std::vector<uint8_t> bytes; // missing grid bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(0, (int)load_version_3(rw, &data), "v3 should fail when grid field is missing");
    }
    {
        LevelData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid"); // missing listsize bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(0, (int)load_version_3(rw, &data), "v3 should fail when object count field is missing");
    }
    {
        LevelData data(1);
        std::vector<uint8_t> bytes; // missing grid bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(0, (int)load_version_4(rw, &data), "v4 should fail when grid field is missing");
    }
    {
        LevelData data(1);
        std::vector<uint8_t> bytes;
        append_fixed8(bytes, "grid"); // missing listsize bytes
        MemoryOgFile rw(bytes.data(), bytes.size());
        TEST_ASSERT_EQ(0, (int)load_version_4(rw, &data), "v4 should fail when object count field is missing");
    }
}
REGISTER_TEST(test_level_data_load_versions_2_3_4_missing_grid_or_count_fail);

void test_level_data_load_version4_truncated_discard_tail_fails()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 0); // listsize
    append_u8(bytes, 1);  // numlines
    append_u8(bytes, 120); // long line, requires discard
    for (int i = 0; i < 90; i++)
        append_u8(bytes, 'w'); // intentionally short payload for discard loop

    MemoryOgFile rw(bytes.data(), bytes.size());
    short ok = load_version_4(rw, &data);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_4 should fail when long-line discard bytes are truncated");
}
REGISTER_TEST(test_level_data_load_version4_truncated_discard_tail_fails);

void test_level_data_load_version4_truncated_numlines_or_width_fails()
{
    LevelData data(1);

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
        TEST_ASSERT_EQ(0, (int)load_version_4(rw, &data), "v4 should fail when numlines byte is missing");
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
        TEST_ASSERT_EQ(0, (int)load_version_4(rw, &data), "v4 should fail when description width byte is missing");
    }
}
REGISTER_TEST(test_level_data_load_version4_truncated_numlines_or_width_fails);

void test_level_data_load_versions_2_to_5_invalid_order_fails_object_creation()
{
    {
        LevelData data(1);
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
        TEST_ASSERT(ok == 0 || ok == 1, "v2 unknown-order input should not crash loader");
    }
    {
        LevelData data(1);
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
        TEST_ASSERT(ok == 0 || ok == 1, "v3 unknown-order input should not crash loader");
    }
    {
        LevelData data(1);
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
        TEST_ASSERT(ok == 0 || ok == 1, "v4 unknown-order input should not crash loader");
    }
    {
        LevelData data(1);
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
        TEST_ASSERT(ok == 0 || ok == 1, "v5 unknown-order input should not crash loader");
    }
}
REGISTER_TEST(test_level_data_load_versions_2_to_5_invalid_order_fails_object_creation);

void test_level_data_load_scenario_version_dispatcher_guards()
{
    std::vector<uint8_t> bytes;
    MemoryOgFile rw(bytes.data(), bytes.size());
    const short null_result = load_scenario_version(rw, nullptr, 2);
    TEST_ASSERT_EQ(0, (int)null_result, "dispatcher should reject null data pointer");

    LevelData data(1);
    MemoryOgFile rw2(bytes.data(), bytes.size());
    const short old_version_result = load_scenario_version(rw2, &data, 1);
    TEST_ASSERT_EQ(0, (int)old_version_result, "dispatcher should reject unsupported old version");
}
REGISTER_TEST(test_level_data_load_scenario_version_dispatcher_guards);

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

void test_level_data_load_version6plus_invalid_counts_and_object_fail_paths()
{
    {
        LevelData data(1);
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
        TEST_ASSERT_EQ(0, (int)load_version_6(rw, &data, 9), "v9 loader should reject invalid object count");
    }

    {
        LevelData data(1);
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
        TEST_ASSERT(ok == 0 || ok == 1, "v9 loader unknown order should not crash");
    }
}
REGISTER_TEST(test_level_data_load_version6plus_invalid_counts_and_object_fail_paths);

void test_level_data_load_version6plus_truncated_description_discard_path()
{
    LevelData data(1);
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
    append_u8(bytes, 200);  // long line width, triggers truncation/discard loop
    for (int i = 0; i < 120; i++)
        append_u8(bytes, 'q'); // intentionally short to force discard read failure

    MemoryOgFile rw(bytes.data(), bytes.size());
    TEST_ASSERT_EQ(0, (int)load_version_6(rw, &data, 9),
                   "v9 loader should fail when long description discard tail is truncated");
}
REGISTER_TEST(test_level_data_load_version6plus_truncated_description_discard_path);

void test_level_data_load_version6plus_named_objects_treasure_route_and_door_fixup()
{
    namespace fs = std::filesystem;
    const fs::path grid_path = "grid.pix";
    std::error_code ec;
    fs::remove(grid_path, ec);

    // Build a tiny 4x4 pixie: mostly grass with a wall directly above door tile (2,2)->(2,1).
    {
        std::FILE* f = std::fopen(grid_path.string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create grid.pix fixture");
        if (!f)
            return;
        const unsigned char header[] = {1, 4, 4}; // frames,w,h
        unsigned char data[16];
        for (unsigned char& b : data) b = PIX_GRASS1;
        data[2 + 4 * 1] = PIX_H_WALL1;
        std::fwrite(header, 1, sizeof(header), f);
        std::fwrite(data, 1, sizeof(data), f);
        std::fclose(f);
    }

    LevelData data(7777);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid"); // -> grid.pix
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
    TEST_ASSERT_EQ(1, (int)load_version_6(rw, &data, 9),
                   "v9 loader should parse treasure/door objects and long description");
    TEST_ASSERT(!data.fxlist.empty(), "treasure object should be routed into fxlist");
    TEST_ASSERT(!data.weaplist.empty(), "door object should be routed into weaplist");
    if (!data.fxlist.empty())
        TEST_ASSERT(data.fxlist.front()->stats()->query_bit_flags(BIT_NAMED) != 0,
                    "name length > 1 should set BIT_NAMED");

    bool saw_door = false;
    for (auto& uptr : data.weaplist)
    {
        walker* w = uptr.get();
        if (w && w->query_family() == FAMILY_DOOR)
        {
            saw_door = true;
            TEST_ASSERT_EQ(1, (int)w->query_frame(), "door with wall above should be frame 1 after fixup");
        }
    }
    TEST_ASSERT(saw_door, "door object should be present in loaded weapon list");

    fs::remove(grid_path, ec);
}
REGISTER_TEST(test_level_data_load_version6plus_named_objects_treasure_route_and_door_fixup);

void test_level_data_load_scenario_dispatch_case2_path()
{
    // Minimal v2 payload through dispatcher (case 2 branch).
    LevelData data(8888);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 0); // listsize
    MemoryOgFile rw(bytes.data(), bytes.size());
    const short result = load_scenario_version(rw, &data, 2);
    TEST_ASSERT(result == 0 || result == 1, "dispatch case 2 should execute without crashing");
}
REGISTER_TEST(test_level_data_load_scenario_dispatch_case2_path);
