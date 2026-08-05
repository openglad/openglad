#include <openglad/resources/io.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

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
    SDL_IOStream* rw = SDL_IOFromFile(file.string().c_str(), "rb");
    if (!rw)
        return false;
    char buf[3] = {};
    size_t got = SDL_ReadIO(rw, buf, 3);
    SDL_CloseIO(rw);
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
    SDL_IOStream* rw = SDL_IOFromFile(file.string().c_str(), "rb");
    if (!rw)
        return false;
    if (SDL_SeekIO(rw, 3, SDL_IO_SEEK_SET) < 0)
    {
        SDL_CloseIO(rw);
        return false;
    }
    unsigned char version = 0;
    const size_t got = SDL_ReadIO(rw, &version, 1);
    SDL_CloseIO(rw);
    if (got != 1)
        return false;
    *out = version;
    return true;
}
} // namespace

class IoNewFileFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        setup_new_file_fixture();
    }

    void TearDown() override
    {
        teardown_new_file_fixture();
    }
};

TEST_F(IoNewFileFixture, io_new_file_helpers_typed_success_paths)
{
    const auto pix_file = g_fixture.base / "new.png";
    const auto map_file = g_fixture.base / "map.png";
    const auto campaign_file = g_fixture.base / "campaign.yaml";
    const auto scen_file = g_fixture.base / "scen1.fss";

    ASSERT_EQ(static_cast<int>(NewFileIoError::None), static_cast<int>(create_new_pix_with_error(pix_file.string(), 4, 3, 11))) << "create_new_pix_with_error should succeed";
    ASSERT_EQ(static_cast<int>(NewFileIoError::None), static_cast<int>(create_new_map_pix_with_error(map_file.string(), 4, 3))) << "create_new_map_pix_with_error should succeed";
    ASSERT_EQ(static_cast<int>(NewFileIoError::None), static_cast<int>(create_new_campaign_descriptor_with_error(campaign_file.string()))) << "create_new_campaign_descriptor_with_error should succeed";
    ASSERT_EQ(static_cast<int>(NewFileIoError::None), static_cast<int>(create_new_scen_file_with_error(scen_file.string(), "scen0001"))) << "create_new_scen_file_with_error should succeed";

    ASSERT_TRUE(std::filesystem::exists(pix_file)) << "new.png should exist";
    ASSERT_TRUE(std::filesystem::exists(map_file)) << "map.png should exist";
    ASSERT_TRUE(std::filesystem::exists(campaign_file)) << "campaign.yaml should exist";
    ASSERT_TRUE(std::filesystem::exists(scen_file)) << "scen1.fss should exist";

    std::string header;
    ASSERT_TRUE(read_header(scen_file, &header)) << "scen file header should be readable";
    ASSERT_TRUE(header == "FSS") << "scenario header should be FSS";

    unsigned char version = 0;
    ASSERT_TRUE(read_version(scen_file, &version)) << "scen file version should be readable";
    ASSERT_EQ(9, (int)version) << "new scenario file should use centralized v9 serializer";
}

TEST_F(IoNewFileFixture, io_new_file_helpers_typed_invalid_dimensions)
{
    const auto invalid_file = g_fixture.base / "invalid.png";
    ASSERT_EQ(static_cast<int>(NewFileIoError::InvalidDimensions), static_cast<int>(create_new_pix_with_error(invalid_file.string(), 0, 5, 1))) << "zero width should return InvalidDimensions";
    ASSERT_EQ(static_cast<int>(NewFileIoError::InvalidDimensions), static_cast<int>(create_new_map_pix_with_error(invalid_file.string(), 5, 0))) << "zero height should return InvalidDimensions";
    ASSERT_TRUE(!std::filesystem::exists(invalid_file)) << "invalid create should not write output file";
}
