/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <memory>

class PixieData;

// Concrete render component for walker entities.
// SDL builds create a WalkerRender (wrapping pixieN);
// headless builds leave render_ as nullptr.
class WalkerRender {
public:
    explicit WalkerRender(const PixieData& data);
    ~WalkerRender();

    WalkerRender(const WalkerRender&) = delete;
    WalkerRender& operator=(const WalkerRender&) = delete;

    const unsigned char* bmp_data() const;
    void set_frame(short framenum);
    void set_data(const PixieData& data);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Backwards-compatibility alias
using IWalkerRender = WalkerRender;
