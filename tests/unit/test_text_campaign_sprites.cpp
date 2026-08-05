/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include "../campaign_sprite_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/interface/ui/text_protocol.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>

#include <iostream>
#include <sstream>
#include <string>

// Issue #162, the headless half. openglad_text realizes the headless loader
// when LevelRuntimeData is constructed — BEFORE the --campaign mount — so a
// campaign shipping its own entity art or pack-family sprites used to run
// the whole session on the default campaign's sprite set (and, because
// sizex/sizey feed collision, on the wrong sim geometry). The protocol
// session must self-heal: after it runs, the headless loader's sprites for
// both a campaign-shipped core name and an embedded pack family must be the
// campaign's bytes.

namespace {

constexpr const char* kFixtureId = "org.test.sprite162.text";

class StdinRedirect {
public:
    explicit StdinRedirect(const std::string& input)
        : input_(input)
        , old_(std::cin.rdbuf(input_.rdbuf()))
    {
    }

    ~StdinRedirect() { std::cin.rdbuf(old_); }

private:
    std::istringstream input_;
    std::streambuf* old_;
};

class CoutRedirect {
public:
    CoutRedirect()
        : old_(std::cout.rdbuf(output_.rdbuf()))
    {
    }

    ~CoutRedirect() { std::cout.rdbuf(old_); }

    std::string str() const { return output_.str(); }

private:
    std::ostringstream output_;
    std::streambuf* old_;
};

// Order-independence: put the default campaign (and a matching loader) back
// no matter how the test exits.
struct RestoreDefaultState {
    ~RestoreDefaultState()
    {
        (void)mount_campaign_package_with_error("gladiator");
        headless_entity_loader()->reload_graphics_if_stale();
        og::test162::remove_sprite_campaign(kFixtureId);
        cleanup_unpacked_campaign();
    }
};

}  // namespace

TEST(TextCampaignSprites, protocol_session_loads_campaign_pack_sprites)
{
    restore_default_campaigns();
    RestoreDefaultState restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    ASSERT_TRUE(og::test162::install_playable_sprite_campaign(kFixtureId));
    // Back on the default campaign: the SESSION must switch the mount and
    // heal the loader, exactly as a real `openglad_text --campaign` run.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    headless_entity_loader()->reload_graphics_if_stale();

    {
        StdinRedirect input("census\nquit\n");
        CoutRedirect output;

        og::ui::TextProtocolArgs args;
        args.campaign = kFixtureId;
        args.level = 1;
        args.team_families = {FAMILY_SOLDIER};
        args.seed = 7;
        ASSERT_EQ(0, og::ui::run_text_protocol_session(args));

        const std::string text = output.str();
        EXPECT_NE(std::string::npos, text.find("\"status\":\"ready\""));
        EXPECT_NE(std::string::npos,
                  text.find("\"cmd\":\"quit\",\"status\":\"ok\""));
    }

    // The fixture campaign is still mounted; the session's reload must have
    // brought the loader with it.
    EXPECT_EQ(kFixtureId, get_mounted_campaign());

    const int family = og::families::resolve_family_string_id(
        Order::Living, og::test162::kBallFamilyId);
    ASSERT_GE(family, NUM_FAMILIES)
        << "the campaign's embedded pack family must be installed";

    const PixieData* ball =
        headless_entity_loader()->graphics_for(Order::Living, family);
    ASSERT_NE(ball, nullptr)
        << "the pack family's sprite must be loaded (pre-fix this was null "
           "and create_walker_owned degraded the family to a soldier)";
    EXPECT_EQ(10, static_cast<int>(ball->w));
    EXPECT_EQ(10, static_cast<int>(ball->h));
    EXPECT_EQ(24, static_cast<int>(ball->frames))
        << "sprite dimensions must come from the pack's own sprite+sidecar "
           "(they feed sizex/sizey and with them collision geometry)";

    const PixieData* soldier =
        headless_entity_loader()->graphics_for(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(soldier, nullptr);
    EXPECT_EQ(24, static_cast<int>(soldier->frames))
        << "the campaign's shipped footman art (elf donor, 24 frames) must "
           "shadow the stock sprite for the session";
}
