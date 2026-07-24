#include <openglad/server/headless_server_runtime.h>

#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/campaign_io.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/progression.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

constexpr std::array<int, DIFFICULTY_SETTINGS> kDifficultyPercent = {
    50,
    100,
    200,
};
constexpr float kInitialMinHpSentinel = 5'000'000.0f;

int difficulty_percent_from_setting(int difficulty) noexcept
{
    const int normalized =
        ((difficulty % DIFFICULTY_SETTINGS) + DIFFICULTY_SETTINGS) %
        DIFFICULTY_SETTINGS;
    return kDifficultyPercent[static_cast<std::size_t>(normalized)];
}

std::unique_ptr<guy> make_guy_from_lobby_character(
    const og::sim::LobbyCharacterData& character)
{
    auto result = std::make_unique<guy>(character.family);
    result->id = character.guy_id;
    result->name = character.name;
    result->family = static_cast<char>(character.family);
    result->strength = character.strength;
    result->dexterity = character.dexterity;
    result->constitution = character.constitution;
    result->intelligence = character.intelligence;
    result->armor = character.armor;
    result->exp = character.exp;
    result->kills = character.kills;
    result->level_kills = character.level_kills;
    result->total_damage = character.total_damage;
    result->total_hits = character.total_hits;
    result->total_shots = character.total_shots;
    result->teamnum = character.teamnum;
    result->scen_damage = character.scen_damage;
    result->scen_kills = character.scen_kills;
    result->scen_damage_taken = character.scen_damage_taken;
    result->scen_min_hp = character.scen_min_hp;
    result->scen_shots = character.scen_shots;
    result->scen_hits = character.scen_hits;
    result->level = character.level;
    // v8: the deploy flag rides the lobby SLOT, not the guy payload; roster
    // assembly only materializes deployed slots, so mark the guy explicitly.
    result->deployed = true;
    return result;
}

void update_primary_team_totals(SaveData& save)
{
    const int team_index =
        (save.my_team >= 0 && save.my_team < MAX_PLAYERS) ? save.my_team : 0;
    save.score = save.m_score[team_index];
    save.totalcash = save.m_totalcash[team_index];
    save.totalscore = save.m_totalscore[team_index];
}

void sync_world_from_save_data(GameWorld& world, const SaveData& save)
{
    world.my_team = save.my_team;
    world.allied_mode = save.allied_mode;
    world.ctf_requested_team_count = save.ctf_team_count;
    world.ctf_requested_capture_limit = save.ctf_capture_limit;
    world.ctf_requested_respawn_ticks = save.ctf_respawn_ticks;
    world.ctf_requested_strip_scenario_troops = save.ctf_strip_scenario_troops;
    // Modes may clamp world knobs (Classic: identity). Applied in BOTH
    // sync_world_from_save_data twins (see screen.cpp).
    world.respawn_mode =
        og::mode::current_progression().clamp_respawn_mode(save.respawn_mode);
    world.generator_rate = save.generator_rate;
    world.keep_fallen_heroes = save.keep_fallen_heroes;
    world.current_scenario = save.scen_num;
    for (int index = 0; index < MAX_PLAYERS; ++index)
        world.m_score[index] = save.m_score[index];

    const auto completed_it = save.completed_levels.find(save.current_campaign);
    if (completed_it != save.completed_levels.end())
        world.completed_levels = completed_it->second;
    else
        world.completed_levels.clear();

    world.withdraw_requested = false;
    world.withdraw_level = -1;
}

walker* find_first_of(GameWorld& world,
                      Order order,
                      unsigned char family,
                      int team_num = -1)
{
    for (const auto& entity : world.oblist)
    {
        walker* const walker_ptr = entity.get();
        if (walker_ptr == nullptr || walker_ptr->dead())
            continue;
        if (walker_ptr->query_order() != order || walker_ptr->family() != family)
            continue;
        if (team_num != -1 && walker_ptr->team_num() != team_num)
            continue;
        return walker_ptr;
    }

    return nullptr;
}

void clear_battle_stats(guy& member)
{
    member.scen_damage = 0.0f;
    member.scen_kills = 0;
    member.scen_damage_taken = 0.0f;
    member.scen_min_hp = kInitialMinHpSentinel;
    member.scen_shots = 0;
    member.scen_hits = 0;
}

walker* create_team_walker(GameWorld& world, const guy& source)
{
    walker* const created =
        world.add_ob(Order::Living, static_cast<std::int32_t>(source.family));
    if (created == nullptr)
        return nullptr;

    created->set_owned_myguy(std::make_unique<guy>(source));
    if (created->stats() != nullptr)
        created->stats()->set_level(created->myguy->level);

    created->myguy->update_derived_stats(created);
    created->set_team_num(static_cast<unsigned char>(created->myguy->teamnum));
    created->set_real_team_num(255);
    return created;
}

void spawn_team_from_save(GameWorld& world, const SaveData& save)
{
    for (const auto& member : save.team_list)
    {
        if (!member)
            continue;

        // §4.2: benched (deployed == false) members stay in the save's
        // roster but never enter the level. Networked rosters arrive
        // pre-filtered (the lobby equivalent drops benched slots and its guy
        // copies re-deploy), so this guard is the LOCAL session's assembly
        // filter — the local equivalent deliberately keeps benched members
        // so the active-company seed never loses them from the save file.
        if (!member->deployed)
            continue;

        walker* const created = create_team_walker(world, *member);
        if (created == nullptr || created->myguy == nullptr)
            continue;

        clear_battle_stats(*created->myguy);

        // Start markers are team-owned. A missing/exhausted exact marker
        // falls back to a neutral teleport, never another team's start.
        walker* marker = find_first_of(
            world,
            Order::Special,
            static_cast<unsigned char>(FAMILY_RESERVED_TEAM),
            static_cast<int>(member->teamnum));

        if (marker != nullptr)
        {
            // Inherit the marker's floor so the team spawns on the authored floor
            // in multi-floor levels. set_floor MUST precede setxy (setxy re-buckets
            // the floor-keyed obmap). Single-floor safe: markers are floor 0.
            created->set_floor(marker->floor());
            created->setxy(marker->xpos(), marker->ypos());
            marker->set_dead(1);
        }
        else
        {
            created->teleport();
        }
        // Record the level-entry spawn point (the classic respawn feature
        // revives heroes here). Read back from the walker so the RNG teleport
        // fallback position is captured exactly.
        created->set_spawn_point(created->xpos(), created->ypos(),
                                 static_cast<std::uint8_t>(created->floor()));
    }

    while (walker* marker = find_first_of(
               world,
               Order::Special,
               static_cast<unsigned char>(FAMILY_RESERVED_TEAM)))
    {
        marker->set_dead(1);
    }
}

bool should_preserve_completed_level_entity(const walker& entity)
{
    const Order order = entity.query_order();
    const int family = entity.family();
    if ((entity.team_num() == 0 || entity.myguy != nullptr) &&
        order == Order::Living)
    {
        return true;
    }

    if (order == Order::Treasure &&
        (family == FAMILY_EXIT || family == FAMILY_TELEPORTER))
    {
        return true;
    }

    return false;
}

template <typename EntityList>
void clear_completed_level_entities(EntityList& entities)
{
    for (const auto& entry : entities)
    {
        walker* const entity = entry.get();
        if (entity == nullptr || should_preserve_completed_level_entity(*entity))
            continue;
        entity->set_dead(1);
    }
}

void apply_completed_level_cleanup(GameWorld& world)
{
    // CTF rematches replay the full map: flags, control points, and bot
    // squads must survive a "level already completed" reload.
    if (world.type & GameWorld::TYPE_CTF)
        return;

    clear_completed_level_entities(world.oblist);
    clear_completed_level_entities(world.weaplist);
    clear_completed_level_entities(world.fxlist);
}

std::uint32_t calculate_headless_time_bonus(const GameWorld& world,
                                            const SaveData& save,
                                            int player_index)
{
    // Single-sourced with the SDL results / curses networked persist through
    // the shared og_resources helper so every machine sizes the same pot.
    return og::progression::calculate_win_time_bonus(world, save, player_index);
}

void prepare_world_for_gameplay(LevelRuntimeData& level_data,
                                og::sim::SimEventLog& events)
{
    GameWorld& world = level_data.world();
    world.tick_count_ = 0;
    world.reset_level_progress();
    world.clear_removed_entity_ids();
    world.clear_grid_dirty_tiles();
    events.clear();
}

} // namespace

namespace og::server {

void copy_headless_server_save_data(SaveData& destination,
                                    const SaveData& source)
{
    destination.save_name = source.save_name;
    destination.current_campaign = source.current_campaign;
    destination.scen_num = source.scen_num;
    destination.completed_levels = source.completed_levels;
    destination.current_levels = source.current_levels;
    destination.score = source.score;
    std::copy(std::begin(source.m_score), std::end(source.m_score),
              std::begin(destination.m_score));
    destination.totalcash = source.totalcash;
    std::copy(std::begin(source.m_totalcash), std::end(source.m_totalcash),
              std::begin(destination.m_totalcash));
    destination.totalscore = source.totalscore;
    std::copy(std::begin(source.m_totalscore), std::end(source.m_totalscore),
              std::begin(destination.m_totalscore));
    destination.my_team = source.my_team;
    destination.team_size = source.team_size;
    destination.numplayers = source.numplayers;
    destination.allied_mode = source.allied_mode;
    destination.ctf_team_count = source.ctf_team_count;
    destination.ctf_capture_limit = source.ctf_capture_limit;
    destination.ctf_respawn_ticks = source.ctf_respawn_ticks;
    destination.ctf_strip_scenario_troops = source.ctf_strip_scenario_troops;
    // Cross-control (protocol v8): session-only match setting; must survive
    // the server/checkpoint copies like the other lobby-negotiated settings
    // (the documented dropped-field bug class, design §4.1).
    destination.cross_control = source.cross_control;
    // Tower run state (GTL v13) must ride the server/checkpoint copies:
    // advance_cursor regenerates floors from tower_run_seed and merges
    // tower_best_floor, and on_run_ended re-writes both to save0 — a copy
    // that dropped them would clobber the on-disk seed to 0 and lose the
    // best-floor merge on the curses/headless paths.
    destination.tower_best_floor = source.tower_best_floor;
    destination.tower_run_seed = source.tower_run_seed;
    // Company bookkeeping (GTL v14): the timestamp must survive the
    // server/checkpoint copies or a copied-then-persisted save would reset
    // the company's recency to 0 (the documented dropped-field bug class,
    // design §3.4). The per-guy deploy flag rides the guy copies below.
    destination.last_played_unix_s = source.last_played_unix_s;

    for (std::size_t index = 0; index < destination.team_list.size(); ++index)
    {
        if (source.team_list[index])
            destination.team_list[index] =
                std::make_unique<guy>(*source.team_list[index]);
        else
            destination.team_list[index].reset();
    }
}

void apply_headless_lobby_game_start_config(
    SaveData& save,
    const og::sim::LobbySaveDataEquivalent& config_save)
{
    save.current_campaign = config_save.current_campaign.empty()
        ? std::string("org.openglad.gladiator")
        : config_save.current_campaign;
    save.scen_num = config_save.scen_num > 0 ? config_save.scen_num : 1;
    save.current_levels[save.current_campaign] = save.scen_num;
    save.numplayers = config_save.numplayers;
    save.allied_mode = static_cast<short>(config_save.allied_mode);
    save.ctf_team_count = static_cast<short>(config_save.ctf_team_count);
    save.ctf_capture_limit = static_cast<short>(config_save.ctf_capture_limit);
    save.ctf_respawn_ticks = static_cast<short>(config_save.ctf_respawn_ticks);
    save.ctf_strip_scenario_troops =
        static_cast<short>(config_save.ctf_strip_scenario_troops);
    save.respawn_mode = static_cast<short>(config_save.respawn_mode);
    save.generator_rate = static_cast<short>(config_save.generator_rate);
    save.keep_fallen_heroes = static_cast<short>(config_save.keep_fallen_heroes);
    save.cross_control = static_cast<short>(config_save.cross_control);
    save.my_team = 0;

    for (auto& member : save.team_list)
        member.reset();
    save.team_size = 0;

    for (const auto& slot : config_save.team_list)
    {
        if (slot.slot_index >= save.team_list.size())
            continue;

        std::unique_ptr<guy> member =
            make_guy_from_lobby_character(slot.character);
        member->owner_player_index = slot.owner_player_index;
        member->owner_save_slot = slot.owner_save_slot;
        save.team_list[slot.slot_index] = std::move(member);
        ++save.team_size;
    }

    update_primary_team_totals(save);
}

void sync_headless_server_save_data_from_world(SaveData& save,
                                               const GameWorld& world)
{
    save.my_team = world.my_team;
    save.allied_mode = world.allied_mode;
    save.scen_num = static_cast<short>(world.current_scenario);
    save.current_levels[save.current_campaign] = save.scen_num;
    for (int index = 0; index < MAX_PLAYERS; ++index)
        save.m_score[index] = world.m_score[index];

    save.completed_levels[save.current_campaign] = world.completed_levels;
    update_primary_team_totals(save);
}

bool load_headless_level_from_save(LevelRuntimeData& level_data,
                                   SaveData& save,
                                   int difficulty_setting,
                                   og::sim::SimEventLog& events,
                                   bool authoritative)
{
    const int current_level =
        load_campaign(save.current_campaign, save.current_levels, save.scen_num);
    if (current_level < 0)
    {
        LogError("headless_server_campaign_load_failed campaign={} error={}\n",
                 save.current_campaign,
                 current_level);
        return false;
    }
    if (current_level != save.scen_num)
    {
        LogError("headless_server_level_mismatch campaign={} save_level={} current_level={}\n",
                 save.current_campaign,
                 save.scen_num,
                 current_level);
    }

    GameWorld& world = level_data.world();
    sync_world_from_save_data(world, save);
    world.id = save.scen_num;
    if (!level_data.load())
    {
        const short requested_level = save.scen_num;
        // Campaigns may not start at scenario 1 (CTF starts at 500); wrap a
        // run-off cursor to the lowest level the mounted campaign ships.
        short fallback_level = 1;
        const std::vector<int> levels = list_levels_v();
        if (!levels.empty())
            fallback_level = static_cast<short>(levels.front());
        LogError("headless_server_level_load_failed level={} action=fallback_to_{}\n",
                 requested_level, fallback_level);
        save.scen_num = fallback_level;
        save.current_levels[save.current_campaign] = save.scen_num;
        sync_world_from_save_data(world, save);
        world.id = save.scen_num;
        if (!level_data.load())
        {
            LogError("headless_server_level_fallback_failed requested={} fallback={}\n",
                     requested_level, fallback_level);
            return false;
        }
    }

    sync_world_from_save_data(world, save);
    world.difficulty = static_cast<short>(
        difficulty_percent_from_setting(difficulty_setting));
    for (const auto& entity : world.oblist)
    {
        walker* const walker_ptr = entity.get();
        if (walker_ptr != nullptr && walker_ptr->stats() != nullptr)
            walker_ptr->set_difficulty(static_cast<std::uint32_t>(
                walker_ptr->stats()->level()));
    }

    spawn_team_from_save(world, save);
    if (save.is_level_completed(save.scen_num))
        apply_completed_level_cleanup(world);

    prepare_world_for_gameplay(level_data, events);
    // The dedicated server (and any headless host) rolls this level's
    // weather; mirror worlds stay at the load-reset None until the first
    // snapshot applies the authoritative kind.
    if (authoritative)
        world.roll_weather();
    return true;
}

bool complete_headless_level_and_load_next(LevelRuntimeData& level_data,
                                           SaveData& active_save,
                                           SaveData& checkpoint_save,
                                           int difficulty_setting,
                                           og::sim::SimEventLog& events,
                                           int next_level)
{
    GameWorld& world = level_data.world();
    sync_headless_server_save_data_from_world(active_save, world);

    // The shared win fold (og::progression::apply_win_fold) tallies the
    // money, marks completion, and advances the cursor; this site keeps its
    // headless time-bonus source and its checkpoint/reload tail.
    og::progression::WinFoldContext fold_ctx;
    for (std::size_t team_index = 0;
         team_index < fold_ctx.time_bonus.size();
         ++team_index)
    {
        fold_ctx.time_bonus[team_index] = calculate_headless_time_bonus(
            world,
            active_save,
            static_cast<int>(team_index));
    }
    fold_ctx.rematch_shape = og::progression::ctf_rematch_shape(
        world, active_save, static_cast<short>(next_level));
    fold_ctx.finished_level = active_save.scen_num;
    fold_ctx.outcome.ending = 0;
    fold_ctx.outcome.next_level = static_cast<short>(next_level);
    fold_ctx.outcome.networked = true;
    og::progression::apply_win_fold(active_save, world, fold_ctx);

    update_primary_team_totals(active_save);
    copy_headless_server_save_data(checkpoint_save, active_save);

    return load_headless_level_from_save(
        level_data,
        active_save,
        difficulty_setting,
        events,
        /*authoritative=*/true);
}

bool withdraw_headless_level(LevelRuntimeData& level_data,
                             SaveData& active_save,
                             SaveData& checkpoint_save,
                             int difficulty_setting,
                             og::sim::SimEventLog& events,
                             int destination_level)
{
    // Withdraw/quit-mission is a run-ending, non-win exit: route the mode's
    // run-end policy (Classic: no-op) before the checkpoint restore discards
    // the level's state. (A headless team-wipe loss has no server-side save
    // mutation at all — each display client routes its own run-end hook.)
    {
        og::mode::LevelOutcome run_outcome;
        run_outcome.ending = 1;
        run_outcome.next_level = static_cast<short>(destination_level);
        run_outcome.networked = true;
        run_outcome.withdrawn = true;
        og::mode::current_progression().on_run_ended(
            active_save, level_data.world(), run_outcome);
    }

    copy_headless_server_save_data(active_save, checkpoint_save);
    active_save.scen_num = static_cast<short>(destination_level);
    active_save.current_levels[active_save.current_campaign] = active_save.scen_num;
    update_primary_team_totals(active_save);
    copy_headless_server_save_data(checkpoint_save, active_save);
    return load_headless_level_from_save(
        level_data,
        active_save,
        difficulty_setting,
        events,
        /*authoritative=*/true);
}

void install_headless_server_callbacks(og::sim::GameServer& game_server,
                                       LevelRuntimeData& level_data,
                                       SaveData& active_save,
                                       SaveData& checkpoint_save,
                                       int difficulty_setting,
                                       og::sim::SimEventLog& events)
{
    game_server.on_save_sync = [&active_save, &level_data] {
        sync_headless_server_save_data_from_world(
            active_save, level_data.world());
    };
    game_server.on_level_transition =
        [&level_data,
         &active_save,
         &checkpoint_save,
         difficulty_setting,
         &events](int next_level) {
            return complete_headless_level_and_load_next(
                level_data,
                active_save,
                checkpoint_save,
                difficulty_setting,
                events,
                next_level);
        };
    game_server.on_exit_accepted =
        [&level_data,
         &active_save,
         &checkpoint_save,
         difficulty_setting,
         &events](int destination) {
            return complete_headless_level_and_load_next(
                level_data,
                active_save,
                checkpoint_save,
                difficulty_setting,
                events,
                destination);
        };
    game_server.on_withdraw_accepted =
        [&level_data,
         &active_save,
         &checkpoint_save,
         difficulty_setting,
         &events](int destination) {
            return withdraw_headless_level(
                level_data,
                active_save,
                checkpoint_save,
                difficulty_setting,
                events,
                destination);
        };
}

} // namespace og::server
