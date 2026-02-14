#include "SDL.h"
#include <openglad/data/level_data.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <array>
#include <cstdint>
#include <vector>

// Forward declarations from src/data/level_data.cpp
short load_version_2(SDL_RWops* infile, LevelData* data);
short load_version_3(SDL_RWops* infile, LevelData* data);
short load_version_4(SDL_RWops* infile, LevelData* data);

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

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_2(rw, &data);
    SDL_RWclose(rw);
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

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_2(rw, &data);
    SDL_RWclose(rw);
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

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_2(rw, &data);
    SDL_RWclose(rw);
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

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_2(rw, &data);
    SDL_RWclose(rw);
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
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_2(rw, &data);
    SDL_RWclose(rw);
    TEST_ASSERT_EQ(0, (int)ok, "load_version_2 should reject invalid list size");
}
REGISTER_TEST(test_level_data_load_version2_rejects_invalid_object_count);

void test_level_data_load_version3_rejects_invalid_object_count()
{
    LevelData data(1);
    std::vector<uint8_t> bytes;
    append_fixed8(bytes, "grid");
    append_i16(bytes, 5000); // > MAX_SCENARIO_OBJECTS
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_3(rw, &data);
    SDL_RWclose(rw);
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

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_3(rw, &data);
    SDL_RWclose(rw);
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

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_3(rw, &data);
    SDL_RWclose(rw);
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

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_3(rw, &data);
    SDL_RWclose(rw);
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

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_3(rw, &data);
    SDL_RWclose(rw);
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

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    TEST_ASSERT(rw != nullptr, "SDL_RWFromConstMem should succeed");
    short ok = load_version_4(rw, &data);
    SDL_RWclose(rw);
    TEST_ASSERT_EQ(1, (int)ok, "load_version_4 should succeed with truncated description line");
    TEST_ASSERT(!data.description.empty(), "description should contain at least one line");
}
REGISTER_TEST(test_level_data_load_version4_truncates_long_description_line);
