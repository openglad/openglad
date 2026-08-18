/* Match-plan census: the one C++ projection feeding on_mode_plan.
 *
 * Plain data only (numbers and tables downstream) — the plan runs under
 * the plan-dispatch fence and can never hold a walker handle, so the
 * entity walk lives here, once, shared by launch (level_mode_init) and
 * preview (level_mode_plan callers). Predicates mirror the walks the mode
 * inits ran in-body before the plan phase existed: live livings split by
 * myguy, live generators, raw flag rows in fx order, anchors from the
 * engine scan, requests from the ctf_requested_* knobs.
 */

#include <openglad/gameplay/mode/match_plan.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

namespace og::sim {

MatchPlanInputs build_match_plan_inputs(const GameWorld& world)
{
    MatchPlanInputs in;
    in.team_count = world.ctf_requested_team_count;
    in.strip_troops = world.ctf_requested_strip_scenario_troops;
    in.score_limit = world.ctf_requested_capture_limit;
    in.respawn_ticks = world.ctf_requested_respawn_ticks;

    for (std::size_t t = 0; t < in.teams.size(); ++t)
        in.teams[t].anchors = world.respawn.anchor_count[t];

    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead())
            continue;
        const unsigned char team = w->team_num();
        if (team >= SCORE_TEAM_COUNT)
            continue;
        MatchPlanTeamInputs& row = in.teams[team];
        const Order order = w->query_order();
        if (order == Order::Living)
        {
            if (w->myguy != nullptr)
                row.roster++;
            else
                row.npcs++;
        }
        else if (order == Order::Generator)
        {
            row.generators++;
        }
    }

    for (const auto& uptr : world.fxlist)
    {
        const walker* fx = uptr.get();
        if (fx == nullptr || fx->dead() ||
            fx->query_order() != Order::Treasure ||
            fx->family() != kMatchPlanFlagFamily)
        {
            continue;
        }
        MatchPlanFlagInput flag;
        flag.team = fx->team_num();
        flag.level = (fx->stats() != nullptr) ? fx->stats()->level() : 0;
        in.flags.push_back(flag);
    }
    return in;
}

} // namespace og::sim
