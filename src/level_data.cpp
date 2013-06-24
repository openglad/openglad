#include "level_data.h"
#include "yam.h"



int toInt(const std::string& s);

#define VERSION_NUM (char) 8 // save scenario type info




CampaignData::CampaignData(const std::string& id)
    : id(id), title("New Campaign"), rating(0.0f), version("1.0"), description("No description."), suggested_power(0), first_level(1), num_levels(0), icondata(NULL), icon(NULL)
{}

CampaignData::~CampaignData()
{
	delete icon;
	free(icondata);
}


// Must unmount old campaign before doing this!
bool CampaignData::load()
{
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
                        description = yam.event.value;
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
        if(icondata != NULL)
            icon = new pixie(icondata+3, icondata[1], icondata[2], myscreen);
        
        // Count the number of levels
        std::list<std::string> levels = list_levels();
        num_levels = levels.size();
        
        unmount_campaign_package(id);
    }
    
    return true;
}





LevelData::LevelData(int id)
    : id(id), title("New Level"), type(0), par_value(1), grid(NULL), maxx(0), maxy(0), pixmaxx(0), pixmaxy(0)
    , myloader(NULL), numobs(0), oblist(NULL), fxlist(NULL), weaplist(NULL)
{}

LevelData::~LevelData()
{
    delete_objects();
    delete_grid();
    delete myloader;
}

void LevelData::clear()
{
    delete_objects();
    delete_grid();
    
    title = "New Level";
    type = 0;
    par_value = 1;
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

	//numobs++;
	//here->ob->ignore = 1;
	return here->ob;
}

walker* LevelData::add_weap_ob(char order, char family)
{
	oblink *here = new oblink;

	here->ob = myloader->create_walker(order, family, myscreen);
	here->next = weaplist;
	weaplist = here;

	return here->ob;
}

void LevelData::delete_grid()
{
    free(grid);
    grid = NULL;
    maxx = 0;
    maxy = 0;
    pixmaxx = 0;
    pixmaxy = 0;
}

void LevelData::delete_objects()
{
	oblink *fx = fxlist;

	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	fx = oblist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	fx = weaplist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;
    
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
	data->maxx = data->grid[1];
	data->maxy = data->grid[2];
	data->grid += 3;
	data->pixmaxx = data->maxx * GRID_SIZE;
	data->pixmaxy = data->maxy * GRID_SIZE;

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
	data->maxx = data->grid[1];
	data->maxy = data->grid[2];
	data->pixmaxx = data->maxx * GRID_SIZE;
	data->pixmaxy = data->maxy * GRID_SIZE;
	data->grid += 3;

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
	data->maxx = (unsigned char)data->grid[1];
	data->maxy = (unsigned char)data->grid[2];
	data->pixmaxx = data->maxx * GRID_SIZE;
	data->pixmaxy = data->maxy * GRID_SIZE;
	data->grid += 3;

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
	data->maxx = data->grid[1];
	data->maxy = data->grid[2];
	data->pixmaxx = data->maxx * GRID_SIZE;
	data->pixmaxy = data->maxy * GRID_SIZE;
	data->grid += 3;
	data->mysmoother.set_target(data->grid, data->maxx, data->maxy);

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
    char newgrid[12] = "grid.pix";  // default grid
    char new_scen_type; // read the scenario type
    char oneline[80];
    char numlines, tempwidth;
    char tempname[12];
    oblink *here;
    char scentitle[30];
    memset(scentitle, 0, 30);
    short temp_par;
    char my_map_name[40];

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
    SDL_RWread(infile, newgrid, 8, 1);
    // Zardus: FIX: make sure they're lowercased
    lowercase((char *)newgrid);
    strcpy(my_map_name, newgrid);

    // Get scenario title, if it exists
    //for (i=0; i < strlen(scentitle); i++)
    //	scentitle[i] = 0;
    SDL_RWread(infile, scentitle, 30, 1);

    // Get the scenario type information
    SDL_RWread(infile, &new_scen_type, 1, 1);

    if (version >= 8)
    {
        SDL_RWread(infile, &temp_par, 2, 1);
    }
    // else we're using the value of the level ..

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
        if (version >= 7)
            SDL_RWread(infile, &shortlevel, 2, 1);
        else
            SDL_RWread(infile, &templevel, 1, 1);
        SDL_RWread(infile, tempname, 12, 1);
        SDL_RWread(infile, tempreserved, 10, 1);
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
    SDL_RWread(infile, &numlines, 1, 1);
    std::list<std::string> desc_lines;
    for (i=0; i < numlines; i++)
    {
        SDL_RWread(infile, &tempwidth, 1, 1);
        if(tempwidth > 0)
        {
            SDL_RWread(infile, oneline, tempwidth, 1);
            oneline[(int)tempwidth] = 0;
        }
        else
            oneline[0] = 0;
        desc_lines.push_back(oneline);
    }

    
    // Now read the grid file to our master screen ..
    strcat(newgrid, ".pix");
    
    data->delete_grid();
    
    data->grid = read_pixie_file(newgrid);
    data->maxx = data->grid[1];
    data->maxy = data->grid[2];
    data->grid += 3;
    data->pixmaxx = data->maxx * GRID_SIZE;
    data->pixmaxy = data->maxy * GRID_SIZE;
    
    // The collected data so far
    data->title = scentitle;
    data->type = new_scen_type;
    data->par_value = temp_par;
    data->description = desc_lines;
    data->mysmoother.set_target(data->grid, data->maxx, data->maxy);

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
    short tempvalue = load_scenario_version(infile, this, versionnumber);
    SDL_RWclose(infile);
    
	return (tempvalue != 0);
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
	char temp_grid[20] = "grid";  // default grid
	char temp_scen_type = 0;
	oblink* head = NULL;
	Sint32 listsize;
	Sint32 i;
	char temp_version = VERSION_NUM;
	char temp_filename[80];
	char numlines, tempwidth;
	char oneline[80];
	char tempname[12];
	char scentitle[30];
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
		Log("Could not open file for writing: %s\n", temp_filename);
		return false;
	}

	// Write id header
	SDL_RWwrite(outfile, temptext, 3, 1);

	// Write version number
	SDL_RWwrite(outfile, &temp_version, 1, 1);

	// Write name of current grid...
	strcpy(temp_grid, this->grid_file.c_str());  // Do NOT include extension

	// Set any chars under 8 not used to 0 ..
	for (i=strlen(temp_grid); i < 8; i++)
		temp_grid[i] = 0;
	SDL_RWwrite(outfile, temp_grid, 8, 1);

	// Write the scenario title, if it exists
	for (i=0; i < int(strlen(scentitle)); i++)
		scentitle[i] = 0;
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
	}

	SDL_RWclose(outfile);
	
	Log("Scenario saved.\n");

	return true;
}
