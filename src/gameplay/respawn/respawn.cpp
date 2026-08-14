/* Respawn engine.
 *
 * The queue/timer/anchor machinery shared by the classic difficulty-submenu
 * respawn modes and the scripted-mode (TYPE_SCRIPTED) Lua respawn surface.
 * Extracted from the retired CTF engine (src/gameplay/ctf/ctf.cpp): the
 * flag/control-point/win machinery moved to campaign-pack Lua, the RNG-free
 * placement and revive paths stayed here. Nothing in this file draws
 * world.rng_, and entity lists iterate in list order, so the simulation
 * stays deterministic and replicable.
 */

#include <openglad/gameplay/respawn/respawn_state.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>

namespace og::sim {

namespace {

walker* classic_fire_respawn(GameWorld& world, const RespawnEntry& entry);

[[nodiscard]] bool is_score_team(unsigned char team)
{
    return team < SCORE_TEAM_COUNT;
}

// setxy() routes obmap updates through obmap::move, which early-outs when the
// position is unchanged. A revived corpse (already removed from the obmap by
// walker::death) placed back on its own tile would silently stay unindexed,
// so re-register explicitly after placement.
void ensure_obmap_registration(GameWorld& world, walker* w)
{
    // dormant(): a delayed spawn holds the "never in the obmap" invariant
    // until its wake tick — no respawn/placement path may re-register one.
    // No current caller passes a dormant walker, so this is pure invariant
    // enforcement (byte-identical for every existing path).
    if (world.myobmap == nullptr || w == nullptr || w->ignore() || w->dead() ||
        w->dormant())
        return;
    if (world.myobmap->walker_to_pos.find(w) == world.myobmap->walker_to_pos.end())
        world.myobmap->add(w, w->xpos(), w->ypos());
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

[[nodiscard]] bool already_scheduled(const RespawnState& respawn,
                                     std::uint32_t entity_id)
{
    for (const auto& entry : respawn.respawn_queue)
    {
        if (entry.walker_entity_id == entity_id)
            return true;
    }
    return false;
}

[[nodiscard]] bool same_guy_identity(const guy* lhs, const guy* rhs)
{
    return lhs != nullptr && rhs != nullptr && lhs->id == rhs->id &&
        lhs->owner_player_index == rhs->owner_player_index &&
        lhs->owner_save_slot == rhs->owner_save_slot;
}

// True when a live walker is already bound to the corpse's character
// (a cleric resurrect beat the respawner): the corpse must stay retired.
// Guy ids are only unique per owning player (each client numbers its roster
// from its own counter), so identity is the (id, owner-player, save-slot)
// triple. Owner tags are kNoOwner for local play, where bare ids suffice.
[[nodiscard]] bool live_duplicate_exists(const GameWorld& world,
                                         const walker* corpse)
{
    if (corpse->myguy == nullptr)
        return false;
    for (const auto& uptr : world.oblist)
    {
        walker* other = uptr.get();
        if (other != nullptr && other != corpse && !other->dead() &&
            same_guy_identity(other->myguy, corpse->myguy))
        {
            return true;
        }
    }
    return false;
}

enum class CorpseDropScrub
{
    LifeGemOnly,
    All,
};

[[nodiscard]] bool stain_matches_corpse_identity(const walker* stain,
                                                  const walker* corpse)
{
    if (corpse->myguy == nullptr)
        return stain->myguy == nullptr;
    return same_guy_identity(stain->myguy, corpse->myguy);
}

[[nodiscard]] bool stain_belongs_to_pending_player(const GameWorld& world,
                                                    const walker* stain)
{
    if (stain->myguy == nullptr)
        return false;
    for (const RespawnEntry& entry : world.respawn.respawn_queue)
    {
        if (entry.kind != 0)
            continue;
        const walker* corpse = world.find_by_id(entry.walker_entity_id);
        if (corpse != nullptr &&
            same_guy_identity(stain->myguy, corpse->myguy) &&
            stain->team_num() == corpse->team_num() &&
            stain->floor() == corpse->floor() &&
            stain->xpos() == corpse->xpos() &&
            stain->ypos() == corpse->ypos())
        {
            return true;
        }
    }
    return false;
}

// LIFE_GEM hearts are retired as soon as any corpse is scheduled: repeated
// respawns must not farm the time-limit tiebreaker. AI stains go with them,
// while player stains remain during the countdown for cleric raises and the
// visible corpse. The player fire paths scrub both drops before reviving (or
// cancelling for a live duplicate), so no stale stain survives the queue.
void scrub_corpse_drops_in(const GameWorld::EntityList& list,
                           const walker* corpse, CorpseDropScrub scrub)
{
    for (const auto& uptr : list)
    {
        walker* fx = uptr.get();
        if (fx == nullptr || fx->dead() ||
            fx->query_order() != Order::Treasure ||
            (fx->family() != FAMILY_LIFE_GEM &&
             (scrub != CorpseDropScrub::All ||
              fx->family() != FAMILY_STAIN)) ||
            fx->team_num() != corpse->team_num() ||
            fx->floor() != corpse->floor())
        {
            continue;
        }
        // Player stains carry a copied guy: pair them by the same identity
        // triple that guards duplicate revives. An AI corpse may only sweep
        // an AI stain, so a nearby player's queued corpse is never collateral.
        if (fx->family() == FAMILY_STAIN &&
            !stain_matches_corpse_identity(fx, corpse))
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

void scrub_corpse_drops(GameWorld& world, const walker* corpse,
                        CorpseDropScrub scrub)
{
    scrub_corpse_drops_in(world.fxlist, corpse, scrub);
    scrub_corpse_drops_in(world.oblist, corpse, scrub);
}

void schedule_respawn(GameWorld& world, walker* corpse)
{
    RespawnState& respawn = world.respawn;
    if (respawn.respawn_queue.size() >= static_cast<std::size_t>(kRespawnMaxQueueEntries))
    {
        const auto victim = std::find_if(
            respawn.respawn_queue.begin(), respawn.respawn_queue.end(),
            [](const RespawnEntry& entry) { return entry.kind == 1; });
        if (victim == respawn.respawn_queue.end())
            return;
        // Fire the evicted bot immediately instead of dropping it: its corpse
        // is already swept, so a plain erase would shrink the roster forever.
        // Scripted modes take the RNG-free classic fire; NO on_respawn
        // dispatch here — scheduling can run inside a Lua binding call, and
        // a nested hook dispatch from an eviction is a re-entrancy the
        // engine does not owe (documented Phase-I behavior).
        const RespawnEntry evicted = *victim;
        respawn.respawn_queue.erase(victim);
        classic_fire_respawn(world, evicted);
        // A blocked classic AI fire re-enqueues itself: if the queue is full
        // again, drop that just-appended retry rather than the incoming
        // corpse. Either way one AI entry is permanently lost (a non-myguy
        // corpse is swept this same tick and never re-scanned), but the
        // retry's spot is currently occupied while the incoming corpse's may
        // be clear — and the cap must hold for the snapshot serializer.
        if (respawn.respawn_queue.size() >=
            static_cast<std::size_t>(kRespawnMaxQueueEntries))
        {
            respawn.respawn_queue.pop_back();
        }
    }

    RespawnEntry entry;
    entry.kind = (corpse->myguy != nullptr) ? 0 : 1;
    // A charmed corpse still wears the charmer's team_num; respawn breaks the
    // charm and the walker rejoins its true team.
    entry.team = (corpse->real_team_num() != 255) ? corpse->real_team_num()
                                                  : corpse->team_num();
    entry.family = static_cast<std::uint8_t>(corpse->family());
    const std::int32_t level =
        (corpse->stats() != nullptr) ? corpse->stats()->level() : 1;
    entry.level = static_cast<std::uint8_t>(std::clamp<std::int32_t>(level, 1, 255));
    entry.ticks_left = respawn.respawn_ticks;
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
    respawn.respawn_queue.push_back(entry);

    scrub_corpse_drops(
        world, corpse,
        entry.kind == 0 ? CorpseDropScrub::LifeGemOnly
                        : CorpseDropScrub::All);
}

} // namespace

// Per-tick countdown + fire (flat 1/tick decrement; the CTF control-point
// halving retired with the CTF engine — a mode wanting faster waits
// schedules with a shorter delay). Fire routing: scripted mode -> RNG-free
// classic placement + the on_respawn level hook (revive-in-place is the
// default when the mode registered none); classic modes -> classic
// placement. mode_run_tick calls this OUTSIDE any Lua dispatch, so the
// on_respawn dispatch never nests.
void respawn_run_timers(GameWorld& world)
{
    RespawnState& respawn = world.respawn;
    if (respawn.respawn_queue.empty())
        return;

    const bool scripted = mode_scripted_active(world);
    for (std::size_t i = 0; i < respawn.respawn_queue.size();)
    {
        RespawnEntry& entry = respawn.respawn_queue[i];
        entry.ticks_left -= std::min<std::uint16_t>(1, entry.ticks_left);
        if (entry.ticks_left > 0)
        {
            ++i;
            continue;
        }
        const RespawnEntry fired = entry;
        respawn.respawn_queue.erase(respawn.respawn_queue.begin() +
                                    static_cast<std::ptrdiff_t>(i));
        if (scripted)
        {
            if (walker* revived = classic_fire_respawn(world, fired))
                og::script::hooks::level_respawn(&world, revived);
        }
        else
            classic_fire_respawn(world, fired);
    }
}

bool classic_respawn_active(const GameWorld& world)
{
    // Scripted (TYPE_SCRIPTED) worlds with an ACTIVE mode own their
    // respawns through the Lua surface; a scripted map whose mode failed
    // activation falls to classic rules, including these.
    return !mode_scripted_active(world) &&
           world.respawn_mode >= kRespawnModeHeroes &&
           world.respawn_mode <= kRespawnModeTeamOneHeroes;
}

bool respawn_retains_player_control(const GameWorld& world,
                                    const walker* control)
{
    if (control == nullptr || !control->dead() || control->myguy == nullptr ||
        control->query_order() != Order::Living)
    {
        return false;
    }

    const unsigned char team = control->real_team_num() != 255
        ? control->real_team_num()
        : control->team_num();
    (void)team;
    // Scripted arm: a mode-scheduled respawn (Lua owns eligibility via
    // og.respawn_schedule) keeps its seat while the entry is pending and the
    // match is undecided — the retired CTF arm generalized.
    if (mode_scripted_active(world))
    {
        return !world.mode.win_latched &&
               already_scheduled(world.respawn, control->entity_id()) &&
               !live_duplicate_exists(world, control);
    }
    if (!classic_respawn_active(world))
        return false;
    const bool mode_allows_hero =
        world.respawn_mode != kRespawnModeTeamOneHeroes || team == 0;
    return mode_allows_hero && !live_duplicate_exists(world, control);
}

bool respawn_suppress_team_wipe_endgame(const GameWorld& world,
                                        short wiped_team)
{
    // An undecided scripted match: wipes never end the level — the mode's
    // Lua owns the end, and respawns give the wiped team a way back in.
    if (mode_scripted_active(world) && !world.mode.win_latched)
        return true;
    if (!classic_respawn_active(world))
        return false;
    // Modes 1 and 2 respawn heroes on every team. Mode 3 protects only the
    // player-facing Team 1 (internal team 0); another team's wipe must remain
    // terminal instead of being swallowed into an unwinnable empty seat.
    return world.respawn_mode != kRespawnModeTeamOneHeroes ||
           wiped_team < 0 || wiped_team == 0;
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
// Returns the revived walker (nullptr when nothing revived) so the scripted
// fire arm can dispatch on_respawn.
walker* classic_fire_player_respawn(GameWorld& world,
                                    const RespawnEntry& entry)
{
    walker* w = world.find_by_id(entry.walker_entity_id);
    if (w == nullptr || !w->dead() || w->query_order() != Order::Living)
        return nullptr;
    scrub_corpse_drops(world, w, CorpseDropScrub::All);
    if (live_duplicate_exists(world, w))
        return nullptr;
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
    return w;
}

// Classic AI fire (mode 2): a fresh walker per the fire_ai_respawn recipe,
// placed at the entry's recorded coordinates. A blocked spot withdraws the
// probe walker and re-enqueues the entry on a short deterministic retry
// cadence instead of teleporting (no world.rng_ draw).
walker* classic_fire_ai_respawn(GameWorld& world, const RespawnEntry& entry)
{
    walker* w = world.add_ob(Order::Living, entry.family);
    if (w == nullptr)
        return nullptr;
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
        return w;
    }
    world.remove_ob(w);
    if (entry.x >= 0 && entry.y >= 0 &&
        world.respawn.respawn_queue.size() <
            static_cast<std::size_t>(kRespawnMaxQueueEntries))
    {
        RespawnEntry retry = entry;
        retry.ticks_left = kClassicBlockedRetryTicks;
        world.respawn.respawn_queue.push_back(retry);
    }
    return nullptr;
}

walker* classic_fire_respawn(GameWorld& world, const RespawnEntry& entry)
{
    if (entry.kind == 0)
        return classic_fire_player_respawn(world, entry);
    return classic_fire_ai_respawn(world, entry);
}

// Classic scheduling eligibility: every dead hero (myguy) for mode 1, Team 1
// heroes only for mode 3, plus heroes and unowned AI livings WITH a recorded
// (level-authored) spawn point for mode 2. No team_active / score-team
// filters: classic levels field arbitrary teams. Generator spawns and summons
// stay the owner's business even after the owner dies — clear_stale_cross_refs
// nulls owner() the tick the owner dies, so the owner check alone would adopt
// orphans as permanent respawners; the spawn_x() gate keeps "endless battle"
// scoped to walkers with an authored placement (classic AI respawn copies
// inherit theirs).
[[nodiscard]] bool classic_respawn_corpse_eligible(GameWorld& world, walker* w)
{
    if (w == nullptr || !w->dead() || w->query_order() != Order::Living)
        return false;
    const std::uint8_t team = w->real_team_num() != 255
        ? w->real_team_num()
        : w->team_num();
    const bool hero_eligible =
        w->myguy != nullptr &&
        (world.respawn_mode != kRespawnModeTeamOneHeroes || team == 0);
    const bool ai_eligible =
        world.respawn_mode == kRespawnModeEveryone &&
        w->myguy == nullptr && w->owner() == nullptr && w->spawn_x() >= 0;
    const bool eligible = hero_eligible || ai_eligible;
    if (!eligible)
        return false;
    if (already_scheduled(world.respawn, w->entity_id()))
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

    RespawnState& respawn = world.respawn;
    // Lazy config resolution. Idempotent, so re-running it every active tick
    // is safe: nothing else writes respawn_ticks on a classic level, and a
    // snapshot restore rewrites this and the requested knob coherently.
    const short requested = world.ctf_requested_respawn_ticks;
    respawn.respawn_ticks =
        (requested >= kClassicMinRespawnTicks &&
         requested <= kClassicMaxRespawnTicks)
            ? static_cast<std::uint16_t>(requested)
            : kRespawnDefaultTicks;

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
    respawn_run_timers(world);
    classic_run_death_scan(world);
}

void classic_respawn_flush_pending(GameWorld& world)
{
    if (!classic_respawn_active(world))
        return;
    RespawnState& respawn = world.respawn;
    // Scan first, matching ctf_run_tick's death-scan-before-win-check order:
    // a hero dying on the very tick the level ends (a mutual kill with the
    // last foe, or a death the synchronous exit-accept path hasn't scanned
    // yet) gets its entry here and revives below. Timer values don't matter.
    classic_run_death_scan(world);
    // The player fire path never mutates the queue; iterate then clear.
    for (std::size_t i = 0; i < respawn.respawn_queue.size(); ++i)
    {
        const RespawnEntry entry = respawn.respawn_queue[i];
        if (entry.kind == 0)
            classic_fire_player_respawn(world, entry);
    }
    respawn.respawn_queue.clear();
}

bool classic_respawn_pending_hostile_foe(GameWorld& world)
{
    if (!classic_respawn_active(world))
        return false;
    // Entries scheduled on earlier ticks. Kind-1 (AI) entries only exist in
    // mode 2; kind-0 covers hostile heroes (other players' teams) too.
    for (const RespawnEntry& entry : world.respawn.respawn_queue)
    {
        if (entry.kind == 1 &&
            world.respawn_mode == kRespawnModeEveryone &&
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

// ===========================================================================
// Mode-agnostic respawn entry points (respawn/respawn_state.h). Serve the
// scripted-mode (TYPE_SCRIPTED) engine and the og.respawn_* bindings; the
// CTF and classic engines above keep their own routes through the same
// shared helpers. Everything here is RNG-free.
// ===========================================================================

void respawn_scan_anchors(GameWorld& world)
{
    // Team start markers, dead ones included (the level bootstrap kills the
    // markers it consumes for player placement). Idempotent: the arrays are
    // rebuilt from scratch.
    RespawnState& respawn = world.respawn;
    for (int t = 0; t < 4; ++t)
        respawn.anchor_count[t] = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->query_order() != Order::Special ||
            w->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        const unsigned char team = w->team_num();
        if (!is_score_team(team) ||
            respawn.anchor_count[team] >= kRespawnMaxAnchorsPerTeam)
            continue;
        respawn.anchor_x[team][respawn.anchor_count[team]] = w->xpos();
        respawn.anchor_y[team][respawn.anchor_count[team]] = w->ypos();
        respawn.anchor_count[team]++;
    }
}

void respawn_flush_revive_all(GameWorld& world)
{
    // First: fire every queued entry through the RNG-free classic paths. A
    // blocked AI fire re-enqueues a retry; swapping the queue out first
    // keeps this loop bounded, and the trailing clear drops those retries
    // (the match is decided — nothing will run their timers meaningfully).
    RespawnState& respawn = world.respawn;
    std::vector<RespawnEntry> pending;
    pending.swap(respawn.respawn_queue);
    for (const RespawnEntry& entry : pending)
        classic_fire_respawn(world, entry);
    respawn.respawn_queue.clear();

    // Then: revive still-unqueued eligible player corpses in place (a hero
    // dying the very tick the latch arms has no entry yet) — the CTF
    // win-check revive-all semantics with classic in-place placement. A
    // corpse that died charmed revives on its recorded true team.
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || !w->dead() ||
            w->query_order() != Order::Living || w->myguy == nullptr)
            continue;
        scrub_corpse_drops(world, w, CorpseDropScrub::All);
        if (live_duplicate_exists(world, w))
            continue;
        const unsigned char raw_team = (w->real_team_num() != 255)
            ? w->real_team_num()
            : w->team_num();
        const int team = is_score_team(raw_team) ? raw_team : 0;
        revive_player_walker(world, w, team);
        ensure_obmap_registration(world, w);
    }
}

bool respawn_schedule_corpse(GameWorld& world, walker* corpse,
                             int ticks_override)
{
    if (corpse == nullptr || !corpse->dead() ||
        corpse->query_order() != Order::Living)
        return false;
    if (already_scheduled(world.respawn, corpse->entity_id()))
        return false;
    if (live_duplicate_exists(world, corpse))
        return false;
    schedule_respawn(world, corpse);
    if (!already_scheduled(world.respawn, corpse->entity_id()))
    {
        // A full queue of player entries refuses new work (schedule_respawn
        // evicts bots only).
        return false;
    }
    if (ticks_override > 0)
    {
        world.respawn.respawn_queue.back().ticks_left =
            static_cast<std::uint16_t>(
                std::min(ticks_override, 65535));
    }
    return true;
}

bool respawn_pending_for(const GameWorld& world, const walker* w)
{
    if (w == nullptr)
        return false;
    return already_scheduled(world.respawn, w->entity_id());
}

int respawn_pending_count(const GameWorld& world, int team)
{
    int count = 0;
    for (const RespawnEntry& entry : world.respawn.respawn_queue)
    {
        if (static_cast<int>(entry.team) == team)
            count++;
    }
    return count;
}

bool respawn_spot_clear(GameWorld& world, walker* w, short x, short y,
                        int floor)
{
    if (w == nullptr)
        return false;
    if (floor < 0)
        return spawn_spot_clear(world, w, x, y);
    return classic_spot_clear(world, w, x, y, floor);
}

void respawn_scrub_stains_at(GameWorld& world, short x, short y, int floor)
{
    // The corpse-relative rule of scrub_corpse_drops_in, keyed by position:
    // (x, y) is the corpse's top-left, treated as a 16px family footprint
    // (larger corpses' drops still land within the 8px Manhattan window in
    // practice — the stain shares the corner, the gem is center_on'd). No
    // team filter: the calling hook context decides which corpse to scrub.
    const auto scrub_list = [&](const GameWorld::EntityList& list) {
        for (const auto& uptr : list)
        {
            walker* fx = uptr.get();
            if (fx == nullptr || fx->dead() ||
                fx->query_order() != Order::Treasure ||
                (fx->family() != FAMILY_STAIN &&
                 fx->family() != FAMILY_LIFE_GEM))
            {
                continue;
            }
            if (floor >= 0 && static_cast<int>(fx->floor()) != floor)
                continue;
            const int dx = std::abs((fx->xpos() + fx->sizex() / 2) - (x + 8));
            const int dy = std::abs((fx->ypos() + fx->sizey() / 2) - (y + 8));
            if (dx + dy > 8)
                continue;
            // A mode may scrub a permanent corpse beside a queued hero. The
            // positional API has no corpse identity, so preserve any player
            // stain whose copied guy and exact corpse location resolve to a
            // pending player entry.
            if (fx->family() == FAMILY_STAIN &&
                stain_belongs_to_pending_player(world, fx))
            {
                continue;
            }
            fx->set_dead(1);
        }
    };
    scrub_list(world.fxlist);
    scrub_list(world.oblist);
}

} // namespace og::sim
