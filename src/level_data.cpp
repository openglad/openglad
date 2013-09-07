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
#include "yam.h"

#include "pixie.h"
#include "loader.h"
#include "walker.h"
#include "stats.h"
#include "smooth.h"
#include "screen.h"
#include "view.h"


int toInt(const std::string& s);

#define VERSION_NUM (char) 8 // save scenario type info




CampaignData::CampaignData(const std::string& id)
    : id(id), title("New Campaign"), rating(0.0f), version("1.0"), suggested_power(0), first_level(1), num_levels(0), icon(NULL)
{
    description.push_back("No description.");
}

CampaignData::~CampaignData()
{
	delete icon;
	icondata.free();
}


bool CampaignData::load()
{
    std::string old_campaign = get_mounted_campaign();
    unmount_campaign_package(old_campaign);
    
    // Load the campaign data from <user_data>/scen/<id>.glad
    if(mount_campaign_package(id))
    {
        SDL_RWops* rwops = open_read_file("campaign.yaml");
        
        Yam yam;
        yam.set_input(rwops_read_handler, rwops);
        
        while(yam.parse_next() == Yam::OK)
        {
            switch(yam.event.type)
            {
                case Yam::PAIR:
                    if(strcmp(yam.event.scalar, "title") == 0)
                        title = yam.event.value;
                    else if(strcmp(yam.event.scalar, "version") == 0)
                        version = yam.event.value;
                    else if(strcmp(yam.event.scalar, "authors") == 0)
                        authors = yam.event.value;
                    else if(strcmp(yam.event.scalar, "contributors") == 0)
                        contributors = yam.event.value;
                    else if(strcmp(yam.event.scalar, "description") == 0)
                    {
                        std::string desc = yam.event.value;
                        description = explode(desc, '\n');
                    }
                    else if(strcmp(yam.event.scalar, "suggested_power") == 0)
                        suggested_power = toInt(yam.event.value);
                    else if(strcmp(yam.event.scalar, "first_level") == 0)
                        first_level = toInt(yam.event.value);
                break;
                default:
                    break;
            }
        }
        
        yam.close_input();
        SDL_RWclose(rwops);
        
        // TODO: Get rating from website
        rating = 0.0f;
        
        std::string icon_file = "icon.pix";
        icondata = read_pixie_file(icon_file.c_str());
        if(icondata.valid())
            icon = new pixie(icondata, myscreen);
        
        // Count the number of levels
        std::list<int> levels = list_levels();
        num_levels = levels.size();
        
        unmount_campaign_package(id);
    }
    
    mount_campaign_package(old_campaign);
    
    return true;
}

bool CampaignData::save()
{
    cleanup_unpacked_campaign();
    
    bool result = true;
    if(unpack_campaign(id))
    {
        // Unmount campaign while it is changed
        //unmount_campaign_package(ascreen->current_campaign);
        
        SDL_RWops* outfile = open_write_file("temp/campaign.yaml");
        if(outfile != NULL)
        {
            char buf[40];
            
            Yam yam;
            yam.set_output(rwops_write_handler, outfile);
            
            yam.emit_pair("format_version", "1");
            yam.emit_pair("title", title.c_str());
            yam.emit_pair("version", version.c_str());
            
            snprintf(buf, 40, "%d", first_level);
            yam.emit_pair("first_level", buf);
            
            snprintf(buf, 40, "%d", suggested_power);
            yam.emit_pair("suggested_power", buf);
            
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
        }
        
        if(result)
        {
            if(repack_campaign(id))
            {
                Log("Campaign saved.\n");
            }
            else
            {
                Log("Save failed: Could not repack campaign: %s\n", id.c_str());
                result = false;
            }
        }
        
        // Remount the new campaign package
        //mount_campaign_package(ascreen->current_campaign);
    }
    else
    {
        Log("Save failed: Could not unpack campaign: %s\n", id.c_str());
        result = false;
    }
    cleanup_unpacked_campaign();
    
    return result;
}

bool CampaignData::save_as(const std::string& new_id)
{
    cleanup_unpacked_campaign();
    
    bool result = true;
    // Unpack the campaign
    if(unpack_campaign(id))
    {
        // Save the descriptor file
        SDL_RWops* outfile = open_write_file("temp/campaign.yaml");
        if(outfile != NULL)
        {
            char buf[40];
            
            Yam yam;
            yam.set_output(rwops_write_handler, outfile);
            
            yam.emit_pair("format_version", "1");
            yam.emit_pair("title", title.c_str());
            yam.emit_pair("version", version.c_str());
            
            snprintf(buf, 40, "%d", first_level);
            yam.emit_pair("first_level", buf);
            
            snprintf(buf, 40, "%d", suggested_power);
            yam.emit_pair("suggested_power", buf);
            
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
                Log("Save failed: Could not repack campaign: %s\n", id.c_str());
                result = false;
            }
        }
    }
    else
    {
        Log("Save failed: Could not unpack campaign: %s\n", id.c_str());
        result = false;
    }
    cleanup_unpacked_campaign();
    
    return result;
}

std::string CampaignData::getDescriptionLine(int i)
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
    : id(id), title("New Level"), type(0), par_value(1), pixmaxx(0), pixmaxy(0)
    , myloader(NULL), numobs(0), oblist(NULL), fxlist(NULL), weaplist(NULL), topx(0), topy(0)
{
    for (int i = 0; i < PIX_MAX; i++)
    {
        back[i] = NULL;
    }
    
	myobmap = new obmap(200*GRID_SIZE, 200*GRID_SIZE);
	
    myloader = new loader;
	
    // Load map data from a pixie format
    load_map_data(pixdata);

    // Initialize a pixie for each background piece
    for(int i = 0; i < PIX_MAX; i++)
        back[i] = new pixieN(pixdata[i], myscreen, 0);

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
    delete myloader;
    
    delete myobmap;
    
        
    for (int i = 0; i < PIX_MAX; i++)
    {
        pixdata[i].free();
        
        if (back[i])
        {
            delete back[i];
            back[i] = NULL;
        }
    }
}

void LevelData::clear()
{
    delete_objects();
    delete_grid();
    
    delete myobmap;
	myobmap = new obmap(200*GRID_SIZE, 200*GRID_SIZE);
    
    title = "New Level";
    type = 0;
    par_value = 1;
    
    topx = 0;
    topy = 0;
}

walker* LevelData::add_ob(char order, char family, bool atstart)
{
	oblink  *here = NULL;

	/*         ------      ------
	          | ob  |     | ob  |
	oblist -> |-----      |-----
	          |   ------->|  --------> Null
	          ------      ------
	*/
	// Point to end of oblink chain

	if (order == ORDER_WEAPON)
		return add_weap_ob(order, family);

	// Going to force at head of list for now, for speed, if it works
	//if (atstart) // add to the end of the list instead of the end ..
	if (oblist)
	{
		here = new oblink;
		here->ob = myloader->create_walker(order, family, myscreen);
		if (!here->ob)
			return NULL;
        here->ob->myobmap = this->myobmap;
		here->next = oblist;
		oblist = here;
		if (order == ORDER_LIVING)
			numobs++;
		return here->ob;
	}
	else // we're the first and only ..
	{
		here = new oblink;
		here->ob = myloader->create_walker(order, family, myscreen);
		if (!here->ob)
			return NULL;
        here->ob->myobmap = this->myobmap;
		here->next = NULL;
		oblist = here;
		if (order == ORDER_LIVING)
			numobs++;
		return here->ob;
	}

	here = oblist;

	if (oblist)
	{
		while(here->next)
			here = here->next;
		here->next = new oblink;
		here = here->next;
	}
	else  // oblink is null
	{
		here = new oblink;
		oblist = here;
	}

	here->next = NULL;
	here->ob = myloader->create_walker(order, family, myscreen);
    here->ob->myobmap = this->myobmap;

	if (order == ORDER_LIVING)
		numobs++;
	return here->ob;
}

walker* LevelData::add_fx_ob(char order, char family)
{
	oblink  *here = NULL;

	here = fxlist;
	if (fxlist)
	{
		while(here->next)
			here = here->next;
		here->next = new oblink;
		here = here->next;
	}
	else  // oblink is null
	{
		here = new oblink;
		fxlist = here;
	}

	here->next = NULL;
	here->ob = myloader->create_walker(order, family, myscreen, false);
    here->ob->myobmap = this->myobmap;

	//numobs++;
	//here->ob->ignore = 1;
	return here->ob;
}

walker* LevelData::add_weap_ob(char order, char family)
{
	oblink *here = new oblink;

	here->ob = myloader->create_walker(order, family, myscreen);
    here->ob->myobmap = this->myobmap;
	here->next = weaplist;
	weaplist = here;

	return here->ob;
}

short LevelData::remove_ob(walker  *ob, short no_delete)
{
	oblink  *here, *prev;

	if (ob && ob->query_order() == ORDER_LIVING)
		numobs--;

	here = weaplist; //most common case
	if (here)
		if (here->ob && here->ob == ob) // this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			weaplist = weaplist->next;
			delete here;
			return 1;
		}

	prev = here;
	while (here)
	{
		if (here->ob && here->ob == ob) //this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			prev->next = here->next; // remove this link
			delete here;
			return 1; //we found it, at least
		}
		prev = here;
		here = here->next;
	}


	here = fxlist; //less common
	if (here)
		if (here->ob && here->ob == ob) // this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			fxlist = fxlist->next;
			delete here;
			return 1;
		}

	prev = here;
	while (here)
	{
		if (here->ob && here->ob == ob) //this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			prev->next = here->next; // remove this link
			delete here;
			return 1; //we found it, at least
		}
		prev = here;
		here = here->next;
	}


	here = oblist; //less common
	if (here)
		if (here->ob && here->ob == ob) // this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			oblist = oblist->next;
			delete here;
			return 1;
		}

	prev = here;
	while (here)
	{
		if (here->ob && here->ob == ob) //this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			prev->next = here->next; // remove this link
			delete here;
			return 1; //we found it, at least
		}
		prev = here;
		here = here->next;
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
    grid.data = new unsigned char[size];
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
        Log("Can't resize grid to these dimensions: %dx%d\n", width, height);
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
    grid.data = new_grid;
    grid.frames = 1;
    grid.w = width;
    grid.h = height;
	pixmaxx = grid.w * GRID_SIZE;
	pixmaxy = grid.h * GRID_SIZE;
    
    
    // Delete objects that fell off the map
	oblink* here;
	oblink* next;
	oblink* prev = NULL;
    
    int x = 0;
    int y = 0;
    int w = grid.w * GRID_SIZE;
    int h = grid.h * GRID_SIZE;
	here = oblist;
	while (here)
	{
        next = here->next;
		if(here->ob == NULL || (x > here->ob->xpos || here->ob->xpos >= x + w || y > here->ob->ypos || here->ob->ypos >= y + h))
		{
		    // Delete the object and the node.  Prev doesn't change
			delete here->ob;
			here->ob = NULL;
			if(prev == NULL)
                oblist = next;
            else
                prev->next = next;
			delete here;
		}
		else
            prev = here;
		
		here = next;
	}
	
	here = fxlist;
	while (here)
	{
        next = here->next;
		if(here->ob == NULL || (x > here->ob->xpos || here->ob->xpos >= x + w || y > here->ob->ypos || here->ob->ypos >= y + h))
		{
		    // Delete the object and the node.  Prev doesn't change
			delete here->ob;
			here->ob = NULL;
			if(prev == NULL)
                fxlist = next;
            else
                prev->next = next;
			delete here;
		}
		else
            prev = here;
		
		here = next;
	}
	
	here = weaplist;
	while (here)
	{
        next = here->next;
		if(here->ob == NULL || (x > here->ob->xpos || here->ob->xpos >= x + w || y > here->ob->ypos || here->ob->ypos >= y + h))
		{
		    // Delete the object and the node.  Prev doesn't change
			delete here->ob;
			here->ob = NULL;
			if(prev == NULL)
                weaplist = next;
            else
                prev->next = next;
			delete here;
		}
		else
            prev = here;
		
		here = next;
	}
}

void LevelData::delete_objects()
{
    oblink* next;
	oblink *fx = fxlist;

	while (fx)
	{
	    next = fx->next;
	    
        delete fx->ob;
        fx->ob = NULL;
        delete fx;
		
		fx = next;
	}

	fx = oblist;
	while (fx)
	{
	    next = fx->next;
	    
        delete fx->ob;
        fx->ob = NULL;
        delete fx;
		
		fx = next;
	}

	fx = weaplist;
	while (fx)
	{
	    next = fx->next;
	    
        delete fx->ob;
        fx->ob = NULL;
        delete fx;
		
		fx = next;
	}
    
    oblist = NULL;
    fxlist = NULL;
    weaplist = NULL;

	numobs = 0;
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
	char newgrid[12] = "grid.pix";  // default grid

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
	SDL_RWread(infile, newgrid, 8, 1);
	newgrid[8] = '\0';
	//buffers: PORT: make sure grid name is lowercase
	lowercase(newgrid);
	data->grid_file = newgrid;

	// Determine number of objects to load ...
	SDL_RWread(infile, &listsize, 2, 1);
	
    data->delete_objects();

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		SDL_RWread(infile, &temporder, 1, 1);
		SDL_RWread(infile, &tempfamily, 1, 1);
		SDL_RWread(infile, &currentx, 2, 1);
		SDL_RWread(infile, &currenty, 2, 1);
		SDL_RWread(infile, &tempteam, 1, 1);
		SDL_RWread(infile, &tempfacing, 1, 1);
		SDL_RWread(infile, &tempcommand, 1, 1);
		SDL_RWread(infile, tempreserved, 11, 1);
		if (temporder == ORDER_TREASURE)
			new_guy = data->add_fx_ob(temporder, tempfamily);  // create new object
		else
			new_guy = data->add_ob(temporder, tempfamily);  // create new object
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
	strcat(newgrid, ".pix");
	
    data->delete_grid();
	
	data->grid = read_pixie_file(newgrid);
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
	char newgrid[12] = "grid.pix";  // default grid
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
	SDL_RWread(infile, newgrid, 8, 1);
	//buffers: PORT: make sure grid name is lowercase
	lowercase((char *)newgrid);
	data->grid_file = newgrid;

	// Determine number of objects to load ...
	SDL_RWread(infile, &listsize, 2, 1);
	
    data->delete_objects();

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		SDL_RWread(infile, &temporder, 1, 1);
		SDL_RWread(infile, &tempfamily, 1, 1);
		SDL_RWread(infile, &currentx, 2, 1);
		SDL_RWread(infile, &currenty, 2, 1);
		SDL_RWread(infile, &tempteam, 1, 1);
		SDL_RWread(infile, &tempfacing, 1, 1);
		SDL_RWread(infile, &tempcommand, 1, 1);
		SDL_RWread(infile, &templevel, 1, 1);
		SDL_RWread(infile, tempreserved, 10, 1);
		if (temporder == ORDER_TREASURE)
			//              new_guy = master->add_fx_ob(temporder, tempfamily);  // create new object
			new_guy = data->add_ob(temporder, tempfamily, 1); // add to top of list
		else
			new_guy = data->add_ob(temporder, tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = tempteam;
		new_guy->stats->level = templevel;
	}

	// Now get the lines of text to read ..
	SDL_RWread(infile, &numlines, 1, 1);

	for (i=0; i < numlines; i++)
	{
		SDL_RWread(infile, &tempwidth, 1, 1);
		SDL_RWread(infile, oneline, tempwidth, 1);
		oneline[(int)tempwidth] = 0;
		data->description.push_back(oneline);
	}


	// Now read the grid file to our master screen ..
	strcat(newgrid, ".pix");
	
    data->delete_grid();
    
	data->grid = read_pixie_file(newgrid);
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
	char newgrid[12] = "grid.pix";  // default grid
	char oneline[80];
	char numlines, tempwidth;
	char tempname[12];


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
	SDL_RWread(infile, newgrid, 8, 1);
	//buffers: PORT: make sure grid name is lowercase
	lowercase((char *)newgrid);
	data->grid_file = newgrid;

	// Determine number of objects to load ...
	SDL_RWread(infile, &listsize, 2, 1);
	
    data->delete_objects();

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		SDL_RWread(infile, &temporder, 1, 1);
		SDL_RWread(infile, &tempfamily, 1, 1);
		SDL_RWread(infile, &currentx, 2, 1);
		SDL_RWread(infile, &currenty, 2, 1);
		SDL_RWread(infile, &tempteam, 1, 1);
		SDL_RWread(infile, &tempfacing, 1, 1);
		SDL_RWread(infile, &tempcommand, 1, 1);
		SDL_RWread(infile, &templevel, 1, 1);
		SDL_RWread(infile, tempname, 12, 1);
		SDL_RWread(infile, tempreserved, 10, 1);
		if (temporder == ORDER_TREASURE)
			//new_guy = data->add_ob(temporder, tempfamily, 1); // add to top of list
			new_guy = data->add_fx_ob(temporder, tempfamily);
		else
			new_guy = data->add_ob(temporder, tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = tempteam;
		new_guy->stats->level = templevel;
		strcpy(new_guy->stats->name, tempname);
		if (strlen(tempname) > 1)                      //chad 5/25/95
			new_guy->stats->set_bit_flags(BIT_NAMED, 1);

	}

	// Now get the lines of text to read ..
	SDL_RWread(infile, &numlines, 1, 1);

	for (i=0; i < numlines; i++)
	{
		SDL_RWread(infile, &tempwidth, 1, 1);
		SDL_RWread(infile, oneline, tempwidth, 1);
		oneline[(int)tempwidth] = 0;
		data->description.push_back(oneline);
	}

	// Now read the grid file...
	strcat(newgrid, ".pix");
	
    data->delete_grid();
    
	data->grid = read_pixie_file(newgrid);
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
	char newgrid[12] = "grid.pix";  // default grid
	char new_scen_type; // read the scenario type
	char oneline[80];
	char numlines, tempwidth;
	char tempname[12];
	oblink *here;

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
	SDL_RWread(infile, newgrid, 8, 1);
	//buffers: PORT: make sure grid name is lowercase
	lowercase((char *)newgrid);
	data->grid_file = newgrid;

	// Get the scenario type information
	SDL_RWread(infile, &new_scen_type, 1, 1);
	data->type = new_scen_type;

	// Determine number of objects to load ...
	SDL_RWread(infile, &listsize, 2, 1);
	
    data->delete_objects();

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		SDL_RWread(infile, &temporder, 1, 1);
		SDL_RWread(infile, &tempfamily, 1, 1);
		SDL_RWread(infile, &currentx, 2, 1);
		SDL_RWread(infile, &currenty, 2, 1);
		SDL_RWread(infile, &tempteam, 1, 1);
		SDL_RWread(infile, &tempfacing, 1, 1);
		SDL_RWread(infile, &tempcommand, 1, 1);
		SDL_RWread(infile, &templevel, 1, 1);
		SDL_RWread(infile, tempname, 12, 1);
		SDL_RWread(infile, tempreserved, 10, 1);
		if (temporder == ORDER_TREASURE)
			new_guy = data->add_fx_ob(temporder, tempfamily);
		else
			new_guy = data->add_ob(temporder, tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = tempteam;
		new_guy->stats->level = templevel;
		strcpy(new_guy->stats->name, tempname);
		if (strlen(tempname) > 1)                      //chad 5/25/95
			new_guy->stats->set_bit_flags(BIT_NAMED, 1);

	}

	// Now get the lines of text to read ..
	SDL_RWread(infile, &numlines, 1, 1);

	for (i=0; i < numlines; i++)
	{
		SDL_RWread(infile, &tempwidth, 1, 1);
		SDL_RWread(infile, oneline, tempwidth, 1);
		oneline[(int)tempwidth] = 0;
		data->description.push_back(oneline);
	}

	// Now read the grid file to our master screen ..
	strcat(newgrid, ".pix");
	
    data->delete_grid();
    
	data->grid = read_pixie_file(newgrid);
	data->pixmaxx = data->grid.w * GRID_SIZE;
	data->pixmaxy = data->grid.h * GRID_SIZE;
	
	data->mysmoother.set_target(data->grid);

	// Fix up doors, etc.
	here = data->weaplist;
	while (here)
	{
		if (here->ob && here->ob->query_family()==FAMILY_DOOR)
		{
			if (data->mysmoother.query_genre_x_y(here->ob->xpos/GRID_SIZE,
			        (here->ob->ypos/GRID_SIZE)-1)==TYPE_WALL)
			{
				here->ob->set_frame(1);  // turn sideways ..
			}
		}
		here = here->next;
	}

	return 1;
} // end load_version_5

#define READ_OR_RETURN(ctx, ptr, size, n) \
do{ \
    if(!SDL_RWread(ctx, ptr, size, n)) \
    { \
        Log("Read error: %s\n", SDL_GetError()); \
        return 0; \
    } \
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
    char newgrid[12];
    memset(newgrid, 0, 12);
    char new_scen_type; // read the scenario type
    char oneline[80];
    memset(oneline, 0, 80);
    char numlines = 0, tempwidth;
    char tempname[12];
    memset(tempname, 0, 12);
    oblink *here;
    char scentitle[30];
    memset(scentitle, 0, 30);
    short temp_par;

    // Format of a scenario object list file version 6/7 is:
    // 3-byte header: 'FSS'
    // 1-byte version #
    // ----- (above is already determined by now)
    // 8-byte string = grid name to load
    // 30-byte scenario title (ver 6+)
    // 1-byte char = scenario type, default is 0
    // 2-bytes par-value, v.8+
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
    lowercase((char *)newgrid);
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
        if (temporder == ORDER_TREASURE)
            new_guy = data->add_fx_ob(temporder, tempfamily);
        else
            new_guy = data->add_ob(temporder, tempfamily);  // create new object
        if (!new_guy)
        {
            Log("Error creating object when loading.\n");
            return 0;
        }
        
        new_guy->setxy(currentx, currenty);
        new_guy->team_num = tempteam;
        if (version >= 7)
            new_guy->stats->level = shortlevel;
        else
            new_guy->stats->level = templevel;
        strcpy(new_guy->stats->name, tempname);
        if (strlen(tempname) > 1)                      //chad 5/25/95
            new_guy->stats->set_bit_flags(BIT_NAMED, 1);

    }
    
    
    // Now get the lines of text to read ..
    READ_OR_RETURN(infile, &numlines, 1, 1);
    std::list<std::string> desc_lines;
    for (i=0; i < numlines; i++)
    {
        READ_OR_RETURN(infile, &tempwidth, 1, 1);
        if(tempwidth > 0)
        {
            READ_OR_RETURN(infile, oneline, tempwidth, 1);
            oneline[(int)tempwidth] = 0;
        }
        else
            oneline[0] = 0;
        desc_lines.push_back(oneline);
    }

    
    // Now read the grid file to our master screen ..
    strcat(newgrid, ".pix");
    
    data->grid = read_pixie_file(newgrid);
    data->pixmaxx = data->grid.w * GRID_SIZE;
    data->pixmaxy = data->grid.h * GRID_SIZE;
    
    // The collected data so far
    data->title = scentitle;
    data->type = new_scen_type;
    data->par_value = temp_par;
    data->description = desc_lines;
    data->mysmoother.set_target(data->grid);

    // Fix up doors, etc.
    here = data->weaplist;
    while (here)
    {
        if (here->ob && here->ob->query_family()==FAMILY_DOOR)
        {
            if (data->mysmoother.query_genre_x_y(here->ob->xpos/GRID_SIZE,
                    (here->ob->ypos/GRID_SIZE)-1)==TYPE_WALL)
            {
                here->ob->set_frame(1);  // turn sideways ..
            }
        }
        here = here->next;
    }
    
    return 1;
} // end load_version_6

short load_scenario_version(SDL_RWops* infile, LevelData* data, short version)
{
    if(data == NULL)
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
			result = load_version_6(infile, data, version);
			break;
		default:
			Log("Scenario %d is version-level %d, and cannot be read.\n",
			       data->id, version);
			break;
	}
    
	return result;
}

bool LevelData::load()
{
	SDL_RWops  *infile = NULL;
	char temptext[10];
	memset(temptext, 0, 10);
	char versionnumber = 0;
	
	// Build up the file name (scen#.fss)
	std::string thefile = "scen";
	char buf[10];
	snprintf(buf, 10, "%d.fss", id);
	thefile += buf;

	// Zardus: much much better this way
	if ( !(infile = open_read_file("scen/", thefile.c_str())))
    {
        Log("Cannot open level file for reading: %s", thefile.c_str());
        return false;
    }

	// Are we a scenario file?
	SDL_RWread(infile, temptext, 1, 3);
	if (strcmp(temptext, "FSS") != 0)
	{
		Log("File %s is not a valid scenario!\n", thefile.c_str());
		SDL_RWclose(infile);
		return false;
	}

	// Check the version number
	SDL_RWread(infile, &versionnumber, 1, 1);
    Log("Loading version %d scenario", versionnumber);
    
    // Reset the loader (which holds graphics for the objects to use)
    delete myloader;
    myloader = new loader;
    
    // Do the rest of the loading
    clear();
    
    // Set default par_value
    par_value = id;
    
    short tempvalue = load_scenario_version(infile, this, versionnumber);
    SDL_RWclose(infile);
    
    // Load background tiles
    {
        // Delete old tiles
        for (int i = 0; i < PIX_MAX; i++)
        {
            pixdata[i].free();
            
            if (back[i])
            {
                delete back[i];
                back[i] = NULL;
            }
        }
        
        // Load map data from a pixie format
        load_map_data(pixdata);

        // Initialize a pixie for each background piece
        for(int i = 0; i < PIX_MAX; i++)
            back[i] = new pixieN(pixdata[i], myscreen, 0);

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
    
	return (tempvalue != 0);
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

	if ( (outfile = open_write_file("temp/pix/", fullpath.c_str())) == NULL )
	{
		Log("Failed to save map file: %s%s\n", "temp/pix/", fullpath.c_str());
		return false;
	}

	x = grid.w;
	y = grid.h;
	numframes = 1;
	SDL_RWwrite(outfile, &numframes, 1, 1);
	SDL_RWwrite(outfile, &x, 1, 1);
	SDL_RWwrite(outfile, &y, 1, 1);

	SDL_RWwrite(outfile, grid.data, 1, (x*y));

	SDL_RWclose(outfile);        // Close the data file
	return true;
}

bool LevelData::save()
{
	Sint32 currentx, currenty;
	char temporder, tempfamily;
	char tempteam, tempfacing, tempcommand;
	short shortlevel;
	char filler[20] = "MSTRMSTRMSTRMSTR"; // for RESERVED
	SDL_RWops  *outfile;
	char temptext[10] = "FSS";
	char temp_grid[20];
    memset(temp_grid, 0, 20);
	char temp_scen_type = 0;
	oblink* head = NULL;
	Sint32 listsize;
	Sint32 i;
	char temp_version = VERSION_NUM;
	char temp_filename[80];
	char numlines, tempwidth;
	char oneline[80];
	char tempname[12];
    memset(tempname, 0, 12);
	char scentitle[30];
    memset(scentitle, 0, 30);
	short temp_par;

	// Format of a scenario object list file is: (ver. 8)
	// 3-byte header: 'FSS'
	// 1-byte version number (from graph.h)
	// 8-byte grid file name
	// 30-byte scenario title
	// 1-byte scenario_type
	// 2-bytes par-value for level
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
	snprintf(temp_filename, 80, "scen%d.fss", this->id);

	if ( (outfile = open_write_file("temp/scen/", temp_filename)) == NULL ) // open for write
	{
		Log("Could not open file for writing: %s%s\n", "temp/scen/", temp_filename);
		return false;
	}

	// Write id header
	SDL_RWwrite(outfile, temptext, 3, 1);

	// Write version number
	SDL_RWwrite(outfile, &temp_version, 1, 1);

	// Write name of current grid...
	strncpy(temp_grid, this->grid_file.c_str(), 8);  // Do NOT include extension
	SDL_RWwrite(outfile, temp_grid, 8, 1);

	// Write the scenario title, if it exists
	strcpy(scentitle, this->title.c_str());
	SDL_RWwrite(outfile, scentitle, 30, 1);

	// Write the scenario type info
	temp_scen_type = this->type;
	SDL_RWwrite(outfile, &temp_scen_type, 1, 1);

	// Write our par value (version 8+)
	temp_par = this->par_value;
	SDL_RWwrite(outfile, &temp_par, 2, 1);

	// Determine size of object list ...
	listsize = 0;
	head = this->oblist;
	while (head)
	{
		if (head->ob)
			listsize++;
		head = head->next;
	} // end of oblist-size check

	// Also check the fx list ..
	head = this->fxlist;
	while (head)
	{
		if (head->ob)
			listsize++;
		head = head->next;
	} // end of fxlist-size check

	// And the weapon list ..
	head = this->weaplist;
	while (head)
	{
		if (head->ob)
			listsize++;
		head = head->next;
	} // end of weaplist-size check

	SDL_RWwrite(outfile, &listsize, 2, 1);

	// Okay, we've written header .. now dump the data ..
	head = this->oblist;  // back to head of list
	while (head)
	{
		if (head->ob)
		{
			if (!head)
            {
                Log("Unexpected NULL object.\n");
                SDL_RWclose(outfile);
				return false;  // Something wrong! Too few objects..
            }
			temporder = head->ob->query_order();
			tempfacing= head->ob->curdir;
			tempfamily= head->ob->query_family();
			tempteam  = head->ob->team_num;
			tempcommand=head->ob->query_act_type();
			currentx  = head->ob->xpos;
			currenty  = head->ob->ypos;
			//templevel = head->ob->stats->level;
			shortlevel = head->ob->stats->level;
			strcpy(tempname, head->ob->stats->name);
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
		// Advance to next object ..
		head = head->next;
	}

	// Now dump the fxlist data ..
	head = this->fxlist;  // back to head of list
	while (head)
	{
		if (head->ob)
		{
			if (!head)
            {
                Log("Unexpected NULL fx object.\n");
                SDL_RWclose(outfile);
				return false;  // Something wrong! Too few objects..
            }
			temporder = head->ob->query_order();
			tempfacing= head->ob->curdir;
			tempfamily= head->ob->query_family();
			tempteam  = head->ob->team_num;
			tempcommand=head->ob->query_act_type();
			currentx  = head->ob->xpos;
			currenty  = head->ob->ypos;
			//templevel = head->ob->stats->level;
			shortlevel = head->ob->stats->level;
			strcpy(tempname, head->ob->stats->name);
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
		// Advance to next object ..
		head = head->next;
	}

	// Now dump the weaplist data ..
	head = this->weaplist;  // back to head of list
	while (head)
	{
		if (head->ob)
		{
			if (!head)
            {
                Log("Unexpected NULL weap object.\n");
                SDL_RWclose(outfile);
				return false;  // Something wrong! Too few objects..
            }
			temporder = head->ob->query_order();
			tempfacing= head->ob->curdir;
			tempfamily= head->ob->query_family();
			tempteam  = head->ob->team_num;
			tempcommand=head->ob->query_act_type();
			currentx  = head->ob->xpos;
			currenty  = head->ob->ypos;
			shortlevel = head->ob->stats->level;
			strcpy(tempname, head->ob->stats->name);
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
		// Advance to next object ..
		head = head->next;
	}

	numlines = this->description.size();
	//printf("saving %d lines\n", numlines);

	SDL_RWwrite(outfile, &numlines, 1, 1);
	std::list<std::string>::iterator e = this->description.begin();
	for (i=0; i < numlines; i++)
	{
		strcpy(oneline, e->c_str());
		tempwidth = strlen(oneline);
		SDL_RWwrite(outfile, &tempwidth, 1, 1);
		SDL_RWwrite(outfile, oneline, tempwidth, 1);
		e++;
	}

	SDL_RWclose(outfile);
	
	// Save map (grid) file
	save_grid_file(grid_file.c_str(), grid);
	
	
	Log("Scenario saved.\n");

	return true;
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
