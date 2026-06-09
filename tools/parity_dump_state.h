#pragma once

#include <cstdint>
#include <string>
#include <vector>

class screen;

namespace og::parity {

struct WalkerEntry
{
    std::uint32_t id           = 0;
    std::string   family;
    std::uint32_t team         = 0;
    std::int32_t  xpos         = 0;
    std::int32_t  ypos         = 0;
    float         hp           = 0.0f;
    float         max_hp       = 0.0f;
    std::int32_t  weapons_left = 0;
    bool          alive        = true;
};

struct EffectEntry
{
    std::uint32_t id       = 0;
    std::string   family;
    std::int32_t  xpos     = 0;
    std::int32_t  ypos     = 0;
    std::int32_t  lifetime = 0;
};

struct EventEntry
{
    std::string   kind;
    std::uint32_t tick     = 0;
    std::uint32_t a        = 0;
    std::uint32_t b        = 0;
    std::string   text;
    std::uint32_t sequence = 0;
};

struct WeaponEntry
{
    std::uint32_t id       = 0;
    std::string   family;
    std::uint32_t team     = 0;
    std::int32_t  xpos     = 0;
    std::int32_t  ypos     = 0;
    std::int32_t  lifetime = 0;
};

struct WeaponTrackSample
{
    std::uint32_t tick     = 0;
    std::string   family;
    std::int32_t  seq      = 0;
    std::int32_t  xpos     = 0;
    std::int32_t  ypos     = 0;
    std::int32_t  lifetime = 0;
};

struct StateDump
{
    std::string              schema_version = "v1";
    std::uint32_t            tick           = 0;
    std::uint32_t            rng_state      = 0;
    bool                     rng_observable = true;
    std::uint32_t            score_per_team[4] = {0, 0, 0, 0};
    std::vector<WalkerEntry> walkers;
    std::vector<EffectEntry> effects;
    std::vector<EventEntry>  events;
    std::int32_t             level_done       = 0;
    std::uint32_t            level_tick_count = 0;
    std::vector<WeaponEntry> weapons;
    std::vector<WeaponTrackSample> weapon_tracks;
};

StateDump capture_state_dump(const screen& game,
                             std::uint32_t tick,
                             std::vector<WeaponTrackSample> weapon_tracks = {});

std::string canonical_serialize(const StateDump& dump);

std::string family_symbol_by_order(std::int32_t order, std::int32_t family_id);
std::string event_kind_symbol(std::uint32_t kind_raw);

} // namespace og::parity
