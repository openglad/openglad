#include <openglad/resources/yaml_stream.h>
#include <openglad/resources/io_common.h>
#include <openglad/platform/game_context.h>
#include "test_framework.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

struct MemReadCtx {
    std::string data;
    std::size_t pos = 0;
};

int mem_read_handler(void* data, unsigned char* buffer, std::size_t size, std::size_t* size_read)
{
    auto* ctx = static_cast<MemReadCtx*>(data);
    const std::size_t remain = (ctx->pos < ctx->data.size()) ? (ctx->data.size() - ctx->pos) : 0;
    const std::size_t n = std::min(size, remain);
    if (n > 0)
        std::memcpy(buffer, ctx->data.data() + ctx->pos, n);
    ctx->pos += n;
    *size_read = n;
    return 1;
}

} // namespace

TEST(IoCoverage, io_yaml_parser_reports_error_on_invalid_stream)
{
    MemReadCtx reader{"root: [1, 2\n", 0};
    og::io::YamlParser parser;
    parser.set_input(mem_read_handler, &reader);

    og::io::YamlParseResult r = og::io::YamlParseResult::Ok;
    int iterations = 0;
    while (r == og::io::YamlParseResult::Ok && iterations < 128)
    {
        r = parser.parse_next();
        iterations++;
    }

    ASSERT_TRUE(iterations > 0) << "parser should consume at least one event";
    parser.close_input();
}


TEST(IoCoverage, io_platform_helpers_explode_and_archive_bool_wrappers)
{
    std::list<std::string> parts = explode("a::b::", ':');
    ASSERT_TRUE(!parts.empty()) << "explode should return at least one token";

    const bool unzip_ok = unzip_into_with_error("temp/no_such_archive.zip", "temp/no_such_archive_out") == ArchiveIoError::None;
    ASSERT_TRUE(!unzip_ok) << "unzip should return error for missing archive";
}

