#include <openglad/resources/save_data.h>
#include <openglad/resources/io_common.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/legacy/base.h>
#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <gtest/gtest.h>

TEST(SaveDataUnit, save_data_update_guys_clamps_to_max_team_size)
{
    SaveData save;
    std::list<std::unique_ptr<walker>> oblist;

    constexpr int kExtraWalkers = 10;
    const int walker_count = MAX_TEAM_SIZE + kExtraWalkers;
    for (int i = 0; i < walker_count; ++i)
    {
        auto w = std::make_unique<walker>();
        w->set_dead(0);
        auto recruited = std::make_unique<guy>(FAMILY_SOLDIER);
        recruited->exp = static_cast<std::uint32_t>(100 + i);
        w->set_owned_myguy(std::move(recruited));
        oblist.push_back(std::move(w));
    }

    save.update_guys(oblist);

    ASSERT_TRUE(static_cast<int>(save.team_size) == MAX_TEAM_SIZE);
    ASSERT_TRUE(save.team_list.front() != nullptr);
    ASSERT_TRUE(save.team_list.back() != nullptr);
    ASSERT_TRUE(save.team_list[MAX_TEAM_SIZE - 1]->exp == static_cast<std::uint32_t>(100 + (MAX_TEAM_SIZE - 1)));
}

TEST(SaveDataUnit, merge_owned_guys_rejects_nonowners_and_invalid_roster_slots)
{
    const auto make_member = [](const char* name, std::uint32_t exp) {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = name;
        member->exp = exp;
        member->deployed = true;
        return member;
    };
    std::list<std::unique_ptr<walker>> empty_level;

    SaveData scalar_no_owner;
    scalar_no_owner.team_list[0] = make_member("UNCHANGED", 10);
    scalar_no_owner.team_size = 1;
    const guy* const scalar_member = scalar_no_owner.team_list[0].get();
    scalar_no_owner.merge_owned_guys_from(empty_level, guy::kNoOwner);
    ASSERT_EQ(1, static_cast<int>(scalar_no_owner.team_size));
    ASSERT_TRUE(scalar_no_owner.team_list[0] != nullptr);
    EXPECT_EQ(scalar_member, scalar_no_owner.team_list[0].get())
        << "the scalar no-owner overload is an exact no-op";
    EXPECT_EQ("UNCHANGED", scalar_no_owner.team_list[0]->name);
    EXPECT_EQ(10u, scalar_no_owner.team_list[0]->exp);
    EXPECT_TRUE(scalar_no_owner.team_list[0]->deployed);

    SaveData span_no_owner;
    span_no_owner.team_list[0] = make_member("SPAN", 20);
    span_no_owner.team_size = 1;
    const guy* const span_member = span_no_owner.team_list[0].get();
    const std::array<std::uint8_t, 2> no_owners = {
        guy::kNoOwner, guy::kNoOwner};
    span_no_owner.merge_owned_guys_from(empty_level, no_owners);
    ASSERT_EQ(1, static_cast<int>(span_no_owner.team_size));
    ASSERT_TRUE(span_no_owner.team_list[0] != nullptr);
    EXPECT_EQ(span_member, span_no_owner.team_list[0].get())
        << "an all-no-owner span is an exact no-op";
    EXPECT_EQ("SPAN", span_no_owner.team_list[0]->name);
    EXPECT_EQ(20u, span_no_owner.team_list[0]->exp);
    EXPECT_TRUE(span_no_owner.team_list[0]->deployed);

    SaveData invalid_slot;
    invalid_slot.team_list[0] = make_member("PRESERVED", 30);
    invalid_slot.team_size = 1;
    std::list<std::unique_ptr<walker>> level;
    auto walker_with_invalid_slot = std::make_unique<walker>();
    auto invalid_guy = make_member("INVALID", 999);
    invalid_guy->owner_player_index = 1;
    invalid_guy->owner_save_slot =
        static_cast<std::uint8_t>(MAX_TEAM_SIZE);
    walker_with_invalid_slot->set_owned_myguy(std::move(invalid_guy));
    walker_with_invalid_slot->set_dead(0);
    level.push_back(std::move(walker_with_invalid_slot));
    invalid_slot.merge_owned_guys_from(level, std::uint8_t{1});
    ASSERT_EQ(1, static_cast<int>(invalid_slot.team_size));
    ASSERT_TRUE(invalid_slot.team_list[0] != nullptr);
    EXPECT_EQ("PRESERVED", invalid_slot.team_list[0]->name)
        << "an invalid owner_save_slot cannot overwrite the private roster";
    EXPECT_EQ(30u, invalid_slot.team_list[0]->exp);
    EXPECT_TRUE(invalid_slot.team_list[0]->deployed);

    SaveData sparse;
    sparse.team_size = 1;
    sparse.merge_owned_guys_from(empty_level, std::uint8_t{1});
    EXPECT_EQ(0, static_cast<int>(sparse.team_size))
        << "a sparse legacy roster is re-densified without a null entry";
    EXPECT_TRUE(sparse.team_list[0] == nullptr);
}

// --- GTL v14 held-back preservation (docs/company-basecamp-design.md §3.3) ---

namespace {

std::unique_ptr<guy> make_roster_guy(const char* name,
                                     std::uint32_t exp,
                                     bool deployed)
{
    auto g = std::make_unique<guy>(FAMILY_SOLDIER);
    g->name = name;
    g->exp = exp;
    g->deployed = deployed;
    return g;
}

std::unique_ptr<walker> make_survivor_walker(const guy& source)
{
    auto w = std::make_unique<walker>();
    w->set_dead(0);
    w->set_owned_myguy(std::make_unique<guy>(source));
    return w;
}

} // namespace

TEST(SaveDataUnit, update_guys_preserves_held_back_entries)
{
    SaveData save;
    save.team_list[0] = make_roster_guy("HELD", 777, /*deployed=*/false);
    save.team_list[1] = make_roster_guy("SENT", 100, /*deployed=*/true);
    save.team_size = 2;

    // Only the deployed character entered the level; it survived and grew.
    std::list<std::unique_ptr<walker>> oblist;
    {
        guy sent(FAMILY_SOLDIER);
        sent.name = "SENT";
        sent.exp = 500;
        oblist.push_back(make_survivor_walker(sent));
    }

    save.update_guys(oblist);

    ASSERT_EQ(2, static_cast<int>(save.team_size))
        << "held-back entry must survive the roster rebuild";
    // Pass 1 places survivors first; pass 2 appends the held-back entries.
    ASSERT_TRUE(save.team_list[0] != nullptr);
    ASSERT_TRUE(save.team_list[1] != nullptr);
    ASSERT_EQ(std::string("SENT"), save.team_list[0]->name);
    ASSERT_EQ(500u, save.team_list[0]->exp) << "survivor exp preserved";
    ASSERT_TRUE(save.team_list[0]->deployed);
    ASSERT_EQ(std::string("HELD"), save.team_list[1]->name)
        << "held-back entry appended after survivors";
    ASSERT_EQ(777u, save.team_list[1]->exp)
        << "held-back guy object preserved untouched (no re-level)";
    ASSERT_FALSE(save.team_list[1]->deployed)
        << "held-back entry keeps deployed == false";
}

TEST(SaveDataUnit, update_guys_survives_held_back_with_empty_oblist)
{
    // Every deployed character died under permadeath: survivors = 0, but the
    // held-back characters were never at risk and must remain.
    SaveData save;
    save.keep_fallen_heroes = 0;
    save.team_list[0] = make_roster_guy("SENT", 100, /*deployed=*/true);
    save.team_list[1] = make_roster_guy("HELD", 200, /*deployed=*/false);
    save.team_size = 2;

    std::list<std::unique_ptr<walker>> oblist; // nobody came back

    save.update_guys(oblist);

    ASSERT_EQ(1, static_cast<int>(save.team_size));
    ASSERT_TRUE(save.team_list[0] != nullptr);
    ASSERT_EQ(std::string("HELD"), save.team_list[0]->name);
    ASSERT_FALSE(save.team_list[0]->deployed);
    ASSERT_TRUE(save.team_list[1] == nullptr) << "roster stays dense";
}

TEST(SaveDataUnit, update_guys_cap_drops_new_recruits_before_held_back)
{
    // 24-cap priority rule (§3.3, pinned): held-back characters always
    // survive; newly-acquired recruits are dropped first when the cap binds.
    SaveData save;
    constexpr int kHeldBack = 4;
    for (int i = 0; i < kHeldBack; ++i)
    {
        save.team_list[static_cast<std::size_t>(i)] = make_roster_guy("HELD", 9000u + static_cast<std::uint32_t>(i),
                                            /*deployed=*/false);
    }
    save.team_size = kHeldBack;

    // A full 24 survivors come out of the level (existing team + new
    // recruits); only 24 - 4 = 20 of them can be kept.
    std::list<std::unique_ptr<walker>> oblist;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
    {
        guy g(FAMILY_SOLDIER);
        g.name = "SURV";
        g.exp = 100u + static_cast<std::uint32_t>(i);
        oblist.push_back(make_survivor_walker(g));
    }

    save.update_guys(oblist);

    ASSERT_EQ(MAX_TEAM_SIZE, static_cast<int>(save.team_size));
    const int survivor_capacity = MAX_TEAM_SIZE - kHeldBack;
    for (int i = 0; i < survivor_capacity; ++i)
    {
        ASSERT_TRUE(save.team_list[static_cast<std::size_t>(i)] != nullptr);
        ASSERT_TRUE(save.team_list[static_cast<std::size_t>(i)]->deployed)
            << "slot " << i << " should hold a survivor";
        ASSERT_EQ(100u + static_cast<std::uint32_t>(i), save.team_list[static_cast<std::size_t>(i)]->exp)
            << "survivors kept in oblist order; the LAST (newly-acquired) "
               "recruits are the ones dropped";
    }
    for (int i = survivor_capacity; i < MAX_TEAM_SIZE; ++i)
    {
        ASSERT_TRUE(save.team_list[static_cast<std::size_t>(i)] != nullptr);
        ASSERT_FALSE(save.team_list[static_cast<std::size_t>(i)]->deployed)
            << "slot " << i << " should hold a preserved held-back entry";
        ASSERT_EQ(9000u + static_cast<std::uint32_t>(i - survivor_capacity),
                  save.team_list[static_cast<std::size_t>(i)]->exp);
    }
}

TEST(SaveDataUnit, reset_does_not_clear_last_played_timestamp)
{
    // §3.1: last_played_unix_s is NOT cleared by reset() (v10+ precedent:
    // reset() untouched) — it is company identity, not campaign state.
    SaveData save;
    save.last_played_unix_s = 1234567890;
    save.team_list[0] = make_roster_guy("ANY", 1, /*deployed=*/false);
    save.team_size = 1;

    save.reset();

    ASSERT_EQ(static_cast<std::int64_t>(1234567890), save.last_played_unix_s);
    ASSERT_EQ(0, static_cast<int>(save.team_size)) << "reset still clears roster";
}

TEST(SaveDataUnit, guy_copy_constructor_propagates_deployed)
{
    guy source(FAMILY_SOLDIER);
    source.deployed = false;
    guy copy(source);
    ASSERT_FALSE(copy.deployed)
        << "guy::guy(const guy&) = default must carry the deploy flag "
           "through update_guys/merge_owned_guys_from";
}

// --- GTL v16 campaign_tag carriage (docs/basecamp-zones-design.md,
// "Per-hero identity": copied wherever the record is copied) ---

TEST(SaveDataUnit, guy_copy_constructor_propagates_campaign_tag)
{
    guy source(FAMILY_SOLDIER);
    source.campaign_tag = 7;
    guy copy(source);
    ASSERT_EQ(7, static_cast<int>(copy.campaign_tag))
        << "guy::guy(const guy&) = default must carry campaign_tag "
           "through update_guys/merge_owned_guys_from and the "
           "headless-server save clone";
}

TEST(SaveDataUnit, update_guys_carries_campaign_tag_through_both_passes)
{
    SaveData save;
    save.team_list[0] = make_roster_guy("HELD", 777, /*deployed=*/false);
    save.team_list[0]->campaign_tag = 2;
    save.team_list[1] = make_roster_guy("SENT", 100, /*deployed=*/true);
    save.team_list[1]->campaign_tag = 1;
    save.team_size = 2;

    // The deployed character survived; its walker's myguy is a copy of the
    // roster entry, so it carries the tag into pass 1's rebuild copy.
    std::list<std::unique_ptr<walker>> oblist;
    {
        guy sent(FAMILY_SOLDIER);
        sent.name = "SENT";
        sent.exp = 500;
        sent.campaign_tag = 1;
        oblist.push_back(make_survivor_walker(sent));
    }

    save.update_guys(oblist);

    ASSERT_EQ(2, static_cast<int>(save.team_size));
    ASSERT_EQ(std::string("SENT"), save.team_list[0]->name);
    ASSERT_EQ(1, static_cast<int>(save.team_list[0]->campaign_tag))
        << "pass 1 (survivor copy) carries the tag";
    ASSERT_EQ(std::string("HELD"), save.team_list[1]->name);
    ASSERT_EQ(2, static_cast<int>(save.team_list[1]->campaign_tag))
        << "pass 2 (held-back move-append) carries the tag";
}

TEST(SaveDataUnit, merge_owned_guys_restores_the_disk_tag_for_every_slot)
{
    // Overlay rule: the DISK slot's tag always wins. A session roster is
    // rebuilt from LobbyCharacterData (mission entry) or GuySnapshot (a
    // joiner's mirror), neither of which carries the byte, so a survivor
    // reaches this merge with tag 0. Trusting it would un-assign the whole
    // deployed company on every won level while benched heroes kept theirs.
    SaveData save;
    save.team_list[0] = make_roster_guy("KEPT", 30, /*deployed=*/true);
    save.team_list[0]->campaign_tag = 5;
    save.team_list[1] = make_roster_guy("BROUGHT", 100, /*deployed=*/true);
    save.team_list[1]->campaign_tag = 1;
    save.team_list[2] = make_roster_guy("STAMPED", 100, /*deployed=*/true);
    save.team_list[2]->campaign_tag = 4;
    save.team_size = 3;

    std::list<std::unique_ptr<walker>> level;
    {
        // The realistic wire shape: the session guy lost the tag entirely.
        guy brought(FAMILY_SOLDIER);
        brought.name = "BROUGHT";
        brought.exp = 400;
        brought.campaign_tag = 0;
        brought.owner_player_index = 1;
        brought.owner_save_slot = 1;
        level.push_back(make_survivor_walker(brought));
    }
    {
        // A nonzero session tag loses to the disk record just the same —
        // nothing inside a level is allowed to author an assignment.
        guy stamped(FAMILY_SOLDIER);
        stamped.name = "STAMPED";
        stamped.exp = 400;
        stamped.campaign_tag = 2;
        stamped.owner_player_index = 1;
        stamped.owner_save_slot = 2;
        level.push_back(make_survivor_walker(stamped));
    }

    save.merge_owned_guys_from(level, std::uint8_t{1});

    ASSERT_EQ(3, static_cast<int>(save.team_size));
    ASSERT_EQ(std::string("KEPT"), save.team_list[0]->name);
    ASSERT_EQ(5, static_cast<int>(save.team_list[0]->campaign_tag))
        << "a kept (not brought) slot preserves the disk tag";
    ASSERT_EQ(std::string("BROUGHT"), save.team_list[1]->name);
    ASSERT_EQ(1, static_cast<int>(save.team_list[1]->campaign_tag))
        << "a survivor that came back tagless keeps its disk assignment";
    ASSERT_EQ(std::string("STAMPED"), save.team_list[2]->name);
    ASSERT_EQ(4, static_cast<int>(save.team_list[2]->campaign_tag))
        << "the disk tag wins even over a nonzero session tag";
}

// ---------------------------------------------------------------------------
// #207 replay excursion arm: transient, never serialized.

TEST(SaveDataUnit, replay_arm_remembers_origin_and_moves_the_cursor)
{
    SaveData save;
    save.reset();
    save.scen_num = 9;

    save.arm_replay(3);
    ASSERT_EQ(3, static_cast<int>(save.scen_num))
        << "arming moves the cursor onto the level";
    ASSERT_EQ(3, static_cast<int>(save.replay_level));
    ASSERT_EQ(9, static_cast<int>(save.replay_origin));
    ASSERT_TRUE(save.replay_armed_for(3));
    ASSERT_FALSE(save.replay_armed_for(9));

    // A re-arm before the excursion resolves keeps the FIRST origin —
    // scen_num already points into the excursion.
    save.arm_replay(5);
    ASSERT_EQ(5, static_cast<int>(save.replay_level));
    ASSERT_EQ(9, static_cast<int>(save.replay_origin))
        << "the origin is the campaign position, not the prior arm";

    save.clear_replay_arm();
    ASSERT_EQ(0, static_cast<int>(save.replay_level));
    ASSERT_EQ(0, static_cast<int>(save.replay_origin));
    ASSERT_FALSE(save.replay_armed_for(5));
    ASSERT_FALSE(save.replay_armed_for(0)) << "unarmed answers no level";
}

TEST(SaveDataUnit, replay_arm_is_cleared_by_reset_and_load)
{
    // reset() clears the pair (a reset company has no excursion in flight).
    SaveData save;
    save.reset();
    save.scen_num = 4;
    save.arm_replay(2);
    save.reset();
    ASSERT_EQ(0, static_cast<int>(save.replay_level));
    ASSERT_EQ(0, static_cast<int>(save.replay_origin));

    // load() clears it too, and the arm is never serialized: a save taken
    // while armed round-trips to an UNARMED file (no GTL change) — the
    // launch sites that need the arm across a disk round-trip re-carry it
    // explicitly. (SaveData::load mounts the file's campaign, so this leg
    // needs the default packages installed. restore_default_campaigns
    // rewrites the .glad files — NEVER call it over a live mount, it
    // desyncs PhysFS from the mount bookkeeping and every later title
    // lookup in the binary answers the raw id. Unmount first, restore the
    // caller's mount after.)
    const std::string mounted_before = get_mounted_campaign();
    if (!mounted_before.empty())
    {
        ASSERT_EQ(CampaignPackageIoError::None,
                  unmount_campaign_package_with_error(mounted_before));
    }
    restore_default_campaigns();
    save.reset();
    save.scen_num = 6;
    save.arm_replay(6);
    ASSERT_TRUE(save.save("replayarm_scratch"));
    SaveData loaded;
    loaded.arm_replay(1);  // a live arm on the destination must not survive
    ASSERT_TRUE(loaded.load("replayarm_scratch"));
    ASSERT_EQ(6, static_cast<int>(loaded.scen_num))
        << "the armed cursor position itself IS serialized";
    ASSERT_EQ(0, static_cast<int>(loaded.replay_level))
        << "the arm is transient: load() answers the file, never a session";
    ASSERT_EQ(0, static_cast<int>(loaded.replay_origin));
    (void)remove_user_file("save/replayarm_scratch.gtl");
    // Leave the mount state EXACTLY as found — including the nothing-mounted
    // case (a leaked mount desyncs against suites that re-init PhysFS, e.g.
    // CompanyScan, and every later title lookup answers the raw id).
    if (get_mounted_campaign() != mounted_before)
    {
        if (!get_mounted_campaign().empty())
        {
            ASSERT_EQ(CampaignPackageIoError::None,
                      unmount_campaign_package_with_error(
                          get_mounted_campaign()));
        }
        if (!mounted_before.empty())
        {
            ASSERT_EQ(CampaignPackageIoError::None,
                      mount_campaign_package_with_error(mounted_before));
        }
    }
}
