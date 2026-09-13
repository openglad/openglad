#pragma once

// Teardown ceilings for the websocket transport tests.
//
// A transport whose io thread has wedged must never be destroyed on the test's
// own thread: the hang becomes the test result, and a hang is a poor red — it
// costs the whole binary and names nothing. Run the teardown on a worker
// instead, give it a generous ceiling, and on timeout detach the worker and
// let it keep everything it owns. The leak is deliberate: the wedged io thread
// is still reading that memory.

#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <utility>

namespace og::test {

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
