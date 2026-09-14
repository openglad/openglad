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

#include <gtest/gtest.h>

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
