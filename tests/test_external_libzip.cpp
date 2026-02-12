#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

    // Opening a 0-byte file with ZIP_CREATE|ZIP_TRUNCATE should succeed.
    const std::string empty_path = path + ".empty";
    {
        std::ofstream ofs(empty_path, std::ios::binary | std::ios::trunc);
        TEST_ASSERT(ofs.good(), "should create empty zip candidate file");
    }
    zip* z_empty = zip_open(empty_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    TEST_ASSERT(z_empty != nullptr, "zip_open with CREATE|TRUNCATE on empty file should succeed");
    TEST_ASSERT(zip_close(z_empty) == 0, "zip_close empty archive");

    // Nonexistent file without ZIP_CREATE should fail.
    const std::string missing_path = path + ".missing";
    zip* z_missing = zip_open(missing_path.c_str(), 0, &err);
    TEST_ASSERT(z_missing == nullptr, "zip_open without ZIP_CREATE on missing file should fail");

    // Existing file with garbage data should fail consistency checks.
    const std::string bad_path = path + ".bad";
    {
        std::ofstream ofs(bad_path, std::ios::binary | std::ios::trunc);
        const char garbage[] = "not-a-zip-central-directory";
        ofs.write(garbage, sizeof(garbage) - 1);
        TEST_ASSERT(ofs.good(), "should write garbage payload");
    }
    zip* z_bad = zip_open(bad_path.c_str(), ZIP_CHECKCONS, &err);
    TEST_ASSERT(z_bad == nullptr, "zip_open should reject malformed archive with CHECKCONS");
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
    TEST_ASSERT(zip_set_archive_flag(za, ZIP_AFL_WANT_TORRENTZIP, 1) == 0, "zip_set_archive_flag torrentzip");

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

    // No-op close path: open read/write and close without changes.
    zip* z_nochange = zip_open(path.c_str(), 0, &err);
    TEST_ASSERT(z_nochange != nullptr, "zip_open for no-change close");
    TEST_ASSERT(zip_close(z_nochange) == 0, "zip_close with unchanged archive should succeed");

    // survivors==0 path: close an empty newly created archive and ensure temp file is removed.
    const std::string empty_archive = path + ".zero";
    {
        std::ofstream ofs(empty_archive, std::ios::binary | std::ios::trunc);
        TEST_ASSERT(ofs.good(), "precreate empty archive file for truncate/remove path");
    }
    zip* z_zero = zip_open(empty_archive.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    TEST_ASSERT(z_zero != nullptr, "zip_open create empty archive");
    TEST_ASSERT(zip_close(z_zero) == 0, "zip_close empty archive should succeed");
    TEST_ASSERT(!std::filesystem::exists(empty_archive), "zip_close should remove empty created archive");
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

    // Internal dirent/cdir APIs that remain compatible in libzip 1.11.x.
    zip_error_t zerr;
    zip_error_init(&zerr);

    zip_dirent_t* de = _zip_dirent_new();
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

    TEST_ASSERT(_zip_dirent_needs_zip64(de, ZIP_FL_CENTRAL), "_zip_dirent_needs_zip64 central");
    TEST_ASSERT(_zip_dirent_needs_zip64(de, ZIP_FL_LOCAL), "_zip_dirent_needs_zip64 local");

    zip_dirent_torrentzip_normalize(de);

    zip_dirent_t* cloned = _zip_dirent_clone(de);
    TEST_ASSERT(cloned != nullptr, "_zip_dirent_clone");
    _zip_dirent_free(cloned);
    _zip_dirent_free(de);

    zip_cdir_t* cd = _zip_cdir_new(&zerr);
    TEST_ASSERT(cd != nullptr, "_zip_cdir_new");
    TEST_ASSERT(_zip_cdir_grow(cd, 3, &zerr), "_zip_cdir_grow");
    _zip_cdir_free(cd);

    zip_error_fini(&zerr);
}
REGISTER_TEST(test_external_libzip_extra_field_api_paths);
