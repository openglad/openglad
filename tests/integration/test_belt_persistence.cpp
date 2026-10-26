/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Ability Select: the belt survives every rebuild of a guy that a menu
// frame or a won level runs. The belt is authored at Base Camp and lives on
// the guy (guy::belt); every place the roster is rebuilt from a wire struct
// copies it, so none of them may quietly put a hero back on the class default.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/save_data.h>

#include <array>
#include <list>
#include <memory>

namespace
{

// Three bytes of every kind: an explicit entry, an alternate, EMPTY, DEFAULT.
constexpr og::sim::belt::Bytes kWarriorBelt{0x03, 0x09, 0xFF, 0x00, 0x0A, 0x05};
constexpr og::sim::belt::Bytes kMageBelt{0x05, 0x00, 0x00, 0xFF, 0x01, 0x0C};

// Swaps the session save's roster out for the test and back on exit.
class RosterSwap
{
public:
    explicit RosterSwap(SaveData& save)
        : save_(save), size_(save.team_size), my_team_(save.my_team),
          numplayers_(save.numplayers), allied_(save.allied_mode)
    {
        for (std::size_t i = 0; i < saved_.size(); ++i)
            saved_[i] = std::move(save_.team_list[i]);
    }
    ~RosterSwap()
    {
        for (std::size_t i = 0; i < saved_.size(); ++i)
            save_.team_list[i] = std::move(saved_[i]);
        save_.team_size = size_;
        save_.my_team = my_team_;
        save_.numplayers = numplayers_;
        save_.allied_mode = allied_;
    }
    RosterSwap(const RosterSwap&) = delete;
    RosterSwap& operator=(const RosterSwap&) = delete;

private:
    SaveData& save_;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_;
    unsigned char size_;
    short my_team_;
    unsigned char numplayers_;
    short allied_;
};

} // namespace

// The local lobby echoes the machine's own roster back into the save on every
// menu frame (picker_lobby_poll -> apply_state_to_save): ten frames in a row,
// deployed and benched, the belt must come back as it went out.
TEST(BeltPersistence, apply_state_to_save_keeps_the_belt_across_ten_polls)
{
    picker_lobby_shutdown();
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    RosterSwap swap(save);

    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "Warrior";
    save.team_list[0]->teamnum = 0;
    save.team_list[0]->deployed = true;
    save.team_list[0]->belt = kWarriorBelt;
    save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[1]->name = "Benched";
    save.team_list[1]->teamnum = 0;
    save.team_list[1]->deployed = false;
    save.team_list[1]->belt = kMageBelt;
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 1;

    // The Base Camp roster-mutation tail (an ABILITIES edit ends here).
    picker_lobby_sync_roster_from_save();
    for (int poll = 0; poll < 10; ++poll)
    {
        picker_lobby_poll();
        ASSERT_TRUE(save.team_list[0] != nullptr);
        ASSERT_TRUE(save.team_list[1] != nullptr);
        ASSERT_EQ(kWarriorBelt, save.team_list[0]->belt)
            << "the deployed hero's belt was lost on poll " << poll;
        ASSERT_EQ(kMageBelt, save.team_list[1]->belt)
            << "the benched hero's belt was lost on poll " << poll;
    }

    // A later edit is what the next frame keeps.
    save.team_list[0]->belt[0] = 0x01;
    picker_lobby_sync_roster_from_save();
    picker_lobby_poll();
    EXPECT_EQ(0x01, save.team_list[0]->belt[0]);

    picker_lobby_shutdown();
}

// Base Camp's joiner rows build display guys from the lobby characters; the
// TRAIN screen's ABILITIES door reads the belt off them.
TEST(BeltPersistence, make_base_camp_display_guy_copies_the_belt)
{
    og::sim::LobbyCharacterData character;
    character.family = FAMILY_MAGE;
    character.name = "Remote";
    character.level = 7;
    character.belt = kMageBelt;
    const std::unique_ptr<guy> shown = og::ui::make_base_camp_display_guy(character);
    ASSERT_TRUE(shown != nullptr);
    EXPECT_EQ(kMageBelt, shown->belt);
    EXPECT_EQ(7, shown->level);
}

// A won networked level merges this machine's survivors back into its save
// (merge_owned_guys_from). The survivor is a copy of the session guy, so the
// session's belt, which the lobby carried in, is what lands on disk.
TEST(BeltPersistence, merge_owned_guys_keeps_the_session_belt)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    scr->save_data.reset();
    scr->save_data.numplayers = 1;
    scr->save_data.current_campaign = "gladiator";
    {
        auto hero = std::make_unique<guy>(FAMILY_SOLDIER);
        hero->name = "Survivor";
        hero->id = 1;
        hero->belt = kWarriorBelt;
        scr->save_data.team_list[0] = std::move(hero);
        scr->save_data.team_size = 1;
    }
    ASSERT_TRUE(scr->save_data.save("save0"));

    std::list<std::unique_ptr<walker>> oblist;
    guy session_guy(FAMILY_SOLDIER);
    session_guy.id = 1;
    session_guy.name = "Survivor";
    session_guy.belt = kMageBelt;  // different from disk: the session wins
    std::unique_ptr<walker> body = guy_create_walker_owned(session_guy, scr);
    ASSERT_TRUE(body != nullptr);
    body->set_dead(0);
    body->myguy->owner_player_index = 0;
    body->myguy->owner_save_slot = 0;
    body->myguy->exp = 4000;
    oblist.push_back(std::move(body));

    SaveData merged;
    ASSERT_EQ(SaveDataIoError::None, merged.load_with_error("save0"));
    ASSERT_EQ(kWarriorBelt, merged.team_list[0]->belt)
        << "the disk record must hold its own belt before the merge";
    merged.merge_owned_guys_from(oblist, /*owner_player_index=*/0);
    ASSERT_TRUE(merged.team_list[0] != nullptr);
    EXPECT_EQ(4000u, merged.team_list[0]->exp) << "the merge ran";
    EXPECT_EQ(kMageBelt, merged.team_list[0]->belt)
        << "a won level keeps the belt the hero played with";

    ASSERT_TRUE(merged.save("save0"));
    SaveData reread;
    ASSERT_EQ(SaveDataIoError::None, reread.load_with_error("save0"));
    EXPECT_EQ(kMageBelt, reread.team_list[0]->belt) << "and it reaches the disk";
    scr->save_data.reset();
}
