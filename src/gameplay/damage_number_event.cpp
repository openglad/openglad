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
    // rule: A damage number's position crosses the wire clamped to 0..65535 (negative/NaN to 0,
    //   huge saturates).
    // tests: DamageNumberEvent.coordinates_saturate_at_the_uint16_ceiling
    // tests: DamageNumberEvent.encode_decode_pins_every_field
    // tests: SemDamageNumber.a_nan_or_zero_coordinate_crosses_as_zero_and_fractions_truncate
    if (!(value > 0.0f))
        return 0u;
    // rule: Part of the rule above that begins "A damage number's position crosses the wire clamped
    //   to ..."
    // tests: DamageNumberEvent.coordinates_saturate_at_the_uint16_ceiling
    // tests: DamageNumberEvent.encode_decode_pins_every_field
    // tests: SemDamageNumber.a_nan_or_zero_coordinate_crosses_as_zero_and_fractions_truncate
    if (value >= 65535.0f)
        return 65535u;
    return static_cast<std::uint32_t>(value);
}

std::uint32_t pack_value_q8(float value)
{
    // rule: A damage number's amount crosses the wire in 1/256 hp, negative clamps to 0, over-range
    //   saturates at 65535.996.
    // tests: DamageNumberEvent.value_quantization_is_exact_for_integers_and_saturates
    if (!(value > 0.0f))
        return 0u;
    const float scaled = std::round(value * 256.0f);
    // rule: Part of the rule above that begins "A damage number's amount crosses the wire in 1/256
    //   ..."
    // tests: DamageNumberEvent.value_quantization_is_exact_for_integers_and_saturates
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
    // rule: Only damage-number events decode as damage numbers.
    // tests: DamageNumberEvent.decode_rejects_every_other_kind
    // tests: DamageNumberEvent.encode_decode_pins_every_field
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
        // rule: Each tick at most kMaxLiftedDamageNumbersPerTick damage numbers are sent to
        //   mirrors; the players' own bodies spend the budget first, then everyone else; a walker
        //   with no entity id is never sent; every walker's pending list is emptied every tick
        //   whether sent or dropped.
        // tests: DamageNumberEvent.lift_budget_caps_the_tick_and_seat_bound_owners_are_lifted_first
        // tests: SemDamageNumber.a_walker_without_an_entity_id_is_never_lifted_but_is_drained
        for (const walker::DamageNumber& number : entity->damage_numbers)
        {
            // rule: Part of the rule above that begins "Each tick at most
            //   kMaxLiftedDamageNumbersPerTick damage numbers are sent ..."
            // tests: DamageNumberEvent.lift_budget_caps_the_tick_and_seat_bound_owners_are_lifted_first
            // tests: SemDamageNumber.a_walker_without_an_entity_id_is_never_lifted_but_is_drained
            if (budget == 0u)
                break;
            out.push_back(encode_damage_number_event(owner_entity_id, number));
            --budget;
        }
    };

    // Pass 1: the seats. A player's own pane must never lose its numbers to a
    // bystander's when the budget bites, so bound owners spend it first.
    // rule: Part of the rule above that begins "Each tick at most kMaxLiftedDamageNumbersPerTick
    //   damage numbers are sent ..."
    // tests: DamageNumberEvent.lift_budget_caps_the_tick_and_seat_bound_owners_are_lifted_first
    // tests: SemDamageNumber.a_walker_without_an_entity_id_is_never_lifted_but_is_drained
    for (auto& uptr : world.oblist)
    {
        walker* const entity = uptr.get();
        // rule: Part of the rule above that begins "Each tick at most
        //   kMaxLiftedDamageNumbersPerTick damage numbers are sent ..."
        // tests: DamageNumberEvent.lift_budget_caps_the_tick_and_seat_bound_owners_are_lifted_first
        // tests: SemDamageNumber.a_walker_without_an_entity_id_is_never_lifted_but_is_drained
        if (entity == nullptr || entity->damage_numbers.empty())
            continue;
        // rule: Part of the rule above that begins "Each tick at most
        //   kMaxLiftedDamageNumbersPerTick damage numbers are sent ..."
        // tests: DamageNumberEvent.lift_budget_caps_the_tick_and_seat_bound_owners_are_lifted_first
        // tests: SemDamageNumber.a_walker_without_an_entity_id_is_never_lifted_but_is_drained
        if (entity->entity_id() == 0u || !bound_to_a_seat(entity))
            continue;
        lift(entity);
    }

    // Pass 2: everyone else. Spectator and follow panes watch walkers no seat
    // owns, and the render already gates the paint per pane
    // (walker_draw.cpp, "only seen by the people they affect"), so delivery is
    // the only half that was missing.
    // rule: Part of the rule above that begins "Each tick at most kMaxLiftedDamageNumbersPerTick
    //   damage numbers are sent ..."
    // tests: DamageNumberEvent.lift_budget_caps_the_tick_and_seat_bound_owners_are_lifted_first
    // tests: SemDamageNumber.a_walker_without_an_entity_id_is_never_lifted_but_is_drained
    for (auto& uptr : world.oblist)
    {
        walker* const entity = uptr.get();
        // rule: Part of the rule above that begins "Each tick at most
        //   kMaxLiftedDamageNumbersPerTick damage numbers are sent ..."
        // tests: DamageNumberEvent.lift_budget_caps_the_tick_and_seat_bound_owners_are_lifted_first
        // tests: SemDamageNumber.a_walker_without_an_entity_id_is_never_lifted_but_is_drained
        if (entity == nullptr || entity->damage_numbers.empty())
            continue;
        // rule: Part of the rule above that begins "Each tick at most
        //   kMaxLiftedDamageNumbersPerTick damage numbers are sent ..."
        // tests: DamageNumberEvent.lift_budget_caps_the_tick_and_seat_bound_owners_are_lifted_first
        // tests: SemDamageNumber.a_walker_without_an_entity_id_is_never_lifted_but_is_drained
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
    // rule: A mirror attaches each received damage number to its owner by id, drops (with trace
    //   'damage_numbers'/'drop unknown owner') a number whose owner it does not know, ignores
    //   non-damage events, and keeps at most kMaxMirrorDamageNumbers per walker, oldest dropped
    //   first.
    // tests: DamageNumberEvent.apply_drops_unknown_owner_and_caps_list
    // tests: SemDamageNumber.a_mirror_ignores_events_of_any_other_kind
    for (const Event& event : batch.events)
    {
        const std::optional<DecodedDamageNumber> decoded =
            decode_damage_number_event(event);
        // rule: Part of the rule above that begins "A mirror attaches each received damage number
        //   to its ..."
        // tests: DamageNumberEvent.apply_drops_unknown_owner_and_caps_list
        // tests: SemDamageNumber.a_mirror_ignores_events_of_any_other_kind
        if (!decoded.has_value())
            continue;

        walker* const owner = world.find_by_id(decoded->owner_entity_id);
        // rule: Part of the rule above that begins "A mirror attaches each received damage number
        //   to its ..."
        // tests: DamageNumberEvent.apply_drops_unknown_owner_and_caps_list
        // tests: SemDamageNumber.a_mirror_ignores_events_of_any_other_kind
        if (owner == nullptr)
        {
            TRACE("damage_numbers", "drop unknown owner=%u",
                  decoded->owner_entity_id);
            continue;
        }

        owner->damage_numbers.push_back(decoded->number);
        // rule: Part of the rule above that begins "A mirror attaches each received damage number
        //   to its ..."
        // tests: DamageNumberEvent.apply_drops_unknown_owner_and_caps_list
        // tests: SemDamageNumber.a_mirror_ignores_events_of_any_other_kind
        while (owner->damage_numbers.size() > kMaxMirrorDamageNumbers)
            owner->damage_numbers.pop_front();
        ++applied;
    }
    return applied;
}

} // namespace og::sim
