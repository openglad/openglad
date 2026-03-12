#include <openglad/resources/io.h>
#include <openglad/resources/io_common.h>
#include <gtest/gtest.h>

#include <SDL.h>
#include <filesystem>
#include <cstring>
#include <string>

static void write_all(SDL_RWops* rw, const void* data, size_t len)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    size_t total = 0;
    while (total < len)
    {
        size_t wrote = SDL_RWwrite(rw, p + total, 1, len - total);
        if (wrote == 0)
            break;
        total += wrote;
    }
}

TEST(IoFilesystem, io_user_path_and_rwops_roundtrip)
{
    std::string user = get_user_path();
    ASSERT_TRUE(!user.empty()) << "get_user_path should return a non-empty path";

    const char* filename = "codex_io_rwops_test.txt";
    SDL_RWops* out = open_write_file(filename);
    ASSERT_TRUE(out != nullptr) << "open_write_file should succeed";

    const char payload[] = "hello-openglad-io";
    write_all(out, payload, strlen(payload));
    SDL_RWclose(out);

    SDL_RWops* in = open_read_file(filename);
    ASSERT_TRUE(in != nullptr) << "open_read_file should succeed";

    char buf[64];
    memset(buf, 0, sizeof(buf));
    size_t n = SDL_RWread(in, buf, 1, sizeof(buf) - 1);
    SDL_RWclose(in);

    ASSERT_TRUE(n == strlen(payload)) << "read back should match payload length";
    ASSERT_STREQ(payload, buf) << "read back should match payload contents";
}


TEST(IoFilesystem, io_list_campaigns_and_levels)
{
    namespace fs = std::filesystem;
    const fs::path user = get_user_path();
    const fs::path campaign_path = user / "campaigns" / "codex_fixture_campaign.glad";
    const fs::path level_path = user / "scen" / "scen4242.fss";

    fs::create_directories(campaign_path.parent_path());
    fs::create_directories(level_path.parent_path());

    {
        FILE* campaign_file = fopen(campaign_path.string().c_str(), "wb");
        ASSERT_TRUE(campaign_file != nullptr) << "campaign fixture file should be creatable";
        if (!campaign_file)
            return;
        fclose(campaign_file);
    }

    {
        FILE* level_file = fopen(level_path.string().c_str(), "wb");
        ASSERT_TRUE(level_file != nullptr) << "level fixture file should be creatable";
        if (!level_file)
            return;
        fclose(level_file);
    }

    std::list<std::string> campaigns = list_campaigns();
    ASSERT_TRUE(!campaigns.empty()) << "list_campaigns should return at least one campaign";

    bool found_fixture_campaign = false;
    for (const auto& id : campaigns)
    {
        if (id == "codex_fixture_campaign")
            found_fixture_campaign = true;
    }
    ASSERT_TRUE(found_fixture_campaign) << "list_campaigns should include the fixture campaign";

    std::vector<int> levels = list_levels_v();
    ASSERT_TRUE(!levels.empty()) << "list_levels_v should return at least one level";

    bool found_fixture_level = false;
    for (const int level : levels)
    {
        if (level == 4242)
            found_fixture_level = true;
    }
    ASSERT_TRUE(found_fixture_level) << "list_levels_v should include the fixture level";
}
