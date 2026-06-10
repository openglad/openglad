/* Capture-the-flag AI director.
 *
 * Stateless cadence-gated role assignment for AI livings. Roles derive
 * entirely from replicated state (oblist order, walker positions, CtfState),
 * draw no randomness, and never touch player-controlled walkers, so every
 * peer that runs the director on the same world state issues the same
 * commands.
 *
 * Per active team, members are partitioned in oblist order:
 *   CARRIER      holds an enemy flag: leader = own flag entity (suppresses
 *                the tick auto-foe backstop), COMMAND_GOTO own flag home.
 *   INTERCEPTOR  own flag carried: the 2 nearest remaining members chase the
 *                enemy carrier (set_foe + COMMAND_SEARCH).
 *   RETRIEVER    own flag dropped: the nearest remaining member runs to it
 *                (a friendly touch returns the flag home).
 *   DEFENDER     first ceil(remaining/3): COMMAND_GOTO own flag home when
 *                more than kCtfDefendRadius away from it.
 *   ATTACKER     the rest: COMMAND_GOTO the nearest unsecured enemy flag's
 *                current position, issued when the queue is idle or holds a
 *                preemptible walk (a self-issued foe search or a stale goto).
 *   ESCORT       every 2nd would-be attacker while the team has a carrier:
 *                leader = carrier, COMMAND_FOLLOW.
 */

#include <openglad/gameplay/ctf/ctf_state.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdlib>
#include <vector>

namespace og::sim {

namespace {

// Defender leash: members on flag-guard duty are sent back to the flag home
// whenever they stray more than this far (Manhattan, px) from it.
inline constexpr int kCtfDefendRadius = 96;

// Manhattan distance, matching walker::distance_to_ob's metric so role
// decisions agree with the chase logic they hand off to.
int point_distance(const walker* w, int x, int y)
{
    return std::abs(static_cast<int>(w->xpos()) - x) +
           std::abs(static_cast<int>(w->ypos()) - y);
}

// Directable: a live, unfrozen AI living. Player-controlled walkers
// (ACT_CONTROL or a bound user) are never touched.
bool is_directable(const walker* w, int team)
{
    return w != nullptr && !w->dead() && w->query_order() == Order::Living &&
           w->team_num() == static_cast<unsigned char>(team) &&
           w->act_type() != ACT_CONTROL && w->user() == -1 &&
           w->stats() != nullptr && w->stats()->frozen_delay() <= 0;
}

bool carries_enemy_flag(const CtfState& ctf, int team, std::uint32_t entity_id)
{
    for (int t = 0; t < kCtfMaxFlags; ++t)
    {
        if (t == team)
            continue;
        const CtfFlag& f = ctf.flags[t];
        if (f.present && f.state == CtfFlagState::Carried &&
            f.carrier_entity_id == entity_id)
        {
            return true;
        }
    }
    return false;
}

bool leader_is_flag_entity(const CtfState& ctf, const walker* w)
{
    const walker* leader = w->leader();
    if (leader == nullptr)
        return false;
    for (int t = 0; t < kCtfMaxFlags; ++t)
    {
        if (ctf.flags[t].flag_entity_id != 0 &&
            ctf.flags[t].flag_entity_id == leader->entity_id())
        {
            return true;
        }
    }
    return false;
}

// Refreshes the front of the queue in place when the wanted command type is
// already active; otherwise force-inserts it. Re-issuing every cadence this
// way keeps the role current without growing the command queue.
void issue_front_command(walker* w, std::int32_t commandtype,
                         std::int32_t iterations, std::int32_t com1,
                         std::int32_t com2)
{
    statistics* s = w->stats();
    if (!s->commands.empty() && s->commands.front().commandtype == commandtype)
    {
        command& front = s->commands.front();
        front.commandcount = iterations;
        front.com1 = com1;
        front.com2 = com2;
        return;
    }
    s->force_command(commandtype, iterations, com1, com2);
}

// A carrier's leader (its own flag entity) outlives the carry: clear it when
// the walker takes a non-carrier role so the tick auto-foe backstop resumes.
void clear_stale_flag_leader(const CtfState& ctf, walker* w)
{
    if (leader_is_flag_entity(ctf, w))
        w->set_leader(nullptr);
}

void run_team_director(GameWorld& world, int team)
{
    CtfState& ctf = world.ctf;
    const CtfFlag& own_flag = ctf.flags[team];

    std::vector<walker*> members;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (is_directable(w, team))
            members.push_back(w);
    }
    if (members.empty())
        return;

    // CARRIERS: run the held flags home. The own-flag-entity leader keeps
    // GameWorld::tick's find_far_foe backstop from re-targeting the runner.
    std::vector<bool> assigned(members.size(), false);
    walker* lead_carrier = nullptr;
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        if (!carries_enemy_flag(ctf, team, members[i]->entity_id()))
            continue;
        assigned[i] = true;
        if (lead_carrier == nullptr)
            lead_carrier = members[i];
        if (own_flag.present)
        {
            if (walker* flag_entity = world.find_by_id(own_flag.flag_entity_id))
                members[i]->set_leader(flag_entity);
            issue_front_command(members[i], COMMAND_GOTO, 60, own_flag.home_x,
                                own_flag.home_y);
        }
    }

    // INTERCEPTORS: own flag stolen — the two nearest unassigned members
    // chase the enemy carrier through the classic foe-search machinery.
    // Nearest by Manhattan distance; ties resolve to the earlier oblist slot.
    if (own_flag.present && own_flag.state == CtfFlagState::Carried)
    {
        walker* enemy_carrier = world.find_by_id(own_flag.carrier_entity_id);
        if (enemy_carrier != nullptr && !enemy_carrier->dead())
        {
            for (int picks = 0; picks < 2; ++picks)
            {
                int best = -1;
                int best_distance = 0;
                for (std::size_t i = 0; i < members.size(); ++i)
                {
                    if (assigned[i])
                        continue;
                    const int distance =
                        point_distance(members[i], enemy_carrier->xpos(),
                                       enemy_carrier->ypos());
                    if (best < 0 || distance < best_distance)
                    {
                        best = static_cast<int>(i);
                        best_distance = distance;
                    }
                }
                if (best < 0)
                    break;
                assigned[best] = true;
                walker* interceptor = members[best];
                clear_stale_flag_leader(ctf, interceptor);
                interceptor->set_foe(enemy_carrier);
                issue_front_command(interceptor, COMMAND_SEARCH, 120, 0, 0);
            }
        }
    }

    // RETRIEVER: own flag on the ground — the nearest unassigned member runs
    // to it (a friendly touch returns it home instantly). Without a tasked
    // retriever a flag dropped inside the enemy base is simply re-picked by
    // the next enemy to bump it, and a both-flags-stolen match never resolves.
    if (own_flag.present && own_flag.state == CtfFlagState::Dropped)
    {
        int best = -1;
        int best_distance = 0;
        for (std::size_t i = 0; i < members.size(); ++i)
        {
            if (assigned[i])
                continue;
            const int distance =
                point_distance(members[i], own_flag.x, own_flag.y);
            if (best < 0 || distance < best_distance)
            {
                best = static_cast<int>(i);
                best_distance = distance;
            }
        }
        if (best >= 0)
        {
            assigned[best] = true;
            walker* retriever = members[best];
            clear_stale_flag_leader(ctf, retriever);
            issue_front_command(retriever, COMMAND_GOTO, 45, own_flag.x,
                                own_flag.y);
        }
    }

    std::vector<walker*> remaining;
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        if (!assigned[i])
            remaining.push_back(members[i]);
    }

    // DEFENDERS: the first ceil(remaining/3) members hold the flag home,
    // leashed back whenever they stray past kCtfDefendRadius.
    const std::size_t defender_count = (remaining.size() + 2) / 3;
    for (std::size_t i = 0; i < defender_count; ++i)
    {
        walker* defender = remaining[i];
        clear_stale_flag_leader(ctf, defender);
        if (!own_flag.present)
            continue;
        if (point_distance(defender, own_flag.home_x, own_flag.home_y) >
            kCtfDefendRadius)
        {
            issue_front_command(defender, COMMAND_GOTO, 45, own_flag.home_x,
                                own_flag.home_y);
        }
    }

    // ATTACKERS and ESCORTS: while the team has a carrier, every 2nd
    // would-be attacker escorts it instead (COMMAND_FOLLOW aborts to fight
    // when a foe appears, so escorts still brawl when engaged). Attackers
    // head for the nearest enemy flag's current position — chasing a flag
    // someone else stole converges on its carrier.
    int attacker_index = 0;
    for (std::size_t i = defender_count; i < remaining.size(); ++i)
    {
        walker* attacker = remaining[i];
        const bool escort_slot =
            lead_carrier != nullptr && (attacker_index % 2) == 1;
        attacker_index++;

        if (escort_slot)
        {
            attacker->set_foe(nullptr);
            attacker->set_leader(lead_carrier);
            issue_front_command(attacker, COMMAND_FOLLOW, 100, 0, 0);
            continue;
        }

        clear_stale_flag_leader(ctf, attacker);
        // Issue the objective when the queue is idle, and also preempt
        // foe-search walks (act_random re-issues a 300-tick COMMAND_SEARCH
        // within a tick of any queue drain, which would otherwise starve
        // attackers into a permanent mid-map brawl) or refresh a stale GOTO
        // (the target flag's current position moves while carried). Active
        // combat commands are left alone: melee on collision, hit_response,
        // and the attack handoff still interrupt the push.
        const statistics* attacker_stats = attacker->stats();
        const std::int32_t front_type = attacker_stats->has_commands()
            ? attacker_stats->commands.front().commandtype
            : 0;
        if (front_type != 0 && front_type != COMMAND_SEARCH &&
            front_type != COMMAND_GOTO)
        {
            continue;
        }

        int target_team = -1;
        int target_distance = 0;
        for (int t = 0; t < kCtfMaxFlags; ++t)
        {
            if (t == team || !ctf.flags[t].present)
                continue;
            // A flag secured by the own team (a teammate carries it) is not
            // an attack target: pressing it would pile the team onto its own
            // carrier and freeze a both-flags-stolen match into a permanent
            // standoff. With every enemy flag secured, attackers stay on the
            // classic chase loop, which drifts them onto the remaining foes
            // — the pressure that retrieves the own flag and unblocks the
            // waiting carrier.
            if (ctf.flags[t].state == CtfFlagState::Carried)
            {
                const walker* flag_carrier =
                    world.find_by_id(ctf.flags[t].carrier_entity_id);
                if (flag_carrier != nullptr &&
                    flag_carrier->team_num() ==
                        static_cast<unsigned char>(team))
                {
                    continue;
                }
            }
            const int distance =
                point_distance(attacker, ctf.flags[t].x, ctf.flags[t].y);
            if (target_team < 0 || distance < target_distance)
            {
                target_team = t;
                target_distance = distance;
            }
        }
        if (target_team >= 0)
        {
            issue_front_command(attacker, COMMAND_GOTO, 60,
                                ctf.flags[target_team].x,
                                ctf.flags[target_team].y);
        }
    }
}

} // namespace

void ctf_run_ai_director(GameWorld& world)
{
    if (!world.ctf.active)
        return;
    for (int team = 0; team < 4; ++team)
    {
        if (world.ctf.team_active[team])
            run_team_director(world, team);
    }
}

} // namespace og::sim
