// Unit tests for the FactKind dispatch in tests/parity/fact_predicate.cpp.
//
// evaluate_one's switch has no `default:` arm on purpose — that is what keeps
// -Wswitch (and therefore the -Werror lanes) turning a NEWLY ADDED enumerator
// into a compile error instead of a silent pass. The guard for a value no
// enumerator names is the mandatory post-switch return, and these tests pin
// that it FAILS loudly rather than returning the default-constructed ok=true
// result. Without them a cast, a corrupted table entry or a stale serialised
// kind would report every scenario row as satisfied.
#include "fact_predicate.h"
#include "state_dump.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

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
