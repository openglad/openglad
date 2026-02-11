#include <cstring>
#include <cstdint>
#include <filesystem>
#include <string>

#include <unistd.h>

#include "zip.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "zipint.h"
#ifdef __cplusplus
}
#endif

#include "test_framework.h"

static std::string tmp_zip_path()
{
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path();
    fs::path path = dir / ("openglad_libzip_" + std::to_string(::getpid()) + ".zip");
    return path.string();
}

void test_external_libzip_create_add_read_close()
{
    const std::string path = tmp_zip_path();

    int err = 0;
    zip* za = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    TEST_ASSERT(za != nullptr, "zip_open create");

    const char* hello = "Hello from libzip!\n";
    zip_source* src = zip_source_buffer(za, hello, strlen(hello), 0);
    TEST_ASSERT(src != nullptr, "zip_source_buffer");

    zip_int64_t idx = zip_file_add(za, "hello.txt", src, ZIP_FL_OVERWRITE);
    TEST_ASSERT(idx >= 0, "zip_file_add");

    zip_int64_t diridx = zip_dir_add(za, "dir/", ZIP_FL_ENC_GUESS);
    TEST_ASSERT(diridx >= 0, "zip_dir_add");

    const char* nested = "Nested libzip file.";
    zip_source* src2 = zip_source_buffer(za, nested, strlen(nested), 0);
    TEST_ASSERT(src2 != nullptr, "zip_source_buffer 2");
    zip_int64_t idx2 = zip_file_add(za, "dir/nested.txt", src2, ZIP_FL_OVERWRITE);
    TEST_ASSERT(idx2 >= 0, "zip_file_add nested");

    TEST_ASSERT(zip_close(za) == 0, "zip_close");

    // Reopen and read back
    zip* zr = zip_open(path.c_str(), ZIP_CHECKCONS, &err);
    TEST_ASSERT(zr != nullptr, "zip_open read");

    zip_int64_t n = zip_get_num_entries(zr, 0);
    TEST_ASSERT(n >= 2, "zip_get_num_entries");

    zip_file* zf = zip_fopen(zr, "hello.txt", 0);
    TEST_ASSERT(zf != nullptr, "zip_fopen hello.txt");
    char buf[64];
    memset(buf, 0, sizeof(buf));
    zip_int64_t got = zip_fread(zf, buf, sizeof(buf) - 1);
    TEST_ASSERT(got > 0, "zip_fread hello.txt");
    zip_fclose(zf);

    TEST_ASSERT(std::string(buf).find("Hello from libzip!") != std::string::npos,
                "hello contents");

    zip_close(zr);

    // Additional zip_open paths.
    zip* z_excl = zip_open(path.c_str(), ZIP_CREATE | ZIP_EXCL, &err);
    TEST_ASSERT(z_excl == nullptr, "zip_open with EXCL on existing archive should fail");

    zip* z_badflags = zip_open(path.c_str(), -1, &err);
    TEST_ASSERT(z_badflags == nullptr, "zip_open with negative flags should fail");

    namespace fs = std::filesystem;
    fs::path dir_path = fs::temp_directory_path() / ("openglad_libzip_dir_" + std::to_string(::getpid()));
    fs::create_directories(dir_path);
    zip* z_dir = zip_open(dir_path.string().c_str(), 0, &err);
    TEST_ASSERT(z_dir == nullptr, "zip_open on directory path should fail");

    zip* z_trunc = zip_open(path.c_str(), ZIP_TRUNCATE, &err);
    TEST_ASSERT(z_trunc != nullptr, "zip_open with TRUNCATE on existing file should succeed");
    TEST_ASSERT(zip_close(z_trunc) == 0, "zip_close truncated archive");
}
REGISTER_TEST(test_external_libzip_create_add_read_close);

void test_external_libzip_rename_comments_and_stat_paths()
{
    const std::string path = tmp_zip_path();
    int err = 0;

    zip* za = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    TEST_ASSERT(za != nullptr, "zip_open create");

    const char* payload = "alpha";
    zip_source* src = zip_source_buffer(za, payload, strlen(payload), 0);
    TEST_ASSERT(src != nullptr, "zip_source_buffer");
    zip_int64_t idx = zip_file_add(za, "a.txt", src, ZIP_FL_OVERWRITE);
    TEST_ASSERT(idx >= 0, "zip_file_add a.txt");

    TEST_ASSERT(zip_set_archive_comment(za, "archive-comment", 15) == 0,
                "zip_set_archive_comment");
    TEST_ASSERT(zip_file_set_comment(za, (zip_uint64_t)idx, "file-comment", 12, 0) == 0,
                "zip_file_set_comment");
    TEST_ASSERT(zip_file_rename(za, (zip_uint64_t)idx, "b.txt", 0) == 0,
                "zip_file_rename");
    TEST_ASSERT(zip_set_file_compression(za, (zip_uint64_t)idx, ZIP_CM_STORE, 0) == 0,
                "zip_set_file_compression");
    TEST_ASSERT(zip_archive_set_tempdir(za, "/tmp") == 0, "zip_archive_set_tempdir");
    TEST_ASSERT(zip_set_archive_flag(za, ZIP_AFL_TORRENT, 1) == 0, "zip_set_archive_flag torrent");

    TEST_ASSERT(zip_close(za) == 0, "zip_close");

    zip* zr = zip_open(path.c_str(), ZIP_CHECKCONS, &err);
    TEST_ASSERT(zr != nullptr, "zip_open read");

    zip_int64_t bidx = zip_name_locate(zr, "b.txt", 0);
    TEST_ASSERT(bidx >= 0, "zip_name_locate b.txt");

    struct zip_stat st;
    zip_stat_init(&st);
    TEST_ASSERT(zip_stat_index(zr, (zip_uint64_t)bidx, 0, &st) == 0, "zip_stat_index");
    TEST_ASSERT(st.size == 5, "zip_stat_index size should match payload");

    zip_file* zf = zip_fopen_index(zr, (zip_uint64_t)bidx, 0);
    TEST_ASSERT(zf != nullptr, "zip_fopen_index");
    char buf[16] = {};
    zip_int64_t got = zip_fread(zf, buf, sizeof(buf));
    TEST_ASSERT(got > 0, "zip_fread");
    zip_fclose(zf);
    TEST_ASSERT(std::string(buf).find("alpha") != std::string::npos, "renamed file content");

    zip_close(zr);
}
REGISTER_TEST(test_external_libzip_rename_comments_and_stat_paths);

void test_external_libzip_replace_delete_and_unchange_paths()
{
    const std::string path = tmp_zip_path();
    int err = 0;

    zip* za = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    TEST_ASSERT(za != nullptr, "zip_open create");

    zip_source* s1 = zip_source_buffer(za, "one", 3, 0);
    zip_source* s2 = zip_source_buffer(za, "two", 3, 0);
    TEST_ASSERT(s1 && s2, "zip_source_buffer");
    zip_int64_t i1 = zip_file_add(za, "one.txt", s1, ZIP_FL_OVERWRITE);
    zip_int64_t i2 = zip_file_add(za, "two.txt", s2, ZIP_FL_OVERWRITE);
    TEST_ASSERT(i1 >= 0 && i2 >= 0, "zip_file_add");
    TEST_ASSERT(zip_close(za) == 0, "zip_close initial");

    zip* zm = zip_open(path.c_str(), 0, &err);
    TEST_ASSERT(zm != nullptr, "zip_open modify");

    zip_int64_t idx_one = zip_name_locate(zm, "one.txt", 0);
    zip_int64_t idx_two = zip_name_locate(zm, "two.txt", 0);
    TEST_ASSERT(idx_one >= 0 && idx_two >= 0, "zip_name_locate existing files");

    zip_source* repl = zip_source_buffer(zm, "ONE-REPLACED", 12, 0);
    TEST_ASSERT(repl != nullptr, "zip_source_buffer replace");
    TEST_ASSERT(zip_file_replace(zm, (zip_uint64_t)idx_one, repl, ZIP_FL_OVERWRITE) == 0,
                "zip_file_replace");

    TEST_ASSERT(zip_delete(zm, (zip_uint64_t)idx_two) == 0, "zip_delete");
    TEST_ASSERT(zip_unchange(zm, (zip_uint64_t)idx_two) == 0, "zip_unchange deleted entry");
    TEST_ASSERT(zip_unchange_archive(zm) == 0, "zip_unchange_archive");
    TEST_ASSERT(zip_unchange_all(zm) == 0, "zip_unchange_all");

    TEST_ASSERT(zip_close(zm) == 0, "zip_close modify");

    // Reopen and do metadata-only change to drive copy_data path in zip_close().
    zip* zmeta = zip_open(path.c_str(), 0, &err);
    TEST_ASSERT(zmeta != nullptr, "zip_open metadata-modify");
    zip_int64_t idx_meta = zip_name_locate(zmeta, "one.txt", 0);
    TEST_ASSERT(idx_meta >= 0, "zip_name_locate one.txt for metadata modify");
    TEST_ASSERT(zip_file_rename(zmeta, (zip_uint64_t)idx_meta, "one-renamed.txt", 0) == 0,
                "zip_file_rename metadata-only");
    TEST_ASSERT(zip_close(zmeta) == 0, "zip_close metadata-only");
}
REGISTER_TEST(test_external_libzip_replace_delete_and_unchange_paths);

void test_external_libzip_extra_field_api_paths()
{
    const std::string path = tmp_zip_path();
    int err = 0;

    zip* za = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    TEST_ASSERT(za != nullptr, "zip_open create");

    zip_source* src = zip_source_buffer(za, "payload", 7, 0);
    TEST_ASSERT(src != nullptr, "zip_source_buffer");
    zip_int64_t idx = zip_file_add(za, "extra.txt", src, ZIP_FL_OVERWRITE);
    TEST_ASSERT(idx >= 0, "zip_file_add");

    const zip_uint8_t ef_data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    TEST_ASSERT(zip_file_extra_field_set(za, (zip_uint64_t)idx, 0xCAFE, 0,
                                         ef_data, 4, ZIP_FL_LOCAL | ZIP_FL_CENTRAL) == 0,
                "zip_file_extra_field_set");

    zip_int16_t cnt_all = zip_file_extra_fields_count(za, (zip_uint64_t)idx, ZIP_FL_LOCAL);
    TEST_ASSERT(cnt_all >= 1, "zip_file_extra_fields_count local should be >= 1");

    zip_int16_t cnt_id = zip_file_extra_fields_count_by_id(za, (zip_uint64_t)idx, 0xCAFE, ZIP_FL_LOCAL);
    TEST_ASSERT(cnt_id >= 1, "zip_file_extra_fields_count_by_id should find inserted id");

    zip_uint16_t got_id = 0;
    zip_uint16_t got_len = 0;
    const zip_uint8_t* got = zip_file_extra_field_get(za, (zip_uint64_t)idx, 0, &got_id, &got_len, ZIP_FL_LOCAL);
    TEST_ASSERT(got != nullptr, "zip_file_extra_field_get should return first field");
    TEST_ASSERT(got_len > 0, "extra field length should be > 0");

    zip_uint16_t got_len2 = 0;
    const zip_uint8_t* got2 = zip_file_extra_field_get_by_id(za, (zip_uint64_t)idx, 0xCAFE, 0, &got_len2, ZIP_FL_LOCAL);
    TEST_ASSERT(got2 != nullptr, "zip_file_extra_field_get_by_id should return inserted field");
    TEST_ASSERT_EQ(4, (int)got_len2, "inserted extra field length should match");

    TEST_ASSERT(zip_file_extra_field_delete_by_id(za, (zip_uint64_t)idx, 0xCAFE, 0, ZIP_FL_LOCAL) == 0,
                "zip_file_extra_field_delete_by_id local");
    // This may no-op if already gone in this location, but should not fail hard.
    (void)zip_file_extra_field_delete(za, (zip_uint64_t)idx, 0, ZIP_FL_CENTRAL);

    TEST_ASSERT(zip_close(za) == 0, "zip_close");

    // Internal dirent/cdir APIs to cover deep zip_dirent.c paths.
    zip_error zerr;
    _zip_error_init(&zerr);

    zip_dirent* de = _zip_dirent_new();
    TEST_ASSERT(de != nullptr, "_zip_dirent_new");
    de->comp_method = ZIP_CM_STORE;
    de->version_madeby = 45;
    de->version_needed = 45;
    de->comp_size = static_cast<zip_uint64_t>(UINT32_MAX) + 1234;
    de->uncomp_size = static_cast<zip_uint64_t>(UINT32_MAX) + 5678;
    de->offset = static_cast<zip_uint64_t>(UINT32_MAX) + 999;

    const char* utf8_name = "utf8-\xCE\xA9.txt";
    const char* utf8_comment = "comment-\xCE\xA9";
    de->filename = _zip_string_new(reinterpret_cast<const zip_uint8_t*>(utf8_name),
                                   static_cast<zip_uint16_t>(strlen(utf8_name)),
                                   ZIP_FL_ENC_GUESS, &zerr);
    de->comment = _zip_string_new(reinterpret_cast<const zip_uint8_t*>(utf8_comment),
                                  static_cast<zip_uint16_t>(strlen(utf8_comment)),
                                  ZIP_FL_ENC_GUESS, &zerr);
    TEST_ASSERT(de->filename != nullptr && de->comment != nullptr, "_zip_string_new for dirent");

    const zip_uint8_t raw_ef[8] = {1,2,3,4,5,6,7,8};
    de->extra_fields = _zip_ef_new(0xBEEF, 8, raw_ef, ZIP_EF_BOTH);
    TEST_ASSERT(de->extra_fields != nullptr, "_zip_ef_new");

    TEST_ASSERT(_zip_dirent_needs_zip64(de, ZIP_FL_CENTRAL) == 1, "_zip_dirent_needs_zip64 central");
    TEST_ASSERT(_zip_dirent_needs_zip64(de, ZIP_FL_LOCAL) == 1, "_zip_dirent_needs_zip64 local");

    FILE* fp = tmpfile();
    TEST_ASSERT(fp != nullptr, "tmpfile for dirent write");

    int write_ret = _zip_dirent_write(de, fp, ZIP_FL_CENTRAL | ZIP_FL_FORCE_ZIP64, &zerr);
    TEST_ASSERT(write_ret >= 0, "_zip_dirent_write central zip64");
    rewind(fp);
    zip_int32_t dsize = _zip_dirent_size(fp, ZIP_FL_CENTRAL, &zerr);
    TEST_ASSERT(dsize > 0, "_zip_dirent_size central");

    rewind(fp);
    zip_dirent parsed;
    _zip_dirent_init(&parsed);
    (void)_zip_dirent_read(&parsed, fp, nullptr, nullptr, 0, &zerr);
    _zip_dirent_finalize(&parsed);
    fclose(fp);

    _zip_dirent_torrent_normalize(de);
    FILE* fp_local = tmpfile();
    TEST_ASSERT(fp_local != nullptr, "tmpfile for local dirent write");
    write_ret = _zip_dirent_write(de, fp_local, ZIP_FL_LOCAL | ZIP_FL_FORCE_ZIP64, &zerr);
    TEST_ASSERT(write_ret >= 0, "_zip_dirent_write local zip64");
    fclose(fp_local);

    zip_dirent* cloned = _zip_dirent_clone(de);
    TEST_ASSERT(cloned != nullptr, "_zip_dirent_clone");
    _zip_dirent_free(cloned);
    _zip_dirent_free(de);

    zip_cdir* cd = _zip_cdir_new(1, &zerr);
    TEST_ASSERT(cd != nullptr, "_zip_cdir_new");
    TEST_ASSERT(_zip_cdir_grow(cd, 3, &zerr) == 0, "_zip_cdir_grow");
    _zip_cdir_free(cd);

    zip_uint8_t buf[16] = {};
    zip_uint8_t* p = buf;
    _zip_poke4(0xA1B2C3D4u, &p);
    _zip_poke8(0x0102030405060708ULL, &p);
    const zip_uint8_t* rp = buf + 4;
    zip_uint64_t r8 = _zip_read8(&rp);
    TEST_ASSERT(r8 == 0x0102030405060708ULL, "_zip_read8 should decode poked value");

    FILE* fw = tmpfile();
    TEST_ASSERT(fw != nullptr, "tmpfile for _zip_write8");
    _zip_write8(0x0F0E0D0C0B0A0908ULL, fw);
    TEST_ASSERT(ferror(fw) == 0, "_zip_write8 should not set error");
    fclose(fw);

    auto put2 = [](unsigned char* p, zip_uint16_t v) {
        p[0] = static_cast<unsigned char>(v & 0xFF);
        p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
    };
    auto put4 = [](unsigned char* p, zip_uint32_t v) {
        p[0] = static_cast<unsigned char>(v & 0xFF);
        p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
        p[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
        p[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
    };

    // Not enough bytes for a central dirent header.
    {
        zip_dirent de;
        _zip_dirent_init(&de);
        const unsigned char tiny[4] = {0, 0, 0, 0};
        const unsigned char* p = tiny;
        zip_uint64_t left = sizeof(tiny);
        TEST_ASSERT(_zip_dirent_read(&de, nullptr, &p, &left, 0, &zerr) < 0,
                    "_zip_dirent_read should fail when left < header size");
        _zip_dirent_finalize(&de);
    }

    // Wrong magic.
    {
        unsigned char hdr[46] = {};
        memcpy(hdr, "NOPE", 4);
        const unsigned char* p = hdr;
        zip_uint64_t left = sizeof(hdr);
        zip_dirent de;
        _zip_dirent_init(&de);
        TEST_ASSERT(_zip_dirent_read(&de, nullptr, &p, &left, 0, &zerr) < 0,
                    "_zip_dirent_read should fail on bad magic");
        _zip_dirent_finalize(&de);
    }

    // Header says filename is present, but left bytes are insufficient.
    {
        unsigned char hdr[46] = {};
        memcpy(hdr, CENTRAL_MAGIC, 4);
        put2(hdr + 4, 20);   // version madeby
        put2(hdr + 6, 20);   // version needed
        put2(hdr + 28, 5);   // filename length
        const unsigned char* p = hdr;
        zip_uint64_t left = sizeof(hdr);
        zip_dirent de;
        _zip_dirent_init(&de);
        TEST_ASSERT(_zip_dirent_read(&de, nullptr, &p, &left, 0, &zerr) < 0,
                    "_zip_dirent_read should fail on truncated variable section");
        _zip_dirent_finalize(&de);
    }

    // UTF-8 flag set with invalid UTF-8 filename.
    {
        unsigned char buf[47] = {};
        memcpy(buf, CENTRAL_MAGIC, 4);
        put2(buf + 4, 20);
        put2(buf + 6, 20);
        put2(buf + 8, ZIP_GPBF_ENCODING_UTF_8);
        put2(buf + 28, 1);   // filename length
        buf[46] = 0xFF;      // invalid single-byte UTF-8
        const unsigned char* p = buf;
        zip_uint64_t left = sizeof(buf);
        zip_dirent de;
        _zip_dirent_init(&de);
        TEST_ASSERT(_zip_dirent_read(&de, nullptr, &p, &left, 0, &zerr) < 0,
                    "_zip_dirent_read should fail for invalid UTF-8 with UTF-8 flag");
        _zip_dirent_finalize(&de);
    }

    // ZIP64 required but no ZIP64 extra field.
    {
        unsigned char hdr[46] = {};
        memcpy(hdr, CENTRAL_MAGIC, 4);
        put2(hdr + 4, 45);
        put2(hdr + 6, 45);
        put4(hdr + 20, ZIP_UINT32_MAX); // compressed size
        put4(hdr + 24, ZIP_UINT32_MAX); // uncompressed size
        put4(hdr + 42, ZIP_UINT32_MAX); // offset
        const unsigned char* p = hdr;
        zip_uint64_t left = sizeof(hdr);
        zip_dirent de;
        _zip_dirent_init(&de);
        TEST_ASSERT(_zip_dirent_read(&de, nullptr, &p, &left, 0, &zerr) < 0,
                    "_zip_dirent_read should fail when ZIP64 sizes are present without ZIP64 EF");
        _zip_dirent_finalize(&de);
    }

    _zip_error_fini(&zerr);
}
REGISTER_TEST(test_external_libzip_extra_field_api_paths);
