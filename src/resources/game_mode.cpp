/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The Tier-B mode dispatch: mounted campaign.yaml `mode:` string ->
// ProgressionKind -> stateless IProgression singleton. See game_mode.h and
// docs/game-modes.md for the seam contract.

#include <openglad/resources/game_mode.h>

#include <openglad/core/util.h>
#include <openglad/resources/campaign_metadata.h>

#include <set>
#include <string>

namespace og::mode {

namespace {

// Classic is all defaults: the base class IS the Classic behavior set.
class ClassicProgression final : public IProgression {
public:
    ProgressionKind kind() const override { return ProgressionKind::Classic; }
};

// Unknown `mode:` strings degrade to Classic; each distinct offender logs
// once per process so a typo'd package doesn't spam every frame.
void log_unknown_mode_once(const std::string& mode)
{
    static std::set<std::string> warned;
    if (warned.insert(mode).second)
        LogError("Unknown campaign mode '{}' — falling back to classic\n", mode);
}

} // namespace

ProgressionKind kind_for_mode_string(std::string_view mode)
{
    if (mode.empty() || mode == "classic")
        return ProgressionKind::Classic;
    if (mode == "tower")
        return ProgressionKind::Tower;
    return ProgressionKind::Classic;
}

IProgression& classic_progression()
{
    static ClassicProgression instance;
    return instance;
}

IProgression& progression_for_kind(ProgressionKind kind)
{
    // Exhaustive switch, no default: adding a ProgressionKind without a case
    // here draws a -Wswitch complaint. Instances are static and stateless.
    switch (kind)
    {
        case ProgressionKind::Classic:
            return classic_progression();
        case ProgressionKind::Tower:
            { extern IProgression& tower_progression(); return tower_progression(); } // WP-5: the Tower instance (tower_progression.cpp)
    }
    return classic_progression(); // unreachable: the switch above is exhaustive
}

IProgression& current_progression()
{
    const std::string mode = og::data::mounted_campaign_mode();
    const ProgressionKind kind = kind_for_mode_string(mode);
    if (kind == ProgressionKind::Classic && !mode.empty() && mode != "classic")
        log_unknown_mode_once(mode);
    return progression_for_kind(kind);
}

} // namespace og::mode
