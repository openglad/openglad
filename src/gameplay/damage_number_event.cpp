#include <openglad/gameplay/damage_number_event.h>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/world_snapshot.h>

#include <algorithm>
#include <cmath>

namespace og::sim {

namespace {

constexpr std::uint32_t kMaxValueQ8 = 0x00FFFFFFu; // 65535.99609375 hp

std::uint32_t pack_coordinate(float value)
{
    if (!(value > 0.0f))
        return 0u;
    if (value >= 65535.0f)
        return 65535u;
    return static_cast<std::uint32_t>(value);
}

std::uint32_t pack_value_q8(float value)
{
    if (!(value > 0.0f))
        return 0u;
    const float scaled = std::round(value * 256.0f);
    if (scaled >= static_cast<float>(kMaxValueQ8))
        return kMaxValueQ8;
    return static_cast<std::uint32_t>(scaled);
}

} // namespace

Event encode_damage_number_event(std::uint32_t owner_entity_id,
                                 const walker::DamageNumber& number)
{
    Event event;
    event.tick = number.created_tick;
    event.kind = EventKind::DamageNumber;
    event.a = owner_entity_id;
    event.b = (pack_coordinate(number.x) << 16) | pack_coordinate(number.y);
    event.c = (static_cast<std::uint32_t>(number.color) << 24) |
              pack_value_q8(number.value);
    event.target_player = -1;
    return event;
}

std::optional<DecodedDamageNumber> decode_damage_number_event(
    const Event& event)
{
    if (event.kind != EventKind::DamageNumber)
        return std::nullopt;

    DecodedDamageNumber decoded;
    decoded.owner_entity_id = event.a;
    decoded.number = walker::DamageNumber(
        static_cast<float>((event.b >> 16) & 0xFFFFu),
        static_cast<float>(event.b & 0xFFFFu),
        static_cast<float>(event.c & kMaxValueQ8) / 256.0f,
        static_cast<unsigned char>((event.c >> 24) & 0xFFu),
        event.tick);
    return decoded;
}

void lift_damage_number_events(
    GameWorld& world,
    const std::array<walker*, kMaxGlobalPlayers>& player_controls,
    std::vector<Event>& out)
{
    std::size_t budget = kMaxLiftedDamageNumbersPerTick;

    const auto bound_to_a_seat = [&player_controls](const walker* entity) {
        return std::any_of(
            player_controls.begin(), player_controls.end(),
            [entity](const walker* control) { return control == entity; });
    };

    const auto lift = [&budget, &out](walker* entity) {
        const std::uint32_t owner_entity_id = entity->entity_id();
        for (const walker::DamageNumber& number : entity->damage_numbers)
        {
            if (budget == 0u)
                break;
            out.push_back(encode_damage_number_event(owner_entity_id, number));
            --budget;
        }
    };

    // Pass 1: the seats. A player's own pane must never lose its numbers to a
    // bystander's when the budget bites, so bound owners spend it first.
    for (auto& uptr : world.oblist)
    {
        walker* const entity = uptr.get();
        if (entity == nullptr || entity->damage_numbers.empty())
            continue;
        if (entity->entity_id() == 0u || !bound_to_a_seat(entity))
            continue;
        lift(entity);
    }

    // Pass 2: everyone else. Spectator and follow panes watch walkers no seat
    // owns, and the render already gates the paint per pane
    // (walker_draw.cpp, "only seen by the people they affect"), so delivery is
    // the only half that was missing.
    for (auto& uptr : world.oblist)
    {
        walker* const entity = uptr.get();
        if (entity == nullptr || entity->damage_numbers.empty())
            continue;
        if (entity->entity_id() != 0u && !bound_to_a_seat(entity))
            lift(entity);

        // Unconditional: render is the only eraser in the classic code, so a
        // headless authority (every server, including the local shadow) would
        // otherwise grow these lists for the life of the level.
        entity->damage_numbers.clear();
    }
}

std::size_t apply_damage_number_events(GameWorld& world,
                                       const SimEventBatch& batch)
{
    std::size_t applied = 0u;
    for (const Event& event : batch.events)
    {
        const std::optional<DecodedDamageNumber> decoded =
            decode_damage_number_event(event);
        if (!decoded.has_value())
            continue;

        walker* const owner = world.find_by_id(decoded->owner_entity_id);
        if (owner == nullptr)
        {
            TRACE("damage_numbers", "drop unknown owner=%u",
                  decoded->owner_entity_id);
            continue;
        }

        owner->damage_numbers.push_back(decoded->number);
        while (owner->damage_numbers.size() > kMaxMirrorDamageNumbers)
            owner->damage_numbers.pop_front();
        ++applied;
    }
    return applied;
}

} // namespace og::sim
