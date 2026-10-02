#include <openglad/gameplay/lobby_state.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <tuple>
#include <variant>
#include <vector>

namespace og::sim {

StartDenialReason start_denial_reason_from_wire(std::uint8_t value) noexcept
{
    return value <= start_denial_reason_value(StartDenialReason::StageFailed)
        ? static_cast<StartDenialReason>(value)
        : StartDenialReason::None;
}

bool start_denial_matches_request(
    const LobbyState& state,
    std::uint32_t pending_request_id) noexcept
{
    return pending_request_id != 0 &&
        state.last_start_request_id == pending_request_id &&
        state.last_start_denial != start_denial_reason_value(
            StartDenialReason::None);
}

bool start_confirmation_matches_request(
    const LobbyMessage& message,
    std::uint32_t pending_request_id) noexcept
{
    const auto* const start =
        std::get_if<LobbyStartGameMessage>(&message.payload);
    return start != nullptr &&
        (pending_request_id == 0 || start->request_id == pending_request_id);
}

std::vector<OrderedLobbySlot> order_lobby_slots(
    const std::vector<LobbyPlayer>& players,
    bool deployed_only)
{
    std::vector<OrderedLobbySlot> ordered;
    for (std::size_t player_order = 0; player_order < players.size(); ++player_order)
    {
        const LobbyPlayer& player = players[player_order];
        for (std::size_t slot_order = 0; slot_order < player.character_slots.size();
             ++slot_order)
        {
            const LobbyCharacterSlot& slot = player.character_slots[slot_order];
            if (deployed_only && !slot.deployed)
                continue;
            ordered.push_back(OrderedLobbySlot{
                .slot_index = slot.slot_index,
                .player_order = player_order,
                .slot_order = slot_order,
                .player = &player,
                .slot = &slot,
            });
        }
    }

    std::sort(ordered.begin(), ordered.end(),
              [](const OrderedLobbySlot& lhs, const OrderedLobbySlot& rhs) {
                  return std::tie(lhs.slot_index, lhs.player_order, lhs.slot_order) <
                      std::tie(rhs.slot_index, rhs.player_order, rhs.slot_order);
              });
    return ordered;
}

std::optional<std::vector<LobbyCharacterSlot>> assemble_gameplay_roster(
    const std::vector<OrderedLobbySlot>& ordered)
{
    if (ordered.size() > kMaxLobbyTeamSize)
        return std::nullopt;

    std::vector<LobbyCharacterSlot> roster;
    roster.reserve(ordered.size());
    for (std::size_t index = 0; index < ordered.size(); ++index)
    {
        LobbyCharacterSlot gameplay_slot = *ordered[index].slot;
        gameplay_slot.slot_index = static_cast<std::uint8_t>(index);
        gameplay_slot.owner_player_index = ordered[index].player->player_index;
        gameplay_slot.owner_save_slot = ordered[index].slot_index;
        roster.push_back(std::move(gameplay_slot));
    }
    canonicalize_lobby_gameplay_guy_ids(roster);
    return roster;
}

} // namespace og::sim
