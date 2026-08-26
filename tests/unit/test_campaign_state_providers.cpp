// og::data::make_campaign_providers (campaign_state_providers.cpp): the
// menu-time og.campaign_* provider glue over a scripted SaveData — acting
// team resolution (Base Camp gold-label rule), wallet spend/grant rules
// (infinite-gold skip, uint32 saturation), the roster snapshot's plain
// values, campaign-state get/set pass-through, and the level/title readers.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
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
    member->campaign_tag = 2;
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

    // Per-hero identity (GTL v16): the campaign_tag byte plus the REAL
    // team_list slot — a roster with a hole keeps disk slot numbers, not
    // dense indices, or an assign write would land on the wrong hero.
    ASSERT_EQ(2, roster[0].tag);
    ASSERT_EQ(0, roster[0].save_slot);
    ASSERT_EQ(0, roster[1].tag) << "an unassigned hero reads tag 0";
    ASSERT_EQ(2, roster[1].save_slot)
        << "save_slot is the team_list index, skipping the null slot";
}

// ---------------------------------------------------------------------------
// assign_set (GTL v16): bounds-checked write of a hero's campaign_tag,
// addressed by save slot.

TEST(CampaignStateProviders, assign_set_writes_tag_with_bounds_checks)
{
    SaveData save;
    save.team_list[0] = make_member(FAMILY_SOLDIER, 0, "SWORN");
    save.team_list[2] = make_member(FAMILY_SOLDIER, 0, "GAPPED");
    save.team_size = 2;

    const CampaignProviders providers = make_campaign_providers(save);
    ASSERT_TRUE(providers.assign_set) << "assign_set must be filled";

    ASSERT_TRUE(providers.assign_set(0, 1)) << "occupied slot takes the tag";
    ASSERT_EQ(1, static_cast<int>(save.team_list[0]->campaign_tag));
    ASSERT_TRUE(providers.assign_set(0, 255)) << "255 is the last legal tag";
    ASSERT_EQ(255, static_cast<int>(save.team_list[0]->campaign_tag));
    ASSERT_TRUE(providers.assign_set(0, 0)) << "0 un-assigns";
    ASSERT_EQ(0, static_cast<int>(save.team_list[0]->campaign_tag));
    ASSERT_TRUE(providers.assign_set(2, 2))
        << "a slot past a roster hole is addressed by its real index";
    ASSERT_EQ(2, static_cast<int>(save.team_list[2]->campaign_tag));

    // Refusals: invalid slot, unoccupied slot, tag outside the byte.
    ASSERT_FALSE(providers.assign_set(-1, 1)) << "negative slot refused";
    ASSERT_FALSE(providers.assign_set(MAX_TEAM_SIZE, 1))
        << "slot past the roster refused";
    ASSERT_FALSE(providers.assign_set(1, 1)) << "empty slot refused";
    ASSERT_FALSE(providers.assign_set(0, -1)) << "negative tag refused";
    ASSERT_FALSE(providers.assign_set(0, 256)) << "tag past the byte refused";
    ASSERT_EQ(0, static_cast<int>(save.team_list[0]->campaign_tag))
        << "refusals never mutate";
    ASSERT_EQ(2, static_cast<int>(save.team_list[2]->campaign_tag));
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

// ---------------------------------------------------------------------------
// #212 match knobs: SaveData field mapping, lobby-sanitizer clamps, unknown
// names, host gating, and the dirty-flag session tail.

TEST(CampaignStateProviders, match_get_reads_the_matchup_knobs)
{
    SaveData save;
    save.ctf_team_count = 3;
    save.ctf_capture_limit = 10;
    save.ctf_respawn_ticks = 120;
    save.ctf_strip_scenario_troops = 2;
    save.respawn_mode = 1;
    save.generator_rate = 200;
    save.time_limit = 7200;

    const CampaignProviders providers = make_campaign_providers(save);
    EXPECT_EQ(3, providers.match_get("team_count"));
    EXPECT_EQ(10, providers.match_get("score_limit"));
    EXPECT_EQ(120, providers.match_get("respawn_ticks"));
    EXPECT_EQ(2, providers.match_get("strip_troops"));
    EXPECT_EQ(1, providers.match_get("respawn_mode"));
    EXPECT_EQ(200, providers.match_get("generator_rate"));
    EXPECT_EQ(7200, providers.match_get("time_limit"));
    EXPECT_EQ(0, providers.match_get("no_such_knob"))
        << "an unknown name reads 0 (the binding errors before this)";
}

TEST(CampaignStateProviders, match_set_clamps_like_the_lobby_sanitizer)
{
    SaveData save;
    (void)og::data::consume_match_settings_dirty();
    const CampaignProviders providers = make_campaign_providers(save);

    // team_count: <= 0 is Auto (0); anything else clamps into [2, 4].
    EXPECT_TRUE(providers.match_set("team_count", 1));
    EXPECT_EQ(2, save.ctf_team_count);
    EXPECT_TRUE(providers.match_set("team_count", 9));
    EXPECT_EQ(4, save.ctf_team_count);
    EXPECT_TRUE(providers.match_set("team_count", -5));
    EXPECT_EQ(0, save.ctf_team_count) << "non-positive collapses to Auto";

    // score_limit clamps into [0, 50].
    EXPECT_TRUE(providers.match_set("score_limit", 99));
    EXPECT_EQ(50, save.ctf_capture_limit);
    EXPECT_TRUE(providers.match_set("score_limit", -1));
    EXPECT_EQ(0, save.ctf_capture_limit);

    // respawn_ticks: 0 stays 0; anything else clamps into [12, 1200].
    EXPECT_TRUE(providers.match_set("respawn_ticks", 5));
    EXPECT_EQ(12, save.ctf_respawn_ticks);
    EXPECT_TRUE(providers.match_set("respawn_ticks", 9999));
    EXPECT_EQ(1200, save.ctf_respawn_ticks);
    EXPECT_TRUE(providers.match_set("respawn_ticks", 0));
    EXPECT_EQ(0, save.ctf_respawn_ticks);

    // generator_rate: 0 stays 0 (default); else clamps into [25, 400].
    EXPECT_TRUE(providers.match_set("generator_rate", 10));
    EXPECT_EQ(25, save.generator_rate);
    EXPECT_TRUE(providers.match_set("generator_rate", 1000));
    EXPECT_EQ(400, save.generator_rate);
    EXPECT_TRUE(providers.match_set("generator_rate", 0));
    EXPECT_EQ(0, save.generator_rate);

    // time_limit: 0 stays 0 (the map's own value); else clamps into
    // [720, 21600] — the same numbers lobby_server.cpp sanitize_settings
    // uses, so a scripted preset can never publish a bounced value.
    EXPECT_TRUE(providers.match_set("time_limit", 30));
    EXPECT_EQ(720, save.time_limit);
    EXPECT_TRUE(providers.match_set("time_limit", 32000));
    EXPECT_EQ(21600, save.time_limit);
    EXPECT_TRUE(providers.match_set("time_limit", 7200));
    EXPECT_EQ(7200, save.time_limit);
    EXPECT_TRUE(providers.match_set("time_limit", 0));
    EXPECT_EQ(0, save.time_limit);

    // The enum knobs REFUSE out-of-range values (where the sanitizer
    // reverts to its fallback, a provider write answers false unwritten).
    EXPECT_TRUE(providers.match_set("strip_troops", 3));
    EXPECT_EQ(3, save.ctf_strip_scenario_troops);
    EXPECT_FALSE(providers.match_set("strip_troops", 4));
    EXPECT_EQ(3, save.ctf_strip_scenario_troops) << "a refusal never writes";
    EXPECT_FALSE(providers.match_set("strip_troops", -1));

    EXPECT_TRUE(providers.match_set("respawn_mode", 2));
    EXPECT_EQ(2, save.respawn_mode);
    EXPECT_FALSE(providers.match_set("respawn_mode", 4));
    EXPECT_EQ(2, save.respawn_mode);
    EXPECT_FALSE(providers.match_set("respawn_mode", -1));

    EXPECT_FALSE(providers.match_set("no_such_knob", 1))
        << "unknown names answer false";
    (void)og::data::consume_match_settings_dirty();
}

TEST(CampaignStateProviders, match_get_and_set_cover_the_per_team_bot_knobs)
{
    SaveData save;
    (void)og::data::consume_match_settings_dirty();
    save.bot_squad = {0, 2, 1, 9};
    save.bot_level = {0, 5, -4, 1};

    const CampaignProviders providers = make_campaign_providers(save);
    // Each name reads its OWN team's slot: a transposed slot map would still
    // pass a test that only checked one index.
    EXPECT_EQ(0, providers.match_get("bot_squad_1"));
    EXPECT_EQ(2, providers.match_get("bot_squad_2"));
    EXPECT_EQ(1, providers.match_get("bot_squad_3"));
    EXPECT_EQ(9, providers.match_get("bot_squad_4"));
    EXPECT_EQ(0, providers.match_get("bot_level_1"));
    EXPECT_EQ(5, providers.match_get("bot_level_2"));
    EXPECT_EQ(-4, providers.match_get("bot_level_3"));
    EXPECT_EQ(1, providers.match_get("bot_level_4"));

    // Team 0 and team 5 are outside the 1..4 vocabulary.
    EXPECT_EQ(0, providers.match_get("bot_squad_0"));
    EXPECT_EQ(0, providers.match_get("bot_squad_5"));
    EXPECT_FALSE(providers.match_set("bot_squad_0", 3));
    EXPECT_FALSE(providers.match_set("bot_level_5", 3));
    EXPECT_FALSE(providers.match_set("bot_squad_", 3));

    // Both knobs CLAMP (0 = AUTO is legal, so neither refuses), with the same
    // bounds the lobby sanitizer applies.
    EXPECT_TRUE(providers.match_set("bot_squad_1", -3));
    EXPECT_EQ(0, save.bot_squad[0]);
    EXPECT_TRUE(providers.match_set("bot_squad_1", 999));
    EXPECT_EQ(og::sim::kMaxBotSquad, save.bot_squad[0]);
    EXPECT_TRUE(providers.match_set("bot_squad_1", 4));
    EXPECT_EQ(4, save.bot_squad[0]);

    // The level is an OFFSET (A6), so a negative is a legal request and
    // the clamp has a floor as well as a ceiling.
    EXPECT_TRUE(providers.match_set("bot_level_4", -1));
    EXPECT_EQ(-1, save.bot_level[3]);
    EXPECT_TRUE(providers.match_set("bot_level_4", -400));
    EXPECT_EQ(og::sim::kMinBotLevel, save.bot_level[3]);
    EXPECT_TRUE(providers.match_set("bot_level_4", 400));
    EXPECT_EQ(og::sim::kMaxBotLevel, save.bot_level[3]);
    EXPECT_TRUE(providers.match_set("bot_level_4", 4));
    EXPECT_EQ(4, save.bot_level[3]);

    // A write to team 3 must not disturb its neighbours.
    EXPECT_TRUE(providers.match_set("bot_squad_3", 7));
    EXPECT_EQ(4, save.bot_squad[0]);
    EXPECT_EQ(2, save.bot_squad[1]);
    EXPECT_EQ(7, save.bot_squad[2]);
    EXPECT_EQ(9, save.bot_squad[3]);
    (void)og::data::consume_match_settings_dirty();
}

TEST(CampaignStateProviders, bot_knob_names_are_in_the_campaign_vocabulary)
{
    // og.campaign_match_get errors on any name outside this list, so a knob
    // that reached the provider but not the vocabulary would be unreachable
    // from Lua.
    const auto listed = [](const char* wanted) {
        for (const char* name :
             og::script::hooks::kCampaignMatchSettingNames)
        {
            if (std::string(name) == wanted)
                return true;
        }
        return false;
    };
    for (int team = 1; team <= 4; ++team)
    {
        EXPECT_TRUE(listed(("bot_squad_" + std::to_string(team)).c_str()))
            << "bot_squad_" << team;
        EXPECT_TRUE(listed(("bot_level_" + std::to_string(team)).c_str()))
            << "bot_level_" << team;
    }
}

TEST(CampaignStateProviders, bot_knob_writes_are_refused_for_non_hosts)
{
    SaveData save;
    save.bot_squad = {0, 0, 0, 0};
    (void)og::data::consume_match_settings_dirty();

    bool host = false;
    const CampaignProviders providers =
        make_campaign_providers(save, [&host] { return host; });
    EXPECT_FALSE(providers.match_set("bot_squad_2", 3));
    EXPECT_EQ(0, save.bot_squad[1]) << "the refusal never writes";
    EXPECT_FALSE(og::data::consume_match_settings_dirty());
    EXPECT_EQ(0, providers.match_get("bot_squad_2"))
        << "reads stay open to every machine";

    host = true;
    EXPECT_TRUE(providers.match_set("bot_squad_2", 3));
    EXPECT_EQ(3, save.bot_squad[1]);
    (void)og::data::consume_match_settings_dirty();
}

TEST(CampaignStateProviders, match_set_refuses_non_hosts)
{
    SaveData save;
    save.ctf_capture_limit = 7;
    (void)og::data::consume_match_settings_dirty();

    bool host = false;
    const CampaignProviders providers =
        make_campaign_providers(save, [&host] { return host; });

    EXPECT_FALSE(providers.is_host());
    EXPECT_FALSE(providers.match_set("score_limit", 20))
        << "a joiner's write is refused";
    EXPECT_EQ(7, save.ctf_capture_limit) << "the refusal never writes";
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "a refused write must not arm the dirty flag";
    EXPECT_EQ(7, providers.match_get("score_limit"))
        << "reads stay open to every machine";

    host = true;
    EXPECT_TRUE(providers.is_host()) << "the predicate is read live";
    EXPECT_TRUE(providers.match_set("score_limit", 20));
    EXPECT_EQ(20, save.ctf_capture_limit);
    (void)og::data::consume_match_settings_dirty();
}

TEST(CampaignStateProviders, default_is_host_is_always_true)
{
    SaveData save;
    const CampaignProviders providers = make_campaign_providers(save);
    EXPECT_TRUE(providers.is_host()) << "local play is always host";
    (void)og::data::consume_match_settings_dirty();
}

TEST(CampaignStateProviders, match_set_dirty_flag_check_and_clear)
{
    SaveData save;
    (void)og::data::consume_match_settings_dirty(); // start clean

    const CampaignProviders providers = make_campaign_providers(save);
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "nothing written yet";

    EXPECT_TRUE(providers.match_set("score_limit", 5));
    EXPECT_TRUE(og::data::consume_match_settings_dirty())
        << "a successful write arms the flag";
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "consuming clears it";

    EXPECT_FALSE(providers.match_set("no_such_knob", 5));
    EXPECT_FALSE(og::data::consume_match_settings_dirty())
        << "a refused write leaves the flag clear";
}

} // namespace
