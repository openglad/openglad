#include <cstdio>
#include <string>
#include <vector>

#include <yaml.h>

#include "test_framework.h"

static bool scan_yaml_tokens(const std::string& input, int* out_tokens)
{
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
        return false;

    yaml_parser_set_input_string(
        &parser,
        reinterpret_cast<const unsigned char*>(input.data()),
        input.size());

    int tokens = 0;
    bool ok = true;
    while (ok)
    {
        yaml_token_t tok;
        if (!yaml_parser_scan(&parser, &tok))
        {
            ok = false;
            break;
        }
        tokens++;
        if (tok.type == YAML_STREAM_END_TOKEN)
        {
            yaml_token_delete(&tok);
            break;
        }
        yaml_token_delete(&tok);
    }

    yaml_parser_delete(&parser);
    if (out_tokens)
        *out_tokens = tokens;
    return ok;
}

static bool emit_yaml_with_options(std::string* out_yaml)
{
    yaml_emitter_t emitter;
    if (!yaml_emitter_initialize(&emitter))
        return false;

    // Exercise option setters (affects emitter codepaths).
    yaml_emitter_set_canonical(&emitter, 1);
    yaml_emitter_set_indent(&emitter, 3);
    yaml_emitter_set_width(&emitter, 50);
    yaml_emitter_set_unicode(&emitter, 1);
    yaml_emitter_set_break(&emitter, YAML_LN_BREAK);

    std::vector<unsigned char> out(8192);
    size_t written = 0;
    yaml_emitter_set_output_string(&emitter, out.data(), out.size(), &written);

    yaml_event_t event;

    if (!yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING) ||
        !yaml_emitter_emit(&emitter, &event))
        goto fail;

    if (!yaml_document_start_event_initialize(&event, nullptr, nullptr, nullptr, 0) ||
        !yaml_emitter_emit(&emitter, &event))
        goto fail;

    if (!yaml_sequence_start_event_initialize(&event, nullptr, nullptr, 1, YAML_BLOCK_SEQUENCE_STYLE) ||
        !yaml_emitter_emit(&emitter, &event))
        goto fail;

    // A few scalar styles.
    if (!yaml_scalar_event_initialize(&event, nullptr, nullptr,
                                      (yaml_char_t*)"!tag",
                                      -1, 1, 1, YAML_PLAIN_SCALAR_STYLE) ||
        !yaml_emitter_emit(&emitter, &event))
        goto fail;

    if (!yaml_scalar_event_initialize(&event, nullptr, nullptr,
                                      (yaml_char_t*)"\"quoted\"",
                                      -1, 1, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE) ||
        !yaml_emitter_emit(&emitter, &event))
        goto fail;

    if (!yaml_scalar_event_initialize(&event, nullptr, nullptr,
                                      (yaml_char_t*)"multi\nline\nvalue\n",
                                      -1, 1, 1, YAML_LITERAL_SCALAR_STYLE) ||
        !yaml_emitter_emit(&emitter, &event))
        goto fail;

    if (!yaml_sequence_end_event_initialize(&event) ||
        !yaml_emitter_emit(&emitter, &event))
        goto fail;

    if (!yaml_document_end_event_initialize(&event, 0) ||
        !yaml_emitter_emit(&emitter, &event))
        goto fail;

    if (!yaml_stream_end_event_initialize(&event) ||
        !yaml_emitter_emit(&emitter, &event))
        goto fail;

    yaml_emitter_delete(&emitter);

    if (written > out.size())
        written = out.size();
    if (out_yaml)
        *out_yaml = std::string(reinterpret_cast<const char*>(out.data()), written);
    return true;

fail:
    yaml_emitter_delete(&emitter);
    return false;
}

TEST(ExternalYamlMore, external_yaml_scanner_tokens_variety)
{
    const std::string input =
        "%YAML 1.2\n"
        "%TAG !e! tag:example.com,2000:\n"
        "---\n"
        "# comment\n"
        "plain: value\n"
        "quoted: \"v:1\"\n"
        "single: 'it''s ok'\n"
        "folded: >\n"
        "  a\n"
        "  b\n"
        "literal: |\n"
        "  x\n"
        "  y\n"
        "flow: {k1: v1, k2: [1, 2, 3]}\n"
        "anchor: &A {n: 1}\n"
        "alias: *A\n"
        "tagged: !e!Thing {x: 1, y: 2}\n"
        "unicode: \"Snowman: \\u2603\"\n"
        "...\n";

    int tokens = 0;
    ASSERT_TRUE(scan_yaml_tokens(input, &tokens)) << "scanner should succeed for valid yaml";
    ASSERT_TRUE(tokens > 0) << "scanner should produce tokens";

    const std::vector<std::string> corpus = {
        // Explicit keys, nested collections, anchors.
        "?\n"
        "  {k1: v1, k2: [1, 2]}\n"
        ":\n"
        "  - &A one\n"
        "  - *A\n",

        // Multi-doc with directives and tags.
        "%YAML 1.1\n"
        "%TAG !e! tag:example.com,2000:\n"
        "---\n"
        "obj: !e!Thing {x: 7, y: 8}\n"
        "...\n"
        "---\n"
        "plain: text\n"
        "...\n",

        // Scalars that stress quoting/escaping.
        "single: 'it''s still fine'\n"
        "double: \"line\\nwith\\tescapes and unicode \\u03A9\"\n"
        "plain: :starts-with-colon-is-tokenized\n"
    };

    for (const auto& doc : corpus) {
        int n = 0;
        ASSERT_TRUE(scan_yaml_tokens(doc, &n)) << "scanner corpus document should succeed";
        ASSERT_TRUE(n > 0) << "scanner corpus should produce tokens";
    }
}


TEST(ExternalYamlMore, external_yaml_scanner_error_unclosed_quote)
{
    const std::string input = "a: \"unterminated\n";
    int tokens = 0;
    ASSERT_TRUE(!scan_yaml_tokens(input, &tokens)) << "scanner should fail on unterminated quote";

    const std::vector<std::string> bad = {
        "a:\n\t- badtab\n",          // illegal tab indentation
        "flow: [1, 2, 3\n",          // unclosed flow sequence
        "map: {a: 1, b: [2, 3}\n",   // mismatched delimiters
        "alias: *\n"                 // malformed alias token
    };
    for (const auto& doc : bad) {
        int n = 0;
        (void)scan_yaml_tokens(doc, &n);
    }
}


TEST(ExternalYamlMore, external_yaml_emitter_options_and_output)
{
    std::string out;
    ASSERT_TRUE(emit_yaml_with_options(&out)) << "emitter should succeed with options";
    ASSERT_TRUE(!out.empty()) << "emitter should write output";

    // Basic sanity: output should still be scannable.
    int tokens = 0;
    ASSERT_TRUE(scan_yaml_tokens(out, &tokens)) << "emitted yaml should be scannable";
    ASSERT_TRUE(tokens > 0) << "emitted yaml should produce tokens";
}

