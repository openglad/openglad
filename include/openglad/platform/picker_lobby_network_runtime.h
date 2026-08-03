#pragma once

#include <openglad/interface/ui/cloud_save_client.h>
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

std::unique_ptr<og::ui::IPickerRelayRoomListRequest>
begin_platform_list_relay_rooms(
    const std::string& base_url,
    const std::string& campaign_tag);

// Cloud saves (#155): blocking text HTTP against the relay's
// /api/save/<KEY> endpoints. Native: ix::HttpClient (10 s transfer
// timeout); web: the EM_ASYNC_JS fetch helpers (10 s Promise.race
// timeout). status 0 = transport failure with `error` filled.
og::ui::cloud::CloudHttpResult platform_cloud_http_get(
    const std::string& url);
og::ui::cloud::CloudHttpResult platform_cloud_http_post(
    const std::string& url,
    const std::string& json_body);

bool install_picker_lobby_gameplay_runtime(
    og::ui::IPickerLobbyClient* client,
    og::runtime::GameSession& session,
    screen& gameplay_screen);

} // namespace og::platform
