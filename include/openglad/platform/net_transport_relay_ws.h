#pragma once

#include <openglad/gameplay/net_transport.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace og::sim {

class RelayWebSocketTransport final : public ITransport
{
public:
    struct Options {
        bool automatic_reconnection = true;
        std::uint32_t min_reconnect_wait_ms = 1;
        std::uint32_t max_reconnect_wait_ms = 10'000;
        std::string protocols;
    };

    explicit RelayWebSocketTransport(std::string url);
    RelayWebSocketTransport(std::string url, Options options);
    ~RelayWebSocketTransport() override;

    RelayWebSocketTransport(const RelayWebSocketTransport&) = delete;
    RelayWebSocketTransport& operator=(const RelayWebSocketTransport&) = delete;
    RelayWebSocketTransport(RelayWebSocketTransport&&) = delete;
    RelayWebSocketTransport& operator=(RelayWebSocketTransport&&) = delete;

    using ITransport::broadcast;

    void send(PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override;
    void broadcast(const std::uint8_t* data, std::size_t len) override;
    [[nodiscard]] std::vector<ReceivedMessage> poll() override;
    void accept_connections() override;
    void disconnect(PeerId peer_id) override;
    [[nodiscard]] std::vector<PeerId> connected_peers() const override;
    [[nodiscard]] TransportLinkState link_state() const noexcept override;

    [[nodiscard]] std::optional<PeerId> local_peer_id() const noexcept;
    [[nodiscard]] std::optional<PeerId> host_peer_id() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace og::sim
