#pragma once

#include <openglad/interface/ui/picker_lobby_network_client.h>

#include <memory>
#include <string>
#include <vector>

class screen;

namespace og::runtime {
class GameSession;
}

namespace og::platform {

std::unique_ptr<og::ui::IPickerLobbyClient>
create_platform_host_picker_lobby_client(
    const og::ui::PickerHostGameOptions& options);

std::unique_ptr<og::ui::IPickerLobbyClient>
create_platform_join_picker_lobby_client(
    const og::ui::PickerJoinGameOptions& options);

std::vector<og::ui::PickerRelayRoomInfo> list_platform_relay_rooms(
    const std::string& base_url,
    const std::string& campaign_tag);

bool install_picker_lobby_gameplay_runtime(
    og::ui::IPickerLobbyClient* client,
    og::runtime::GameSession& session,
    screen& gameplay_screen);

} // namespace og::platform
