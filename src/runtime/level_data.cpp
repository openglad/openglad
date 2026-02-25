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

#include <openglad/data/level_data.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/platform/io_common.h>
#include <openglad/io/og_file.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/data/smooth.h>
#include <openglad/entities/obmap.h>
#include <openglad/core/util.h>

#include <openglad/io/yaml_stream.h>
#include <openglad/io/ogfile_yaml.h>
#include <openglad/runtime/game_context.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/data/level_data_hooks.h>
#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <utility>


int toInt(const std::string& s);

void LevelData::set_sim_context(SaveData* save, std::int32_t* enemy_freeze,
                                og::sim::SimEventLog* events, IRandom* rng,
                                cfg_store* config)
{
    (void)save;
    (void)enemy_freeze;
    (void)rng;
    (void)config;
    sim_ctx_events_ = events;

    // Synchronize when this LevelData is already active, or when the current
    // gameplay context has no world bound yet (unit/headless fixtures).
    if (og::gameplay::current_game &&
        (og::gameplay::current_game->world == nullptr ||
         og::gameplay::current_game->world == &world_ref_))
    {
        og::gameplay::current_game->world = &world_ref_;
        og::gameplay::current_game->sim_events = events;
    }
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
        auto file = og::io::og_open_read("campaign.yaml", true);
        if(!file)
        {
            last_io_error_ = IoError::OpenReadFailed;
            (void)unmount_campaign_package_with_error(id);
            (void)mount_campaign_package_with_error(old_campaign);
            return false;
        }

        og::io::YamlParser yaml;
        yaml.set_input(ogfile_read_handler, file.get());

        auto parse_result = og::io::YamlParseResult::Ok;
        while((parse_result = yaml.parse_next()) == og::io::YamlParseResult::Ok)
        {
            const og::io::YamlEvent& ev = yaml.event();
            switch(ev.type)
            {
                case og::io::YamlEventType::Pair:
                    if(ev.scalar == "title")
                        title = ev.value;
                    else if(ev.scalar == "version")
                        version = ev.value;
                    else if(ev.scalar == "authors")
                        authors = ev.value;
                    else if(ev.scalar == "contributors")
                        contributors = ev.value;
                    else if(ev.scalar == "description")
                    {
                        std::string desc = ev.value;
                        description = explode(desc, '\n');
                    }
                    else if(ev.scalar == "suggested_power")
                        suggested_power = toInt(ev.value);
                    else if(ev.scalar == "first_level")
                        first_level = toInt(ev.value);
                break;
                default:
                    break;
            }
        }
        if(parse_result == og::io::YamlParseResult::Error)
            last_io_error_ = IoError::ParseFailed;

        yaml.close_input();

        // TODO: Get rating from website
        rating = 0.0f;

        icondata = read_pixie_file("icon.pix");

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
        
        auto outfile = og::io::og_open_write("temp/campaign.yaml");
        if(outfile)
        {
            og::io::YamlEmitter yaml;
            if (!yaml.set_output(ogfile_write_handler, outfile.get()))
            {
                LogError("Couldn't initialize YAML emitter for temp/campaign.yaml.\n");
                last_io_error_ = IoError::OpenWriteFailed;
                cleanup_unpacked_campaign();
                return false;
            }

            yaml.emit_pair("format_version", "1");
            yaml.emit_pair("title", title.c_str());
            yaml.emit_pair("version", version.c_str());

            std::string buf = std::format("{}", first_level);
            yaml.emit_pair("first_level", buf.c_str());

            buf = std::format("{}", suggested_power);
            yaml.emit_pair("suggested_power", buf.c_str());

            yaml.emit_pair("authors", authors.c_str());
            yaml.emit_pair("contributors", contributors.c_str());

            std::string desc;
            for(auto e = description.begin(); e != description.end();)
            {
                desc += *e;
                e++;
                if(e != description.end())
                    desc += '\n';
            }

            yaml.emit_pair("description", desc.c_str());

            yaml.close_output();
        }
        else
        {
            Log("Couldn't open temp/campaign.yaml for writing.\n");
            result = false;
            last_io_error_ = IoError::OpenWriteFailed;
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
        last_io_error_ = IoError::None;
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
        auto outfile = og::io::og_open_write("temp/campaign.yaml");
        if(outfile)
        {
            og::io::YamlEmitter yaml;
            if (!yaml.set_output(ogfile_write_handler, outfile.get()))
            {
                LogError("Couldn't initialize YAML emitter for temp/campaign.yaml.\n");
                result = false;
                last_io_error_ = IoError::OpenWriteFailed;
            }

            if (result)
            {
                yaml.emit_pair("format_version", "1");
                yaml.emit_pair("title", title.c_str());
                yaml.emit_pair("version", version.c_str());

                std::string buf = std::format("{}", first_level);
                yaml.emit_pair("first_level", buf.c_str());

                buf = std::format("{}", suggested_power);
                yaml.emit_pair("suggested_power", buf.c_str());

                yaml.emit_pair("authors", authors.c_str());
                yaml.emit_pair("contributors", contributors.c_str());

                std::string desc;
                for(auto e = description.begin(); e != description.end();)
                {
                    desc += *e;
                    e++;
                    if(e != description.end())
                        desc += '\n';
                }

                yaml.emit_pair("description", desc.c_str());

                yaml.close_output();
            }
        }
        else
        {
            Log("Couldn't open temp/campaign.yaml for writing.\n");
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
        last_io_error_ = IoError::None;
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





LevelData::LevelData(int level_id)
    : LevelData(level_id, false, nullptr, nullptr)
{
}

LevelData::LevelData(int level_id, const LevelDataHooks* hooks)
    : LevelData(level_id, false, hooks, nullptr)
{
}

LevelData::LevelData(int level_id, bool headless)
    : LevelData(level_id, headless, nullptr, nullptr)
{
}

LevelData::LevelData(int level_id, bool headless, const LevelDataHooks* hooks)
    : LevelData(level_id, headless, hooks, nullptr)
{
}

LevelData::LevelData(int level_id, bool headless, const LevelDataHooks* hooks,
                     og::gameplay::GameWorld* external_world)
    : owned_world_(external_world ? nullptr : std::make_unique<og::gameplay::GameWorld>())
    , world_ref_(external_world ? *external_world : *owned_world_)
    , id(world_ref_.id)
    , title(world_ref_.title)
    , type(world_ref_.type)
    , par_value(world_ref_.par_value)
    , time_bonus_limit(world_ref_.time_bonus_limit)
    , difficulty(world_ref_.difficulty)
    , grid(world_ref_.grid)
    , pixmaxx(world_ref_.pixmaxx)
    , pixmaxy(world_ref_.pixmaxy)
    , level_done(world_ref_.level_done)
    , mysmoother(world_ref_.mysmoother)
    , myloader(nullptr)
    , numobs(world_ref_.living_count)
    , oblist(world_ref_.oblist)
    , fxlist(world_ref_.fxlist)
    , weaplist(world_ref_.weaplist)
    , dead_list(world_ref_.dead_list)
    , myobmap(world_ref_.myobmap)
{
    hooks_ = hooks;
    headless_ = headless;
    restore_external_world_callbacks_ = false;

    id = level_id;

    if (!myobmap)
	    myobmap = std::make_unique<obmap>();
    if (!external_world)
    {
        myloader = std::make_unique<loader>();
        wire_entity_factory_callbacks();
    }

}

LevelData::~LevelData()
{
    if (og::gameplay::current_game &&
        og::gameplay::current_game->world == &world_ref_)
    {
        if (og::gameplay::current_game->sim_events == sim_ctx_events_)
            og::gameplay::current_game->sim_events = nullptr;
        og::gameplay::current_game->world = nullptr;
    }

    // External-world adapters are non-owning views. They must not tear down
    // screen/world resources when the adapter goes out of scope.
    if (owned_world_)
    {
        delete_objects();
        delete_grid();
        world_ref_.entity_factory = nullptr;
        world_ref_.entity_configure = nullptr;
        world_ref_.entity_derived_stats = nullptr;
        world_ref_.entity_graphics = nullptr;
        myobmap.reset();
    }
    else if (restore_external_world_callbacks_)
    {
        world_ref_.entity_factory = std::move(prev_entity_factory_);
        world_ref_.entity_configure = std::move(prev_entity_configure_);
        world_ref_.entity_derived_stats = std::move(prev_entity_derived_stats_);
        world_ref_.entity_graphics = std::move(prev_entity_graphics_);
    }

    myloader.reset();

}

void LevelData::clear()
{
    world_ref_.clear();

}

walker* LevelData::add_ob(Order order, std::int32_t family, bool atstart)
{
    return world_ref_.add_ob(order, family, atstart);
}

walker* LevelData::add_fx_ob(Order order, std::int32_t family)
{
    return world_ref_.add_fx_ob(order, family);
}

walker* LevelData::add_weap_ob(Order order, std::int32_t family)
{
    return world_ref_.add_weap_ob(order, family);
}

void LevelData::wire_entity(walker* w)
{
    (void)w;
}

void LevelData::wire_entity_factory_callbacks()
{
    if (!myloader)
    {
        world_ref_.entity_factory = nullptr;
        world_ref_.entity_configure = nullptr;
        world_ref_.entity_derived_stats = nullptr;
        world_ref_.entity_graphics = nullptr;
        return;
    }

    EntityFactory factory;
    if (!headless_)
    {
        factory.attach_render = [](walker& w, const PixieData& data) {
            w.attach_render(data);
        };
    }
    myloader->set_entity_factory(std::move(factory));

    world_ref_.entity_factory = [this](Order order, int family) -> std::unique_ptr<walker> {
        if (!myloader)
            return nullptr;
        auto w = myloader->create_walker_owned(order, family);
        if (!w)
            return nullptr;
        wire_entity(w.get());
        return w;
    };
    world_ref_.entity_configure = [this](walker* w, Order order, int family) -> walker* {
        if (!myloader || !w)
            return nullptr;
        return myloader->set_walker(w, order, family);
    };
    world_ref_.entity_derived_stats = [this](walker* w, Order order, int family) {
        if (!myloader || !w)
            return;
        myloader->set_derived_stats(w, order, family);
    };
    world_ref_.entity_graphics = [this](Order order, int family) -> const PixieData* {
        if (!myloader)
            return nullptr;
        if (family < 0 || family >= NUM_FAMILIES)
            return nullptr;
        const int order_i = static_cast<int>(order);
        constexpr int kOrderCount = static_cast<int>(Order::Button1) + 1;
        if (order_i < 0 || order_i >= kOrderCount)
            return nullptr;
        const PixieData& data = myloader->graphics[PIX(order, family)];
        return data.valid() ? &data : nullptr;
    };
}

short LevelData::remove_ob(walker  *ob)
{
	if (ob && ob->query_order() == Order::Living)
		numobs--;

    auto pred = [ob](const std::unique_ptr<walker>& p) { return p.get() == ob; };

    auto e = std::find_if(weaplist.begin(), weaplist.end(), pred);
    if(e != weaplist.end())
    {
        weaplist.erase(e);
        return 1;
    }

    auto f = std::find_if(fxlist.begin(), fxlist.end(), pred);
    if(f != fxlist.end())
    {
        fxlist.erase(f);
        return 1;
    }

    auto g = std::find_if(oblist.begin(), oblist.end(), pred);
    if(g != oblist.end())
    {
        oblist.erase(g);
        return 1;
    }

	return 0;
}

void LevelData::delete_grid() { world_ref_.delete_grid(); }

void LevelData::create_new_grid() { world_ref_.create_new_grid(); }

void LevelData::resize_grid(int width, int height) { world_ref_.resize_grid(width, height); }

void LevelData::delete_objects()
{
	world_ref_.delete_objects();

    // If this is the active screen level, clear any stale control pointers
    // that may reference walkers just deleted above.
    if (hooks_ && hooks_->clear_stale_view_controls)
        hooks_->clear_stale_view_controls(&world_ref_);

	    // Clear the obmap references
	    // Since the walker destructor removes itself from the obmap, this should be empty already.
	    if(myobmap->walker_to_pos.size() > 0)
	    {
	        Log("obmap::walker_to_pos has {} elements left.\n", myobmap->walker_to_pos.size());

	        // FIXME: Freeing them here does naughty things!
	        // obmap only indexes walkers; it doesn't own them. If we see leftovers here it usually
	        // means something mutated the obmap out-of-order, or walkers are being kept alive
	        // outside LevelData's owning lists. We clear the index defensively below; do not
	        // attempt to delete walkers from the obmap to "fix" this (double-frees / UAF risk).
	    }
    // pos_to_walker will have a bunch of 0-size lists in it
	myobmap->pos_to_walker.clear();
	myobmap->walker_to_pos.clear();
}

namespace {

LevelData::IoError map_level_file_error(og::data::LevelFileIoError err)
{
    switch (err)
    {
        case og::data::LevelFileIoError::None:
            return LevelData::IoError::None;
        case og::data::LevelFileIoError::OpenReadFailed:
            return LevelData::IoError::OpenReadFailed;
        case og::data::LevelFileIoError::OpenWriteFailed:
            return LevelData::IoError::OpenWriteFailed;
        case og::data::LevelFileIoError::InvalidHeader:
            return LevelData::IoError::InvalidHeader;
        case og::data::LevelFileIoError::UnsupportedVersion:
            return LevelData::IoError::UnsupportedVersion;
        case og::data::LevelFileIoError::SerializeFailed:
            return LevelData::IoError::SerializeFailed;
        case og::data::LevelFileIoError::ParseFailed:
        default:
            return LevelData::IoError::ParseFailed;
    }
}

LevelVisuals& fallback_level_visuals(LevelVisuals* visuals)
{
    static LevelVisuals fallback;
    return visuals ? *visuals : fallback;
}

} // namespace

bool LevelData::load(LevelVisuals* visuals)
{
    TRACE("game", "LevelData::load id=%d headless=%d", id, headless_ ? 1 : 0);
    last_io_error_ = IoError::None;

    // Clear any stale view control pointers before world data is replaced.
    delete_objects();

    if (!myloader)
        myloader = std::make_unique<loader>();
    wire_entity_factory_callbacks();

    std::string thefile = std::format("scen{}.fss", id);

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = grid_file;
    metadata.description = description;

    og::data::LevelFileIoError io_error = og::data::LevelFileIoError::None;
    if (!og::data::load_level(thefile, world_ref_, fallback_level_visuals(visuals),
                              metadata, &io_error))
    {
        last_io_error_ = map_level_file_error(io_error);
        return false;
    }

    grid_file = metadata.grid_file;
    description = metadata.description;

    TRACE("game", "LevelData::load complete");
    last_io_error_ = IoError::None;
    return true;
}

bool LevelData::save(LevelVisuals* visuals)
{
    last_io_error_ = IoError::None;

    std::string temp_filename = std::format("scen{}.fss", id);

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = grid_file;
    metadata.description = description;

    og::data::LevelFileIoError io_error = og::data::LevelFileIoError::None;
    if (!og::data::save_level(world_ref_, fallback_level_visuals(visuals),
                              temp_filename, metadata, &io_error))
    {
        last_io_error_ = map_level_file_error(io_error);
        return false;
    }

    last_io_error_ = IoError::None;
    return true;
}

LevelData::IoError LevelData::load_with_error(LevelVisuals* visuals)
{
    load(visuals);
    return last_io_error_;
}

LevelData::IoError LevelData::save_with_error(LevelVisuals* visuals)
{
    save(visuals);
    return last_io_error_;
}

std::string LevelData::get_description_line(int i)
{
    if(i >= int(description.size()))
        return "";

    std::list<std::string>::iterator e = description.begin();
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

short remaining_foes(LevelData& level, walker* myguy)
{
    return level.game_world().remaining_foes(myguy);
}

// ---- Collision / passability queries ----

bool LevelData::query_grid_passable(float x, float y, walker  *ob)
{
    return world_ref_.query_grid_passable(x, y, ob);
}

bool LevelData::query_object_passable(float x, float y, walker  *ob)
{
    return world_ref_.query_object_passable(x, y, ob);
}

bool LevelData::query_passable(float x, float y, walker  *ob)
{
    return world_ref_.query_passable(x, y, ob);
}

// ---- Entity search ----

walker *LevelData::find_near_foe(walker  *ob)
{
    return world_ref_.find_near_foe(ob);
}

walker  *LevelData::find_far_foe(walker  *ob)
{
    return world_ref_.find_far_foe(ob);
}

walker  * LevelData::find_nearest_blood(walker  *who)
{
    return world_ref_.find_nearest_blood(who);
}

walker* LevelData::find_nearest_player(walker *ob)
{
    return world_ref_.find_nearest_player(ob);
}

std::list<walker*> LevelData::find_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob)
{
    return world_ref_.find_in_range(somelist, range, howmany, ob);
}

std::list<walker*> LevelData::find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob)
{
    return world_ref_.find_foes_in_range(somelist, range, howmany, ob);
}

std::list<walker*> LevelData::find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range,
                                      std::int32_t* howmany, walker* ob)
{
    return world_ref_.find_foe_weapons_in_range(somelist, range, howmany, ob);
}

std::list<walker*> LevelData::find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range,
                                      std::int32_t* howmany, walker* ob)
{
    return world_ref_.find_friends_in_range(somelist, range, howmany, ob);
}
