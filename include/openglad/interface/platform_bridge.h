#pragma once

#include <openglad/interface/ui/cloud_save_client.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class video;

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
    std::function<std::vector<og::ui::PickerRelayRoomInfo>(
        const std::string& base_url,
        const std::string& campaign_tag)> list_relay_rooms;
    std::function<std::unique_ptr<og::ui::IPickerRelayRoomListRequest>(
        const std::string& base_url,
        const std::string& campaign_tag)> begin_list_relay_rooms;

    // Cloud saves (#155): generic blocking text-HTTP callbacks, so the
    // parsers stay in the pure layer (cloud_save_client.cpp). Left empty on
    // clients without HTTP (the headless text bridge) — the flows then
    // degrade with "Cloud sync is not available in this client."
    std::function<og::ui::cloud::CloudHttpResult(const std::string& url)>
        cloud_http_get;
    std::function<og::ui::cloud::CloudHttpResult(
        const std::string& url,
        const std::string& json_body)> cloud_http_post;
};

void set_platform_bridge(PlatformBridge bridge);
const PlatformBridge& platform_bridge();
