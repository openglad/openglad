#pragma once

// Setup and teardown support for the websocket transport stress tests.
//
// A transport whose io thread has wedged must never be destroyed on the test's
// own thread: the hang becomes the test result, and a hang is a poor red — it
// costs the whole binary and names nothing. Run the teardown on a worker
// instead, give it a generous ceiling, and on timeout detach the worker and
// let it keep everything it owns. The leak is deliberate: the wedged io thread
// is still reading that memory.

#include <openglad/platform/net_transport_websocket_server.h>

#include <ixwebsocket/IXGetFreePort.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace og::test {

struct ListeningWebSocketServer
{
    int port = 0;
    std::unique_ptr<sim::WebSocketServerTransport> transport;
};

// OpenGlad's IXWebSocket safety patch makes getFreePort() bind port 0 in every
// build, including ASan, then close the probe before the real server binds.
// The probe still cannot reserve the candidate across that handoff.
// The two 200-cycle teardown stress tests therefore share this bounded
// fixture-setup loop. A collision advances to a new candidate, but once the
// listener exists every transport action and assertion remains single-shot.
inline ListeningWebSocketServer listen_websocket_server_on_available_port(
    const sim::WebSocketServerTransport::Options& options)
{
    constexpr int kListenCandidates = 16;
    for (int candidate = 0; candidate < kListenCandidates; ++candidate)
    {
        const int port = ix::getFreePort();
        auto transport =
            std::make_unique<sim::WebSocketServerTransport>(port, options);
        try
        {
            transport->accept_connections();
            return {.port = port, .transport = std::move(transport)};
        }
        catch (const std::runtime_error&)
        {
            if (candidate + 1 == kListenCandidates)
                throw;
        }
    }

    throw std::logic_error("websocket listen candidate loop fell through");
}

// Runs `action` on a worker thread. Returns true once it has finished, false
// if it is still running after `ceiling` — in which case the worker is
// detached and whatever the action owns stays alive for the rest of the run.
template <typename Action>
bool finishes_within(std::chrono::milliseconds ceiling, Action&& action)
{
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> finished = done->get_future();
    std::thread worker(
        [done, action = std::forward<Action>(action)]() mutable {
            action();
            done->set_value();
        });

    if (finished.wait_for(ceiling) == std::future_status::ready)
    {
        worker.join();
        return true;
    }

    worker.detach();
    return false;
}

} // namespace og::test
