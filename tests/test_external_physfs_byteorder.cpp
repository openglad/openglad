#include <filesystem>
#include <string>

#include <unistd.h>

#include <physfs.h>

#include "test_framework.h"

void test_external_physfs_swap_endian_helpers_roundtrip()
{
    TEST_ASSERT(PHYSFS_swapULE16(0x1234) == 0x1234, "swapULE16 on little-endian should preserve value");
    TEST_ASSERT(PHYSFS_swapUBE16(0x1234) == 0x3412, "swapUBE16 should byte-swap on little-endian");

    TEST_ASSERT(PHYSFS_swapULE32(0x12345678u) == 0x12345678u, "swapULE32 should preserve value");
    TEST_ASSERT(PHYSFS_swapUBE32(0x12345678u) == 0x78563412u, "swapUBE32 should byte-swap");

    const PHYSFS_uint64 v64 = 0x1122334455667788ULL;
    TEST_ASSERT(PHYSFS_swapULE64(v64) == v64, "swapULE64 should preserve value");
    TEST_ASSERT(PHYSFS_swapUBE64(v64) == 0x8877665544332211ULL, "swapUBE64 should byte-swap");

    TEST_ASSERT(PHYSFS_swapSBE16((PHYSFS_sint16)0x1234) == (PHYSFS_sint16)0x3412, "signed 16-bit swap should work");
    TEST_ASSERT(PHYSFS_swapSBE32((PHYSFS_sint32)0x12345678) == (PHYSFS_sint32)0x78563412, "signed 32-bit swap should work");
}
REGISTER_TEST(test_external_physfs_swap_endian_helpers_roundtrip);

void test_external_physfs_read_write_endian_helpers()
{
    TEST_ASSERT(PHYSFS_isInit(), "PHYSFS should be initialized");

    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_physfs_bo_" + std::to_string(::getpid()));
    fs::create_directories(base);

    const char* old_write = PHYSFS_getWriteDir();
    std::string old_write_s = old_write ? old_write : "";

    TEST_ASSERT(PHYSFS_setWriteDir(base.string().c_str()), "PHYSFS_setWriteDir should succeed");
    TEST_ASSERT(PHYSFS_mount(base.string().c_str(), nullptr, 0), "PHYSFS_mount(write dir) should succeed");

    PHYSFS_File* wf = PHYSFS_openWrite("byteorder.bin");
    TEST_ASSERT(wf != nullptr, "PHYSFS_openWrite should succeed");

    TEST_ASSERT(PHYSFS_writeULE16(wf, 0x1234u), "write ULE16");
    TEST_ASSERT(PHYSFS_writeSLE16(wf, (PHYSFS_sint16)-1234), "write SLE16");
    TEST_ASSERT(PHYSFS_writeUBE16(wf, 0x5678u), "write UBE16");
    TEST_ASSERT(PHYSFS_writeSBE16(wf, (PHYSFS_sint16)-2345), "write SBE16");

    TEST_ASSERT(PHYSFS_writeULE32(wf, 0x12345678u), "write ULE32");
    TEST_ASSERT(PHYSFS_writeSLE32(wf, (PHYSFS_sint32)-1234567), "write SLE32");
    TEST_ASSERT(PHYSFS_writeUBE32(wf, 0x89ABCDEFu), "write UBE32");
    TEST_ASSERT(PHYSFS_writeSBE32(wf, (PHYSFS_sint32)-2345678), "write SBE32");

    TEST_ASSERT(PHYSFS_writeULE64(wf, 0x1122334455667788ULL), "write ULE64");
    TEST_ASSERT(PHYSFS_writeSLE64(wf, (PHYSFS_sint64)-1234567890123LL), "write SLE64");
    TEST_ASSERT(PHYSFS_writeUBE64(wf, 0x8877665544332211ULL), "write UBE64");
    TEST_ASSERT(PHYSFS_writeSBE64(wf, (PHYSFS_sint64)-2345678901234LL), "write SBE64");
    TEST_ASSERT(PHYSFS_close(wf), "PHYSFS_close(write) should succeed");

    PHYSFS_File* rf = PHYSFS_openRead("byteorder.bin");
    TEST_ASSERT(rf != nullptr, "PHYSFS_openRead should succeed");

    PHYSFS_uint16 u16 = 0;
    PHYSFS_sint16 s16 = 0;
    PHYSFS_uint32 u32 = 0;
    PHYSFS_sint32 s32 = 0;
    PHYSFS_uint64 u64 = 0;
    PHYSFS_sint64 s64 = 0;

    TEST_ASSERT(PHYSFS_readULE16(rf, &u16) && u16 == 0x1234u, "read ULE16");
    TEST_ASSERT(PHYSFS_readSLE16(rf, &s16) && s16 == (PHYSFS_sint16)-1234, "read SLE16");
    TEST_ASSERT(PHYSFS_readUBE16(rf, &u16) && u16 == 0x5678u, "read UBE16");
    TEST_ASSERT(PHYSFS_readSBE16(rf, &s16) && s16 == (PHYSFS_sint16)-2345, "read SBE16");

    TEST_ASSERT(PHYSFS_readULE32(rf, &u32) && u32 == 0x12345678u, "read ULE32");
    TEST_ASSERT(PHYSFS_readSLE32(rf, &s32) && s32 == (PHYSFS_sint32)-1234567, "read SLE32");
    TEST_ASSERT(PHYSFS_readUBE32(rf, &u32) && u32 == 0x89ABCDEFu, "read UBE32");
    TEST_ASSERT(PHYSFS_readSBE32(rf, &s32) && s32 == (PHYSFS_sint32)-2345678, "read SBE32");

    TEST_ASSERT(PHYSFS_readULE64(rf, &u64) && u64 == 0x1122334455667788ULL, "read ULE64");
    TEST_ASSERT(PHYSFS_readSLE64(rf, &s64) && s64 == (PHYSFS_sint64)-1234567890123LL, "read SLE64");
    TEST_ASSERT(PHYSFS_readUBE64(rf, &u64) && u64 == 0x8877665544332211ULL, "read UBE64");
    TEST_ASSERT(PHYSFS_readSBE64(rf, &s64) && s64 == (PHYSFS_sint64)-2345678901234LL, "read SBE64");

    TEST_ASSERT(!PHYSFS_readULE16(rf, nullptr), "read with null output pointer should fail");
    TEST_ASSERT(!PHYSFS_readULE16(rf, &u16), "read past EOF should fail");

    TEST_ASSERT(PHYSFS_close(rf), "PHYSFS_close(read) should succeed");

    (void)PHYSFS_removeFromSearchPath(base.string().c_str());
    if (!old_write_s.empty())
        (void)PHYSFS_setWriteDir(old_write_s.c_str());
}
REGISTER_TEST(test_external_physfs_read_write_endian_helpers);
