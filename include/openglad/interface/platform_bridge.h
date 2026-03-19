#pragma once

#include <functional>
#include <memory>

class video;

namespace og::ui {
class IPickerLobbyClient;
struct PickerHostGameOptions;
struct PickerJoinGameOptions;
}

// Cross-component callback bridge from interface -> platform.
//
// The interface layer calls these callbacks for platform-owned operations.
// Platform installs SDL (or headless no-op) implementations at startup.
struct PlatformBridge {
    // Rendering
    std::function<void()> present_frame;

    // Audio
    std::function<void(int sound_id)> play_sound;
    std::function<void(const char* music_file)> play_music;
    std::function<void()> stop_music;

    // Render surface creation (abstract video base, never SDL_Surface).
    std::function<video*(int w, int h)> create_surface;

    // Network lobby factories.
    std::function<std::unique_ptr<og::ui::IPickerLobbyClient>(
        const og::ui::PickerHostGameOptions&)> create_host_picker_lobby_client;
    std::function<std::unique_ptr<og::ui::IPickerLobbyClient>(
        const og::ui::PickerJoinGameOptions&)> create_join_picker_lobby_client;
};

void set_platform_bridge(PlatformBridge bridge);
const PlatformBridge& platform_bridge();
