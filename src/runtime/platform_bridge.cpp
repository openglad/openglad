#include <openglad/interface/platform_bridge.h>
#include <atomic>
#include <mutex>
#include <utility>

namespace og::interface {

namespace {
PlatformBridge& platform_bridge_instance()
{
    static PlatformBridge bridge{};
    return bridge;
}

std::once_flag g_platform_bridge_install_once;
std::atomic<const PlatformBridge*> g_platform_bridge{&platform_bridge_instance()};
}

void install_platform_bridge(PlatformBridge bridge)
{
    std::call_once(g_platform_bridge_install_once, [installed = std::move(bridge)]() {
        platform_bridge_instance() = std::move(installed);
        g_platform_bridge.store(&platform_bridge_instance(), std::memory_order_release);
    });
}

PlatformBridge& platform_bridge()
{
    return const_cast<PlatformBridge&>(
        *g_platform_bridge.load(std::memory_order_acquire));
}

const PlatformBridge& platform_bridge_const()
{
    return *g_platform_bridge.load(std::memory_order_acquire);
}

} // namespace og::interface
