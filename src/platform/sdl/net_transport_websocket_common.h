#pragma once

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <chrono>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace og::sim::detail {

class IxNetSystemGuard
{
public:
    IxNetSystemGuard()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ref_count_++ == 0 && !ix::initNetSystem())
        {
            ref_count_ = 0;
            throw std::runtime_error("IXWebSocket failed to initialize the network system");
        }
    }

    ~IxNetSystemGuard()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ref_count_ == 0)
            return;

        if (--ref_count_ == 0)
            (void)ix::uninitNetSystem();
    }

private:
    static inline std::mutex mutex_;
    static inline std::size_t ref_count_ = 0;
};

// How long teardown will wait for a socket to come to rest before falling
// through to stop() anyway. Generous on purpose: ix gives a close handshake at
// most 300 ms (WebSocketTransport::kClosingMaximumWaitingDelayInMs), and a dial
// that was already in flight only has to finish or be cancelled.
inline constexpr std::chrono::milliseconds kWebSocketQuiesceDeadline{10000};
inline constexpr std::chrono::milliseconds kWebSocketQuiescePollInterval{1};

// How many consecutive Closed observations count as "the io thread is at rest".
// ix has no Connecting ready-state — a socket in the middle of a dial reads
// exactly like one that has finished — so a single Closed reading proves
// nothing. Each pass also re-arms ix's init-cancellation flag, which is what
// aborts a dial that is still running.
inline constexpr int kWebSocketQuiesceSettlePasses = 25;

// Bring a client websocket to rest before joining its io thread.
//
// ix::WebSocket::stop() closes the socket and only THEN raises its own _stop
// flag (IXWebSocket.cpp:187-200). With automatic reconnection live the io
// thread can re-dial inside that gap: the new socket comes up OPEN and, since
// nothing configures a ping interval, its poll() timeout is infinite
// (IXWebSocketTransport.cpp:340), so run() never reaches the _stop check and
// the join at the end of stop() never returns.
//
// Take the re-dial away first, then close until the socket really is Closed.
// close() on an OPEN socket starts the close handshake and wakes the poll;
// close() on an already-Closed one raises _requestInitCancellation, which
// cancels a handshake that is still running (IXWebSocketHandshake.cpp:98-101)
// and re-arms the select interrupt (IXWebSocketTransport.cpp:1199-1216). A
// CLOSING socket is left alone: ix drops it to Closed once its close-handshake
// window expires, and poking it again would cut the handshake short.
inline void quiesce_and_stop(ix::WebSocket& websocket)
{
    websocket.disableAutomaticReconnection();

    const auto deadline =
        std::chrono::steady_clock::now() + kWebSocketQuiesceDeadline;
    int closed_passes = 0;
    while (closed_passes < kWebSocketQuiesceSettlePasses)
    {
        const ix::ReadyState state = websocket.getReadyState();
        if (state == ix::ReadyState::Closing)
        {
            closed_passes = 0;
        }
        else
        {
            websocket.close();
            closed_passes = (state == ix::ReadyState::Closed) ? closed_passes + 1 : 0;
        }

        if (std::chrono::steady_clock::now() >= deadline)
            break;

        std::this_thread::sleep_for(kWebSocketQuiescePollInterval);
    }

    websocket.stop();
}

} // namespace og::sim::detail
