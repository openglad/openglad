#pragma once

// The kicked joiner the picker tests share: a networked, non-host lobby
// client whose was_kicked() / session_lost() follow two flags the test
// raises. Hoisted out of tests/integration/test_networking_menu.cpp when the
// TRAIN, DETAILS and Base Camp kick flows (test_abilities_flow.cpp) needed
// the same fake: one fake, not two that drift.
//
// Give it to the picker through an OWNING slot (the join factory bridge, or
// picker_testing_set_lobby_client_owner) so the per-frame kicked check may
// swap it for a local client.

#include <openglad/interface/ui/picker_lobby_client.h>

#include <atomic>
#include <optional>
#include <string>

// A joiner client the picker OWNS (installed through the join factory
// bridge), so the per-frame kicked check may swap it: was_kicked() flips
// when the test raises the shared flag — the picker glue under test is
// picker_revert_lobby_client_if_kicked, not the wire (wave 1 pinned that).
class FakeKickedJoinClient final : public og::ui::IPickerLobbyClient
{
public:
    // `lost_flag` (#278) models the session_lost() latch of a joiner whose
    // link died; nullptr = never lost.
    explicit FakeKickedJoinClient(std::atomic<bool>* kicked_flag,
                                  std::atomic<bool>* lost_flag = nullptr)
        : kicked_flag_(kicked_flag), lost_flag_(lost_flag)
    {
    }
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override { return std::nullopt; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override { return std::nullopt; }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }
    [[nodiscard]] bool was_kicked() const noexcept override
    {
        return kicked_flag_->load();
    }
    [[nodiscard]] bool session_lost() const noexcept override
    {
        return lost_flag_ != nullptr && lost_flag_->load();
    }
    [[nodiscard]] std::optional<std::string> connection_alert() const override
    {
        if (kicked_flag_->load())
            return "KICKED BY HOST";
        return std::nullopt;
    }

private:
    std::atomic<bool>* kicked_flag_;
    std::atomic<bool>* lost_flag_;
};
