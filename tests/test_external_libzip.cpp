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

