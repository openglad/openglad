#include <openglad/interface/platform_bridge.h>
#include <utility>

namespace og::interface {

namespace {
PlatformBridge& platform_bridge_instance()
{
    static PlatformBridge bridge{};
    return bridge;
}
}

void install_platform_bridge(PlatformBridge bridge)
{
    platform_bridge_instance() = std::move(bridge);
}

PlatformBridge& platform_bridge()
{
    return platform_bridge_instance();
}

const PlatformBridge& platform_bridge_const()
{
    return platform_bridge_instance();
}

} // namespace og::interface
