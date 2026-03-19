#pragma once

#include <memory>

class screen;

namespace og::ui {
class IPickerLobbyClient;
struct PickerHostGameOptions;
struct PickerJoinGameOptions;
}

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

bool install_picker_lobby_gameplay_runtime(
    og::ui::IPickerLobbyClient* client,
    og::runtime::GameSession& session,
    screen& gameplay_screen);

} // namespace og::platform
