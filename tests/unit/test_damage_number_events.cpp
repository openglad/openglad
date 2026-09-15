// The DamageNumber wire codec (P13): the 2013 floating hit/heal overlay was
// dead on every client from #106 (the mirror world never ticks the sim and
// apply_snapshot cleared the list). These tests pin the packing GameServer
// lifts with and GameClient applies.

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
