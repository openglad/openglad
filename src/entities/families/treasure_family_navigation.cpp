/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/entities/treasure_family_descriptor.h>
#include <openglad/entities/treasure.h>
#include <openglad/core/stats.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/entities/obmap.h>
#ifndef OPENGLAD_HEADLESS
#include <openglad/input/input.h>
#endif
#include <openglad/sim/sim_emit.h>
#include <format>
#include <string>

std::string get_scenario_title(const char* filename);

#ifndef OPENGLAD_HEADLESS
void get_input_events(bool);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
#else
static inline bool yes_or_no_prompt(const char*, const char*, bool default_value) { return default_value; }
static inline void clear_keyboard() {}
#endif

static bool exit_on_eat(treasure* self, walker* eater)
{
    if (eater->in_act) return true;
    if (eater->query_act_type() != ACT_CONTROL || (eater->skip_exit > 1))
        return true;
    eater->skip_exit = 10;
    // See if there are any enemies left ...
    short guys_here;
    if (self->sim_level->level_done == 0)
        guys_here = 1;
    else
        guys_here = 0;
    // Get the name of our exit..
    std::string message = std::format("scen{}", self->stats()->level);
    std::string exitname = get_scenario_title(message.c_str());

    if (exitname == "none")
    {
        exitname = std::format("Level {}", self->stats()->level);
    }

    std::int32_t leftside  = 160 - ( (static_cast<int>(exitname.size()) + 18) * 3);
    std::int32_t rightside = 160 + ( (static_cast<int>(exitname.size()) + 18) * 3);
    // First check to see if we're withdrawing into
    //    somewhere we've been, in which case we abort
    //    this level, and set our current level to
    //    that pointed to by the exit ...
    if ( self->sim_save->is_level_completed(self->stats()->level)
            && !self->sim_save->is_level_completed(self->sim_save->scen_num)
            && (guys_here != 0)
       ) // okay to leave
    {
        leftside -= 12;
        rightside += 12;

        std::string buf = std::format("Withdraw to {}?", exitname);
        bool result = yes_or_no_prompt("Exit Field", buf.c_str(), false);
        // Redraw screen ..
        og::sim::emit_event(self->sim_events, og::sim::EventKind::RequestRedraw);

        if (result) // accepted level change
        {
            clear_keyboard();
            // Delete all of our current information and abort ..
            for(auto& uptr : self->sim_level->oblist)
            {
                walker* w = uptr.get();
                if (w && w->query_order() == Order::Living)
                {
                    w->dead = 1;
                    self->sim_level->myobmap->remove(w);
                }
            }

            // Now reload the autosave to revert our changes during battle (don't use SaveData::update_guys())
            self->sim_save->load("save0");

            // Go to the exit's level
            self->sim_save->scen_num = static_cast<short>(self->stats()->level);

            // Autosave because we escaped to a new level
            // Save with the new current level
            self->sim_save->save("save0");

            // Signal end and emit endgame event for retreat
            og::sim::emit_event(self->sim_events, og::sim::EventKind::SetEnd);
            og::sim::emit_event(self->sim_events, og::sim::EventKind::EndGame,
                                1, static_cast<std::uint32_t>(self->stats()->level));
            return true;
        }  // end of accepted withdraw to new level ..
        clear_keyboard();
    } // end of checking for withdrawal to completed level

    //buffers: also, allow exit if scenario_type == can exit
    if (!guys_here || (self->sim_level->type == SCEN_TYPE_CAN_EXIT)) // nobody evil left, so okay to exit level ..
    {
        std::string buf = std::format("Exit to {}?", exitname);
        bool result = yes_or_no_prompt("Exit Field", buf.c_str(), false);
        // Redraw screen ..
        og::sim::emit_event(self->sim_events, og::sim::EventKind::RequestRedraw);

        if(result) // accepted level change
        {
            clear_keyboard();
            og::sim::emit_event(self->sim_events, og::sim::EventKind::EndGame,
                                0, static_cast<std::uint32_t>(self->stats()->level));
            return true;
        }
        clear_keyboard();
        return true;
    }
    return true;
}

static bool teleporter_on_eat(treasure* self, walker* eater)
{
    if (eater->skip_exit > 1)
        return true;
    std::int32_t distance = self->distance_to_ob_center(eater); // how far away?
    if (distance > 21)
        return true;
    if (distance < 4 && eater->skip_exit)
    {
        eater->skip_exit = 8;
        return true;
    }
    // If we're close enough, teleport ..
    eater->skip_exit = eater->skip_exit + 20;
    walker* target;
    if (!self->leader)
        target = self->find_teleport_target();
    else
        target = self->leader;
    if (!target)
        return true;
    self->leader = target;
    eater->center_on(target);
    if (!self->sim_level->query_passable(eater->xpos, eater->ypos, eater))
    {
        eater->center_on(self);
        return true;
    }
    // Now do special effects
    walker* flash = self->sim_level->add_ob(Order::FX, FAMILY_FLASH);
    flash->ani_type = ANI_EXPAND_8;
    flash->center_on(self);
    return true;
}

const TreasureFamilyDescriptor& describe_treasure_exit()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_EXIT,
        .name = "EXIT",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = exit_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_teleporter()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_TELEPORTER,
        .name = "TELEPORTER",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = teleporter_on_eat,
    };
    return desc;
}
