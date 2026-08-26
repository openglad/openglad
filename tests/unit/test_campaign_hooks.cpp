/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Campaign scripting (issue #206): the og.register_campaign_hooks
// registrar, the campaign-dispatch fence, the og.campaign_* provider
// bindings, page/action dispatch, and the sim-side og.campaign_var read.
// See docs/campaign-scripting-design.md.

#include <gtest/gtest.h>

#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/ui/picker_common.h>

#include <algorithm>
#include <optional>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace og::script;

namespace {

// Chunk names deliberately do NOT start with `packs/`: that prefix declares
// the bytes to the pack-Lua coverage inventory, and these throwaway chunks
// exist nowhere in the repository (the test_classpack_lua_decl discipline).
constexpr const char* kPack = "test.campaign";

class CampaignHooksTest : public ::testing::Test {
protected:
    CampaignHooksTest()
    {
        previous_ = current_game;
        current_game = nullptr;  // dispatch resolves the shared UI VM
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_pack_lib_modules();
        hooks::clear_campaign_providers();
    }

    ~CampaignHooksTest() override
    {
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_pack_lib_modules();
        hooks::clear_campaign_providers();
        current_game = previous_;
    }

    static void register_script(const std::string& source,
                                const char* chunk = "campaigntest/scripts/c.lua")
    {
        register_pack_script({kPack, chunk, source});
    }

    static const std::vector<ScriptError>& vm_errors()
    {
        return active_world_scripts().host().errors();
    }

    static const std::vector<std::string>& vm_log()
    {
        return active_world_scripts().host().log();
    }

    static bool errors_contain(const std::string& needle)
    {
        for (const ScriptError& e : vm_errors()) {
            if (e.message.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

    GameplayContext* previous_ = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

TEST_F(CampaignHooksTest, registration_happy_path_serves_picker_and_vars)
{
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { "delve_counted", "watch_paid" },
  picker_menu = function(page_id)
    return { title = "BOOK" }
  end,
  picker_action = function(entry_id)
    return nil
  end,
}))LUA");
    EXPECT_TRUE(hooks::campaign_picker_registered());
    const std::vector<std::string> vars = hooks::campaign_registered_vars();
    ASSERT_EQ(2u, vars.size());
    EXPECT_EQ("delve_counted", vars[0]);
    EXPECT_EQ("watch_paid", vars[1]);
    EXPECT_TRUE(vm_errors().empty()) << vm_errors().front().message;
}

TEST_F(CampaignHooksTest, no_registration_means_no_picker)
{
    register_script(R"LUA(og.log("no campaign registration here"))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(hooks::campaign_registered_vars().empty());
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("", page));
    hooks::CampaignActionResult result;
    EXPECT_FALSE(hooks::campaign_picker_action("x", result));
}

TEST_F(CampaignHooksTest, declare_mode_registrar_is_a_silent_noop)
{
    og::data::ClasspackData data;
    register_pack_family_chunk({kPack, "campaigntest/families/a.lua",
                                R"LUA(og.register_campaign_hooks({
  vars = { "delve_counted" },
  picker_menu = function(page_id)
    return { title = "BOOK" }
  end,
}))LUA"});
    const DeclareResult result = declare_pack_families(kPack, data);
    EXPECT_TRUE(result.ok) << result.error;
}

TEST_F(CampaignHooksTest, declare_mode_still_rejects_a_bad_registration)
{
    og::data::ClasspackData data;
    register_pack_family_chunk({kPack, "campaigntest/families/a.lua",
                                R"LUA(og.register_campaign_hooks({
  picker_men = function(page_id)
    return { title = "BOOK" }
  end,
}))LUA"});
    const DeclareResult result = declare_pack_families(kPack, data);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(std::string::npos, result.error.find("picker_menu"))
        << result.error;
}

TEST_F(CampaignHooksTest, duplicate_registration_conflicts_and_reports_once)
{
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { "delve_counted" },
  picker_menu = function(page_id)
    return { title = "FIRST" }
  end,
}))LUA",
                    "campaigntest/scripts/c1.lua");
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { title = "SECOND" }
  end,
}))LUA",
                    "campaigntest/scripts/c2.lua");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(hooks::campaign_registered_vars().empty());
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("", page));

    // Recorded once (the query above asked three times), naming both chunks.
    std::size_t records = 0;
    for (const ScriptError& e : vm_errors()) {
        if (e.message.find("duplicate og.register_campaign_hooks") ==
            std::string::npos)
            continue;
        records++;
        EXPECT_EQ(1u, e.count);
        EXPECT_NE(std::string::npos, e.message.find("c1.lua")) << e.message;
        EXPECT_NE(std::string::npos, e.message.find("c2.lua")) << e.message;
    }
    EXPECT_EQ(1u, records);
}

TEST_F(CampaignHooksTest, extra_registrar_argument_is_dropped)
{
    // The registrar reads absolute stack indices 2/3; a stray second
    // argument must be dropped, not read as the picker_menu value.
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { "delve_counted" },
  picker_menu = function(page_id)
    return { title = "BOOK" }
  end,
}, "stray extra argument"))LUA");
    EXPECT_TRUE(hooks::campaign_picker_registered())
        << "a stray second argument must not shadow the hook reads";
    const std::vector<std::string> vars = hooks::campaign_registered_vars();
    ASSERT_EQ(1u, vars.size());
    EXPECT_EQ("delve_counted", vars[0]);
    EXPECT_TRUE(vm_errors().empty()) << vm_errors().front().message;
}

TEST_F(CampaignHooksTest, unknown_key_is_a_load_error_with_did_you_mean)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_men = function(page_id)
    return { title = "X" }
  end,
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("unknown key 'picker_men'"));
    EXPECT_TRUE(errors_contain("did you mean 'picker_menu'"));
}

TEST_F(CampaignHooksTest, neither_hook_function_is_a_load_error)
{
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { "delve_counted" },
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain(
        "register at least one of 'picker_menu' / 'picker_action' / "
        "'base_camp'"));
}

TEST_F(CampaignHooksTest, base_camp_alone_is_a_legal_registration)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster" } } }
  end,
}))LUA");
    EXPECT_TRUE(hooks::campaign_picker_registered());
    EXPECT_TRUE(hooks::campaign_zone_registered());
    // The picker surfaces stay unserved.
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("", page));
    hooks::CampaignActionResult result;
    EXPECT_FALSE(hooks::campaign_picker_action("x", result));
    hooks::CampaignZone zone;
    ASSERT_TRUE(hooks::campaign_zone(zone));
    ASSERT_EQ(1u, zone.widgets.size());
    EXPECT_TRUE(vm_errors().empty()) << vm_errors().front().message;
}

TEST_F(CampaignHooksTest, non_function_base_camp_is_a_load_error)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = "not a function",
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("'base_camp' must be a function"));
}

TEST_F(CampaignHooksTest, base_camp_key_typo_gets_did_you_mean)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_cam = function()
    return { widgets = {} }
  end,
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("unknown key 'base_cam'"));
    EXPECT_TRUE(errors_contain("did you mean 'base_camp'"));
}

TEST_F(CampaignHooksTest, zone_unregistered_without_base_camp_hook)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { title = "BOOK" }
  end,
}))LUA");
    EXPECT_TRUE(hooks::campaign_picker_registered());
    EXPECT_FALSE(hooks::campaign_zone_registered());
    hooks::CampaignZone zone;
    EXPECT_FALSE(hooks::campaign_zone(zone));
}

TEST_F(CampaignHooksTest, non_function_hook_is_a_load_error)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = "not a function",
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("'picker_menu' must be a function"));
}

TEST_F(CampaignHooksTest, bad_vars_are_load_errors)
{
    // Not a table.
    register_script(R"LUA(og.register_campaign_hooks({
  vars = "delve_counted",
  picker_menu = function(page_id)
    return { title = "X" }
  end,
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("'vars' must be an array"));

    // Charset: uppercase is rejected.
    clear_pack_scripts();
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { "BadName" },
  picker_menu = function(page_id)
    return { title = "X" }
  end,
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("[a-z0-9_]"));

    // Length: 33 chars is rejected.
    clear_pack_scripts();
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { "a123456789012345678901234567890123" },
  picker_menu = function(page_id)
    return { title = "X" }
  end,
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("1-32 chars"));

    // A non-string entry is rejected.
    clear_pack_scripts();
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { 7 },
  picker_menu = function(page_id)
    return { title = "X" }
  end,
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("vars[1] must be a string"));

    // 65 names blow the 64 bound.
    clear_pack_scripts();
    register_script(R"LUA(local vars = {}
for i = 1, 65 do
  table.insert(vars, "v" .. i)
end
og.register_campaign_hooks({
  vars = vars,
  picker_menu = function(page_id)
    return { title = "X" }
  end,
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("max 64"));

    // A hash-keyed table has rawlen 0 and would silently register NOTHING —
    // rejected with the array rule named.
    clear_pack_scripts();
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { watch_paid = 1 },
  picker_menu = function(page_id)
    return { title = "X" }
  end,
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("'vars' must be an ARRAY of names"));

    // A mixed array+hash table is rejected the same way.
    clear_pack_scripts();
    register_script(R"LUA(og.register_campaign_hooks({
  vars = { "a", extra = 1 },
  picker_menu = function(page_id)
    return { title = "X" }
  end,
}))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());
    EXPECT_TRUE(errors_contain("found a non-array key"));
}

// ---------------------------------------------------------------------------
// The campaign-dispatch fence
// ---------------------------------------------------------------------------

TEST_F(CampaignHooksTest, fence_walk_everything_outside_the_allowlist_errors)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    for name in string.gmatch(page_id, "%S+") do
      local ok, err = pcall(og[name])
      if ok then
        og.log("NOFENCE " .. name)
      elseif string.find(err, "campaign hooks", 1, true) == nil then
        og.log("WRONGMSG " .. name .. ": " .. err)
      end
    end
    local selffenced = { "use", "family", "anims", "pack" }
    for i = 1, #selffenced do
      local ok = pcall(og[selffenced[i]])
      if ok then
        og.log("NOFENCE " .. selffenced[i])
      end
    end
    og.log("maxok " .. og.max(1, 2))
    og.log("divok " .. og.div(7, 2))
    og.log("famok " .. tostring(pcall(og.family_id, "living", "no_such")))
    og.log("gridok " .. og.C.GRID_SIZE)
    og.log("combatok " .. tostring(og.combat.yell_radius ~= nil))
    return { title = "walk" }
  end,
}))LUA");

    // The allowlist (docs/campaign-scripting-design.md): og.campaign_*, the
    // sandbox arithmetic (kOgFuncs), og.max/min/clamp/sign, og.family_id.
    // og.use/family/anims/pack self-fence with their own messages and are
    // walked separately above.
    const std::set<std::string> allowed = {
        "campaign_state_get", "campaign_state_set", "campaign_gold",
        "campaign_spend_gold", "campaign_grant_gold", "campaign_team",
        "campaign_level_completed", "campaign_current_level",
        "campaign_scenario_title",
        "campaign_match_get", "campaign_match_set", "campaign_is_host",
        "campaign_random",
        "div", "mod", "fadd", "fsub", "fmul", "fdiv", "i8", "i16", "i32",
        "u8", "trunc", "log",
        "max", "min", "clamp", "sign",
        "family_id",
        "use", "family", "anims", "pack",  // walked with own messages
    };
    const std::vector<std::string> names = hooks::og_function_names();
    // The walk must not be vacuous: the world API, the RNG and all three
    // registrars are in it.
    for (const char* expected :
         {"rand", "rand0", "add_ob", "oblist", "tuning", "set_entity_hooks",
          "register_hooks", "register_level_hooks",
          "register_campaign_hooks", "campaign_var", "mode_set"})
        EXPECT_TRUE(std::find(names.begin(), names.end(), expected) !=
                    names.end())
            << expected << " missing from the og table walk";

    std::string page_id;
    for (const std::string& name : names) {
        if (allowed.count(name) != 0)
            continue;
        page_id += name;
        page_id += " ";
    }
    ASSERT_GT(page_id.size(), 100u);

    hooks::CampaignPage page;
    ASSERT_TRUE(hooks::campaign_picker_page(page_id, page));
    EXPECT_EQ("walk", page.title);
    for (const std::string& line : vm_log()) {
        EXPECT_EQ(std::string::npos, line.find("NOFENCE")) << line;
        EXPECT_EQ(std::string::npos, line.find("WRONGMSG")) << line;
    }
    // The allowlisted surface really worked during the dispatch.
    ASSERT_GE(vm_log().size(), 5u);
    const std::vector<std::string>& log = vm_log();
    EXPECT_TRUE(std::find(log.begin(), log.end(), "maxok 2") != log.end());
    EXPECT_TRUE(std::find(log.begin(), log.end(), "divok 3") != log.end());
    EXPECT_TRUE(std::find(log.begin(), log.end(), "famok true") != log.end());
    EXPECT_TRUE(std::find(log.begin(), log.end(), "gridok 16") != log.end());
    EXPECT_TRUE(std::find(log.begin(), log.end(), "combatok true") !=
                log.end());
}

TEST_F(CampaignHooksTest, campaign_bindings_error_outside_campaign_dispatch)
{
    register_script(R"LUA(local names = {
  "campaign_state_get",
  "campaign_state_set",
  "campaign_gold",
  "campaign_spend_gold",
  "campaign_grant_gold",
  "campaign_team",
  "campaign_level_completed",
  "campaign_current_level",
  "campaign_scenario_title",
  "campaign_match_get",
  "campaign_match_set",
  "campaign_is_host",
  "campaign_random",
}
for i = 1, #names do
  local ok, err = pcall(og[names[i]])
  if ok then
    og.log("NOERR " .. names[i])
  elseif string.find(err, "campaign-hook only", 1, true) == nil then
    og.log("WRONGMSG " .. names[i] .. ": " .. err)
  end
end
og.log("walked"))LUA");
    EXPECT_FALSE(hooks::campaign_picker_registered());  // builds the VM
    const std::vector<std::string>& log = vm_log();
    ASSERT_FALSE(log.empty());
    EXPECT_EQ("walked", log.back());
    for (const std::string& line : log) {
        EXPECT_EQ(std::string::npos, line.find("NOERR")) << line;
        EXPECT_EQ(std::string::npos, line.find("WRONGMSG")) << line;
    }
}

// ---------------------------------------------------------------------------
// Provider bindings
// ---------------------------------------------------------------------------

TEST_F(CampaignHooksTest, campaign_bindings_error_with_no_provider)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    local calls = {
      { "campaign_state_get", "k" },
      { "campaign_state_set", "k", 1 },
      { "campaign_gold" },
      { "campaign_spend_gold", 1 },
      { "campaign_grant_gold", 1 },
      { "campaign_team" },
      { "campaign_level_completed", 1 },
      { "campaign_current_level" },
      { "campaign_scenario_title", 1 },
      { "campaign_match_get", "score_limit" },
      { "campaign_match_set", "score_limit", 2 },
      { "campaign_is_host" },
      { "campaign_random", 1 },
    }
    for i = 1, #calls do
      local c = calls[i]
      local ok, err = pcall(og[c[1]], c[2], c[3])
      if ok then
        og.log("NOERR " .. c[1])
      elseif string.find(err, "no campaign provider installed", 1, true) == nil then
        og.log("WRONGMSG " .. c[1] .. ": " .. err)
      end
    end
    return { title = "walked" }
  end,
}))LUA");
    hooks::CampaignPage page;
    ASSERT_TRUE(hooks::campaign_picker_page("", page));
    for (const std::string& line : vm_log()) {
        EXPECT_EQ(std::string::npos, line.find("NOERR")) << line;
        EXPECT_EQ(std::string::npos, line.find("WRONGMSG")) << line;
    }
}

TEST_F(CampaignHooksTest, provider_bindings_happy_path)
{
    std::map<std::string, std::int32_t> store;
    std::int64_t gold = 100;
    hooks::CampaignProviders providers;
    providers.state_get = [&](const std::string& key) -> std::int32_t {
        const auto it = store.find(key);
        return it == store.end() ? 0 : it->second;
    };
    providers.state_set = [&](const std::string& key, std::int32_t value) {
        store[key] = value;
        return true;
    };
    providers.gold_get = [&]() { return gold; };
    providers.gold_spend = [&](std::int64_t amount) {
        if (amount > gold)
            return false;
        gold -= amount;
        return true;
    };
    providers.gold_grant = [&](std::int64_t amount) { gold += amount; };
    providers.team_snapshot = [] {
        std::vector<hooks::CampaignRosterEntry> team;
        // The trailing pair is the GTL v16 per-hero identity: campaign_tag
        // then the save slot the assign write is addressed by. Both rows
        // carry values that are distinct per field AND per row, so a
        // transposition of the two lua_setfield pushes changes the log.
        team.push_back({"MILO", "SOLDIER", 3, 220, 12, 8, 10, 6, 8, 0, 7, 2});
        // tag 0 = unassigned, and a gapped slot: the roster is dense, the
        // slots it reports are not.
        team.push_back({"WYRD", "MAGE", 2, 90, 4, 7, 5, 14, 2, 1, 0, 5});
        return team;
    };
    providers.level_completed = [](int id) { return id == 5; };
    providers.current_level = [] { return 7; };
    providers.scenario_title = [](int id) {
        return id == 7 ? std::string("THE CIRCLE") : std::string();
    };
    hooks::install_campaign_providers(std::move(providers));

    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    og.log("get0 " .. og.campaign_state_get("delve"))
    og.campaign_state_set("delve", 3)
    og.log("get1 " .. og.campaign_state_get("delve"))
    og.log("gold " .. og.campaign_gold())
    og.log("spend " .. tostring(og.campaign_spend_gold(60)))
    og.log("broke " .. tostring(og.campaign_spend_gold(1000)))
    og.campaign_grant_gold(10)
    og.log("gold2 " .. og.campaign_gold())
    local team = og.campaign_team()
    og.log("teamn " .. #team)
    og.log("first " .. team[1].name .. " " .. team[1].family)
    og.log("stats " .. team[1].strength .. " " .. team[1].intelligence)
    og.log("second " .. team[2].name .. " " .. team[2].team)
    og.log("ident " .. team[1].tag .. " " .. team[1].save_slot ..
           " " .. team[2].tag .. " " .. team[2].save_slot)
    og.log("done5 " .. tostring(og.campaign_level_completed(5)))
    og.log("done6 " .. tostring(og.campaign_level_completed(6)))
    og.log("cur " .. og.campaign_current_level())
    og.log("title " .. og.campaign_scenario_title(7))
    og.log("blank " .. og.campaign_scenario_title(8))
    return { title = "ok" }
  end,
}))LUA");

    hooks::CampaignPage page;
    ASSERT_TRUE(hooks::campaign_picker_page("", page));
    const std::vector<std::string> expected = {
        "get0 0",   "get1 3",         "gold 100",  "spend true",
        "broke false", "gold2 50",    "teamn 2",
        "first MILO SOLDIER",         "stats 12 6",
        "second WYRD 1",              "ident 7 2 0 5",
        "done5 true", "done6 false",
        "cur 7",    "title THE CIRCLE", "blank ",
    };
    ASSERT_EQ(expected.size(), vm_log().size());
    for (std::size_t i = 0; i < expected.size(); i++)
        EXPECT_EQ(expected[i], vm_log()[i]);
    EXPECT_EQ(50, gold);
    ASSERT_EQ(1u, store.size());
    EXPECT_EQ(3, store.at("delve"));
}

TEST_F(CampaignHooksTest, state_set_bounds_rejection_raises_before_mutation)
{
    std::map<std::string, std::int32_t> store;
    std::vector<std::string> keys_offered;
    hooks::CampaignProviders providers;
    providers.state_set = [&](const std::string& key, std::int32_t value) {
        keys_offered.push_back(key);
        if (value > 1000)
            return false;  // check-then-write: refused, nothing written
        store[key] = value;
        return true;
    };
    hooks::install_campaign_providers(std::move(providers));

    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    local ok, err = pcall(og.campaign_state_set, "big", 5000)
    og.log("rej " .. tostring(ok))
    og.log("msg " .. err)
    local ok2, err2 = pcall(og.campaign_state_set, "BadKey", 1)
    og.log("badkey " .. tostring(ok2))
    og.log("msg2 " .. err2)
    local ok3, err3 = pcall(og.campaign_state_set, "wide", 3000000000)
    og.log("wide " .. tostring(ok3))
    og.log("msg3 " .. err3)
    return nil
  end,
}))LUA");

    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("x", result));
    EXPECT_TRUE(result.ok);
    const std::vector<std::string>& log = vm_log();
    ASSERT_EQ(6u, log.size());
    EXPECT_EQ("rej false", log[0]);
    EXPECT_NE(std::string::npos, log[1].find("rejected 'big'")) << log[1];
    EXPECT_EQ("badkey false", log[2]);
    EXPECT_NE(std::string::npos, log[3].find("[a-z0-9_]")) << log[3];
    EXPECT_EQ("wide false", log[4]);
    EXPECT_NE(std::string::npos, log[5].find("int32")) << log[5];
    // The store never mutated; the provider saw only the well-formed key
    // (the bad key and the out-of-range value raise BEFORE the provider).
    EXPECT_TRUE(store.empty());
    ASSERT_EQ(1u, keys_offered.size());
    EXPECT_EQ("big", keys_offered[0]);
}

// #212 og.campaign_match_get / og.campaign_match_set / og.campaign_is_host:
// the boolean answer rides through, get's unknown-name error matches the
// sim twin's, and the provider sees exactly the names the script sent.
TEST_F(CampaignHooksTest, match_bindings_round_trip)
{
    std::map<std::string, std::int32_t> knobs = {{"respawn_ticks", 0},
                                                 {"score_limit", 5}};
    bool host = true;
    hooks::CampaignProviders providers;
    providers.match_get = [&](const std::string& name) -> std::int32_t {
        const auto it = knobs.find(name);
        return it == knobs.end() ? 0 : it->second;
    };
    providers.match_set = [&](const std::string& name, std::int32_t value) {
        if (!host)
            return false;
        const auto it = knobs.find(name);
        if (it == knobs.end())
            return false;
        it->second = value;
        return true;
    };
    providers.is_host = [&] { return host; };
    hooks::install_campaign_providers(std::move(providers));

    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    og.log("host " .. tostring(og.campaign_is_host()))
    og.log("get0 " .. og.campaign_match_get("score_limit"))
    og.log("set " .. tostring(og.campaign_match_set("score_limit", 20)))
    og.log("get1 " .. og.campaign_match_get("score_limit"))
    og.log("unk " .. tostring(og.campaign_match_set("no_such_knob", 1)))
    local ok, err = pcall(og.campaign_match_get, "no_such_knob")
    og.log("getunk " .. tostring(ok))
    og.log("msg " .. err)
    return nil
  end,
}))LUA");

    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("x", result));
    EXPECT_TRUE(result.ok);
    const std::vector<std::string>& log = vm_log();
    ASSERT_EQ(7u, log.size());
    EXPECT_EQ("host true", log[0]);
    EXPECT_EQ("get0 5", log[1]);
    EXPECT_EQ("set true", log[2]);
    EXPECT_EQ("get1 20", log[3]);
    EXPECT_EQ("unk false", log[4])
        << "match_set answers false for unknown names, it does not raise";
    EXPECT_EQ("getunk false", log[5])
        << "match_get errors on unknown names, like og.match_setting";
    EXPECT_NE(std::string::npos, log[6].find("unknown setting")) << log[6];
    EXPECT_EQ(20, knobs.at("score_limit"));

    // Non-host: the same write answers false and nothing moves. A fresh
    // registration (a second one would be a duplicate-conflict) drives the
    // same action with the predicate flipped.
    host = false;
    clear_pack_scripts();
    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    og.log("host2 " .. tostring(og.campaign_is_host()))
    og.log("set2 " .. tostring(og.campaign_match_set("score_limit", 44)))
    return nil
  end,
}))LUA",
                    "campaigntest/scripts/d.lua");
    ASSERT_TRUE(hooks::campaign_picker_action("x", result));
    const std::vector<std::string>& log2 = vm_log();
    ASSERT_GE(log2.size(), 2u);
    EXPECT_EQ("host2 false", log2[log2.size() - 2]);
    EXPECT_EQ("set2 false", log2.back());
    EXPECT_EQ(20, knobs.at("score_limit")) << "a joiner's write never lands";
}

// D3 og.campaign_random: the provider's answer rides through untouched (a
// deterministic test provider proves the value is the provider's, not a
// hidden roll), the binding hands the provider exactly the n the script
// sent, and the two argument rejections — n < 1 and n past int32 — raise
// BEFORE the provider runs.
TEST_F(CampaignHooksTest, campaign_random_provider_answer_and_bounds)
{
    std::vector<int> asked;
    hooks::CampaignProviders providers;
    providers.random_pick = [&](int n) {
        asked.push_back(n);
        return n;  // deterministic: always the top of the range
    };
    hooks::install_campaign_providers(std::move(providers));

    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    og.log("one " .. og.campaign_random(1))
    og.log("many " .. og.campaign_random(39))
    local ok, err = pcall(og.campaign_random, 0)
    og.log("zero " .. tostring(ok))
    og.log("msg " .. err)
    local ok2, err2 = pcall(og.campaign_random, -5)
    og.log("neg " .. tostring(ok2))
    local ok3, err3 = pcall(og.campaign_random, 3000000000)
    og.log("wide " .. tostring(ok3))
    og.log("msg3 " .. err3)
    return nil
  end,
}))LUA");

    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("x", result));
    EXPECT_TRUE(result.ok);
    const std::vector<std::string>& log = vm_log();
    ASSERT_EQ(7u, log.size());
    EXPECT_EQ("one 1", log[0]);
    EXPECT_EQ("many 39", log[1]);
    EXPECT_EQ("zero false", log[2]) << "n < 1 raises";
    EXPECT_NE(std::string::npos, log[3].find("n must be >= 1")) << log[3];
    EXPECT_EQ("neg false", log[4]);
    EXPECT_EQ("wide false", log[5]) << "n past int32 raises";
    EXPECT_NE(std::string::npos, log[6].find("int32")) << log[6];
    // The provider saw ONLY the two legal asks — the rejections raised
    // before it ran.
    ASSERT_EQ(2u, asked.size());
    EXPECT_EQ(1, asked[0]);
    EXPECT_EQ(39, asked[1]);
}

// ---------------------------------------------------------------------------
// Page parse
// ---------------------------------------------------------------------------

namespace {

// One registration serving every parse shape, keyed on page_id.
constexpr const char* kParsePages = R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    if page_id == "" then
      return {
        title = "CHOOSE A GAME",
        lines = { "The Gamesmaster opens the book.", "Choose." },
        entries = {
          { id = "tdm", label = "TEAM DEATHMATCH", kind = "page", note = "6 arenas" },
          { id = "300", label = "THE CIRCLE", kind = "level", level = 300, note = "4 teams", replay = true },
          { id = "buy_kit", label = "FIELD KIT  60g", kind = "action", cost = 60 },
          { id = "bare", kind = "level" },
        },
      }
    end
    if page_id == "clip" then
      local lines = {}
      for i = 1, 8 do
        table.insert(lines, "line " .. i)
      end
      local entries = {}
      for i = 1, 30 do
        table.insert(entries, { id = "e" .. i, kind = "page" })
      end
      return { title = "CLIP", lines = lines, entries = entries }
    end
    if page_id == "nil" then
      return nil
    end
    if page_id == "number" then
      return 7
    end
    if page_id == "notitle" then
      return { entries = {} }
    end
    if page_id == "noid" then
      return { title = "X", entries = { { kind = "page" } } }
    end
    if page_id == "badkind" then
      return { title = "X", entries = { { id = "a", kind = "warp" } } }
    end
    if page_id == "entrynotable" then
      return { title = "X", entries = { 5 } }
    end
    if page_id == "badline" then
      return { title = "X", lines = { 1 } }
    end
    if page_id == "badlevel" then
      return { title = "X", entries = { { id = "a", kind = "level", level = "high" } } }
    end
    if page_id == "badreplay" then
      return { title = "X", entries = { { id = "a", kind = "level", level = 1, replay = 1 } } }
    end
    if page_id == "boom" then
      error("boom")
    end
    return { title = "EMPTY" }
  end,
}))LUA";

}  // namespace

TEST_F(CampaignHooksTest, page_parse_happy_path)
{
    register_script(kParsePages);
    hooks::CampaignPage page;
    ASSERT_TRUE(hooks::campaign_picker_page("", page));
    EXPECT_EQ("CHOOSE A GAME", page.title);
    ASSERT_EQ(2u, page.lines.size());
    EXPECT_EQ("The Gamesmaster opens the book.", page.lines[0]);
    ASSERT_EQ(4u, page.entries.size());
    EXPECT_EQ("tdm", page.entries[0].id);
    EXPECT_EQ("TEAM DEATHMATCH", page.entries[0].label);
    EXPECT_EQ(hooks::CampaignPageEntry::Kind::Page, page.entries[0].kind);
    EXPECT_EQ("6 arenas", page.entries[0].note);
    EXPECT_EQ(hooks::CampaignPageEntry::Kind::Level, page.entries[1].kind);
    EXPECT_EQ(300, page.entries[1].level);
    EXPECT_TRUE(page.entries[1].replay) << "#207: the replay mark parses";
    EXPECT_EQ(hooks::CampaignPageEntry::Kind::Action, page.entries[2].kind);
    EXPECT_EQ(60, page.entries[2].cost);
    // Optional fields default cleanly.
    EXPECT_EQ("", page.entries[3].label);
    EXPECT_EQ("", page.entries[3].note);
    EXPECT_EQ(0, page.entries[3].level);
    EXPECT_EQ(0, page.entries[3].cost);
    EXPECT_FALSE(page.entries[3].replay) << "absent replay defaults false";
    // A page with no lines/entries is legal.
    hooks::CampaignPage empty;
    ASSERT_TRUE(hooks::campaign_picker_page("other", empty));
    EXPECT_EQ("EMPTY", empty.title);
    EXPECT_TRUE(empty.lines.empty());
    EXPECT_TRUE(empty.entries.empty());
    EXPECT_TRUE(vm_errors().empty()) << vm_errors().front().message;
}

TEST_F(CampaignHooksTest, page_parse_clips_entries_and_lines)
{
    register_script(kParsePages);
    hooks::CampaignPage page;
    ASSERT_TRUE(hooks::campaign_picker_page("clip", page));
    ASSERT_EQ(6u, page.lines.size());
    EXPECT_EQ("line 6", page.lines[5]);
    ASSERT_EQ(24u, page.entries.size());
    EXPECT_EQ("e1", page.entries[0].id);
    EXPECT_EQ("e24", page.entries[23].id);
}

TEST_F(CampaignHooksTest, malformed_pages_answer_no_scripted_picker)
{
    register_script(kParsePages);
    hooks::CampaignPage page;
    const struct {
        const char* page_id;
        const char* named;
    } cases[] = {
        {"nil", "not a page table"},
        {"number", "not a page table"},
        {"notitle", "'title' is missing"},
        {"noid", ".id is missing"},
        {"badkind", ".kind must be"},
        {"entrynotable", "entries[1] is not a table"},
        {"badline", "lines[1] is not a string"},
        {"badlevel", ".level is not an integer"},
        {"badreplay", ".replay is not a boolean"},
    };
    for (const auto& c : cases) {
        EXPECT_FALSE(hooks::campaign_picker_page(c.page_id, page))
            << c.page_id;
        EXPECT_TRUE(errors_contain(c.named)) << c.page_id;
    }
    // An erroring hook is also "no scripted picker" for that dispatch.
    EXPECT_FALSE(hooks::campaign_picker_page("boom", page));
    EXPECT_TRUE(errors_contain("boom"));
    // The registration itself is still healthy.
    EXPECT_TRUE(hooks::campaign_picker_registered());
    EXPECT_TRUE(hooks::campaign_picker_page("", page));
}

// ---------------------------------------------------------------------------
// Action dispatch
// ---------------------------------------------------------------------------

TEST_F(CampaignHooksTest, action_dispatch_and_message)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    if entry_id == "buy_kit" then
      return { message = "Kit stowed for the road." }
    end
    if entry_id == "silent" then
      return nil
    end
    if entry_id == "notable" then
      return 12
    end
    error("action exploded")
  end,
}))LUA");
    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("buy_kit", result));
    EXPECT_TRUE(result.ok);
    EXPECT_EQ("Kit stowed for the road.", result.message);

    ASSERT_TRUE(hooks::campaign_picker_action("silent", result));
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.message.empty());

    // A non-table return is treated as "no toast" (the action already ran).
    ASSERT_TRUE(hooks::campaign_picker_action("notable", result));
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.message.empty());

    // Hook error: dispatched, ok=false (a spend already applied sticks).
    ASSERT_TRUE(hooks::campaign_picker_action("kaboom", result));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.empty());
    EXPECT_TRUE(errors_contain("action exploded"));

    // No picker_menu registered: pages answer false, actions still serve.
    hooks::CampaignPage page;
    EXPECT_FALSE(hooks::campaign_picker_page("", page));
}

// D3: the optional `level` an action answers with. In-range values ride
// through beside the message; everything else — absent, negative, past the
// loader's id ceiling, wrapped past 64 bits, or not an integer at all —
// stays the "no level carried" default (-1), never an error.
TEST_F(CampaignHooksTest, action_result_level_parse_and_bounds)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_action = function(entry_id)
    if entry_id == "plain" then
      return { level = 305, message = "Rolled." }
    end
    if entry_id == "zero" then
      return { level = 0 }
    end
    if entry_id == "ceiling" then
      return { level = 32767 }
    end
    if entry_id == "negative" then
      return { level = -3 }
    end
    if entry_id == "past_ceiling" then
      return { level = 32768 }
    end
    if entry_id == "wrapped" then
      return { level = 4294967301 }
    end
    if entry_id == "not_integer" then
      return { level = "three" }
    end
    return { message = "No level here." }
  end,
}))LUA");

    hooks::CampaignActionResult result;
    ASSERT_TRUE(hooks::campaign_picker_action("plain", result));
    EXPECT_EQ(305, result.level);
    EXPECT_EQ("Rolled.", result.message) << "level and message coexist";

    ASSERT_TRUE(hooks::campaign_picker_action("zero", result));
    EXPECT_EQ(0, result.level) << "0 is a legal scenario id";
    ASSERT_TRUE(hooks::campaign_picker_action("ceiling", result));
    EXPECT_EQ(32767, result.level);

    const char* rejected[] = {"negative", "past_ceiling", "wrapped",
                              "not_integer", "no_level"};
    for (const char* id : rejected)
    {
        ASSERT_TRUE(hooks::campaign_picker_action(id, result)) << id;
        EXPECT_TRUE(result.ok) << id;
        EXPECT_EQ(-1, result.level) << id << ": out-of-range or absent "
                                            "levels read as none carried";
    }
}

TEST_F(CampaignHooksTest, action_without_registration_answers_false)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    return { title = "MENU ONLY" }
  end,
}))LUA");
    hooks::CampaignActionResult result;
    EXPECT_FALSE(hooks::campaign_picker_action("x", result));
    EXPECT_TRUE(hooks::campaign_picker_registered());
}

// ---------------------------------------------------------------------------
// Base Camp zone parse (docs/basecamp-zones-design.md "The widget contract")
// ---------------------------------------------------------------------------

namespace {

// One base_camp registration serving every parse shape. The hook takes no
// argument, so the shape is selected through an installed state_get
// provider (which also proves og.campaign_* bindings work inside a
// base_camp dispatch).
constexpr const char* kZoneShapes = R"LUA(og.register_campaign_hooks({
  base_camp = function()
    local shape = og.campaign_state_get("shape")
    if shape == 0 then
      return {
        widgets = {
          { kind = "readout", weight = 1,
            items = {
              { label = "WAGES", value = "1400g" },
              { label = "DEBT", value = "900g" },
              { label = "COINS", value = "3" },
            } },
          { kind = "text",
            lines = { "The season turns.", "Collectors at the Toll." } },
          { kind = "roster", weight = 5, can_hire = false, can_team = false,
            locks = {
              { tag = 2, reason = "WITH THE BEARER" },
              { unset = true, reason = "WAITS AT THE FALLS" },
            },
            assign = { key = "falls_road", labels = { "WAR", "BURDEN" },
                       frozen = "The Falls parted the company." } },
          { kind = "actions",
            entries = {
              { id = "ride", kind = "level", level = 12, label = "RIDE ON" },
              { id = "stores", kind = "page", label = "STORES" },
              { id = "coin", kind = "action", cost = 150, label = "PASS" },
            } },
        },
      }
    end
    if shape == 1 then
      return { widgets = { { kind = "roster" } } }
    end
    if shape == 2 then
      return {}
    end
    if shape == 3 then
      return nil
    end
    if shape == 4 then
      return { widgets = { {}, {}, {}, {}, {}, {}, {} } }
    end
    if shape == 5 then
      return { widgets = { { kind = "roster" }, { kind = "roster" } } }
    end
    if shape == 6 then
      return { widgets = { { kind = "text" } } }
    end
    if shape == 7 then
      return { widgets = { { kind = "roster" }, { kind = "readout" },
                           { kind = "readout" } } }
    end
    if shape == 8 then
      return { widgets = { { kind = "roster" }, { kind = "actions" },
                           { kind = "actions" }, { kind = "actions" } } }
    end
    if shape == 9 then
      return { widgets = { { kind = "roster" }, { kind = "text" },
                           { kind = "text" }, { kind = "text" } } }
    end
    if shape == 10 then
      local first = {}
      for i = 1, 9 do
        table.insert(first, { id = "a" .. i, kind = "action" })
      end
      local second = {}
      for i = 1, 8 do
        table.insert(second, { id = "b" .. i, kind = "action" })
      end
      return { widgets = { { kind = "roster" },
                           { kind = "actions", entries = first },
                           { kind = "actions", entries = second } } }
    end
    if shape == 11 then
      local lines = {}
      for i = 1, 7 do
        table.insert(lines, "line " .. i)
      end
      return { widgets = { { kind = "roster" },
                           { kind = "text", lines = lines } } }
    end
    if shape == 12 then
      local items = {}
      for i = 1, 4 do
        table.insert(items, { label = "L" .. i, value = "V" .. i })
      end
      return { widgets = { { kind = "roster" },
                           { kind = "readout", items = items } } }
    end
    if shape == 13 then
      return { widgets = { { kind = "roster",
                             locks = { { tag = 1, unset = true } } } } }
    end
    if shape == 14 then
      return { widgets = { { kind = "roster",
                             locks = { { reason = "nobody" } } } } }
    end
    if shape == 15 then
      return { widgets = { { kind = "roster",
                             locks = { { tag = 0 } } } } }
    end
    if shape == 16 then
      return { widgets = { { kind = "roster",
                             locks = { { tag = 256 } } } } }
    end
    if shape == 17 then
      return { widgets = { { kind = "roster",
                             assign = { key = "road",
                                        labels = { "WAR" } } } } }
    end
    if shape == 18 then
      return { widgets = { { kind = "roster",
                             assign = { key = "BadKey",
                                        labels = { "A", "B" } } } } }
    end
    if shape == 19 then
      return { widgets = { { kind = "banner" } } }
    end
    if shape == 20 then
      return { widgets = { { kind = "roster", weight = -1 } } }
    end
    if shape == 21 then
      error("zone boom")
    end
    if shape == 22 then
      return { widgets = { { kind = "roster", can_deploy = 1 } } }
    end
    if shape == 23 then
      return { widgets = { { kind = "roster" },
                           { kind = "actions",
                             entries = { { kind = "action" } } } } }
    end
    if shape == 24 then
      local locks = {}
      for i = 1, 25 do
        table.insert(locks, { tag = 1 })
      end
      return { widgets = { { kind = "roster", locks = locks } } }
    end
    if shape == 25 then
      return { widgets = { { kind = "roster", weight = "heavy" } } }
    end
    if shape == 26 then
      return { widgets = { { kind = "readout",
                             items = { { label = "L" } } },
                           { kind = "roster" } } }
    end
    -- Element/field TYPE guards. Each malformed widget is widgets[1] and is
    -- followed by a valid roster, so the per-kind caps would pass: only the
    -- widget-loop rejection can be what refuses these.
    if shape == 27 then
      return { widgets = { 5, { kind = "roster" } } }
    end
    if shape == 28 then
      return { widgets = { { kind = "roster", locks = "nope" } } }
    end
    if shape == 29 then
      return { widgets = { { kind = "roster", locks = { 5 } } } }
    end
    if shape == 30 then
      return { widgets = { { kind = "roster",
                             locks = { { tag = "two" } } } } }
    end
    if shape == 31 then
      return { widgets = { { kind = "roster",
                             locks = { { tag = 1, unset = 1 } } } } }
    end
    if shape == 32 then
      return { widgets = { { kind = "roster",
                             locks = { { tag = 1, reason = 7 } } } } }
    end
    if shape == 33 then
      return { widgets = { { kind = "roster", assign = "x" } } }
    end
    if shape == 34 then
      return { widgets = { { kind = "roster",
                             assign = { key = "road",
                                        labels = { "", "B" } } } } }
    end
    if shape == 35 then
      return { widgets = { { kind = "roster",
                             assign = { key = "road",
                                        labels = { "A", "B" },
                                        frozen = 3 } } } }
    end
    if shape == 36 then
      return { widgets = { { kind = "text", lines = "one" },
                           { kind = "roster" } } }
    end
    if shape == 37 then
      return { widgets = { { kind = "text", lines = { 5 } },
                           { kind = "roster" } } }
    end
    if shape == 38 then
      return { widgets = { { kind = "actions", entries = "go" },
                           { kind = "roster" } } }
    end
    if shape == 39 then
      return { widgets = { { kind = "actions", entries = { 5 } },
                           { kind = "roster" } } }
    end
    if shape == 40 then
      return { widgets = { { kind = "readout", items = "x" },
                           { kind = "roster" } } }
    end
    if shape == 41 then
      return { widgets = { { kind = "readout", items = { 5 } },
                           { kind = "roster" } } }
    end
    if shape == 42 then
      return { widgets = { { kind = "readout",
                             items = { { value = "V" } } },
                           { kind = "roster" } } }
    end
    -- Both sides of the weight ceiling: the whole band (8 units) parses,
    -- one unit past it is a named rejection.
    if shape == 43 then
      return { widgets = { { kind = "roster", weight = 8 } } }
    end
    if shape == 44 then
      return { widgets = { { kind = "roster", weight = 9 } } }
    end
    if shape == 45 then
      return { widgets = { { kind = "roster" },
                           { kind = "text", weight = 9,
                             lines = { "over the band" } } } }
    end
    return { widgets = { { kind = "roster" } } }
  end,
  picker_menu = function(page_id)
    return { title = "STILL SERVING" }
  end,
}))LUA";

}  // namespace

TEST_F(CampaignHooksTest, zone_parse_happy_path)
{
    int shape = 0;
    hooks::CampaignProviders providers;
    providers.state_get = [&shape](const std::string&) -> std::int32_t {
        return shape;
    };
    hooks::install_campaign_providers(std::move(providers));
    register_script(kZoneShapes);
    EXPECT_TRUE(hooks::campaign_zone_registered());

    hooks::CampaignZone zone;
    ASSERT_TRUE(hooks::campaign_zone(zone));
    ASSERT_EQ(4u, zone.widgets.size());

    const hooks::CampaignZoneWidget& readout = zone.widgets[0];
    EXPECT_EQ(hooks::CampaignZoneWidget::Kind::Readout, readout.kind);
    EXPECT_EQ(1, readout.weight);
    ASSERT_EQ(3u, readout.items.size());
    EXPECT_EQ("WAGES", readout.items[0].label);
    EXPECT_EQ("1400g", readout.items[0].value);
    EXPECT_EQ("COINS", readout.items[2].label);
    EXPECT_EQ("3", readout.items[2].value);

    const hooks::CampaignZoneWidget& text = zone.widgets[1];
    EXPECT_EQ(hooks::CampaignZoneWidget::Kind::Text, text.kind);
    EXPECT_EQ(0, text.weight) << "weight defaults to 0 (layout default)";
    ASSERT_EQ(2u, text.lines.size());
    EXPECT_EQ("The season turns.", text.lines[0]);

    const hooks::CampaignZoneWidget& roster = zone.widgets[2];
    EXPECT_EQ(hooks::CampaignZoneWidget::Kind::Roster, roster.kind);
    EXPECT_EQ(5, roster.weight);
    EXPECT_TRUE(roster.can_deploy);
    EXPECT_TRUE(roster.can_train);
    EXPECT_TRUE(roster.can_reorder);
    EXPECT_FALSE(roster.can_team);
    EXPECT_FALSE(roster.can_hire);
    ASSERT_EQ(2u, roster.locks.size());
    EXPECT_EQ(2, roster.locks[0].tag);
    EXPECT_FALSE(roster.locks[0].unset);
    EXPECT_EQ("WITH THE BEARER", roster.locks[0].reason);
    EXPECT_EQ(-1, roster.locks[1].tag);
    EXPECT_TRUE(roster.locks[1].unset);
    EXPECT_EQ("WAITS AT THE FALLS", roster.locks[1].reason);
    EXPECT_TRUE(roster.assign.active);
    EXPECT_EQ("falls_road", roster.assign.key);
    ASSERT_EQ(2u, roster.assign.labels.size());
    EXPECT_EQ("WAR", roster.assign.labels[0]);
    EXPECT_EQ("BURDEN", roster.assign.labels[1]);
    EXPECT_EQ("The Falls parted the company.", roster.assign.frozen);

    const hooks::CampaignZoneWidget& actions = zone.widgets[3];
    EXPECT_EQ(hooks::CampaignZoneWidget::Kind::Actions, actions.kind);
    ASSERT_EQ(3u, actions.entries.size());
    EXPECT_EQ("ride", actions.entries[0].id);
    EXPECT_EQ(hooks::CampaignPageEntry::Kind::Level,
              actions.entries[0].kind);
    EXPECT_EQ(12, actions.entries[0].level);
    EXPECT_EQ("RIDE ON", actions.entries[0].label);
    EXPECT_EQ(hooks::CampaignPageEntry::Kind::Page,
              actions.entries[1].kind);
    EXPECT_EQ(hooks::CampaignPageEntry::Kind::Action,
              actions.entries[2].kind);
    EXPECT_EQ(150, actions.entries[2].cost);
    EXPECT_TRUE(vm_errors().empty()) << vm_errors().front().message;

    // Shape 1: a bare roster widget keeps every default.
    shape = 1;
    hooks::CampaignZone minimal;
    ASSERT_TRUE(hooks::campaign_zone(minimal));
    ASSERT_EQ(1u, minimal.widgets.size());
    const hooks::CampaignZoneWidget& bare = minimal.widgets[0];
    EXPECT_EQ(hooks::CampaignZoneWidget::Kind::Roster, bare.kind);
    EXPECT_EQ(0, bare.weight);
    EXPECT_TRUE(bare.can_deploy);
    EXPECT_TRUE(bare.can_train);
    EXPECT_TRUE(bare.can_reorder);
    EXPECT_TRUE(bare.can_team);
    EXPECT_TRUE(bare.can_hire);
    EXPECT_TRUE(bare.locks.empty());
    EXPECT_FALSE(bare.assign.active);
    EXPECT_TRUE(bare.lines.empty());
    EXPECT_TRUE(bare.entries.empty());
    EXPECT_TRUE(bare.items.empty());
}

TEST_F(CampaignHooksTest, zone_bounds_and_malformed_shapes_fall_to_default)
{
    int shape = 0;
    hooks::CampaignProviders providers;
    providers.state_get = [&shape](const std::string&) -> std::int32_t {
        return shape;
    };
    hooks::install_campaign_providers(std::move(providers));
    register_script(kZoneShapes);

    const struct {
        int shape;
        const char* named;
    } cases[] = {
        {2, "'widgets' is missing or not an array"},
        {3, "returned a nil, not a zone table"},
        {4, "lists 7 widgets (max 6)"},
        {5, "declares 2 roster widgets (exactly one required)"},
        {6, "declares 0 roster widgets (exactly one required)"},
        {7, "declares 2 readout widgets (max 1)"},
        {8, "declares 3 actions widgets (max 2)"},
        {9, "declares 3 text widgets (max 2)"},
        {10, "the zone lists more than 16 action rows in total"},
        {11, "widgets[2] lists 7 lines (max 6)"},
        {12, "widgets[2] lists 4 readout items (max 3)"},
        {13, "locks[1] needs exactly one of tag / unset = true"},
        {14, "locks[1] needs exactly one of tag / unset = true"},
        {15, "locks[1].tag must be 1..255"},
        {16, "locks[1].tag must be 1..255"},
        {17, ".assign.labels must list exactly 2 labels"},
        {18, ".assign.key must be 1-32 chars of [a-z0-9_]"},
        {19, ".kind must be \"roster\", \"text\", \"actions\" or "
             "\"readout\""},
        {20, "widgets[1].weight is negative"},
        {22, "widgets[1].can_deploy is not a boolean"},
        {23, "widgets[2].entries[1].id is missing or not a string"},
        {24, ".locks lists 25 locks (max 24)"},
        {25, "widgets[1].weight is not an integer"},
        {26, "widgets[1].items[1].value is missing or not a string"},
        // Element/field TYPE guards. Nine of these are the only thing
        // between a malformed book and a lua_rawget/lua_rawlen on a
        // non-table: api_check is compiled out, so a dropped guard is a
        // garbage Table* dereference, not a Lua error.
        {27, "widgets[1] is not a table"},
        {28, "widgets[1].locks is not an array"},
        {29, "widgets[1].locks[1] is not a table"},
        {30, "widgets[1].locks[1].tag is not an integer"},
        {31, "widgets[1].locks[1].unset is not a boolean"},
        {32, "widgets[1].locks[1].reason is not a string"},
        {33, "widgets[1].assign is not a table"},
        {34, "widgets[1].assign.labels[1] must be a non-empty string"},
        {35, "widgets[1].assign.frozen is not a string"},
        {36, "widgets[1].lines is not an array"},
        {37, "widgets[1].lines[1] is not a string"},
        {38, "widgets[1].entries is not an array"},
        {39, "widgets[1].entries[1] is not a table"},
        {40, "widgets[1].items is not an array"},
        {41, "widgets[1].items[1] is not a table"},
        {42, "widgets[1].items[1].label is missing or not a string"},
        // The weight ceiling, upper side (the accepted side is asserted
        // below): a widget cannot ask for more row units than the whole
        // 8-unit band, whatever its kind.
        {44, "widgets[1].weight is 9 row units (max 8)"},
        {45, "widgets[2].weight is 9 row units (max 8)"},
    };
    hooks::CampaignZone zone;
    for (const auto& c : cases) {
        shape = c.shape;
        EXPECT_FALSE(hooks::campaign_zone(zone)) << "shape " << c.shape;
        EXPECT_TRUE(errors_contain(c.named))
            << "shape " << c.shape << " should record: " << c.named;
    }
    // The bound's ACCEPTED side: exactly the band parses, so the rejection
    // above is a ceiling and not an off-by-one that also refuses the only
    // composition a full-height widget can ever have.
    shape = 43;
    ASSERT_TRUE(hooks::campaign_zone(zone))
        << "weight == the whole band must parse";
    ASSERT_EQ(1u, zone.widgets.size());
    EXPECT_EQ(hooks::kCampaignZoneMaxWeight, zone.widgets[0].weight);
    // An erroring hook is also "no scripted zone" for that dispatch.
    shape = 21;
    EXPECT_FALSE(hooks::campaign_zone(zone));
    EXPECT_TRUE(errors_contain("zone boom"));
    // The registration itself is still healthy: the zone serves again and
    // the picker page surface never flinched.
    shape = 1;
    EXPECT_TRUE(hooks::campaign_zone(zone));
    hooks::CampaignPage page;
    ASSERT_TRUE(hooks::campaign_picker_page("", page));
    EXPECT_EQ("STILL SERVING", page.title);
}

TEST_F(CampaignHooksTest, zone_dispatch_is_fenced_and_brackets_cleanly)
{
    register_script(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    local ok, err = pcall(og.rand, 3)
    if ok then
      og.log("NOFENCE rand")
    elseif string.find(err, "campaign hooks", 1, true) == nil then
      og.log("WRONGMSG rand: " .. err)
    end
    local ok2 = pcall(og.register_campaign_hooks, {})
    if ok2 then
      og.log("NOFENCE register")
    end
    return { widgets = { { kind = "roster" } } }
  end,
  picker_menu = function(page_id)
    return { title = "AFTER" }
  end,
}))LUA");
    hooks::CampaignZone zone;
    ASSERT_TRUE(hooks::campaign_zone(zone));
    for (const std::string& line : vm_log()) {
        EXPECT_EQ(std::string::npos, line.find("NOFENCE")) << line;
        EXPECT_EQ(std::string::npos, line.find("WRONGMSG")) << line;
    }
    // The fence disarmed on exit: a follow-up picker dispatch (its own
    // bracket) and a second zone dispatch both serve.
    hooks::CampaignPage page;
    ASSERT_TRUE(hooks::campaign_picker_page("", page));
    EXPECT_EQ("AFTER", page.title);
    ASSERT_TRUE(hooks::campaign_zone(zone));
    ASSERT_EQ(1u, zone.widgets.size());
}

// ---------------------------------------------------------------------------
// Sim-side og.campaign_var
// ---------------------------------------------------------------------------

TEST_F(CampaignHooksTest, campaign_var_reads_world_values)
{
    GameWorld world(7);
    world.id = 42;
    world.campaign_vars.emplace_back("delve_counted", 3);
    world.campaign_vars.emplace_back("watch_paid", -2);
    GameplayContext context{};
    og::sim::SimEventLog events;
    context.world = &world;
    context.sim_events = &events;
    current_game = &context;

    register_script(R"LUA(og.register_level_hooks(42, {
  on_load = function(level)
    og.log("var " .. og.campaign_var("delve_counted"))
    og.log("neg " .. og.campaign_var("watch_paid"))
    og.log("missing " .. og.campaign_var("nope"))
  end,
}))LUA");
    world.tick();
    const std::vector<std::string>& log = world.scripts().host().log();
    ASSERT_EQ(3u, log.size());
    EXPECT_EQ("var 3", log[0]);
    EXPECT_EQ("neg -2", log[1]);
    EXPECT_EQ("missing 0", log[2]);
    current_game = nullptr;
}

// ---------------------------------------------------------------------------
// The LINEUP hook (docs/lineup-design.md §3.3, §4)
// ---------------------------------------------------------------------------

TEST_F(CampaignHooksTest, lineup_registers_and_prices_a_fighter)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = {
    power = function(row)
      return row.hp * 2 + row.level
    end,
  },
}))LUA");
    EXPECT_TRUE(hooks::campaign_picker_registered());
    EXPECT_TRUE(hooks::campaign_lineup_registered());

    hooks::LineupPowerRow row;
    row.family = "SOLDIER";
    row.level = 7;
    row.hp = 30;
    long long power = -1;
    ASSERT_TRUE(hooks::campaign_fighter_power(row, power));
    EXPECT_EQ(67, power);
    EXPECT_TRUE(vm_errors().empty()) << vm_errors().front().message;
}

TEST_F(CampaignHooksTest, lineup_power_reads_the_whole_row)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = { power = function(row)
    return row.hp + row.mp + row.armor + row.damage + row.stepsize +
           row.fire_frequency + row.level +
           (row.family == "MAGE" and 4 or 0)
  end },
}))LUA");
    hooks::LineupPowerRow row;
    row.family = "MAGE";  // 4
    row.level = 1;
    row.hp = 2;
    row.mp = 4;
    row.armor = 8;
    row.damage = 16;
    row.stepsize = 32;
    row.fire_frequency = 64;
    long long power = 0;
    ASSERT_TRUE(hooks::campaign_fighter_power(row, power));
    EXPECT_EQ(131, power);
}

// Amendment B1 deleted `lineup.presets`: the five-value FILL wheel names
// nothing a campaign owns, so a book that still writes the old key is
// refused outright rather than half-registered.
TEST_F(CampaignHooksTest, lineup_presets_key_is_gone)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = { presets = { "BRUTES" }, power = function(row) return 1 end },
}))LUA");
    EXPECT_FALSE(hooks::campaign_lineup_registered());
    EXPECT_TRUE(errors_contain("unknown 'lineup' key 'presets'"));
}

TEST_F(CampaignHooksTest, lineup_power_alone_is_the_whole_table)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = { power = function(row) return 1 end },
}))LUA");
    EXPECT_TRUE(hooks::campaign_lineup_registered());
    hooks::LineupPowerRow row;
    long long power = 0;
    EXPECT_TRUE(hooks::campaign_fighter_power(row, power));
}

TEST_F(CampaignHooksTest, no_lineup_hook_means_no_lineup)
{
    register_script(R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id) return { title = "BOOK" } end,
}))LUA");
    EXPECT_TRUE(hooks::campaign_picker_registered());
    EXPECT_FALSE(hooks::campaign_lineup_registered());
    hooks::LineupPowerRow row;
    long long power = 0;
    EXPECT_FALSE(hooks::campaign_fighter_power(row, power));
}

TEST_F(CampaignHooksTest, lineup_power_that_errors_answers_false)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = { power = function(row) return nil + 1 end },
}))LUA");
    hooks::LineupPowerRow row;
    long long power = 12345;
    EXPECT_FALSE(hooks::campaign_fighter_power(row, power));
    EXPECT_EQ(12345, power) << "the caller's value is untouched";
    EXPECT_FALSE(vm_errors().empty());
}

TEST_F(CampaignHooksTest, lineup_power_that_returns_a_non_number_answers_false)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = { power = function(row) return "4200" end },
}))LUA");
    hooks::LineupPowerRow row;
    long long power = 0;
    EXPECT_FALSE(hooks::campaign_fighter_power(row, power));
    EXPECT_TRUE(errors_contain("not a number"));
}

TEST_F(CampaignHooksTest, lineup_is_fenced_like_every_other_campaign_hook)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = { power = function(row) return og.rand(10) end },
}))LUA");
    hooks::LineupPowerRow row;
    long long power = 0;
    EXPECT_FALSE(hooks::campaign_fighter_power(row, power));
    EXPECT_FALSE(vm_errors().empty())
        << "the sim RNG stays fenced under campaign dispatch";
}

TEST_F(CampaignHooksTest, lineup_registration_rejections)
{
    struct Case {
        const char* source;
        const char* needle;
    };
    const Case cases[] = {
        {R"LUA(og.register_campaign_hooks({ lineup = 7 }))LUA",
         "'lineup' must be a table"},
        {R"LUA(og.register_campaign_hooks({ lineup = { power = 7 } }))LUA",
         "'lineup.power' must be a function"},
        {R"LUA(og.register_campaign_hooks({ lineup = { presets = "X" } }))LUA",
         "unknown 'lineup' key 'presets'"},
        {R"LUA(og.register_campaign_hooks({ lineup = { powr = 1 } }))LUA",
         "unknown 'lineup' key 'powr'"},
        {R"LUA(og.register_campaign_hooks({ lineup = {} }))LUA",
         "carries no 'power'"},
    };
    for (const Case& c : cases) {
        clear_pack_scripts();
        register_script(c.source);
        EXPECT_FALSE(hooks::campaign_lineup_registered()) << c.needle;
        EXPECT_TRUE(errors_contain(c.needle)) << c.needle;
    }
}

TEST_F(CampaignHooksTest, lineup_alone_is_a_whole_registration)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = { power = function(row) return 1 end },
}))LUA");
    EXPECT_TRUE(hooks::campaign_picker_registered())
        << "a book with only a lineup table is still a book";
    EXPECT_TRUE(vm_errors().empty()) << vm_errors().front().message;
}

TEST_F(CampaignHooksTest, lineup_power_for_guy_bridges_the_engine_stats)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = { power = function(row) return row.hp * 2 + row.level end },
}))LUA");
    guy fighter(FAMILY_SOLDIER);
    fighter.level = 5;
    const og::ui::DerivedStats stats = og::ui::compute_derived_stats(fighter);
    const std::optional<long long> power =
        og::ui::lineup_power_for_guy(fighter);
    ASSERT_TRUE(power.has_value());
    EXPECT_EQ(static_cast<long long>(stats.hp) * 2 + 5, *power)
        << "the row carries the engine's own derived stats";
}

TEST_F(CampaignHooksTest, lineup_power_for_guy_is_nothing_without_a_hook)
{
    register_script(R"LUA(og.log("a campaign with no lineup"))LUA");
    guy fighter(FAMILY_SOLDIER);
    EXPECT_FALSE(og::ui::lineup_power_for_guy(fighter).has_value());
}

// ---------------------------------------------------------------------------
// The lineup review rows (wp/review-lua L5/L6)
// ---------------------------------------------------------------------------

// L5 (the presets-sequence refusal) retired with `lineup.presets` itself
// (amendment B1): the key is unknown now, so the registrar refuses every
// shape of it — pinned by lineup_presets_key_is_gone and the unknown-key
// row in lineup_registration_rejections above.

// L6: lineup.power answers are read as int64 only when they are one — an
// integer, or a finite float inside the int64 range (truncated). NaN,
// either infinity, a float past the range and a string all answer false
// (the band shows `--`), never a static_cast of a NaN.
TEST_F(CampaignHooksTest, lineup_power_non_integer_answers)
{
    register_script(R"LUA(og.register_campaign_hooks({
  lineup = { power = function(row)
    if row.level == 1 then return 0/0 end
    if row.level == 2 then return 1/0 end
    if row.level == 3 then return 1.5 end
    if row.level == 4 then return -(1/0) end
    if row.level == 5 then return 2^63 end
    if row.level == 6 then return -2.5 end
    if row.level == 7 then return 4200 end
    return "x"
  end },
}))LUA");
    struct Case {
        int level;
        bool ok;
        long long value;
    };
    const Case cases[] = {
        {1, false, 0},  // NaN
        {2, false, 0},  // +inf
        {3, true, 1},   // 1.5 truncates
        {4, false, 0},  // -inf
        {5, false, 0},  // 2^63 is past int64
        {6, true, -2},  // -2.5 truncates toward zero
        {7, true, 4200},
        {8, false, 0},  // "x"
    };
    for (const Case& c : cases) {
        hooks::LineupPowerRow row;
        row.level = c.level;
        long long power = 12345;
        EXPECT_EQ(c.ok, hooks::campaign_fighter_power(row, power))
            << "level " << c.level;
        if (c.ok)
            EXPECT_EQ(c.value, power) << "level " << c.level;
        else
            EXPECT_EQ(12345, power) << "refused: the caller's value stands";
    }
    EXPECT_TRUE(errors_contain("not a finite integer"));
}
