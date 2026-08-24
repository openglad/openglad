/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/interface/ui/input_cycler.h>

#include <openglad/gameplay/input_state.h>
#include <openglad/interface/device_seats.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_mappings.h>

#include <format>

namespace og::ui {

std::vector<InputCycleOption> input_cycle_options(cfg_store& cfg,
                                                  int seat,
                                                  int active_player_count)
{
    std::vector<InputCycleOption> options;
    for (const std::string& name :
         og::input::available_mapping_names(cfg, seat, active_player_count))
    {
        options.push_back({.name = name});
    }

    const int device_count = joystick_device_count();
    for (int device = 0; device < device_count; ++device)
    {
        bool held_elsewhere = false;
        for (int other = 0; other < active_player_count; ++other)
        {
            if (other != seat && player_joystick_device(other) == device)
            {
                held_elsewhere = true;
                break;
            }
        }
        if (held_elsewhere)
            continue;
        options.push_back({.name = og::input::joystick_mapping_name(device),
                           .is_joystick = true,
                           .joystick_device = device});
    }
    return options;
}

InputCycleOption current_input_selection(int seat)
{
    const int device = player_joystick_device(seat);
    if (device >= 0)
    {
        return {.name = og::input::joystick_mapping_name(device),
                .is_joystick = true,
                .joystick_device = device};
    }
    return {.name = og::input::current_mapping_name(seat)};
}

bool apply_input_cycle_selection(cfg_store& cfg,
                                 int seat,
                                 const InputCycleOption& option)
{
    if (option.is_joystick)
        return assign_joystick_to_player(seat, option.joystick_device);

    if (player_joystick_device(seat) >= 0)
        clear_player_joystick(seat);
    const og::input::MappingDefinition mapping =
        og::input::resolve_mapping(cfg, option.name);
    return og::input::assign_mapping_to_player(seat, mapping);
}

std::string cycle_player_input(cfg_store& cfg, int seat, int active_player_count,
                               std::string* out_unavailable)
{
    if (out_unavailable != nullptr)
        out_unavailable->clear();
    const std::vector<InputCycleOption> options =
        input_cycle_options(cfg, seat, active_player_count);
    const InputCycleOption current = current_input_selection(seat);
    if (options.empty())
        return current.name;

    // The current selection may be absent from the option list (an unsaved
    // custom name is appended by available_mapping_names, but a joystick that
    // was just unplugged is not) — treat "not found" as position -1 so the
    // cycle lands on the first option.
    std::size_t current_index = options.size();
    for (std::size_t i = 0; i < options.size(); ++i)
    {
        if (options[i].name == current.name)
        {
            current_index = i;
            break;
        }
    }
    const std::size_t next_index =
        current_index >= options.size() ? 0u
                                        : (current_index + 1) % options.size();
    // Walk forward from the next option and take the first one that applies.
    // An enumerated-but-unopenable device fails in
    // assign_joystick_to_player; parking the cycle on it would make every
    // option behind it permanently unreachable, so it is skipped and named.
    for (std::size_t step = 0; step < options.size(); ++step)
    {
        const InputCycleOption& option =
            options[(next_index + step) % options.size()];
        if (option.name == current.name)
            break; // wrapped back to the seat's own selection: no change
        if (apply_input_cycle_selection(cfg, seat, option))
            return option.name;
        if (out_unavailable != nullptr && out_unavailable->empty() &&
            option.is_joystick)
            *out_unavailable = option.name;
    }
    return current.name;
}

bool ensure_unique_seat_mapping(cfg_store& cfg, int seat, int active_player_count)
{
    const InputCycleOption current = current_input_selection(seat);
    if (current.is_joystick)
        return false; // devices are exclusively held; nothing to collide with

    bool collides = false;
    for (int other = 0; other < active_player_count; ++other)
    {
        if (other == seat)
            continue;
        if (current_input_selection(other).name == current.name)
        {
            collides = true;
            break;
        }
    }
    if (!collides)
        return false;

    for (const InputCycleOption& option :
         input_cycle_options(cfg, seat, active_player_count))
    {
        // The seat's own (colliding) name is always offered as the cycle
        // anchor; a joystick must never be grabbed uninvited.
        if (option.is_joystick || option.name == current.name)
            continue;
        return apply_input_cycle_selection(cfg, seat, option);
    }
    return false;
}

bool claim_free_joystick_for_seat(cfg_store& cfg, int seat,
                                  int active_player_count)
{
    // The uninvited-grab rule (cycle_player_input / ensure_unique_seat_mapping
    // skip joysticks) protects seats that have a keyboard to fall back on. A
    // single-seat device has none: a seat added there exists only because a
    // pad opened the cap, so the add path claims the first free one for it.
    if (current_input_selection(seat).is_joystick)
        return false;
    for (const InputCycleOption& option :
         input_cycle_options(cfg, seat, active_player_count))
    {
        if (!option.is_joystick)
            continue;
        if (apply_input_cycle_selection(cfg, seat, option))
            return true;
    }
    return false;
}

std::string input_cycle_button_label(int seat)
{
    const InputCycleOption selection = current_input_selection(seat);
    // Same owner-token rule as the seat card (base_camp_seat_label): on a
    // single-seat device the touchscreen is the controller, so a keyboard
    // mapping's keys would name hardware the device lacks; a seat cycled
    // onto a real pad still names that pad.
    const std::string owner = og::input::seat_owner_is_screen(
                                  og::input::is_single_seat_device(),
                                  selection.is_joystick, seat)
        ? std::string(og::input::kScreenSeatOwnerLabel)
        : og::input::mapping_short_name(selection.name);
    return std::format("INPUT: {}", owner);
}

} // namespace og::ui
