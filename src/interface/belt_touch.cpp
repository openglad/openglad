/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/interface/belt_touch.h>

#include <utility>

namespace og::ui {

namespace {

BeltTouchObserver& observer()
{
    static BeltTouchObserver registered;
    return registered;
}

} // namespace

void set_belt_touch_observer(BeltTouchObserver next)
{
    observer() = std::move(next);
}

bool belt_touch_observed()
{
    return static_cast<bool>(observer());
}

void publish_belt_touch(const BeltTouchState& state)
{
    if (const BeltTouchObserver& call = observer())
        call(state);
}

} // namespace og::ui
