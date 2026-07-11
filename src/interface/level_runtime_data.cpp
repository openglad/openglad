/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/base.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/campaign_yaml.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

#include <openglad/interface/level_render.h>
#include <openglad/resources/level_data_hooks.h>
#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>


void LevelRuntimeData::set_sim_context(SaveData* save, std::int32_t* enemy_freeze,
                                og::sim::SimEventLog* events, IRandom* rng,
                                cfg_store* config)
{
    (void)enemy_freeze;
    (void)rng;
    sim_context_save_ = save;
    sim_context_events_ = events;
    sim_context_config_ = config;
    world().set_gameplay_context_bindings(save, events, config);
}

static EntityFactory make_default_entity_factory()
{
    EntityFactory factory;
    factory.attach_render = [](walker& w, const PixieData& data) {
        w.attach_render(data);
    };
    factory.report_error = [](const std::string& message) {
        LogError("{}\n", message);
    };
    return factory;
}

static void wire_world_loader(GameWorld& world,
                              const std::shared_ptr<loader>& game_loader)
{
    world.entity_factory = [game_loader](Order order, std::int32_t family) -> std::unique_ptr<walker> {
        if (!game_loader)
            return nullptr;
        return game_loader->create_walker_owned(order, family);
    };

    world.entity_configurator = [game_loader](walker& entity, Order order, std::int32_t family) -> const PixieData* {
        if (!game_loader)
            return nullptr;
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(), entity.family());
    };

    world.entity_derived_stats = [game_loader](walker* entity, Order order, std::int32_t family) {
        if (entity == nullptr || !game_loader)
            return;
        game_loader->set_derived_stats(entity, order, family);
    };
}

static std::string join_description_lines(const std::list<std::string>& description)
{
    std::string result;
    for(auto e = description.begin(); e != description.end();)
    {
        result += *e;
        e++;
        if(e != description.end())
            result += '\n';
    }
    return result;
}

static og::data::CampaignYaml campaign_data_to_yaml(const CampaignData& campaign)
{
    og::data::CampaignYaml metadata;
    metadata.title = campaign.title;
    metadata.version = campaign.version;
    metadata.first_level = campaign.first_level;
    metadata.suggested_power = campaign.suggested_power;
    metadata.authors = campaign.authors;
    metadata.contributors = campaign.contributors;
    metadata.description = join_description_lines(campaign.description);
    return metadata;
}

static void apply_campaign_yaml(CampaignData& campaign, const og::data::CampaignYaml& metadata)
{
    if(metadata.saw_title)
        campaign.title = metadata.title;
    if(metadata.saw_version)
        campaign.version = metadata.version;
    if(metadata.saw_authors)
        campaign.authors = metadata.authors;
    if(metadata.saw_contributors)
        campaign.contributors = metadata.contributors;
    if(metadata.saw_description)
        campaign.description = explode(metadata.description, '\n');
    if(metadata.saw_suggested_power)
        campaign.suggested_power = metadata.suggested_power;
    if(metadata.saw_first_level)
        campaign.first_level = metadata.first_level;
}

static void wire_world_entity_services(GameWorld* world,
                                       LevelRuntimeData* level,
                                       const LevelDataHooks* hooks)
{
    if (world == nullptr || level == nullptr)
        return;

    if (hooks != nullptr && hooks->wire_world_entity_services != nullptr)
    {
        hooks->wire_world_entity_services(world, level);
        return;
    }

    EntityFactory entity_factory = (hooks != nullptr && hooks->create_entity_factory != nullptr)
        ? hooks->create_entity_factory()
        : make_default_entity_factory();
    wire_world_loader(*world, std::make_shared<loader>(std::move(entity_factory)));
}

static void clear_world_entity_services(GameWorld* world)
{
    if (world == nullptr)
        return;
    world->entity_factory = {};
    world->entity_configurator = {};
    world->entity_derived_stats = {};
}

static void install_world_detach_callback(GameWorld* world, LevelRuntimeData* level)
{
    if (world == nullptr)
        return;
    if (level == nullptr)
    {
        world->set_detach_callback({});
        return;
    }

    world->set_detach_callback([level, world] {
        if (&level->world() == world)
            level->attach_world(nullptr);
    });
}

class ScopedCurrentGameWorldBinding
{
public:
    ScopedCurrentGameWorldBinding()
        : context_(current_game)
        , original_world_(context_ ? context_->world : nullptr)
    {
    }

    void bind(GameWorld* world)
    {
        if (context_ != nullptr)
            context_->world = world;
    }

    void restore()
    {
        if (restored_ || context_ == nullptr)
            return;
        context_->world = original_world_;
        restored_ = true;
    }

    ~ScopedCurrentGameWorldBinding()
    {
        restore();
    }

private:
    GameplayContext* context_ = nullptr;
    GameWorld* original_world_ = nullptr;
    bool restored_ = false;
};

class ScopedLoadWorldRngOverride
{
public:
    explicit ScopedLoadWorldRngOverride(IRandom* rng)
        : rng_(rng)
    {
        if (rng_ == nullptr || gameplay_rng_override() != nullptr)
            return;

        set_gameplay_rng_override(&rng_);
        installed_ = true;
    }

    ~ScopedLoadWorldRngOverride()
    {
        if (installed_)
            set_gameplay_rng_override(nullptr);
    }

private:
    IRandom* rng_ = nullptr;
    bool installed_ = false;
};

void replace_loaded_world_state(LevelRuntimeData* level, GameWorld& loaded_world)
{
    if (level == nullptr)
        return;

    GameWorld& dst = level->world();

    level->delete_objects();
    dst.delete_grid();

    dst.title = std::move(loaded_world.title);
    dst.type = loaded_world.type;
    dst.par_value = loaded_world.par_value;
    dst.time_bonus_limit = loaded_world.time_bonus_limit;
    dst.difficulty = loaded_world.difficulty;
    dst.level_done = loaded_world.level_done;
    dst.game_ended = loaded_world.game_ended;
    dst.completion_events_emitted = loaded_world.completion_events_emitted;
    dst.next_level = loaded_world.next_level;
    dst.ending = loaded_world.ending;
    dst.enemy_freeze = loaded_world.enemy_freeze;
    dst.timer_wait = loaded_world.timer_wait;
    dst.end = loaded_world.end;
    dst.retry = loaded_world.retry;
    dst.control_hp = loaded_world.control_hp;
    dst.withdraw_requested = loaded_world.withdraw_requested;
    dst.withdraw_level = loaded_world.withdraw_level;
    std::copy(std::begin(loaded_world.m_score), std::end(loaded_world.m_score),
              std::begin(dst.m_score));
    dst.my_team = loaded_world.my_team;
    dst.allied_mode = loaded_world.allied_mode;
    dst.ctf_requested_team_count = loaded_world.ctf_requested_team_count;
    dst.ctf_requested_capture_limit = loaded_world.ctf_requested_capture_limit;
    dst.ctf_requested_respawn_ticks = loaded_world.ctf_requested_respawn_ticks;
    dst.ctf_requested_strip_scenario_troops =
        loaded_world.ctf_requested_strip_scenario_troops;
    dst.ctf = std::move(loaded_world.ctf);
    dst.current_scenario = loaded_world.current_scenario;
    dst.completed_levels = std::move(loaded_world.completed_levels);
    // Fresh worlds carry WeatherKind::None: every level load resets the
    // weather; only the authoritative side re-rolls it afterwards.
    dst.set_weather(loaded_world.weather());
    dst.move_entities_from(loaded_world);
    dst.living_count = loaded_world.living_count;
    dst.grid = std::move(loaded_world.grid);
    dst.pixmaxx = loaded_world.pixmaxx;
    dst.pixmaxy = loaded_world.pixmaxy;
    dst.myobmap = std::move(loaded_world.myobmap);
    if (!dst.myobmap)
        dst.myobmap = std::make_unique<obmap>();
    if (dst.grid.valid())
        dst.mysmoother.set_target(dst.grid);
    else
        dst.mysmoother.reset();
    // Z-axis: carry the loaded stacked floors across too, re-targeting each
    // floor smoother at its moved-in grid (the span must follow the buffer).
    dst.extra_floors_ = std::move(loaded_world.extra_floors_);
    for (auto& fl : dst.extra_floors_)
        if (fl.grid.valid())
            fl.floor_smoother.set_target(fl.grid);
}


CampaignData::CampaignData(const std::string& campaign_id)
    : id(campaign_id), title("New Campaign"), rating(0.0f), version("1.0"), suggested_power(0), first_level(1), num_levels(0)
{
    description.push_back("No description.");
}

CampaignData::~CampaignData()
{
	icondata.free();
}


bool CampaignData::load()
{
    last_io_error_ = IoError::None;
    std::string old_campaign = get_mounted_campaign();
    (void)unmount_campaign_package_with_error(old_campaign);

    // Load the campaign data from <user_data>/scen/<id>.glad
    if(mount_campaign_package_with_error(id) == CampaignPackageIoError::None)
    {
        og::data::CampaignYaml metadata;
        const auto read_result = og::data::read_campaign_yaml("campaign.yaml", metadata, true);
        if(read_result == og::data::CampaignYamlReadResult::OpenFailed)
        {
            last_io_error_ = IoError::OpenReadFailed;
            (void)unmount_campaign_package_with_error(id);
            (void)mount_campaign_package_with_error(old_campaign);
            return false;
        }

        apply_campaign_yaml(*this, metadata);
        if(read_result == og::data::CampaignYamlReadResult::ParseFailed)
            last_io_error_ = IoError::ParseFailed;

        // TODO: Get rating from website
        rating = 0.0f;

        icondata = read_pixie_file("icon.png");

        // Count the number of levels
        std::list<int> levels = list_levels();
        num_levels = static_cast<int>(levels.size());

        (void)unmount_campaign_package_with_error(id);
    }
    else
    {
        last_io_error_ = IoError::PackageMountFailed;
    }

    (void)mount_campaign_package_with_error(old_campaign);
    
    return (last_io_error_ == IoError::None);
}

bool CampaignData::save()
{
    last_io_error_ = IoError::None;
    cleanup_unpacked_campaign();
    
    bool result = true;
    if(unpack_campaign(id))
    {
        // Unmount campaign while it is changed
        //unmount_campaign_package(ascreen->current_campaign);
        
        if (!og::data::write_campaign_yaml("temp/campaign.yaml", campaign_data_to_yaml(*this)))
        {
            LogError("Couldn't write YAML descriptor for temp/campaign.yaml.\n");
            last_io_error_ = IoError::OpenWriteFailed;
            cleanup_unpacked_campaign();
            return false;
        }

        if(result)
        {
            if(repack_campaign(id))
            {
                Log("Campaign saved.\n");
            }
            else
            {
                LogError("campaign_save_failed id={} reason=repack_failed\n", id);
                result = false;
                last_io_error_ = IoError::PackageRepackFailed;
            }
        }
    }
    else
    {
        LogError("campaign_save_failed id={} reason=unpack_failed\n", id);
        result = false;
        last_io_error_ = IoError::PackageUnpackFailed;
    }
    cleanup_unpacked_campaign();

    if(result)
    {
        last_io_error_ = IoError::None;
        og::data::clear_campaign_metadata_cache();
    }
    return result;
}

bool CampaignData::save_as(const std::string& new_id)
{
    last_io_error_ = IoError::None;
    cleanup_unpacked_campaign();

    bool result = true;
    // Unpack the campaign
    if(unpack_campaign(id))
    {
        // Save the descriptor file
        if (!og::data::write_campaign_yaml("temp/campaign.yaml", campaign_data_to_yaml(*this)))
        {
            LogError("Couldn't write YAML descriptor for temp/campaign.yaml.\n");
            result = false;
            last_io_error_ = IoError::OpenWriteFailed;
        }
        
        // Repack the campaign
        if(result)
        {
            if(repack_campaign(new_id))
            {
                // Success!
                id = new_id;
                Log("Campaign saved.\n");
            }
            else
            {
                LogError("campaign_save_as_failed src_id={} dst_id={} reason=repack_failed\n", id, new_id);
                result = false;
                last_io_error_ = IoError::PackageRepackFailed;
            }
        }
    }
    else
    {
        LogError("campaign_save_as_failed src_id={} dst_id={} reason=unpack_failed\n", id, new_id);
        result = false;
        last_io_error_ = IoError::PackageUnpackFailed;
    }
    cleanup_unpacked_campaign();

    if(result)
    {
        last_io_error_ = IoError::None;
        og::data::clear_campaign_metadata_cache();
    }
    return result;
}

CampaignData::IoError CampaignData::load_with_error()
{
    load();
    return last_io_error_;
}

CampaignData::IoError CampaignData::save_with_error()
{
    save();
    return last_io_error_;
}

CampaignData::IoError CampaignData::save_as_with_error(const std::string& new_id)
{
    save_as(new_id);
    return last_io_error_;
}

std::string CampaignData::getDescriptionLine(int i)
{
    return get_description_line(i);
}

std::string CampaignData::get_description_line(int i)
{
    if(i < 0 || i >= int(description.size()))
        return "";

    std::list<std::string>::iterator e = description.begin();
    while(i > 0)
    {
        e++;
        i--;
    }

    return *e;
}





LevelRuntimeData::LevelRuntimeData(int level_id)
    : LevelRuntimeData(level_id, false, nullptr, nullptr)
{
}

LevelRuntimeData::LevelRuntimeData(int level_id, const LevelDataHooks* hooks)
    : LevelRuntimeData(level_id, false, hooks, nullptr)
{
}

LevelRuntimeData::LevelRuntimeData(int level_id, bool headless)
    : LevelRuntimeData(level_id, headless, nullptr, nullptr)
{
}

LevelRuntimeData::LevelRuntimeData(int level_id, bool headless, const LevelDataHooks* hooks)
    : LevelRuntimeData(level_id, headless, hooks, nullptr)
{
}

LevelRuntimeData::LevelRuntimeData(int level_id, bool headless,
                                   const LevelDataHooks* hooks,
                                   og::gameplay::ILevelVisuals* visuals)
    : level_done(this)
    , numobs(this)
    , world_(&owned_world_)
    , level_visuals_(visuals ? visuals : &owned_level_visuals_)
{
    hooks_ = hooks;
    headless_ = headless;
    world_->id = level_id;

	world_->myobmap = std::make_unique<obmap>();
    wire_world_entity_services(world_, this, hooks_);

    if (!headless)
    {
        load_map_data(level_visuals().pixdata);
        if (hooks_ && hooks_->create_level_render)
            level_visuals().renderer_ = hooks_->create_level_render(level_visuals().pixdata);
    }
}

LevelRuntimeData::~LevelRuntimeData()
{
    delete_objects();
    delete_grid();

    if (world_ != nullptr)
    {
        world_->set_detach_callback({});
        clear_world_entity_services(world_);
        world_ = nullptr;
    }

    level_visuals().renderer_.reset();
    for (int i = 0; i < PIX_MAX; i++)
        level_visuals().pixdata[i].free();
}

void LevelRuntimeData::clear()
{
    world().clear();
    level_visuals().topx = 0;
    level_visuals().topy = 0;
}

void LevelRuntimeData::attach_world(GameWorld* world)
{
    GameWorld* old_world = world_;
    GameWorld* next_world = world ? world : &owned_world_;
    if (next_world == old_world)
    {
        if (old_world != nullptr)
        {
            old_world->set_detach_callback({});
            wire_world_entity_services(old_world, this, hooks_);
            old_world->set_gameplay_context_bindings(
                sim_context_save_, sim_context_events_, sim_context_config_);
            if (old_world != &owned_world_)
                install_world_detach_callback(old_world, this);
        }
        return;
    }

    if (old_world != nullptr)
    {
        next_world->id = old_world->id;
        next_world->title = old_world->title;
        next_world->type = old_world->type;
        next_world->par_value = old_world->par_value;
        next_world->time_bonus_limit = old_world->time_bonus_limit;
        next_world->difficulty = old_world->difficulty;
        next_world->level_done = old_world->level_done;
        next_world->game_ended = old_world->game_ended;
        next_world->completion_events_emitted = old_world->completion_events_emitted;
        next_world->next_level = old_world->next_level;
        next_world->ending = old_world->ending;
        next_world->enemy_freeze = old_world->enemy_freeze;
        next_world->timer_wait = old_world->timer_wait;
        next_world->end = old_world->end;
        next_world->retry = old_world->retry;
        next_world->control_hp = old_world->control_hp;
        next_world->withdraw_requested = old_world->withdraw_requested;
        next_world->withdraw_level = old_world->withdraw_level;
        std::copy(std::begin(old_world->m_score), std::end(old_world->m_score),
                  std::begin(next_world->m_score));
        next_world->my_team = old_world->my_team;
        next_world->allied_mode = old_world->allied_mode;
        next_world->ctf_requested_team_count = old_world->ctf_requested_team_count;
        next_world->ctf_requested_capture_limit = old_world->ctf_requested_capture_limit;
        next_world->ctf_requested_respawn_ticks = old_world->ctf_requested_respawn_ticks;
        next_world->ctf_requested_strip_scenario_troops =
            old_world->ctf_requested_strip_scenario_troops;
        next_world->ctf = std::move(old_world->ctf);
        next_world->current_scenario = old_world->current_scenario;
        next_world->completed_levels = old_world->completed_levels;
        next_world->move_entities_from(*old_world);
        next_world->living_count = old_world->living_count;
        next_world->grid = std::move(old_world->grid);
        next_world->pixmaxx = old_world->pixmaxx;
        next_world->pixmaxy = old_world->pixmaxy;
        next_world->myobmap = std::move(old_world->myobmap);
        if (next_world->grid.valid())
            next_world->mysmoother.set_target(next_world->grid);
        else
            next_world->mysmoother.reset();

        old_world->living_count = 0;
        old_world->pixmaxx = 0;
        old_world->pixmaxy = 0;
        old_world->withdraw_requested = false;
        old_world->withdraw_level = -1;
        old_world->ctf = og::sim::CtfState{};
        old_world->mysmoother.reset();
        if (!old_world->myobmap)
            old_world->myobmap = std::make_unique<obmap>();
        old_world->set_detach_callback({});
        clear_world_entity_services(old_world);
        old_world->set_gameplay_context_bindings(nullptr, nullptr, nullptr);
    }

    world_ = next_world;
    wire_world_entity_services(world_, this, hooks_);
    world_->set_gameplay_context_bindings(
        sim_context_save_, sim_context_events_, sim_context_config_);
    if (world_ != &owned_world_)
        install_world_detach_callback(world_, this);
    else
        world_->set_detach_callback({});
}

walker* LevelRuntimeData::add_ob(Order order, std::int32_t family, bool atstart)
{
    return world().add_ob(order, family, atstart);
}

walker* LevelRuntimeData::add_fx_ob(Order order, std::int32_t family)
{
    return world().add_fx_ob(order, family);
}

walker* LevelRuntimeData::add_weap_ob(Order order, std::int32_t family)
{
    return world().add_weap_ob(order, family);
}

short LevelRuntimeData::remove_ob(walker  *ob)
{
    return world().remove_ob(ob);
}

void LevelRuntimeData::delete_grid()
{
    world().delete_grid();
}

void LevelRuntimeData::create_new_grid()
{
    world().create_new_grid();
}

void LevelRuntimeData::resize_grid(int width, int height)
{
    world().resize_grid(width, height);
}

void LevelRuntimeData::delete_objects()
{
    world().delete_objects();

    // If this is the active screen level, clear any stale control pointers
    // that may reference walkers just deleted above.
    if (hooks_ && hooks_->clear_stale_view_controls)
        hooks_->clear_stale_view_controls(this);

	    // Clear the obmap references
	    // Since the walker destructor removes itself from the obmap, this should be empty already.
	    if(world().myobmap->walker_to_pos.size() > 0)
	    {
	        Log("obmap::walker_to_pos has {} elements left.\n", world().myobmap->walker_to_pos.size());

	        // FIXME: Freeing them here does naughty things!
	        // obmap only indexes walkers; it doesn't own them. If we see leftovers here it usually
	        // means something mutated the obmap out-of-order, or walkers are being kept alive
	        // outside LevelRuntimeData's owning lists. We clear the index defensively below; do not
	        // attempt to delete walkers from the obmap to "fix" this (double-frees / UAF risk).
	    }
    // pos_to_walker will have a bunch of 0-size lists in it
	world().myobmap->pos_to_walker.clear();
	world().myobmap->walker_to_pos.clear();
}


namespace {

LevelRuntimeData::IoError map_level_file_error(og::data::LevelFileIoError err)
{
    switch (err)
    {
        case og::data::LevelFileIoError::None:
            return LevelRuntimeData::IoError::None;
        case og::data::LevelFileIoError::OpenReadFailed:
            return LevelRuntimeData::IoError::OpenReadFailed;
        case og::data::LevelFileIoError::OpenWriteFailed:
            return LevelRuntimeData::IoError::OpenWriteFailed;
        case og::data::LevelFileIoError::InvalidHeader:
            return LevelRuntimeData::IoError::InvalidHeader;
        case og::data::LevelFileIoError::UnsupportedVersion:
            return LevelRuntimeData::IoError::UnsupportedVersion;
        case og::data::LevelFileIoError::SerializeFailed:
            return LevelRuntimeData::IoError::SerializeFailed;
        case og::data::LevelFileIoError::ParseFailed:
        default:
            return LevelRuntimeData::IoError::ParseFailed;
    }
}

short load_version_bridge(og::io::OgFile& infile, LevelRuntimeData* data,
                          short version)
{
    if (data == nullptr)
        return 0;

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = data->grid_file;
    metadata.description = data->description;

    const short result = og::data::load_scenario_version(
        infile, &data->world(), &metadata, version);

    data->grid_file = std::move(metadata.grid_file);
    data->description = std::move(metadata.description);
    return result;
}

} // namespace

short load_version_2(og::io::OgFile& infile, LevelRuntimeData* data)
{
    return load_version_bridge(infile, data, 2);
}

short load_version_3(og::io::OgFile& infile, LevelRuntimeData* data)
{
    return load_version_bridge(infile, data, 3);
}

short load_version_4(og::io::OgFile& infile, LevelRuntimeData* data)
{
    return load_version_bridge(infile, data, 4);
}

short load_version_5(og::io::OgFile& infile, LevelRuntimeData* data)
{
    return load_version_bridge(infile, data, 5);
}

short load_version_6(og::io::OgFile& infile, LevelRuntimeData* data,
                     short version)
{
    return load_version_bridge(infile, data, version);
}

short load_scenario_version(og::io::OgFile& infile, LevelRuntimeData* data,
                            short version)
{
    return load_version_bridge(infile, data, version);
}

bool save_grid_file(const char* gridname, const PixieData& grid)
{
    return og::data::save_grid_file(gridname, grid);
}

bool LevelRuntimeData::load()
{
	TRACE("game", "LevelRuntimeData::load id=%d headless=%d", world().id, headless_ ? 1 : 0);
    last_io_error_ = IoError::None;

    wire_world_entity_services(world_, this, hooks_);

    std::string thefile = std::format("scen{}.fss", world().id);

    og::data::LevelFileMetadata loaded_metadata;
    loaded_metadata.grid_file = grid_file;
    loaded_metadata.description = description;

    ScopedCurrentGameWorldBinding world_binding;
    ScopedLoadWorldRngOverride load_rng_binding(&world().rng_);
    GameWorld loaded_world(world().rng_.state_);
    loaded_world.id = world().id;
    wire_world_entity_services(&loaded_world, this, hooks_);
    world_binding.bind(&loaded_world);

    og::data::LevelFileIoError io_error = og::data::LevelFileIoError::None;
    if (!og::data::load_level(thefile, loaded_world, loaded_metadata, &io_error))
    {
        last_io_error_ = map_level_file_error(io_error);
        return false;
    }

    // Restore active gameplay context before replacing world-owned state.
    world_binding.restore();

    replace_loaded_world_state(this, loaded_world);

    grid_file = std::move(loaded_metadata.grid_file);
    description = std::move(loaded_metadata.description);
    level_visuals().topx = 0;
    level_visuals().topy = 0;

    // Reload background tiles (only when rendering)
    if (!headless_)
    {
        level_visuals().renderer_.reset();
        for (int i = 0; i < PIX_MAX; i++)
            level_visuals().pixdata[i].free();

        load_map_data(level_visuals().pixdata);
        if (hooks_ && hooks_->create_level_render)
            level_visuals().renderer_ = hooks_->create_level_render(level_visuals().pixdata);
    }

	TRACE("game", "LevelRuntimeData::load complete");
    last_io_error_ = IoError::None;
	return true;
}

bool LevelRuntimeData::save()
{
    last_io_error_ = IoError::None;

	// Zardus: PORT: no longer need to put in scen/ in this part
	std::string temp_filename = std::format("scen{}.fss", this->world().id);

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = grid_file;
    metadata.description = description;

    og::data::LevelFileIoError io_error = og::data::LevelFileIoError::None;
    if (!og::data::save_level(world(), temp_filename, metadata, &io_error))
    {
        last_io_error_ = map_level_file_error(io_error);
        return false;
    }

    // Editor saves can rewrite the scenario title.
    og::data::clear_campaign_metadata_cache();

    last_io_error_ = IoError::None;
	return true;
}

LevelRuntimeData::IoError LevelRuntimeData::load_with_error()
{
    load();
    return last_io_error_;
}

LevelRuntimeData::IoError LevelRuntimeData::save_with_error()
{
    save();
    return last_io_error_;
}
void LevelRuntimeData::set_draw_pos(std::int32_t new_topx, std::int32_t new_topy)
{
    level_visuals().topx = new_topx;
    level_visuals().topy = new_topy;
}

void LevelRuntimeData::add_draw_pos(std::int32_t dx, std::int32_t dy)
{
    level_visuals().topx += dx;
    level_visuals().topy += dy;
}

void LevelRuntimeData::draw(screen* screenp)
{
    if (!screenp) return;
    if (hooks_ && hooks_->draw)
        hooks_->draw(this, screenp);
}

std::string LevelRuntimeData::get_description_line(int i) const
{
    // Guard the empty-list case explicitly: with a negative i the size check
    // below is bypassed and *begin() would dereference end() on an empty list.
    // A non-empty list with negative i still returns the first line (tested).
    if(description.empty() || i >= int(description.size()))
        return "";

    std::list<std::string>::const_iterator e = description.begin();
    while(i > 0 && e != description.end())
    {
        i--;
        e++;
    }
    return *e;
}

// ---- Scenario title reader (moved from screen for sim/render split) ----

std::string get_scenario_title(const char* filename)
{
    return og::data::load_scenario_title(filename);
}

// ---- remaining_foes (moved from glad_gameplay for sim/render split) ----

short remaining_foes(LevelRuntimeData& level, walker* myguy)
{
    return level.world().remaining_foes(myguy);
}

// ---- Collision / passability queries ----

bool LevelRuntimeData::query_grid_passable(float x, float y, walker  *ob)
{
    return world().query_grid_passable(x, y, ob);
}

bool LevelRuntimeData::query_object_passable(float x, float y, walker  *ob)
{
    return world().query_object_passable(x, y, ob);
}

bool LevelRuntimeData::query_passable(float x, float y, walker  *ob)
{
    return world().query_passable(x, y, ob);
}

// ---- Entity search ----

walker *LevelRuntimeData::find_near_foe(walker  *ob)
{
    return world().find_near_foe(ob);
}

walker  *LevelRuntimeData::find_far_foe(walker  *ob)
{
    return world().find_far_foe(ob);
}

walker  * LevelRuntimeData::find_nearest_blood(walker  *who)
{
    return world().find_nearest_blood(who);
}

walker* LevelRuntimeData::find_nearest_player(walker *ob)
{
    return world().find_nearest_player(ob);
}

std::list<walker*> LevelRuntimeData::find_in_range(const std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob)
{
    return world().find_in_range(somelist, range, howmany, ob);
}

std::list<walker*> LevelRuntimeData::find_foes_in_range(const std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob)
{
    return world().find_foes_in_range(somelist, range, howmany, ob);
}

std::list<walker*> LevelRuntimeData::find_foe_weapons_in_range(const std::list<std::unique_ptr<walker>>& somelist, std::int32_t range,
                                                               std::int32_t* howmany, walker* ob)
{
    return world().find_foe_weapons_in_range(somelist, range, howmany, ob);
}

std::list<walker*> LevelRuntimeData::find_friends_in_range(const std::list<std::unique_ptr<walker>>& somelist, std::int32_t range,
                                                           std::int32_t* howmany, walker* ob)
{
    return world().find_friends_in_range(somelist, range, howmany, ob);
}
