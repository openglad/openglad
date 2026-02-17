/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Abstract audio playback interface.
// SDL builds create an SdlAudio (wrapping soundob);
// headless builds leave the audio pointer as nullptr.
class IAudio {
public:
    virtual ~IAudio() = default;
    virtual void play_sound(short which) = 0;
    virtual void set_volume(int vol) = 0;

protected:
    IAudio() = default;
};
