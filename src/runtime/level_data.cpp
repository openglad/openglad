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
#include <openglad/data/level_render.h>
#include <openglad/data/level_data_hooks.h>
#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <string_view>


int toInt(const std::string& s);

void LevelData::set_sim_context(SaveData* save, std::int32_t* enemy_freeze,
                                og::sim::SimEventLog* events, IRandom* rng,
                                cfg_store* config)
{
    sim_ctx_save_ = save;
    sim_ctx_enemy_freeze_ = enemy_freeze;
    sim_ctx_events_ = events;
    sim_ctx_rng_ = rng;
    sim_ctx_config_ = config;
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
    unmount_campaign_package(old_campaign);
    
    // Load the campaign data from <user_data>/scen/<id>.glad
    if(mount_campaign_package(id))
    {
        auto file = og::io::og_open_read("campaign.yaml", true);
        if(!file)
        {
            last_io_error_ = IoError::OpenReadFailed;
            unmount_campaign_package(id);
            mount_campaign_package(old_campaign);
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

        unmount_campaign_package(id);
    }
    else
    {
        last_io_error_ = IoError::PackageMountFailed;
    }
    
    mount_campaign_package(old_campaign);
    
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
    : LevelData(level_id, false, nullptr)
{
}

LevelData::LevelData(int level_id, const LevelDataHooks* hooks)
    : LevelData(level_id, false, hooks)
{
}

LevelData::LevelData(int level_id, bool headless)
    : LevelData(level_id, headless, nullptr)
{
}

LevelData::LevelData(int level_id, bool headless, const LevelDataHooks* hooks)
    : id(level_id), title("New Level"), type(0), par_value(1), time_bonus_limit(4000), pixmaxx(0), pixmaxy(0)
    , myloader(nullptr), numobs(0), topx(0), topy(0)
{
    hooks_ = hooks;
    headless_ = headless;

	myobmap = std::make_unique<obmap>();
    myloader = std::make_unique<loader>(this);

    if (!headless)
    {
        load_map_data(pixdata);
        if (hooks_ && hooks_->create_level_render)
            renderer_ = hooks_->create_level_render(pixdata);
    }
}

LevelData::~LevelData()
{
    delete_objects();
    delete_grid();
    myloader.reset();

    myobmap.reset();

    renderer_.reset();
    for (int i = 0; i < PIX_MAX; i++)
        pixdata[i].free();
}

void LevelData::clear()
{
    delete_objects();
    delete_grid();
    
	myobmap = std::make_unique<obmap>();
    
    title = "New Level";
    type = 0;
    par_value = 1;
    time_bonus_limit = 4000;
    
    topx = 0;
    topy = 0;
}

walker* LevelData::add_ob(Order order, std::int32_t family, bool atstart)
{
	(void)atstart;
	if (order == Order::Weapon)
		return add_weap_ob(order, family);

    auto w = myloader->create_walker_owned(order, family);
    if (!w)
        return nullptr;

    wire_entity(w.get());
    if (hooks_ && hooks_->wire_entity_from_screen)
        hooks_->wire_entity_from_screen(w.get());
    if (order == Order::Living)
        numobs++;

    walker* raw = w.get();
    oblist.push_back(std::move(w));
    return raw;
}

walker* LevelData::add_fx_ob(Order order, std::int32_t family)
{
	auto w = myloader->create_walker_owned(order, family);
	if (!w)
		return nullptr;

	wire_entity(w.get());
	if (hooks_ && hooks_->wire_entity_from_screen)
		hooks_->wire_entity_from_screen(w.get());

	walker* raw = w.get();
	fxlist.push_back(std::move(w));
	return raw;
}

walker* LevelData::add_weap_ob(Order order, std::int32_t family)
{
	auto w = myloader->create_walker_owned(order, family);
    if (!w)
        return nullptr;

    wire_entity(w.get());
    if (hooks_ && hooks_->wire_entity_from_screen)
        hooks_->wire_entity_from_screen(w.get());

    walker* raw = w.get();
    weaplist.push_back(std::move(w));
	return raw;
}

void LevelData::wire_entity(walker* w)
{
    w->myobmap = myobmap.get();
    w->sim_level = this;
    w->sim_save = sim_ctx_save_;
    w->sim_enemy_freeze = sim_ctx_enemy_freeze_;
    w->sim_events = sim_ctx_events_;
    w->sim_rng = sim_ctx_rng_;
    w->sim_config = sim_ctx_config_;
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

void LevelData::delete_grid()
{
    grid.free();
    pixmaxx = 0;
    pixmaxy = 0;
    // mysmoother stores a raw pointer to grid data; keep it in sync.
    mysmoother.reset();
}

void LevelData::create_new_grid()
{
    grid.free();
    
    grid.frames = 1;
    grid.w = 40;
    grid.h = 60;
	pixmaxx = grid.w * GRID_SIZE;
	pixmaxy = grid.h * GRID_SIZE;
	
	int size = grid.w*grid.h;
	    grid.data = std::make_unique<unsigned char[]>(size);
	for(int i = 0; i < size; i++)
    {
        // Color
        switch(rand()%4)
        {
            case 0:
            grid.data[i] = PIX_GRASS1;
            break;
            case 1:
            grid.data[i] = PIX_GRASS2;
            break;
            case 2:
            grid.data[i] = PIX_GRASS3;
            break;
            case 3:
            grid.data[i] = PIX_GRASS4;
            break;
        }
    }

    // mysmoother stores a raw pointer to grid data; keep it in sync.
    mysmoother.set_target(grid);
}

void LevelData::resize_grid(int width, int height)
{
    // Size is limited to one byte in the file format
    if(width < 3 || height < 3 || width > 255 || height > 255)
    {
        Log("Can't resize grid to these dimensions: {}x{}\n", width, height);
        return;
    }
    
    auto random_grass_tile = []() -> unsigned char {
        switch(rand()%4)
        {
            case 0: return PIX_GRASS1;
            case 1: return PIX_GRASS2;
            case 2: return PIX_GRASS3;
            case 3: return PIX_GRASS4;
        }
        return PIX_GRASS1;
    };

    // Create new grid
	int size = width*height;
    auto new_grid = std::make_unique<unsigned char[]>(size);
    
    // Copy the map data
	for(int i = 0; i < width; i++)
    {
        for(int j = 0; j < height; j++)
        {
            if(i < grid.w && j < grid.h)
            {
                new_grid[j*width + i] = grid.data[j*grid.w + i];
            }
            else
            {
                new_grid[j*width + i] = random_grass_tile();
            }
        }
    }
    
    // Delete the old, use the new
    grid.free();
    grid.data = std::move(new_grid);
    grid.frames = 1;
    grid.w = static_cast<unsigned char>(width);
    grid.h = static_cast<unsigned char>(height);
	pixmaxx = grid.w * GRID_SIZE;
	pixmaxy = grid.h * GRID_SIZE;

    // mysmoother stores a raw pointer to grid data; keep it in sync.
    mysmoother.set_target(grid);
    
    
    // Delete objects that fell off the map
    int x = 0;
    int y = 0;
    int w = grid.w * GRID_SIZE;
    int h = grid.h * GRID_SIZE;

    auto off_map = [x, y, w, h](const std::unique_ptr<walker>& uptr) {
        walker* ob = uptr.get();
        return ob == nullptr || (x > ob->xpos || ob->xpos >= x + w || y > ob->ypos || ob->ypos >= y + h);
    };

    std::erase_if(oblist, off_map);
    std::erase_if(fxlist, off_map);
    std::erase_if(weaplist, off_map);
}

void LevelData::delete_objects()
{
	oblist.clear();
	fxlist.clear();
	weaplist.clear();
    dead_list.clear();

	numobs = 0;

    // If this is the active screen level, clear any stale control pointers
    // that may reference walkers just deleted above.
    if (hooks_ && hooks_->clear_stale_view_controls)
        hooks_->clear_stale_view_controls(this);

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

short load_version_2(og::io::OgFile& infile, LevelData* data)
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
		new_guy ->team_num = tempteam;
	}

	// Now read the grid file to our master screen ..
	std::string gridpix = ensure_pix_extension(newgrid);

    data->delete_grid();

	data->grid = read_pixie_file(gridpix.c_str());
	data->pixmaxx = data->grid.w * GRID_SIZE;
	data->pixmaxy = data->grid.h * GRID_SIZE;

	return 1;
}

// Version 3 scenarios have a block of text which can be displayed
// at the start, etc.  Format is
// # of lines,
//  1-byte character width
//  n bytes specified from above
short load_version_3(og::io::OgFile& infile, LevelData* data)
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
		new_guy->team_num = tempteam;
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

	data->grid = read_pixie_file(gridpix2.c_str());
	data->pixmaxx = data->grid.w * GRID_SIZE;
	data->pixmaxy = data->grid.h * GRID_SIZE;

	return 1;
}

// Version 4 scenarios include a 12-byte name for EVERY walker..
short load_version_4(og::io::OgFile& infile, LevelData* data)
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
	char tempname[12] = {};


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
		new_guy->team_num = tempteam;
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

	data->grid = read_pixie_file(gridpix3.c_str());
	data->pixmaxx = data->grid.w * GRID_SIZE;
	data->pixmaxy = data->grid.h * GRID_SIZE;

	return 1;
} // end load_version_4

// Version 5 scenarios include a 1-byte 'scenario-type' specifier after
// the grid name.
short load_version_5(og::io::OgFile& infile, LevelData* data)
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
	char tempname[12] = {};

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
	data->type = new_scen_type;

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
		new_guy->team_num = tempteam;
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

	data->grid = read_pixie_file(gridpix4.c_str());
	data->pixmaxx = data->grid.w * GRID_SIZE;
	data->pixmaxy = data->grid.h * GRID_SIZE;

	data->mysmoother.set_target(data->grid);

	// Fix up doors, etc.
	for(auto& uptr : data->weaplist)
	{
	    walker* w = uptr.get();
		if (w && w->query_family()==FAMILY_DOOR)
		{
			if (data->mysmoother.query_genre_x_y(w->xpos/GRID_SIZE,
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
short load_version_6(og::io::OgFile& infile, LevelData* data, short version)
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
        new_guy->team_num = tempteam;
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

    data->grid = read_pixie_file(gridpix5.c_str());
    data->pixmaxx = data->grid.w * GRID_SIZE;
    data->pixmaxy = data->grid.h * GRID_SIZE;
    
    // The collected data so far
    data->title = std::string(scentitle, strnlen(scentitle, sizeof(scentitle)));
    data->type = new_scen_type;
    data->par_value = temp_par;
    data->time_bonus_limit = temp_time_limit;
    data->description = desc_lines;
    data->mysmoother.set_target(data->grid);

    // Fix up doors, etc.
	for(auto& uptr : data->weaplist)
	{
	    walker* w = uptr.get();
        if (w && w->query_family()==FAMILY_DOOR)
        {
            if (data->mysmoother.query_genre_x_y(w->xpos/GRID_SIZE,
                    (w->ypos/GRID_SIZE)-1)==TYPE_WALL)
            {
                w->set_frame(1);  // turn sideways ..
            }
        }
    }

    return 1;
} // end load_version_6

short load_scenario_version(og::io::OgFile& infile, LevelData* data, short version)
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
			       data->id, version);
			break;
	}
    
	return result;
}

bool LevelData::load()
{
	TRACE("game", "LevelData::load id=%d headless=%d", id, headless_ ? 1 : 0);
    last_io_error_ = IoError::None;
	char temptext[10] = {};
	char versionnumber = 0;

	std::string thefile = std::format("scen{}.fss", id);

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
            id, static_cast<int>(versionnumber));
        last_io_error_ = IoError::UnsupportedVersion;
        return false;
    }
    Log("Loading version {} scenario", static_cast<int>(versionnumber));

    myloader = std::make_unique<loader>(this);
    clear();
    par_value = static_cast<short>(id);

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
        renderer_.reset();
        for (int i = 0; i < PIX_MAX; i++)
            pixdata[i].free();

        load_map_data(pixdata);
        if (hooks_ && hooks_->create_level_render)
            renderer_ = hooks_->create_level_render(pixdata);
    }

	TRACE("game", "LevelData::load complete");
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

bool LevelData::save()
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
	// 1-byte version number (from graph.h)
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
	temp_filename = std::format("scen{}.fss", this->id);

	auto outfile = og::io::og_open_write("temp/scen/", temp_filename.c_str());
	if (!outfile)
	{
		Log("Could not open file for writing: temp/scen/{}\n", temp_filename);
        last_io_error_ = IoError::OpenWriteFailed;
		return false;
	}

#define WRITE_FIELD(src, size, count) outfile->write((src), (size), (count))

	// Write id header
	WRITE_FIELD(temptext, 3, 1);

	// Write version number
	WRITE_FIELD(&temp_version, 1, 1);

	// Write name of current grid...
	fill_fixed_field(temp_grid, 8, this->grid_file, "grid_file");  // Do NOT include extension (max 8 chars)
	WRITE_FIELD(temp_grid, 8, 1);

	// Write the scenario title, if it exists
	fill_fixed_field(scentitle, 30, this->title, "title");
	WRITE_FIELD(scentitle, 30, 1);

	// Write the scenario type info
	temp_scen_type = this->type;
	WRITE_FIELD(&temp_scen_type, 1, 1);

	// Write our par value (version 8+)
	temp_par = this->par_value;
	WRITE_FIELD(&temp_par, 2, 1);

	// Write the time limit (version 9+)
	temp_time_limit = this->time_bonus_limit;
	WRITE_FIELD(&temp_time_limit, 2, 1);

	// Determine size of object list ...
	{
		const size_t listsize_st = oblist.size() + fxlist.size() + weaplist.size();
		listsize = static_cast<short>(listsize_st);
	}
	WRITE_FIELD(&listsize, 2, 1);

	// Okay, we've written header .. now dump the data ..
	for(auto& uptr : oblist)
	{
	    walker* w = uptr.get();
        if (w == nullptr)
        {
            Log("Unexpected nullptr object.\n");
            last_io_error_ = IoError::SerializeFailed;
            return false;  // Something wrong! Too few objects..
        }
        temporder = static_cast<unsigned char>(w->query_order());
        tempfacing= w->curdir;
        tempfamily= w->query_family();
        tempteam  = w->team_num;
        tempcommand = static_cast<char>(w->query_act_type());
        currentx  = w->xpos;
        currenty  = w->ypos;
        //templevel = w->stats()->level;
        shortlevel = static_cast<short>(w->stats()->level);
        snprintf(tempname, sizeof(tempname), "%s", w->stats()->name.c_str());
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
	}

	// Now dump the fxlist data ..
	for(auto& uptr : fxlist)
	{
	    walker* ob = uptr.get();
        if (ob == nullptr)
        {
            Log("Unexpected nullptr fx object.\n");
            last_io_error_ = IoError::SerializeFailed;
            return false;  // Something wrong! Too few objects..
        }
        temporder = static_cast<unsigned char>(ob->query_order());
        tempfacing= ob->curdir;
        tempfamily= ob->query_family();
        tempteam  = ob->team_num;
        tempcommand = static_cast<char>(ob->query_act_type());
        currentx  = ob->xpos;
        currenty  = ob->ypos;
        //templevel = ob->stats()->level;
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
	}

	// Now dump the weaplist data ..
	for(auto& uptr : weaplist)
	{
	    walker* ob = uptr.get();
        if (ob == nullptr)
        {
            Log("Unexpected nullptr weap object.\n");
            last_io_error_ = IoError::SerializeFailed;
            return false;  // Something wrong! Too few objects..
        }
        temporder = static_cast<unsigned char>(ob->query_order());
        tempfacing= ob->curdir;
        tempfamily= ob->query_family();
        tempteam  = ob->team_num;
        tempcommand = static_cast<char>(ob->query_act_type());
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
	if (!save_grid_file(grid_file.c_str(), grid))
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

LevelData::IoError LevelData::load_with_error()
{
    load();
    return last_io_error_;
}

LevelData::IoError LevelData::save_with_error()
{
    save();
    return last_io_error_;
}

void LevelData::set_draw_pos(std::int32_t new_topx, std::int32_t new_topy)
{
    this->topx = new_topx;
    this->topy = new_topy;
}

void LevelData::add_draw_pos(std::int32_t dx, std::int32_t dy)
{
    this->topx += dx;
    this->topy += dy;
}

void LevelData::draw(screen* screenp)
{
    if (!screenp) return;
    if (hooks_ && hooks_->draw)
        hooks_->draw(this, screenp);
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

short remaining_foes(LevelData& level, walker* myguy)
{
    short myfoes = 0;
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            (w->query_order() == Order::Living) &&
            !myguy->is_friendly(w))
            myfoes++;
    }
    return myfoes;
}

// ---- Collision / passability queries ----

static constexpr short MAX_SPREAD = 10;

bool LevelData::query_grid_passable(float x, float y, walker  *ob)
{
	std::int32_t i,j;
	std::int32_t xtrax = 1;
	std::int32_t xtray = 1;
	std::int32_t xtarg;
	std::int32_t ytarg;
	std::int32_t dist;
	const std::int32_t x_i = static_cast<std::int32_t>(x);
	const std::int32_t y_i = static_cast<std::int32_t>(y);
	std::int32_t xover = x_i + ob->sizex;
	std::int32_t yover = y_i + ob->sizey;

	if (x_i < 0 || y_i < 0 || xover >= pixmaxx || yover >= pixmaxy)
		return 0;

	if (ob->stats()->query_bit_flags(BIT_ETHEREAL) )
		return 1;

	if (!grid.valid())
		return 0;

	if (!((xover)%GRID_SIZE))
		xtrax = 0;
	if (!((yover)%GRID_SIZE))
		xtray = 0;

	xtarg = (xover/GRID_SIZE) + xtrax;
	ytarg = (yover/GRID_SIZE) + xtray;

	for (i = x_i/GRID_SIZE; i < xtarg; i++)
		for (j = y_i/GRID_SIZE; j < ytarg; j++)
		{
			switch (static_cast<unsigned char>(grid.data[i+grid.w*j]))
			{
				case PIX_GRASS1:
				case PIX_GRASS2:
				case PIX_GRASS3:
				case PIX_GRASS4:
				case PIX_GRASS_DARK_1:
				case PIX_GRASS_DARK_2:
				case PIX_GRASS_DARK_3:
				case PIX_GRASS_DARK_4:
				case PIX_GRASS_DARK_LL:
				case PIX_GRASS_DARK_UR:
				case PIX_GRASS_DARK_B1:
				case PIX_GRASS_DARK_B2:
				case PIX_GRASS_DARK_BR:
				case PIX_GRASS_DARK_R1:
				case PIX_GRASS_DARK_R2:
				case PIX_GRASS_RUBBLE:
				case PIX_GRASS1_DAMAGED:
				case PIX_GRASS_LIGHT_1:
				case PIX_GRASS_LIGHT_TOP:
				case PIX_GRASS_LIGHT_RIGHT_TOP:
				case PIX_GRASS_LIGHT_RIGHT:
				case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
				case PIX_GRASS_LIGHT_BOTTOM:
				case PIX_GRASS_LIGHT_LEFT_BOTTOM:
				case PIX_GRASS_LIGHT_LEFT:
				case PIX_GRASS_LIGHT_LEFT_TOP:
				case PIX_GRASSWATER_LL:
				case PIX_GRASSWATER_LR:
				case PIX_GRASSWATER_UL:
				case PIX_GRASSWATER_UR:
				case PIX_PAVEMENT1:
				case PIX_PAVEMENT2:
				case PIX_PAVEMENT3:
				case PIX_COBBLE_1:
				case PIX_COBBLE_2:
				case PIX_COBBLE_3:
				case PIX_COBBLE_4:
				case PIX_FLOOR_PAVEL:
				case PIX_FLOOR_PAVER:
				case PIX_FLOOR_PAVEU:
				case PIX_FLOOR_PAVED:
				case PIX_PAVESTEPS1:
				case PIX_PAVESTEPS2:
				case PIX_PAVESTEPS2L:
				case PIX_PAVESTEPS2R:
				case PIX_FLOOR1:
				case PIX_CARPET_LL:
				case PIX_CARPET_B:
				case PIX_CARPET_LR:
				case PIX_CARPET_UR:
				case PIX_CARPET_U:
				case PIX_CARPET_UL:
				case PIX_CARPET_L:
				case PIX_CARPET_M:
				case PIX_CARPET_M2:
				case PIX_CARPET_R:
				case PIX_CARPET_SMALL_HOR:
 				case PIX_CARPET_SMALL_VER:
				case PIX_CARPET_SMALL_CUP:
				case PIX_CARPET_SMALL_CAP:
				case PIX_CARPET_SMALL_LEFT:
				case PIX_CARPET_SMALL_RIGHT:
				case PIX_CARPET_SMALL_TINY:
				case PIX_DIRT_1:
				case PIX_DIRTGRASS_UL1:
				case PIX_DIRTGRASS_UR1:
				case PIX_DIRTGRASS_LL1:
				case PIX_DIRTGRASS_LR1:
				case PIX_DIRT_DARK_1:
				case PIX_DIRTGRASS_DARK_UL1:
				case PIX_DIRTGRASS_DARK_UR1:
				case PIX_DIRTGRASS_DARK_LL1:
				case PIX_DIRTGRASS_DARK_LR1:
				case PIX_PATH_1:
				case PIX_PATH_2:
				case PIX_PATH_3:
				case PIX_PATH_4:
					break;
				case PIX_TREE_M1:
				case PIX_TREE_ML:
				case PIX_TREE_MR:
				case PIX_TREE_MT:
				case PIX_TREE_T1:
					if (ob->stats()->query_bit_flags(BIT_FORESTWALK) )
						break;
					else if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left)
						break;
					else
						return 0;
				case PIX_TREE_B1:
					{
						if (ob->query_order() == Order::Weapon
						        || ob->stats()->query_bit_flags(BIT_FORESTWALK) )
							break;
						else if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left)
							break;
						else
							return 0;
					}

				case PIX_H_WALL1:
				case PIX_WALL2:
				case PIX_WALL3:
				case PIX_WALL_LL:
				case PIX_WALLTOP_H:
					return 0;

					case PIX_WALL4:
					case PIX_WALL5:
					case PIX_WALL_ARROW_GRASS:
					case PIX_WALL_ARROW_FLOOR:
					case PIX_WALL_ARROW_GRASS_DARK:
						{
							if (ob->query_order()==Order::Living)
								return 0;

							if (abs(ob->xpos - ob->owner->xpos) >
							        abs(ob->ypos - ob->owner->ypos))
								dist = abs(ob->xpos - ob->owner->xpos);
							else
								dist = abs(ob->ypos - ob->owner->ypos);

							dist -= (GRID_SIZE/2);
							if (dist < GRID_SIZE)
							{
								dist += GRID_SIZE;
							}

							if (ob->sim_rng->next(dist/GRID_SIZE))
							{
								return 0;
							}
						}
						[[fallthrough]];
					case PIX_WATER1:
				case PIX_WATER2:
				case PIX_WATER3:
				case PIX_WATERGRASS_LL:
				case PIX_WATERGRASS_LR:
				case PIX_WATERGRASS_UL:
				case PIX_WATERGRASS_UR:
				case PIX_WATERGRASS_U:
				case PIX_WATERGRASS_L:
				case PIX_WATERGRASS_R:
				case PIX_WATERGRASS_D:
				case PIX_WALLSIDE_L:
				case PIX_WALLSIDE1:
				case PIX_WALLSIDE_R:
				case PIX_WALLSIDE_C:
				case PIX_WALLSIDE_CRACK_C1:
				case PIX_TORCH1:
				case PIX_TORCH2:
				case PIX_TORCH3:
				case PIX_BRAZIER1:
				case PIX_COLUMN1:
				case PIX_COLUMN2:
				case PIX_BOULDER_1:
				case PIX_BOULDER_2:
				case PIX_BOULDER_3:
				case PIX_BOULDER_4:
					{
						if (ob->query_order() == Order::Weapon)
							break;
						else if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left)
							break;
						else
							return 0;
					}
				default:
					return 0;
			}

		}
	return 1;
}

bool LevelData::query_object_passable(float x, float y, walker  *ob)
{
	if (ob->dead)
		return 1;
	return myobmap->query_list(ob, static_cast<short>(x), static_cast<short>(y));
}

bool LevelData::query_passable(float x, float y, walker  *ob)
{
	return query_grid_passable(x, y, ob) && query_object_passable(x, y, ob);
}

// ---- Entity search ----

walker *LevelData::find_near_foe(walker  *ob)
{
	short targx, targy;
	short spread=1,xchange=0;
	short loop=0;
	short resolution = myobmap->obmapres;

	if (!ob)
	{
		Log("no ob in find near foe.\n");
		return nullptr;
	}
	targx = ob->xpos;
	targy = ob->ypos;
	spread = 1;

	while (spread < MAX_SPREAD)
	{
		for (loop=0;loop<spread;loop++)
		{
			if (!(xchange%2))
			{
				targx += resolution;
				if (targx<=0)
					return find_far_foe(ob);
				if (targx>=pixmaxx)
					return find_far_foe(ob);
			}
			else
			{
				targy += resolution;
				if (targy<=0)
					return find_far_foe(ob);
				if (targy>=pixmaxy)
					return find_far_foe(ob);
			}

			std::list<walker*>& ls = myobmap->obmap_get_list(targx,targy);
			for(auto* w : ls)
			{
				if (!(w->dead) && (ob->is_friendly(w)==0)  &&
				        (ob->sim_rng->next(w->invisibility_left/20)==0)
				   )
				{
					if (w->query_order() == Order::Living ||
					        w->query_order() == Order::Generator)
						return w;
				}
			}

		}
		xchange++;
		if (!(xchange%2))
		{
			resolution = static_cast<short>(-resolution);
			spread++;
		}
	}
	return find_far_foe(ob);
}

walker  *LevelData::find_far_foe(walker  *ob)
{
	std::int32_t distance, tempdistance;
	walker  *endfoe;

	if (!ob)
	{
		Log("no ob in find far foe.\n");
		return nullptr;
	}

	endfoe = nullptr;
	distance = 10000;
	ob->stats()->last_distance = 10000;

    for(auto& uptr : oblist)
	{
	    walker* foe = uptr.get();
		if (foe == nullptr || foe->dead)
			continue;

		if (ob->is_friendly(foe) == 0)
		{
			if (
			    (foe->query_order() == Order::Living ||
			     foe->query_order() == Order::Generator)  &&
			    (!(ob->sim_rng->next(foe->invisibility_left/20)))
			)
			{
				tempdistance = ob->distance_to_ob(foe);
				if (tempdistance < distance)
				{
					distance = tempdistance;
					endfoe = foe;
				}
			}
		}
	}
	return endfoe;
}

walker  * LevelData::find_nearest_blood(walker  *who)
{
	std::int32_t distance, newdistance;
	walker  *returnob = nullptr;

	if (!who)
		return nullptr;

	distance = 800;

	for(auto& uptr : fxlist)
	{
	    walker* w = uptr.get();
		if (w && w->query_order() == Order::Treasure &&
		        w->query_family() == FAMILY_STAIN && !w->dead)
		{
			newdistance = static_cast<std::int32_t>(who->distance_to_ob_center(w));
			if (newdistance < distance)
			{
				distance = newdistance;
				returnob = w;
			}
		}
	}
	return returnob;
}

walker* LevelData::find_nearest_player(walker *ob)
{
	walker *returnob = nullptr;
	std::uint32_t distance = 32000;
	std::uint32_t tempdistance;

	if (!ob)
		return nullptr;

	for(auto& uptr : oblist)
	{
	    walker* w = uptr.get();
		if (w && (w->user != -1) )
		{
			tempdistance = ob->distance_to_ob(w);
			if (tempdistance < distance)
			{
				distance = tempdistance;
				returnob = w;
			}
		}
	}

	return returnob;
}

std::list<walker*> LevelData::find_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob)
{
    std::list<walker*> result;

	*howmany = 0;

	if(!ob)
		return result;

	for(auto& uptr : somelist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead)
		{
			if (ob->distance_to_ob(w) <= range)
			{
			    result.push_back(w);
				(*howmany)++;
			}
		}
	}

	return result;
}

std::list<walker*> LevelData::find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob)
{
    std::list<walker*> result;
    *howmany = 0;

	if(!ob)
		return result;

	for(auto& uptr : somelist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead &&
		        (w->query_order() == Order::Living ||
		         w->query_order() == Order::Generator)
		        && (ob->is_friendly(w) == 0)
		   )
		{
			if (ob->distance_to_ob(w) <= range)
			{
			    result.push_back(w);
				(*howmany)++;
			}
		}
	}

	return result;
}

std::list<walker*> LevelData::find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range,
                                      std::int32_t* howmany, walker* ob)
{
    std::list<walker*> result;
    *howmany = 0;

	if(!ob)
		return result;

	for(auto& uptr : somelist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead &&
		        (w->query_order() == Order::Weapon)
		        && ( ob->is_friendly(w) )
		   )
		{
			if (ob->distance_to_ob(w) <= range)
			{
			    result.push_back(w);
				(*howmany)++;
			}
		}
	}

	return result;
}

std::list<walker*> LevelData::find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range,
                                      std::int32_t* howmany, walker* ob)
{
    std::list<walker*> result;
    *howmany = 0;

	if(!ob)
		return result;

	for(auto& uptr : somelist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead && w->query_order() == Order::Living
		        && ( ob->is_friendly(w) )
		   )
		{
			if (ob->distance_to_ob(w) <= range)
			{
			    result.push_back(w);
				(*howmany)++;
			}
		}
	}

	return result;
}
