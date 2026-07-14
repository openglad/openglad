#include <SDL3/SDL.h>
#include "physfs.h"

#include <cstddef>

namespace {

PHYSFS_File* physfs_file(SDL_IOStream* rw)
{
    return static_cast<PHYSFS_File*>(rw->hidden.unknown.data1);
}

Sint64 SDLCALL physfsrw_size(SDL_IOStream* rw)
{
    PHYSFS_File* handle = physfs_file(rw);
    return static_cast<Sint64>(PHYSFS_fileLength(handle));
}

Sint64 SDLCALL physfsrw_seek(SDL_IOStream* rw, Sint64 offset, int whence)
{
    PHYSFS_File* handle = physfs_file(rw);
    PHYSFS_sint64 pos = 0;

    if (whence == SDL_IO_SEEK_SET)
    {
        pos = static_cast<PHYSFS_sint64>(offset);
    }
    else if (whence == SDL_IO_SEEK_CUR)
    {
        const PHYSFS_sint64 current = PHYSFS_tell(handle);
        if (current == -1)
        {
            SDL_SetError("Can't find position in file: %s",
                         PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            return -1;
        }
        pos = current + static_cast<PHYSFS_sint64>(offset);
    }
    else if (whence == SDL_IO_SEEK_END)
    {
        const PHYSFS_sint64 len = PHYSFS_fileLength(handle);
        if (len == -1)
        {
            SDL_SetError("Can't find end of file: %s",
                         PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            return -1;
        }
        pos = len + static_cast<PHYSFS_sint64>(offset);
    }
    else
    {
        SDL_SetError("Invalid 'whence' parameter.");
        return -1;
    }

    if (pos < 0)
    {
        SDL_SetError("Attempt to seek past start of file.");
        return -1;
    }

    if (PHYSFS_seek(handle, static_cast<PHYSFS_uint64>(pos)) == 0)
    {
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return -1;
    }

    return static_cast<Sint64>(pos);
}

std::size_t SDLCALL physfsrw_read(SDL_IOStream* rw, void* ptr, std::size_t size, std::size_t maxnum)
{
    if (size == 0 || maxnum == 0)
        return 0;

    PHYSFS_File* handle = physfs_file(rw);
    const PHYSFS_uint64 readlen = static_cast<PHYSFS_uint64>(maxnum * size);
    const PHYSFS_sint64 rc = PHYSFS_readBytes(handle, ptr, readlen);
    if (rc < 0)
    {
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return 0;
    }
    return static_cast<std::size_t>(rc) / size;
}

std::size_t SDLCALL physfsrw_write(SDL_IOStream* rw, const void* ptr, std::size_t size, std::size_t num)
{
    if (size == 0 || num == 0)
        return 0;

    PHYSFS_File* handle = physfs_file(rw);
    const PHYSFS_uint64 writelen = static_cast<PHYSFS_uint64>(num * size);
    const PHYSFS_sint64 rc = PHYSFS_writeBytes(handle, ptr, writelen);
    if (rc < 0)
    {
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return 0;
    }
    return static_cast<std::size_t>(rc) / size;
}

int SDLCALL physfsrw_close(SDL_IOStream* rw)
{
    PHYSFS_File* handle = physfs_file(rw);
    if (PHYSFS_close(handle) == 0)
    {
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return -1;
    }

    SDL_FreeRW(rw);
    return 0;
}

SDL_IOStream* make_rwops(PHYSFS_File* handle)
{
    if (handle == nullptr)
    {
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return nullptr;
    }

    SDL_IOStream* rw = SDL_AllocRW();
    if (rw == nullptr)
    {
        (void)PHYSFS_close(handle);
        return nullptr;
    }

    rw->size = physfsrw_size;
    rw->seek = physfsrw_seek;
    rw->read = physfsrw_read;
    rw->write = physfsrw_write;
    rw->close = physfsrw_close;
    rw->hidden.unknown.data1 = handle;
    return rw;
}

} // namespace

namespace og::io {

SDL_IOStream* physfsrw_open_read(const char* path)
{
    return make_rwops(PHYSFS_openRead(path));
}

SDL_IOStream* physfsrw_open_write(const char* path)
{
    return make_rwops(PHYSFS_openWrite(path));
}

} // namespace og::io
