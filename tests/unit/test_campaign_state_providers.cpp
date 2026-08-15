// og::data::make_campaign_providers (campaign_state_providers.cpp): the
// menu-time og.campaign_* provider glue over a scripted SaveData — acting
// team resolution (Base Camp gold-label rule), wallet spend/grant rules
// (infinite-gold skip, uint32 saturation), the roster snapshot's plain
// values, campaign-state get/set pass-through, and the level/title readers.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/resources/campaign_state_providers.h>
#include <openglad/resources/save_data.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using og::data::make_campaign_providers;
using og::script::hooks::CampaignProviders;
using og::script::hooks::CampaignRosterEntry;

std::unique_ptr<guy> make_member(int family, short teamnum,
                                 const std::string& name)
{
    auto member = std::make_unique<guy>(static_cast<char>(family));
    member->teamnum = teamnum;
    member->name = name;
    return member;
}

// ---------------------------------------------------------------------------
// Acting team: lowest team number present on the roster; empty company
// falls back to my_team clamped to [0,3].

TEST(CampaignStateProviders, gold_reads_lowest_roster_team_wallet)
{
    SaveData save;
    save.team_list[0] = make_member(FAMILY_SOLDIER, 2, "HIGH");
    save.team_list[1] = make_member(FAMILY_SOLDIER, 1, "LOW");
    save.team_size = 2;
    save.my_team = 3; // must lose to the roster scan
    save.m_totalcash[1] = 777;
    save.m_totalcash[3] = 111;

    const CampaignProviders providers = make_campaign_providers(save);
    ASSERT_EQ(777, providers.gold_get())
        << "the acting wallet is the lowest roster team, not my_team";
}

TEST(CampaignStateProviders, gold_empty_roster_falls_back_to_my_team)
{
    SaveData save;
    save.my_team = 2;
    save.m_totalcash[2] = 4242;

    const CampaignProviders providers = make_campaign_providers(save);
    ASSERT_EQ(4242, providers.gold_get())
        << "an empty company reads my_team's wallet";

    save.my_team = 9; // out of range -> clamp to team 0
    save.m_totalcash[0] = 55;
    ASSERT_EQ(55, providers.gold_get())
        << "an out-of-range my_team falls back to team 0";
}

// ---------------------------------------------------------------------------
// Spend: affordability-checked debit; infinite gold answers true and skips
// the debit; refusals never mutate.

TEST(CampaignStateProviders, gold_spend_rules)
{
    SaveData save;
    save.my_team = 0;
    save.m_totalcash[0] = 100;

    const CampaignProviders providers = make_campaign_providers(save);

    ASSERT_TRUE(providers.gold_spend(60)) << "affordable spend succeeds";
    ASSERT_EQ(40u, save.m_totalcash[0]) << "spend debits the wallet";

    ASSERT_FALSE(providers.gold_spend(41)) << "cost > wallet is refused";
    ASSERT_EQ(40u, save.m_totalcash[0]) << "a refused spend must not debit";

    ASSERT_FALSE(providers.gold_spend(-1)) << "negative cost is refused";
    ASSERT_EQ(40u, save.m_totalcash[0]);

    ASSERT_TRUE(providers.gold_spend(0)) << "zero cost is trivially affordable";
    ASSERT_EQ(40u, save.m_totalcash[0]);

    save.infinite_gold = 1;
    ASSERT_TRUE(providers.gold_spend(1000000))
        << "infinite gold answers affordable";
    ASSERT_EQ(40u, save.m_totalcash[0])
        << "infinite gold skips the debit — the wallet is never written";
}

TEST(CampaignStateProviders, gold_grant_saturates_at_uint32_max)
{
    SaveData save;
    save.my_team = 0;
    save.m_totalcash[0] = 100;

    const CampaignProviders providers = make_campaign_providers(save);

    providers.gold_grant(50);
    ASSERT_EQ(150u, save.m_totalcash[0]) << "grant credits the wallet";

    providers.gold_grant(-10);
    ASSERT_EQ(150u, save.m_totalcash[0]) << "negative grant is a no-op";

    constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
    save.m_totalcash[0] = kMax - 5;
    providers.gold_grant(1000);
    ASSERT_EQ(kMax, save.m_totalcash[0]) << "grant saturates at uint32 max";
    providers.gold_grant(1);
    ASSERT_EQ(kMax, save.m_totalcash[0]) << "a full wallet stays saturated";
}

// ---------------------------------------------------------------------------
// Roster snapshot: plain values, display-name family, team clamped [0,3].

TEST(CampaignStateProviders, team_snapshot_carries_plain_values)
{
    SaveData save;
    auto member = make_member(FAMILY_SOLDIER, 1, "VALKYRIE");
    member->level = 4;
    member->exp = 1234;
    member->strength = 14;
    member->dexterity = 13;
    member->constitution = 12;
    member->intelligence = 11;
    member->armor = 10;
    save.team_list[0] = std::move(member);
    save.team_list[2] = make_member(FAMILY_SOLDIER, 9, "OFFTEAM");
    save.team_size = 2;

    const CampaignProviders providers = make_campaign_providers(save);
    const std::vector<CampaignRosterEntry> roster = providers.team_snapshot();

    ASSERT_EQ(2u, roster.size()) << "null slots are skipped";
    ASSERT_EQ("VALKYRIE", roster[0].name);
    ASSERT_EQ("SOLDIER", roster[0].family)
        << "family is the descriptor display name";
    ASSERT_EQ(4, roster[0].level);
    ASSERT_EQ(1234, roster[0].exp);
    ASSERT_EQ(14, roster[0].strength);
    ASSERT_EQ(13, roster[0].dexterity);
    ASSERT_EQ(12, roster[0].constitution);
    ASSERT_EQ(11, roster[0].intelligence);
    ASSERT_EQ(10, roster[0].armor);
    ASSERT_EQ(1, roster[0].team);
    ASSERT_EQ(3, roster[1].team) << "team is clamped into [0,3]";
}

// ---------------------------------------------------------------------------
// Campaign state pass-through: providers read/write the CURRENT campaign's
// store, and the bounds choke answers false through the provider too.

TEST(CampaignStateProviders, state_roundtrip_follows_current_campaign)
{
    SaveData save;
    save.current_campaign = "camp_one";

    const CampaignProviders providers = make_campaign_providers(save);

    ASSERT_EQ(0, providers.state_get("delve_counted")) << "unset reads 0";
    ASSERT_TRUE(providers.state_set("delve_counted", 3));
    ASSERT_EQ(3, providers.state_get("delve_counted"));
    ASSERT_EQ(3, save.campaign_state_get("camp_one", "delve_counted"))
        << "the write landed under the current campaign";

    save.current_campaign = "camp_two";
    ASSERT_EQ(0, providers.state_get("delve_counted"))
        << "state is per campaign — switching campaigns switches the book";
    ASSERT_TRUE(providers.state_set("delve_counted", 8));
    ASSERT_EQ(8, providers.state_get("delve_counted"));
    ASSERT_EQ(3, save.campaign_state_get("camp_one", "delve_counted"))
        << "the first campaign's book is untouched";

    ASSERT_FALSE(providers.state_set("BAD KEY", 1))
        << "the SaveData bounds choke answers through the provider";
}

// ---------------------------------------------------------------------------
// Level/cursor/title readers.

TEST(CampaignStateProviders, level_and_title_readers)
{
    SaveData save;
    save.current_campaign = "camp_one";
    save.scen_num = 7;
    save.add_level_completed("camp_one", 3);

    const CampaignProviders providers = make_campaign_providers(save);

    ASSERT_EQ(7, providers.current_level()) << "the campaign cursor";
    ASSERT_TRUE(providers.level_completed(3));
    ASSERT_FALSE(providers.level_completed(4));

    ASSERT_EQ(std::string(), providers.scenario_title(654321))
        << "a missing scenario answers the empty string, never 'none'";
}

} // namespace
