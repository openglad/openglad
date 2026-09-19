#pragma once

// The camp flows' one starting save: a two-soldier company, deployed, on a
// named campaign and cursor, with every match knob at a DEFINED resting
// state.
//
// It lived in tests/integration/test_campaign_zone_ui.cpp until the SETUP
// wizard's flows wanted the same company in the same binary and copied it.
// A fixture twin drifts the moment one copy gains a field — the resting
// state below is the whole point of this fixture, and a copy that forgot
// one of its lines would fail only in binary order, under --gtest_shuffle,
// on somebody else's flow. One implementation.

#include <gtest/gtest.h>

#include <openglad/gameplay/guy.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/save_data.h>

#include <memory>
#include <string>
#include <vector>

// `company` empty leaves save_name alone (the flows that pin a company name
// pass their own).
inline void write_save0_with_two_soldiers(
    const std::string& campaign, short scen_num,
    const std::vector<int>& completed = {},
    const std::string& company = std::string())
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    const char* names[] = {"Alpha", "Beta"};
    for (std::size_t i = 0; i < 2; ++i)
    {
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[i]->name = names[i];
        save.team_list[i]->teamnum = 0;
        save.team_list[i]->deployed = true;
        save.team_list[i]->campaign_tag = 0;
    }
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;
    // A defined resting state includes the match knobs: in binary order an
    // earlier flow's fill/map_units would otherwise leak into this save and
    // the amendment-5 macro faces (derived from fill[]) would not be at
    // rest (caught by the ordered og_test_matchup run, invisible alone).
    // The RULES step's two wheels are in that set now: the wizard's SCORE
    // and TIME LIMIT rows read them, and a leaked value moves their face.
    save.fill = {};
    save.map_units = {};
    save.ctf_capture_limit = 0;
    save.time_limit = 0;
    // ...and the arena deal memo (amendment 7): a memo left by an earlier
    // flow on the same cursor would mark the fresh bands as already dealt.
    save.arena_lineup_dealt_campaign.clear();
    save.arena_lineup_dealt_scen = 0;
    save.scen_num = scen_num;
    save.current_campaign = campaign;
    save.current_levels.clear();
    save.current_levels[campaign] = scen_num;
    save.m_totalcash[0] = 5000;
    save.campaign_state.clear();
    save.completed_levels.clear();
    for (int level : completed)
        save.add_level_completed(campaign, level);
    if (!company.empty())
        save.save_name = company;
    ASSERT_TRUE(save.save("save0"));
}
