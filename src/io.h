#ifndef __IO_H__
#define __IO_H__

#include "SDL.h"
#include <list>
#include <string>

void io_init(int argc, char* argv[]);
void io_exit();

SDL_RWops* open_read_file(const char* file);
SDL_RWops* open_read_file(const char* path, const char* file);
SDL_RWops* open_write_file(const char* file);
SDL_RWops* open_write_file(const char* path, const char* file);

std::list<std::string> list_files(const std::string& dirname);


int rwops_read_handler(void *data, unsigned char *buffer, size_t size, size_t *size_read);

#endif
