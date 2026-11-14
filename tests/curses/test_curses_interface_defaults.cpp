#include <gtest/gtest.h>

#include <openglad/gameplay/game_world.h>
#include <openglad/platform/curses/curses_game_runtime.h>
#include <openglad/platform/curses/curses_network.h>
#include <openglad/platform/curses/text_util.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
class MinimalSession final : public og::curses::CursesGameSession
{
public:
    GameWorld world{17};
    int inputs = 0;
    int frames = 0;
    int aborts = 0;
    void send_input(const InputState&) override { ++inputs; }
    void advance() override { ++frames; }
    GameWorld& mirror_world() override { return world; }
    std::uint32_t followed_entity_id() const override { return 29u; }
    std::uint32_t next_input_tick() const override { return 13u; }
    bool ended() const override { return false; }
    int ending() const override { return -1; }
    int next_level() const override { return 42; }
    void request_abort() override { ++aborts; }
    std::vector<std::string> drain_messages() override { return {}; }
};

class MinimalLobby final : public og::curses::CursesLobby
{
public:
    int starts = 0;
    int cancels = 0;
    bool poll(og::curses::ITerminal&, og::curses::IClock&) override { return false; }
    bool is_host() const override { return false; }
    void request_start() override { ++starts; }
    std::unique_ptr<og::curses::CursesGameSession> take_session() override { return {}; }
    std::vector<std::string> status_lines() const override { return {"waiting"}; }
    void cancel() override { ++cancels; }
    bool request_seat_team_change(std::uint8_t, short) override { return false; }
    bool request_seat_team_change(std::uint8_t, og::sim::LobbySeatId, short) override { return false; }
    std::vector<std::uint8_t> local_player_indices() const override { return {}; }
    bool set_ready(bool) override { return false; }
    bool local_ready() const override { return false; }
    std::vector<og::sim::LobbyPlayer> players() const override { return {}; }
};
}

TEST(CursesInterfaceDefaults, optional_session_controls_do_not_send_or_advance)
{
    MinimalSession session;
    // Positive controls establish that accidental delegation is observable.
    session.send_input(InputState{});
    session.advance();
    session.request_abort();
    ASSERT_EQ(1, session.inputs);
    ASSERT_EQ(1, session.frames);
    ASSERT_EQ(1, session.aborts);

    session.request_pause();
    session.send_pause_response();
    session.set_belt(og::sim::belt::Bytes{1, 2, 3, 4, 5, 6});
    EXPECT_EQ(1, session.inputs);
    EXPECT_EQ(1, session.frames);
    EXPECT_EQ(1, session.aborts);
    EXPECT_EQ(29u, session.driven_entity_id());
    EXPECT_EQ(13u, session.next_input_tick());
}

TEST(CursesInterfaceDefaults, absent_exit_prompt_ignores_both_answers)
{
    MinimalSession session;
    EXPECT_FALSE(session.exit_prompt_pending());
    EXPECT_EQ("", session.exit_prompt_text());
    session.respond_to_exit_prompt(true);
    session.respond_to_exit_prompt(false);
    EXPECT_EQ(0, session.aborts);
    EXPECT_EQ(0, session.inputs);
    EXPECT_EQ(0, session.frames);
    EXPECT_FALSE(session.ended());
    EXPECT_EQ(42, session.next_level());
    session.request_abort();
    EXPECT_EQ(1, session.aborts);
}

TEST(CursesInterfaceDefaults, lobby_without_optional_services_has_no_stage_or_kick)
{
    MinimalLobby lobby;
    EXPECT_FALSE(lobby.cancelled());
    EXPECT_FALSE(lobby.kick_machine(17u));
    EXPECT_EQ(std::nullopt, lobby.connection_alert());
    EXPECT_EQ(nullptr, lobby.staged_world());
    EXPECT_EQ(0u, lobby.stage_generation());
    EXPECT_EQ("", lobby.stage_failure_line());
    EXPECT_EQ((std::vector<std::string>{"waiting"}), lobby.status_lines());
    EXPECT_EQ(0, lobby.starts);
    EXPECT_EQ(0, lobby.cancels);
    lobby.cancel();
    EXPECT_EQ(1, lobby.cancels);
}

TEST(CursesTextUtil, multibyte_decode_and_invalid_continuation_preserve_consumption)
{
    struct Example { std::string_view bytes; char32_t codepoint; std::size_t consumed; };
    const std::array<Example, 8> examples{{
        {"", 0, 0}, {"A", U'A', 1}, {"\xc3\xa9", U'é', 2},
        {"\xe7\x95\x8c", U'界', 3}, {"\xf0\x9f\x98\x80", U'😀', 4},
        {"\xc3", 0xc3, 1}, {"\xe7" "A\x8c", 0xe7, 1},
        {"\xf0\x9f\x98", 0xf0, 1},
    }};
    for (const auto& example : examples) {
        char32_t decoded = U'?';
        EXPECT_EQ(example.consumed, og::curses::utf8_decode(example.bytes, decoded));
        EXPECT_EQ(example.codepoint, decoded);
    }
    const std::string_view malformed = "\xe7" "A\x8c";
    char32_t decoded = 0;
    const std::size_t consumed = og::curses::utf8_decode(malformed, decoded);
    ASSERT_EQ(1u, consumed);
    EXPECT_EQ(1u, og::curses::utf8_decode(malformed.substr(consumed), decoded));
    EXPECT_EQ(U'A', decoded);
}
