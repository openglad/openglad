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

#include <algorithm>
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
        "register at least one of 'picker_menu' / 'picker_action'"));
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
        team.push_back({"MILO", "SOLDIER", 3, 220, 12, 8, 10, 6, 8, 0});
        team.push_back({"WYRD", "MAGE", 2, 90, 4, 7, 5, 14, 2, 1});
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
        "second WYRD 1",              "done5 true", "done6 false",
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
          { id = "300", label = "THE CIRCLE", kind = "level", level = 300, note = "4 teams" },
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
    EXPECT_EQ(hooks::CampaignPageEntry::Kind::Action, page.entries[2].kind);
    EXPECT_EQ(60, page.entries[2].cost);
    // Optional fields default cleanly.
    EXPECT_EQ("", page.entries[3].label);
    EXPECT_EQ("", page.entries[3].note);
    EXPECT_EQ(0, page.entries[3].level);
    EXPECT_EQ(0, page.entries[3].cost);
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
