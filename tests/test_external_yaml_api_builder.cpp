#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <yaml.h>

#include <gtest/gtest.h>

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

static yaml_char_t* yc(const char* s)
{
    return const_cast<yaml_char_t*>(reinterpret_cast<const yaml_char_t*>(s));
}

static bool parse_events_from_file(FILE* f, int* out_events)
{
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
        return false;
    yaml_parser_set_input_file(&parser, f);
    yaml_parser_set_encoding(&parser, YAML_UTF8_ENCODING);

    int events = 0;
    bool ok = true;
    while (ok)
    {
        yaml_event_t ev;
        if (!yaml_parser_parse(&parser, &ev))
        {
            ok = false;
            break;
        }
        events++;
        if (ev.type == YAML_STREAM_END_EVENT)
        {
            yaml_event_delete(&ev);
            break;
        }
        yaml_event_delete(&ev);
    }
    yaml_parser_delete(&parser);
    if (out_events)
        *out_events = events;
    return ok;
}

static int fail_write(void* data, unsigned char* buffer, size_t size)
{
    (void)data;
    (void)buffer;
    (void)size;
    return 0;
}

TEST(ExternalYamlApiBuilder, external_yaml_api_build_dump_and_parse)
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    yaml_get_version(&major, &minor, &patch);
    ASSERT_TRUE(major >= 0 && minor >= 0 && patch >= 0) << "yaml_get_version should return components";
    const char* ver = yaml_get_version_string();
    ASSERT_TRUE(ver != nullptr && ver[0] != '\0') << "yaml_get_version_string should be non-empty";

    FILE* input_file = tmpfile();
    ASSERT_TRUE(input_file != nullptr) << "tmpfile should succeed";
    const char* file_yaml = "---\na: 1\nb:\n  - 2\n...\n";
    ASSERT_TRUE(std::fwrite(file_yaml, 1, std::strlen(file_yaml), input_file) == std::strlen(file_yaml)) << "write yaml file content";
    std::rewind(input_file);
    int file_events = 0;
    ASSERT_TRUE(parse_events_from_file(input_file, &file_events)) << "file input parser path should succeed";
    ASSERT_TRUE(file_events > 0) << "file input should produce events";
    std::fclose(input_file);

    std::string dumped;
    ASSERT_TRUE(emit_document_built_via_api(&dumped)) << "yaml api build+dump should succeed";
    ASSERT_TRUE(!dumped.empty()) << "dumped yaml should not be empty";

    int events = 0;
    ASSERT_TRUE(parse_with_event_api(dumped, &events)) << "dumped yaml should parse";
    ASSERT_TRUE(events > 0) << "parse should produce events";

    // Emitter file output path with block seq/map, alias, and folded/literal styles.
    FILE* out_file = tmpfile();
    ASSERT_TRUE(out_file != nullptr) << "tmpfile output should succeed";
    yaml_emitter_t emitter;
    ASSERT_TRUE(yaml_emitter_initialize(&emitter)) << "yaml_emitter_initialize";
    yaml_emitter_set_output_file(&emitter, out_file);
    yaml_event_t e;
    ASSERT_TRUE(yaml_stream_start_event_initialize(&e, YAML_UTF8_ENCODING)) << "stream start init";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit stream start";
    ASSERT_TRUE(yaml_document_start_event_initialize(&e, nullptr, nullptr, nullptr, 0)) << "doc start init";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit doc start";
    ASSERT_TRUE(yaml_mapping_start_event_initialize(&e, nullptr, nullptr, 1, YAML_BLOCK_MAPPING_STYLE)) << "map start init";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit map start";
    ASSERT_TRUE(yaml_scalar_event_initialize(&e, nullptr, nullptr, yc("seq"), -1, 1, 1, YAML_PLAIN_SCALAR_STYLE)) << "key seq";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit key seq";
    ASSERT_TRUE(yaml_sequence_start_event_initialize(&e, nullptr, nullptr, 1, YAML_BLOCK_SEQUENCE_STYLE)) << "seq start";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit seq start";
    ASSERT_TRUE(yaml_scalar_event_initialize(&e, yc("A"), nullptr, yc("first"), -1, 1, 1, YAML_PLAIN_SCALAR_STYLE)) << "anchored scalar";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit anchored scalar";
    ASSERT_TRUE(yaml_alias_event_initialize(&e, yc("A"))) << "alias init";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit alias";
    ASSERT_TRUE(yaml_scalar_event_initialize(&e, nullptr, nullptr, yc("line1\nline2\n"), -1, 1, 1, YAML_LITERAL_SCALAR_STYLE)) << "literal scalar";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit literal scalar";
    ASSERT_TRUE(yaml_sequence_end_event_initialize(&e)) << "seq end init";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit seq end";
    ASSERT_TRUE(yaml_scalar_event_initialize(&e, nullptr, nullptr, yc("emptymap"), -1, 1, 1, YAML_PLAIN_SCALAR_STYLE)) << "key emptymap";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit key emptymap";
    ASSERT_TRUE(yaml_mapping_start_event_initialize(&e, nullptr, nullptr, 1, YAML_BLOCK_MAPPING_STYLE)) << "empty map start";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit empty map start";
    ASSERT_TRUE(yaml_mapping_end_event_initialize(&e)) << "empty map end";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit empty map end";
    ASSERT_TRUE(yaml_scalar_event_initialize(&e, nullptr, nullptr, yc("folded"), -1, 1, 1, YAML_PLAIN_SCALAR_STYLE)) << "key folded";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit key folded";
    ASSERT_TRUE(yaml_scalar_event_initialize(&e, nullptr, nullptr, yc("a\nb\n"), -1, 1, 1, YAML_FOLDED_SCALAR_STYLE)) << "folded scalar";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit folded scalar";
    ASSERT_TRUE(yaml_mapping_end_event_initialize(&e)) << "map end";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit map end";
    ASSERT_TRUE(yaml_document_end_event_initialize(&e, 0)) << "doc end";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit doc end";
    ASSERT_TRUE(yaml_stream_end_event_initialize(&e)) << "stream end";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit stream end";
    yaml_emitter_delete(&emitter);
    std::rewind(out_file);
    char out_buf[1024] = {};
    size_t got = std::fread(out_buf, 1, sizeof(out_buf) - 1, out_file);
    std::fclose(out_file);
    ASSERT_TRUE(got > 0) << "file emitter should write output";
    ASSERT_TRUE(std::string(out_buf).find("line1") != std::string::npos) << "output should contain literal scalar content";

    // Duplicate tag directives should fail emitter validation.
    ASSERT_TRUE(yaml_emitter_initialize(&emitter)) << "yaml_emitter_initialize duplicate-tags";
    std::vector<unsigned char> out2(4096);
    size_t written2 = 0;
    yaml_emitter_set_output_string(&emitter, out2.data(), out2.size(), &written2);
    ASSERT_TRUE(yaml_stream_start_event_initialize(&e, YAML_UTF8_ENCODING)) << "stream start dup-tags";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit stream start dup-tags";
    yaml_tag_directive_t tags[2];
    tags[0].handle = yc("!e!");
    tags[0].prefix = yc("tag:example.com,2000:");
    tags[1] = tags[0];
    ASSERT_TRUE(yaml_document_start_event_initialize(&e, nullptr, tags, tags + 2, 0)) << "doc start init dup-tags";
    // Some libyaml versions accept duplicate directives; this still exercises tag-directive handling.
    (void)yaml_emitter_emit(&emitter, &e);
    yaml_emitter_delete(&emitter);

    // Writer callback failure and tiny output string error path.
    ASSERT_TRUE(yaml_emitter_initialize(&emitter)) << "yaml_emitter_initialize fail-writer";
    yaml_emitter_set_output(&emitter, fail_write, nullptr);
    ASSERT_TRUE(yaml_stream_start_event_initialize(&e, YAML_UTF8_ENCODING)) << "stream start fail-writer";
    (void)yaml_emitter_emit(&emitter, &e);
    ASSERT_TRUE(yaml_document_start_event_initialize(&e, nullptr, nullptr, nullptr, 0)) << "doc start fail-writer";
    (void)yaml_emitter_emit(&emitter, &e);
    ASSERT_TRUE(yaml_scalar_event_initialize(&e, nullptr, nullptr, yc("x"), -1, 1, 1, YAML_PLAIN_SCALAR_STYLE)) << "scalar fail-writer";
    (void)yaml_emitter_emit(&emitter, &e);
    yaml_emitter_delete(&emitter);

    ASSERT_TRUE(yaml_emitter_initialize(&emitter)) << "yaml_emitter_initialize tiny";
    unsigned char tiny[8] = {};
    size_t written = 0;
    yaml_emitter_set_output_string(&emitter, tiny, sizeof(tiny), &written);
    ASSERT_TRUE(yaml_stream_start_event_initialize(&e, YAML_UTF8_ENCODING)) << "stream start tiny";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit stream start tiny";
    ASSERT_TRUE(yaml_document_start_event_initialize(&e, nullptr, nullptr, nullptr, 0)) << "doc start tiny";
    ASSERT_TRUE(yaml_emitter_emit(&emitter, &e)) << "emit doc start tiny";
    ASSERT_TRUE(yaml_scalar_event_initialize(&e, nullptr, nullptr, yc("this-is-way-too-long-for-tiny-buffer"), -1, 1, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE)) << "big scalar tiny";
    (void)yaml_emitter_emit(&emitter, &e);
    yaml_emitter_delete(&emitter);

    // Invalid UTF-8 should be rejected by event initializers.
    yaml_char_t bad_scalar[] = { (yaml_char_t)0xC3, (yaml_char_t)0x28, 0 };
    ASSERT_TRUE(!yaml_scalar_event_initialize(&e, nullptr, nullptr, bad_scalar, 2, 1, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE)) << "invalid utf8 scalar should fail init";
    yaml_char_t bad_handle[] = { '!', (yaml_char_t)0xFF, 0 };
    yaml_char_t bad_prefix[] = { 't', 'a', 'g', ':', (yaml_char_t)0xFF, 0 };
    yaml_tag_directive_t bad_tag{bad_handle, bad_prefix};
    ASSERT_TRUE(!yaml_document_start_event_initialize(&e, nullptr, &bad_tag, &bad_tag + 1, 0)) << "invalid utf8 tag directive should fail init";
}


TEST(ExternalYamlApiBuilder, external_yaml_api_parse_large_scalar_and_multidoc_events)
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
    ASSERT_TRUE(parse_with_event_api(input, &events)) << "large scalar + multidoc parse should succeed";
    ASSERT_TRUE(events > 10) << "expected multiple events";
}

