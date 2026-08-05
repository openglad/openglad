// normalize_legacy_id — the read-boundary mapping for stored campaign and
// pack ids written before the reverse-DNS purge ("org.openglad.gladiator"
// era saves/replays must keep loading under the plain ids).
#include <gtest/gtest.h>

#include <openglad/core/campaign_ids.h>

TEST(CampaignIds, normalize_strips_the_retired_prefix_from_campaign_ids)
{
    EXPECT_EQ("gladiator", og::normalize_legacy_id("org.openglad.gladiator"));
    EXPECT_EQ("tryxian", og::normalize_legacy_id("org.openglad.tryxian"));
    EXPECT_EQ("westlands", og::normalize_legacy_id("org.openglad.westlands"));
    EXPECT_EQ("longseason", og::normalize_legacy_id("org.openglad.longseason"));
    EXPECT_EQ("tower", og::normalize_legacy_id("org.openglad.tower"));
    EXPECT_EQ("concept", og::normalize_legacy_id("org.openglad.concept"));
    EXPECT_EQ("modes", og::normalize_legacy_id("org.openglad.modes"));
}

TEST(CampaignIds, normalize_strips_the_retired_prefix_from_pack_ids)
{
    // Pack ids keep their dotted tail: the prefix is stripped literally,
    // never collapsed to a last segment.
    EXPECT_EQ("modes.core", og::normalize_legacy_id("org.openglad.modes.core"));
    EXPECT_EQ("concept.showcase",
              og::normalize_legacy_id("org.openglad.concept.showcase"));
}

TEST(CampaignIds, normalize_is_idempotent_on_plain_ids)
{
    EXPECT_EQ("gladiator", og::normalize_legacy_id("gladiator"));
    EXPECT_EQ("modes.core", og::normalize_legacy_id("modes.core"));
    EXPECT_EQ("gladiator",
              og::normalize_legacy_id(
                  og::normalize_legacy_id("org.openglad.gladiator")));
}

TEST(CampaignIds, normalize_leaves_foreign_ids_untouched)
{
    EXPECT_EQ("com.example.westlands",
              og::normalize_legacy_id("com.example.westlands"));
    EXPECT_EQ("my-campaign", og::normalize_legacy_id("my-campaign"));
    EXPECT_EQ("org.opengladx.foo", og::normalize_legacy_id("org.opengladx.foo"));
    // A prefix of the prefix is not the prefix.
    EXPECT_EQ("org.openglad", og::normalize_legacy_id("org.openglad"));
}

TEST(CampaignIds, normalize_never_produces_an_empty_id)
{
    EXPECT_EQ("org.openglad.", og::normalize_legacy_id("org.openglad."));
    EXPECT_EQ("", og::normalize_legacy_id(""));
}
