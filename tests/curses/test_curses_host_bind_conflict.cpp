/* The terminal client's half of the host bind-conflict contract.
 *
 * A WebSocketServerTransport binds in accept_connections(), never in its
 * constructor, so a try/catch that wraps only the construction cannot see a
 * port that is already in use. The terminal host is supposed to report such a
 * failure through its `*error` out-parameter ("Direct: ..."), which
 * make_host_lobby turns into the "Networking unavailable" panel; an escaping
 * exception instead unwinds past make_host_lobby into curses_app / the picker
 * client, which have no handler for it.
 */
#include <gtest/gtest.h>

#include <openglad/platform/curses/curses_network.h>

#include <openglad/gameplay/net_transport.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/platform/net_transport_websocket_server.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <ixwebsocket/IXGetFreePort.h>

#include <memory>
#include <string>

#include "curses_mount_restore.h"

namespace og::curses {
namespace {

void init_host_save(SaveData& save)
{
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.numplayers = 1;
    save.my_team = 0;
    save.allied_mode = 0;
    og::ui::initialize_starting_team(save, {FAMILY_SOLDIER});
}

} // namespace

// Somebody already owns the port the terminal host was asked for (a second
// copy of the game, a stale host). With no relay configured there is nothing
// left to host over, so the answer is a refusal the caller can render — not
// an exception thrown through two call frames that never catch one.
TEST(CursesHostLobby, a_busy_direct_port_is_reported_not_thrown)
{
    // build_transferable_packs() reads the mounted tree; keep the process
    // mount exactly as this test found it.
    MountRestore mount_restore;

    const int busy_port = ix::getFreePort();
    og::sim::WebSocketServerTransport blocker(busy_port);
    blocker.accept_connections();

    SaveData save;
    init_host_save(save);
    HostOptions options;
    options.port = busy_port;   // no relay_url: direct is the only transport

    std::string error;
    std::unique_ptr<CursesLobby> lobby;
    ASSERT_NO_THROW(lobby = make_host_lobby(save, options, &error))
        << "a busy port must come back as a refusal, not an exception";
    EXPECT_EQ(nullptr, lobby)
        << "there is no transport left to host over";
    EXPECT_NE(std::string::npos, error.find("Direct: "))
        << "the refusal names the direct listener that failed; error was \""
        << error << "\"";
}

} // namespace og::curses
