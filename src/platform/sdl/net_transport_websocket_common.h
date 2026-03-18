#pragma once

#include <ixwebsocket/IXNetSystem.h>

#include <cstddef>
#include <mutex>
#include <stdexcept>

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

} // namespace og::sim::detail
