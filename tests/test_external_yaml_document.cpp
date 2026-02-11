#include <string>
#include <vector>

#include <yaml.h>

#include "test_framework.h"

static bool load_yaml_documents(const std::string& input, int* out_docs)
{
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
        return false;

    yaml_parser_set_input_string(
        &parser,
        reinterpret_cast<const unsigned char*>(input.data()),
        input.size());

    int docs = 0;
    bool ok = true;

    while (ok)
    {
        yaml_document_t doc;
        if (!yaml_parser_load(&parser, &doc))
        {
            ok = false;
            break;
        }

        yaml_node_t* root = yaml_document_get_root_node(&doc);
        if (root == nullptr)
        {
            yaml_document_delete(&doc);
            break; // stream end
        }

        // Touch node traversal APIs.
        if (root->type == YAML_MAPPING_NODE)
        {
            for (yaml_node_pair_t* p = root->data.mapping.pairs.start;
                 p < root->data.mapping.pairs.top; ++p)
            {
                (void)yaml_document_get_node(&doc, p->key);
                (void)yaml_document_get_node(&doc, p->value);
            }
        }

        docs++;
        yaml_document_delete(&doc);
    }

    yaml_parser_delete(&parser);
    if (out_docs)
        *out_docs = docs;
    return ok;
}

static bool dump_yaml_document(const std::string& input, std::string* out)
{
    yaml_parser_t parser;
    yaml_emitter_t emitter;
    yaml_document_t doc;

    if (!yaml_parser_initialize(&parser))
        return false;
    if (!yaml_emitter_initialize(&emitter))
    {
        yaml_parser_delete(&parser);
        return false;
    }

    yaml_parser_set_input_string(
        &parser,
        reinterpret_cast<const unsigned char*>(input.data()),
        input.size());

    if (!yaml_parser_load(&parser, &doc))
    {
        yaml_emitter_delete(&emitter);
        yaml_parser_delete(&parser);
        return false;
    }

    std::vector<unsigned char> buf(16384);
    size_t written = 0;
    yaml_emitter_set_output_string(&emitter, buf.data(), buf.size(), &written);

    bool ok = yaml_emitter_dump(&emitter, &doc) != 0;

    yaml_emitter_delete(&emitter);
    yaml_parser_delete(&parser);

    if (!ok)
        return false;
    if (written > buf.size())
        written = buf.size();
    if (out)
        *out = std::string(reinterpret_cast<const char*>(buf.data()), written);
    return true;
}

void test_external_yaml_parser_load_multi_document_stream()
{
    const std::string input =
        "---\n"
        "a: 1\n"
        "b: [2, 3]\n"
        "...\n"
        "---\n"
        "c: {x: 9, y: 8}\n"
        "d: |\n"
        "  line1\n"
        "  line2\n"
        "...\n";

    int docs = 0;
    TEST_ASSERT(load_yaml_documents(input, &docs), "yaml_parser_load should succeed");
    TEST_ASSERT_EQ(2, docs, "should parse two documents");
}
REGISTER_TEST(test_external_yaml_parser_load_multi_document_stream);

void test_external_yaml_emitter_dump_and_reload()
{
    const std::string input =
        "root:\n"
        "  list: [1, 2, 3]\n"
        "  map: {k: v}\n";

    std::string dumped;
    TEST_ASSERT(dump_yaml_document(input, &dumped), "yaml_emitter_dump should succeed");
    TEST_ASSERT(!dumped.empty(), "dumped yaml should not be empty");

    int docs = 0;
    TEST_ASSERT(load_yaml_documents(dumped, &docs), "dumped yaml should parse");
    TEST_ASSERT_EQ(1, docs, "dumped yaml should contain one document");
}
REGISTER_TEST(test_external_yaml_emitter_dump_and_reload);

void test_external_yaml_parser_load_error_on_invalid_input()
{
    const std::string bad =
        "---\n"
        "x: [1, 2\n"; // missing closing ]

    int docs = 0;
    TEST_ASSERT(!load_yaml_documents(bad, &docs), "invalid yaml should fail load");
}
REGISTER_TEST(test_external_yaml_parser_load_error_on_invalid_input);

