#include "graph.h"
#include "gladpack.h"
#include <fstream.h>
#include <stdlib.h>
#include <dos.h>
#include <ctype.h>
#include <i86.h>

// Use this for globally setting the graphics dir, etc..
char pix_directory[80];
packfile *pixpack; // the packed pixies; perfect
packfile tempack;
long opened = 0;  // use with tempack

char* get_cfg_item(char *section, char *item)
{
  FILE  *cfgfile;
  char  *temp;
  char returnvalue[80];

  cfgfile = open_cfg_file("glad.cfg");
 
  if (!cfgfile)
  {
         printf("Error opening glad.cfg:\n");
         printf("Please make sure it is in the current directory.\n");
         printf("You may need to run SETUP.EXE\n");
         release_keyboard();
         fclose(cfgfile);
         //return NULL;
         exit(0);
  }
  temp = query_cfg_value(cfgfile, section, item);
  if ( temp==NULL  )
  {
         printf("The '%s' setting in the glad.cfg file is incorrect:\n",item);
         printf("It should be something like: graphics=e:\\files\\glad\\%s\\\n",item);
         fclose(cfgfile);
         return NULL;
  }
  strcpy(returnvalue, temp);
  fclose(cfgfile);

  return &returnvalue[0];
}

short get_pix_directory()
{
  char temp[80];

  if (get_pix_directory(temp))
         return 1;

  return 0;
}

short get_pix_directory(char *whereto)
{
  short i;
  FILE *cfgfile;
  char temp[80];

  // Get the graphics directory ..
  cfgfile = open_cfg_file("glad.cfg");
  if (!cfgfile)
  {
       printf("Error opening glad.cfg:\n");
       release_keyboard();
         strcpy(pix_directory, "");
         strcpy(whereto, "");
         return 1;
  }
  strcpy(temp, query_cfg_value(cfgfile, "directories", "graphics") );
  if ( (strlen(temp) < 2) )
  {
       printf("The 'graphics' setting in the glad.cfg file is incorrect;\n");
         strcpy(pix_directory, "");
         strcpy(whereto, "");
         fclose(cfgfile);
         return 1;
  }
  strcpy(pix_directory, temp);
  strcpy(whereto, temp);
  fclose(cfgfile);
  i = 1;
  return i;
}


// ************************************************************
//  Other graphics routines
// ************************************************************

//have been moved to video


// ************* Editor related functions? ************************
// have also been moved to video

unsigned char  * read_pixie_file(char  * filename)
{
  // Create a file stream, and read the image
  // File data in form:
  // <# of frames>      1 byte
  // <x size>                   1 byte
  // <y size>                   1 byte
  // <pixie data>               <x*y*frames> bytes

        unsigned char numframes, x, y;
        unsigned char  *newpic;
        char fullpath[40];
        FILE  *infile = NULL;
        long gotit = 0;

  // Open the pixie-pack, if not already done ...
  if (!opened) 
  {
    if (tempack.open("graphics.001") == -1) // not in current directory
    {
      // Create the full pathname for the resource file
      strcpy(fullpath, pix_directory);
      strcat(fullpath, "graphics.001");
      if (tempack.open(fullpath) == -1) // not in graphics directory
      {
        printf("Cannot open graphics resource file!\n");
        release_keyboard();
        exit(0);
      }
    }
    opened = 1;
  }

  pixpack = NULL;
  gotit = 0;

    // Create the full pathname for the pixie file
    strcpy(fullpath, pix_directory);
    strcat(fullpath, filename);

    if ( (infile = fopen(fullpath, "rb")) == NULL ) // open for read
    {
      // Check the current directory if not in path ..
      if ( (infile = fopen(filename, "rb")) == NULL )       // open for read
      {
        gotit = 0; // couldn't get it from a normal file ..
      }
      else
        gotit = 1; // got the actual file from disk
    }
    else
    {
      gotit = 1; // got the actual file from disk
    }

  // Now try to get info from the pack-file ..
  if (opened && gotit==0)
  {
    infile = tempack.get_subfile(filename);
    gotit = 1;
  }
  if (!infile || gotit==0) // failed to get this way, either
  {
        printf("Error in reading file %s <press return>\n", filename);
        printf("You may need to run SETUP.EXE\n");
        release_keyboard();
        getch();
        exit(0);
  }

    fread(&numframes, 1, 1, infile);
    fread(&x, 1, 1, infile);
    fread(&y, 1, 1, infile);

    newpic = new unsigned char[x * y * numframes + 3];
    newpic[0] = numframes;
    newpic[1] = x;
    newpic[2] = y;

    // Now read the data in a big chunk
    fread(&newpic[3], 1, (x*y*numframes), infile);

    if (!gotit) // this means we're a 'manual' file
      fclose(infile); // Close the data file
    //tempack.close();
    return newpic;

} // End of image-reading routine



void load_map_data(unsigned char  * *whereto)
{
  // load the pixie graphics data shorto memory
  whereto[0] = read_pixie_file("16tile.pix");             //done
  whereto[PIX_GRASS1] = read_pixie_file("16grass1.pix");  //done
  whereto[PIX_WATER1] = read_pixie_file("16water1.pix");  //done
  whereto[3] = read_pixie_file("16space.pix");            //done
  whereto[4] = read_pixie_file("16wall2.pix");            //done
  whereto[5] = read_pixie_file("16wall3.pix");            //done
  whereto[6] = read_pixie_file("16floor.pix");            //done
  whereto[7] = read_pixie_file("16walllo.pix");           //done
  whereto[8] = read_pixie_file("16w2lo.pix");            //done wall2lo
 // return; // works up to here ..
 
  whereto[9] = read_pixie_file("16carpll.pix");          //done thru
  whereto[11] = read_pixie_file("16carpb.pix");
  whereto[12] = read_pixie_file("16carplr.pix");
  whereto[13] = read_pixie_file("16carpur.pix");
  whereto[14] = read_pixie_file("16carpu.pix");
  whereto[15] = read_pixie_file("16carpul.pix");
  whereto[PIX_CARPET_L] = read_pixie_file("16carpl.pix");
  whereto[PIX_CARPET_M] = read_pixie_file("16carpm.pix");
  whereto[PIX_CARPET_M2] = read_pixie_file("16carpm2.pix");
  whereto[PIX_CARPET_R] = read_pixie_file("16carpr.pix");    // here

  whereto[PIX_CARPET_SMALL_HOR] = read_pixie_file("16cshor.pix");    // here
  whereto[PIX_CARPET_SMALL_VER] = read_pixie_file("16csver.pix");    // here
  whereto[PIX_CARPET_SMALL_CUP] = read_pixie_file("16cscup.pix");    // here
  whereto[PIX_CARPET_SMALL_CAP] = read_pixie_file("16cscap.pix");    // here
  whereto[PIX_CARPET_SMALL_LEFT] = read_pixie_file("16csleft.pix");    // here
  whereto[PIX_CARPET_SMALL_RIGHT] = read_pixie_file("16csrigh.pix");    // here
  whereto[PIX_CARPET_SMALL_TINY] = read_pixie_file("16cstiny.pix");    // here

  whereto[PIX_GRASS2] = read_pixie_file("16grass2.pix");  //done
  whereto[PIX_GRASS3] = read_pixie_file("16grass3.pix");  //done
  whereto[PIX_GRASS4] = read_pixie_file("16grass4.pix");  //done

  whereto[PIX_GRASS_DARK_1] = read_pixie_file("16grassD.pix");  //done
  whereto[PIX_GRASS_DARK_2] = read_pixie_file("16grD2.pix");  //done
  whereto[PIX_GRASS_DARK_3] = read_pixie_file("16grD3.pix");  //done
  whereto[PIX_GRASS_DARK_4] = read_pixie_file("16grD4.pix");  //done
  whereto[PIX_GRASS_DARK_LL] = read_pixie_file("16grassI.pix");  //done
  whereto[PIX_GRASS_DARK_UR] = read_pixie_file("16grassH.pix");  //done
  whereto[PIX_GRASS_RUBBLE] = read_pixie_file("16grassR.pix");  //done

  whereto[PIX_GRASS_DARK_B1] = read_pixie_file("16grdb1.pix");  //done
  whereto[PIX_GRASS_DARK_B2] = read_pixie_file("16grdb2.pix");  //done
  whereto[PIX_GRASS_DARK_R1] = read_pixie_file("16grdr1.pix");  //done
  whereto[PIX_GRASS_DARK_R2] = read_pixie_file("16grdr2.pix");  //done
  whereto[PIX_GRASS_DARK_BR] = read_pixie_file("16grdbr.pix");  //done
  
  whereto[PIX_GRASS_LIGHT_1] = read_pixie_file("16grl1.pix");  //done
  whereto[PIX_GRASS_LIGHT_TOP] = read_pixie_file("16grlt.pix");  //done
  whereto[PIX_GRASS_LIGHT_RIGHT_TOP] = read_pixie_file("16grlrt.pix");  //done
  whereto[PIX_GRASS_LIGHT_RIGHT] = read_pixie_file("16grlr.pix");  //done
  whereto[PIX_GRASS_LIGHT_RIGHT_BOTTOM] = read_pixie_file("16grlrb.pix");  //done
  whereto[PIX_GRASS_LIGHT_BOTTOM] = read_pixie_file("16grlb.pix");  //done
  whereto[PIX_GRASS_LIGHT_LEFT_BOTTOM] = read_pixie_file("16grllb.pix");  //done
  whereto[PIX_GRASS_LIGHT_LEFT] = read_pixie_file("16grll.pix");  //done
  whereto[PIX_GRASS_LIGHT_LEFT_TOP] = read_pixie_file("16grllt.pix");  //done

  whereto[PIX_WATER2] = read_pixie_file("16water2.pix");  //done
  whereto[PIX_WATER3] = read_pixie_file("16water3.pix");  //done

  whereto[PIX_WATERGRASS_LL] = read_pixie_file("16wgll.pix"); //done thru
  whereto[PIX_WATERGRASS_LR] = read_pixie_file("16wglr.pix");
  whereto[PIX_WATERGRASS_UL] = read_pixie_file("16wgul.pix");
  whereto[PIX_WATERGRASS_UR] = read_pixie_file("16wgur.pix");
  whereto[PIX_WATERGRASS_U] = read_pixie_file("16wgu.pix");
  whereto[PIX_WATERGRASS_D] = read_pixie_file("16wgd.pix");
  whereto[PIX_WATERGRASS_L] = read_pixie_file("16wgl.pix");
  whereto[PIX_WATERGRASS_R] = read_pixie_file("16wgr.pix");
  whereto[PIX_GRASSWATER_LL] = read_pixie_file("16gwll.pix");
  whereto[PIX_GRASSWATER_LR] = read_pixie_file("16gwlr.pix");
  whereto[PIX_GRASSWATER_UL] = read_pixie_file("16gwul.pix");
  whereto[PIX_GRASSWATER_UR] = read_pixie_file("16gwur.pix"); // here ..done

  whereto[PIX_PAVEMENT1] = read_pixie_file("16pave1.pix"); // pavement done
  whereto[PIX_PAVEMENT2] = read_pixie_file("16pave2.pix"); //done
  whereto[PIX_PAVEMENT3] = read_pixie_file("16pave3.pix"); //done
  whereto[PIX_PAVESTEPS1] = read_pixie_file("16pstep.pix");   // pavestep done
  whereto[PIX_PAVESTEPS2] = read_pixie_file("16ptest.pix");   //done
  whereto[PIX_PAVESTEPS2L] = read_pixie_file("16ptestl.pix");
  whereto[PIX_PAVESTEPS2R] = read_pixie_file("16ptestr.pix"); //done

  whereto[PIX_WALLSIDE1] = read_pixie_file("16brick1.pix");  // 'ELL' //done
  whereto[PIX_WALLSIDE_L] = read_pixie_file("16brickl.pix"); // 'ONE' //done
  whereto[PIX_WALLSIDE_R] = read_pixie_file("16brickr.pix"); //done
  whereto[PIX_WALLSIDE_C] = read_pixie_file("16brickc.pix"); //done
  whereto[PIX_WALLSIDE_CRACK_C1] = read_pixie_file("16brick3.pix"); //done

  whereto[PIX_WALL_LL] = read_pixie_file("16wallll.pix");  //done

  whereto[PIX_BRAZIER1] = read_pixie_file("16braz1.pix"); //brazier1

  whereto[PIX_WALLTOP_H] = read_pixie_file("16ttop.pix"); //tiletop

  whereto[PIX_TORCH1] = read_pixie_file("16torch1.pix");  //done
  whereto[PIX_TORCH2] = read_pixie_file("16torch2.pix");  //done
  whereto[PIX_TORCH3] = read_pixie_file("16torch3.pix");  //done

  whereto[PIX_FLOOR_PAVEL] = read_pixie_file("16fpl.pix"); //done flrpavel
  whereto[PIX_FLOOR_PAVER] = read_pixie_file("16fpr.pix"); //done flrpaver
  whereto[PIX_FLOOR_PAVEU] = read_pixie_file("16fpu.pix"); //done flrpaveu
  whereto[PIX_FLOOR_PAVED] = read_pixie_file("16fpd.pix"); //done flrpaved
//return; // this is probably buggged if we load more ..

  whereto[PIX_COLUMN1] = read_pixie_file("16colm0.pix"); //done column0
  whereto[PIX_COLUMN2] = read_pixie_file("16colm1.pix"); //done column1

  // Tree stuff ..
  whereto[PIX_TREE_B1] = read_pixie_file("16treeb1.pix"); //done tree_b1

  whereto[PIX_TREE_M1] = read_pixie_file("16treem1.pix"); //done tree_m1
  whereto[PIX_TREE_ML] = read_pixie_file("16treemL.pix"); //done tree_mL
  whereto[PIX_TREE_MR] = read_pixie_file("16treemR.pix"); //done tree_mR
  whereto[PIX_TREE_MT] = read_pixie_file("16treemT.pix"); //done tree_mT (thin)

  whereto[PIX_TREE_T1] = read_pixie_file("16treet1.pix"); //done tree_t2

  whereto[PIX_DIRT_1] = read_pixie_file("16dirt2.pix");
  whereto[PIX_DIRTGRASS_UL1] = read_pixie_file("16dgul1.pix");
  whereto[PIX_DIRTGRASS_UR1] = read_pixie_file("16dgur1.pix");
  whereto[PIX_DIRTGRASS_LL1] = read_pixie_file("16dgll1.pix");
  whereto[PIX_DIRTGRASS_LR1] = read_pixie_file("16dglr1.pix");

  whereto[PIX_DIRT_DARK_1] = read_pixie_file("16dirtd1.pix");
  whereto[PIX_DIRTGRASS_DARK_UL1] = read_pixie_file("16dguld.pix");
  whereto[PIX_DIRTGRASS_DARK_UR1] = read_pixie_file("16dgurd.pix");
  whereto[PIX_DIRTGRASS_DARK_LL1] = read_pixie_file("16dglld.pix");
  whereto[PIX_DIRTGRASS_DARK_LR1] = read_pixie_file("16dglrd.pix");

  whereto[PIX_PATH_1] = read_pixie_file("16path1.pix");
  whereto[PIX_PATH_2] = read_pixie_file("16path2.pix");
  whereto[PIX_PATH_3] = read_pixie_file("16path3.pix");
  whereto[PIX_PATH_4] = read_pixie_file("16path4.pix");

  whereto[PIX_BOULDER_1] = read_pixie_file("16stone1.pix");
  whereto[PIX_BOULDER_2] = read_pixie_file("16stone2.pix");
  whereto[PIX_BOULDER_3] = read_pixie_file("16stone3.pix");
  whereto[PIX_BOULDER_4] = read_pixie_file("16stone4.pix");

  whereto[PIX_COBBLE_1] = read_pixie_file("16cob1.pix");
  whereto[PIX_COBBLE_2] = read_pixie_file("16cob2.pix");
  whereto[PIX_COBBLE_3] = read_pixie_file("16cob3.pix");
  whereto[PIX_COBBLE_4] = read_pixie_file("16cob4.pix");

  whereto[PIX_WALL_ARROW_GRASS] = read_pixie_file("16wallog.pix");
  whereto[PIX_WALL_ARROW_FLOOR] = read_pixie_file("16wallof.pix");
  whereto[PIX_WALL_ARROW_GRASS_DARK] = read_pixie_file("16wallod.pix");

  // Cliff tiles
  whereto[PIX_CLIFF_BOTTOM] = read_pixie_file("16cliff1.pix");
  whereto[PIX_CLIFF_TOP] = read_pixie_file("16cliff2.pix");
  whereto[PIX_CLIFF_LEFT] = read_pixie_file("16cliff3.pix");
  whereto[PIX_CLIFF_RIGHT] = read_pixie_file("16cliff4.pix");
  whereto[PIX_CLIFF_BACK_1] = read_pixie_file("16clifup.pix");
  whereto[PIX_CLIFF_BACK_2] = read_pixie_file("16clifu2.pix");
  whereto[PIX_CLIFF_BACK_L] = read_pixie_file("16cliful.pix");
  whereto[PIX_CLIFF_BACK_R] = read_pixie_file("16clifur.pix");
  whereto[PIX_CLIFF_TOP_L] = read_pixie_file("16clifdl.pix");
  whereto[PIX_CLIFF_TOP_R] = read_pixie_file("16clifdr.pix");

  // Damaged tiles ..
  whereto[PIX_GRASS1_DAMAGED] = read_pixie_file("16grasd1.pix");

  // Pete's graphics
  whereto[PIX_JAGGED_GROUND_1] = read_pixie_file("16jwg1.pix");
  whereto[PIX_JAGGED_GROUND_2] = read_pixie_file("16jwg2.pix");
  whereto[PIX_JAGGED_GROUND_3] = read_pixie_file("16jwg3.pix");
  whereto[PIX_JAGGED_GROUND_4] = read_pixie_file("16jwg1.pix");

}

#if (0)
// Testing for the sound modules
packfile soundpack;
long sound_opened = 0;  // use with soundpack

FILE * open_sound_file(char *filename)
{
  FILE *infile = NULL;

  // Open the pixie-pack, if not already done ...
  if (!sound_opened) 
  {
    if (soundpack.open("sound.001") == -1) // not in current directory
    {
      printf("Cannot open sound resource file!\n");
      release_keyboard();
      exit(0);
    }
    sound_opened = 1;
  }

  // First try to get info from the pack-file ..
  if (sound_opened)
    infile = soundpack.get_subfile(filename);

  if (infile)
    return infile;

  // Else, we return NULL
  //infile = fopen(filename, "rb");
  //return infile;
  return NULL;
}
#endif
