// Unit tests for the FactKind dispatch in tests/parity/fact_predicate.cpp.
//
// evaluate_one's switch has no `default:` arm on purpose — that is what keeps
// -Wswitch (and therefore the -Werror lanes) turning a NEWLY ADDED enumerator
// into a compile error instead of a silent pass. The guard for a value no
// enumerator names is the mandatory post-switch return, and these tests pin
// that it FAILS loudly rather than returning the default-constructed ok=true
// result. Without them a cast, a corrupted table entry or a stale serialised
// kind would report every scenario row as satisfied.
#include "event_kind_symbol_pins.h"
#include "fact_predicate.h"
#include "state_dump.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace {

// A predicate that passes on a default-constructed dump: tick 0 >= 0.
og::parity::FactPredicate passing_predicate(std::string_view label)
{
    og::parity::FactPredicate p{};
    p.kind  = og::parity::FactKind::TickReached;
    p.arg0  = 0;
    p.label = label;
    return p;
}

// 0xFF is not — and must never become — a named FactKind enumerator.
og::parity::FactPredicate bogus_predicate(std::string_view label)
{
    og::parity::FactPredicate p{};
    p.kind  = static_cast<og::parity::FactKind>(0xFF);
    p.label = label;
    return p;
}

} // namespace

TEST(FactPredicate, unknown_kind_is_a_loud_failure)
{
    const og::parity::FactPredicate p = bogus_predicate("probe");
    const og::parity::StateDump     dump{};

    const og::parity::FactEvalResult r = og::parity::evaluate_one(p, dump);

    EXPECT_FALSE(r.ok)
        << "an out-of-range FactKind must FAIL, never fall through to pass";
    EXPECT_FALSE(r.indeterminate)
        << "an unhandled kind is a hard failure, not a missing-data pass";
    EXPECT_NE(std::string::npos, r.message.find("kind=255"))
        << "the message must name the offending kind; got: " << r.message;
    EXPECT_NE(std::string::npos, r.message.find("label=\"probe\""))
        << "the message must name the predicate label; got: " << r.message;
    EXPECT_NE(std::string::npos, r.message.find("not handled"))
        << "the message must say the kind was not handled; got: " << r.message;
}

TEST(FactPredicate, handled_kind_passes_with_an_empty_message)
{
    // Control for the test above: a kind the switch DOES handle, satisfied by
    // the dump, leaves the default-constructed result untouched. Without this
    // leg "ok=false" above could be an artifact of the dump, not of the kind.
    const og::parity::FactPredicate p = passing_predicate("control");
    const og::parity::StateDump     dump{};

    const og::parity::FactEvalResult r = og::parity::evaluate_one(p, dump);

    EXPECT_TRUE(r.ok) << "TickReached(0) holds on a tick-0 dump";
    EXPECT_FALSE(r.indeterminate) << "TickReached is always determinate";
    EXPECT_EQ(std::string(), r.message) << "a passing predicate says nothing";
}

TEST(FactPredicate, evaluate_facts_propagates_the_unknown_kind)
{
    const og::parity::FactPredicate facts[2] = {
        passing_predicate("first"),
        bogus_predicate("second"),
    };
    const og::parity::StateDump dump{};

    const og::parity::FactEvalResult r = og::parity::evaluate_facts(
        og::parity::FactSide::Branch, facts, 2, dump);

    EXPECT_FALSE(r.ok)
        << "one unhandled kind must red the whole fact array";
    EXPECT_EQ(0u, r.message.rfind("[#1] ", 0))
        << "the aggregate must index the FAILING predicate; got: " << r.message;
    EXPECT_NE(std::string::npos, r.message.find("kind=255"))
        << "the aggregate must carry the per-predicate detail; got: "
        << r.message;
}

TEST(FactPredicate, evaluate_facts_passes_when_every_kind_is_handled)
{
    const og::parity::FactPredicate facts[2] = {
        passing_predicate("first"),
        passing_predicate("second"),
    };
    const og::parity::StateDump dump{};

    const og::parity::FactEvalResult r = og::parity::evaluate_facts(
        og::parity::FactSide::Branch, facts, 2, dump);

    EXPECT_TRUE(r.ok) << "two satisfied predicates aggregate to a pass";
    EXPECT_EQ(std::string(), r.message) << "a full pass reports no message";
}

// ---------------------------------------------------------------------------
// WalkerOfOrderFamilyCount — the order-aware oblist count.
//
// The rule: dump.walkers[] entries are counted by rendering (arg0, arg1)
// through family_symbol_by_order, so an FX family id names its FX symbol and
// NOT the Living family that happens to share its ordinal; alive and dead
// entries both count, because the dead FX/treasure entry parked in oblist is
// the whole evidence these rows exist to pin.
namespace {

// FAMILY_FLASH is FX ordinal 4; Living ordinal 4 is FAMILY_SKELETON. The
// collision is the point: a kind that resolved arg0 through the Living table
// would report the flash as a skeleton.
constexpr std::int32_t kFxOrder     = static_cast<std::int32_t>(Order::FX);
constexpr std::int32_t kLivingOrder = static_cast<std::int32_t>(Order::Living);

og::parity::StateDump dump_with_dead_flash_and_live_soldier()
{
    og::parity::StateDump d{};
    og::parity::WalkerEntry flash{};
    flash.id     = 7;
    flash.family = og::parity::family_symbol_by_order(kFxOrder, FAMILY_FLASH);
    flash.xpos   = 94;
    flash.ypos   = 118;
    flash.alive  = false;   // expired FX, still in oblist
    og::parity::WalkerEntry soldier{};
    soldier.id     = 1;
    soldier.family = og::parity::family_symbol_by_order(kLivingOrder, FAMILY_SOLDIER);
    soldier.alive  = true;
    d.walkers.push_back(flash);
    d.walkers.push_back(soldier);
    return d;
}

} // namespace

TEST(FactPredicate, order_family_count_counts_dead_fx_entries)
{
    const og::parity::StateDump dump = dump_with_dead_flash_and_live_soldier();
    ASSERT_EQ("FAMILY_FLASH", dump.walkers[0].family)
        << "fixture must render FAMILY_FLASH under the FX table";
    ASSERT_FALSE(dump.walkers[0].alive)
        << "the flash under test must be the DEAD oblist resident";

    const og::parity::FactEvalResult r = og::parity::evaluate_one(
        og::parity::pred::WalkerOfOrderFamilyCount(FAMILY_FLASH, kFxOrder, 1, 1,
                                                   "one expired flash"),
        dump);

    EXPECT_TRUE(r.ok)
        << "exactly one FX-order FAMILY_FLASH entry satisfies (1,1); got: "
        << r.message;
    EXPECT_FALSE(r.indeterminate)
        << "the count is always determinate — walkers[] is always carried";
    EXPECT_EQ(std::string(), r.message) << "a passing predicate says nothing";
}

TEST(FactPredicate, order_family_count_does_not_alias_living_order)
{
    // The discrimination, both legs on ONE dump and ONE family ordinal:
    // arg1 must select the family table. FX ordinal 4 is FAMILY_FLASH and
    // Living ordinal 4 is FAMILY_SKELETON, so the same arg0 must count 1
    // under FX and 0 under Living. An evaluator that hard-codes EITHER
    // table reds one of these legs.
    const og::parity::StateDump dump = dump_with_dead_flash_and_live_soldier();

    const og::parity::FactEvalResult fx = og::parity::evaluate_one(
        og::parity::pred::WalkerOfOrderFamilyCount(FAMILY_FLASH, kFxOrder, 1, 1,
                                                   "alias probe: FX leg"),
        dump);
    const og::parity::FactEvalResult living = og::parity::evaluate_one(
        og::parity::pred::WalkerOfOrderFamilyCount(FAMILY_FLASH, kLivingOrder, 1, 1,
                                                   "alias probe: Living leg"),
        dump);

    EXPECT_TRUE(fx.ok)
        << "ordinal 4 under the FX table is FAMILY_FLASH, present once; got: "
        << fx.message;
    EXPECT_FALSE(living.ok)
        << "ordinal 4 under the Living table is FAMILY_SKELETON, absent here";
    EXPECT_FALSE(living.indeterminate)
        << "an out-of-range count is a hard failure";
    EXPECT_NE(std::string::npos, living.message.find("count=0"))
        << "the message must name the count it saw; got: " << living.message;
    EXPECT_NE(std::string::npos, living.message.find("FAMILY_SKELETON"))
        << "the message must name the symbol arg1 selected; got: "
        << living.message;
    EXPECT_EQ(std::string::npos, living.message.find("FAMILY_FLASH"))
        << "Living order must not resolve to the FX symbol; got: "
        << living.message;
}

TEST(FactPredicate, order_family_count_out_of_range_fails_with_bounds)
{
    const og::parity::StateDump dump = dump_with_dead_flash_and_live_soldier();

    // The dump holds one flash; (0,0) asserts none. Both halves of the
    // bounds test are exercised: this is the max-exceeded half, and the
    // count=0 case above is the min-underrun half.
    const og::parity::FactEvalResult r = og::parity::evaluate_one(
        og::parity::pred::WalkerOfOrderFamilyCount(FAMILY_FLASH, kFxOrder, 0, 0,
                                                   "no flash expected"),
        dump);

    EXPECT_FALSE(r.ok) << "one flash present must fail a (0,0) bound";
    EXPECT_FALSE(r.indeterminate) << "an out-of-range count is a hard failure";
    EXPECT_NE(std::string::npos, r.message.find("count=1 of FAMILY_FLASH"))
        << "the message must name count and symbol; got: " << r.message;
    EXPECT_NE(std::string::npos, r.message.find("out of [0,0]"))
        << "the message must name the bounds it broke; got: " << r.message;
}

TEST(FactPredicate, order_family_count_kind_renders_by_name)
{
    // og::parity::fact_kind_name is the ONE table every consumer prints
    // from (parity_runner_smoke --facts, scenario_facts_dump's generated
    // JSON). A kind missing its arm there cannot compile (no `default:`);
    // a kind spelt wrong there silently renames the predicate in every
    // generated artefact, which is what this pins.
    EXPECT_STREQ("WalkerOfOrderFamilyCount",
                 og::parity::fact_kind_name(
                     og::parity::FactKind::WalkerOfOrderFamilyCount))
        << "the new kind must print under its own name";
    EXPECT_STREQ("WalkerFamilyCount",
                 og::parity::fact_kind_name(
                     og::parity::FactKind::WalkerFamilyCount))
        << "control: the neighbouring count kind keeps its name";
    EXPECT_STREQ("Unknown",
                 og::parity::fact_kind_name(
                     static_cast<og::parity::FactKind>(0xFF)))
        << "an unnamed ordinal renders as Unknown, never as a real kind";
}

// ---------------------------------------------------------------------------
// The frozen EventKind ordinal table (og::parity::event_kind_symbol_of_ordinal).
//
// Two separate rules meet here and these cases pin them apart:
//
//  * The ORDINALS are frozen by scenario_table.h, which spells them as bare
//    integers in every EventKindAtLeast / EventKindExactly row. Reordering
//    the table repoints hundreds of committed rows at a different event kind
//    without touching a single one of them.
//  * The NAMES must be exactly the strings
//    tests/parity/state_dump.cpp::event_kind_symbol writes into a dump's
//    events[], because the evaluator counts events by comparing those
//    strings. A rename on either side makes every row that binds that kind
//    count zero — and, for an `at least 0` row, pass for the wrong reason.
//
// The ordinals are NOT the EventKind enumerator values (that enum is sparse)
// and they do not follow the renderer's case order, so neither is checked.

TEST(FactPredicate, ordinals_are_frozen)
{
    using og::parity::event_kind_symbol_of_ordinal;

    // scenario_table.h writes these numbers as literals. Append only.
    EXPECT_STREQ("none",                      event_kind_symbol_of_ordinal(0));
    EXPECT_STREQ("play_sound",                event_kind_symbol_of_ordinal(1));
    EXPECT_STREQ("notification",              event_kind_symbol_of_ordinal(2));
    EXPECT_STREQ("set_palette",               event_kind_symbol_of_ordinal(3));
    EXPECT_STREQ("request_redraw",            event_kind_symbol_of_ordinal(4));
    EXPECT_STREQ("end_game",                  event_kind_symbol_of_ordinal(5));
    EXPECT_STREQ("set_end",                   event_kind_symbol_of_ordinal(6));
    EXPECT_STREQ("request_exit_confirmation", event_kind_symbol_of_ordinal(7));
    EXPECT_STREQ("withdraw_to_level",         event_kind_symbol_of_ordinal(8));
    EXPECT_STREQ("score_change",              event_kind_symbol_of_ordinal(9));
    EXPECT_STREQ("damage_tile",               event_kind_symbol_of_ordinal(10));

    EXPECT_EQ(11, og::parity::kEventKindOrdinalCount)
        << "the count must match the eleven rows pinned above";
    EXPECT_STREQ("", event_kind_symbol_of_ordinal(
                         og::parity::kEventKindOrdinalCount))
        << "one past the end must be UNNAMED, so the evaluator can reject it";
    EXPECT_STREQ("", event_kind_symbol_of_ordinal(-1))
        << "a negative ordinal must be UNNAMED, never an out-of-bounds read";
}

TEST(FactPredicate, every_ordinal_names_a_symbol_the_renderer_emits)
{
    for (std::int32_t ordinal = 0;
         ordinal < og::parity::kEventKindOrdinalCount; ++ordinal)
    {
        const std::string_view name =
            og::parity::event_kind_symbol_of_ordinal(ordinal);

        const og::sim::EventKind* named = nullptr;
        for (const auto& [kind, symbol] : kEventKindSymbolPins)
            if (symbol == name) named = &kind;

        ASSERT_NE(nullptr, named)
            << "ordinal " << ordinal << " names \"" << name
            << "\", which tests/parity/state_dump.cpp::event_kind_symbol never "
               "emits — every row binding that ordinal would count zero events";
        EXPECT_EQ(std::string(name),
                  og::parity::event_kind_symbol(
                      static_cast<std::uint32_t>(*named)))
            << "ordinal " << ordinal
            << " must spell the symbol the renderer writes into the dump";
    }
}

TEST(FactPredicate, ordinal_of_symbol_round_trips)
{
    for (std::int32_t ordinal = 0;
         ordinal < og::parity::kEventKindOrdinalCount; ++ordinal)
    {
        const std::string_view name =
            og::parity::event_kind_symbol_of_ordinal(ordinal);
        const std::optional<std::int32_t> back =
            og::parity::event_kind_ordinal_of_symbol(name);

        ASSERT_TRUE(back.has_value())
            << "\"" << name << "\" is ordinal " << ordinal
            << " but the inverse lookup does not find it";
        EXPECT_EQ(ordinal, *back)
            << "the inverse lookup must answer the SAME ordinal the coverage "
               "gate then binds scenario rows by";
    }

    EXPECT_FALSE(og::parity::event_kind_ordinal_of_symbol("damage_number")
                     .has_value())
        << "damage_number is a real EventKind the SIM NEVER EMITS (GameServer "
           "lifts it), so no parity ordinal names it and no row may bind it";
    EXPECT_FALSE(og::parity::event_kind_ordinal_of_symbol("bogus").has_value())
        << "a symbol no ordinal names must answer nullopt, not ordinal 0";
}

TEST(FactPredicate, unknown_event_ordinal_is_a_loud_failure)
{
    const og::parity::StateDump dump{};   // no events at all

    const og::parity::FactEvalResult r = og::parity::evaluate_one(
        og::parity::pred::EventKindExactly(/*kind_ordinal=*/99, /*exact=*/0,
                                           "bogus ordinal"),
        dump);

    EXPECT_FALSE(r.ok)
        << "an ordinal the frozen table does not name must FAIL: counting it "
           "as 0 would satisfy `exactly 0` vacuously";
    EXPECT_FALSE(r.indeterminate)
        << "an unnamed ordinal is a hard failure, not missing dump data";
    EXPECT_NE(std::string::npos,
              r.message.find("unknown event-kind ordinal 99"))
        << "the message must name the offending ordinal; got: " << r.message;

    // Control: a NAMED ordinal with the same expectation on the same empty
    // dump passes, so the failure above is about the ordinal, not the dump.
    const og::parity::FactEvalResult ok = og::parity::evaluate_one(
        og::parity::pred::EventKindExactly(/*kind_ordinal=*/0, /*exact=*/0,
                                           "named ordinal"),
        dump);
    EXPECT_TRUE(ok.ok)
        << "ordinal 0 (\"none\") is named; zero such events on an empty dump "
           "satisfies `exactly 0`";
    EXPECT_EQ(std::string(), ok.message) << "a passing predicate says nothing";

    // The at-least arm carries the same guard.
    const og::parity::FactEvalResult at_least = og::parity::evaluate_one(
        og::parity::pred::EventKindAtLeast(/*kind_ordinal=*/99, /*min=*/0,
                                           "bogus ordinal"),
        dump);
    EXPECT_FALSE(at_least.ok)
        << "EventKindAtLeast must reject an unnamed ordinal too; `at least 0` "
           "is the most vacuous row of all";
    EXPECT_NE(std::string::npos,
              at_least.message.find("unknown event-kind ordinal 99"))
        << "got: " << at_least.message;
}

TEST(FactPredicate, fact_kind_name_renders_every_enumerator)
{
    // One switch for the whole harness (parity_runner_smoke --facts and
    // scenario_facts_dump's committed JSON both print from it), so a
    // misspelling here renames the predicate in every generated artefact.
    EXPECT_STREQ("TickReached",
                 og::parity::fact_kind_name(og::parity::FactKind::TickReached))
        << "the first enumerator keeps its name";
    EXPECT_STREQ("TreasureFamilyOfOrderRemovedFromOblist",
                 og::parity::fact_kind_name(
                     og::parity::FactKind::TreasureFamilyOfOrderRemovedFromOblist))
        << "the longest name is spelt in full, not truncated";
    EXPECT_STREQ("WalkerOnFloor",
                 og::parity::fact_kind_name(og::parity::FactKind::WalkerOnFloor))
        << "the multi-floor kind keeps its name";
    EXPECT_STREQ("Unknown",
                 og::parity::fact_kind_name(
                     static_cast<og::parity::FactKind>(0xFF)))
        << "a value no enumerator names falls through to the post-switch "
           "guard, never to a neighbouring kind's name";
}
