#include <cstring>
#include <string>
#include <vector>

#include <physfs.h>

#include "test_framework.h"

void test_external_physfs_utf8_to_ucs_and_back_basic()
{
    const char* utf8 = "Hello \xE2\x98\x83"; // "Hello " + snowman

    PHYSFS_uint32 ucs4[64] = {0};
    PHYSFS_utf8ToUcs4(utf8, ucs4, sizeof(ucs4));
    TEST_ASSERT(ucs4[0] == 'H', "ucs4 first codepoint should be H");

    char back[128] = {0};
    PHYSFS_utf8FromUcs4(ucs4, back, sizeof(back));
    TEST_ASSERT(std::string(back).find("Hello") != std::string::npos, "roundtrip should keep ascii text");

    PHYSFS_uint16 ucs2[64] = {0};
    PHYSFS_utf8ToUcs2(utf8, ucs2, sizeof(ucs2));
    char back2[128] = {0};
    PHYSFS_utf8FromUcs2(ucs2, back2, sizeof(back2));
    TEST_ASSERT(std::string(back2).find("Hello") != std::string::npos, "ucs2 roundtrip should keep ascii text");
}
REGISTER_TEST(test_external_physfs_utf8_to_ucs_and_back_basic);

void test_external_physfs_utf8_invalid_sequences_are_handled()
{
    // Bad leading continuation byte, invalid 5-byte start, and truncated multibyte.
    const char bad_utf8[] = {
        char(0x80), 'A',
        char(0xF8), char(0x88), char(0x80), char(0x80), char(0x80),
        char(0xE2), char(0x82), // truncated 3-byte sequence
        0
    };

    PHYSFS_uint32 ucs4[64] = {0};
    PHYSFS_utf8ToUcs4(bad_utf8, ucs4, sizeof(ucs4));
    TEST_ASSERT(ucs4[0] != 0, "bad sequences should map to replacement-ish codepoint, not immediate terminator");

    PHYSFS_uint16 ucs2[64] = {0};
    PHYSFS_utf8ToUcs2(bad_utf8, ucs2, sizeof(ucs2));
    TEST_ASSERT(ucs2[0] != 0, "ucs2 conversion should produce replacement-ish codepoint");

    // Convert back to ensure encoder handles odd codepoints.
    char out[128] = {0};
    PHYSFS_utf8FromUcs4(ucs4, out, sizeof(out));
    TEST_ASSERT(out[0] != '\0', "utf8FromUcs4 should produce output for converted input");
}
REGISTER_TEST(test_external_physfs_utf8_invalid_sequences_are_handled);

void test_external_physfs_utf8_from_latin1_and_small_buffers()
{
    // Latin-1 bytes including >127 values.
    const char latin1[] = { char(0x41), char(0xE9), char(0xF1), 0 }; // A, e-acute, n-tilde
    char utf8[16] = {0};
    PHYSFS_utf8FromLatin1(latin1, utf8, sizeof(utf8));
    TEST_ASSERT(utf8[0] == 'A', "latin1 conversion should preserve ASCII");

    // Tiny buffer should still stay null-terminated and not overflow.
    char tiny[2] = {0};
    PHYSFS_utf8FromLatin1(latin1, tiny, sizeof(tiny));
    TEST_ASSERT(tiny[1] == '\0' || tiny[0] == '\0', "tiny buffer conversion should remain terminated");

    // Encode from UCS4 with invalid values to hit fallback codepaths.
    PHYSFS_uint32 weird[] = { 0x110000, 0xFFFE, 0xD800, 0 };
    char out[32] = {0};
    PHYSFS_utf8FromUcs4(weird, out, sizeof(out));
    TEST_ASSERT(out[0] != '\0', "invalid UCS4 codepoints should map to fallback output");
}
REGISTER_TEST(test_external_physfs_utf8_from_latin1_and_small_buffers);

