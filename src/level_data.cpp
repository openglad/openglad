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

#include "level_data.h"
#include "test_trace.h"
#include "yam.h"

#include "pixie.h"
#include "gloader.h"
#include "walker.h"
#include "stats.h"
#include "smooth.h"
#include "screen.h"
#include "view.h"
#include <algorithm>
#include <format>
#include <string>


int toInt(const std::string& s);

static constexpr char VERSION_NUM = 9; // save scenario type info

static bool rw_read_exact_or_log(SDL_RWops* rwops, void* dst, size_t size, size_t count)
{
    const size_t got = SDL_RWread(rwops, dst, size, count);
    if (got != count)
    {
        Log("Read error: expected {} items, got {} (SDL: {})\n", count, got, SDL_GetError());
        return false;
    }
    return true;
}




CampaignData::CampaignData(const std::string& id)
    : id(id), title("New Campaign"), rating(0.0f), version("1.0"), suggested_power(0), first_level(1), num_levels(0)
{
    description.push_back("No description.");
}

CampaignData::~CampaignData()
{
	icon.reset();
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
        SDL_RWops* rwops = open_read_file("campaign.yaml");
        if(rwops == nullptr)
        {
            last_io_error_ = IoError::OpenReadFailed;
            unmount_campaign_package(id);
            mount_campaign_package(old_campaign);
            return false;
        }
        
        Yam yam;
        yam.set_input(rwops_read_handler, rwops);
        
        int parse_result = Yam::OK;
        while((parse_result = yam.parse_next()) == Yam::OK)
        {
            switch(yam.event.type)
            {
                case Yam::PAIR:
                    if(std::string(yam.event.scalar) == "title")
                        title = yam.event.value;
                    else if(std::string(yam.event.scalar) == "version")
                        version = yam.event.value;
                    else if(std::string(yam.event.scalar) == "authors")
                        authors = yam.event.value;
                    else if(std::string(yam.event.scalar) == "contributors")
                        contributors = yam.event.value;
                    else if(std::string(yam.event.scalar) == "description")
                    {
                        std::string desc = yam.event.value;
                        description = explode(desc, '\n');
                    }
                    else if(std::string(yam.event.scalar) == "suggested_power")
                        suggested_power = toInt(yam.event.value);
                    else if(std::string(yam.event.scalar) == "first_level")
                        first_level = toInt(yam.event.value);
                break;
                default:
                    break;
            }
        }
        if(parse_result == Yam::ERROR)
            last_io_error_ = IoError::ParseFailed;
        
        yam.close_input();
        SDL_RWclose(rwops);
        
        // TODO: Get rating from website
        rating = 0.0f;
        
        std::string icon_file = "icon.pix";
        icondata = read_pixie_file(icon_file.c_str());
        if(icondata.valid())
            icon = std::make_unique<pixie>(icondata);
        
        // Count the number of levels
        std::list<int> levels = list_levels();
        num_levels = levels.size();
        
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
        
        SDL_RWops* outfile = open_write_file("temp/campaign.yaml");
        if(outfile != nullptr)
        {
            Yam yam;
            yam.set_output(rwops_write_handler, outfile);

            yam.emit_pair("format_version", "1");
            yam.emit_pair("title", title.c_str());
            yam.emit_pair("version", version.c_str());

            std::string buf = std::format("{}", first_level);
            yam.emit_pair("first_level", buf.c_str());

            buf = std::format("{}", suggested_power);
            yam.emit_pair("suggested_power", buf.c_str());
            
            yam.emit_pair("authors", authors.c_str());
            yam.emit_pair("contributors", contributors.c_str());
            
            std::string desc;
            for(std::list<std::string>::const_iterator e = description.begin(); e != description.end();)
            {
                desc += *e;
                
                e++;
                if(e != description.end())
                    desc += '\n';
            }
            
            yam.emit_pair("description", desc.c_str());
            
            yam.close_output();
            SDL_RWclose(outfile);
            
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

        // Remount the new campaign package
        //mount_campaign_package(ascreen->current_campaign);
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
        SDL_RWops* outfile = open_write_file("temp/campaign.yaml");
        if(outfile != nullptr)
        {
            Yam yam;
            yam.set_output(rwops_write_handler, outfile);

            yam.emit_pair("format_version", "1");
            yam.emit_pair("title", title.c_str());
            yam.emit_pair("version", version.c_str());

            std::string buf = std::format("{}", first_level);
            yam.emit_pair("first_level", buf.c_str());

            buf = std::format("{}", suggested_power);
            yam.emit_pair("suggested_power", buf.c_str());
            
            yam.emit_pair("authors", authors.c_str());
            yam.emit_pair("contributors", contributors.c_str());
            
            std::string desc;
            for(std::list<std::string>::const_iterator e = description.begin(); e != description.end();)
            {
                desc += *e;
                
                e++;
                if(e != description.end())
                    desc += '\n';
            }
            
            yam.emit_pair("description", desc.c_str());
            
            yam.close_output();
            SDL_RWclose(outfile);
            
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





LevelData::LevelData(int id)
    : id(id), title("New Level"), type(0), par_value(1), time_bonus_limit(4000), pixmaxx(0), pixmaxy(0)
    , myloader(nullptr), numobs(0), topx(0), topy(0)
{
	myobmap = std::make_unique<obmap>();

    myloader = std::make_unique<loader>();

    // Load map data from a pixie format
    load_map_data(pixdata);

    // Initialize a pixie for each background piece
    for(int i = 0; i < PIX_MAX; i++)
        back[i] = std::make_unique<pixieN>(pixdata[i], 0);

    //buffers: after we set all the tiles to use acceleration, we go
    //through the tiles that have pal cycling to turn of the accel.
    back[PIX_WATER1]->set_accel(0);
    back[PIX_WATER2]->set_accel(0);
    back[PIX_WATER3]->set_accel(0);
    back[PIX_WATERGRASS_LL]->set_accel(0);
    back[PIX_WATERGRASS_LR]->set_accel(0);
    back[PIX_WATERGRASS_UL]->set_accel(0);
    back[PIX_WATERGRASS_UR]->set_accel(0);
    back[PIX_WATERGRASS_U]->set_accel(0);
    back[PIX_WATERGRASS_D]->set_accel(0);
    back[PIX_WATERGRASS_L]->set_accel(0);
    back[PIX_WATERGRASS_R]->set_accel(0);
    back[PIX_GRASSWATER_LL]->set_accel(0);
    back[PIX_GRASSWATER_LR]->set_accel(0);
    back[PIX_GRASSWATER_UL]->set_accel(0);
    back[PIX_GRASSWATER_UR]->set_accel(0);
}

LevelData::~LevelData()
{
    delete_objects();
    delete_grid();
    myloader.reset();

    myobmap.reset();
    
        
    for (int i = 0; i < PIX_MAX; i++)
    {
        pixdata[i].free();
        back[i].reset();
    }
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

walker* LevelData::add_ob(Order order, char family, [[maybe_unused]] bool atstart)
{
	if (order == Order::Weapon)
		return add_weap_ob(order, family);

    // Create the walker
    walker* w = myloader->create_walker(order, family, myscreen);
    if(w == nullptr)
        return nullptr;

    w->myobmap = this->myobmap.get();
    if (order == Order::Living)
        numobs++;
    
    oblist.push_back(std::unique_ptr<walker>(w));
    return w;
}

walker* LevelData::add_fx_ob(Order order, char family)
{
	walker* w = myloader->create_walker(order, family, myscreen, false);
    if (w == nullptr)
        return nullptr;

    w->myobmap = this->myobmap.get();

	//numobs++;
	//w->ignore = 1;
	
	fxlist.push_back(std::unique_ptr<walker>(w));
	return w;
}

walker* LevelData::add_weap_ob(Order order, char family)
{
	walker* w = myloader->create_walker(order, family, myscreen);
    if (w == nullptr)
        return nullptr;

    w->myobmap = this->myobmap.get();

    weaplist.push_back(std::unique_ptr<walker>(w));
	return w;
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
}

void LevelData::resize_grid(int width, int height)
{
    // Size is limited to one byte in the file format
    if(width < 3 || height < 3 || width > 255 || height > 255)
    {
        Log("Can't resize grid to these dimensions: {}x{}\n", width, height);
        return;
    }
    
    // Create new grid
	int size = width*height;
    unsigned char* new_grid = new unsigned char[size];
    
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
                switch(rand()%4)
                {
                    case 0:
                    new_grid[j*width + i] = PIX_GRASS1;
                    break;
                    case 1:
                    new_grid[j*width + i] = PIX_GRASS2;
                    break;
                    case 2:
                    new_grid[j*width + i] = PIX_GRASS3;
                    break;
                    case 3:
                    new_grid[j*width + i] = PIX_GRASS4;
                    break;
                }
            }
        }
    }
    
    // Delete the old, use the new
    grid.free();
    grid.data.reset(new_grid);
    grid.frames = 1;
    grid.w = width;
    grid.h = height;
	pixmaxx = grid.w * GRID_SIZE;
	pixmaxy = grid.h * GRID_SIZE;
    
    
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
    if (myscreen != nullptr && &myscreen->level_data == this)
    {
        for (auto& view : myscreen->viewob)
        {
            if (view)
                view->control = nullptr;
        }
    }
	
    // Clear the obmap references
    // Since the walker destructor removes itself from the obmap, this should be empty already.
    if(myobmap->walker_to_pos.size() > 0)
    {
        Log("obmap::walker_to_pos has {} elements left.\n", myobmap->walker_to_pos.size());
        
        // FIXME: Freeing them here does naughty things!
        /*
        std::vector<walker*> walkers;
        for(auto e = myobmap->walker_to_pos.begin(); e != myobmap->walker_to_pos.end(); e++)
        {
            Log("Order: %d, Family: %d\n", e->first->query_order(), e->first->query_family());
            walkers.push_back(e->first);
        }
        
        for(auto e = walkers.begin(); e != walkers.end(); e++)
        {
            delete *e;
        }*/
    }
    // pos_to_walker will have a bunch of 0-size lists in it
	myobmap->pos_to_walker.clear();
	myobmap->walker_to_pos.clear();
}

short load_version_2(SDL_RWops  *infile, LevelData* data)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[16] = "grid.pix";  // default grid

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
	std::string gridpix = std::string(newgrid) + ".pix";

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
short load_version_3(SDL_RWops  *infile, LevelData* data)
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
	char newgrid[16] = "grid.pix";  // default grid
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
	//buffers: PORT: make sure grid name is lowercase
	lowercase(newgrid);
	data->grid_file = newgrid;

	// Determine number of objects to load ...
	if (!rw_read_exact_or_log(infile, &listsize, 2, 1))
		return 0;
	
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
	std::string gridpix2 = std::string(newgrid) + ".pix";

    data->delete_grid();

	data->grid = read_pixie_file(gridpix2.c_str());
	data->pixmaxx = data->grid.w * GRID_SIZE;
	data->pixmaxy = data->grid.h * GRID_SIZE;

	return 1;
}

// Version 4 scenarios include a 12-byte name for EVERY walker..
short load_version_4(SDL_RWops  *infile, LevelData* data)
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
	char newgrid[16] = "grid.pix";  // default grid
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
	//buffers: PORT: make sure grid name is lowercase
	lowercase(newgrid);
	data->grid_file = newgrid;

	// Determine number of objects to load ...
	if (!rw_read_exact_or_log(infile, &listsize, 2, 1))
		return 0;
	
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
	std::string gridpix3 = std::string(newgrid) + ".pix";

    data->delete_grid();

	data->grid = read_pixie_file(gridpix3.c_str());
	data->pixmaxx = data->grid.w * GRID_SIZE;
	data->pixmaxy = data->grid.h * GRID_SIZE;

	return 1;
} // end load_version_4

// Version 5 scenarios include a 1-byte 'scenario-type' specifier after
// the grid name.
short load_version_5(SDL_RWops  *infile, LevelData* data)
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
	char newgrid[16] = "grid.pix";  // default grid
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
	std::string gridpix4 = std::string(newgrid) + ".pix";

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
short load_version_6(SDL_RWops  *infile, LevelData* data, short version)
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
    char newgrid[16] = {}; // 8-byte grid name + ".pix" + null
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
        new_guy->stats()->name = tempname;
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
    std::string gridpix5 = std::string(newgrid) + ".pix";

    data->grid = read_pixie_file(gridpix5.c_str());
    data->pixmaxx = data->grid.w * GRID_SIZE;
    data->pixmaxy = data->grid.h * GRID_SIZE;
    
    // The collected data so far
    data->title = scentitle;
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

short load_scenario_version(SDL_RWops* infile, LevelData* data, short version)
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
	TRACE("game", "LevelData::load id=%d", id);
    last_io_error_ = IoError::None;
	SDL_RWops  *infile = nullptr;
	char temptext[10] = {};
	char versionnumber = 0;
	
	// Build up the file name (scen#.fss)
	std::string thefile = std::format("scen{}.fss", id);

	// Zardus: much much better this way
	if ( !(infile = open_read_file("scen/", thefile.c_str())))
    {
        LogError("Cannot open level file for reading: {}\n", thefile);
        last_io_error_ = IoError::OpenReadFailed;
        return false;
    }

	// Are we a scenario file?
	SDL_RWread(infile, temptext, 1, 3);
	if (std::string(temptext) != "FSS")
	{
		LogError("File {} is not a valid scenario!\n", thefile);
		SDL_RWclose(infile);
        last_io_error_ = IoError::InvalidHeader;
		return false;
	}

	// Check the version number
	SDL_RWread(infile, &versionnumber, 1, 1);
    if(versionnumber < 2 || versionnumber > VERSION_NUM)
    {
        Log("Scenario {} is version-level {}, and cannot be read.\n",
            id, static_cast<int>(versionnumber));
        SDL_RWclose(infile);
        last_io_error_ = IoError::UnsupportedVersion;
        return false;
    }
    Log("Loading version {} scenario", static_cast<int>(versionnumber));
    
    // Reset the loader (which holds graphics for the objects to use)
    myloader = std::make_unique<loader>();
    
    // Do the rest of the loading
    clear();
    
    // Set default par_value
    par_value = id;
    
    short tempvalue = load_scenario_version(infile, this, versionnumber);
    SDL_RWclose(infile);
    if(tempvalue == 0)
    {
        if(last_io_error_ == IoError::None)
            last_io_error_ = IoError::ParseFailed;
        return false;
    }
    
    // Load background tiles
    {
        // Delete old tiles
        for (int i = 0; i < PIX_MAX; i++)
        {
            pixdata[i].free();
            back[i].reset();
        }

        // Load map data from a pixie format
        load_map_data(pixdata);

        // Initialize a pixie for each background piece
        for(int i = 0; i < PIX_MAX; i++)
            back[i] = std::make_unique<pixieN>(pixdata[i], 0);

        //buffers: after we set all the tiles to use acceleration, we go
        //through the tiles that have pal cycling to turn of the accel.
        back[PIX_WATER1]->set_accel(0);
        back[PIX_WATER2]->set_accel(0);
        back[PIX_WATER3]->set_accel(0);
        back[PIX_WATERGRASS_LL]->set_accel(0);
        back[PIX_WATERGRASS_LR]->set_accel(0);
        back[PIX_WATERGRASS_UL]->set_accel(0);
        back[PIX_WATERGRASS_UR]->set_accel(0);
        back[PIX_WATERGRASS_U]->set_accel(0);
        back[PIX_WATERGRASS_D]->set_accel(0);
        back[PIX_WATERGRASS_L]->set_accel(0);
        back[PIX_WATERGRASS_R]->set_accel(0);
        back[PIX_GRASSWATER_LL]->set_accel(0);
        back[PIX_GRASSWATER_LR]->set_accel(0);
        back[PIX_GRASSWATER_UL]->set_accel(0);
        back[PIX_GRASSWATER_UR]->set_accel(0);
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
	SDL_RWops  *outfile;

	// Create the full pathname for the pixie file
	fullpath += ".pix";

	lowercase (fullpath);

	if ( (outfile = open_write_file("temp/pix/", fullpath.c_str())) == nullptr )
	{
		Log("Failed to save map file: temp/pix/{}\n", fullpath);
		return false;
	}

	x = grid.w;
	y = grid.h;
	numframes = 1;
	SDL_RWwrite(outfile, &numframes, 1, 1);
	SDL_RWwrite(outfile, &x, 1, 1);
	SDL_RWwrite(outfile, &y, 1, 1);

	SDL_RWwrite(outfile, grid.data.get(), 1, (x*y));

	SDL_RWclose(outfile);        // Close the data file
	return true;
}

bool LevelData::save()
{
    last_io_error_ = IoError::None;
	Sint32 currentx, currenty;
	unsigned char temporder;
	char tempfamily;
	char tempteam, tempfacing, tempcommand;
	short shortlevel;
	char filler[20] = "MSTRMSTRMSTRMSTR"; // for RESERVED
	SDL_RWops  *outfile;
	char temptext[10] = "FSS";
	char temp_grid[20] = {};
	char temp_scen_type = 0;
	Sint32 listsize;
	Sint32 i;
	char temp_version = VERSION_NUM;
	std::string temp_filename;
	char numlines, tempwidth;
	char oneline[80];
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
	// 2-bytes (Sint32) = total objects to follow
	// List of n objects, each of 20-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte Sint32 xpos
	// 2-byte Sint32 ypos
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

	if ( (outfile = open_write_file("temp/scen/", temp_filename.c_str())) == nullptr ) // open for write
	{
		Log("Could not open file for writing: temp/scen/{}\n", temp_filename);
        last_io_error_ = IoError::OpenWriteFailed;
		return false;
	}

	// Write id header
	SDL_RWwrite(outfile, temptext, 3, 1);

	// Write version number
	SDL_RWwrite(outfile, &temp_version, 1, 1);

	// Write name of current grid...
	snprintf(temp_grid, 9, "%s", this->grid_file.c_str());  // Do NOT include extension (max 8 chars)
	SDL_RWwrite(outfile, temp_grid, 8, 1);

	// Write the scenario title, if it exists
	snprintf(scentitle, sizeof(scentitle), "%s", this->title.c_str());
	SDL_RWwrite(outfile, scentitle, 30, 1);

	// Write the scenario type info
	temp_scen_type = this->type;
	SDL_RWwrite(outfile, &temp_scen_type, 1, 1);

	// Write our par value (version 8+)
	temp_par = this->par_value;
	SDL_RWwrite(outfile, &temp_par, 2, 1);

	// Write the time limit (version 9+)
	temp_time_limit = this->time_bonus_limit;
	SDL_RWwrite(outfile, &temp_time_limit, 2, 1);

	// Determine size of object list ...
	listsize = oblist.size();

	// Also check the fx list ..
	listsize += fxlist.size();

	// And the weapon list ..
	listsize += weaplist.size();

	SDL_RWwrite(outfile, &listsize, 2, 1);

	// Okay, we've written header .. now dump the data ..
	for(auto& uptr : oblist)
	{
	    walker* w = uptr.get();
        if (w == nullptr)
        {
            Log("Unexpected nullptr object.\n");
            SDL_RWclose(outfile);
            last_io_error_ = IoError::SerializeFailed;
            return false;  // Something wrong! Too few objects..
        }
        temporder = static_cast<unsigned char>(w->query_order());
        tempfacing= w->curdir;
        tempfamily= w->query_family();
        tempteam  = w->team_num;
        tempcommand=w->query_act_type();
        currentx  = w->xpos;
        currenty  = w->ypos;
        //templevel = w->stats()->level;
        shortlevel = w->stats()->level;
        snprintf(tempname, sizeof(tempname), "%s", w->stats()->name.c_str());
        SDL_RWwrite(outfile, &temporder, 1, 1);
        SDL_RWwrite(outfile, &tempfamily, 1, 1);
        SDL_RWwrite(outfile, &currentx, 2, 1);
        SDL_RWwrite(outfile, &currenty, 2, 1);
        SDL_RWwrite(outfile, &tempteam, 1, 1);
        SDL_RWwrite(outfile, &tempfacing, 1, 1);
        SDL_RWwrite(outfile, &tempcommand, 1, 1);
        SDL_RWwrite(outfile, &shortlevel, 2, 1);
        SDL_RWwrite(outfile, tempname, 12, 1);
        SDL_RWwrite(outfile, filler, 10, 1);
	}

	// Now dump the fxlist data ..
	for(auto& uptr : fxlist)
	{
	    walker* ob = uptr.get();
        if (ob == nullptr)
        {
            Log("Unexpected nullptr fx object.\n");
            SDL_RWclose(outfile);
            last_io_error_ = IoError::SerializeFailed;
            return false;  // Something wrong! Too few objects..
        }
        temporder = static_cast<unsigned char>(ob->query_order());
        tempfacing= ob->curdir;
        tempfamily= ob->query_family();
        tempteam  = ob->team_num;
        tempcommand=ob->query_act_type();
        currentx  = ob->xpos;
        currenty  = ob->ypos;
        //templevel = ob->stats()->level;
        shortlevel = ob->stats()->level;
        snprintf(tempname, sizeof(tempname), "%s", ob->stats()->name.c_str());
        SDL_RWwrite(outfile, &temporder, 1, 1);
        SDL_RWwrite(outfile, &tempfamily, 1, 1);
        SDL_RWwrite(outfile, &currentx, 2, 1);
        SDL_RWwrite(outfile, &currenty, 2, 1);
        SDL_RWwrite(outfile, &tempteam, 1, 1);
        SDL_RWwrite(outfile, &tempfacing, 1, 1);
        SDL_RWwrite(outfile, &tempcommand, 1, 1);
        SDL_RWwrite(outfile, &shortlevel, 2, 1);
        SDL_RWwrite(outfile, tempname, 12, 1);
        SDL_RWwrite(outfile, filler, 10, 1);
	}

	// Now dump the weaplist data ..
	for(auto& uptr : weaplist)
	{
	    walker* ob = uptr.get();
        if (ob == nullptr)
        {
            Log("Unexpected nullptr weap object.\n");
            SDL_RWclose(outfile);
            last_io_error_ = IoError::SerializeFailed;
            return false;  // Something wrong! Too few objects..
        }
        temporder = static_cast<unsigned char>(ob->query_order());
        tempfacing= ob->curdir;
        tempfamily= ob->query_family();
        tempteam  = ob->team_num;
        tempcommand=ob->query_act_type();
        currentx  = ob->xpos;
        currenty  = ob->ypos;
        shortlevel = ob->stats()->level;
        snprintf(tempname, sizeof(tempname), "%s", ob->stats()->name.c_str());
        SDL_RWwrite(outfile, &temporder, 1, 1);
        SDL_RWwrite(outfile, &tempfamily, 1, 1);
        SDL_RWwrite(outfile, &currentx, 2, 1);
        SDL_RWwrite(outfile, &currenty, 2, 1);
        SDL_RWwrite(outfile, &tempteam, 1, 1);
        SDL_RWwrite(outfile, &tempfacing, 1, 1);
        SDL_RWwrite(outfile, &tempcommand, 1, 1);
        SDL_RWwrite(outfile, &shortlevel, 2, 1);
        SDL_RWwrite(outfile, tempname, 12, 1);
        SDL_RWwrite(outfile, filler, 10, 1);
	}

	numlines = this->description.size();
	//printf("saving %d lines\n", numlines);

	SDL_RWwrite(outfile, &numlines, 1, 1);
	for (auto& line : this->description)
	{
		snprintf(oneline, sizeof(oneline), "%s", line.c_str());
		tempwidth = static_cast<unsigned char>(line.size());
		SDL_RWwrite(outfile, &tempwidth, 1, 1);
		SDL_RWwrite(outfile, oneline, tempwidth, 1);
	}

	SDL_RWclose(outfile);
	
	// Save map (grid) file
	save_grid_file(grid_file.c_str(), grid);
	
	
	Log("Scenario saved.\n");

    last_io_error_ = IoError::None;
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

void LevelData::set_draw_pos(Sint32 topx, Sint32 topy)
{
    this->topx = topx;
    this->topy = topy;
}

void LevelData::add_draw_pos(Sint32 topx, Sint32 topy)
{
    this->topx += topx;
    this->topy += topy;
}

void LevelData::draw(screen* myscreen)
{
	short i;
	for (i=0; i < myscreen->numviews; i++)
    {
        myscreen->viewob[i]->redraw(this, false);  // Don't draw the radar here
    }
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
