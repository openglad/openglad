// Scripted-mode og.* binding coverage: every new binding is ENTERED through
// a real level-hook dispatch — success path and error arms both — against a
// live world. The error-arm cases pin the sandbox promise: a bad call raises
// a Lua error the pack can pcall, and the world is untouched.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>

#include "../test_game_world_fixture.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

loader& mode_test_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

// TestGameWorld wired to the production loader factory (real livings with
// stats) plus pack-script lifecycle management.
struct ModeBindingsWorld : TestGameWorld
{
    explicit ModeBindingsWorld(int level_id = 42)
        : TestGameWorld(level_id)
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        loader* game_loader = &mode_test_loader();
        world().entity_factory =
            [game_loader](Order order, std::int32_t family) {
                return game_loader->create_walker_owned(order, family);
            };
        world().entity_configurator =
            [game_loader](walker& entity, Order order,
                          std::int32_t family) -> const PixieData* {
                game_loader->set_walker(&entity, order, family);
                return game_loader->graphics_for(entity.query_order(),
                                                 entity.family());
            };
        world().entity_derived_stats =
            [game_loader](walker* entity, Order order, std::int32_t family) {
                if (entity != nullptr)
                    game_loader->set_derived_stats(entity, order, family);
            };
    }

    ~ModeBindingsWorld() { og::script::clear_pack_scripts(); }

    // Registers `body` as the on_load hook of level 42 and runs one tick.
    void run_on_load(const std::string& body)
    {
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.mode", "probe.lua",
             "og.register_level_hooks(42, {\n"
             "  on_load = function(level)\n" + body +
                 "\n  end,\n"
                 "})\n"});
        world().tick();
    }

    const std::vector<std::string>& vm_log()
    {
        return world().scripts().host().log();
    }

    bool logged(const std::string& needle)
    {
        for (const auto& line : vm_log())
        {
            if (line.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

    std::string script_errors()
    {
        std::string out;
        for (const auto& e : world().scripts().host().errors())
        {
            out += e.where;
            out += ": ";
            out += e.message;
            out += "\n";
        }
        for (const auto& line : vm_log())
        {
            out += "log: ";
            out += line;
            out += "\n";
        }
        return out;
    }

    walker* spawn_living(int family, int team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(ACT_CONTROL);
        return w;
    }

    walker* spawn_marker(int team, int x, int y)
    {
        walker* marker = world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (marker == nullptr)
            return nullptr;
        marker->setxy(static_cast<short>(x), static_cast<short>(y));
        marker->set_team_num(static_cast<unsigned char>(team));
        return marker;
    }
};

}  // namespace

TEST(ModeBindings, mode_get_set_roundtrip_and_index_errors)
{
    ModeBindingsWorld fx;
    fx.run_on_load(
        "    og.mode_set(0, 7)\n"
        "    og.mode_set(63, -3)\n"
        "    og.log('v0', og.mode_get(0))\n"
        "    og.log('v63', og.mode_get(63))\n"
        "    local ok1, e1 = pcall(og.mode_get, -1)\n"
        "    local ok2, e2 = pcall(og.mode_get, 64)\n"
        "    local ok3, e3 = pcall(og.mode_set, 64, 1)\n"
        "    og.log('errs', ok1 and 1 or 0, ok2 and 1 or 0, ok3 and 1 or 0)\n"
        "    og.log('msg', e1)\n");
    EXPECT_TRUE(fx.logged("v0\t7"));
    EXPECT_TRUE(fx.logged("v63\t-3"));
    EXPECT_TRUE(fx.logged("errs\t0\t0\t0"));
    EXPECT_TRUE(fx.logged("out of range"));
    EXPECT_EQ(7, fx.world().mode.vars[0]);
    EXPECT_EQ(-3, fx.world().mode.vars[63]);
}

TEST(ModeBindings, end_level_latches_and_validates)
{
    ModeBindingsWorld fx;
    fx.run_on_load(
        "    local ok1 = pcall(og.end_level, 2, 43)\n"
        "    local ok2 = pcall(og.end_level, 0, -2)\n"
        "    og.log('errs', ok1 and 1 or 0, ok2 and 1 or 0)\n"
        "    og.end_level(1, -1)\n"
        "    og.end_level(0, 43)\n");
    EXPECT_TRUE(fx.logged("errs\t0\t0"));
    // Last write wins.
    EXPECT_TRUE(fx.world().mode.win_latched);
    EXPECT_EQ(0, fx.world().mode.win_ending);
    EXPECT_EQ(43, fx.world().mode.win_next_level);
}

TEST(ModeBindings, declare_winner_records_team_and_validates)
{
    ModeBindingsWorld fx;
    EXPECT_EQ(-1, fx.world().mode.winner_team);
    fx.run_on_load(
        "    og.log('undecided', og.mode_winner())\n"
        "    local ok = pcall(og.declare_winner, 4)\n"
        "    og.log('err', ok and 1 or 0)\n"
        "    og.declare_winner(2)\n"
        "    og.log('winner', og.mode_winner())\n");
    EXPECT_TRUE(fx.logged("undecided\t-1"));
    EXPECT_TRUE(fx.logged("err\t0"));
    EXPECT_TRUE(fx.logged("winner\t2"));
    EXPECT_TRUE(fx.world().mode.win_latched);
    EXPECT_EQ(2, fx.world().mode.winner_team);
    // No myguy walker on team 2: a bot win latches the rematch shape.
    EXPECT_FALSE(fx.world().mode.winner_is_player);
    EXPECT_EQ(fx.world().id, fx.world().mode.win_next_level);
}

TEST(ModeBindings, set_mode_name_clamps_to_eleven_bytes)
{
    ModeBindingsWorld fx;
    fx.run_on_load("    og.set_mode_name('ONSLAUGHTXYZ99')\n");
    EXPECT_STREQ("ONSLAUGHTXY", fx.world().mode.name.data());
}

TEST(ModeBindings, hud_lines_write_clamp_clear_and_validate)
{
    ModeBindingsWorld fx;
    fx.run_on_load(
        "    og.set_hud_line(0, 'RED 2 CAPS', 0)\n"
        "    og.set_hud_line(3, '0123456789012345678901234567890')\n"
        "    local ok1 = pcall(og.set_hud_line, 4, 'x')\n"
        "    local ok2 = pcall(og.set_hud_line, 0, 'x', 9)\n"
        "    local ok3 = pcall(og.clear_hud_line, -1)\n"
        "    og.log('errs', ok1 and 1 or 0, ok2 and 1 or 0, ok3 and 1 or 0)\n"
        "    og.clear_hud_line(0)\n");
    EXPECT_TRUE(fx.logged("errs\t0\t0\t0"));
    // Slot 3 clamped to 25 chars.
    EXPECT_STREQ("0123456789012345678901234",
                 fx.world().mode.hud[3].text.data());
    EXPECT_EQ(255, fx.world().mode.hud[3].team);
    // Slot 0 was written with team 0 then cleared back to defaults. The
    // ok2 error arm must not have half-written it either.
    EXPECT_STREQ("", fx.world().mode.hud[0].text.data());
    EXPECT_EQ(255, fx.world().mode.hud[0].team);
}

TEST(ModeBindings, beacons_set_clear_and_validate)
{
    ModeBindingsWorld fx;
    walker* mutant = fx.spawn_living(FAMILY_SOLDIER, 1, 160, 160);
    ASSERT_NE(nullptr, mutant);
    const std::uint32_t id = mutant->entity_id();
    fx.run_on_load(
        "    local obs = og.oblist()\n"
        "    og.set_beacon(0, obs[1], 1)\n"
        "    og.set_beacon(1, obs[1])\n"
        "    local ok1 = pcall(og.set_beacon, 4, obs[1])\n"
        "    local ok2 = pcall(og.set_beacon, 0, obs[1], 7)\n"
        "    og.log('errs', ok1 and 1 or 0, ok2 and 1 or 0)\n"
        "    og.set_beacon(1, nil)\n");
    EXPECT_TRUE(fx.logged("errs\t0\t0"));
    EXPECT_EQ(static_cast<std::int32_t>(id),
              fx.world().mode.beacons[0].entity_id);
    EXPECT_EQ(1, fx.world().mode.beacons[0].team);
    // Slot 1 was set with default team then cleared.
    EXPECT_EQ(0, fx.world().mode.beacons[1].entity_id);
    EXPECT_EQ(255, fx.world().mode.beacons[1].team);
}

TEST(ModeBindings, team_score_reads_award_scores_counter)
{
    ModeBindingsWorld fx;
    fx.world().m_score[2] = 1234;
    fx.run_on_load(
        "    og.log('score', og.team_score(2))\n"
        "    og.award_score(2, 6)\n"
        "    og.log('after', og.team_score(2))\n"
        "    local ok = pcall(og.team_score, 4)\n"
        "    og.log('err', ok and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("score\t1234"));
    EXPECT_TRUE(fx.logged("after\t1240"));
    EXPECT_TRUE(fx.logged("err\t0"));
}

TEST(ModeBindings, team_color_name_matches_engine_table)
{
    ModeBindingsWorld fx;
    fx.run_on_load(
        "    og.log('names', og.team_color_name(0), og.team_color_name(1),\n"
        "           og.team_color_name(2), og.team_color_name(3))\n"
        "    local ok = pcall(og.team_color_name, -1)\n"
        "    og.log('err', ok and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("names\tRED\tGREEN\tBLUE\tYELLOW"));
    EXPECT_TRUE(fx.logged("err\t0"));
}

// ---------------------------------------------------------------------------
// FFA fighter band 16-31 through the widened guards (docs/ffa-design.md §5)
// ---------------------------------------------------------------------------

TEST(ModeBindings, declare_winner_accepts_band_rejects_gap_and_beyond)
{
    ModeBindingsWorld fx;
    fx.run_on_load(
        "    local ok4 = pcall(og.declare_winner, 4)\n"
        "    local ok15 = pcall(og.declare_winner, 15)\n"
        "    local ok32 = pcall(og.declare_winner, 32)\n"
        "    local ok255 = pcall(og.declare_winner, 255)\n"
        "    og.log('errs', ok4 and 1 or 0, ok15 and 1 or 0,\n"
        "           ok32 and 1 or 0, ok255 and 1 or 0)\n"
        "    og.declare_winner(31)\n"
        "    og.log('winner', og.mode_winner())\n");
    EXPECT_TRUE(fx.logged("errs\t0\t0\t0\t0"));
    EXPECT_TRUE(fx.logged("winner\t31"));
    EXPECT_TRUE(fx.world().mode.win_latched);
    EXPECT_EQ(31, fx.world().mode.winner_team);
    // No myguy walker on band byte 31: the bot-win rematch shape holds.
    EXPECT_FALSE(fx.world().mode.winner_is_player);
    EXPECT_EQ(fx.world().id, fx.world().mode.win_next_level);
}

TEST(ModeBindings, hud_line_accepts_band_tint_keeps_nil_default)
{
    ModeBindingsWorld fx;
    fx.run_on_load(
        "    og.set_hud_line(0, 'LEADER', 16)\n"
        "    og.set_hud_line(1, 'PLAIN')\n"
        "    local ok4 = pcall(og.set_hud_line, 2, 'x', 4)\n"
        "    local ok15 = pcall(og.set_hud_line, 2, 'x', 15)\n"
        "    local ok32 = pcall(og.set_hud_line, 2, 'x', 32)\n"
        "    og.log('errs', ok4 and 1 or 0, ok15 and 1 or 0,\n"
        "           ok32 and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("errs\t0\t0\t0"));
    EXPECT_STREQ("LEADER", fx.world().mode.hud[0].text.data());
    EXPECT_EQ(16, fx.world().mode.hud[0].team);
    // The nil-team default stays 255.
    EXPECT_EQ(255, fx.world().mode.hud[1].team);
    // The error arms left slot 2 untouched.
    EXPECT_STREQ("", fx.world().mode.hud[2].text.data());
    EXPECT_EQ(255, fx.world().mode.hud[2].team);
}

TEST(ModeBindings, beacon_accepts_band_team_rejects_gap_and_beyond)
{
    ModeBindingsWorld fx;
    walker* leader = fx.spawn_living(FAMILY_SOLDIER, 1, 160, 160);
    ASSERT_NE(nullptr, leader);
    const std::uint32_t id = leader->entity_id();
    fx.run_on_load(
        "    local obs = og.oblist()\n"
        "    og.set_beacon(0, obs[1], 31)\n"
        "    local ok15 = pcall(og.set_beacon, 1, obs[1], 15)\n"
        "    local ok40 = pcall(og.set_beacon, 1, obs[1], 40)\n"
        "    og.log('errs', ok15 and 1 or 0, ok40 and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("errs\t0\t0"));
    EXPECT_EQ(static_cast<std::int32_t>(id),
              fx.world().mode.beacons[0].entity_id);
    EXPECT_EQ(31, fx.world().mode.beacons[0].team);
    // The error arms left slot 1 empty.
    EXPECT_EQ(0, fx.world().mode.beacons[1].entity_id);
    EXPECT_EQ(255, fx.world().mode.beacons[1].team);
}

TEST(ModeBindings, team_color_name_band_names_and_edges)
{
    ModeBindingsWorld fx;
    fx.run_on_load(
        "    og.log('band', og.team_color_name(16), og.team_color_name(17),\n"
        "           og.team_color_name(18), og.team_color_name(19),\n"
        "           og.team_color_name(20), og.team_color_name(21),\n"
        "           og.team_color_name(22), og.team_color_name(23),\n"
        "           og.team_color_name(24), og.team_color_name(25),\n"
        "           og.team_color_name(26), og.team_color_name(27),\n"
        "           og.team_color_name(28), og.team_color_name(29),\n"
        "           og.team_color_name(30), og.team_color_name(31))\n"
        "    local ok4 = pcall(og.team_color_name, 4)\n"
        "    local ok15 = pcall(og.team_color_name, 15)\n"
        "    local ok32 = pcall(og.team_color_name, 32)\n"
        "    og.log('errs', ok4 and 1 or 0, ok15 and 1 or 0,\n"
        "           ok32 and 1 or 0)\n");
    EXPECT_TRUE(fx.logged(
        "band\tRED\tGREEN\tBLUE\tYELLOW\tMAGENTA\tCYAN\tTAN\tROSE\t"
        "LAVENDER\tSALMON\tORANGE\tPINK\tVIOLET\tTEAL\tGOLD\tSLATE"));
    EXPECT_TRUE(fx.logged("errs\t0\t0\t0"));
}

TEST(ModeBindings, ffa_band_constants_exported_on_C)
{
    ModeBindingsWorld fx;
    fx.run_on_load(
        "    og.log('ffa_c', og.C.FFA_TEAM_BASE, og.C.FFA_TEAM_COUNT)\n");
    EXPECT_TRUE(fx.logged("ffa_c\t16\t16"));
}

TEST(ModeBindings, match_setting_reads_all_six_knobs)
{
    ModeBindingsWorld fx;
    fx.world().ctf_requested_team_count = 3;
    fx.world().ctf_requested_capture_limit = 5;
    fx.world().ctf_requested_respawn_ticks = 90;
    fx.world().ctf_requested_strip_scenario_troops = 1;
    fx.world().respawn_mode = og::sim::kRespawnModeHeroes;
    fx.world().ctf_requested_time_limit = 7200;
    fx.run_on_load(
        "    og.log('knobs', og.match_setting('team_count'),\n"
        "           og.match_setting('score_limit'),\n"
        "           og.match_setting('respawn_ticks'),\n"
        "           og.match_setting('strip_troops'),\n"
        "           og.match_setting('respawn_mode'),\n"
        "           og.match_setting('time_limit'))\n"
        "    local ok = pcall(og.match_setting, 'bogus')\n"
        "    og.log('err', ok and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("knobs\t3\t5\t90\t1\t1\t7200"));
    EXPECT_TRUE(fx.logged("err\t0"));
}

TEST(ModeBindings, match_setting_reads_the_eight_per_team_bot_knobs)
{
    ModeBindingsWorld fx;
    // Distinct per team and distinct between squad and level, so an
    // off-by-one or transposed index shows as a value mismatch.
    fx.world().ctf_requested_fill = {0, 2, 1, 9};
    fx.world().ctf_requested_map_units = {3, 0, 9, 1};
    fx.run_on_load(
        "    og.log('squad', og.match_setting('fill_1'),\n"
        "           og.match_setting('fill_2'),\n"
        "           og.match_setting('fill_3'),\n"
        "           og.match_setting('fill_4'))\n"
        "    og.log('level', og.match_setting('map_units_1'),\n"
        "           og.match_setting('map_units_2'),\n"
        "           og.match_setting('map_units_3'),\n"
        "           og.match_setting('map_units_4'))\n"
        "    local ok = pcall(og.match_setting, 'fill_5')\n"
        "    og.log('err', ok and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("squad\t0\t2\t1\t9"));
    EXPECT_TRUE(fx.logged("level\t3\t0\t9\t1"));
    EXPECT_TRUE(fx.logged("err\t0"))
        << "a fifth team is outside the vocabulary and must raise";
}

TEST(ModeBindings, match_setting_reads_back_matched_sentinel_raw)
{
    ModeBindingsWorld fx;
    fx.world().ctf_requested_strip_scenario_troops = og::sim::kTroopsMatched;
    fx.run_on_load(
        "    og.log('troops', og.match_setting('strip_troops'))\n");
    // og.match_setting returns the RAW short: mode Lua sees 3, never a
    // normalized boolean — the Lua-side matched check is raw == 3
    // (matched-teams D27/D29).
    EXPECT_TRUE(fx.logged("troops\t3"));
}

// RAII registration of the SHIPPED mode_core.lua as a lib module of the
// test pack, so the probe below can og.use it (og.use is pack-relative).
// The chunk name stays outside packs/ on purpose: these bytes are measured
// where the pack genuinely mounts (og_unit_modes), not here.
struct ShippedModeCoreModule
{
    bool loaded = false;

    ShippedModeCoreModule()
    {
        std::ifstream in(
            "campaigns/modes/packs/modes.core/lib/mode_core.lua",
            std::ios::binary);
        if (!in.good())
            return;
        std::ostringstream buf;
        buf << in.rdbuf();
        og::script::register_pack_lib_module(
            {"test.mode", "mode_core", "test.mode/mode_core.lua",
             buf.str()});
        loaded = true;
    }

    ~ShippedModeCoreModule()
    {
        og::script::unregister_pack_lib_modules("test.mode");
    }
};

TEST(ModeBindings, matched_constant_equals_the_mode_core_lua_constant)
{
    // The D29 constant-equality pin: the world field is stamped from the
    // C++ og::sim::kTroopsMatched sentinel and round-tripped through
    // og.match_setting('strip_troops'); the SHIPPED mode_core.lua must
    // carry the same value in core.MATCHED_TROOPS. (Its one-time
    // classification helper, core.team_count_request, retired with the
    // plan phase — the strip-field read lives in match.activation now.)
    ShippedModeCoreModule mode_core;
    ASSERT_TRUE(mode_core.loaded)
        << "mode_core.lua unreadable — run from the repo root (ctest does)";
    ModeBindingsWorld fx;
    fx.world().ctf_requested_strip_scenario_troops = og::sim::kTroopsMatched;
    og::script::clear_pack_scripts();
    og::script::register_pack_script(
        {"test.mode", "probe.lua",
         "local core = og.use('mode_core')\n"
         "og.register_level_hooks(42, {\n"
         "  on_load = function(level)\n"
         "    og.log('constant', core.MATCHED_TROOPS)\n"
         "    og.log('pin', core.MATCHED_TROOPS ==\n"
         "           og.match_setting('strip_troops') and 1 or 0)\n"
         "  end,\n"
         "})\n"});
    fx.world().tick();
    EXPECT_TRUE(fx.logged(
        "constant\t" + std::to_string(og::sim::kTroopsMatched)))
        << fx.script_errors();
    EXPECT_TRUE(fx.logged("pin\t1")) << fx.script_errors();
}

TEST(ModeBindings, team_masks_follow_markers_and_requested_count)
{
    ModeBindingsWorld fx;
    // Sparse authored teams {0, 2, 3}; requested 2 activates {0, 2} — the
    // index-order rule, not a numeric [0, N) clamp.
    fx.spawn_marker(0, 128, 128);
    fx.spawn_marker(2, 256, 128);
    fx.spawn_marker(3, 384, 128);
    fx.world().ctf_requested_team_count = 2;
    fx.run_on_load(
        "    og.log('masks', og.authored_team_mask(),\n"
        "           og.effective_team_mask())\n");
    EXPECT_TRUE(fx.logged("masks\t13\t5"));
}

TEST(ModeBindings, matched_troops_request_feeds_no_mask)
{
    ModeBindingsWorld fx;
    // Same sparse authored set {0, 2, 3} as above, with the TROOPS: FAIR
    // sentinel armed: the strip field feeds no mask anywhere (matched-teams
    // D29 — effective_team_mask reads only the team count), so an Auto
    // count still activates the whole authored mask and a numeric count
    // still clamps it. The retired teams sentinel 5 is junk again and
    // clamps like any value above 4.
    fx.spawn_marker(0, 128, 128);
    fx.spawn_marker(2, 256, 128);
    fx.spawn_marker(3, 384, 128);
    fx.world().ctf_requested_strip_scenario_troops = og::sim::kTroopsMatched;
    fx.run_on_load(
        "    og.log('masks', og.authored_team_mask(),\n"
        "           og.effective_team_mask())\n");
    EXPECT_TRUE(fx.logged("masks\t13\t13"));

    ModeBindingsWorld clamped;
    clamped.spawn_marker(0, 128, 128);
    clamped.spawn_marker(2, 256, 128);
    clamped.spawn_marker(3, 384, 128);
    clamped.world().ctf_requested_strip_scenario_troops =
        og::sim::kTroopsMatched;
    clamped.world().ctf_requested_team_count = 5;  // retired sentinel = junk
    clamped.run_on_load(
        "    og.log('masks', og.authored_team_mask(),\n"
        "           og.effective_team_mask())\n");
    EXPECT_TRUE(clamped.logged("masks\t13\t13"))
        << "5 clamps to 4, and only 3 authored teams exist";
}

TEST(ModeBindings, find_by_id_resolves_and_nils)
{
    ModeBindingsWorld fx;
    walker* w = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    fx.run_on_load(
        "    local obs = og.oblist()\n"
        "    local id = og.entity_id(obs[1])\n"
        "    local h = og.find_by_id(id)\n"
        "    og.log('found', h ~= nil and og.entity_id(h) == id and 1 or 0)\n"
        "    og.log('zero', og.find_by_id(0) == nil and 1 or 0)\n"
        "    og.log('absent', og.find_by_id(999999) == nil and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("found\t1"));
    EXPECT_TRUE(fx.logged("zero\t1"));
    EXPECT_TRUE(fx.logged("absent\t1"));
}

TEST(ModeBindings, fxlist_weaplist_enumerate_their_lists)
{
    ModeBindingsWorld fx;
    ASSERT_NE(nullptr, fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN));
    ASSERT_NE(nullptr, fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN));
    ASSERT_NE(nullptr, fx.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE));
    fx.run_on_load(
        "    og.log('fx', #og.fxlist())\n"
        "    og.log('weap', #og.weaplist())\n");
    EXPECT_TRUE(fx.logged("fx\t2"));
    EXPECT_TRUE(fx.logged("weap\t1"));
}

TEST(ModeBindings, world_tick_is_the_absolute_counter)
{
    ModeBindingsWorld fx;
    fx.world().tick_count_ = 500;
    fx.run_on_load("    og.log('tick', og.world_tick())\n");
    // tick() incremented before on_load dispatched.
    EXPECT_TRUE(fx.logged("tick\t501"));
}

TEST(ModeBindings, respawn_schedule_pending_and_counts)
{
    ModeBindingsWorld fx;
    walker* live = fx.spawn_living(FAMILY_SOLDIER, 1, 160, 160);
    walker* corpse = fx.spawn_living(FAMILY_SOLDIER, 1, 224, 160);
    ASSERT_NE(nullptr, live);
    ASSERT_NE(nullptr, corpse);
    corpse->set_dead(1);
    fx.run_on_load(
        "    local corpse = nil\n"
        "    local live = nil\n"
        "    for _, w in ipairs(og.oblist()) do\n"
        "      if w:dead() ~= 0 then corpse = w else live = w end\n"
        "    end\n"
        "    og.log('live_refused', og.respawn_schedule(live) and 1 or 0)\n"
        "    og.log('queued', og.respawn_schedule(corpse, 25) and 1 or 0)\n"
        "    og.log('dupe', og.respawn_schedule(corpse) and 1 or 0)\n"
        "    og.log('pending', og.respawn_pending(corpse) and 1 or 0)\n"
        "    og.log('not_pending', og.respawn_pending(live) and 1 or 0)\n"
        "    og.log('count1', og.respawn_pending_count(1))\n"
        "    og.log('count0', og.respawn_pending_count(0))\n"
        "    local ok1 = pcall(og.respawn_schedule, corpse, 0)\n"
        "    local ok2 = pcall(og.respawn_pending_count, 4)\n"
        "    og.log('errs', ok1 and 1 or 0, ok2 and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("live_refused\t0")) << fx.script_errors();
    EXPECT_TRUE(fx.logged("queued\t1"));
    EXPECT_TRUE(fx.logged("dupe\t0"));
    EXPECT_TRUE(fx.logged("pending\t1"));
    EXPECT_TRUE(fx.logged("not_pending\t0"));
    EXPECT_TRUE(fx.logged("count1\t1"));
    EXPECT_TRUE(fx.logged("count0\t0"));
    EXPECT_TRUE(fx.logged("errs\t0\t0"));
    ASSERT_EQ(1u, fx.world().respawn.respawn_queue.size());
    EXPECT_EQ(25, fx.world().respawn.respawn_queue[0].ticks_left);
}

TEST(ModeBindings, respawn_anchors_read_back_and_validate)
{
    ModeBindingsWorld fx;
    fx.spawn_marker(0, 128, 96);
    fx.spawn_marker(0, 192, 96);
    fx.spawn_marker(1, 320, 96);
    og::sim::respawn_scan_anchors(fx.world());
    fx.run_on_load(
        "    og.log('counts', og.respawn_anchor_count(0),\n"
        "           og.respawn_anchor_count(1),\n"
        "           og.respawn_anchor_count(2))\n"
        "    local x, y = og.respawn_anchor(0, 1)\n"
        "    og.log('anchor', x, y)\n"
        "    local ok1 = pcall(og.respawn_anchor, 0, 2)\n"
        "    local ok2 = pcall(og.respawn_anchor_count, 5)\n"
        "    og.log('errs', ok1 and 1 or 0, ok2 and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("counts\t2\t1\t0"));
    EXPECT_TRUE(fx.logged("anchor\t192\t96"));
    EXPECT_TRUE(fx.logged("errs\t0\t0"));
}

TEST(ModeBindings, spawn_spot_clear_probes_without_eating)
{
    ModeBindingsWorld fx;
    walker* prober = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    walker* blocker = fx.spawn_living(FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_NE(nullptr, prober);
    ASSERT_NE(nullptr, blocker);
    fx.run_on_load(
        "    local w = og.oblist()[1]\n"
        "    og.log('open', og.spawn_spot_clear(w, 224, 224) and 1 or 0)\n"
        "    og.log('blocked', og.spawn_spot_clear(w, 320, 320) and 1 or 0)\n"
        "    og.log('floored',\n"
        "           og.spawn_spot_clear(w, 224, 224, 0) and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("open\t1"));
    EXPECT_TRUE(fx.logged("blocked\t0"));
    EXPECT_TRUE(fx.logged("floored\t1"));
}

TEST(ModeBindings, scrub_corpse_stain_kills_nearby_drops)
{
    ModeBindingsWorld fx;
    walker* stain = fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    walker* far_stain = fx.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain);
    ASSERT_NE(nullptr, far_stain);
    stain->setxy(160, 160);
    far_stain->setxy(400, 400);
    fx.run_on_load(
        "    og.scrub_corpse_stain(160, 160)\n"
        "    og.scrub_corpse_stain(96, 96, 3)\n");
    EXPECT_TRUE(stain->dead());
    // Different position (and the floor-3 call misses everything on 0).
    EXPECT_FALSE(far_stain->dead());
}

TEST(ModeBindings, set_act_type_refuses_control_and_restores)
{
    ModeBindingsWorld fx;
    walker* w = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    // spawn_living left it ACT_CONTROL; hand it to the AI first so the
    // one-deep undo has a defined base.
    w->set_act_type(ACT_RANDOM);
    fx.run_on_load(
        "    local w = og.oblist()[1]\n"
        "    w:set_act_type(og.C.ACT_GUARD)\n"
        "    og.log('set', w:act_type())\n"
        "    w:restore_act_type()\n"
        "    og.log('restored', w:act_type())\n"
        "    local ok = pcall(function() w:set_act_type(og.C.ACT_CONTROL) end)\n"
        "    og.log('err', ok and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("err\t0"));
    EXPECT_EQ(ACT_RANDOM, w->act_type());
}

TEST(ModeBindings, front_command_reader_and_teleport_stamp)
{
    ModeBindingsWorld fx;
    walker* w = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    w->set_last_self_teleport_tick(77);
    fx.run_on_load(
        "    local w = og.oblist()[1]\n"
        "    og.log('empty', w:s_front_command())\n"
        "    w:s_force_command(og.C.COMMAND_GOTO, 45, 320, 320)\n"
        "    og.log('front', w:s_front_command())\n"
        "    og.log('goto_is_fifteen', og.C.COMMAND_GOTO)\n"
        "    og.log('teams', og.C.SCORE_TEAM_COUNT)\n"
        "    og.log('blink', w:last_self_teleport_tick())\n");
    EXPECT_TRUE(fx.logged("empty\t0"));
    EXPECT_TRUE(fx.logged("front\t15"));
    EXPECT_TRUE(fx.logged("goto_is_fifteen\t15"));
    EXPECT_TRUE(fx.logged("teams\t4"));
    EXPECT_TRUE(fx.logged("blink\t77"));
}

TEST(ModeBindings, bit_32768_reads_back_truthy)
{
    // The highest free snapshot-safe stats mark: query_bit_flags returns
    // short, so 32768 comes back NEGATIVE — the binding's ~= 0 idiom must
    // still see it (the Mutant identity mark depends on this).
    ModeBindingsWorld fx;
    walker* w = fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    fx.run_on_load(
        "    local w = og.oblist()[1]\n"
        "    w:s_set_bit_flags(32768, 1)\n"
        "    og.log('truthy', w:s_query_bit_flags(32768) and 1 or 0)\n"
        "    w:s_set_bit_flags(32768, 0)\n"
        "    og.log('cleared', w:s_query_bit_flags(32768) and 1 or 0)\n");
    EXPECT_TRUE(fx.logged("truthy\t1"));
    EXPECT_TRUE(fx.logged("cleared\t0"));
}

TEST(ModeBindings, radar_landmark_declares_on_treasure_and_fx_only)
{
    og::script::clear_pack_family_chunks();
    og::data::ClasspackData data;
    og::script::register_pack_family_chunk(
        {"modes", "modes/families/a.lua",
         "og.family('treasure', { id = 'modes:flag', name = 'FLAG',\n"
         "                        radar_landmark = true })\n"
         "og.family('effect', { id = 'modes:ball', name = 'BALL',\n"
         "                      radar_landmark = true })\n"});
    const og::script::DeclareResult ok =
        og::script::declare_pack_families("modes", data);
    ASSERT_TRUE(ok.ok) << ok.error;
    ASSERT_EQ(1u, data.treasures.size());
    EXPECT_TRUE(data.treasures[0].presentation.radar_landmark.value_or(false));
    ASSERT_EQ(1u, data.effects.size());
    EXPECT_TRUE(data.effects[0].presentation.radar_landmark.value_or(false));

    // The key is landmark-map furniture vocabulary: livings (and the other
    // orders) reject it as unknown.
    og::script::clear_pack_family_chunks();
    og::data::ClasspackData rejected;
    og::script::register_pack_family_chunk(
        {"modes", "modes/families/b.lua",
         "og.family('living', { id = 'modes:guy', name = 'GUY',\n"
         "                      radar_landmark = true })\n"});
    const og::script::DeclareResult bad =
        og::script::declare_pack_families("modes", rejected);
    EXPECT_FALSE(bad.ok);
    EXPECT_NE(std::string::npos, bad.error.find("radar_landmark"))
        << "error was: " << bad.error;
    og::script::clear_pack_family_chunks();
}
