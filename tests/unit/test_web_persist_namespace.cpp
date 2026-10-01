// Pins the browser persistence namespace rule (docs/INSTALL.md, "Embedding /
// hosting for multiple users"): window.__opengladPersistNamespace selects the
// IDBFS mount "/persist_<token>" only for 1..64 characters of [A-Za-z0-9_-];
// anything else keeps the shared "/persist" store. The rule is compiled on
// every platform so these teeth bite off the wasm target.
#include <gtest/gtest.h>

#include <openglad/resources/io_common.h>

#include <string>
#include <string_view>

TEST(WebPersistNamespace, empty_token_selects_the_default_store)
{
    ASSERT_FALSE(is_valid_web_persist_namespace(""));
    ASSERT_EQ("/persist", web_persist_root_for_namespace(""));
    ASSERT_EQ(std::string(kWebPersistRootDefault),
              web_persist_root_for_namespace(std::string_view()));
}

TEST(WebPersistNamespace, valid_token_names_its_own_store)
{
    ASSERT_TRUE(is_valid_web_persist_namespace("u_9f83c1a4e2b7"));
    ASSERT_EQ("/persist_u_9f83c1a4e2b7",
              web_persist_root_for_namespace("u_9f83c1a4e2b7"));
    // Every character class the contract admits, including both separators.
    ASSERT_EQ("/persist_AZaz09_-", web_persist_root_for_namespace("AZaz09_-"));
    // Single-character tokens are the lower bound of the contract.
    ASSERT_EQ("/persist_x", web_persist_root_for_namespace("x"));
    ASSERT_EQ("/persist_-", web_persist_root_for_namespace("-"));
}

TEST(WebPersistNamespace, length_bound_is_sixty_four_inclusive)
{
    ASSERT_EQ(64u, kWebPersistNamespaceMaxLength);
    const std::string at_limit(kWebPersistNamespaceMaxLength, 'a');
    const std::string over_limit(kWebPersistNamespaceMaxLength + 1, 'a');
    ASSERT_TRUE(is_valid_web_persist_namespace(at_limit));
    ASSERT_EQ("/persist_" + at_limit, web_persist_root_for_namespace(at_limit));
    ASSERT_FALSE(is_valid_web_persist_namespace(over_limit));
    ASSERT_EQ("/persist", web_persist_root_for_namespace(over_limit));
}

TEST(WebPersistNamespace, any_character_outside_the_alphabet_rejects_the_token)
{
    // One probe per excluded class: whitespace, path syntax (the token must
    // never be able to climb out of "/persist_"), punctuation adjacent to the
    // allowed separators, a NUL in the middle, and a non-ASCII byte.
    const std::string_view rejected[] = {
        "a b", "a/b", "../x", "a.b", "a+b", "a=b", "a~b", "a:b", "a\\b",
        std::string_view("a\0b", 3), "\xC3\xA9", " ", "-\n",
    };
    for (const std::string_view token : rejected)
    {
        ASSERT_FALSE(is_valid_web_persist_namespace(token))
            << "token accepted: " << std::string(token);
        ASSERT_EQ("/persist", web_persist_root_for_namespace(token))
            << "token leaked into the mount: " << std::string(token);
    }
    // A valid-length token is also rejected when only its LAST byte is bad,
    // so the scan covers the whole string, not a prefix.
    std::string tail_bad(kWebPersistNamespaceMaxLength - 1, 'z');
    tail_bad += '!';
    ASSERT_FALSE(is_valid_web_persist_namespace(tail_bad));
    ASSERT_EQ("/persist", web_persist_root_for_namespace(tail_bad));
}
