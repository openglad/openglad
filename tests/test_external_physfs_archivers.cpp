#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

#include <physfs.h>

#include "test_framework.h"

static void append_u32le(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back((uint8_t)(v & 0xff));
    out.push_back((uint8_t)((v >> 8) & 0xff));
    out.push_back((uint8_t)((v >> 16) & 0xff));
    out.push_back((uint8_t)((v >> 24) & 0xff));
}

static bool write_temp_file_bytes(const std::vector<uint8_t>& bytes,
                                  const char* suffix,
                                  std::string* out_path)
{
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path();
    fs::path path = dir / (std::string("openglad_") + std::to_string(::getpid()) + suffix);

    FILE* f = fopen(path.string().c_str(), "wb");
    if (!f)
        return false;
    size_t written = fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    if (written != bytes.size())
        return false;

    if (out_path)
        *out_path = path.string();
    return true;
}

static bool read_physfs_file_all(const char* vpath, std::string* out)
{
    PHYSFS_File* f = PHYSFS_openRead(vpath);
    if (!f)
        return false;

    std::string tmp;
    char buf[256];
    while (1) {
        const PHYSFS_sint64 got = PHYSFS_read(f, buf, 1, sizeof(buf));
        if (got < 0) {
            PHYSFS_close(f);
            return false;
        }
        if (got == 0)
            break;
        tmp.append(buf, (size_t)got);
        if (PHYSFS_eof(f))
            break;
    }

    PHYSFS_close(f);
    if (out)
        *out = tmp;
    return true;
}

static std::vector<uint8_t> make_qpak_one_file(const char* name56, const std::string& contents)
{
    std::vector<uint8_t> out;

    // Header: "PACK" + dir offset + dir len (both U32 LE).
    out.insert(out.end(), {'P', 'A', 'C', 'K'});

    const uint32_t header_sz = 12;
    const uint32_t file_pos = header_sz;
    const uint32_t file_sz = (uint32_t)contents.size();
    const uint32_t dir_off = header_sz + file_sz;
    const uint32_t dir_len = 64;

    append_u32le(out, dir_off);
    append_u32le(out, dir_len);

    // File data.
    out.insert(out.end(), contents.begin(), contents.end());

    // Directory entry (64 bytes).
    std::vector<uint8_t> entry(64, 0);
    for (int i = 0; i < 56 && name56[i]; i++)
        entry[i] = (uint8_t)name56[i];
    // pos, len
    entry[56] = (uint8_t)(file_pos & 0xff);
    entry[57] = (uint8_t)((file_pos >> 8) & 0xff);
    entry[58] = (uint8_t)((file_pos >> 16) & 0xff);
    entry[59] = (uint8_t)((file_pos >> 24) & 0xff);
    entry[60] = (uint8_t)(file_sz & 0xff);
    entry[61] = (uint8_t)((file_sz >> 8) & 0xff);
    entry[62] = (uint8_t)((file_sz >> 16) & 0xff);
    entry[63] = (uint8_t)((file_sz >> 24) & 0xff);

    out.insert(out.end(), entry.begin(), entry.end());
    return out;
}

static std::vector<uint8_t> make_wad_one_lump(const char* name8, const std::string& contents)
{
    std::vector<uint8_t> out;

    // Header: "PWAD" + numlumps + directory offset.
    out.insert(out.end(), {'P', 'W', 'A', 'D'});
    append_u32le(out, 1); // lumps

    const uint32_t header_sz = 12;
    const uint32_t lump_pos = header_sz;
    const uint32_t lump_sz = (uint32_t)contents.size();
    const uint32_t dir_off = header_sz + lump_sz;
    append_u32le(out, dir_off);

    // Lump data.
    out.insert(out.end(), contents.begin(), contents.end());

    // Directory entry: pos, size, name (8 bytes).
    append_u32le(out, lump_pos);
    append_u32le(out, lump_sz);
    for (int i = 0; i < 8; i++)
        out.push_back((uint8_t)(name8 && name8[i] ? name8[i] : 0));

    return out;
}

static std::vector<uint8_t> make_hog_one_file(const char* name13, const std::string& contents)
{
    std::vector<uint8_t> out;
    out.insert(out.end(), {'D', 'H', 'F'});

    // Entry header: name (13 bytes) + size (U32 LE).
    for (int i = 0; i < 13; i++)
        out.push_back((uint8_t)(name13 && name13[i] ? name13[i] : 0));
    append_u32le(out, (uint32_t)contents.size());

    // File data.
    out.insert(out.end(), contents.begin(), contents.end());
    return out;
}

static std::vector<uint8_t> make_grp_one_file(const char* name12, const std::string& contents)
{
    std::vector<uint8_t> out;
    // Header: "KenSilverman" + num files (U32 LE)
    out.insert(out.end(), {'K','e','n','S','i','l','v','e','r','m','a','n'});
    append_u32le(out, 1);

    // Directory entry: name[12] + size(U32 LE)
    for (int i = 0; i < 12; i++)
        out.push_back((uint8_t)(name12 && name12[i] ? name12[i] : 0));
    append_u32le(out, (uint32_t)contents.size());

    // File data
    out.insert(out.end(), contents.begin(), contents.end());
    return out;
}

static std::vector<uint8_t> make_mvl_one_file(const char* name13, const std::string& contents)
{
    std::vector<uint8_t> out;
    // Header: "DMVL" + num files (U32 LE)
    out.insert(out.end(), {'D','M','V','L'});
    append_u32le(out, 1);

    // Directory entry: name[13] + size(U32 LE)
    for (int i = 0; i < 13; i++)
        out.push_back((uint8_t)(name13 && name13[i] ? name13[i] : 0));
    append_u32le(out, (uint32_t)contents.size());

    // File data
    out.insert(out.end(), contents.begin(), contents.end());
    return out;
}

void test_external_physfs_qpak_wad_hog_mount_and_read()
{
    TEST_ASSERT(PHYSFS_isInit(), "PHYSFS should be initialized");

    // --- QPAK ---
#if PHYSFS_SUPPORTS_QPAK
    {
        std::vector<uint8_t> pak = make_qpak_one_file("hello.txt", "PAK\n");
        std::string path;
        TEST_ASSERT(write_temp_file_bytes(pak, "_arch.pak", &path), "write qpak temp file");
        TEST_ASSERT(PHYSFS_mount(path.c_str(), nullptr, 1), "PHYSFS_mount qpak");
        std::string got;
        TEST_ASSERT(read_physfs_file_all("hello.txt", &got), "read file from qpak");
        TEST_ASSERT(got == "PAK\n", "qpak file contents");
        TEST_ASSERT(PHYSFS_unmount(path.c_str()), "unmount qpak");
    }
#endif

    // --- WAD ---
#if PHYSFS_SUPPORTS_WAD
    {
        std::vector<uint8_t> wad = make_wad_one_lump("HELLO", "WAD!");
        std::string path;
        TEST_ASSERT(write_temp_file_bytes(wad, "_arch.wad", &path), "write wad temp file");
        TEST_ASSERT(PHYSFS_mount(path.c_str(), nullptr, 1), "PHYSFS_mount wad");
        std::string got;
        // WAD is case-insensitive in PhysFS; use uppercase lump name.
        TEST_ASSERT(read_physfs_file_all("HELLO", &got), "read lump from wad");
        TEST_ASSERT(got == "WAD!", "wad lump contents");
        TEST_ASSERT(PHYSFS_unmount(path.c_str()), "unmount wad");
    }
#endif

    // --- HOG ---
#if PHYSFS_SUPPORTS_HOG
    {
        std::vector<uint8_t> hog = make_hog_one_file("HELLO.TXT", "HOG");
        std::string path;
        TEST_ASSERT(write_temp_file_bytes(hog, "_arch.hog", &path), "write hog temp file");
        TEST_ASSERT(PHYSFS_mount(path.c_str(), nullptr, 1), "PHYSFS_mount hog");
        std::string got;
        TEST_ASSERT(read_physfs_file_all("HELLO.TXT", &got), "read file from hog");
        TEST_ASSERT(got == "HOG", "hog file contents");
        TEST_ASSERT(PHYSFS_unmount(path.c_str()), "unmount hog");
    }
#endif
}
REGISTER_TEST(test_external_physfs_qpak_wad_hog_mount_and_read);

void test_external_physfs_grp_mvl_mount_and_read()
{
    TEST_ASSERT(PHYSFS_isInit(), "PHYSFS should be initialized");

    // --- GRP ---
#if PHYSFS_SUPPORTS_GRP
    {
        std::vector<uint8_t> grp = make_grp_one_file("HELLO.TXT", "GRP!");
        std::string path;
        TEST_ASSERT(write_temp_file_bytes(grp, "_arch.grp", &path), "write grp temp file");
        TEST_ASSERT(PHYSFS_mount(path.c_str(), nullptr, 1), "PHYSFS_mount grp");
        std::string got;
        TEST_ASSERT(read_physfs_file_all("HELLO.TXT", &got), "read file from grp");
        TEST_ASSERT(got == "GRP!", "grp file contents");
        TEST_ASSERT(PHYSFS_unmount(path.c_str()), "unmount grp");
    }
#endif

    // --- MVL ---
#if PHYSFS_SUPPORTS_MVL
    {
        std::vector<uint8_t> mvl = make_mvl_one_file("INTRO.HNM", "MVL");
        std::string path;
        TEST_ASSERT(write_temp_file_bytes(mvl, "_arch.mvl", &path), "write mvl temp file");
        TEST_ASSERT(PHYSFS_mount(path.c_str(), nullptr, 1), "PHYSFS_mount mvl");
        std::string got;
        TEST_ASSERT(read_physfs_file_all("INTRO.HNM", &got), "read file from mvl");
        TEST_ASSERT(got == "MVL", "mvl file contents");
        TEST_ASSERT(PHYSFS_unmount(path.c_str()), "unmount mvl");
    }
#endif
}
REGISTER_TEST(test_external_physfs_grp_mvl_mount_and_read);
