#include <openglad/interface/platform_bridge.h>
#include <utility>

namespace og::interface {

namespace {
PlatformBridge g_platform_bridge{};
}

void install_platform_bridge(PlatformBridge bridge)
{
    g_platform_bridge = std::move(bridge);
}

PlatformBridge& platform_bridge()
{
    return g_platform_bridge;
}

const PlatformBridge& platform_bridge_const()
{
    return g_platform_bridge;
}

} // namespace og::interface
