#pragma once

#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/world_snapshot.h>

#include <cstdint>
#include <optional>
#include <vector>

struct InputState;
class GameWorld;

namespace og::sim {

class GameClient
{
public:
    explicit GameClient(ITransport& transport,
                        PeerId server_peer_id,
                        GameWorld* world = nullptr);

    void send_input(const InputState& input, std::uint32_t tick);
    void poll_messages();

    [[nodiscard]] const std::optional<WorldSnapshot>& baseline() const noexcept
    {
        return baseline_;
    }

    [[nodiscard]] const std::vector<TypedReceivedMessage>&
    last_polled_messages() const noexcept
    {
        return last_polled_messages_;
    }

    [[nodiscard]] const std::vector<SimEventBatch>&
    sim_event_batches() const noexcept
    {
        return sim_event_batches_;
    }

    [[nodiscard]] const std::vector<SimEventBatch>&
    game_flow_event_batches() const noexcept
    {
        return game_flow_event_batches_;
    }

private:
    ITransport& transport_;
    PeerId server_peer_id_ = 0;
    GameWorld* world_ = nullptr;
    std::optional<WorldSnapshot> baseline_ = std::nullopt;
    std::vector<TypedReceivedMessage> last_polled_messages_;
    std::vector<SimEventBatch> sim_event_batches_;
    std::vector<SimEventBatch> game_flow_event_batches_;
};

} // namespace og::sim
