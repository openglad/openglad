#include <openglad/gameplay/replay.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/util.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <format>
#include <iterator>
#include <type_traits>

namespace og::sim {
namespace {

constexpr std::array<std::uint8_t, 4> kReplayMagic = {
    static_cast<std::uint8_t>('O'),
    static_cast<std::uint8_t>('G'),
    static_cast<std::uint8_t>('R'),
    static_cast<std::uint8_t>('P'),
};

void set_error(ReplayIoError* out_error, ReplayIoError value)
{
    if (out_error != nullptr)
        *out_error = value;
}

void write_u32_le(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void write_i16_le(std::vector<std::uint8_t>& bytes, std::int16_t value)
{
    const std::uint16_t encoded = static_cast<std::uint16_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(encoded & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((encoded >> 8) & 0xffu));
}

std::int16_t read_i16_le(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

void write_bytes(std::vector<std::uint8_t>& bytes,
                 std::span<const std::uint8_t> payload)
{
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

bool read_section(std::span<const std::uint8_t> bytes,
                  std::size_t& offset,
                  std::uint32_t length,
                  std::span<const std::uint8_t>& out_section)
{
    if (offset > bytes.size() || length > (bytes.size() - offset))
        return false;

    out_section = bytes.subspan(offset, length);
    offset += length;
    return true;
}

template <typename T>
std::string value_to_string(const T& value)
{
    if constexpr (std::is_same_v<T, bool>)
    {
        return value ? "true" : "false";
    }
    else if constexpr (std::is_enum_v<T>)
    {
        return std::format("{}", static_cast<int>(value));
    }
    else if constexpr (std::is_same_v<T, std::uint8_t> ||
                       std::is_same_v<T, std::int8_t> ||
                       std::is_same_v<T, char> ||
                       std::is_same_v<T, signed char> ||
                       std::is_same_v<T, unsigned char>)
    {
        return std::format("{}", static_cast<int>(value));
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        return std::format("{:.9g}", value);
    }
    else
    {
        return std::format("{}", value);
    }
}

template <typename T>
bool compare_value(std::string_view field,
                   const T& expected,
                   const T& actual,
                   ReplayVerificationFailure& failure)
{
    if (expected == actual)
        return true;

    failure.field = std::string(field);
    failure.expected_value = value_to_string(expected);
    failure.actual_value = value_to_string(actual);
    return false;
}

bool compare_guy_snapshot(const GuySnapshot& expected,
                          const GuySnapshot& actual,
                          std::string_view prefix,
                          ReplayVerificationFailure& failure)
{
#define OG_REPLAY_COMPARE(field) \
    do { \
        if (!compare_value(std::format("{}" #field, prefix), \
                           expected.field, actual.field, failure)) \
            return false; \
    } while (false)

    OG_REPLAY_COMPARE(guy_id);
    OG_REPLAY_COMPARE(name);
    OG_REPLAY_COMPARE(family);
    OG_REPLAY_COMPARE(strength);
    OG_REPLAY_COMPARE(dexterity);
    OG_REPLAY_COMPARE(constitution);
    OG_REPLAY_COMPARE(intelligence);
    OG_REPLAY_COMPARE(armor);
    OG_REPLAY_COMPARE(exp);
    OG_REPLAY_COMPARE(kills);
    OG_REPLAY_COMPARE(level_kills);
    OG_REPLAY_COMPARE(total_damage);
    OG_REPLAY_COMPARE(total_hits);
    OG_REPLAY_COMPARE(total_shots);
    OG_REPLAY_COMPARE(teamnum);
    OG_REPLAY_COMPARE(scen_damage);
    OG_REPLAY_COMPARE(scen_kills);
    OG_REPLAY_COMPARE(scen_damage_taken);
    OG_REPLAY_COMPARE(scen_min_hp);
    OG_REPLAY_COMPARE(scen_shots);
    OG_REPLAY_COMPARE(scen_hits);
    OG_REPLAY_COMPARE(level);

#undef OG_REPLAY_COMPARE
    return true;
}

bool compare_entity_snapshot(const EntitySnapshot& expected,
                             const EntitySnapshot& actual,
                             std::string_view prefix,
                             ReplayVerificationFailure& failure,
                             bool compare_dirty_masks)
{
    if (compare_dirty_masks)
    {
        for (std::size_t i = 0; i < kEntitySnapshotDirtyMaskWords; ++i)
        {
            if (!compare_value(std::format("{}dirty_mask[{}]", prefix, i),
                               expected.dirty_mask[i],
                               actual.dirty_mask[i],
                               failure))
            {
                return false;
            }
        }
    }

#define OG_REPLAY_COMPARE(field) \
    do { \
        if (!compare_value(std::format("{}" #field, prefix), \
                           expected.field, actual.field, failure)) \
            return false; \
    } while (false)

    OG_REPLAY_COMPARE(guy_id);
    OG_REPLAY_COMPARE(entity_id);
    OG_REPLAY_COMPARE(xpos);
    OG_REPLAY_COMPARE(ypos);
    OG_REPLAY_COMPARE(sizex);
    OG_REPLAY_COMPARE(sizey);
    OG_REPLAY_COMPARE(team_num);
    OG_REPLAY_COMPARE(real_team_num);
    OG_REPLAY_COMPARE(user);
    OG_REPLAY_COMPARE(dead);
    OG_REPLAY_COMPARE(death_called);
    OG_REPLAY_COMPARE(invulnerable_left);
    OG_REPLAY_COMPARE(invisibility_left);
    OG_REPLAY_COMPARE(flight_left);
    OG_REPLAY_COMPARE(bonus_rounds);
    OG_REPLAY_COMPARE(order);
    OG_REPLAY_COMPARE(family);
    OG_REPLAY_COMPARE(frame);
    OG_REPLAY_COMPARE(worldx);
    OG_REPLAY_COMPARE(worldy);
    OG_REPLAY_COMPARE(lastx);
    OG_REPLAY_COMPARE(lasty);
    OG_REPLAY_COMPARE(stepsize);
    OG_REPLAY_COMPARE(normal_stepsize);
    OG_REPLAY_COMPARE(curdir);
    OG_REPLAY_COMPARE(enddir);
    OG_REPLAY_COMPARE(damage);
    OG_REPLAY_COMPARE(fire_frequency);
    OG_REPLAY_COMPARE(busy);
    OG_REPLAY_COMPARE(current_weapon);
    OG_REPLAY_COMPARE(default_weapon);
    OG_REPLAY_COMPARE(attack_lunge);
    OG_REPLAY_COMPARE(attack_lunge_angle);
    OG_REPLAY_COMPARE(hit_recoil);
    OG_REPLAY_COMPARE(hit_recoil_angle);
    OG_REPLAY_COMPARE(last_hitpoints);
    OG_REPLAY_COMPARE(action);
    OG_REPLAY_COMPARE(act_type);
    OG_REPLAY_COMPARE(old_act_type);
    OG_REPLAY_COMPARE(ani_type);
    OG_REPLAY_COMPARE(cycle);
    OG_REPLAY_COMPARE(drawcycle);
    OG_REPLAY_COMPARE(current_special);
    OG_REPLAY_COMPARE(ignore);
    OG_REPLAY_COMPARE(in_act);
    OG_REPLAY_COMPARE(shifter_down);
    OG_REPLAY_COMPARE(yo_delay);
    OG_REPLAY_COMPARE(skip_exit);
    OG_REPLAY_COMPARE(outline);
    OG_REPLAY_COMPARE(hurt_flash);
    OG_REPLAY_COMPARE(lifetime);
    OG_REPLAY_COMPARE(speed_bonus);
    OG_REPLAY_COMPARE(speed_bonus_left);
    OG_REPLAY_COMPARE(charm_left);
    OG_REPLAY_COMPARE(weapons_left);
    OG_REPLAY_COMPARE(keys);
    OG_REPLAY_COMPARE(view_all);
    OG_REPLAY_COMPARE(lineofsight);
    OG_REPLAY_COMPARE(path_check_counter);
    OG_REPLAY_COMPARE(regen_delay);
    OG_REPLAY_COMPARE(foe_id);
    OG_REPLAY_COMPARE(leader_id);
    OG_REPLAY_COMPARE(owner_id);
    OG_REPLAY_COMPARE(collide_ob_id);
    OG_REPLAY_COMPARE(hitpoints);
    OG_REPLAY_COMPARE(max_hitpoints);
    OG_REPLAY_COMPARE(magicpoints);
    OG_REPLAY_COMPARE(max_magicpoints);
    OG_REPLAY_COMPARE(max_heal_delay);
    OG_REPLAY_COMPARE(current_heal_delay);
    OG_REPLAY_COMPARE(max_magic_delay);
    OG_REPLAY_COMPARE(current_magic_delay);
    OG_REPLAY_COMPARE(magic_per_round);
    OG_REPLAY_COMPARE(heal_per_round);
    OG_REPLAY_COMPARE(armor);
    OG_REPLAY_COMPARE(level);
    OG_REPLAY_COMPARE(bit_flags);
    OG_REPLAY_COMPARE(delete_me);
    OG_REPLAY_COMPARE(frozen_delay);
    OG_REPLAY_COMPARE(weapon_cost);
    for (int i = 0; i < NUM_SPECIALS; ++i)
    {
        if (!compare_value(std::format("{}special_cost[{}]", prefix, i),
                           expected.special_cost[i],
                           actual.special_cost[i],
                           failure))
        {
            return false;
        }
    }
    OG_REPLAY_COMPARE(old_order);
    OG_REPLAY_COMPARE(old_family);
    OG_REPLAY_COMPARE(last_distance);
    OG_REPLAY_COMPARE(current_distance);
    OG_REPLAY_COMPARE(controller_id);
    OG_REPLAY_COMPARE(do_bounce);

#undef OG_REPLAY_COMPARE
    return true;
}

bool compare_ctf_snapshot_state(const WorldSnapshot& expected,
                                const WorldSnapshot& actual,
                                ReplayVerificationFailure& failure)
{
    for (int team = 0; team < 4; ++team)
    {
        if (!compare_value(std::format("ctf_captures[{}]", team),
                           expected.ctf_captures[team],
                           actual.ctf_captures[team],
                           failure) ||
            !compare_value(std::format("ctf_team_active[{}]", team),
                           expected.ctf_team_active[team],
                           actual.ctf_team_active[team],
                           failure) ||
            !compare_value(std::format("ctf_anchor_count[{}]", team),
                           expected.ctf_anchor_count[team],
                           actual.ctf_anchor_count[team],
                           failure))
        {
            return false;
        }

        for (int i = 0; i < kCtfMaxAnchorsPerTeam; ++i)
        {
            if (!compare_value(std::format("ctf_anchor_x[{}][{}]", team, i),
                               expected.ctf_anchor_x[team][i],
                               actual.ctf_anchor_x[team][i],
                               failure) ||
                !compare_value(std::format("ctf_anchor_y[{}][{}]", team, i),
                               expected.ctf_anchor_y[team][i],
                               actual.ctf_anchor_y[team][i],
                               failure))
            {
                return false;
            }
        }
    }

    for (int t = 0; t < kCtfMaxFlags; ++t)
    {
        const CtfFlag& expected_flag = expected.ctf_flags[t];
        const CtfFlag& actual_flag = actual.ctf_flags[t];
        const std::string prefix = std::format("ctf_flags[{}].", t);

#define OG_REPLAY_COMPARE(field) \
    do { \
        if (!compare_value(prefix + #field, \
                           expected_flag.field, actual_flag.field, failure)) \
            return false; \
    } while (false)

        OG_REPLAY_COMPARE(state);
        OG_REPLAY_COMPARE(carrier_entity_id);
        OG_REPLAY_COMPARE(x);
        OG_REPLAY_COMPARE(y);
        OG_REPLAY_COMPARE(home_x);
        OG_REPLAY_COMPARE(home_y);
        OG_REPLAY_COMPARE(return_ticks);
        OG_REPLAY_COMPARE(flag_entity_id);
        OG_REPLAY_COMPARE(present);

#undef OG_REPLAY_COMPARE
    }

    for (int i = 0; i < kCtfMaxControlPoints; ++i)
    {
        const CtfControlPoint& expected_cp = expected.ctf_cps[i];
        const CtfControlPoint& actual_cp = actual.ctf_cps[i];
        const std::string prefix = std::format("ctf_cps[{}].", i);

#define OG_REPLAY_COMPARE(field) \
    do { \
        if (!compare_value(prefix + #field, \
                           expected_cp.field, actual_cp.field, failure)) \
            return false; \
    } while (false)

        OG_REPLAY_COMPARE(owner);
        OG_REPLAY_COMPARE(progress);
        OG_REPLAY_COMPARE(progress_team);
        OG_REPLAY_COMPARE(x);
        OG_REPLAY_COMPARE(y);
        OG_REPLAY_COMPARE(radius_tiles);
        OG_REPLAY_COMPARE(entity_id);
        OG_REPLAY_COMPARE(next_pulse_tick);

#undef OG_REPLAY_COMPARE
    }

    if (!compare_value("ctf_respawn_queue.size",
                       expected.ctf_respawn_queue.size(),
                       actual.ctf_respawn_queue.size(),
                       failure))
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.ctf_respawn_queue.size(); ++i)
    {
        const CtfRespawnEntry& expected_entry = expected.ctf_respawn_queue[i];
        const CtfRespawnEntry& actual_entry = actual.ctf_respawn_queue[i];
        const std::string prefix = std::format("ctf_respawn_queue[{}].", i);

#define OG_REPLAY_COMPARE(field) \
    do { \
        if (!compare_value(prefix + #field, \
                           expected_entry.field, actual_entry.field, failure)) \
            return false; \
    } while (false)

        OG_REPLAY_COMPARE(kind);
        OG_REPLAY_COMPARE(team);
        OG_REPLAY_COMPARE(family);
        OG_REPLAY_COMPARE(level);
        OG_REPLAY_COMPARE(ticks_left);
        OG_REPLAY_COMPARE(walker_entity_id);

#undef OG_REPLAY_COMPARE
    }

    return true;
}

bool compare_world_snapshot(const WorldSnapshot& expected,
                            const WorldSnapshot& actual,
                            ReplayVerificationFailure& failure,
                            bool compare_dirty_masks)
{
#define OG_REPLAY_COMPARE(field) \
    do { \
        if (!compare_value(#field, expected.field, actual.field, failure)) \
            return false; \
    } while (false)

    OG_REPLAY_COMPARE(tick_count);
    OG_REPLAY_COMPARE(rng_state);
    OG_REPLAY_COMPARE(level_tick_count);
    OG_REPLAY_COMPARE(level_done);
    OG_REPLAY_COMPARE(game_ended);
    OG_REPLAY_COMPARE(end);
    OG_REPLAY_COMPARE(retry);
    OG_REPLAY_COMPARE(next_level);
    OG_REPLAY_COMPARE(ending);
    OG_REPLAY_COMPARE(enemy_freeze);
    OG_REPLAY_COMPARE(timer_wait);
    OG_REPLAY_COMPARE(living_count);
    OG_REPLAY_COMPARE(control_hp);
    OG_REPLAY_COMPARE(my_team);
    OG_REPLAY_COMPARE(allied_mode);
    OG_REPLAY_COMPARE(difficulty);
    OG_REPLAY_COMPARE(withdraw_requested);
    OG_REPLAY_COMPARE(withdraw_level);
    OG_REPLAY_COMPARE(guy_id_counter);
    OG_REPLAY_COMPARE(current_palette_id);
    OG_REPLAY_COMPARE(pending_exit_prompt);
    OG_REPLAY_COMPARE(paused);
    OG_REPLAY_COMPARE(pause_player_index);
    OG_REPLAY_COMPARE(ctf_active);
    OG_REPLAY_COMPARE(ctf_init_attempted);
    OG_REPLAY_COMPARE(ctf_team_count);
    OG_REPLAY_COMPARE(ctf_capture_limit);
    OG_REPLAY_COMPARE(ctf_respawn_ticks);
    OG_REPLAY_COMPARE(ctf_flag_return_ticks);
    OG_REPLAY_COMPARE(ctf_time_limit_ticks);
    OG_REPLAY_COMPARE(ctf_winner_team);
    OG_REPLAY_COMPARE(ctf_winner_is_player);
    OG_REPLAY_COMPARE(ctf_respawn_serial);
    OG_REPLAY_COMPARE(ctf_cp_count);
    OG_REPLAY_COMPARE(ctf_requested_team_count);
    OG_REPLAY_COMPARE(ctf_requested_capture_limit);
    OG_REPLAY_COMPARE(ctf_requested_respawn_ticks);
    OG_REPLAY_COMPARE(ctf_requested_strip_scenario_troops);
    OG_REPLAY_COMPARE(grid_width);
    OG_REPLAY_COMPARE(grid_height);
    OG_REPLAY_COMPARE(grid_dirty);
    OG_REPLAY_COMPARE(grid_full_resend);

#undef OG_REPLAY_COMPARE

    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (!compare_value(std::format("m_score[{}]", i),
                           expected.m_score[i],
                           actual.m_score[i],
                           failure))
        {
            return false;
        }
    }

    if (!compare_ctf_snapshot_state(expected, actual, failure))
        return false;

    if (!compare_value("full_grid_data.size",
                       expected.full_grid_data.size(),
                       actual.full_grid_data.size(),
                       failure))
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.full_grid_data.size(); ++i)
    {
        if (!compare_value(std::format("full_grid_data[{}]", i),
                           expected.full_grid_data[i],
                           actual.full_grid_data[i],
                           failure))
        {
            return false;
        }
    }

    if (!compare_value("grid_dirty_tiles.size",
                       expected.grid_dirty_tiles.size(),
                       actual.grid_dirty_tiles.size(),
                       failure))
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.grid_dirty_tiles.size(); ++i)
    {
        const std::string prefix = std::format("grid_dirty_tiles[{}].", i);
        if (!compare_value(prefix + "x",
                           expected.grid_dirty_tiles[i].x,
                           actual.grid_dirty_tiles[i].x,
                           failure) ||
            !compare_value(prefix + "y",
                           expected.grid_dirty_tiles[i].y,
                           actual.grid_dirty_tiles[i].y,
                           failure) ||
            !compare_value(prefix + "value",
                           expected.grid_dirty_tiles[i].value,
                           actual.grid_dirty_tiles[i].value,
                           failure))
        {
            return false;
        }
    }

    if (!compare_value("guy_snapshots.size",
                       expected.guy_snapshots.size(),
                       actual.guy_snapshots.size(),
                       failure))
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.guy_snapshots.size(); ++i)
    {
        const std::string prefix = std::format("guy_snapshots[{}].", i);
        if (!compare_guy_snapshot(expected.guy_snapshots[i],
                                  actual.guy_snapshots[i],
                                  prefix,
                                  failure))
        {
            return false;
        }
    }

    if (!compare_value("oblist.size",
                       expected.oblist.size(),
                       actual.oblist.size(),
                       failure))
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.oblist.size(); ++i)
    {
        const std::string prefix = std::format("oblist[{}].", i);
        if (!compare_entity_snapshot(expected.oblist[i],
                                     actual.oblist[i],
                                     prefix,
                                     failure,
                                     compare_dirty_masks))
        {
            return false;
        }
    }

    if (!compare_value("fxlist.size",
                       expected.fxlist.size(),
                       actual.fxlist.size(),
                       failure))
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.fxlist.size(); ++i)
    {
        const std::string prefix = std::format("fxlist[{}].", i);
        if (!compare_entity_snapshot(expected.fxlist[i],
                                     actual.fxlist[i],
                                     prefix,
                                     failure,
                                     compare_dirty_masks))
        {
            return false;
        }
    }

    if (!compare_value("weaplist.size",
                       expected.weaplist.size(),
                       actual.weaplist.size(),
                       failure))
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.weaplist.size(); ++i)
    {
        const std::string prefix = std::format("weaplist[{}].", i);
        if (!compare_entity_snapshot(expected.weaplist[i],
                                     actual.weaplist[i],
                                     prefix,
                                     failure,
                                     compare_dirty_masks))
        {
            return false;
        }
    }

    if (!compare_value("removed_entity_ids.size",
                       expected.removed_entity_ids.size(),
                       actual.removed_entity_ids.size(),
                       failure))
    {
        return false;
    }
    for (std::size_t i = 0; i < expected.removed_entity_ids.size(); ++i)
    {
        if (!compare_value(std::format("removed_entity_ids[{}]", i),
                           expected.removed_entity_ids[i],
                           actual.removed_entity_ids[i],
                           failure))
        {
            return false;
        }
    }

    if (!compare_value("snapshot_hash",
                       expected.snapshot_hash,
                       actual.snapshot_hash,
                       failure))
    {
        return false;
    }

    return true;
}

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path,
                                          ReplayIoError* out_error)
{
    std::ifstream infile(path, std::ios::binary | std::ios::ate);
    if (!infile)
    {
        set_error(out_error, ReplayIoError::OpenReadFailed);
        return {};
    }

    const std::streamsize size = infile.tellg();
    if (size < 0)
    {
        set_error(out_error, ReplayIoError::MalformedData);
        return {};
    }

    infile.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty())
    {
        infile.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!infile)
        {
            set_error(out_error, ReplayIoError::MalformedData);
            return {};
        }
    }

    set_error(out_error, ReplayIoError::None);
    return bytes;
}

} // namespace

std::vector<std::uint8_t> serialize_replay(const ReplayHeader& header,
                                           const WorldSnapshot& initial_snapshot,
                                           std::span<const InputStateMessage> frames)
{
    const std::vector<std::uint8_t> initial_snapshot_bytes =
        serialize_snapshot(initial_snapshot);
    const std::span<const std::uint8_t> campaign_bytes(
        reinterpret_cast<const std::uint8_t*>(header.campaign_id.data()),
        header.campaign_id.size());

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kReplayHeaderSize + campaign_bytes.size() +
                  initial_snapshot_bytes.size() +
                  frames.size() * kSerializedInputMessageSize);

    bytes.insert(bytes.end(), kReplayMagic.begin(), kReplayMagic.end());
    bytes.push_back(header.version);
    bytes.push_back(header.player_count);
    bytes.push_back(static_cast<std::uint8_t>(header.timer_wait));
    bytes.push_back(0);
    write_u32_le(bytes, header.initial_rng_state);
    write_u32_le(bytes, static_cast<std::uint32_t>(header.level_id));
    write_i16_le(bytes, header.my_team);
    write_i16_le(bytes, header.allied_mode);
    write_i16_le(bytes, header.difficulty);
    write_i16_le(bytes, 0);
    write_u32_le(bytes, static_cast<std::uint32_t>(campaign_bytes.size()));
    write_u32_le(bytes, static_cast<std::uint32_t>(initial_snapshot_bytes.size()));
    write_bytes(bytes, campaign_bytes);
    write_bytes(bytes, initial_snapshot_bytes);

    for (const InputStateMessage& frame : frames)
    {
        const auto serialized = serialize_input(frame.tick, frame.input);
        write_bytes(bytes, serialized);
    }

    return bytes;
}

std::optional<ReplayData> deserialize_replay(std::span<const std::uint8_t> bytes,
                                             ReplayIoError* out_error)
{
    set_error(out_error, ReplayIoError::None);

    if (bytes.size() < kReplayHeaderSize)
    {
        set_error(out_error, ReplayIoError::InvalidHeader);
        return std::nullopt;
    }

    if (!std::equal(kReplayMagic.begin(), kReplayMagic.end(), bytes.begin()))
    {
        set_error(out_error, ReplayIoError::InvalidHeader);
        return std::nullopt;
    }

    ReplayData replay;
    replay.header.version = bytes[4];
    if (replay.header.version != kReplayFormatVersion)
    {
        set_error(out_error, ReplayIoError::UnsupportedVersion);
        return std::nullopt;
    }

    replay.header.player_count = bytes[5];
    replay.header.timer_wait = static_cast<std::int8_t>(bytes[6]);
    replay.header.initial_rng_state = read_u32_le(bytes, 8);
    replay.header.level_id = static_cast<std::int32_t>(read_u32_le(bytes, 12));
    replay.header.my_team = read_i16_le(bytes, 16);
    replay.header.allied_mode = read_i16_le(bytes, 18);
    replay.header.difficulty = read_i16_le(bytes, 20);
    const std::uint32_t campaign_length = read_u32_le(bytes, 24);
    const std::uint32_t initial_snapshot_length = read_u32_le(bytes, 28);

    if (replay.header.player_count > MAX_PLAYERS)
    {
        set_error(out_error, ReplayIoError::MalformedData);
        return std::nullopt;
    }

    std::size_t payload_offset = kReplayHeaderSize;
    std::span<const std::uint8_t> campaign_bytes;
    std::span<const std::uint8_t> initial_snapshot_bytes;
    if (!read_section(bytes, payload_offset, campaign_length, campaign_bytes) ||
        !read_section(bytes,
                      payload_offset,
                      initial_snapshot_length,
                      initial_snapshot_bytes))
    {
        set_error(out_error, ReplayIoError::MalformedData);
        return std::nullopt;
    }

    replay.header.campaign_id.assign(
        reinterpret_cast<const char*>(campaign_bytes.data()),
        campaign_bytes.size());
    if (replay.header.campaign_id.empty() || initial_snapshot_bytes.empty())
    {
        set_error(out_error, ReplayIoError::MalformedData);
        return std::nullopt;
    }

    try
    {
        replay.initial_snapshot =
            deserialize_snapshot(initial_snapshot_bytes);
    }
    catch (const std::runtime_error&)
    {
        set_error(out_error, ReplayIoError::MalformedData);
        return std::nullopt;
    }

    const std::size_t payload_size = bytes.size() - payload_offset;
    if ((payload_size % kSerializedInputMessageSize) != 0)
    {
        set_error(out_error, ReplayIoError::MalformedData);
        return std::nullopt;
    }

    const std::size_t frame_count = payload_size / kSerializedInputMessageSize;
    replay.frames.reserve(frame_count);
    for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index)
    {
        const std::size_t offset =
            payload_offset + frame_index * kSerializedInputMessageSize;
        const std::span<const std::uint8_t> frame_bytes =
            bytes.subspan(offset, kSerializedInputMessageSize);
        const std::optional<InputStateMessage> frame =
            deserialize_input_message(frame_bytes);
        if (!frame.has_value())
        {
            set_error(out_error, ReplayIoError::MalformedData);
            return std::nullopt;
        }

        replay.frames.push_back(*frame);
    }

    return replay;
}

std::optional<ReplayVerificationFailure>
find_first_snapshot_difference(std::uint32_t tick,
                               const WorldSnapshot& expected,
                               const WorldSnapshot& actual,
                               bool compare_dirty_masks)
{
    ReplayVerificationFailure failure;
    failure.tick = tick;
    if (compare_world_snapshot(expected, actual, failure, compare_dirty_masks))
        return std::nullopt;
    return failure;
}

void ReplayRecorder::clear() noexcept
{
    header_ = {};
    initial_snapshot_ = {};
    has_initial_snapshot_ = false;
    frames_.clear();
    checkpoints_.clear();
}

void ReplayRecorder::record_input(std::uint32_t tick, const InputState& input)
{
    frames_.push_back(InputStateMessage{tick, input});
}

void ReplayRecorder::set_initial_snapshot(const WorldSnapshot& snapshot)
{
    initial_snapshot_ = snapshot;
    has_initial_snapshot_ = true;
}

void ReplayRecorder::record_initial_world(GameWorld& world)
{
    set_initial_snapshot(peek_keyframe_snapshot(world));
}

void ReplayRecorder::record_snapshot(std::uint32_t tick,
                                     const WorldSnapshot& snapshot,
                                     ReplayCheckpointKind kind)
{
    if (!has_initial_snapshot_ &&
        tick == 0 &&
        kind == ReplayCheckpointKind::Keyframe)
    {
        set_initial_snapshot(snapshot);
    }
    checkpoints_.push_back(ReplayCheckpoint{tick, kind, snapshot});
}

void ReplayRecorder::record_world_snapshot(std::uint32_t tick, GameWorld& world)
{
    record_snapshot(tick, peek_snapshot(world), ReplayCheckpointKind::Snapshot);
}

void ReplayRecorder::record_world_keyframe(std::uint32_t tick, GameWorld& world)
{
    record_snapshot(tick, peek_keyframe_snapshot(world), ReplayCheckpointKind::Keyframe);
}

std::vector<std::uint8_t> ReplayRecorder::serialize() const
{
    return serialize_replay(header_, initial_snapshot_, frames_);
}

bool ReplayRecorder::write_file(const std::filesystem::path& path,
                                ReplayIoError* out_error) const
{
    if (header_.campaign_id.empty() || !has_initial_snapshot_)
    {
        set_error(out_error, ReplayIoError::MalformedData);
        return false;
    }

    const std::vector<std::uint8_t> bytes = serialize();
    std::ofstream outfile(path, std::ios::binary | std::ios::trunc);
    if (!outfile)
    {
        set_error(out_error, ReplayIoError::OpenWriteFailed);
        return false;
    }

    if (!bytes.empty())
        outfile.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
    if (!outfile)
    {
        set_error(out_error, ReplayIoError::OpenWriteFailed);
        return false;
    }

    set_error(out_error, ReplayIoError::None);
    return true;
}

void ReplayPlayer::clear() noexcept
{
    data_ = {};
    reset();
    checkpoints_.clear();
}

bool ReplayPlayer::load_bytes(std::span<const std::uint8_t> bytes,
                              ReplayIoError* out_error)
{
    clear();

    const std::optional<ReplayData> replay = deserialize_replay(bytes, out_error);
    if (!replay.has_value())
        return false;

    data_ = *replay;
    set_error(out_error, ReplayIoError::None);
    return true;
}

bool ReplayPlayer::load_file(const std::filesystem::path& path,
                             ReplayIoError* out_error)
{
    clear();

    if (!std::filesystem::exists(path))
    {
        set_error(out_error, ReplayIoError::OpenReadFailed);
        return false;
    }

    const std::vector<std::uint8_t> bytes = read_file_bytes(path, out_error);
    if (out_error != nullptr && *out_error != ReplayIoError::None)
        return false;

    return load_bytes(bytes, out_error);
}

void ReplayPlayer::reset() noexcept
{
    next_frame_index_ = 0;
    next_checkpoint_index_ = 0;
    first_divergence_.reset();
}

void ReplayPlayer::set_checkpoints(std::vector<ReplayCheckpoint> checkpoints)
{
    checkpoints_ = std::move(checkpoints);
    std::stable_sort(checkpoints_.begin(),
                     checkpoints_.end(),
                     [](const ReplayCheckpoint& lhs, const ReplayCheckpoint& rhs) {
                         return lhs.tick < rhs.tick;
                     });
    next_checkpoint_index_ = 0;
    first_divergence_.reset();
}

std::optional<InputStateMessage> ReplayPlayer::next_frame()
{
    if (!has_next_frame())
        return std::nullopt;

    return data_.frames[next_frame_index_++];
}

std::optional<ReplayVerificationFailure> ReplayPlayer::verify_world(
    GameWorld& world,
    std::uint32_t tick,
    bool compare_dirty_masks)
{
    while (next_checkpoint_index_ < checkpoints_.size() &&
           checkpoints_[next_checkpoint_index_].tick < tick)
    {
        next_checkpoint_index_++;
    }

    if (next_checkpoint_index_ >= checkpoints_.size() ||
        checkpoints_[next_checkpoint_index_].tick != tick)
    {
        return std::nullopt;
    }

    const ReplayCheckpoint& checkpoint = checkpoints_[next_checkpoint_index_++];
    const WorldSnapshot actual =
        checkpoint.kind == ReplayCheckpointKind::Snapshot
            ? peek_snapshot(world)
            : peek_keyframe_snapshot(world);
    const std::optional<ReplayVerificationFailure> failure =
        find_first_snapshot_difference(tick,
                                       checkpoint.snapshot,
                                       actual,
                                       compare_dirty_masks);
    if (failure.has_value())
    {
        if (!first_divergence_.has_value())
            first_divergence_ = failure;
        LogError("replay_divergence tick={} field={} expected={} actual={}\n",
                 failure->tick,
                 failure->field,
                 failure->expected_value,
                 failure->actual_value);
    }

    return failure;
}

} // namespace og::sim
