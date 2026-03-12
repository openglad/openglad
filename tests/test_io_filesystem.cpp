#include <openglad/resources/io.h>
#include "test_framework.h"

#include <SDL.h>
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
    // Default campaigns are bundled. We expect at least one and that the
    // default campaign id is present.
    std::list<std::string> campaigns = list_campaigns();
    ASSERT_TRUE(!campaigns.empty()) << "list_campaigns should return at least one campaign";

    bool found_default = false;
    for (const auto& id : campaigns)
    {
        if (id == "org.openglad.gladiator")
            found_default = true;
    }
    ASSERT_TRUE(found_default) << "list_campaigns should include org.openglad.gladiator";

    std::vector<int> levels = list_levels_v();
    ASSERT_TRUE(!levels.empty()) << "list_levels_v should return at least one level";
}

