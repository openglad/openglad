/* gladpack.h
 *
 * Header for gladpack.cpp
 *
 * Created 8/7/95 Doug Ricket
 *
*/

#ifndef GLADPACK_H
#define GLADPACK_H

#include <stdio.h>

#define GLAD_HEADER             "GladPack"
#define GLAD_HEADER_SIZE        8
#define FILENAME_SIZE           13

class packfileinfo;     /* Used internally by packfile */

class packfile
{
    private:

    FILE *datafile;

    short numfiles;
    short last_subfile;
    packfileinfo *fileinfo;

    public:

    packfile() { numfiles = 0; }
    ~packfile() { close(); }

    int open(char *filename);
    int close();

    FILE *get_subfile(char *subfilename);
    long get_subfilesize();
    
};

#endif
