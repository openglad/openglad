#include <openglad/gameplay/lobby_state.h>

#include <cstdint>
#include <variant>

namespace og::sim {

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

} // namespace og::sim
