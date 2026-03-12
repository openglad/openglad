#include <string>
#include <vector>

#include <yaml.h>

#include "test_framework.h"

static yaml_char_t* y(const char* s)
{
    return const_cast<yaml_char_t*>(reinterpret_cast<const yaml_char_t*>(s));
}

static bool parse_yaml_events(const std::string& input, int* out_events)
{
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
        return false;

    yaml_parser_set_input_string(
        &parser,
        reinterpret_cast<const unsigned char*>(input.data()),
        input.size());

    int events = 0;
    bool ok = true;
    while (ok) {
        yaml_event_t event;
        if (!yaml_parser_parse(&parser, &event)) {
            ok = false;
            break;
        }
        events++;
        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }
        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    if (out_events)
        *out_events = events;
    return ok;
}

static bool emit_yaml_document_with_variety(std::string* out_yaml)
{
    yaml_emitter_t emitter;
    if (!yaml_emitter_initialize(&emitter))
        return false;

    std::vector<unsigned char> out(4096);
    size_t written = 0;
    yaml_emitter_set_output_string(&emitter, out.data(), out.size(), &written);

    yaml_event_t event;

    if (!yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_document_start_event_initialize(&event, nullptr, nullptr, nullptr, 0) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_mapping_start_event_initialize(&event, nullptr, y("!tagged"), 1, YAML_BLOCK_MAPPING_STYLE) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    // key: "anchors"
    if (!yaml_scalar_event_initialize(&event, nullptr, nullptr,
                                      y("anchors"),
                                      -1, 1, 1, YAML_PLAIN_SCALAR_STYLE) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    // value: sequence with anchor and alias
    if (!yaml_sequence_start_event_initialize(&event, nullptr, nullptr, 1, YAML_FLOW_SEQUENCE_STYLE) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_scalar_event_initialize(&event,
                                      y("A"),
                                      nullptr,
                                      y("first"),
                                      -1, 1, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_alias_event_initialize(&event,
                                     y("A")) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_sequence_end_event_initialize(&event) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    // key: mapping
    if (!yaml_scalar_event_initialize(&event, nullptr, nullptr,
                                      y("mapping"),
                                      -1, 1, 1, YAML_PLAIN_SCALAR_STYLE) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_mapping_start_event_initialize(&event, nullptr, nullptr, 1, YAML_FLOW_MAPPING_STYLE) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_scalar_event_initialize(&event, nullptr, nullptr,
                                      y("n"),
                                      -1, 1, 1, YAML_PLAIN_SCALAR_STYLE) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_scalar_event_initialize(&event, nullptr, nullptr,
                                      y("123"),
                                      -1, 1, 1, YAML_PLAIN_SCALAR_STYLE) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_mapping_end_event_initialize(&event) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_mapping_end_event_initialize(&event) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_document_end_event_initialize(&event, 0) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    if (!yaml_stream_end_event_initialize(&event) ||
        !yaml_emitter_emit(&emitter, &event)) {
        yaml_emitter_delete(&emitter);
        return false;
    }

    yaml_emitter_delete(&emitter);

    if (written > out.size())
        written = out.size();
    if (out_yaml)
        *out_yaml = std::string(reinterpret_cast<const char*>(out.data()), written);
    return true;
}

TEST(ExternalYaml, parse_scalars_sequences_mappings)
{
    const std::string input =
        "a: 1\n"
        "b: [2, 3, 4]\n"
        "c:\n"
        "  - x\n"
        "  - y\n";
    int events = 0;
    ASSERT_TRUE(parse_yaml_events(input, &events)) << "parser should succeed";
    ASSERT_TRUE(events > 0) << "parser should produce events";

    const std::vector<std::string> corpus = {
        // Explicit complex key, nested flow/block mixes.
        "?\n"
        "  - k1\n"
        "  - k2\n"
        ":\n"
        "  nested: {a: [1, 2], b: {c: d}}\n",

        // Merge keys and aliases.
        "base: &B {x: 1, y: 2}\n"
        "obj:\n"
        "  <<: *B\n"
        "  z: 3\n",

        // Multiple documents with tags and folded/literal blocks.
        "%YAML 1.1\n"
        "---\n"
        "s: >\n"
        "  line1\n"
        "  line2\n"
        "l: |\n"
        "  a\n"
        "  b\n"
        "t: !!str hello\n"
        "...\n"
        "---\n"
        "arr:\n"
        "  - {k: v}\n"
        "  - [1, 2, 3]\n"
        "...\n",

        // Empty stream is valid and should parse.
        ""
    };

    for (const auto& doc : corpus) {
        int n = 0;
        ASSERT_TRUE(parse_yaml_events(doc, &n)) << "parser corpus document should succeed";
        ASSERT_TRUE(n > 0 || doc.empty()) << "parser corpus should produce events for non-empty docs";
    }
}


TEST(ExternalYaml, parse_anchors_aliases_tags)
{
    const std::string input =
        "defaults: &def {n: 1, s: \"str\"}\n"
        "use: *def\n"
        "tagged: !!str hello\n";
    int events = 0;
    ASSERT_TRUE(parse_yaml_events(input, &events)) << "parser should handle anchors/aliases/tags";
    ASSERT_TRUE(events > 0) << "parser should produce events";
}


TEST(ExternalYaml, parse_error_path)
{
    const std::string input = "a: [1, 2\n"; // missing closing bracket
    int events = 0;
    ASSERT_TRUE(!parse_yaml_events(input, &events)) << "invalid yaml should fail parse";

    const std::vector<std::string> bad = {
        "a:\n\t- tab-indented\n",                // illegal tab indentation
        "x: &A 1\nx2: *B\n",                     // unknown alias
        "---\n[1, 2, 3\n",                       // broken flow sequence
        "map: {a: 1, b: [2, 3}\n",               // mismatched delimiters
        "%YAML 9.9\n---\na: 1\n"                 // invalid version directive
    };
    for (const auto& doc : bad) {
        int n = 0;
        (void)parse_yaml_events(doc, &n);
    }
}


TEST(ExternalYaml, emit_and_reparse)
{
    std::string out;
    ASSERT_TRUE(emit_yaml_document_with_variety(&out)) << "emitter should succeed";
    ASSERT_TRUE(!out.empty()) << "emitter should produce output";

    // Re-parse emitted content to exercise both sides.
    int events = 0;
    ASSERT_TRUE(parse_yaml_events(out, &events)) << "re-parse emitted yaml should succeed";
    ASSERT_TRUE(events > 0) << "re-parse should produce events";
}

