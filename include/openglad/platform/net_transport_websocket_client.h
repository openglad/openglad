#pragma once

#include <openglad/gameplay/net_transport.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace og::sim {

class WebSocketClientTransport final : public ITransport
{
public:
    struct Options {
        PeerId remote_peer_id = 1;
        bool automatic_reconnection = true;
        std::uint32_t min_reconnect_wait_ms = 1;
        // IXWebSocket doubles the wait after each failed dial (2^n * 100 ms)
        // and clamps it here. Ten seconds meant a host that blipped for a few
        // seconds went unnoticed for up to ten more after it came back; one
        // second is the whole recovery latency a returning host can cost.
        // The RELAY transport keeps its own ten-second clamp on purpose: that
        // one re-dials Cloudflare, not a peer on the LAN.
        std::uint32_t max_reconnect_wait_ms = 1'000;
        // How long a single dial may occupy the io thread before it is
        // abandoned. A dial that is answered by a socket nobody services —
        // ix's acceptor leaks one on every stop()-vs-dial race
        // (IXSocketServer.cpp:415 accepts, then returns on its stop flag
        // without closing the descriptor) — parks the io thread in the HTTP
        // status-line read, and no re-dial runs while it waits. ix's own
        // default is sixty seconds, which is a minute of a returning host
        // going unnoticed. Ten seconds bounds that while still covering DNS,
        // TCP connect and upgrade for a direct join typed at a WAN address:
        // this UI accepts any ws:// address, not only a peer on the LAN.
        int handshake_timeout_secs = 10;
        // WebSocket PING/PONG on the direct link, in seconds. Without an
        // interval ix's poll() timeout is infinite and a half-open link is
        // never noticed at all: the io thread either sits on a socket that
        // will never speak again or spins on poll() -> Error forever, and
        // either way it never re-dials. With one, ix sends a heartbeat every
        // interval and closes the socket when the pong does not come back
        // (kPingTimeoutMessage), which lets its reconnection loop run. The
        // RELAY transport carries no pings on purpose: that one talks to
        // Cloudflare, not to a peer.
        int ping_interval_secs = 5;
    };

    // IXWebSocket invokes callbacks on background I/O threads. Those callbacks
    // only enqueue connection/message/disconnection events here; the single
    // remote peer state is updated later on the game thread from poll(). A send
    // while disconnected or while IXWebSocket is reconnecting is treated as a
    // no-op instead of throwing from the game thread.
    explicit WebSocketClientTransport(std::string url);
    WebSocketClientTransport(std::string url, Options options);
    ~WebSocketClientTransport() override;

    WebSocketClientTransport(const WebSocketClientTransport&) = delete;
    WebSocketClientTransport& operator=(const WebSocketClientTransport&) = delete;
    WebSocketClientTransport(WebSocketClientTransport&&) = delete;
    WebSocketClientTransport& operator=(WebSocketClientTransport&&) = delete;

    void send(PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override;
    [[nodiscard]] std::vector<ReceivedMessage> poll() override;
    void accept_connections() override;
    void disconnect(PeerId peer_id) override;
    [[nodiscard]] std::vector<PeerId> connected_peers() const override;
    [[nodiscard]] TransportLinkState link_state() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace og::sim
