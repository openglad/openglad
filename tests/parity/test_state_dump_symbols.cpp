// Teeth for the two symbol renderers in tests/parity/state_dump.cpp.
//
// Both switches lost their `default:` arm so that -Wswitch (and the -Werror
// lanes) turn a NEW enumerator into a compile error instead of a silently
// numbered symbol. That compile-time half is invisible to a test run, so these
// cases pin the runtime half: every named EventKind renders its committed
// symbol (a golden-byte producer — state_dump.cpp writes the return value into
// the dump's events array), the symbols stay distinct, and the post-switch
// fallback still numbers a raw value no enumerator names.
//
// DamageNumber is the enumerator the old `default:` arm hid: it rendered as
// "kind_19" until the case was added. No golden carries a `kind_` symbol, so
// naming it moved zero bytes.
#include "event_kind_symbol_pins.h"
#include "state_dump.h"

#include <openglad/core/order.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>

using og::parity::event_kind_symbol;
using og::parity::family_symbol_by_order;

TEST(EventKindSymbol, every_named_kind_renders_its_pinned_symbol)
{
    for (const auto& [kind, symbol] : kEventKindSymbolPins)
    {
        const auto raw = static_cast<std::uint32_t>(kind);
        EXPECT_EQ(std::string(symbol), event_kind_symbol(raw))
            << "EventKind enumerator with value " << raw
            << " must render the symbol \"" << symbol
            << "\" — these strings are written into the parity goldens";
    }
}

TEST(EventKindSymbol, no_two_named_kinds_share_a_symbol)
{
    std::set<std::string> rendered;
    for (const auto& [kind, symbol] : kEventKindSymbolPins)
    {
        (void)symbol;
        rendered.insert(event_kind_symbol(static_cast<std::uint32_t>(kind)));
    }
    EXPECT_EQ(std::size_t{12}, rendered.size())
        << "each named EventKind must render a DISTINCT symbol; a duplicate "
           "would make two kinds indistinguishable in a golden dump";
}

TEST(EventKindSymbol, out_of_range_kind_renders_the_numeric_fallback)
{
    EXPECT_EQ(std::string("kind_255"), event_kind_symbol(0xFFu))
        << "a raw value no enumerator names must fall through the switch to "
           "the numeric guard, not to a silent placeholder";
    EXPECT_EQ(std::string("kind_1"), event_kind_symbol(1u))
        << "1 is a GAP in the sparse EventKind numbering (0, 4, 8, 11..19); "
           "it must render numerically, never as a neighbouring symbol";
}

TEST(FamilySymbol, unhandled_orders_render_the_unknown_marker)
{
    EXPECT_EQ(std::string("FAMILY_UNKNOWN_5_0"),
              family_symbol_by_order(static_cast<std::int32_t>(Order::Special), 0))
        << "Order::Special has no family table on purpose: its arm must leave "
           "table==nullptr so the unknown marker renders";
    EXPECT_EQ(std::string("FAMILY_UNKNOWN_6_0"),
              family_symbol_by_order(static_cast<std::int32_t>(Order::Button1), 0))
        << "Order::Button1 has no family table on purpose: its arm must leave "
           "table==nullptr so the unknown marker renders";
    EXPECT_EQ(std::string("FAMILY_SOLDIER"),
              family_symbol_by_order(static_cast<std::int32_t>(Order::Living), 0))
        << "control: a MAPPED order must still reach its table";
    EXPECT_EQ(std::string("FAMILY_UNKNOWN_0_999"),
              family_symbol_by_order(static_cast<std::int32_t>(Order::Living), 999))
        << "a family id past the end of a mapped table must render the "
           "unknown marker, never read out of bounds";
}
