#include <openglad/platform/io.h>
#include "test_framework.h"

#include <filesystem>
#include <string>

#include <unistd.h>

namespace {
struct NewFileFixture {
    std::filesystem::path base;
};

NewFileFixture g_fixture;

void setup_new_file_fixture()
{
    g_fixture.base = std::filesystem::temp_directory_path() /
        ("openglad_newfile_" + std::to_string(::getpid()));
    create_dir(g_fixture.base.string());
}

void teardown_new_file_fixture()
{
    std::error_code ec;
    std::filesystem::remove_all(g_fixture.base, ec);
}

bool read_header(const std::filesystem::path& file, std::string* out)
{
    SDL_RWops* rw = SDL_RWFromFile(file.string().c_str(), "rb");
    if (!rw)
        return false;
    char buf[3] = {};
    size_t got = SDL_RWread(rw, buf, 1, 3);
    SDL_RWclose(rw);
    if (got != 3)
        return false;
    if (out)
        *out = std::string(buf, 3);
    return true;
}

bool read_version(const std::filesystem::path& file, unsigned char* out)
{
    if (out == nullptr)
        return false;
    SDL_RWops* rw = SDL_RWFromFile(file.string().c_str(), "rb");
    if (!rw)
        return false;
    if (SDL_RWseek(rw, 3, RW_SEEK_SET) < 0)
    {
        SDL_RWclose(rw);
        return false;
    }
    unsigned char version = 0;
    const size_t got = SDL_RWread(rw, &version, 1, 1);
    SDL_RWclose(rw);
    if (got != 1)
        return false;
    *out = version;
    return true;
}
} // namespace

void test_io_new_file_helpers_typed_success_paths()
{
    const auto pix_file = g_fixture.base / "new.pix";
    const auto map_file = g_fixture.base / "map.pix";
    const auto campaign_file = g_fixture.base / "campaign.yaml";
    const auto scen_file = g_fixture.base / "scen1.fss";

    TEST_ASSERT_EQ(static_cast<int>(NewFileIoError::None),
        static_cast<int>(create_new_pix_with_error(pix_file.string(), 4, 3, 11)),
        "create_new_pix_with_error should succeed");
    TEST_ASSERT_EQ(static_cast<int>(NewFileIoError::None),
        static_cast<int>(create_new_map_pix_with_error(map_file.string(), 4, 3)),
        "create_new_map_pix_with_error should succeed");
    TEST_ASSERT_EQ(static_cast<int>(NewFileIoError::None),
        static_cast<int>(create_new_campaign_descriptor_with_error(campaign_file.string())),
        "create_new_campaign_descriptor_with_error should succeed");
    TEST_ASSERT_EQ(static_cast<int>(NewFileIoError::None),
        static_cast<int>(create_new_scen_file_with_error(scen_file.string(), "scen0001")),
        "create_new_scen_file_with_error should succeed");

    TEST_ASSERT(std::filesystem::exists(pix_file), "new.pix should exist");
    TEST_ASSERT(std::filesystem::exists(map_file), "map.pix should exist");
    TEST_ASSERT(std::filesystem::exists(campaign_file), "campaign.yaml should exist");
    TEST_ASSERT(std::filesystem::exists(scen_file), "scen1.fss should exist");

    std::string header;
    TEST_ASSERT(read_header(scen_file, &header), "scen file header should be readable");
    TEST_ASSERT(header == "FSS", "scenario header should be FSS");

    unsigned char version = 0;
    TEST_ASSERT(read_version(scen_file, &version), "scen file version should be readable");
    TEST_ASSERT_EQ(9, (int)version, "new scenario file should use centralized v9 serializer");
}
REGISTER_TEST_WITH_FIXTURE(test_io_new_file_helpers_typed_success_paths,
    setup_new_file_fixture, teardown_new_file_fixture);

void test_io_new_file_helpers_typed_invalid_dimensions()
{
    const auto invalid_file = g_fixture.base / "invalid.pix";
    TEST_ASSERT_EQ(static_cast<int>(NewFileIoError::InvalidDimensions),
        static_cast<int>(create_new_pix_with_error(invalid_file.string(), 0, 5, 1)),
        "zero width should return InvalidDimensions");
    TEST_ASSERT_EQ(static_cast<int>(NewFileIoError::InvalidDimensions),
        static_cast<int>(create_new_map_pix_with_error(invalid_file.string(), 5, 0)),
        "zero height should return InvalidDimensions");
    TEST_ASSERT(!std::filesystem::exists(invalid_file),
        "invalid create should not write output file");
}
REGISTER_TEST_WITH_FIXTURE(test_io_new_file_helpers_typed_invalid_dimensions,
    setup_new_file_fixture, teardown_new_file_fixture);
