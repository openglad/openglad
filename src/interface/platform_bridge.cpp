#include <openglad/interface/platform_bridge.h>

#include <utility>

namespace {
PlatformBridge& bridge_storage()
{
    static PlatformBridge bridge{};
    return bridge;
}
} // namespace

void set_platform_bridge(PlatformBridge bridge)
{
    bridge_storage() = std::move(bridge);
}

const PlatformBridge& platform_bridge()
{
    return bridge_storage();
}
