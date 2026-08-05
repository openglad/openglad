/*
 * WorldSnapshot / EntitySnapshot - SDL-free snapshot DTOs for authoritative
 * world state replication.
 */
#pragma once

#include <openglad/core/order.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/dirty_field_bits.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/statistics.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace og::sim {

class SimEventLog;
}

class GameWorld;

namespace og::sim {

inline constexpr std::size_t kEntitySnapshotDirtyMaskWords = 2;
inline constexpr std::int32_t kNoGuyId = -1;
inline constexpr std::uint8_t kNoPausePlayerIndex = 0xff;
inline constexpr std::uint8_t kSnapshotFormatVersion = 10;
inline constexpr std::uint8_t kSnapshotProtocolVersion = kNetworkProtocolVersion;
inline constexpr std::uint8_t kDeltaPayloadUncompressedFlag = 0x01;
inline constexpr std::size_t kDeltaPayloadHeaderSize = 1;

using EntitySnapshotDirtyMask =
    std::array<std::uint64_t, kEntitySnapshotDirtyMaskWords>;

// Fixed-layout entity payload used by capture/apply and byte serialization.
// Keep this trivially copyable so memcpy-based field serialization stays safe.
struct EntitySnapshot {
    std::uint64_t dirty_mask[kEntitySnapshotDirtyMaskWords] = {};

    // Player character linkage is serialized with the entity header but is not
    // dirty-mask tracked. WorldSnapshot::guy_snapshots carries the actual data.
    // Use a negative sentinel because real guy IDs are allocated from 0 upward.
    std::int32_t guy_id = kNoGuyId;

    std::uint32_t entity_id = 0;
    std::int16_t xpos = 0;
    std::int16_t ypos = 0;
    std::int16_t sizex = 0;
    std::int16_t sizey = 0;
    std::uint8_t team_num = 0;
    std::uint8_t real_team_num = 255;
    std::int8_t user = -1;
    std::int16_t dead = 0;
    std::int16_t death_called = 0;
    std::int16_t invulnerable_left = 0;
    std::int16_t invisibility_left = 0;
    std::int16_t flight_left = 0;
    std::int16_t bonus_rounds = 0;
    Order order = Order::Living;
    std::int8_t family = 0;
    std::int16_t frame = 0;
    float worldx = -1.0f;
    float worldy = -1.0f;

    float lastx = 0.0f;
    float lasty = 0.0f;
    float stepsize = 0.0f;
    float normal_stepsize = 0.0f;
    std::int8_t curdir = 0;
    std::int8_t enddir = 0;
    float damage = 0.0f;
    float fire_frequency = 0.0f;
    float busy = 0.0f;
    std::uint16_t current_weapon = 0;
    std::uint16_t default_weapon = 0;
    float attack_lunge = 0.0f;
    float attack_lunge_angle = 0.0f;
    float hit_recoil = 0.0f;
    float hit_recoil_angle = 0.0f;
    float last_hitpoints = 0.0f;
    std::int8_t action = 0;
    std::int8_t act_type = 0;
    std::int8_t old_act_type = 0;
    std::int8_t ani_type = 0;
    std::int8_t cycle = 0;
    std::uint8_t drawcycle = 0;
    std::int8_t current_special = 0;
    std::int8_t ignore = 0;
    std::uint8_t in_act = 0;
    std::int16_t shifter_down = 0;
    std::int16_t yo_delay = 0;
    std::int16_t skip_exit = 0;
    std::uint8_t outline = 0;
    std::uint8_t hurt_flash = 0;
    std::int32_t lifetime = 0;
    float speed_bonus = 0.0f;
    std::int32_t speed_bonus_left = 0;
    std::int16_t charm_left = 0;
    std::int16_t weapons_left = 0;
    std::uint32_t keys = 0;
    std::int16_t view_all = 0;
    std::int32_t lineofsight = 0;
    std::int32_t path_check_counter = 0;
    std::int32_t regen_delay = 0;
    std::uint32_t foe_id = 0;
    std::uint32_t leader_id = 0;
    std::uint32_t owner_id = 0;
    std::uint32_t collide_ob_id = 0;

    float hitpoints = 0.0f;
    float max_hitpoints = 0.0f;
    float magicpoints = 0.0f;
    float max_magicpoints = 0.0f;
    std::int32_t max_heal_delay = 0;
    std::int32_t current_heal_delay = 0;
    std::int32_t max_magic_delay = 0;
    std::int32_t current_magic_delay = 0;
    float magic_per_round = 0.0f;
    float heal_per_round = 0.0f;
    float armor = 0.0f;
    std::int32_t level = 0;
    std::int32_t bit_flags = 0;
    std::int16_t delete_me = 0;
    std::int16_t frozen_delay = 0;
    std::int16_t weapon_cost = 0;
    std::uint16_t special_cost[NUM_SPECIALS] = {};
    Order old_order = Order::Living;
    std::int8_t old_family = 0;
    std::uint32_t last_distance = 0;
    std::int32_t current_distance = 0;
    std::uint32_t controller_id = 0;

    std::int32_t do_bounce = 0;

    // Z-axis / multi-floor (dirty bits 86-89). Defaults 0 so single-floor
    // capture + serialization is byte-identical to pre-Z. floor is uint8 (caps
    // at 255 floors, saves a wire byte) and is clamped to the valid range on
    // apply for untrusted-peer safety.
    float worldz = 0.0f;
    float vz = 0.0f;
    std::int16_t sizez = 0;
    std::uint8_t floor = 0;

    // Level-entry spawn point (dirty bits 90-92). Defaults match the walker
    // defaults (-1/-1 x/y sentinel = never recorded, floor 0), so capturing a
    // pre-feature walker produces the sentinel and apply restores it exactly.
    std::int16_t spawn_x = -1;
    std::int16_t spawn_y = -1;
    std::uint8_t spawn_floor = 0;
};

struct GuySnapshot {
    std::int32_t guy_id = 0;
    std::string name;
    std::int8_t family = 0;
    std::int16_t strength = 0;
    std::int16_t dexterity = 0;
    std::int16_t constitution = 0;
    std::int16_t intelligence = 0;
    std::int16_t armor = 0;
    std::uint32_t exp = 0;
    std::int16_t kills = 0;
    std::int32_t level_kills = 0;
    std::int32_t total_damage = 0;
    std::int32_t total_hits = 0;
    std::int32_t total_shots = 0;
    std::int16_t teamnum = 0;
    float scen_damage = 0.0f;
    std::int16_t scen_kills = 0;
    float scen_damage_taken = 0.0f;
    float scen_min_hp = 0.0f;
    std::int16_t scen_shots = 0;
    std::int16_t scen_hits = 0;
    std::int16_t level = 0;
    // Networked player slot that owns this character (guy::kNoOwner == 0xff for
    // single-player / AI). Lets each peer persist only its own characters.
    std::uint8_t owner_player_index = 0xff;
    // The owning player's save0 team slot this character came from.
    std::uint8_t owner_save_slot = 0xff;
};

struct GridTileSnapshot {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint8_t value = 0;
};

struct SimEventBatch {
    // Phase 6 reuses the producing tick as a monotonic batch sequence.
    std::uint32_t sequence = 0;
    std::vector<Event> events;
};

using GameFlowEventBatch = SimEventBatch;

struct WorldSnapshot {
    std::uint32_t tick_count = 0;
    std::uint32_t rng_state = 0;
    std::uint32_t level_tick_count = 0;

    std::int16_t level_done = 0;
    bool game_ended = false;
    std::int8_t end = 0;
    bool retry = false;
    std::int16_t next_level = -1;
    std::int16_t ending = 0;

    std::int32_t enemy_freeze = 0;
    std::int8_t timer_wait = 6;
    std::int32_t living_count = 0;
    float control_hp = 0.0f;
    std::int16_t my_team = 0;
    std::int16_t allied_mode = 0;
    std::int16_t difficulty = 100;
    bool withdraw_requested = false;
    std::int16_t withdraw_level = -1;
    std::int32_t guy_id_counter = 0;
    std::uint32_t m_score[4] = {};

    std::uint8_t current_palette_id = 0;
    bool pending_exit_prompt = false;
    bool paused = false;
    std::uint8_t pause_player_index = kNoPausePlayerIndex;
    // Per-level weather kind (WeatherKind as a wire byte). Render-only world
    // state: the authoritative side rolls it once per level and every client
    // applies it via set_weather. Values above WeatherKind::Snow clamp to
    // None on apply (untrusted-peer safety).
    std::uint8_t weather = 0;
    std::uint32_t snapshot_hash = 0;

    // Snapshot v10: the CTF block is replaced by the respawn-engine block +
    // the scripted-mode block. Captured normalized: anchors and the respawn
    // queue carry exactly the counted entries (everything past a count stays
    // default), and mode name/HUD text is NUL-terminated, so struct
    // comparison matches the wire encoding.
    og::sim::RespawnState respawn;
    og::sim::ModeState mode;
    // Lobby-requested match knobs (GameWorld config shorts). Storage names
    // keep the ctf_ prefix on purpose: they are the generic match settings
    // (og.match_setting) and renaming them would cost a SaveData bump.
    std::int16_t ctf_requested_team_count = 0; // 0 = Auto
    std::int16_t ctf_requested_capture_limit = 0;
    std::int16_t ctf_requested_respawn_ticks = 0;
    std::int16_t ctf_requested_strip_scenario_troops = 0;
    // Classic respawn / generator knobs (GameWorld scalars).
    std::int16_t respawn_mode = 0;
    std::int16_t generator_rate = 0;
    // Snapshot v9 (protocol v8, company-basecamp design §4.1/§4.4): the
    // server-authoritative control policy (0 = legacy claim-anything, 1 =
    // owner-locked) and the per-global-player machine map, serialized right
    // after the generator_rate scalar. Encoding per entry: low 7 bits =
    // machine id (the owning peer's lowest bound global player index),
    // bit7 = "this player's machine deployed >= 1 character this level",
    // 0xff = no player bound at that index.
    std::uint8_t control_policy = 0;
    std::array<std::uint8_t, kMaxGlobalPlayers> player_machine = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    std::uint8_t grid_width = 0;
    std::uint8_t grid_height = 0;
    bool grid_dirty = false;
    bool grid_full_resend = false;
    std::vector<std::uint8_t> full_grid_data;
    std::vector<GridTileSnapshot> grid_dirty_tiles;

    std::vector<GuySnapshot> guy_snapshots;
    std::vector<EntitySnapshot> oblist;
    std::vector<EntitySnapshot> fxlist;
    std::vector<EntitySnapshot> weaplist;

    // Present for delta serialization even though keyframes can ignore it.
    std::vector<std::uint32_t> removed_entity_ids;
};

struct EntitySnapshotFieldDesc {
    std::uint8_t bit_index = 0;
    std::uint8_t size = 0;
    std::uint16_t snap_offset = 0;
};

constexpr bool entity_snapshot_field_is_manual(std::uint8_t bit_index)
{
    return bit_index == og::dirty::BIT_REGEN_DELAY ||
           bit_index == og::dirty::BIT_DO_BOUNCE;
}

inline constexpr std::uint8_t kEntitySnapshotManualBits[] = {
    og::dirty::BIT_REGEN_DELAY,
    og::dirty::BIT_DO_BOUNCE,
};

inline constexpr std::size_t kEntitySnapshotManualFieldCount =
    sizeof(kEntitySnapshotManualBits) / sizeof(kEntitySnapshotManualBits[0]);

inline constexpr EntitySnapshotFieldDesc kEntitySnapshotFields[] = {
    {og::dirty::BIT_ENTITY_ID, sizeof(std::uint32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, entity_id))},
    {og::dirty::BIT_XPOS, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, xpos))},
    {og::dirty::BIT_YPOS, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, ypos))},
    {og::dirty::BIT_SIZEX, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, sizex))},
    {og::dirty::BIT_SIZEY, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, sizey))},
    {og::dirty::BIT_TEAM_NUM, sizeof(std::uint8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, team_num))},
    {og::dirty::BIT_REAL_TEAM_NUM, sizeof(std::uint8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, real_team_num))},
    {og::dirty::BIT_USER, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, user))},
    {og::dirty::BIT_DEAD, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, dead))},
    {og::dirty::BIT_DEATH_CALLED, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, death_called))},
    {og::dirty::BIT_INVULNERABLE_LEFT, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, invulnerable_left))},
    {og::dirty::BIT_INVISIBILITY_LEFT, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, invisibility_left))},
    {og::dirty::BIT_FLIGHT_LEFT, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, flight_left))},
    {og::dirty::BIT_BONUS_ROUNDS, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, bonus_rounds))},
    {og::dirty::BIT_ORDER, sizeof(Order),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, order))},
    {og::dirty::BIT_FAMILY, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, family))},
    {og::dirty::BIT_FRAME, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, frame))},
    {og::dirty::BIT_WORLDX, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, worldx))},
    {og::dirty::BIT_WORLDY, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, worldy))},
    {og::dirty::BIT_LASTX, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, lastx))},
    {og::dirty::BIT_LASTY, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, lasty))},
    {og::dirty::BIT_STEPSIZE, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, stepsize))},
    {og::dirty::BIT_NORMAL_STEPSIZE, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, normal_stepsize))},
    {og::dirty::BIT_CURDIR, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, curdir))},
    {og::dirty::BIT_ENDDIR, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, enddir))},
    {og::dirty::BIT_DAMAGE, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, damage))},
    {og::dirty::BIT_FIRE_FREQUENCY, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, fire_frequency))},
    {og::dirty::BIT_BUSY, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, busy))},
    {og::dirty::BIT_CURRENT_WEAPON, sizeof(std::uint16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, current_weapon))},
    {og::dirty::BIT_DEFAULT_WEAPON, sizeof(std::uint16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, default_weapon))},
    {og::dirty::BIT_ATTACK_LUNGE, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, attack_lunge))},
    {og::dirty::BIT_ATTACK_LUNGE_ANGLE, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, attack_lunge_angle))},
    {og::dirty::BIT_HIT_RECOIL, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, hit_recoil))},
    {og::dirty::BIT_HIT_RECOIL_ANGLE, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, hit_recoil_angle))},
    {og::dirty::BIT_LAST_HITPOINTS, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, last_hitpoints))},
    {og::dirty::BIT_ACTION, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, action))},
    {og::dirty::BIT_ACT_TYPE, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, act_type))},
    {og::dirty::BIT_OLD_ACT_TYPE, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, old_act_type))},
    {og::dirty::BIT_ANI_TYPE, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, ani_type))},
    {og::dirty::BIT_CYCLE, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, cycle))},
    {og::dirty::BIT_DRAWCYCLE, sizeof(std::uint8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, drawcycle))},
    {og::dirty::BIT_CURRENT_SPECIAL, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, current_special))},
    {og::dirty::BIT_IGNORE, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, ignore))},
    {og::dirty::BIT_IN_ACT, sizeof(std::uint8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, in_act))},
    {og::dirty::BIT_SHIFTER_DOWN, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, shifter_down))},
    {og::dirty::BIT_YO_DELAY, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, yo_delay))},
    {og::dirty::BIT_SKIP_EXIT, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, skip_exit))},
    {og::dirty::BIT_OUTLINE, sizeof(std::uint8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, outline))},
    {og::dirty::BIT_HURT_FLASH, sizeof(std::uint8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, hurt_flash))},
    {og::dirty::BIT_LIFETIME, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, lifetime))},
    {og::dirty::BIT_SPEED_BONUS, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, speed_bonus))},
    {og::dirty::BIT_SPEED_BONUS_LEFT, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, speed_bonus_left))},
    {og::dirty::BIT_CHARM_LEFT, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, charm_left))},
    {og::dirty::BIT_WEAPONS_LEFT, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, weapons_left))},
    {og::dirty::BIT_KEYS, sizeof(std::uint32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, keys))},
    {og::dirty::BIT_VIEW_ALL, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, view_all))},
    {og::dirty::BIT_LINEOFSIGHT, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, lineofsight))},
    {og::dirty::BIT_PATH_CHECK_COUNTER, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, path_check_counter))},
    {og::dirty::BIT_FOE_ID, sizeof(std::uint32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, foe_id))},
    {og::dirty::BIT_LEADER_ID, sizeof(std::uint32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, leader_id))},
    {og::dirty::BIT_OWNER_ID, sizeof(std::uint32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, owner_id))},
    {og::dirty::BIT_COLLIDE_OB_ID, sizeof(std::uint32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, collide_ob_id))},
    {og::dirty::BIT_HITPOINTS, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, hitpoints))},
    {og::dirty::BIT_MAX_HITPOINTS, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, max_hitpoints))},
    {og::dirty::BIT_MAGICPOINTS, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, magicpoints))},
    {og::dirty::BIT_MAX_MAGICPOINTS, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, max_magicpoints))},
    {og::dirty::BIT_MAX_HEAL_DELAY, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, max_heal_delay))},
    {og::dirty::BIT_CURRENT_HEAL_DELAY, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, current_heal_delay))},
    {og::dirty::BIT_MAX_MAGIC_DELAY, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, max_magic_delay))},
    {og::dirty::BIT_CURRENT_MAGIC_DELAY, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, current_magic_delay))},
    {og::dirty::BIT_MAGIC_PER_ROUND, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, magic_per_round))},
    {og::dirty::BIT_HEAL_PER_ROUND, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, heal_per_round))},
    {og::dirty::BIT_ARMOR, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, armor))},
    {og::dirty::BIT_LEVEL, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, level))},
    {og::dirty::BIT_BIT_FLAGS, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, bit_flags))},
    {og::dirty::BIT_DELETE_ME, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, delete_me))},
    {og::dirty::BIT_FROZEN_DELAY, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, frozen_delay))},
    {og::dirty::BIT_WEAPON_COST, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, weapon_cost))},
    {og::dirty::BIT_SPECIAL_COST,
     static_cast<std::uint8_t>(sizeof(std::uint16_t) * NUM_SPECIALS),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, special_cost))},
    {og::dirty::BIT_OLD_ORDER, sizeof(Order),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, old_order))},
    {og::dirty::BIT_OLD_FAMILY, sizeof(std::int8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, old_family))},
    {og::dirty::BIT_LAST_DISTANCE, sizeof(std::uint32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, last_distance))},
    {og::dirty::BIT_CURRENT_DISTANCE, sizeof(std::int32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, current_distance))},
    {og::dirty::BIT_CONTROLLER_ID, sizeof(std::uint32_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, controller_id))},
    {og::dirty::BIT_WORLDZ, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, worldz))},
    {og::dirty::BIT_VZ, sizeof(float),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, vz))},
    {og::dirty::BIT_SIZEZ, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, sizez))},
    {og::dirty::BIT_FLOOR, sizeof(std::uint8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, floor))},
    {og::dirty::BIT_SPAWN_X, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, spawn_x))},
    {og::dirty::BIT_SPAWN_Y, sizeof(std::int16_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, spawn_y))},
    {og::dirty::BIT_SPAWN_FLOOR, sizeof(std::uint8_t),
     static_cast<std::uint16_t>(offsetof(EntitySnapshot, spawn_floor))},
};

inline constexpr std::size_t kEntitySnapshotTableFieldCount =
    sizeof(kEntitySnapshotFields) / sizeof(kEntitySnapshotFields[0]);
inline constexpr std::size_t kEntitySnapshotTrackedFieldCount =
    kEntitySnapshotTableFieldCount + kEntitySnapshotManualFieldCount;

constexpr bool entity_snapshot_field_table_is_valid()
{
    if (kEntitySnapshotTrackedFieldCount != og::dirty::FIELD_COUNT) {
        return false;
    }

    std::array<bool, og::dirty::FIELD_COUNT> seen_bits = {};

    for (std::size_t i = 0; i < kEntitySnapshotTableFieldCount; ++i) {
        const EntitySnapshotFieldDesc& field = kEntitySnapshotFields[i];
        if (entity_snapshot_field_is_manual(field.bit_index)) {
            return false;
        }
        if (field.size == 0) {
            return false;
        }
        if (static_cast<std::size_t>(field.snap_offset) + field.size >
            sizeof(EntitySnapshot)) {
            return false;
        }
        if (seen_bits[field.bit_index]) {
            return false;
        }
        seen_bits[field.bit_index] = true;
    }

    for (std::uint8_t bit = 0; bit < og::dirty::FIELD_COUNT; ++bit) {
        if (entity_snapshot_field_is_manual(bit)) {
            if (seen_bits[bit]) {
                return false;
            }
            continue;
        }

        if (!seen_bits[bit]) {
            return false;
        }
    }

    return true;
}

static_assert(std::is_standard_layout_v<EntitySnapshot>,
              "EntitySnapshot must stay standard-layout for offsetof-based tables");
static_assert(std::is_trivially_copyable_v<EntitySnapshot>,
              "EntitySnapshot must stay trivially copyable for memcpy-based serialization");
static_assert(kEntitySnapshotTrackedFieldCount == og::dirty::FIELD_COUNT,
              "EntitySnapshot tracked field count drift -- update world_snapshot.h");
static_assert(entity_snapshot_field_table_is_valid(),
              "EntitySnapshot field table drift -- update world_snapshot.h");

// Captures a tick snapshot and drains per-tick replication bookkeeping from
// the live world, including entity dirty masks, removed entity ids, and grid
// dirty tiles.
WorldSnapshot capture_snapshot(GameWorld& world);
WorldSnapshot capture_keyframe_snapshot(GameWorld& world);
// Non-destructive snapshot variants for replay/debug verification. These
// preserve entity dirty masks and transient bookkeeping in the live world.
WorldSnapshot peek_snapshot(GameWorld& world);
WorldSnapshot peek_keyframe_snapshot(GameWorld& world);
// Returns false when the snapshot could not be faithfully applied — a
// full-grid resend whose payload size mismatches the target world's grid
// (the world is running a DIFFERENT map than the sender: the unrecoverable
// desync shape), or a world with no bound gameplay context. Entity/scalar
// state is still applied best-effort; callers bound their retries on the
// signal instead of rubber-banding forever.
bool apply_snapshot(GameWorld& world, const WorldSnapshot& snapshot);
std::uint32_t compute_snapshot_hash(const WorldSnapshot& snapshot);
std::vector<std::uint8_t> serialize_snapshot(const WorldSnapshot& snapshot);
WorldSnapshot deserialize_snapshot(std::span<const std::uint8_t> data);
std::vector<std::uint8_t> serialize_delta(const WorldSnapshot& delta);
WorldSnapshot deserialize_delta(std::span<const std::uint8_t> data);
std::vector<std::uint8_t> serialize_sim_event_batch(const SimEventBatch& batch);
SimEventBatch deserialize_sim_event_batch(std::span<const std::uint8_t> data);
std::vector<std::uint8_t> serialize_game_flow_event_batch(const SimEventBatch& batch);
SimEventBatch deserialize_game_flow_event_batch(std::span<const std::uint8_t> data);
bool is_game_flow_event(EventKind kind) noexcept;
void normalize_endgame_event_order(GameFlowEventBatch& batch);
void split_event_batches(const SimEventBatch& source,
                         SimEventBatch& sim_batch,
                         GameFlowEventBatch& game_flow_batch);
void apply_delta(WorldSnapshot& baseline, const WorldSnapshot& delta);
SimEventBatch drain_sim_events(SimEventLog& log);

} // namespace og::sim
