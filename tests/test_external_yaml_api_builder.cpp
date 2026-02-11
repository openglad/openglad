#include <cstdio>
#include <string>
#include <vector>

#include <yaml.h>

#include "test_framework.h"

static bool emit_document_built_via_api(std::string* out_yaml)
{
    yaml_document_t doc;
    yaml_version_directive_t ver{};
    ver.major = 1;
    ver.minor = 1;

    yaml_tag_directive_t tags[1];
    tags[0].handle = (yaml_char_t*)"!e!";
    tags[0].prefix = (yaml_char_t*)"tag:example.com,2000:";

    if (!yaml_document_initialize(&doc, &ver, tags, tags + 1, 1, 1))
        return false;

    int root_map = yaml_document_add_mapping(&doc, nullptr, YAML_BLOCK_MAPPING_STYLE);
    if (root_map <= 0)
    {
        yaml_document_delete(&doc);
        return false;
    }

    int key_name = yaml_document_add_scalar(&doc, nullptr, (yaml_char_t*)"name", -1, YAML_PLAIN_SCALAR_STYLE);
    int val_name = yaml_document_add_scalar(&doc, nullptr, (yaml_char_t*)"builder", -1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
    if (key_name <= 0 || val_name <= 0)
    {
        yaml_document_delete(&doc);
        return false;
    }
    if (!yaml_document_append_mapping_pair(&doc, root_map, key_name, val_name))
    {
        yaml_document_delete(&doc);
        return false;
    }

    int key_list = yaml_document_add_scalar(&doc, nullptr, (yaml_char_t*)"items", -1, YAML_PLAIN_SCALAR_STYLE);
    int seq = yaml_document_add_sequence(&doc, nullptr, YAML_FLOW_SEQUENCE_STYLE);
    if (key_list <= 0 || seq <= 0)
    {
        yaml_document_delete(&doc);
        return false;
    }
    for (int i = 0; i < 5; i++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "v%d", i);
        int item = yaml_document_add_scalar(&doc, nullptr, (yaml_char_t*)buf, -1, YAML_PLAIN_SCALAR_STYLE);
        if (item <= 0 || !yaml_document_append_sequence_item(&doc, seq, item))
        {
            yaml_document_delete(&doc);
            return false;
        }
    }
    if (!yaml_document_append_mapping_pair(&doc, root_map, key_list, seq))
    {
        yaml_document_delete(&doc);
        return false;
    }

    int key_map = yaml_document_add_scalar(&doc, nullptr, (yaml_char_t*)"nested", -1, YAML_PLAIN_SCALAR_STYLE);
    int nested = yaml_document_add_mapping(&doc, (yaml_char_t*)"!e!Thing", YAML_FLOW_MAPPING_STYLE);
    int nk = yaml_document_add_scalar(&doc, nullptr, (yaml_char_t*)"k", -1, YAML_PLAIN_SCALAR_STYLE);
    int nv = yaml_document_add_scalar(&doc, nullptr, (yaml_char_t*)"123", -1, YAML_PLAIN_SCALAR_STYLE);
    if (key_map <= 0 || nested <= 0 || nk <= 0 || nv <= 0 ||
        !yaml_document_append_mapping_pair(&doc, nested, nk, nv) ||
        !yaml_document_append_mapping_pair(&doc, root_map, key_map, nested))
    {
        yaml_document_delete(&doc);
        return false;
    }

    yaml_emitter_t emitter;
    if (!yaml_emitter_initialize(&emitter))
    {
        yaml_document_delete(&doc);
        return false;
    }
    std::vector<unsigned char> out(16384);
    size_t written = 0;
    yaml_emitter_set_output_string(&emitter, out.data(), out.size(), &written);

    const bool ok = yaml_emitter_dump(&emitter, &doc) != 0;
    yaml_emitter_delete(&emitter);
    if (!ok)
    {
        yaml_document_delete(&doc);
        return false;
    }

    if (written > out.size())
        written = out.size();
    if (out_yaml)
        *out_yaml = std::string(reinterpret_cast<const char*>(out.data()), written);
    return true;
}

static bool parse_with_event_api(const std::string& input, int* out_events)
{
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
        return false;
    yaml_parser_set_input_string(&parser, reinterpret_cast<const unsigned char*>(input.data()), input.size());

    int events = 0;
    bool ok = true;
    while (ok)
    {
        yaml_event_t e;
        if (!yaml_parser_parse(&parser, &e))
        {
            ok = false;
            break;
        }
        events++;
        if (e.type == YAML_STREAM_END_EVENT)
        {
            yaml_event_delete(&e);
            break;
        }
        yaml_event_delete(&e);
    }
    yaml_parser_delete(&parser);
    if (out_events)
        *out_events = events;
    return ok;
}

void test_external_yaml_api_build_dump_and_parse()
{
    std::string dumped;
    TEST_ASSERT(emit_document_built_via_api(&dumped), "yaml api build+dump should succeed");
    TEST_ASSERT(!dumped.empty(), "dumped yaml should not be empty");

    int events = 0;
    TEST_ASSERT(parse_with_event_api(dumped, &events), "dumped yaml should parse");
    TEST_ASSERT(events > 0, "parse should produce events");
}
REGISTER_TEST(test_external_yaml_api_build_dump_and_parse);

void test_external_yaml_api_parse_large_scalar_and_multidoc_events()
{
    std::string big(2048, 'x');
    std::string input =
        "---\n"
        "a: " + big + "\n"
        "...\n"
        "---\n"
        "b:\n"
        "  - 1\n"
        "  - 2\n"
        "  - 3\n"
        "...\n";
    int events = 0;
    TEST_ASSERT(parse_with_event_api(input, &events), "large scalar + multidoc parse should succeed");
    TEST_ASSERT(events > 10, "expected multiple events");
}
REGISTER_TEST(test_external_yaml_api_parse_large_scalar_and_multidoc_events);
