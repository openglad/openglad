//
// parsecfg.h
//

//#include <stdio.h>
//#include <string.h>
//#include <cstring.h>
//includes moved to parsecfg.cpp
#include <stdio.h> //needed here for a file* function

FILE* open_cfg_file(char *filename);   // returns a poshorter to open                                                                                                                          // .cfg file
void close_cfg_file(FILE *cfgfile);     // close a .cfg file
void dump_cfg_file(FILE *cfgfile);
char* read_one_line(FILE *cfgfile);    // read until \n
char* query_cfg_value(FILE *cfgfile, char *section, char *pattern);
