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

#include <openglad/runtime/level_runtime_data.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/platform/io_common.h>
#include <openglad/io/og_file.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/data/smooth.h>
#include <openglad/entities/obmap.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

#include <openglad/io/yaml_stream.h>
#include <openglad/io/ogfile_yaml.h>
#include <openglad/data/level_render.h>
#include <openglad/data/level_data_hooks.h>
#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <string_view>


int toInt(const std::string& s);

void LevelRuntimeData::set_sim_context(SaveData* save, std::int32_t* enemy_freeze,
                                og::sim::SimEventLog* events, IRandom* rng,
                                cfg_store* config)
{
    (void)save;
    (void)config;
    (void)enemy_freeze;
    (void)rng;
    (void)events;
}

static constexpr char VERSION_NUM = 9; // save scenario type info

static constexpr short MAX_SCENARIO_OBJECTS = 4096;

static bool rw_read_exact_or_log(og::io::OgFile& file, void* dst, size_t size, size_t count)
{
    const size_t got = file.read(dst, size, count);
    if (got != count)
    {
        Log("Read error: expected {} items, got {}\n", count, got);
        return false;
    }
    return true;
}

static unsigned char sanitize_loaded_team_num(unsigned char team_num)
{
    if (team_num <= MAX_TEAM)
        return team_num;
    LogWarn("Scenario object uses invalid team id {}. Clamping to team 0.\n", static_cast<int>(team_num));
    return 0;
}

static void fill_fixed_field(char* dst, size_t fixed_len, std::string_view src, const char* field_name)
{
    if (dst == nullptr || fixed_len == 0)
        return;

    memset(dst, 0, fixed_len);
    const size_t to_copy = std::min(src.size(), fixed_len);
    memcpy(dst, src.data(), to_copy);
    if (src.size() > fixed_len)
        LogWarn("Truncating {} to {} bytes for scenario serialization.\n", field_name, fixed_len);
}

static std::string ensure_pix_extension(std::string_view name)
{
    // Scenario files store an 8-byte "grid name" field; some content uses "foo",
    // others may include "foo.pix". Be tolerant and avoid ".pix.pix".
    std::string s(name);
    if (s.size() >= 4 && s.compare(s.size() - 4, 4, ".pix") == 0)
        return s;
    return s + ".pix";
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
        return game_loader->graphics_for(entity.query_order(), entity.family);
    };

    world.entity_derived_stats = [game_loader](walker* entity, Order order, std::int32_t family) {
        if (entity == nullptr || !game_loader)
            return;
        game_loader->set_derived_stats(entity, order, family);
    };
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

LevelRuntimeData::LevelRuntimeData(int level_id, bool headless, const LevelDataHooks* hooks, LevelVisuals* visuals)
    : level_done(this)
    , numobs(this)
    , oblist(this, WalkerListForwarder::Kind::Objects)
    , fxlist(this, WalkerListForwarder::Kind::Effects)
    , weaplist(this, WalkerListForwarder::Kind::Weapons)
    , dead_list(this, WalkerListForwarder::Kind::Dead)
    , world_(&owned_world_)
    , level_visuals_(visuals ? visuals : &owned_level_visuals_)
    , pixdata(level_visuals_->pixdata)
    , renderer_(level_visuals_->renderer_)
    , topx(level_visuals_->topx)
    , topy(level_visuals_->topy)
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
        next_world->current_scenario = old_world->current_scenario;
        next_world->completed_levels = old_world->completed_levels;
        next_world->oblist.splice(next_world->oblist.end(), old_world->oblist);
        next_world->fxlist.splice(next_world->fxlist.end(), old_world->fxlist);
        next_world->weaplist.splice(next_world->weaplist.end(), old_world->weaplist);
        next_world->dead_list.splice(next_world->dead_list.end(), old_world->dead_list);
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
        old_world->mysmoother.reset();
        if (!old_world->myobmap)
            old_world->myobmap = std::make_unique<obmap>();
        old_world->set_detach_callback({});
        clear_world_entity_services(old_world);
    }

    world_ = next_world;
    wire_world_entity_services(world_, this, hooks_);
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

short load_version_2(og::io::OgFile& infile, LevelRuntimeData* data)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[9] = "grid";  // default grid (8-byte field, no implicit NUL)

	// Format of a scenario object list file version 2 is:
	// 3-byte header: 'FSS'
	// 1-byte version #
	// ----- (above is already determined by now)
	// 8-byte string = grid name to load
	// 2-bytes (short) = total objects to follow
	// List of n objects, each of 7-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte short xpos
	// 2-byte short ypos
	// 1-byte TEAM
	// 1-byte facing
	// 1-byte command
	// ---
	// 11 bytes reserved

	// Get grid file to load
	if (!rw_read_exact_or_log(infile, newgrid, 8, 1))
		return 0;
	newgrid[8] = '\0';
	//buffers: PORT: make sure grid name is lowercase
	lowercase(newgrid);
	data->grid_file = newgrid;

	// Determine number of objects to load ...
	if (!rw_read_exact_or_log(infile, &listsize, 2, 1))
		return 0;
	if (listsize < 0 || listsize > MAX_SCENARIO_OBJECTS)
	{
		Log("Invalid scenario object count: {}\n", listsize);
		return 0;
	}
	
    data->delete_objects();

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		if (!rw_read_exact_or_log(infile, &temporder, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempfamily, 1, 1) ||
		    !rw_read_exact_or_log(infile, &currentx, 2, 1) ||
		    !rw_read_exact_or_log(infile, &currenty, 2, 1) ||
		    !rw_read_exact_or_log(infile, &tempteam, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempfacing, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempcommand, 1, 1) ||
		    !rw_read_exact_or_log(infile, tempreserved, 11, 1))
			return 0;
		if (static_cast<Order>(temporder) == Order::Treasure)
			new_guy = data->add_fx_ob(static_cast<Order>(temporder), tempfamily);  // create new object
		else
			new_guy = data->add_ob(static_cast<Order>(temporder), tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			return 0;
		}
		new_guy ->setxy(currentx, currenty);
		//       Log("X: %d  Y: %d  \n", currentx, currenty);
		new_guy->team_num = sanitize_loaded_team_num(tempteam);
	}

	// Now read the grid file to our master screen ..
	std::string gridpix = ensure_pix_extension(newgrid);

    data->delete_grid();

	data->world().grid = read_pixie_file(gridpix.c_str());
	data->world().pixmaxx = data->world().grid.w * GRID_SIZE;
	data->world().pixmaxy = data->world().grid.h * GRID_SIZE;

	return 1;
}

// Version 3 scenarios have a block of text which can be displayed
// at the start, etc.  Format is
// # of lines,
//  1-byte character width
//  n bytes specified from above
short load_version_3(og::io::OgFile& infile, LevelRuntimeData* data)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char templevel;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[9] = "grid";  // default grid (8-byte field, no implicit NUL)
	char oneline[80];
	char numlines, tempwidth;


	// Format of a scenario object list file version 2 is:
	// 3-byte header: 'FSS'
	// 1-byte version #
	// ----- (above is already determined by now)
	// 8-byte string = grid name to load
	// 2-bytes (short) = total objects to follow
	// List of n objects, each of 7-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte short xpos
	// 2-byte short ypos
	// 1-byte TEAM
	// 1-byte facing
	// 1-byte command
	// 1-byte level
	// ---
	// 10 bytes reserved
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line


	// Get grid file to load
	if (!rw_read_exact_or_log(infile, newgrid, 8, 1))
		return 0;
	newgrid[8] = '\0';
	//buffers: PORT: make sure grid name is lowercase
	lowercase(newgrid);
	data->grid_file = newgrid;

	// Determine number of objects to load ...
	if (!rw_read_exact_or_log(infile, &listsize, 2, 1))
		return 0;
	if (listsize < 0 || listsize > MAX_SCENARIO_OBJECTS)
	{
		Log("Invalid scenario object count: {}\n", listsize);
		return 0;
	}
	
    data->delete_objects();

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		if (!rw_read_exact_or_log(infile, &temporder, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempfamily, 1, 1) ||
		    !rw_read_exact_or_log(infile, &currentx, 2, 1) ||
		    !rw_read_exact_or_log(infile, &currenty, 2, 1) ||
		    !rw_read_exact_or_log(infile, &tempteam, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempfacing, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempcommand, 1, 1) ||
		    !rw_read_exact_or_log(infile, &templevel, 1, 1) ||
		    !rw_read_exact_or_log(infile, tempreserved, 10, 1))
			return 0;
		if (static_cast<Order>(temporder) == Order::Treasure)
			//              new_guy = master->add_fx_ob(static_cast<Order>(temporder), tempfamily);  // create new object
			new_guy = data->add_ob(static_cast<Order>(temporder), tempfamily, 1); // add to top of list
		else
			new_guy = data->add_ob(static_cast<Order>(temporder), tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = sanitize_loaded_team_num(tempteam);
		new_guy->stats()->level = templevel;
	}

	// Now get the lines of text to read ..
	if (!rw_read_exact_or_log(infile, &numlines, 1, 1))
		return 0;

	for (i=0; i < numlines; i++)
	{
		if (!rw_read_exact_or_log(infile, &tempwidth, 1, 1))
			return 0;
		const int original_width = static_cast<unsigned char>(tempwidth);
		int width = original_width;
		if (width >= static_cast<int>(sizeof(oneline)))
			width = sizeof(oneline) - 1;
		if (width > 0)
		{
			if (!rw_read_exact_or_log(infile, oneline, width, 1))
				return 0;
			oneline[width] = 0;
			// Skip any remaining bytes to keep the stream aligned
			if (original_width > width)
			{
				char discard[256];
				int remaining = original_width - width;
				while (remaining > 0)
				{
					const int chunk = remaining < static_cast<int>(sizeof(discard)) ? remaining : static_cast<int>(sizeof(discard));
					if (!rw_read_exact_or_log(infile, discard, chunk, 1))
						return 0;
					remaining -= chunk;
				}
			}
		}
		else
		{
			oneline[0] = 0;
		}
		data->description.push_back(oneline);
	}


	// Now read the grid file to our master screen ..
	std::string gridpix2 = ensure_pix_extension(newgrid);

    data->delete_grid();

	data->world().grid = read_pixie_file(gridpix2.c_str());
	data->world().pixmaxx = data->world().grid.w * GRID_SIZE;
	data->world().pixmaxy = data->world().grid.h * GRID_SIZE;

	return 1;
}

// Version 4 scenarios include a 12-byte name for EVERY walker..
short load_version_4(og::io::OgFile& infile, LevelRuntimeData* data)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char templevel;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[9] = "grid";  // default grid (8-byte field, no implicit NUL)
	char oneline[80] = {};
	char numlines, tempwidth;
	char tempname[13] = {};


	// Format of a scenario object list file version 4 is:
	// 3-byte header: 'FSS'
	// 1-byte version #
	// ----- (above is already determined by now)
	// 8-byte string = grid name to load
	// 2-bytes (short) = total objects to follow
	// List of n objects, each of 7-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte short xpos
	// 2-byte short ypos
	// 1-byte TEAM
	// 1-byte facing
	// 1-byte command
	// 1-byte level
	// 12-bytes name
	// ---
	// 10 bytes reserved
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line


	// Get grid file to load
	if (!rw_read_exact_or_log(infile, newgrid, 8, 1))
		return 0;
	newgrid[8] = '\0';
	//buffers: PORT: make sure grid name is lowercase
	lowercase(newgrid);
	data->grid_file = newgrid;

	// Determine number of objects to load ...
	if (!rw_read_exact_or_log(infile, &listsize, 2, 1))
		return 0;
	if (listsize < 0 || listsize > MAX_SCENARIO_OBJECTS)
	{
		Log("Invalid scenario object count: {}\n", listsize);
		return 0;
	}
	
    data->delete_objects();

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		if (!rw_read_exact_or_log(infile, &temporder, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempfamily, 1, 1) ||
		    !rw_read_exact_or_log(infile, &currentx, 2, 1) ||
		    !rw_read_exact_or_log(infile, &currenty, 2, 1) ||
		    !rw_read_exact_or_log(infile, &tempteam, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempfacing, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempcommand, 1, 1) ||
		    !rw_read_exact_or_log(infile, &templevel, 1, 1) ||
		    !rw_read_exact_or_log(infile, tempname, 12, 1) ||
		    !rw_read_exact_or_log(infile, tempreserved, 10, 1))
			return 0;
		tempname[12] = '\0';
		if (static_cast<Order>(temporder) == Order::Treasure)
			//new_guy = data->add_ob(static_cast<Order>(temporder), tempfamily, 1); // add to top of list
			new_guy = data->add_fx_ob(static_cast<Order>(temporder), tempfamily);
		else
			new_guy = data->add_ob(static_cast<Order>(temporder), tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = sanitize_loaded_team_num(tempteam);
		new_guy->stats()->level = templevel;
		new_guy->stats()->name = tempname;
		if (new_guy->stats()->name.size() > 1)           //chad 5/25/95
			new_guy->stats()->set_bit_flags(BIT_NAMED, 1);

	}

	// Now get the lines of text to read ..
	if (!rw_read_exact_or_log(infile, &numlines, 1, 1))
		return 0;

	for (i=0; i < numlines; i++)
	{
		if (!rw_read_exact_or_log(infile, &tempwidth, 1, 1))
			return 0;
		const int original_width = static_cast<unsigned char>(tempwidth);
		int width = original_width;
		if (width >= static_cast<int>(sizeof(oneline)))
			width = sizeof(oneline) - 1;
		if (width > 0)
		{
			if (!rw_read_exact_or_log(infile, oneline, width, 1))
				return 0;
			oneline[width] = 0;
			// Skip any remaining bytes to keep the stream aligned
			if (original_width > width)
			{
				char discard[256];
				int remaining = original_width - width;
				while (remaining > 0)
				{
					const int chunk = remaining < static_cast<int>(sizeof(discard)) ? remaining : static_cast<int>(sizeof(discard));
					if (!rw_read_exact_or_log(infile, discard, chunk, 1))
						return 0;
					remaining -= chunk;
				}
			}
		}
		else
		{
			oneline[0] = 0;
		}
		data->description.push_back(oneline);
	}

	// Now read the grid file...
	std::string gridpix3 = ensure_pix_extension(newgrid);

    data->delete_grid();

	data->world().grid = read_pixie_file(gridpix3.c_str());
	data->world().pixmaxx = data->world().grid.w * GRID_SIZE;
	data->world().pixmaxy = data->world().grid.h * GRID_SIZE;

	return 1;
} // end load_version_4

// Version 5 scenarios include a 1-byte 'scenario-type' specifier after
// the grid name.
short load_version_5(og::io::OgFile& infile, LevelRuntimeData* data)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char templevel;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[9] = "grid";  // default grid (8-byte field, no implicit NUL)
	char new_scen_type; // read the scenario type
	char oneline[80] = {};
	char numlines, tempwidth;
	char tempname[13] = {};

	// Format of a scenario object list file version 5 is:
	// 3-byte header: 'FSS'
	// 1-byte version #
	// ----- (above is already determined by now)
	// 8-byte string = grid name to load
	// 1-byte char = scenario type, default is 0
	// 2-bytes (short) = total objects to follow
	// List of n objects, each of 7-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte short xpos
	// 2-byte short ypos
	// 1-byte TEAM
	// 1-byte facing
	// 1-byte command
	// 1-byte level
	// 12-bytes name
	// ---
	// 10 bytes reserved
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line


	// Get grid file to load
	if (!rw_read_exact_or_log(infile, newgrid, 8, 1))
		return 0;
	newgrid[8] = '\0';
	//buffers: PORT: make sure grid name is lowercase
	lowercase(newgrid);
	data->grid_file = newgrid;

	// Get the scenario type information
	if (!rw_read_exact_or_log(infile, &new_scen_type, 1, 1))
		return 0;
	data->world().type = new_scen_type;

	// Determine number of objects to load ...
	if (!rw_read_exact_or_log(infile, &listsize, 2, 1))
		return 0;
	if (listsize < 0 || listsize > MAX_SCENARIO_OBJECTS)
	{
		Log("Invalid scenario object count: {}\n", listsize);
		return 0;
	}
	
    data->delete_objects();

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		if (!rw_read_exact_or_log(infile, &temporder, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempfamily, 1, 1) ||
		    !rw_read_exact_or_log(infile, &currentx, 2, 1) ||
		    !rw_read_exact_or_log(infile, &currenty, 2, 1) ||
		    !rw_read_exact_or_log(infile, &tempteam, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempfacing, 1, 1) ||
		    !rw_read_exact_or_log(infile, &tempcommand, 1, 1) ||
		    !rw_read_exact_or_log(infile, &templevel, 1, 1) ||
		    !rw_read_exact_or_log(infile, tempname, 12, 1) ||
		    !rw_read_exact_or_log(infile, tempreserved, 10, 1))
			return 0;
		tempname[12] = '\0';
		if (static_cast<Order>(temporder) == Order::Treasure)
			new_guy = data->add_fx_ob(static_cast<Order>(temporder), tempfamily);
		else
			new_guy = data->add_ob(static_cast<Order>(temporder), tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = sanitize_loaded_team_num(tempteam);
		new_guy->stats()->level = templevel;
		new_guy->stats()->name = tempname;
		if (new_guy->stats()->name.size() > 1)           //chad 5/25/95
			new_guy->stats()->set_bit_flags(BIT_NAMED, 1);

	}

	// Now get the lines of text to read ..
	if (!rw_read_exact_or_log(infile, &numlines, 1, 1))
		return 0;

	for (i=0; i < numlines; i++)
	{
		if (!rw_read_exact_or_log(infile, &tempwidth, 1, 1))
			return 0;
		const int original_width = static_cast<unsigned char>(tempwidth);
		int width = original_width;
		if (width >= static_cast<int>(sizeof(oneline)))
			width = sizeof(oneline) - 1;
		if (width > 0)
		{
			if (!rw_read_exact_or_log(infile, oneline, width, 1))
				return 0;
			oneline[width] = 0;
			// Skip any remaining bytes to keep the stream aligned
			if (original_width > width)
			{
				char discard[256];
				int remaining = original_width - width;
				while (remaining > 0)
				{
					const int chunk = remaining < static_cast<int>(sizeof(discard)) ? remaining : static_cast<int>(sizeof(discard));
					if (!rw_read_exact_or_log(infile, discard, chunk, 1))
						return 0;
					remaining -= chunk;
				}
			}
		}
		else
		{
			oneline[0] = 0;
		}
		data->description.push_back(oneline);
	}

	// Now read the grid file to our master screen ..
	std::string gridpix4 = ensure_pix_extension(newgrid);

    data->delete_grid();

	data->world().grid = read_pixie_file(gridpix4.c_str());
	data->world().pixmaxx = data->world().grid.w * GRID_SIZE;
	data->world().pixmaxy = data->world().grid.h * GRID_SIZE;

	data->world().mysmoother.set_target(data->world().grid);

	// Fix up doors, etc.
	for(auto& uptr : data->weaplist)
	{
	    walker* w = uptr.get();
		if (w && w->family==FAMILY_DOOR)
		{
			if (data->world().mysmoother.query_genre_x_y(w->xpos/GRID_SIZE,
			        (w->ypos/GRID_SIZE)-1)==TYPE_WALL)
			{
				w->set_frame(1);  // turn sideways ..
			}
		}
	}

	return 1;
} // end load_version_5

#define READ_OR_RETURN(ctx, ptr, size, n) \
do{ \
    if(!rw_read_exact_or_log((ctx), (ptr), (size), (n))) \
        return 0; \
} while(0)

// Version 6 includes a 30-byte scenario title after the grid name.
// Also load version 7 and 8 here, since it's a simple change ..
short load_version_6(og::io::OgFile& infile, LevelRuntimeData* data, short version)
{
    short currentx, currenty;
    unsigned char temporder, tempfamily;
    unsigned char tempteam;
    char tempfacing, tempcommand;
    char templevel;
    short shortlevel;
    char tempreserved[20];
    short listsize;
    short i;
    walker * new_guy;
    char newgrid[9] = "grid"; // 8-byte grid name + NUL
    char new_scen_type; // read the scenario type
    char oneline[80] = {};
    char numlines = 0, tempwidth;
    char tempname[12] = {};
    char scentitle[30] = {};
    short temp_par = 1;
    short temp_time_limit = 4000;

    // Format of a scenario object list file version 6/7 is:
    // 3-byte header: 'FSS'
    // 1-byte version #
    // ----- (above is already determined by now)
    // 8-byte string = grid name to load
    // 30-byte scenario title (ver 6+)
    // 1-byte char = scenario type, default is 0
    // 2-bytes par-value, v.8+
	// 2-bytes time limit for bonus points, v9+
    // 2-bytes (short) = total objects to follow
    // List of n objects, each of 7-bytes of form:
    // 1-byte ORDER
    // 1-byte FAMILY
    // 2-byte short xpos
    // 2-byte short ypos
    // 1-byte TEAM
    // 1-byte facing
    // 1-byte command
    // 1-byte level // 2 bytes in version 7+
    // 12-bytes name
    // ---
    // 10 bytes reserved
    // 1-byte # of lines of text to load
    // List of n lines of text, each of form:
    // 1-byte character width of line
    // m bytes == characters on this line


    // Get grid file to load
    READ_OR_RETURN(infile, newgrid, 8, 1);
    newgrid[8] = '\0';
    // Zardus: FIX: make sure they're lowercased
    lowercase(newgrid);
	data->grid_file = newgrid;

    // Get scenario title, if it exists
    READ_OR_RETURN(infile, scentitle, 30, 1);

    // Get the scenario type information
    READ_OR_RETURN(infile, &new_scen_type, 1, 1);

    if (version >= 8)
    {
        READ_OR_RETURN(infile, &temp_par, 2, 1);
    }
    // else we're using the value of the level ..

    if (version >= 9)
    {
        READ_OR_RETURN(infile, &temp_time_limit, 2, 1);
    }

    // Determine number of objects to load ...
    READ_OR_RETURN(infile, &listsize, 2, 1);
    if (listsize < 0 || listsize > MAX_SCENARIO_OBJECTS)
    {
        Log("Invalid scenario object count: {}\n", listsize);
        return 0;
    }

    // Now read in the objects one at a time
    for (i=0; i < listsize; i++)
    {
        READ_OR_RETURN(infile, &temporder, 1, 1);
        READ_OR_RETURN(infile, &tempfamily, 1, 1);
        READ_OR_RETURN(infile, &currentx, 2, 1);
        READ_OR_RETURN(infile, &currenty, 2, 1);
        READ_OR_RETURN(infile, &tempteam, 1, 1);
        READ_OR_RETURN(infile, &tempfacing, 1, 1);
        READ_OR_RETURN(infile, &tempcommand, 1, 1);
        if (version >= 7)
            READ_OR_RETURN(infile, &shortlevel, 2, 1);
        else
            READ_OR_RETURN(infile, &templevel, 1, 1);
        READ_OR_RETURN(infile, tempname, 12, 1);
        READ_OR_RETURN(infile, tempreserved, 10, 1);
        if (static_cast<Order>(temporder) == Order::Treasure)
            new_guy = data->add_fx_ob(static_cast<Order>(temporder), tempfamily);
        else
            new_guy = data->add_ob(static_cast<Order>(temporder), tempfamily);  // create new object
        if (!new_guy)
        {
            Log("Error creating object when loading.\n");
            return 0;
        }
        
        new_guy->setxy(currentx, currenty);
        new_guy->team_num = sanitize_loaded_team_num(tempteam);
        if (version >= 7)
            new_guy->stats()->level = shortlevel;
        else
            new_guy->stats()->level = templevel;
        new_guy->stats()->name = std::string(tempname, strnlen(tempname, sizeof(tempname)));
        if (new_guy->stats()->name.size() > 1)           //chad 5/25/95
            new_guy->stats()->set_bit_flags(BIT_NAMED, 1);

    }
    
    
    // Now get the lines of text to read ..
    READ_OR_RETURN(infile, &numlines, 1, 1);
    std::list<std::string> desc_lines;
    for (i=0; i < numlines; i++)
    {
        READ_OR_RETURN(infile, &tempwidth, 1, 1);
        if(tempwidth > 0)
        {
            int original_width = static_cast<unsigned char>(tempwidth);
            int width = original_width;
            if(width >= static_cast<int>(sizeof(oneline)))
                width = sizeof(oneline) - 1;
            READ_OR_RETURN(infile, oneline, width, 1);
            oneline[width] = 0;
            // Skip any remaining bytes to keep the stream aligned
            if(original_width > width)
            {
                char discard[256];
                int remaining = original_width - width;
                while(remaining > 0)
                {
                    int chunk = remaining < static_cast<int>(sizeof(discard)) ? remaining : static_cast<int>(sizeof(discard));
                    READ_OR_RETURN(infile, discard, chunk, 1);
                    remaining -= chunk;
                }
            }
        }
        else
            oneline[0] = 0;
        desc_lines.push_back(oneline);
    }

    
    // Now read the grid file to our master screen ..
    std::string gridpix5 = ensure_pix_extension(newgrid);

    data->world().grid = read_pixie_file(gridpix5.c_str());
    data->world().pixmaxx = data->world().grid.w * GRID_SIZE;
    data->world().pixmaxy = data->world().grid.h * GRID_SIZE;
    
    // The collected data so far
    data->world().title = std::string(scentitle, strnlen(scentitle, sizeof(scentitle)));
    data->world().type = new_scen_type;
    data->world().par_value = temp_par;
    data->world().time_bonus_limit = temp_time_limit;
    data->description = desc_lines;
    data->world().mysmoother.set_target(data->world().grid);

    // Fix up doors, etc.
	for(auto& uptr : data->weaplist)
	{
	    walker* w = uptr.get();
        if (w && w->family==FAMILY_DOOR)
        {
            if (data->world().mysmoother.query_genre_x_y(w->xpos/GRID_SIZE,
                    (w->ypos/GRID_SIZE)-1)==TYPE_WALL)
            {
                w->set_frame(1);  // turn sideways ..
            }
        }
    }

    return 1;
} // end load_version_6

short load_scenario_version(og::io::OgFile& infile, LevelRuntimeData* data, short version)
{
    if(data == nullptr)
        return 0;
    
    short result = 0;
	switch (version)
	{
		case 2:
			result = load_version_2(infile, data);
			break;
		case 3:
			result = load_version_3(infile, data);
			break;
		case 4:
			result = load_version_4(infile, data);
			break;
		case 5:
			result = load_version_5(infile, data);
			break;
		case 6:
		case 7:
		case 8:
		case 9:
			result = load_version_6(infile, data, version);
			break;
		default:
			Log("Scenario {} is version-level {}, and cannot be read.\n",
			       data->world().id, version);
			break;
	}
    
	return result;
}

bool LevelRuntimeData::load()
{
	TRACE("game", "LevelRuntimeData::load id=%d headless=%d", world().id, headless_ ? 1 : 0);
    last_io_error_ = IoError::None;
	char temptext[10] = {};
	char versionnumber = 0;

	std::string thefile = std::format("scen{}.fss", world().id);

	auto infile = og::io::og_open_read("scen/", thefile.c_str());
	if (!infile)
    {
        LogError("Cannot open level file for reading: {}\n", thefile);
        last_io_error_ = IoError::OpenReadFailed;
        return false;
    }

	if (!rw_read_exact_or_log(*infile, temptext, 1, 3))
	{
        last_io_error_ = IoError::ParseFailed;
		return false;
	}
	if (std::string(temptext) != "FSS")
	{
		LogError("File {} is not a valid scenario!\n", thefile);
        last_io_error_ = IoError::InvalidHeader;
		return false;
	}

	if (!rw_read_exact_or_log(*infile, &versionnumber, 1, 1))
	{
        last_io_error_ = IoError::ParseFailed;
		return false;
	}
    if(versionnumber < 2 || versionnumber > VERSION_NUM)
    {
        Log("Scenario {} is version-level {}, and cannot be read.\n",
            world().id, static_cast<int>(versionnumber));
        last_io_error_ = IoError::UnsupportedVersion;
        return false;
    }
    Log("Loading version {} scenario", static_cast<int>(versionnumber));

    wire_world_entity_services(world_, this, hooks_);
    clear();
    world().par_value = static_cast<short>(world().id);

    short tempvalue = load_scenario_version(*infile, this, versionnumber);
    if(tempvalue == 0)
    {
        if(last_io_error_ == IoError::None)
            last_io_error_ = IoError::ParseFailed;
        return false;
    }

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

bool save_grid_file(const char* gridname, const PixieData& grid)
{
	// File data in form:
	// <# of frames>      1 byte
	// <x size>                   1 byte
	// <y size>                   1 byte
	// <pixie data>               <x*y*frames> bytes

	char numframes, x, y;
	std::string fullpath(gridname);

	// Create the full pathname for the pixie file
	fullpath += ".pix";

	lowercase (fullpath);

	auto outfile = og::io::og_open_write("temp/pix/", fullpath.c_str());
	if (!outfile)
	{
		Log("Failed to save map file: temp/pix/{}\n", fullpath);
		return false;
	}

	x = grid.w;
	y = grid.h;
	numframes = 1;
	outfile->write(&numframes, 1, 1);
	outfile->write(&x, 1, 1);
	outfile->write(&y, 1, 1);

	outfile->write(grid.data.get(), 1, (x*y));

	return true;
}

bool LevelRuntimeData::save()
{
    last_io_error_ = IoError::None;
	std::int32_t currentx, currenty;
	unsigned char temporder;
	char tempfamily;
	char tempteam, tempfacing, tempcommand;
	short shortlevel;
	char filler[20] = "MSTRMSTRMSTRMSTR"; // for RESERVED
	char temptext[10] = "FSS";
	char temp_grid[20] = {};
	char temp_scen_type = 0;
	short listsize = 0;
	char temp_version = VERSION_NUM;
	std::string temp_filename;
	std::uint8_t numlines = 0;
	std::uint8_t tempwidth = 0;
	char tempname[12] = {};
	char scentitle[30] = {};
	short temp_par;
	short temp_time_limit;

	// Format of a scenario object list file is: (ver. 8)
	// 3-byte header: 'FSS'
	// 1-byte version number
	// 8-byte grid file name
	// 30-byte scenario title
	// 1-byte scenario_type
	// 2-bytes par-value for level
	// 2-bytes time limit for bonus points, v9+
	// 2-bytes (std::int32_t) = total objects to follow
	// List of n objects, each of 20-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte std::int32_t xpos
	// 2-byte std::int32_t ypos
	// 1-byte TEAM
	// 1-byte current facing
	// 1-byte current command
	// 1-byte level // this is 2 bytes in version 7+
	// 12-bytes name
	// ---
	// 10 bytes RESERVED
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line

	// Zardus: PORT: no longer need to put in scen/ in this part
	//strcpy(temp_filename, scen_directory);
	temp_filename = std::format("scen{}.fss", this->world().id);

	auto outfile = og::io::og_open_write("temp/scen/", temp_filename.c_str());
	if (!outfile)
	{
		Log("Could not open file for writing: temp/scen/{}\n", temp_filename);
        last_io_error_ = IoError::OpenWriteFailed;
		return false;
	}

#define WRITE_FIELD(src, size, count)                                                                    \
    do {                                                                                                 \
        if (outfile->write((src), (size), (count)) != static_cast<std::size_t>(count)) {               \
            Log("Failed to write scenario file: temp/scen/{}\n", temp_filename);                        \
            last_io_error_ = IoError::SerializeFailed;                                                   \
            return false;                                                                                \
        }                                                                                                \
    } while (0)

	// Write id header
	WRITE_FIELD(temptext, 3, 1);

	// Write version number
	WRITE_FIELD(&temp_version, 1, 1);

	// Write name of current grid...
	fill_fixed_field(temp_grid, 8, this->grid_file, "grid_file");  // Do NOT include extension (max 8 chars)
	WRITE_FIELD(temp_grid, 8, 1);

	// Write the scenario title, if it exists
	fill_fixed_field(scentitle, 30, this->world().title, "title");
	WRITE_FIELD(scentitle, 30, 1);

	// Write the scenario type info
	temp_scen_type = this->world().type;
	WRITE_FIELD(&temp_scen_type, 1, 1);

	// Write our par value (version 8+)
	temp_par = this->world().par_value;
	WRITE_FIELD(&temp_par, 2, 1);

	// Write the time limit (version 9+)
	temp_time_limit = this->world().time_bonus_limit;
	WRITE_FIELD(&temp_time_limit, 2, 1);

	// Determine size of object list and clamp to loader's accepted range.
	const size_t total_objects = oblist.size() + fxlist.size() + weaplist.size();
	const size_t serialized_objects = std::min(total_objects, static_cast<size_t>(MAX_SCENARIO_OBJECTS));
	if (serialized_objects != total_objects)
		Log("Scenario object count {} exceeds {}, truncating on save.\n", total_objects, MAX_SCENARIO_OBJECTS);
	listsize = static_cast<short>(serialized_objects);
	WRITE_FIELD(&listsize, 2, 1);

	size_t remaining_objects = serialized_objects;
	auto write_object_list = [&](std::list<std::unique_ptr<walker>>& list, const char* null_label) -> bool
	{
		for (auto& uptr : list)
		{
			if (remaining_objects == 0)
				break;

			walker* ob = uptr.get();
			if (ob == nullptr)
			{
				Log("Unexpected nullptr {} object.\n", null_label);
				last_io_error_ = IoError::SerializeFailed;
				return false;
			}
			temporder = static_cast<unsigned char>(ob->query_order());
			tempfacing= ob->curdir;
			tempfamily= ob->family;
			tempteam  = ob->team_num;
			tempcommand = static_cast<char>(ob->act_type);
			currentx  = ob->xpos;
			currenty  = ob->ypos;
			shortlevel = static_cast<short>(ob->stats()->level);
			snprintf(tempname, sizeof(tempname), "%s", ob->stats()->name.c_str());
			WRITE_FIELD( &temporder, 1, 1);
			WRITE_FIELD( &tempfamily, 1, 1);
			WRITE_FIELD( &currentx, 2, 1);
			WRITE_FIELD( &currenty, 2, 1);
			WRITE_FIELD( &tempteam, 1, 1);
			WRITE_FIELD( &tempfacing, 1, 1);
			WRITE_FIELD( &tempcommand, 1, 1);
			WRITE_FIELD( &shortlevel, 2, 1);
			WRITE_FIELD( tempname, 12, 1);
			WRITE_FIELD( filler, 10, 1);
			remaining_objects--;
		}
		return true;
	};

	// Okay, we've written header .. now dump the data ..
	if (!write_object_list(oblist, "regular") ||
	    !write_object_list(fxlist, "fx") ||
	    !write_object_list(weaplist, "weap"))
	{
		return false;
	}

	numlines = static_cast<Uint8>(this->description.size());
	//printf("saving %d lines\n", numlines);

	WRITE_FIELD( &numlines, 1, 1);
	for (auto& line : this->description)
	{
		const size_t serialized_width = std::min<size_t>(line.size(), 0xffu);
		tempwidth = static_cast<Uint8>(serialized_width);
		WRITE_FIELD( &tempwidth, 1, 1);
		if(serialized_width > 0)
			WRITE_FIELD(line.data(), serialized_width, 1);
	}

	// Save map (grid) file
	if (!save_grid_file(grid_file.c_str(), world().grid))
	{
		last_io_error_ = IoError::OpenWriteFailed;
#undef WRITE_FIELD
		return false;
	}

	Log("Scenario saved.\n");

    last_io_error_ = IoError::None;
#undef WRITE_FIELD
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
    if(i >= int(description.size()))
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
    if (filename == nullptr || filename[0] == '\0')
        return "none";

    std::string tempfile = std::string(filename) + ".fss";
    auto infile = og::io::og_open_read("scen/", tempfile.c_str());
    if (!infile)
        return "none";

    char temptext[4] = {};
    char versionnumber = 0;
    char gridname[8] = {};
    char buffer[31] = {};
    std::string result = "none";

    if (!rw_read_exact_or_log(*infile, temptext, 1, 3) ||
        std::string(temptext, 3) != "FSS")
        return result;
    if (!rw_read_exact_or_log(*infile, &versionnumber, 1, 1) || versionnumber < 6)
        return result;
    if (!rw_read_exact_or_log(*infile, gridname, 1, 8))
        return result;
    if (!rw_read_exact_or_log(*infile, buffer, 1, 30))
        return result;
    result = std::string(buffer);
    return result;
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

std::list<walker*> LevelRuntimeData::find_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob)
{
    return world().find_in_range(somelist, range, howmany, ob);
}

std::list<walker*> LevelRuntimeData::find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob)
{
    return world().find_foes_in_range(somelist, range, howmany, ob);
}

std::list<walker*> LevelRuntimeData::find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range,
                                      std::int32_t* howmany, walker* ob)
{
    return world().find_foe_weapons_in_range(somelist, range, howmany, ob);
}

std::list<walker*> LevelRuntimeData::find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range,
                                      std::int32_t* howmany, walker* ob)
{
    return world().find_friends_in_range(somelist, range, howmany, ob);
}
