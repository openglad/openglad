// Unit tests for the og_test_parity golden byte-compare (issue #283).
//
// The compare itself is one line — std::mismatch over two strings — so what
// these tests pin is the failure report a red row prints: the byte offset, the
// JSON key path open at that byte, and the two 96-byte excerpt windows. The
// corpus is a single synthetic StateDump serialised by the real emitter and
// pinned as a literal below; every offset in this file is a literal computed
// once from that literal, never recomputed at run time (a self-computing
// expectation would agree with any bug the scanner has).
#include "golden_compare.h"
#include "state_dump.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace {

// Two walkers (one on a non-zero floor, so the optional "floor" key is
// exercised), one effect, three events (the middle one carrying an escaped
// quote the path scanner has to cross), one weapon and two weapon-track
// samples — one instance of every array the canonical emitter writes, in the
// canonical order capture_state_dump would have left them in.
og::parity::StateDump make_synthetic_dump()
{
    og::parity::StateDump d;
    d.schema_version   = "v1";
    d.tick             = 3;
    d.rng_state        = 0x2D0FD00Du;
    d.rng_observable   = true;
    d.score_per_team[0] = 5;
    d.score_per_team[1] = 0;
    d.score_per_team[2] = 0;
    d.score_per_team[3] = 0;
    d.level_done       = 0;
    d.level_tick_count = 3;

    d.walkers.push_back(og::parity::WalkerEntry{
        /*id*/ 1, /*family*/ "FAMILY_SOLDIER", /*team*/ 0, /*xpos*/ 100,
        /*ypos*/ 120, /*hp*/ 36.0f, /*max_hp*/ 40.0f, /*weapons_left*/ 2,
        /*alive*/ true, /*floor*/ 0});
    d.walkers.push_back(og::parity::WalkerEntry{
        /*id*/ 2, /*family*/ "FAMILY_ORC", /*team*/ 1, /*xpos*/ 160,
        /*ypos*/ 120, /*hp*/ 12.5f, /*max_hp*/ 20.0f, /*weapons_left*/ 0,
        /*alive*/ true, /*floor*/ 1});

    d.effects.push_back(og::parity::EffectEntry{
        /*id*/ 3, /*family*/ "FAMILY_EXPLOSION", /*xpos*/ 130, /*ypos*/ 120,
        /*lifetime*/ 0});

    d.events.push_back(og::parity::EventEntry{
        /*kind*/ "play_sound", /*tick*/ 1, /*a*/ 11, /*b*/ 0, /*text*/ "",
        /*sequence*/ 0});
    d.events.push_back(og::parity::EventEntry{
        /*kind*/ "notification", /*tick*/ 2, /*a*/ 1, /*b*/ 0,
        /*text*/ "he said \"go\"", /*sequence*/ 0});
    d.events.push_back(og::parity::EventEntry{
        /*kind*/ "score_change", /*tick*/ 2, /*a*/ 0, /*b*/ 113, /*text*/ "",
        /*sequence*/ 1});

    d.weapons.push_back(og::parity::WeaponEntry{
        /*id*/ 4, /*family*/ "FAMILY_KNIFE", /*team*/ 0, /*xpos*/ 110,
        /*ypos*/ 120, /*lifetime*/ 0});

    d.weapon_tracks.push_back(og::parity::WeaponTrackSample{
        /*tick*/ 1, /*family*/ "FAMILY_KNIFE", /*seq*/ 0, /*xpos*/ 104,
        /*ypos*/ 120, /*lifetime*/ 0});
    d.weapon_tracks.push_back(og::parity::WeaponTrackSample{
        /*tick*/ 2, /*family*/ "FAMILY_KNIFE", /*seq*/ 0, /*xpos*/ 108,
        /*ypos*/ 120, /*lifetime*/ 0});

    return d;
}

// canonical_serialize(make_synthetic_dump()), pinned. Printed once and pasted;
// test 1 is the guard that keeps it honest when the emitter moves.
const std::string kBaseJson =
    R"JSON({"effects":[{"family":"FAMILY_EXPLOSION","id":3,"xpos":130,"ypos":120}],"events":[{"a":11,"b":0,"kind":"play_sound","sequence":0,"text":"","tick":1},{"a":1,"b":0,"kind":"notification","sequence":0,"text":"he said \"go\"","tick":2},{"a":0,"b":113,"kind":"score_change","sequence":1,"text":"","tick":2}],"level_done":0,"level_tick_count":3,"rng_state":"0x2D0FD00D","schema_version":"v1","score_per_team":[5,0,0,0],"tick":3,"walkers":[{"alive":true,"family":"FAMILY_SOLDIER","hp":36.000000,"id":1,"max_hp":40.000000,"team":0,"weapons_left":2,"xpos":100,"ypos":120},{"alive":true,"family":"FAMILY_ORC","floor":1,"hp":12.500000,"id":2,"max_hp":20.000000,"team":1,"weapons_left":0,"xpos":160,"ypos":120}],"weapon_tracks":[{"family":"FAMILY_KNIFE","seq":0,"tick":1,"xpos":104,"ypos":120},{"family":"FAMILY_KNIFE","seq":0,"tick":2,"xpos":108,"ypos":120}],"weapons":[{"family":"FAMILY_KNIFE","id":4,"team":0,"xpos":110,"ypos":120}]}
)JSON";

// Byte offsets of the divergences the tests below plant, computed once from
// kBaseJson and written down.
constexpr std::size_t kHpOffset            = 478; // walkers[0].hp digit "6"
constexpr std::size_t kEventBOffset        = 244; // events[2].b digit "3"
constexpr std::size_t kLevelTickKeyOffset  = 318; // "l" of "level_tick_count"

std::string with_replacement(const std::string& subject,
                             const std::string& from, const std::string& to)
{
    const std::size_t at = subject.find(from);
    EXPECT_NE(std::string::npos, at) << "fixture edit \"" << from
                                     << "\" is not present in kBaseJson";
    if (at == std::string::npos) return subject;
    std::string out = subject;
    out.replace(at, from.size(), to);
    return out;
}

} // namespace

TEST(GoldenCompare, synthetic_dump_serialises_to_the_pinned_literal)
{
    EXPECT_EQ(kBaseJson, og::parity::canonical_serialize(make_synthetic_dump()));
    ASSERT_EQ('\n', kBaseJson.back());
}

TEST(GoldenCompare, identical_dumps_have_no_divergence)
{
    EXPECT_EQ(std::nullopt,
              og::parity::first_golden_divergence(kBaseJson, kBaseJson));
}

TEST(GoldenCompare, hp_edit_reports_walker_path_and_excerpts)
{
    const std::string branch =
        with_replacement(kBaseJson, "\"hp\":36.000000", "\"hp\":37.000000");
    const auto div = og::parity::first_golden_divergence(branch, kBaseJson);
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(kHpOffset, div->offset);
    EXPECT_EQ("walkers[0].hp", div->path);
    EXPECT_EQ(R"EX(...ick":3,"walkers":[{"alive":true,"family":"FAMILY_SOLDIER","hp":37.000000,"id":1,"max_hp":40.0000...)EX", div->branch_excerpt);
    EXPECT_EQ(R"EX(...ick":3,"walkers":[{"alive":true,"family":"FAMILY_SOLDIER","hp":36.000000,"id":1,"max_hp":40.0000...)EX", div->golden_excerpt);
}

TEST(GoldenCompare, event_payload_edit_crosses_an_escaped_string)
{
    const std::string branch =
        with_replacement(kBaseJson, "\"b\":113", "\"b\":114");
    const auto div = og::parity::first_golden_divergence(branch, kBaseJson);
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(kEventBOffset, div->offset);
    EXPECT_EQ("events[2].b", div->path);
    EXPECT_EQ(R"EX(...on","sequence":0,"text":"he said \"go\"","tick":2},{"a":0,"b":114,"kind":"score_change","sequenc...)EX", div->branch_excerpt);
    EXPECT_EQ(R"EX(...on","sequence":0,"text":"he said \"go\"","tick":2},{"a":0,"b":113,"kind":"score_change","sequenc...)EX", div->golden_excerpt);
}

TEST(GoldenCompare, first_divergence_is_the_lowest_byte)
{
    std::string branch =
        with_replacement(kBaseJson, "\"hp\":36.000000", "\"hp\":37.000000");
    branch = with_replacement(branch, "\"b\":113", "\"b\":114");
    const auto div = og::parity::first_golden_divergence(branch, kBaseJson);
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(kEventBOffset, div->offset);
    EXPECT_EQ("events[2].b", div->path);
}

TEST(GoldenCompare, deleted_top_level_key_reports_branch_side_key)
{
    const std::string golden =
        with_replacement(kBaseJson, ",\"level_tick_count\":3", "");
    const auto div = og::parity::first_golden_divergence(kBaseJson, golden);
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(kLevelTickKeyOffset, div->offset);
    EXPECT_EQ("level_tick_count", div->path);
    EXPECT_EQ(R"EX(...score_change","sequence":1,"text":"","tick":2}],"level_done":0,"level_tick_count":3,"rng_state":...)EX", div->branch_excerpt);
    EXPECT_EQ(R"EX(...score_change","sequence":1,"text":"","tick":2}],"level_done":0,"rng_state":"0x2D0FD00D","schema_...)EX", div->golden_excerpt);
}

TEST(GoldenCompare, missing_trailing_newline_reports_root_at_end)
{
    const std::string golden = kBaseJson.substr(0, kBaseJson.size() - 1);
    const auto div = og::parity::first_golden_divergence(kBaseJson, golden);
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(kBaseJson.size() - 1, div->offset);
    EXPECT_EQ("<root>", div->path);
    EXPECT_EQ(R"EX(..."family":"FAMILY_KNIFE","id":4,"team":0,"xpos":110,"ypos":120}]}\n)EX", div->branch_excerpt);
    EXPECT_EQ(R"EX(..."family":"FAMILY_KNIFE","id":4,"team":0,"xpos":110,"ypos":120}]})EX", div->golden_excerpt);
}

TEST(GoldenCompare, failure_message_is_verbatim)
{
    const std::string branch =
        with_replacement(kBaseJson, "\"hp\":36.000000", "\"hp\":37.000000");
    const auto div = og::parity::first_golden_divergence(branch, kBaseJson);
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(R"MSG(golden byte-compare FAIL for x_scen99: branch dump differs from tests/parity/golden/x_scen99.json at byte 478 (walkers[0].hp)
  branch: ...ick":3,"walkers":[{"alive":true,"family":"FAMILY_SOLDIER","hp":37.000000,"id":1,"max_hp":40.0000...
  golden: ...ick":3,"walkers":[{"alive":true,"family":"FAMILY_SOLDIER","hp":36.000000,"id":1,"max_hp":40.0000...
  triage: mkdir -p build/ci-test/parity && ./build/ci-test/parity_runner_smoke --scenario x_scen99 --out build/ci-test/parity/x_scen99.json && scripts/parity/run_parity_diff.sh x_scen99
  adjudicate per tests/parity/golden/DRIFT_LEDGER.md (companion -> branch -> merge base); a companion recapture is never pasted over a ledger-blessed golden)MSG",
              og::parity::format_golden_divergence("x_scen99", *div));
}
