/* Capture-the-flag core engine.
 *
 * Owns lazy initialization, respawn bookkeeping, the flag state machine,
 * control-point occupancy, and the match win check. Everything here runs
 * only behind the GameWorld::TYPE_CTF gate in GameWorld::tick(), keeps all
 * randomness on world.rng_, and iterates entity lists in list order so the
 * simulation stays deterministic and replicable.
 */

#include <openglad/gameplay/ctf/ctf_state.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <string>

namespace og::sim {

const char* ctf_team_color_name(int team)
{
    switch (team)
    {
        case 0: return "RED";
        case 1: return "GREEN";
        case 2: return "BLUE";
        default: return "YELLOW";
    }
}

namespace {

const char* team_color_name(int team)
{
    return ctf_team_color_name(team);
}

void fire_ai_respawn(GameWorld& world, const CtfRespawnEntry& entry);
void classic_fire_respawn(GameWorld& world, const CtfRespawnEntry& entry);

[[nodiscard]] bool is_score_team(unsigned char team)
{
    return team < SCORE_TEAM_COUNT;
}

void notify(const std::string& message)
{
    if (current_game != nullptr)
        emit_notification(current_game->sim_events, message);
}

void play(std::uint32_t sound_id)
{
    if (current_game != nullptr)
        emit_sound(current_game->sim_events, sound_id);
}

void award_team_score(GameWorld& world, int team, std::uint32_t points)
{
    if (team < 0 || team >= SCORE_TEAM_COUNT)
        return;
    world.m_score[team] += points;
    if (current_game != nullptr)
        emit_event(current_game->sim_events, EventKind::ScoreChange,
                   static_cast<std::uint32_t>(team), points);
}

walker* flag_entity_for(GameWorld& world, const CtfFlag& flag)
{
    return world.find_by_id(flag.flag_entity_id);
}

// setxy() routes obmap updates through obmap::move, which early-outs when the
// position is unchanged. A revived corpse (already removed from the obmap by
// walker::death) placed back on its own tile would silently stay unindexed,
// so re-register explicitly after placement.
void ensure_obmap_registration(GameWorld& world, walker* w)
{
    if (world.myobmap == nullptr || w == nullptr || w->ignore() || w->dead())
        return;
    if (world.myobmap->walker_to_pos.find(w) == world.myobmap->walker_to_pos.end())
        world.myobmap->add(w, w->xpos(), w->ypos());
}

void send_flag_home(GameWorld& world, int team)
{
    CtfFlag& f = world.ctf.flags[team];
    f.state = CtfFlagState::AtHome;
    f.carrier_entity_id = 0;
    f.return_ticks = 0;
    f.x = f.home_x;
    f.y = f.home_y;
    if (walker* fe = flag_entity_for(world, f))
    {
        fe->set_ignore(0);
        fe->setxy(f.home_x, f.home_y);
        ensure_obmap_registration(world, fe);
    }
}

// Passability probe for spawn placement. query_passable routes through
// ob_pass_check, whose treasure overlap dispatches eat_me — a probe must
// never eat a drumstick or pick up a flag at a position the walker is not
// actually placed at. Grid check plus a manual blocking-entity scan instead.
[[nodiscard]] bool spawn_spot_blocked_by(const walker* other, const walker* w,
                                         short x, short y)
{
    if (other == nullptr || other == w || other->dead())
        return false;
    const Order order = other->query_order();
    const bool blocking =
        order == Order::Living || order == Order::Generator ||
        (order == Order::Weapon &&
         (other->family() == FAMILY_DOOR || other->family() == FAMILY_TREE ||
          other->family() == FAMILY_BOULDER));
    if (!blocking)
        return false;
    return x + w->sizex() > other->xpos() &&
           x < other->xpos() + other->sizex() &&
           y + w->sizey() > other->ypos() &&
           y < other->ypos() + other->sizey();
}

[[nodiscard]] bool spawn_spot_clear(GameWorld& world, walker* w, short x, short y)
{
    if (!world.query_grid_passable(x, y, w))
        return false;
    if (world.myobmap == nullptr)
        return true;
    // Walkers register in every 32px obmap bucket their bbox spans; scan all
    // buckets the spawn bbox touches (a boolean check needs no dedup).
    constexpr int kBucket = 32;
    for (int bx = x; bx < x + w->sizex() + kBucket; bx += kBucket)
    {
        for (int by = y; by < y + w->sizey() + kBucket; by += kBucket)
        {
            const std::list<walker*>& pile = world.myobmap->obmap_get_list(
                static_cast<short>(bx), static_cast<short>(by));
            for (walker* other : pile)
            {
                if (spawn_spot_blocked_by(other, w, x, y))
                    return false;
            }
        }
    }
    return true;
}

// Places a walker at the team's next respawn anchor: rotates respawn_serial,
// skips anchors that fail the eat-free probe, falls back to the flag home,
// and finally to teleport() (world.rng_ draw, CTF-gated by construction).
bool place_at_anchor(GameWorld& world, walker* w, int team)
{
    CtfState& ctf = world.ctf;
    short final_x = -1;
    short final_y = -1;

    if (team >= 0 && team < 4)
    {
        const int count = ctf.anchor_count[team];
        for (int attempt = 0; attempt < count; ++attempt)
        {
            const int slot = ctf.respawn_serial % count;
            ctf.respawn_serial++;
            const short ax = ctf.anchor_x[team][slot];
            const short ay = ctf.anchor_y[team][slot];
            if (spawn_spot_clear(world, w, ax, ay))
            {
                final_x = ax;
                final_y = ay;
                break;
            }
        }
        if (final_x < 0 && ctf.flags[team].present &&
            spawn_spot_clear(world, w, ctf.flags[team].home_x,
                             ctf.flags[team].home_y))
        {
            final_x = ctf.flags[team].home_x;
            final_y = ctf.flags[team].home_y;
        }
    }

    if (final_x >= 0)
    {
        w->setxy(final_x, final_y);
        ensure_obmap_registration(world, w);
        return true;
    }
    return w->teleport();
}

// Revives a player corpse in place: same walker, same entity id, full HP,
// cleared targeting/commands. Placement is the caller's business (CTF paths
// rotate to the team's next anchor; classic paths move home when clear). If
// another live walker already holds the same player binding (the player
// switched bodies while dead), the revived walker rejoins the AI pool instead.
void revive_player_walker(GameWorld& world, walker* w, int team)
{
    w->set_dead(0);
    w->set_death_called(0);
    if (w->stats() != nullptr)
    {
        // Tick cleanup nulls a corpse's controller binding; restore it
        // before clear_command(), which dereferences the controller.
        w->stats()->set_controller(w);
        w->stats()->set_hitpoints(w->stats()->max_hitpoints());
        w->stats()->clear_command();
    }
    w->set_foe(nullptr);
    w->set_leader(nullptr);
    // Respawn breaks charm: rejoin the recorded true team, fully uncharmed.
    if (team >= 0 && team < 4)
        w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(255);
    w->set_charm_left(0);

    if (w->user() != -1)
    {
        for (const auto& uptr : world.oblist)
        {
            walker* other = uptr.get();
            if (other != nullptr && other != w && !other->dead() &&
                other->query_order() == Order::Living &&
                other->user() == w->user() && other->act_type() == ACT_CONTROL)
            {
                w->set_user(-1);
                w->set_act_type(ACT_RANDOM);
                break;
            }
        }
    }
}

[[nodiscard]] bool already_scheduled(const CtfState& ctf, std::uint32_t entity_id)
{
    for (const auto& entry : ctf.respawn_queue)
    {
        if (entry.walker_entity_id == entity_id)
            return true;
    }
    return false;
}

// True when a live walker is already bound to the corpse's character
// (a cleric resurrect beat the respawner): the corpse must stay retired.
// Guy ids are only unique per owning player (each client numbers its roster
// from its own counter), so identity is the (id, owner) pair — owner tags
// are kNoOwner for everyone in local play, where bare ids suffice.
[[nodiscard]] bool live_duplicate_exists(GameWorld& world, const walker* corpse)
{
    if (corpse->myguy == nullptr)
        return false;
    for (const auto& uptr : world.oblist)
    {
        walker* other = uptr.get();
        if (other != nullptr && other != corpse && !other->dead() &&
            other->myguy != nullptr &&
            other->myguy->id == corpse->myguy->id &&
            other->myguy->owner_player_index ==
                corpse->myguy->owner_player_index &&
            other->myguy->owner_save_slot == corpse->myguy->owner_save_slot)
        {
            return true;
        }
    }
    return false;
}

// Kills the corpse's fresh bloodstain so a cleric cannot resurrect a copy of
// a walker that the CTF respawner is about to bring back. The fresh
// LIFE_GEM heart goes with it: respawn cycles would otherwise farm score
// into the time-limit tiebreaker.
void scrub_corpse_drops_in(const GameWorld::EntityList& list,
                           const walker* corpse)
{
    for (const auto& uptr : list)
    {
        walker* fx = uptr.get();
        if (fx == nullptr || fx->dead() ||
            fx->query_order() != Order::Treasure ||
            (fx->family() != FAMILY_STAIN && fx->family() != FAMILY_LIFE_GEM) ||
            fx->team_num() != corpse->team_num())
        {
            continue;
        }
        // Compare sprite centers: the LIFE_GEM is center_on'd the corpse
        // (corner deltas reach 3px per axis for 16px families), the stain
        // shares the corpse's corner.
        const int dx = std::abs((fx->xpos() + fx->sizex() / 2) -
                                (corpse->xpos() + corpse->sizex() / 2));
        const int dy = std::abs((fx->ypos() + fx->sizey() / 2) -
                                (corpse->ypos() + corpse->sizey() / 2));
        if (dx + dy <= 8)
            fx->set_dead(1);
    }
}

void scrub_corpse_stain(GameWorld& world, const walker* corpse)
{
    scrub_corpse_drops_in(world.fxlist, corpse);
    scrub_corpse_drops_in(world.oblist, corpse);
}

void schedule_respawn(GameWorld& world, walker* corpse)
{
    CtfState& ctf = world.ctf;
    if (ctf.respawn_queue.size() >= static_cast<std::size_t>(kCtfMaxRespawnEntries))
    {
        const auto victim = std::find_if(
            ctf.respawn_queue.begin(), ctf.respawn_queue.end(),
            [](const CtfRespawnEntry& entry) { return entry.kind == 1; });
        if (victim == ctf.respawn_queue.end())
            return;
        // Fire the evicted bot immediately instead of dropping it: its corpse
        // is already swept, so a plain erase would shrink the roster forever.
        const CtfRespawnEntry evicted = *victim;
        ctf.respawn_queue.erase(victim);
        if (classic_respawn_active(world))
            classic_fire_respawn(world, evicted);
        else
            fire_ai_respawn(world, evicted);
        // A blocked classic AI fire re-enqueues itself: if the queue is full
        // again, drop that just-appended retry rather than the incoming
        // corpse. Either way one AI entry is permanently lost (a non-myguy
        // corpse is swept this same tick and never re-scanned), but the
        // retry's spot is currently occupied while the incoming corpse's may
        // be clear — and the cap must hold for the snapshot serializer.
        if (ctf.respawn_queue.size() >=
            static_cast<std::size_t>(kCtfMaxRespawnEntries))
        {
            ctf.respawn_queue.pop_back();
        }
    }

    CtfRespawnEntry entry;
    entry.kind = (corpse->myguy != nullptr) ? 0 : 1;
    // A charmed corpse still wears the charmer's team_num; respawn breaks the
    // charm and the walker rejoins its true team.
    entry.team = (corpse->real_team_num() != 255) ? corpse->real_team_num()
                                                  : corpse->team_num();
    entry.family = static_cast<std::uint8_t>(corpse->family());
    const std::int32_t level =
        (corpse->stats() != nullptr) ? corpse->stats()->level() : 1;
    entry.level = static_cast<std::uint8_t>(std::clamp<std::int32_t>(level, 1, 255));
    entry.ticks_left = ctf.respawn_ticks;
    entry.walker_entity_id = corpse->entity_id();
    // Classic respawn location: the corpse's recorded spawn point when set,
    // else where it fell. CTF fire paths ignore these (anchor rotation stays
    // authoritative for CTF).
    if (corpse->spawn_x() >= 0 && corpse->spawn_y() >= 0)
    {
        entry.x = corpse->spawn_x();
        entry.y = corpse->spawn_y();
        entry.floor = corpse->spawn_floor();
    }
    else
    {
        entry.x = corpse->xpos();
        entry.y = corpse->ypos();
        entry.floor = static_cast<std::uint8_t>(corpse->floor());
    }
    ctf.respawn_queue.push_back(entry);

    scrub_corpse_stain(world, corpse);
}

void fire_player_respawn(GameWorld& world, const CtfRespawnEntry& entry)
{
    walker* w = world.find_by_id(entry.walker_entity_id);
    if (w == nullptr || !w->dead() || w->query_order() != Order::Living)
        return;
    if (live_duplicate_exists(world, w))
        return;
    revive_player_walker(world, w, entry.team);
    place_at_anchor(world, w, entry.team);
}

void fire_ai_respawn(GameWorld& world, const CtfRespawnEntry& entry)
{
    walker* w = world.add_ob(Order::Living, entry.family);
    if (w == nullptr)
        return;
    w->set_team_num(entry.team);
    w->set_real_team_num(255);
    if (w->stats() != nullptr)
        w->stats()->set_level(entry.level);
    w->set_difficulty(entry.level);
    place_at_anchor(world, w, entry.team);
}

// Phase 1: respawn timers. Owning any control point halves the wait.
void run_respawn_timers(GameWorld& world)
{
    CtfState& ctf = world.ctf;
    if (ctf.respawn_queue.empty())
        return;

    std::array<bool, 4> team_owns_cp = {};
    for (int i = 0; i < ctf.cp_count; ++i)
    {
        const std::int8_t owner = ctf.cps[i].owner;
        if (owner >= 0 && owner < 4)
            team_owns_cp[static_cast<std::size_t>(owner)] = true;
    }

    for (std::size_t i = 0; i < ctf.respawn_queue.size();)
    {
        CtfRespawnEntry& entry = ctf.respawn_queue[i];
        const std::uint16_t dec =
            (entry.team < 4 && team_owns_cp[static_cast<std::size_t>(entry.team)]) ? 2 : 1;
        entry.ticks_left -= std::min<std::uint16_t>(dec, entry.ticks_left);
        if (entry.ticks_left > 0)
        {
            ++i;
            continue;
        }
        const CtfRespawnEntry fired = entry;
        ctf.respawn_queue.erase(ctf.respawn_queue.begin() +
                                static_cast<std::ptrdiff_t>(i));
        if (classic_respawn_active(world))
            classic_fire_respawn(world, fired);
        else if (fired.kind == 0)
            fire_player_respawn(world, fired);
        else
            fire_ai_respawn(world, fired);
    }
}

// Phase 2: death scan. Runs before the dead sweep, so corpses that died this
// tick are still in oblist. Player corpses (myguy) persist across ticks and
// are deduplicated by entity id; AI corpses are seen exactly once. Walkers
// with an owner (summons, generator spawns) are the generator's business.
void run_death_scan(GameWorld& world)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || !w->dead() || w->query_order() != Order::Living)
            continue;
        const unsigned char team = w->team_num();
        if (!is_score_team(team) || !world.ctf.team_active[team])
            continue;
        if (w->myguy == nullptr && w->owner() != nullptr)
            continue;
        if (already_scheduled(world.ctf, w->entity_id()))
            continue;
        if (live_duplicate_exists(world, w))
            continue;
        schedule_respawn(world, w);
    }
}

// Drops a flag at an explicit point: the carrier's feet for the classic
// drops, the departure point for the teleport rule. `carrier` (non-null)
// supplies the footprint for the terrain probe.
void drop_flag_at(GameWorld& world, int team, walker* carrier,
                  short drop_x, short drop_y)
{
    CtfFlag& f = world.ctf.flags[team];

    // Stranding rule: a drop tile that fails the terrain probe (water, wall)
    // returns the flag home instantly instead of dropping it out of reach.
    if (!world.query_grid_passable(drop_x, drop_y, carrier))
    {
        send_flag_home(world, team);
        notify(std::format("{} FLAG RETURNED!", team_color_name(team)));
        play(SOUND_TELEPORT);
        return;
    }

    f.state = CtfFlagState::Dropped;
    f.carrier_entity_id = 0;
    f.x = drop_x;
    f.y = drop_y;
    f.return_ticks = world.ctf.flag_return_ticks;
    if (walker* fe = flag_entity_for(world, f))
    {
        fe->set_ignore(0);
        fe->setxy(drop_x, drop_y);
        ensure_obmap_registration(world, fe);
    }
    notify(std::format("{} FLAG DROPPED!", team_color_name(team)));
    play(SOUND_YO);
}

void drop_flag(GameWorld& world, int team, walker* carrier)
{
    // No corpse to validate a drop tile against: send the flag home.
    if (carrier == nullptr)
    {
        send_flag_home(world, team);
        notify(std::format("{} FLAG RETURNED!", team_color_name(team)));
        play(SOUND_TELEPORT);
        return;
    }
    drop_flag_at(world, team, carrier, carrier->xpos(), carrier->ypos());
}

// Phase 3: carried flags ride their carrier; invalid carriers (gone, dead,
// or charm-flipped onto the flag's own team) drop the flag. A carrier that
// SELF-teleported this tick — walker::teleport / teleport_ranged stamp the
// carrier with the tick the blink began, covering mage/archmage/skeleton
// blinks and marker beacons — leaves every carried flag behind at the
// departure point (Unreal Tournament semantics): the carried flag's
// replicated (x, y) still holds the carrier's pre-blink position. Map
// teleporter pads never stamp the marker, so pad rides carry the flag
// through with no special-casing. Each carried flag is evaluated
// independently, so one blink drops every flag a multi-carrier holds.
void run_carried_flags(GameWorld& world)
{
    for (int t = 0; t < kCtfMaxFlags; ++t)
    {
        CtfFlag& f = world.ctf.flags[t];
        if (!f.present || f.state != CtfFlagState::Carried)
            continue;
        walker* carrier = world.find_by_id(f.carrier_entity_id);
        if (carrier == nullptr || carrier->dead() ||
            carrier->team_num() == static_cast<unsigned char>(t))
        {
            drop_flag(world, t, carrier);
            continue;
        }
        if (carrier->last_self_teleport_tick() == world.tick_count_)
        {
            drop_flag_at(world, t, carrier, f.x, f.y);
            continue;
        }
        f.x = carrier->xpos();
        f.y = carrier->ypos();
        if (walker* fe = flag_entity_for(world, f))
            fe->setxy(carrier->xpos(), static_cast<short>(carrier->ypos() - 8));
    }
}

// Phase 4: dropped flags count down and auto-return home.
void run_dropped_flags(GameWorld& world)
{
    for (int t = 0; t < kCtfMaxFlags; ++t)
    {
        CtfFlag& f = world.ctf.flags[t];
        if (!f.present || f.state != CtfFlagState::Dropped)
            continue;
        if (f.return_ticks > 0)
            --f.return_ticks;
        if (f.return_ticks == 0)
        {
            send_flag_home(world, t);
            notify(std::format("{} FLAG RETURNED!", team_color_name(t)));
            play(SOUND_TELEPORT);
        }
    }
}

// Phase 5: control points. Majority occupancy accumulates progress for the
// dominant team, owner dominance decays it symmetrically, and owned points
// pulse a localized speed bonus to nearby owner-team livings.
void run_control_points(GameWorld& world)
{
    CtfState& ctf = world.ctf;
    for (int i = 0; i < ctf.cp_count; ++i)
    {
        CtfControlPoint& cp = ctf.cps[i];
        const std::int32_t radius = cp.radius_tiles * GRID_SIZE;

        std::array<int, 4> present = {};
        for (const auto& uptr : world.oblist)
        {
            walker* w = uptr.get();
            if (w == nullptr || w->dead() || w->query_order() != Order::Living)
                continue;
            const unsigned char team = w->team_num();
            if (!is_score_team(team) || !ctf.team_active[team])
                continue;
            const std::int32_t dx = w->xpos() - cp.x;
            const std::int32_t dy = w->ypos() - cp.y;
            if (dx * dx + dy * dy <= radius * radius)
                present[static_cast<std::size_t>(team)]++;
        }

        // Majority-occupancy contender: the strongest team contests the
        // point only with a strict majority of ALL live walkers in the
        // disc. Reduces to sole occupancy when one team is present; exact
        // ties (1v1, 2v2) and balanced multi-team brawls hold the meter,
        // so a single passing enemy no longer freezes a 2-strong retake.
        int strongest = -1;
        int strongest_count = 0;
        int total_present = 0;
        for (int t = 0; t < 4; ++t)
        {
            total_present += present[static_cast<std::size_t>(t)];
            if (present[static_cast<std::size_t>(t)] > strongest_count)
            {
                strongest_count = present[static_cast<std::size_t>(t)];
                strongest = t;
            }
        }
        const int contender =
            (strongest >= 0 && strongest_count * 2 > total_present)
                ? strongest
                : -1;

        if (contender >= 0 && contender != cp.owner)
        {
            if (cp.progress_team != contender)
            {
                cp.progress_team = static_cast<std::int8_t>(contender);
                cp.progress = 0;
            }
            cp.progress++;
            if (cp.progress >= kCtfCpCaptureTicks)
            {
                cp.owner = static_cast<std::int8_t>(contender);
                cp.progress = 0;
                cp.progress_team = -1;
                if (walker* ce = world.find_by_id(cp.entity_id))
                    ce->set_team_num(static_cast<unsigned char>(contender));
                award_team_score(world, contender, kCtfCpCaptureScore);
                notify(std::format("{} TAKES WAYPOINT!",
                                   team_color_name(contender)));
                play(SOUND_MONEY);
            }
        }
        else if (contender >= 0 && contender == cp.owner)
        {
            // Owner dominance decays accrued progress one step per tick
            // (symmetric with accrual) instead of wiping it: a lone owner
            // transit no longer zeroes a near-complete retake, but the
            // owner can still fully cleanse the meter by holding the pad
            // as long as the attacker did. The meter only clears its
            // contending team when it drains to zero.
            if (cp.progress > 0 && --cp.progress == 0)
                cp.progress_team = -1;
        }
        // Contested (no strict majority) or empty: hold the meter.

        if (cp.owner >= 0 && world.tick_count_ >= cp.next_pulse_tick)
        {
            for (const auto& uptr : world.oblist)
            {
                walker* w = uptr.get();
                if (w == nullptr || w->dead() ||
                    w->query_order() != Order::Living ||
                    w->team_num() != static_cast<unsigned char>(cp.owner))
                {
                    continue;
                }
                const std::int32_t dx = w->xpos() - cp.x;
                const std::int32_t dy = w->ypos() - cp.y;
                if (dx * dx + dy * dy <=
                    kCtfCpPulseRadius * kCtfCpPulseRadius)
                {
                    w->set_speed_bonus(1.0f);
                    w->set_speed_bonus_left(60);
                }
            }
            cp.next_pulse_tick = world.tick_count_ + kCtfCpPulsePeriod;
        }
    }
}

// Phase 7: win check. Capture limit first; the time limit picks the leader
// (captures, then m_score, then smaller team index). All pending player
// corpses revive before game_ended so roster merging never sees permadeath.
void run_win_check(GameWorld& world)
{
    CtfState& ctf = world.ctf;
    int winner = -1;
    for (int t = 0; t < 4 && winner < 0; ++t)
    {
        if (ctf.team_active[t] && ctf.captures[t] >= ctf.capture_limit)
            winner = t;
    }
    if (winner < 0 && ctf.time_limit_ticks != 0 &&
        world.level_tick_count() >= ctf.time_limit_ticks)
    {
        for (int t = 0; t < 4; ++t)
        {
            if (!ctf.team_active[t])
                continue;
            if (winner < 0 ||
                ctf.captures[t] > ctf.captures[winner] ||
                (ctf.captures[t] == ctf.captures[winner] &&
                 world.m_score[t] > world.m_score[winner]))
            {
                winner = t;
            }
        }
    }
    if (winner < 0)
        return;

    ctf.winner_team = static_cast<std::int8_t>(winner);
    // Color name, not a bare 1-indexed team number: players see team tints,
    // never indices. Worst case "YELLOW TEAM WINS!" is 17 chars, inside the
    // 25-char notification budget.
    notify(std::format("{} TEAM WINS!", team_color_name(winner)));
    play(SOUND_CHARGE);

    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && w->dead() && w->query_order() == Order::Living &&
            w->myguy != nullptr && !live_duplicate_exists(world, w))
        {
            // A corpse that died charmed still wears the charmer's team;
            // revive on the recorded true team like the queue path does.
            const unsigned char raw_team = (w->real_team_num() != 255)
                ? w->real_team_num()
                : w->team_num();
            const int team = is_score_team(raw_team) ? raw_team : 0;
            revive_player_walker(world, w, team);
            place_at_anchor(world, w, team);
        }
    }
    ctf.respawn_queue.clear();

    bool winner_is_player = false;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            w->team_num() == static_cast<unsigned char>(winner) &&
            w->myguy != nullptr)
        {
            winner_is_player = true;
            break;
        }
    }
    ctf.winner_is_player = winner_is_player;

    world.game_ended = true;
    world.ending = 0;
    world.next_level =
        static_cast<short>(winner_is_player ? world.id + 1 : world.id);
}

void spawn_bot_squad(GameWorld& world, int team)
{
    static constexpr std::array<int, 5> kSquad = {FAMILY_SOLDIER, FAMILY_ARCHER,
                                                  FAMILY_ELF, FAMILY_MAGE, FAMILY_THIEF};
    const std::int32_t level = std::max(1, world.difficulty / 100 + 1);
    for (int member : kSquad)
    {
        walker* w = world.add_ob(Order::Living, member);
        if (w == nullptr)
            continue;
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        if (w->stats() != nullptr)
            w->stats()->set_level(level);
        w->set_difficulty(static_cast<std::uint32_t>(level));
        place_at_anchor(world, w, team);
    }
}

} // namespace

void ctf_initialize_for_level(GameWorld& world)
{
    world.ctf = CtfState{};
    CtfState& ctf = world.ctf;
    ctf.init_attempted = true;

    // Scan authored flags and control points (fxlist, list order). The first
    // flag with a stats level above 1 sets the per-map capture limit (level 1
    // is the default for every fresh entity, so it means "use defaults").
    std::int32_t map_capture_limit = 0;
    for (const auto& uptr : world.fxlist)
    {
        walker* fx = uptr.get();
        if (fx == nullptr || fx->dead() || fx->query_order() != Order::Treasure)
            continue;
        if (fx->family() == og::FAMILY_FLAG)
        {
            const unsigned char team = fx->team_num();
            if (!is_score_team(team))
            {
                fx->set_dead(1);
                continue;
            }
            CtfFlag& f = ctf.flags[team];
            if (f.present)
            {
                fx->set_dead(1); // one flag per team
                continue;
            }
            f.present = true;
            f.home_x = fx->xpos();
            f.home_y = fx->ypos();
            f.x = f.home_x;
            f.y = f.home_y;
            f.flag_entity_id = fx->entity_id();
            if (map_capture_limit == 0 && fx->stats() != nullptr &&
                fx->stats()->level() > 1)
            {
                map_capture_limit = fx->stats()->level();
            }
        }
        else if (fx->family() == og::FAMILY_CTF_POINT)
        {
            if (ctf.cp_count >= kCtfMaxControlPoints)
            {
                fx->set_dead(1);
                continue;
            }
            CtfControlPoint& cp = ctf.cps[ctf.cp_count++];
            cp.x = fx->xpos();
            cp.y = fx->ypos();
            cp.entity_id = fx->entity_id();
        }
    }

    // Respawn anchors: team start markers, dead ones included (the level
    // bootstrap kills the markers it consumes for player placement).
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->query_order() != Order::Special ||
            w->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        const unsigned char team = w->team_num();
        if (!is_score_team(team) || ctf.anchor_count[team] >= kCtfMaxAnchorsPerTeam)
            continue;
        ctf.anchor_x[team][ctf.anchor_count[team]] = w->xpos();
        ctf.anchor_y[team][ctf.anchor_count[team]] = w->ypos();
        ctf.anchor_count[team]++;
    }

    // Active teams: the requested count (0 = use everything the map authors)
    // intersected with authored flag teams, in team index order.
    const int requested = world.ctf_requested_team_count;
    const int max_active =
        (requested <= 0) ? 4 : std::clamp(requested, 2, 4);
    int active_count = 0;
    for (int t = 0; t < 4; ++t)
    {
        if (ctf.flags[t].present && active_count < max_active)
        {
            ctf.team_active[t] = true;
            active_count++;
        }
    }

    // Fewer than two flag teams: not a playable CTF map. Leave the world
    // untouched and stay inactive (init_attempted latches the decision).
    if (active_count < 2)
        return;

    // SAVE_ALL's named-NPC death check fires straight from walker::death and
    // bypasses the CTF team-wipe suppression; respawns make it meaningless
    // here, so the bit is dropped on activation.
    world.type &= static_cast<char>(~GameWorld::TYPE_MUST_PROTECT_NAMED_NPCS);

    // Consume the active teams' start markers (their positions are recorded
    // above) so the marker entities cannot block their own respawn anchors.
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Special ||
            w->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        const unsigned char team = w->team_num();
        if (is_score_team(team) && ctf.team_active[team])
            w->set_dead(1);
    }

    // Strip inactive-team entities: surplus flags, livings, and generators.
    // Player roster walkers (myguy) are never stripped.
    for (int t = 0; t < 4; ++t)
    {
        if (ctf.team_active[t] || !ctf.flags[t].present)
            continue;
        if (walker* fe = flag_entity_for(world, ctf.flags[t]))
            fe->set_dead(1);
        ctf.flags[t] = CtfFlag{};
        ctf.anchor_count[t] = 0;
    }
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->dead())
            continue;
        const Order order = w->query_order();
        if (order != Order::Living && order != Order::Generator &&
            !(order == Order::Special && w->family() == FAMILY_RESERVED_TEAM))
        {
            continue;
        }
        const unsigned char team = w->team_num();
        if (is_score_team(team) && ctf.team_active[team])
            continue;
        if (w->myguy != nullptr)
            continue;
        w->set_dead(1);
    }

    // Roster-only armies on request: strip the authored livings and
    // generators from every active team that fields a roster (myguy) walker.
    // Teams without a roster keep their authored troops (they are the
    // opposition; empty ones still get bot squads below), and roster walkers
    // themselves are never stripped.
    if (world.ctf_requested_strip_scenario_troops != 0)
    {
        std::array<bool, 4> team_has_myguy = {};
        for (const auto& uptr : world.oblist)
        {
            walker* w = uptr.get();
            if (w == nullptr || w->dead() ||
                w->query_order() != Order::Living || w->myguy == nullptr)
            {
                continue;
            }
            const unsigned char team = w->team_num();
            if (is_score_team(team))
                team_has_myguy[static_cast<std::size_t>(team)] = true;
        }
        for (const auto& uptr : world.oblist)
        {
            walker* w = uptr.get();
            if (w == nullptr || w->dead())
                continue;
            const Order order = w->query_order();
            if (order != Order::Living && order != Order::Generator)
                continue;
            if (w->myguy != nullptr)
                continue;
            const unsigned char team = w->team_num();
            if (!is_score_team(team) || !ctf.team_active[team] ||
                !team_has_myguy[static_cast<std::size_t>(team)])
            {
                continue;
            }
            // Move the corpse off the score-team range first: run_death_scan
            // (same tick) only schedules respawns for score-team livings, and
            // an init-stripped troop must NOT come back as a bot respawn.
            // Deterministic, replicated via entity snapshots.
            w->set_team_num(SCORE_TEAM_COUNT);
            w->set_dead(1);
        }
    }

    // Resolve config: explicit request > per-map value > defaults.
    const short requested_limit = world.ctf_requested_capture_limit;
    std::int32_t limit = kCtfDefaultCaptureLimit;
    if (requested_limit > 0)
        limit = requested_limit;
    else if (map_capture_limit > 0)
        limit = map_capture_limit;
    ctf.capture_limit =
        static_cast<std::uint8_t>(std::clamp<std::int32_t>(limit, 1, 255));

    const short requested_respawn = world.ctf_requested_respawn_ticks;
    ctf.respawn_ticks = (requested_respawn > 0)
        ? static_cast<std::uint16_t>(requested_respawn)
        : kCtfDefaultRespawnTicks;
    ctf.flag_return_ticks = kCtfDefaultFlagReturnTicks;
    ctf.time_limit_ticks = kCtfDefaultTimeLimitTicks;
    ctf.team_count = static_cast<std::uint8_t>(active_count);

    // Bot squads for active teams that field no livings.
    std::array<int, 4> living_per_team = {};
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            is_score_team(w->team_num()))
        {
            living_per_team[static_cast<std::size_t>(w->team_num())]++;
        }
    }
    for (int t = 0; t < 4; ++t)
    {
        if (ctf.team_active[t] && living_per_team[static_cast<std::size_t>(t)] == 0)
            spawn_bot_squad(world, t);
    }

    ctf.active = true;
    notify(std::format("CAPTURE THE FLAG! TO {}",
                       static_cast<int>(ctf.capture_limit)));
    play(SOUND_CHARGE);
}

void ctf_authored_flag_teams(const GameWorld& world, bool (&present)[4])
{
    for (bool& team_present : present)
        team_present = false;
    for (const auto& uptr : world.fxlist)
    {
        walker* fx = uptr.get();
        if (fx == nullptr || fx->dead() ||
            fx->query_order() != Order::Treasure ||
            fx->family() != og::FAMILY_FLAG)
        {
            continue;
        }
        const unsigned char team = fx->team_num();
        if (is_score_team(team))
            present[team] = true;
    }
}

std::uint8_t ctf_authored_flag_team_mask(const GameWorld& world) noexcept
{
    bool present[SCORE_TEAM_COUNT] = {};
    ctf_authored_flag_teams(world, present);
    std::uint8_t mask = 0;
    for (int team = 0; team < SCORE_TEAM_COUNT; ++team)
    {
        if (present[team])
        {
            mask = static_cast<std::uint8_t>(
                mask | (1u << static_cast<unsigned>(team)));
        }
    }
    return mask;
}

void ctf_run_tick(GameWorld& world)
{
    CtfState& ctf = world.ctf;
    if (!ctf.active && !ctf.init_attempted)
        ctf_initialize_for_level(world);
    if (!ctf.active)
        return;

    // Match already decided: keep the end-state latched without re-running
    // phases (a session layer may step the world again after game_ended).
    // tick() resets game_ended/ending/next_level at entry, so the full win
    // shape must be re-asserted or the win degrades to a terminal end.
    if (ctf.winner_team >= 0)
    {
        world.game_ended = true;
        world.ending = 0;
        world.next_level = static_cast<short>(
            ctf.winner_is_player ? world.id + 1 : world.id);
        return;
    }

    run_respawn_timers(world);
    run_death_scan(world);
    run_carried_flags(world);
    run_dropped_flags(world);
    run_control_points(world);
    if (world.tick_count_ % kCtfAiCadenceTicks == 0)
        ctf_run_ai_director(world);
    run_win_check(world);
}

bool ctf_on_flag_touch(walker* flag, walker* eater)
{
    if (current_game == nullptr || current_game->world == nullptr)
        return true;
    GameWorld& world = *current_game->world;
    if (!(world.type & GameWorld::TYPE_CTF) || !world.ctf.active)
        return true;
    if (flag == nullptr || flag->dead())
        return true;
    if (eater == nullptr || eater->dead() ||
        eater->query_order() != Order::Living)
    {
        return true;
    }
    // A flag touch fired while the eater is mid-self-teleport is not a real
    // touch: walker::teleport / teleport_ranged probe their destination for
    // passability before moving, and the obmap probe fires eat_me on
    // whatever overlaps the target spot. Honoring such an eat would bank a
    // capture or relocate a flag across the blink while the eater still
    // stands at the departure point, so the touch is ignored outright; the
    // walker can touch the flag for real by moving on a later tick.
    if (eater->last_self_teleport_tick() == world.tick_count_)
        return true;
    const unsigned char eater_team = eater->team_num();
    if (!is_score_team(eater_team) || !world.ctf.team_active[eater_team])
        return true;

    int flag_team = -1;
    for (int t = 0; t < kCtfMaxFlags; ++t)
    {
        if (world.ctf.flags[t].present &&
            world.ctf.flags[t].flag_entity_id == flag->entity_id())
        {
            flag_team = t;
            break;
        }
    }
    if (flag_team < 0)
        return true;
    CtfFlag& f = world.ctf.flags[flag_team];

    if (eater_team == static_cast<unsigned char>(flag_team))
    {
        if (f.state == CtfFlagState::Dropped)
        {
            send_flag_home(world, flag_team);
            notify(std::format("{} FLAG RETURNED!", team_color_name(flag_team)));
            play(SOUND_TELEPORT);
        }
        else if (f.state == CtfFlagState::AtHome)
        {
            // Touching the own flag at home banks every carried enemy flag.
            int captured = 0;
            for (int t = 0; t < kCtfMaxFlags; ++t)
            {
                if (t == flag_team)
                    continue;
                CtfFlag& enemy_flag = world.ctf.flags[t];
                if (!enemy_flag.present ||
                    enemy_flag.state != CtfFlagState::Carried ||
                    enemy_flag.carrier_entity_id != eater->entity_id())
                {
                    continue;
                }
                send_flag_home(world, t);
                captured++;
            }
            if (captured > 0)
            {
                world.ctf.captures[flag_team] =
                    static_cast<std::uint16_t>(world.ctf.captures[flag_team] +
                                               captured);
                award_team_score(world, flag_team,
                                 static_cast<std::uint32_t>(captured) *
                                     kCtfCaptureScore);
                notify(std::format("TEAM {} SCORES! {}/{}", flag_team + 1,
                                   world.ctf.captures[flag_team],
                                   static_cast<int>(world.ctf.capture_limit)));
                play(SOUND_MONEY);
            }
        }
    }
    else if (f.state == CtfFlagState::AtHome ||
             f.state == CtfFlagState::Dropped)
    {
        // Announce only when the flag leaves home: regrab scrums over a
        // dropped flag would otherwise ping-pong TAKEN lines every touch.
        const bool taken_from_home = (f.state == CtfFlagState::AtHome);
        f.state = CtfFlagState::Carried;
        f.carrier_entity_id = eater->entity_id();
        f.return_ticks = 0;
        f.x = eater->xpos();
        f.y = eater->ypos();
        flag->set_ignore(1);
        flag->setxy(eater->xpos(), static_cast<short>(eater->ypos() - 8));
        if (taken_from_home)
        {
            notify(std::format("{} FLAG TAKEN!", team_color_name(flag_team)));
            play(SOUND_YO);
        }
    }
    return true;
}

bool ctf_on_point_touch(walker* point, walker* eater)
{
    // Control points are occupancy-driven (phase 5); touching is a no-op.
    (void)point;
    (void)eater;
    return true;
}

bool ctf_suppress_team_wipe_endgame(const GameWorld& world)
{
    return (world.type & GameWorld::TYPE_CTF) != 0 && world.ctf.active &&
           world.ctf.winner_team < 0;
}

// ===========================================================================
// Classic (non-CTF) respawn engine. Reuses the CtfState respawn substate and
// the file-static helpers above, but fires with classic placement rules
// (recorded spawn points instead of anchor rotation) and NEVER draws
// world.rng_, so respawn_mode == 0 stays byte-identical (og_test_parity).
// ===========================================================================

bool classic_respawn_active(const GameWorld& world)
{
    return (world.type & GameWorld::TYPE_CTF) == 0 && world.respawn_mode > 0;
}

bool respawn_suppress_team_wipe_endgame(const GameWorld& world)
{
    return ctf_suppress_team_wipe_endgame(world) ||
           classic_respawn_active(world);
}

namespace {

// Floor-explicit spawn probe: classic respawn targets can sit on any floor,
// while spawn_spot_clear probes the walker's own grid floor and the floor-0
// obmap buckets (fine for CTF anchors, which carry no floor channel).
[[nodiscard]] bool classic_spot_clear(GameWorld& world, walker* w,
                                      short x, short y, int floor)
{
    if (!world.query_grid_passable(x, y, w, floor))
        return false;
    if (world.myobmap == nullptr)
        return true;
    // Same bucket sweep as spawn_spot_clear, keyed to the target floor.
    constexpr int kBucket = 32;
    for (int bx = x; bx < x + w->sizex() + kBucket; bx += kBucket)
    {
        for (int by = y; by < y + w->sizey() + kBucket; by += kBucket)
        {
            const std::list<walker*>& pile = world.myobmap->obmap_get_list(
                static_cast<short>(bx), static_cast<short>(by), floor);
            for (walker* other : pile)
            {
                if (spawn_spot_blocked_by(other, w, x, y))
                    return false;
            }
        }
    }
    return true;
}

// Classic player fire: revive the corpse in place (its position is already
// legal), then move it home when its own recorded spawn point is clear. No
// teleport() fallback — the classic engine never draws world.rng_.
void classic_fire_player_respawn(GameWorld& world, const CtfRespawnEntry& entry)
{
    walker* w = world.find_by_id(entry.walker_entity_id);
    if (w == nullptr || !w->dead() || w->query_order() != Order::Living)
        return;
    if (live_duplicate_exists(world, w))
        return;
    revive_player_walker(world, w, entry.team);
    if (w->spawn_x() >= 0 && w->spawn_y() >= 0 &&
        classic_spot_clear(world, w, w->spawn_x(), w->spawn_y(),
                           static_cast<int>(w->spawn_floor())))
    {
        // set_floor BEFORE setxy: the obmap is floor-keyed.
        w->set_floor(static_cast<short>(w->spawn_floor()));
        w->setxy(w->spawn_x(), w->spawn_y());
    }
    ensure_obmap_registration(world, w);
}

// Classic AI fire (mode 2): a fresh walker per the fire_ai_respawn recipe,
// placed at the entry's recorded coordinates. A blocked spot withdraws the
// probe walker and re-enqueues the entry on a short deterministic retry
// cadence instead of teleporting (no world.rng_ draw).
void classic_fire_ai_respawn(GameWorld& world, const CtfRespawnEntry& entry)
{
    walker* w = world.add_ob(Order::Living, entry.family);
    if (w == nullptr)
        return;
    w->set_team_num(entry.team);
    w->set_real_team_num(255);
    if (w->stats() != nullptr)
        w->stats()->set_level(entry.level);
    w->set_difficulty(entry.level);
    if (entry.x >= 0 && entry.y >= 0 &&
        classic_spot_clear(world, w, entry.x, entry.y,
                           static_cast<int>(entry.floor)))
    {
        w->set_floor(static_cast<short>(entry.floor));
        w->setxy(entry.x, entry.y);
        ensure_obmap_registration(world, w);
        // Keep the placement durable: the replacement inherits the spawn
        // point, so its own later deaths respawn at the authored spot.
        w->set_spawn_point(entry.x, entry.y, entry.floor);
        return;
    }
    world.remove_ob(w);
    if (entry.x >= 0 && entry.y >= 0 &&
        world.ctf.respawn_queue.size() <
            static_cast<std::size_t>(kCtfMaxRespawnEntries))
    {
        CtfRespawnEntry retry = entry;
        retry.ticks_left = kClassicBlockedRetryTicks;
        world.ctf.respawn_queue.push_back(retry);
    }
}

void classic_fire_respawn(GameWorld& world, const CtfRespawnEntry& entry)
{
    if (entry.kind == 0)
        classic_fire_player_respawn(world, entry);
    else
        classic_fire_ai_respawn(world, entry);
}

// Classic scheduling eligibility: every dead hero (myguy) for mode >= 1,
// plus unowned AI livings WITH a recorded (level-authored) spawn point for
// mode 2. No team_active / score-team filters: classic levels field
// arbitrary teams. Generator spawns and summons stay the owner's business
// even after the owner dies — clear_stale_cross_refs nulls owner() the tick
// the owner dies, so the owner check alone would adopt orphans as permanent
// respawners; the spawn_x() gate keeps "endless battle" scoped to walkers
// with an authored placement (classic AI respawn copies inherit theirs).
[[nodiscard]] bool classic_respawn_corpse_eligible(GameWorld& world, walker* w)
{
    if (w == nullptr || !w->dead() || w->query_order() != Order::Living)
        return false;
    const bool eligible =
        w->myguy != nullptr ||
        (world.respawn_mode >= 2 && w->owner() == nullptr &&
         w->spawn_x() >= 0);
    if (!eligible)
        return false;
    if (already_scheduled(world.ctf, w->entity_id()))
        return false;
    if (live_duplicate_exists(world, w))
        return false;
    return true;
}

// Classic death scan: the same pre-sweep corpse scan as run_death_scan, with
// the classic eligibility above.
void classic_run_death_scan(GameWorld& world)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (!classic_respawn_corpse_eligible(world, w))
            continue;
        schedule_respawn(world, w);
    }
}

// The classic-effective team of a corpse / queue entry (schedule_respawn's
// charm-break rule), tested with the same strict team-color rule used by
// targeting, damage, and level completion.
[[nodiscard]] bool classic_team_hostile(const GameWorld& world,
                                        std::uint8_t team)
{
    return static_cast<short>(team) != world.my_team;
}

} // namespace

void classic_respawn_run_tick(GameWorld& world)
{
    if (!classic_respawn_active(world))
        return;

    CtfState& ctf = world.ctf;
    // Lazy config resolution. Idempotent, so re-running it every active tick
    // is safe: nothing else writes respawn_ticks on a non-CTF level, and a
    // snapshot restore rewrites this and the requested knob coherently. The
    // rest of CtfState (anchors, flags, CPs, team_active, init_attempted,
    // active) is deliberately untouched — every CTF-only consumer keys on
    // ctf.active staying false here.
    const short requested = world.ctf_requested_respawn_ticks;
    ctf.respawn_ticks =
        (requested >= kClassicMinRespawnTicks &&
         requested <= kClassicMaxRespawnTicks)
            ? static_cast<std::uint16_t>(requested)
            : kCtfDefaultRespawnTicks;

    // End of level (decided this same tick): revive every pending player
    // corpse immediately so roster persistence never sees a mid-respawn hero
    // — mirrors run_win_check's revive-all. Pending AI entries are dropped
    // with the cleared queue (the level is over).
    if (world.game_ended)
    {
        classic_respawn_flush_pending(world);
        return;
    }

    // Timers first, then the death scan, matching ctf_run_tick's phase order:
    // a fresh corpse always waits its full delay.
    run_respawn_timers(world);
    classic_run_death_scan(world);
}

void classic_respawn_flush_pending(GameWorld& world)
{
    if (!classic_respawn_active(world))
        return;
    CtfState& ctf = world.ctf;
    // Scan first, matching ctf_run_tick's death-scan-before-win-check order:
    // a hero dying on the very tick the level ends (a mutual kill with the
    // last foe, or a death the synchronous exit-accept path hasn't scanned
    // yet) gets its entry here and revives below. Timer values don't matter.
    classic_run_death_scan(world);
    // The player fire path never mutates the queue; iterate then clear.
    for (std::size_t i = 0; i < ctf.respawn_queue.size(); ++i)
    {
        const CtfRespawnEntry entry = ctf.respawn_queue[i];
        if (entry.kind == 0)
            classic_fire_player_respawn(world, entry);
    }
    ctf.respawn_queue.clear();
}

bool classic_respawn_pending_hostile_foe(GameWorld& world)
{
    if (!classic_respawn_active(world))
        return false;
    // Entries scheduled on earlier ticks. Kind-1 (AI) entries only exist in
    // mode 2; kind-0 covers hostile heroes (other players' teams) too.
    for (const CtfRespawnEntry& entry : world.ctf.respawn_queue)
    {
        if (entry.kind == 1 && world.respawn_mode >= 2 &&
            classic_team_hostile(world, entry.team))
            return true;
        if (entry.kind == 0 && classic_team_hostile(world, entry.team))
            return true;
    }
    // Corpses that died this very tick: the death scan runs AFTER the
    // completion decision, so the last foe's corpse has no entry yet.
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (!classic_respawn_corpse_eligible(world, w))
            continue;
        const std::uint8_t team = (w->real_team_num() != 255)
                                      ? w->real_team_num()
                                      : w->team_num();
        if (classic_team_hostile(world, team))
            return true;
    }
    return false;
}

} // namespace og::sim
