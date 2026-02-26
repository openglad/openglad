#pragma once

#include <functional>

namespace og::gameplay { class GameWorld; }
namespace og::render { class VideoBase; }

namespace og::interface {

struct PlatformBridge {
    // Rendering
    std::function<void()> present_frame;

    // Audio
    std::function<void(int sound_id)> play_sound;
    std::function<void(const char* music_file)> play_music;
    std::function<void()> stop_music;

    // Surface creation via abstract video interface (never SDL types).
    std::function<og::render::VideoBase*(int w, int h)> create_surface;

    // Legacy runtime wiring hook used while migrating old call sites.
    std::function<void(og::gameplay::GameWorld* world)> clear_stale_view_controls;
};

void install_platform_bridge(PlatformBridge bridge);
PlatformBridge& platform_bridge();
const PlatformBridge& platform_bridge_const();

} // namespace og::interface
