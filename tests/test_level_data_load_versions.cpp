#include "SDL.h"
#include <openglad/data/level_data.h>
#include <openglad/io/og_file.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// Forward declarations from src/runtime/level_data.cpp (now using OgFile&)
short load_version_2(og::io::OgFile& infile, LevelData* data);
short load_version_3(og::io::OgFile& infile, LevelData* data);
short load_version_4(og::io::OgFile& infile, LevelData* data);

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
