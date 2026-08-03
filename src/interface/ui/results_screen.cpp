/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/interface/ui/results_screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/sound.h>
#include <openglad/interface/render/text.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/game_mode.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <span>

// For deterministic helper-only test coverage.
#ifdef TESTING
#include <openglad/interface/ui/results_screen.h>
#include <atomic>
namespace {
bool s_force_full_results_ui = false;
// Injector-thread synchronization for the full results UI (no wall-clock
// pacing in tests): `live` flips true while the modal button loop runs,
// `frames` counts loop iterations so an injector can hold a synthetic mouse
// press across a known number of polled frames.
std::atomic<bool> s_results_ui_loop_live{false};
std::atomic<int> s_results_ui_frames{0};
}
#endif


bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);

bool prompt_for_string(const std::string& message, std::string& result);
void popup_dialog(const char* title, const char* message);

inline constexpr Sint32 OG_OK = 4;
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);

namespace
{
struct UiRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Tier-B mode popup dispatch (tower-triple §2.7): the mounted mode may
// replace the generic ending popup (tower: FLOOR CLEARED / THE TOWER CLAIMS
// YOU / THE CLIMB ABANDONED). Classic returns nullopt. The outcome mirrors
// screen::endgame's construction — by the time this runs, on_run_ended has
// already reset a tower cursor, so the popup derives the floor from
// world.id, never from scen_num. Returns true when it showed a popup.
bool show_mode_ending_popup(int ending, int nextlevel)
{
    og::mode::LevelOutcome outcome;
    outcome.ending = static_cast<short>(ending);
    outcome.next_level = static_cast<short>(nextlevel);
    outcome.networked = og::runtime::current_session != nullptr &&
                        og::runtime::current_session->networked_session_;
    outcome.withdrawn = (ending == 1 && nextlevel != -1);
    screen* const s = og::runtime::current_session->myscreen_;
    const std::optional<og::mode::ModePopup> popup =
        og::mode::current_progression().ending_popup(s->save_data, s->world(),
                                                     outcome);
    if (!popup)
        return false;
    popup_dialog(popup->title.c_str(), popup->body.c_str());
    return true;
}

// CTF match end carries the classic WIN shape (ending == 0) even when the
// local player LOST, so the popup must be derived from the match outcome,
// never from ending alone. Returns true when it showed the CTF popup.
//
// Title: VICTORY!/DEFEAT! from the local viewports' controls vs the winner —
// NEVER ctf.winner_is_player, which is global (any human on the winning team)
// and would tell a losing networked client "VICTORY". Null/stale controls or
// split-screen panes spanning both sides fall back to a neutral MATCH OVER.
// Flow line: chosen from the end shape itself, evaluated BEFORE screen::
// endgame rewrites scen_num — the loss/rematch shape keeps nextlevel == the
// current level.
bool show_ctf_ending_popup(int nextlevel)
{
    screen* const s = og::runtime::current_session->myscreen_;
    const GameWorld& world = s->world();
    if (!(world.type & GameWorld::TYPE_CTF) || !world.ctf.active ||
        world.ctf.winner_team < 0)
    {
        return false;
    }

    const int winner = world.ctf.winner_team;
    bool any_winner = false;
    bool any_loser = false;
    for (int i = 0; i < s->numviews; ++i)
    {
        viewscreen* const view = s->viewob[i].get();
        if (view == nullptr || view->control == nullptr)
            continue;
        if (view->control->team_num() == static_cast<unsigned char>(winner))
            any_winner = true;
        else
            any_loser = true;
    }

    const char* title = "MATCH OVER";
    if (any_winner && !any_loser)
        title = "VICTORY!";
    else if (any_loser && !any_winner)
        title = "DEFEAT!";

    std::string body =
        std::format("{} TEAM WINS!", og::sim::ctf_team_color_name(winner));
    if (nextlevel == s->save_data.scen_num)
        body += "\nGet ready for a rematch!";
    else
    {
        // The scenario name sits on its own line: popup_dialog sizes itself
        // to the longest line, and "Moving on to <NNN. TITLE>" can run the
        // full 320px screen width.
        body += std::format(
            "\nMoving on to\n{}", og::data::scenario_display_name(nextlevel));
    }
    popup_dialog(title, body.c_str());
    return true;
}

// Scripted-mode (TYPE_SCRIPTED) match end: the CTF popup's logic read from
// ModeState. Gated on a DECIDED match (winner_team >= 0, og.declare_winner);
// an og.end_level without a winner keeps the generic ending popups. Title
// from the LOCAL viewports' controls vs the winner — never winner_is_player,
// which is global (any human on the winning team) and would tell a losing
// networked client "VICTORY". The body leads with the mode's own name.
bool show_scripted_mode_ending_popup(int nextlevel)
{
    screen* const s = og::runtime::current_session->myscreen_;
    const GameWorld& world = s->world();
    if (!(world.type & GameWorld::TYPE_SCRIPTED) || !world.mode.active ||
        world.mode.winner_team < 0)
    {
        return false;
    }

    const int winner = world.mode.winner_team;
    bool any_winner = false;
    bool any_loser = false;
    for (int i = 0; i < s->numviews; ++i)
    {
        viewscreen* const view = s->viewob[i].get();
        if (view == nullptr || view->control == nullptr)
            continue;
        if (view->control->team_num() == static_cast<unsigned char>(winner))
            any_winner = true;
        else
            any_loser = true;
    }

    const char* title = "MATCH OVER";
    if (any_winner && !any_loser)
        title = "VICTORY!";
    else if (any_loser && !any_winner)
        title = "DEFEAT!";

    std::string body;
    if (world.mode.name[0] != '\0')
        body = std::format("{}: ", world.mode.name.data());
    body += std::format("{} TEAM WINS!", og::sim::team_color_name(winner));
    if (nextlevel == s->save_data.scen_num)
        body += "\nGet ready for a rematch!";
    else
    {
        body += std::format(
            "\nMoving on to\n{}", og::data::scenario_display_name(nextlevel));
    }
    popup_dialog(title, body.c_str());
    return true;
}

}


void show_ending_popup(int ending, int nextlevel)
{
	if (ending == 1)  // 1 = lose, for some reason
	{
		if (show_mode_ending_popup(ending, nextlevel))
			return; // mode-owned run-end popup shown (tower loss/abandon)
		if (nextlevel == -1) // generic defeat
		{
		    popup_dialog("Defeat!", "YOUR MEN ARE CRUSHED!");
		}
		else // we're withdrawing to another level
		{
		    std::string buf = std::format("Retreating to\n{}\n(You may take this field later)", og::data::scenario_display_name(nextlevel));
		    popup_dialog("Retreat!", buf.c_str());
		}

	}
	else if (ending == SCEN_TYPE_SAVE_ALL) // failed to save a guy
	{
		if (show_mode_ending_popup(ending, nextlevel))
			return; // mode-owned run-end popup shown
        popup_dialog("Defeat!", "YOU ARE DEFEATED!\nYOU FAILED TO KEEP YOUR ALLY ALIVE");
	}
	else if (ending == 0) // we won
	{
		if (show_mode_ending_popup(ending, nextlevel))
			return; // mode-owned win popup (tower floor cleared) shown
		if (show_scripted_mode_ending_popup(nextlevel))
			return; // decided scripted-mode match: outcome-aware popup shown
		if (show_ctf_ending_popup(nextlevel))
			return; // decided CTF match: outcome-aware popup shown
		if (og::runtime::current_session->myscreen_->save_data.is_level_completed(og::runtime::current_session->myscreen_->save_data.scen_num)) // this scenario is completed ..
		{
		    std::string buf = std::format("Moving on to\n{}", og::data::scenario_display_name(nextlevel));
		    popup_dialog("Traveling on...", buf.c_str());
		}
		else
		{
		    popup_dialog("Victory!", "You have won the battle!");
		}
	}
}

bool results_screen(int ending, int nextlevel)
{
#ifdef TESTING
    if (!s_force_full_results_ui)
    {
        // In test mode we still want to exercise the "ending" branching logic,
        // but must avoid modal UI loops that would hang CI.
        show_ending_popup(ending, nextlevel);
        return false;
    }
#endif
    // Popup the ending dialog
    show_ending_popup(ending, nextlevel);
    return false;
}



class TroopResult
{
public:
    guy* before = nullptr;
    walker* after = nullptr;

    std::string get_name() const;
    std::string get_class_name() const;
    char get_family() const;
    int get_level() const;
    bool gained_level() const;
    bool lost_level() const;
    std::vector<std::string> get_gained_specials() const;
    
    // These are percentages of what you need for the next level.
    float get_XP_base() const;
    float get_XP_gain() const;  // could be negative, if a level is lost, it is a percentage of the XP needed for the lost level
    
    int get_tallies() const;
    float get_HP() const;  // percentage of total
    bool is_dead() const;
    bool is_new() const;
    
    void draw_guy(int cx, int cy, int frame);
    
    TroopResult(guy* before_, walker* after_);
};

TroopResult::TroopResult(guy* before_, walker* after_)
    : before(before_), after(after_)
{
    if(after != nullptr && after->myguy == nullptr)
        after = nullptr;
}

std::string TroopResult::get_name() const
{
    if(before != nullptr)
        return before->name;
    if(after != nullptr)
        return after->myguy->name;
    return std::string();
}

char TroopResult::get_family() const
{
    char family = FAMILY_SOLDIER;
    if(before != nullptr)
        family = before->family;
    if(after != nullptr)
        family = after->myguy->family;
    return family;
}

const char* get_family_string(Sint32 family);

std::string TroopResult::get_class_name() const
{
    if(before != nullptr)
        return og::ui::family_display_name(before->family);
    if(after != nullptr)
        return og::ui::family_display_name(after->myguy->family);
    return std::string();
}

int TroopResult::get_level() const
{
    if(after != nullptr)
        return calculate_level(after->myguy->exp);
    if(before != nullptr)
        return calculate_level(before->exp);
    
    return 0;
}

bool TroopResult::gained_level() const
{
    if(after == nullptr || before == nullptr)
        return false;
    
    return calculate_level(after->myguy->exp) > before->level;
}

bool TroopResult::lost_level() const
{
    if(after == nullptr || before == nullptr)
        return false;
    
    return calculate_level(after->myguy->exp) < before->level;
}

std::vector<std::string> TroopResult::get_gained_specials() const
{
    std::vector<std::string> result;
    
    int family = get_family();
    if (family < 0 || family >= NUM_FAMILIES)
        return result;  // bogus family from an untrusted save -> no specials

    Sint32 test1 = get_level() - 1;
    if ( !(test1%3) ) // we're on a special-gaining level
    {
        test1 = (test1 / 3) + 1; // this is the special #
        if ( (test1 <= 4) // raise this when we have more than 4 specials
                && (og::runtime::current_session->myscreen_->special_name[family][test1] != "NONE") )
        {
            result.push_back(og::runtime::current_session->myscreen_->special_name[family][test1]);
        }
    }
    
    return result;
}

float TroopResult::get_XP_base() const
{
    if(before == nullptr)
        return 0.0f;
    
    if(gained_level())
        return 0.0f;
    
    const Uint32 exp_prev = calculate_exp(before->level);
    const Uint32 exp_next = calculate_exp(before->level + 1);
    return static_cast<float>(before->exp - exp_prev) / static_cast<float>(exp_next);
}

float TroopResult::get_XP_gain() const
{
    if(after == nullptr || before == nullptr)
        return 0.0f;
        
    if(gained_level())
    {
        const Uint32 exp_next = calculate_exp(before->level + 1);
        const Uint32 exp_next_next = calculate_exp(before->level + 2);
        return static_cast<float>(after->myguy->exp - exp_next) / static_cast<float>(exp_next_next);
    }
    
    if(lost_level())
    {
        const std::int64_t delta = static_cast<std::int64_t>(after->myguy->exp) - static_cast<std::int64_t>(before->exp);
        return static_cast<float>(delta) / static_cast<float>(calculate_exp(before->level));
    }
    
    {
        const std::int64_t delta = static_cast<std::int64_t>(after->myguy->exp) - static_cast<std::int64_t>(before->exp);
        return static_cast<float>(delta) / static_cast<float>(calculate_exp(before->level + 1));
    }
}

int TroopResult::get_tallies() const
{
    if(after == nullptr)
        return 0;
    
    return after->myguy->scen_kills;
}

float TroopResult::get_HP() const
{
    if(after == nullptr)
        return 0.0f;
    
    if(after->myguy->scen_min_hp > after->stats()->hitpoints())
        return 1.0f;
    
    return after->myguy->scen_min_hp/after->stats()->max_hitpoints();
}

bool TroopResult::is_dead() const
{
    return (get_HP() <= 0.0f);
}

bool TroopResult::is_new() const
{
    return (before == nullptr && after != nullptr);
}


void show_guy(Sint32 frames, guy* myguy, Sint32 centerx, Sint32 centery) // shows the current guy ..
{
	std::unique_ptr<walker> mywalker;
	Sint32 i;
	Sint32 newfamily;

	if (!myguy)
		return;

	frames = abs(frames);

	newfamily = myguy->family;

	mywalker = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Living,
	           newfamily);
	mywalker->stats()->set_bit_flags(0);
	mywalker->set_curdir(FACE_DOWN);
	mywalker->set_ani_type(ANI_WALK);
	for (i=0; i <= (frames/4)%4; i++)
		mywalker->animate();
    
	mywalker->set_team_num(static_cast<unsigned char>(myguy->teamnum));
    
    viewscreen* view_buf = og::runtime::current_session->myscreen_->viewob[0].get();
	mywalker->setxy(centerx - (mywalker->sizex()/2) + view_buf->topx - view_buf->xloc, centery - (mywalker->sizey()/2) + view_buf->topy - view_buf->yloc);
	draw_walker(*mywalker, view_buf);
}

void TroopResult::draw_guy(int cx, int cy, int frame)
{
    guy* myguy = nullptr;
    if(after != nullptr)
        myguy = after->myguy;
    else if(before != nullptr)
        myguy = before;
    else
        return;
    
    if(!is_dead() && myguy != nullptr)
        show_guy(frame, myguy, cx, cy);
}




#define BEGIN_IF_IN_SCROLL_AREA \
if(area_inner.y < y && y + 10 < area_inner.y + area_inner.h) {

#define END_IF_IN_SCROLL_AREA \
}

std::string format_ctf_winner_banner(int winner_team)
{
    // Same wording as the sim's match-end notification: color name, never a
    // bare 1-indexed team number (players see tints, not indices).
    return std::format("{} TEAM WINS!", og::sim::ctf_team_color_name(winner_team));
}

std::vector<CtfCapsSegment> format_ctf_caps_segments(const og::sim::CtfState& ctf)
{
    std::vector<CtfCapsSegment> segments;
    segments.push_back({"CAPS ", -1});
    bool first = true;
    for (int team = 0; team < 4; ++team)
    {
        if (!ctf.team_active[team])
            continue;
        if (!first)
            segments.push_back({":", -1});
        first = false;
        segments.push_back({std::format("{}", ctf.captures[team]), team});
    }
    return segments;
}

std::vector<CtfCapsSegment> format_mode_scoreboard_segments(
    const og::sim::ModeState& mode)
{
    // The mode's own scoreboard text is HUD slot 0, verbatim — the generic
    // twin of the CAPS line (Lua owns the wording, C++ never knows modes).
    std::vector<CtfCapsSegment> segments;
    const og::sim::ModeHudLine& line = mode.hud[0];
    if (line.text[0] == '\0')
        return segments;
    const int team =
        (line.team < SCORE_TEAM_COUNT) ? static_cast<int>(line.team) : -1;
    segments.push_back({line.text.data(), team});
    return segments;
}

int get_num_foes(LevelRuntimeData& level, short viewer_team)
{
    int result = 0;
    
	for(auto& uptr : level.world().oblist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead() && ob->query_order() == Order::Living &&
		    ob->team_num() != static_cast<unsigned char>(viewer_team))
		{
		    result++;
		}
	}
	
	return result;
}

Uint32 get_time_bonus(int playernum)
{
    if(playernum > 0)
        return 0;
    // Simulation ticks are authoritative and part of every world snapshot.
    // Display framecount can run ahead/behind on a delayed network client,
    // which used to let peers independently calculate different payouts.
    Uint32 frames =
        og::runtime::current_session->myscreen_->world().level_tick_count();
    Uint32 time_limit = (og::runtime::current_session->myscreen_->world().time_bonus_limit > 0? og::runtime::current_session->myscreen_->world().time_bonus_limit : 0);
    Log("Frames used: {}\n", frames);
    if(frames >= time_limit)
        return 0;
    
    short par_value = og::runtime::current_session->myscreen_->world().par_value;
    Uint32 score = og::runtime::current_session->myscreen_->save_data.m_score[playernum];
    float multiplier = (1.0f + static_cast<float>(par_value)/10.0f) * (static_cast<float>(time_limit - frames) / static_cast<float>(time_limit));
    Log("Time bonus: {:.0f}\n", static_cast<float>(score) * multiplier);
    return static_cast<Uint32>(static_cast<float>(score) * multiplier);
}

bool results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after)
{
#ifdef TESTING
    if (!s_force_full_results_ui)
    {
        // Same rationale as the 2-arg overload: cover branching without UI loops.
        show_ending_popup(ending, nextlevel);
        (void)before;
        (void)after;
        return false;
    }
#endif
    video& output = *og::runtime::current_session->myscreen_;
    ScopedUiCanvas canvas_target(output);

    // Popup the ending dialog
    show_ending_popup(ending, nextlevel);

    // The popup and results panel share the fixed UI surface. Restore the
    // nearest-scaled gameplay backdrop so the closed popup does not remain
    // visible around the results panel.
    if (canvas_target.entered_from_world())
        output.prepare_ui_canvas_from_world();

    // Clear any stale input events after popup closes
    // This helps prevent ASYNCIFY state issues in Emscripten
    og::input_native::sleep_ms(50);  // Small delay to let browser events settle
    get_input_events(POLL);  // Drain event queue

    LevelRuntimeData& level_data = og::runtime::current_session->myscreen_->level_runtime_data();
    SaveData& save_data = og::runtime::current_session->myscreen_->save_data;
    
    int num_foes_left = get_num_foes(level_data, save_data.my_team);
    int num_foes_total = 0;
    {
        LevelRuntimeData original_level(level_data.world().id);
        original_level.load();
        num_foes_total = get_num_foes(original_level, save_data.my_team);
    }

	text& mytext = og::runtime::current_session->myscreen_->text_normal;
	text& bigtext = og::runtime::current_session->myscreen_->text_big;
	std::array<Uint32, 4> bonuscash = {0, 0, 0, 0};
	Uint32 allscore = 0, allbonuscash = 0;

	for(int i = 0; i < 4; i++)
		allscore += save_data.m_score[i];

	if(ending == 0)  // we won
	{
	    // Calculate bonuses
		for (int i = 0; i < 4; i++)
		{
			bonuscash[static_cast<std::size_t>(i)] = get_time_bonus(i);

			allbonuscash += bonuscash[static_cast<std::size_t>(i)];
		}
		if (save_data.is_level_completed(save_data.scen_num)) // already won, no bonus
		{
			for(int i = 0; i < 4; i++)
				bonuscash[static_cast<std::size_t>(i)] = 0;
			allbonuscash = 0;
		}
	}
	
    // Now show the results
    
    std::set<int> used_troops;
    std::vector<TroopResult> troops;
    
    // Get the guys from "before"
    for(auto& [id, g] : before)
    {
        used_troops.insert(id);
        troops.push_back(TroopResult(g, after[id]));
    }

    // Get the ones from "after" that weren't in "before"
    for(auto& [id, w] : after)
    {
        if(used_troops.insert(id).second)
            troops.push_back(TroopResult(before[id], w));
    }
    
    // MVP. A foreign-color company member is an opponent in this mission and
    // must never headline the local team's results. In a decided CTF or
    // scripted-mode match, scope to the winning team instead. If that side
    // fielded no rostered humans, mvp stays null and the line is omitted
    // below.
    const GameWorld& mvp_world = og::runtime::current_session->myscreen_->world();
    const bool ctf_winner_scope =
        (mvp_world.type & GameWorld::TYPE_CTF) && mvp_world.ctf.active &&
        mvp_world.ctf.winner_team >= 0;
    const bool mode_winner_scope =
        (mvp_world.type & GameWorld::TYPE_SCRIPTED) && mvp_world.mode.active &&
        mvp_world.mode.winner_team >= 0;
    const short mvp_team = ctf_winner_scope
        ? mvp_world.ctf.winner_team
        : mode_winner_scope ? static_cast<short>(mvp_world.mode.winner_team)
                            : mvp_world.my_team;
    walker* mvp = nullptr;
    float mvp_points = 0;
    for(auto& troop : troops)
    {
        float points = 0;

        if(troop.after == nullptr)
            continue;

        if(troop.after->team_num() != static_cast<unsigned char>(mvp_team))
            continue;

        points = troop.after->myguy->scen_damage + 3*troop.after->myguy->scen_damage_taken;

        if(mvp_points < points)
        {
            mvp = troop.after;
            mvp_points = points;
        }
    }
    if (mvp != nullptr)
        TRACE("results", "mvp_pick name=%s team=%d", mvp->myguy->name.c_str(),
              static_cast<int>(mvp->team_num()));
    else
        TRACE("results", "mvp_none");
    
    // Hold indices for troops
    std::vector<int> recruits;
    std::vector<int> losses;
    int i = 0;
    for(auto& troop : troops)
    {
        if(troop.is_dead())
            losses.push_back(i);
        else if(troop.is_new())
            recruits.push_back(i);
        i++;
    }
    
    
    bool retry = false;
    // Tier-B retry suppression (tower-triple §2.7, D10): a run-based mode
    // hides RETRY entirely (tower: a wipe ends the run; replaying the death
    // floor for free would be infinite retries). The nav links below route
    // around the hidden button — MenuNav links are raw indices and do not
    // skip hidden buttons.
    // The authoritative network server has already committed the completed
    // outcome before peers enter this display-only screen. A local Retry here
    // cannot roll that state back consistently across peers.
    const bool suppress_retry =
        (og::runtime::current_session != nullptr &&
         og::runtime::current_session->networked_session_) ||
        og::mode::current_progression().suppress_retry();
    int mode = 0;
    float scroll = 0.0f;
    int frame = 0;
    
    int screenW = 320;
    int screenH = 200;
    
    UiRect area;
    area.x = 50;
    area.y = 20;
    area.w = screenW - 2*area.x;
    area.h = screenH - 2*area.y;
    
    UiRect area_inner = {area.x + 3, area.y + 17, area.w - 6, area.h - 34};

    // Buttons
    UiRect ok_rect = {area.x + area.w/2 - 45, area.y + area.h - 14, 35, 10};
    UiRect retry_rect = {area.x + area.w/2 + 10, area.y + area.h - 14, 35, 10};

    UiRect overview_rect = {area.x + area.w/2 - 100, area.y + 4, 50, 10};
    UiRect troops_rect = {area.x + area.w/2 + 50, area.y + 4, 50, 10};
    
    
    // Controller input
    int retvalue = 0;
	int highlighted_button = 0;
	
	int ok_index = 0;
	int retry_index = 1;
	int overview_index = 2;
	int troops_index = 3;
	int num_buttons = 4;
	
	button buttons[] = {
        button("ok", "OK", KEYSTATE_UNKNOWN, ok_rect.x, ok_rect.y, ok_rect.w, ok_rect.h, 0, -1 , MenuNav{.up=overview_index, .right=(suppress_retry ? -1 : retry_index)}),
        button("retry", "RETRY", KEYSTATE_UNKNOWN, retry_rect.x, retry_rect.y, retry_rect.w, retry_rect.h, 0, -1 , MenuNav{.up=troops_index, .left=ok_index}, suppress_retry),
        button("overview", "OVERVIEW", KEYSTATE_UNKNOWN, overview_rect.x, overview_rect.y, overview_rect.w, overview_rect.h, 0, -1 , MenuNav{.down=ok_index, .right=troops_index}),
        button("troops", "TROOPS", KEYSTATE_UNKNOWN, troops_rect.x, troops_rect.y, troops_rect.w, troops_rect.h, 0, -1 , MenuNav{.down=(suppress_retry ? ok_index : retry_index), .left=overview_index}),
	};
	
	
    bool done = false;
    bool was_mouse_down = false;  // Track previous mouse state for edge detection
#ifdef TESTING
    s_results_ui_loop_live.store(true);
#endif
    while (!done)
    {
#ifdef TESTING
        s_results_ui_frames.fetch_add(1);
#endif
        // Reset the timer count to zero ...
        reset_timer();

        if(og::runtime::current_session->myscreen_->world().end)
        {
            TRACE("results", "exit world_end");
            break;
        }

        // Get keys and stuff
        get_input_events(POLL);

        handle_menu_nav(buttons, highlighted_button, retvalue, false);

        // Mouse stuff ..
        // Use no_poll version since we already called get_input_events above
		MouseState& mymouse = query_mouse_no_poll();
        int mx = static_cast<int>(mymouse.x);
        int my = static_cast<int>(mymouse.y);

        // Detect mouse click (transition from not pressed to pressed)
        // This avoids blocking while loops that cause issues with Emscripten/ASYNCIFY
        bool mouse_down = mymouse.left;
        bool do_click = mouse_down && !was_mouse_down;
        was_mouse_down = mouse_down;

		scroll -= static_cast<float>(get_and_reset_scroll_amount());
		if(scroll < 0.0f)
            scroll = 0.0f;

		bool do_ok = ((do_click && ok_rect.x <= mx && mx <= ok_rect.x + ok_rect.w
               && ok_rect.y <= my && my <= ok_rect.y + ok_rect.h) || (retvalue == OG_OK && highlighted_button == ok_index));
		bool do_retry = !suppress_retry && ((do_click && retry_rect.x <= mx && mx <= retry_rect.x + retry_rect.w
               && retry_rect.y <= my && my <= retry_rect.y + retry_rect.h) || (retvalue == OG_OK && highlighted_button == retry_index));
		bool do_overview = ((do_click && overview_rect.x <= mx && mx <= overview_rect.x + overview_rect.w
               && overview_rect.y <= my && my <= overview_rect.y + overview_rect.h) || (retvalue == OG_OK && highlighted_button == overview_index));
		bool do_troops = ((do_click && troops_rect.x <= mx && mx <= troops_rect.x + troops_rect.w
               && troops_rect.y <= my && my <= troops_rect.y + troops_rect.h) || (retvalue == OG_OK && highlighted_button == troops_index));

       // Ok
       if(do_ok)
       {
           og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
           TRACE("results", "exit ok_click");
           done = true;
       }
       // Retry
       else if(do_retry)
       {
           og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
           const char* msg = (ending == 0? "Try this level again?\nYou will lose your progress\non this level." : "Try this level again?");
           if(yes_or_no_prompt("Retry level", msg, false))
           {
               // Try again
               done = true;
               retry = true;
           }
       }
       // Overview
       else if(do_overview)
       {
           og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
           mode = 0;
       }
       // Troops
       else if(do_troops)
       {
           og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
           mode = 1;
       }
       
        retvalue = 0;

        // Draw
        og::runtime::current_session->myscreen_->draw_button(area.x, area.y, area.x + area.w - 1, area.y + area.h - 1, 1, 1);
        og::runtime::current_session->myscreen_->draw_button_inverted(area_inner.x, area_inner.y, area_inner.w, area_inner.h);
        bigtext.write_xy_center(area.x + area.w/2, area.y + 4, RED, "RESULTS");
        
	        int y = 0;
        if(mode == 0)
        {
            // Overview
            int x = area.x + 12;
	            y = static_cast<int>(static_cast<float>(area.y + 30) - scroll);

            // CTF match summary: winner banner + a single capture line
            // ("CAPS 3:5:1:0", counts in their team ramps), at a tight 8px
            // pitch so the classic gold/foes block stays on the first screen.
            const GameWorld& ctf_world =
                og::runtime::current_session->myscreen_->world();
            if ((ctf_world.type & GameWorld::TYPE_CTF) && ctf_world.ctf.active)
            {
                if (ctf_world.ctf.winner_team >= 0)
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy_center_shadow(
                        area.x + area.w/2, y,
                        static_cast<unsigned char>(ctf_world.ctf.winner_team * 16 + 40),
                        "%s",
                        format_ctf_winner_banner(ctf_world.ctf.winner_team).c_str());
                    END_IF_IN_SCROLL_AREA;
                    y += 8;
                }
                BEGIN_IF_IN_SCROLL_AREA;
                const std::vector<CtfCapsSegment> caps_segments =
                    format_ctf_caps_segments(ctf_world.ctf);
                Sint32 caps_width = 0;
                for (const CtfCapsSegment& segment : caps_segments)
                    caps_width += static_cast<Sint32>(segment.text.size()) * 6;
                Sint32 caps_x = area.x + area.w/2 - caps_width/2;
                for (const CtfCapsSegment& segment : caps_segments)
                {
                    const unsigned char color = (segment.team < 0)
                        ? PURE_WHITE
                        : static_cast<unsigned char>(segment.team * 16 + 40);
                    mytext.write_xy_shadow(caps_x, y, color, "%s",
                                           segment.text.c_str());
                    caps_x += static_cast<Sint32>(segment.text.size()) * 6;
                }
                END_IF_IN_SCROLL_AREA;
                y += 8;
            }

            // Scripted-mode match summary: winner banner + the mode's own
            // scoreboard line (HUD slot 0 verbatim), at the CTF block's
            // tight 8px pitch. Disjoint from the CTF block (no CTF level is
            // scripted until the CTF engine retirement).
            if ((ctf_world.type & GameWorld::TYPE_SCRIPTED) &&
                ctf_world.mode.active)
            {
                if (ctf_world.mode.winner_team >= 0)
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy_center_shadow(
                        area.x + area.w/2, y,
                        static_cast<unsigned char>(
                            ctf_world.mode.winner_team * 16 + 40),
                        "%s",
                        format_ctf_winner_banner(
                            ctf_world.mode.winner_team).c_str());
                    TRACE("results", "mode_winner_banner team=%d",
                          static_cast<int>(ctf_world.mode.winner_team));
                    END_IF_IN_SCROLL_AREA;
                    y += 8;
                }
                const std::vector<CtfCapsSegment> mode_segments =
                    format_mode_scoreboard_segments(ctf_world.mode);
                if (!mode_segments.empty())
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    Sint32 seg_width = 0;
                    for (const CtfCapsSegment& segment : mode_segments)
                        seg_width += static_cast<Sint32>(segment.text.size()) * 6;
                    Sint32 seg_x = area.x + area.w/2 - seg_width/2;
                    for (const CtfCapsSegment& segment : mode_segments)
                    {
                        const unsigned char color = (segment.team < 0)
                            ? PURE_WHITE
                            : static_cast<unsigned char>(segment.team * 16 + 40);
                        mytext.write_xy_shadow(seg_x, y, color, "%s",
                                               segment.text.c_str());
                        seg_x += static_cast<Sint32>(segment.text.size()) * 6;
                    }
                    TRACE("results", "mode_scoreboard %s",
                          mode_segments.front().text.c_str());
                    END_IF_IN_SCROLL_AREA;
                    y += 8;
                }
            }

            // Tier-B mode summary (tower-triple §2.7): the mounted mode may
            // add overview lines (tower: "Floor N conquered - best B"),
            // centered at the CTF banner's tight 8px pitch so the classic
            // gold/time block stays on the first screen. Classic adds none.
            {
                const std::vector<std::string> mode_lines =
                    og::mode::current_progression().results_summary_lines(
                        save_data, ctf_world);
                for (const std::string& line : mode_lines)
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy_center_shadow(area.x + area.w/2, y,
                                                  PURE_WHITE, "%s",
                                                  line.c_str());
                    TRACE("results", "mode_summary_drawn %s", line.c_str());
                    END_IF_IN_SCROLL_AREA;
                    y += 8;
                }
            }

            if(ending == 0)
            {
                // TODO: Show total possible gold collected?  How is this factored into allscore?
                if(area_inner.y < y && y + 10 < area_inner.y + area_inner.h)
                {
                    std::string buf = std::format("{}", allscore*2);
                    mytext.write_xy_center_shadow(area.x + area.w/2, y, YELLOW, "%s Gold       ", buf.c_str());
                    std::string spaces(buf.size(), ' ');
                    mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "%s      Gained", spaces.c_str());
                }
                if(area_inner.y < y && y + 10 < area_inner.y + area_inner.h)
                {
                    if(allbonuscash > 0)
                    {
                        mytext.write_xy_center_shadow(area.x + area.w/2, y + 9, YELLOW, "+ %d Time Bonus", allbonuscash);
                    }
                }
                y += 22;
            }

            // §2.8: a networked win shows THIS machine's prospective share of
            // the pot (split by deploy-ratio; 0-deploy machines see 0). The
            // results screen renders pre-fold, so the delta is computed on the
            // spot from the live m_score + time bonus. Local/solo prints
            // nothing here, keeping single-player output byte-identical.
            if (ending == 0 &&
                og::runtime::current_session != nullptr &&
                og::runtime::current_session->networked_session_)
            {
                BEGIN_IF_IN_SCROLL_AREA;
                og::progression::NetWinFoldCapture prospective;
                for (std::size_t t = 0; t < 4; ++t)
                    prospective.cash_delta[t] =
                        save_data.m_score[t] * 2u +
                        bonuscash[t];
                prospective.deployed =
                    og::progression::collect_deployed_contributors(
                        og::runtime::current_session->myscreen_->world());
                const std::uint64_t share =
                    og::progression::machine_cash_share(
                        prospective,
                        std::span<const std::uint8_t>(
                            og::runtime::current_session->own_player_indices_));
                const std::uint64_t pot =
                    static_cast<std::uint64_t>(allscore) * 2u + allbonuscash;
                std::string share_buf = std::format("{}", share);
                std::string pot_buf = std::format("{}", pot);
                mytext.write_xy_center_shadow(area.x + area.w / 2, y, YELLOW,
                    "YOUR SHARE: %s OF %s", share_buf.c_str(), pot_buf.c_str());
                END_IF_IN_SCROLL_AREA;
                y += 12;
            }

            BEGIN_IF_IN_SCROLL_AREA;
            if(ending == 0)
            {
                std::string buf = std::format("{}", num_foes_total - num_foes_left);
                mytext.write_xy_center_shadow(area.x + area.w/2, y, PURE_WHITE, "%s Foes         ", buf.c_str());
                std::string spaces(buf.size(), ' ');
                mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "%s      Defeated", spaces.c_str());
            }
            else
            {
                std::string buf = std::format("{}", num_foes_total - num_foes_left);
                std::string buf2 = std::format("{}", num_foes_total);
                mytext.write_xy_center_shadow(area.x + area.w/2, y, PURE_WHITE, "%s of %s Foes         ", buf.c_str(), buf2.c_str());
                std::string spaces(buf.size(), ' ');
                std::string spaces2(buf2.size(), ' ');
                mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "%s    %s      Defeated", spaces.c_str(), spaces2.c_str());
            }
            END_IF_IN_SCROLL_AREA;
            y += 22;
            
            if(mvp != nullptr)
            {
                BEGIN_IF_IN_SCROLL_AREA;
                
                mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "MVP: %s the %s", mvp->myguy->name.c_str(), og::ui::family_display_name(mvp->myguy->family));
                mytext.write_xy_center(area.x + area.w/2, y + 8, DARK_BLUE, "(%.0f pts)", mvp_points);
                y += 22;
                
                END_IF_IN_SCROLL_AREA;
            }
            
            if(ending == 0 && recruits.size() > 0)
            {
                BEGIN_IF_IN_SCROLL_AREA;
                mytext.write_xy(x, y, DARK_BLUE, "%d Recruits:", recruits.size());
                END_IF_IN_SCROLL_AREA;
                y += 22;
                for(auto idx : recruits)
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy(x, y, DARK_BLUE, " + %s the %s LVL %d", troops[idx].get_name().c_str(), troops[idx].get_class_name().c_str(), troops[idx].get_level());
                    END_IF_IN_SCROLL_AREA;
                    
                    y += 11;
                }
                y += 11;
            }
            
            if(ending != 1 && losses.size() > 0)  // won or lost due to NPC
            {
                BEGIN_IF_IN_SCROLL_AREA;
                mytext.write_xy(x, y, DARK_BLUE, "%d Losses:", losses.size());
                END_IF_IN_SCROLL_AREA;
                
                y += 22;
                for(auto idx : losses)
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy(x, y, DARK_BLUE, " - %s the %s LVL %d", troops[idx].get_name().c_str(), troops[idx].get_class_name().c_str(), troops[idx].get_level());
                    END_IF_IN_SCROLL_AREA;
                    
                    y += 11;
                }
                y += 11;
            }
        }
        else if(mode == 1)
        {
            int barH = 5;
            // Troops
	            y = static_cast<int>(static_cast<float>(area.y + 30) - scroll);
	            for(size_t troop_idx = 0; troop_idx < troops.size(); troop_idx++)
	            {
	                int x = area.x + 12;
	                
	                int tallies = troops[troop_idx].get_tallies();
	                
	                BEGIN_IF_IN_SCROLL_AREA;
	                int name_w = mytext.write_xy(x, y, PURE_BLACK, "%s", troops[troop_idx].get_name().c_str());
	                name_w += mytext.write_xy(x + name_w, y, PURE_BLACK + 2, " the %s", troops[troop_idx].get_class_name().c_str());
	                if(troops[troop_idx].gained_level())
	                    mytext.write_xy(x + name_w, y, YELLOW, " LVL UP %d", troops[troop_idx].get_level());
	                else if(troops[troop_idx].lost_level())
	                    mytext.write_xy(x + name_w, y, RED, " LVL DOWN %d", troops[troop_idx].get_level());
	                else
	                    mytext.write_xy(x + name_w, y, DARK_GREEN, " LVL %d", troops[troop_idx].get_level());
	                END_IF_IN_SCROLL_AREA;
                
	                y += 10;
	                
	                BEGIN_IF_IN_SCROLL_AREA;
	                troops[troop_idx].draw_guy(x + 5, y + 2, static_cast<Sint32>(static_cast<float>(frame) * troops[troop_idx].get_HP()));
	                
	                // HP
	                if(troops[troop_idx].is_dead())
	                {
	                    mytext.write_xy(x + 15, y, RED, "LOST");
	                }
	                else
	                {
	                    x += 15;
	                    mytext.write_xy(x, y, RED, "HP");
	                    x += 14;
	                    og::runtime::current_session->myscreen_->fastbox(x, y, static_cast<Sint32>(60.0f * troops[troop_idx].get_HP()), barH, RED);
	                    og::runtime::current_session->myscreen_->fastbox_outline(x, y, 60, barH, PURE_BLACK);
	                    
	                    // XP
	                    x += 70;
	                    mytext.write_xy(x, y, DARK_GREEN, "EXP");
	                    x += 20;
	                    float base = 60.0f * troops[troop_idx].get_XP_base();
	                    float gain = 60.0f * troops[troop_idx].get_XP_gain();
	                    if(gain >= 0)
	                    {
	                        og::runtime::current_session->myscreen_->fastbox(x, y, static_cast<Sint32>(base), barH, DARK_GREEN);
	                        og::runtime::current_session->myscreen_->fastbox(x + static_cast<Sint32>(base), y, static_cast<Sint32>(gain), barH, LIGHT_GREEN);
	                    }
	                    else
	                        og::runtime::current_session->myscreen_->fastbox(x + 60 + static_cast<Sint32>(gain), y, static_cast<Sint32>(-gain), barH, RED);
	                    og::runtime::current_session->myscreen_->fastbox_outline(x, y, 60, barH, PURE_BLACK);
                    
                    if(gain > 0.0f)
                        mytext.write_xy(x + 63, y, DARK_GREEN, "+");
                    else if(gain < 0.0f)
                        mytext.write_xy(x + 63, y, RED, "-");
                }
	                END_IF_IN_SCROLL_AREA;
                
                
	                if(tallies > 0)
	                {
	                    y += 10;
	                    BEGIN_IF_IN_SCROLL_AREA;
	                    mytext.write_xy(area.x + 20, y, DARK_GREEN, "%d Tall%s", tallies, (tallies == 1? "y" : "ies"));
	                    END_IF_IN_SCROLL_AREA;
	                }
	                
	                if(troops[troop_idx].gained_level())
	                {
	                    std::vector<std::string> specials = troops[troop_idx].get_gained_specials();
                    if(specials.size() > 0)
                    {
                        y += 10;
                        BEGIN_IF_IN_SCROLL_AREA;
                        mytext.write_xy(area.x + 20, y, DARK_BLUE, "Gained special%s:", (specials.size() == 1? "" : "s"));
                        END_IF_IN_SCROLL_AREA;
                        
                        for(auto& spec : specials)
                        {
                            y += 10;
                            BEGIN_IF_IN_SCROLL_AREA;
                            mytext.write_xy(area.x + 30, y, DARK_BLUE, "%s", spec.c_str());
                            END_IF_IN_SCROLL_AREA;
                        }
                    }
                }
                
                y += 13;
                
                BEGIN_IF_IN_SCROLL_AREA;
                og::runtime::current_session->myscreen_->hor_line(area_inner.x + 6, y - 3, area_inner.w - 30, GREY - 4);
                END_IF_IN_SCROLL_AREA;
            }
        }
        
        // Draw scroll indicator
	        if(static_cast<float>(y) + scroll > static_cast<float>(area_inner.y + area_inner.h - 30))
	        {
	            const float denom = static_cast<float>(y) + scroll - static_cast<float>(area.y);
	            const float y_off = (denom != 0.0f) ? (static_cast<float>(area_inner.h) * scroll / denom) : 0.0f;
	            og::runtime::current_session->myscreen_->ver_line(area_inner.x, static_cast<Sint32>(static_cast<float>(area_inner.y) + y_off), 6, PURE_BLACK);
	        }
        
        // Limit the scrolling depending on how long 'y' is.
	        if(y < area_inner.y + 30)
	            scroll = static_cast<float>(y - (area_inner.y + 30)) + scroll;
        
        
	        for(int button_i = 0; button_i < num_buttons; button_i++)
	        {
	            if(buttons[button_i].hidden)
	                continue; // retry suppressed by the mounted mode
	            if((mode == 0 && button_i == overview_index) || (mode == 1 && button_i == troops_index))
	                og::runtime::current_session->myscreen_->draw_button_inverted(buttons[button_i].x, buttons[button_i].y, buttons[button_i].sizex, buttons[button_i].sizey);
	            else
	                og::runtime::current_session->myscreen_->draw_button(buttons[button_i].x, buttons[button_i].y, buttons[button_i].x + buttons[button_i].sizex - 1, buttons[button_i].y + buttons[button_i].sizey - 1, 1, 1);
	            mytext.write_xy(buttons[button_i].x + buttons[button_i].sizex/2 - 3*static_cast<Sint32>(buttons[button_i].label.size()), buttons[button_i].y + 2, buttons[button_i].label.c_str(), DARK_BLUE, 1);
	        }
        
        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        og::input_native::sleep_ms(10);
        
        frame++;
        if(frame > 1000000)
            frame = 0;
    }

#ifdef TESTING
    s_results_ui_loop_live.store(false);
#endif
    return retry;
}

#ifdef TESTING
#include "../../../tests/coverage_internal/results_screen_internal.inc"
#endif
