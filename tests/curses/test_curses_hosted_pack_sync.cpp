/* The host's class-pack announcement must describe the campaign it is
 * actually hosting.
 *
 * build_transferable_packs() walks the MOUNTED packs/ tree, and hosting
 * stages the lobby's campaign on its first poll — which remounts underneath
 * an announcement taken at construction time. A set snapshotted once
 * therefore advertises the previous campaign's packs, and joiners spend the
 * lobby downloading a pack for a campaign nobody is playing (and, before the
 * session-end seam, keep it mounted for the rest of the process).
 */
#include <gtest/gtest.h>

#include <openglad/platform/curses/curses_network.h>
#include <openglad/platform/curses/clock.h>
#include <openglad/platform/curses/headless_terminal.h>

#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "curses_mount_restore.h"

namespace og::curses {

// Test-only hooks exported from curses_network.cpp (see test_curses_network.cpp).
std::unique_ptr<CursesLobby> make_host_lobby_over_transport_for_testing(
    SaveData& save, int difficulty,
    std::shared_ptr<og::sim::ITransport> combined_transport,
    std::shared_ptr<og::sim::InProcessTransport> host_client_transport,
    std::uint32_t pinned_match_seed = 0);
std::vector<std::string> curses_network_testing_hosted_pack_ids(
    CursesLobby& lobby);

namespace {

bool contains(const std::vector<std::string>& ids, std::string_view id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

// The machine still has the PREVIOUS campaign mounted when the player opens
// a lobby for a different one — the ordinary shape, since the mount only
// converges when the lobby stages its campaign.
TEST(CursesHostedPacks, the_announcement_follows_the_staging_remount)
{
    MountRestore mount_restore;

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("westlands"));

    SaveData save;
    save.current_campaign = "modes";
    save.scen_num = 1;
    save.numplayers = 1;
    save.my_team = 0;
    save.allied_mode = 0;
    og::ui::initialize_starting_team(save, {FAMILY_SOLDIER});

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto lobby =
        make_host_lobby_over_transport_for_testing(save, 1, server, host_client);
    ASSERT_NE(nullptr, lobby);

    HeadlessTerminal term(24, 80);
    FakeClock clock;
    lobby->poll(term, clock);

    ASSERT_EQ("modes", get_mounted_campaign())
        << "staging converges the mount onto the lobby's campaign";

    const std::vector<std::string> ids =
        curses_network_testing_hosted_pack_ids(*lobby);
    EXPECT_TRUE(contains(ids, "modes.core"))
        << "the hosted campaign's own class pack must be on offer";
    EXPECT_FALSE(contains(ids, "westlands.fire"))
        << "a joiner must not be sent the pack of a campaign nobody is "
           "playing";
}

} // namespace og::curses
