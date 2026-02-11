#include <cstring>
#include <filesystem>
#include <string>

#include <unistd.h>

#include "zip.h"

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
}
REGISTER_TEST(test_external_libzip_extra_field_api_paths);
