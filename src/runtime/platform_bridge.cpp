#include <openglad/interface/platform_bridge.h>
#include <atomic>
#include <mutex>
#include <utility>

namespace og::interface {

namespace {
const PlatformBridge& default_platform_bridge()
{
    static const PlatformBridge bridge{};
    return bridge;
}

std::once_flag g_platform_bridge_install_once;
std::atomic<const PlatformBridge*> g_platform_bridge{&default_platform_bridge()};
}

void install_platform_bridge(PlatformBridge bridge)
{
    std::call_once(g_platform_bridge_install_once, [installed = std::move(bridge)]() {
        auto* stable_bridge = new PlatformBridge(std::move(installed));
        g_platform_bridge.store(stable_bridge, std::memory_order_release);
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
