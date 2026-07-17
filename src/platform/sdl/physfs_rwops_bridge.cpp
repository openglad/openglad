// PhysFS <-> SDL_IOStream bridge: exposes PHYSFS_File handles as SDL
// IO streams via SDL_OpenIO + SDL_IOStreamInterface. The PHYSFS_File* is
// the stream userdata; SDL_CloseIO destroys the SDL_IOStream itself, so the
// close callback only closes the PhysFS handle.

#include <SDL3/SDL.h>
#include "physfs.h"

#include <cstddef>

namespace {

PHYSFS_File* physfs_file(void* userdata)
{
    return static_cast<PHYSFS_File*>(userdata);
}

Sint64 SDLCALL physfsio_size(void* userdata)
{
    PHYSFS_File* handle = physfs_file(userdata);
    return static_cast<Sint64>(PHYSFS_fileLength(handle));
}

Sint64 SDLCALL physfsio_seek(void* userdata, Sint64 offset, SDL_IOWhence whence)
{
    PHYSFS_File* handle = physfs_file(userdata);
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
        // SDL_SeekIO forwards `whence` unchecked; keep rejecting bogus values.
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

std::size_t SDLCALL physfsio_read(void* userdata, void* ptr, std::size_t size, SDL_IOStatus* status)
{
    // SDL_ReadIO short-circuits size == 0 before the callback: size > 0 here.
    PHYSFS_File* handle = physfs_file(userdata);
    const PHYSFS_sint64 rc = PHYSFS_readBytes(handle, ptr, static_cast<PHYSFS_uint64>(size));
    if (rc < 0)
    {
        *status = SDL_IO_STATUS_ERROR;
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return 0;
    }

    const std::size_t bytes = static_cast<std::size_t>(rc);
    if (bytes < size)
    {
        if (PHYSFS_eof(handle))
        {
            *status = SDL_IO_STATUS_EOF;
        }
        else
        {
            *status = SDL_IO_STATUS_ERROR;
            SDL_SetError("PhysicsFS error: %s",
                         PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        }
    }
    return bytes;
}

std::size_t SDLCALL physfsio_write(void* userdata, const void* ptr, std::size_t size, SDL_IOStatus* status)
{
    // SDL_WriteIO short-circuits size == 0 before the callback: size > 0 here.
    PHYSFS_File* handle = physfs_file(userdata);
    const PHYSFS_sint64 rc = PHYSFS_writeBytes(handle, ptr, static_cast<PHYSFS_uint64>(size));
    if (rc < 0)
    {
        *status = SDL_IO_STATUS_ERROR;
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return 0;
    }

    const std::size_t bytes = static_cast<std::size_t>(rc);
    if (bytes < size)
    {
        *status = SDL_IO_STATUS_ERROR;
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    }
    return bytes;
}

bool SDLCALL physfsio_close(void* userdata)
{
    // SDL_CloseIO destroys the SDL_IOStream itself even if this fails —
    // never free the stream here.
    PHYSFS_File* handle = physfs_file(userdata);
    if (PHYSFS_close(handle) == 0)
    {
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }
    return true;
}

SDL_IOStream* make_iostream(PHYSFS_File* handle)
{
    if (handle == nullptr)
    {
        SDL_SetError("PhysicsFS error: %s",
                     PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return nullptr;
    }

    SDL_IOStreamInterface iface;
    SDL_INIT_INTERFACE(&iface);
    iface.size = physfsio_size;
    iface.seek = physfsio_seek;
    iface.read = physfsio_read;
    iface.write = physfsio_write;
    iface.flush = nullptr; // NULL flush is a no-op (SDL2 bridge had none)
    iface.close = physfsio_close;

    SDL_IOStream* io = SDL_OpenIO(&iface, handle); // iface is copied — stack ok
    if (io == nullptr)
    {
        (void)PHYSFS_close(handle);
        return nullptr;
    }
    return io;
}

} // namespace

namespace og::io {

SDL_IOStream* physfsio_open_read(const char* path)
{
    return make_iostream(PHYSFS_openRead(path));
}

SDL_IOStream* physfsio_open_write(const char* path)
{
    return make_iostream(PHYSFS_openWrite(path));
}

} // namespace og::io
