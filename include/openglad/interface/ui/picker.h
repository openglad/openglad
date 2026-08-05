/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace og::ui {

enum class TextPickerErrorCode : std::int32_t
{
    None = 0,
    ParseError,
    InvalidSelection,
    CampaignIoError,
    SaveIoError,
    LoadIoError,
    Unsupported,
};

struct TextPickerError
{
    TextPickerErrorCode code = TextPickerErrorCode::None;
    std::string detail;
};

struct TextPickerConfig
{
    std::string campaign = "gladiator";
    int level = 1;
    std::vector<int> team_families;
    std::uint32_t seed = 42;
    // Protocol-mode only: upgrade each spawned team guy to this level
    // (0 = leave loader-default stats). See TextProtocolArgs::team_level.
    int team_level = 0;
    std::string save_name = "text_quicksave";
};

// Runs the text picker state machine. Drives the full lifecycle including
// looping back after games. Returns when the user quits from the picker.
void run_text_picker(TextPickerConfig& config, TextPickerError* error = nullptr);

// Runs the text protocol gameplay session using picker config values.
// Returns the same status code as run_text_protocol_session().
int run_text_picker_protocol_session(const TextPickerConfig& config);

} // namespace og::ui
