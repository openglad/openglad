// The DamageNumber wire codec (P13): the 2013 floating hit/heal overlay was
// dead on every client from #106 (the mirror world never ticks the sim and
// apply_snapshot cleared the list). These tests pin the packing GameServer
// lifts with and GameClient applies.

#include <openglad/core/constants.h>
#include <openglad/gameplay/damage_number_event.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "../test_game_world_fixture.h"

namespace {

walker::DamageNumber make_number(float x,
                                 float y,
                                 float value,
                                 unsigned char color,
                                 std::uint32_t created_tick)
{
    return walker::DamageNumber(x, y, value, color, created_tick);
}

float encode_decode_value(float value)
{
    const og::sim::Event event =
        og::sim::encode_damage_number_event(7u, make_number(0, 0, value, 40, 0));
    const auto decoded = og::sim::decode_damage_number_event(event);
    EXPECT_TRUE(decoded.has_value()) << "a DamageNumber event must decode";
    return decoded.has_value() ? decoded->number.value : -1.0f;
}

} // namespace

TEST(DamageNumberEvent, encode_decode_pins_every_field)
{
    const og::sim::Event event = og::sim::encode_damage_number_event(
        42u, make_number(1234.0f, 567.0f, 7.0f, 235, 99u));

    EXPECT_EQ(og::sim::EventKind::DamageNumber, event.kind)
        << "the lifted overlay rides kind 19";
    EXPECT_EQ(42u, event.a) << "a carries the owner entity id";
    EXPECT_EQ(0x04D20237u, event.b)
        << "b packs (uint16 x << 16) | uint16 y in world pixels";
    EXPECT_EQ(0xEB000700u, event.c)
        << "c packs (colour << 24) | value as unsigned 16.8 fixed point";
    EXPECT_EQ(99u, event.tick) << "tick carries DamageNumber::created_tick";
    EXPECT_EQ(-1, event.target_player)
        << "the per-pane filter is a render rule, not an addressee";
    EXPECT_TRUE(event.text.empty()) << "the overlay carries no text";

    const auto decoded = og::sim::decode_damage_number_event(event);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(42u, decoded->owner_entity_id);
    EXPECT_FLOAT_EQ(1234.0f, decoded->number.x);
    EXPECT_FLOAT_EQ(567.0f, decoded->number.y);
    EXPECT_FLOAT_EQ(7.0f, decoded->number.value);
    EXPECT_EQ(235, static_cast<int>(decoded->number.color));
    EXPECT_EQ(99u, decoded->number.created_tick);
    EXPECT_FLOAT_EQ(1.0f, decoded->number.t)
        << "a freshly decoded number starts its rise at full alpha";
}

TEST(DamageNumberEvent, value_quantization_is_exact_for_integers_and_saturates)
{
    // Every combat/heal/flight amount is a short or the literal 1, so the
    // 1/256 grid is exact for them; only fall damage is a float.
    EXPECT_FLOAT_EQ(30.0f, encode_decode_value(30.0f));
    EXPECT_FLOAT_EQ(1.0f, encode_decode_value(1.0f));
    EXPECT_FLOAT_EQ(3.30078125f, encode_decode_value(3.3f))
        << "fall damage is quantized to 1/256 hp (the renderer prints %.0f)";
    EXPECT_FLOAT_EQ(65535.99609375f, encode_decode_value(70000.0f))
        << "an out-of-range value saturates instead of wrapping";
    EXPECT_FLOAT_EQ(0.0f, encode_decode_value(-5.0f))
        << "a negative value clamps to zero instead of wrapping";
}

TEST(DamageNumberEvent, coordinates_saturate_at_the_uint16_ceiling)
{
    const og::sim::Event event = og::sim::encode_damage_number_event(
        1u, make_number(70000.0f, -3.0f, 1.0f, 40, 0u));
    const auto decoded = og::sim::decode_damage_number_event(event);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FLOAT_EQ(65535.0f, decoded->number.x);
    EXPECT_FLOAT_EQ(0.0f, decoded->number.y);
}

TEST(DamageNumberEvent, decode_rejects_every_other_kind)
{
    og::sim::Event notification;
    notification.kind = og::sim::EventKind::Notification;
    notification.a = 5u;
    EXPECT_FALSE(og::sim::decode_damage_number_event(notification).has_value());
}

TEST(DamageNumberEvent, is_not_a_game_flow_event)
{
    EXPECT_FALSE(og::sim::is_game_flow_event(og::sim::EventKind::DamageNumber))
        << "the overlay rides the cosmetic sim batch, not the game-flow batch";
}

TEST(DamageNumberEvent, batch_roundtrip_keeps_c_and_the_target_player_offset)
{
    og::sim::SimEventBatch batch;
    batch.sequence = 3u;
    og::sim::Event notification;
    notification.tick = 4u;
    notification.kind = og::sim::EventKind::Notification;
    notification.target_player = 6;
    notification.text = "";
    batch.events.push_back(notification);
    batch.events.push_back(og::sim::encode_damage_number_event(
        42u, make_number(1234.0f, 567.0f, 7.0f, 235, 99u)));

    ASSERT_EQ(0u, batch.events.front().c)
        << "every pre-existing kind leaves the third scalar at zero";

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_sim_event_batch(batch);
    const og::sim::SimEventBatch decoded =
        og::sim::deserialize_sim_event_batch(bytes);
    EXPECT_EQ(batch.sequence, decoded.sequence);
    ASSERT_EQ(2u, decoded.events.size());
    EXPECT_EQ(batch.events, decoded.events)
        << "Event::operator== is defaulted, so c must survive the round trip";
    EXPECT_EQ(0xEB000700u, decoded.events.back().c);

    // #230 offset pin: the addressee still rides between b and text, so the
    // new scalar appended after the string moved nothing. Payload layout is
    // [header][sequence u32][count u32] then per event
    // [tick][kind][a][b][target_player][text len + bytes][c].
    const std::size_t target_player_offset =
        og::sim::kTransportHeaderSize + 4u + 4u + (4u * 4u);
    EXPECT_EQ(6u, bytes[target_player_offset + 0]);
    EXPECT_EQ(0u, bytes[target_player_offset + 1]);
    EXPECT_EQ(0u, bytes[target_player_offset + 2]);
    EXPECT_EQ(0u, bytes[target_player_offset + 3])
        << "the notification's target_player 6 must still start at byte "
        << target_player_offset;
}

TEST(DamageNumberEvent, apply_drops_unknown_owner_and_caps_list)
{
    TestGameWorld fixture;
    walker* const owner =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, owner) << "the mirror needs one walker to own numbers";
    owner->setxy(64, 64);

    og::sim::SimEventBatch stranger;
    stranger.events.push_back(og::sim::encode_damage_number_event(
        999999u, make_number(1.0f, 2.0f, 3.0f, 40, 0u)));
    EXPECT_EQ(0u,
              og::sim::apply_damage_number_events(fixture.world(), stranger))
        << "an event for an id this mirror does not hold is dropped";
    EXPECT_TRUE(owner->damage_numbers.empty());

    og::sim::SimEventBatch flood;
    for (int i = 0; i < 70; ++i)
    {
        flood.events.push_back(og::sim::encode_damage_number_event(
            owner->entity_id(),
            make_number(0.0f, 0.0f, static_cast<float>(i), 40,
                        static_cast<std::uint32_t>(i))));
    }
    EXPECT_EQ(70u, og::sim::apply_damage_number_events(fixture.world(), flood))
        << "every event with a known owner counts as applied";
    ASSERT_EQ(og::sim::kMaxMirrorDamageNumbers, owner->damage_numbers.size())
        << "a hostile server must not grow the mirror overlay without bound";
    EXPECT_FLOAT_EQ(6.0f, owner->damage_numbers.front().value)
        << "the OLDEST entries are the ones dropped (70 - 64 = 6 gone)";
    EXPECT_FLOAT_EQ(69.0f, owner->damage_numbers.back().value);
}

// Q3: the lift walks EVERY oblist walker, but a tick's batch has to fit the
// 16-bit wire length (serialize_event_batch_message throws above 65535 bytes),
// so kMaxLiftedDamageNumbersPerTick bounds it. Two rules are pinned here: the
// budget is exact, and the SEAT-BOUND owner is lifted before the budget is
// spent even when oblist puts it LAST — a player's own pane must never lose
// its numbers to a bystander's. Every list is drained regardless, including
// the ones whose numbers were dropped.
TEST(DamageNumberEvent, lift_budget_caps_the_tick_and_seat_bound_owners_are_lifted_first)
{
    TestGameWorld fixture;
    GameWorld& world = fixture.world();

    constexpr int kWalkerCount = 8;
    constexpr int kNumbersEach = 100;
    std::vector<walker*> owners;
    for (int i = 0; i < kWalkerCount; ++i)
    {
        walker* const owner = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, owner) << "walker " << i << " must spawn";
        owner->setxy(static_cast<short>(32 + 16 * i),
                     static_cast<short>(64));
        ASSERT_NE(0u, owner->entity_id())
            << "the lift skips id 0, so the fixture must hand out real ids";
        for (int n = 1; n <= kNumbersEach; ++n)
        {
            owner->damage_numbers.push_back(make_number(
                static_cast<float>(owner->xpos()),
                static_cast<float>(owner->ypos()),
                static_cast<float>(n), RED, static_cast<std::uint32_t>(n)));
        }
        owners.push_back(owner);
    }

    walker* const bound = owners.back();
    ASSERT_EQ(bound, world.oblist.back().get())
        << "the seat-bound owner must be LAST in oblist, or pass-1 ordering "
           "would be proved by accident";
    const std::uint32_t bound_id = bound->entity_id();

    std::array<walker*, og::sim::kMaxGlobalPlayers> controls{};
    controls[0] = bound;

    std::vector<og::sim::Event> out;
    og::sim::lift_damage_number_events(world, controls, out);

    ASSERT_EQ(og::sim::kMaxLiftedDamageNumbersPerTick, out.size())
        << "the per-tick budget is exact: 8 x 100 queued, 512 lifted";

    int bound_events = 0;
    int other_events = 0;
    for (const og::sim::Event& event : out)
    {
        const auto decoded = og::sim::decode_damage_number_event(event);
        ASSERT_TRUE(decoded.has_value()) << "the lift emits only kind 19";
        if (decoded->owner_entity_id == bound_id)
            ++bound_events;
        else
            ++other_events;
    }
    EXPECT_EQ(kNumbersEach, bound_events)
        << "the seat-bound owner is lifted before the budget is spent";
    EXPECT_EQ(static_cast<int>(og::sim::kMaxLiftedDamageNumbersPerTick) -
                  kNumbersEach,
              other_events)
        << "the remaining budget goes to the unbound owners";

    for (std::size_t i = 0; i < static_cast<std::size_t>(kNumbersEach); ++i)
    {
        const auto decoded = og::sim::decode_damage_number_event(out[i]);
        ASSERT_TRUE(decoded.has_value());
        ASSERT_EQ(bound_id, decoded->owner_entity_id)
            << "event " << i << " must belong to the seat-bound owner: "
               "pass 1 runs to completion before pass 2 starts";
    }

    for (std::size_t i = 0; i < owners.size(); ++i)
    {
        EXPECT_TRUE(owners[i]->damage_numbers.empty())
            << "walker " << i
            << " must be drained even when its numbers were dropped by the "
               "budget: render is the only eraser, so an undrained list grows "
               "for the life of the level";
    }
}
