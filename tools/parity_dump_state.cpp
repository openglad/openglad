#include "parity_dump_state.h"

#include "parity_event_log.h"

#include "screen.h"
#include "stats.h"
#include "walker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <list>
#include <string_view>
#include <utility>

namespace og::parity {
namespace {

std::string format_float(float value)
{
    if (value == 0.0f) value = 0.0f;
    if (std::isnan(value) || std::isinf(value)) value = 0.0f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(value));
    if (buf[0] == '-' && std::strcmp(buf + 1, "0.000000") == 0)
        return "0.000000";
    return std::string(buf);
}

std::string format_hex32(std::uint32_t value)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", value);
    return std::string(buf);
}

void append_escaped_string(std::string& out, std::string_view s)
{
    out.push_back('"');
    for (char c : s)
    {
        switch (c)
        {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n");  break;
            case '\r': out.append("\\r");  break;
            case '\t': out.append("\\t");  break;
            case '\b': out.append("\\b");  break;
            case '\f': out.append("\\f");  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char esc[8];
                    std::snprintf(esc, sizeof(esc), "\\u%04X",
                                  static_cast<unsigned char>(c));
                    out.append(esc);
                }
                else
                {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

void append_uint(std::string& out, std::uint64_t value)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    out.append(buf);
}

void append_int(std::string& out, std::int64_t value)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
    out.append(buf);
}

void append_bool(std::string& out, bool value)
{
    out.append(value ? "true" : "false");
}

constexpr std::string_view kLivingFamilies[] = {
    "FAMILY_SOLDIER", "FAMILY_ELF", "FAMILY_ARCHER", "FAMILY_MAGE",
    "FAMILY_SKELETON", "FAMILY_CLERIC", "FAMILY_FIREELEMENTAL",
    "FAMILY_FAERIE", "FAMILY_SLIME", "FAMILY_SMALL_SLIME",
    "FAMILY_MEDIUM_SLIME", "FAMILY_THIEF", "FAMILY_GHOST",
    "FAMILY_DRUID", "FAMILY_ORC", "FAMILY_BIG_ORC", "FAMILY_BARBARIAN",
    "FAMILY_ARCHMAGE", "FAMILY_GOLEM", "FAMILY_GIANT_SKELETON",
    "FAMILY_TOWER1",
};

constexpr std::string_view kWeaponFamilies[] = {
    "FAMILY_KNIFE", "FAMILY_ROCK", "FAMILY_ARROW", "FAMILY_FIREBALL",
    "FAMILY_TREE", "FAMILY_METEOR", "FAMILY_SPRINKLE", "FAMILY_BONE",
    "FAMILY_BLOOD", "FAMILY_BLOB", "FAMILY_FIRE_ARROW",
    "FAMILY_LIGHTNING", "FAMILY_GLOW", "FAMILY_WAVE", "FAMILY_WAVE2",
    "FAMILY_WAVE3", "FAMILY_CIRCLE_PROTECTION", "FAMILY_HAMMER",
    "FAMILY_DOOR", "FAMILY_BOULDER",
};

constexpr std::string_view kTreasureFamilies[] = {
    "FAMILY_STAIN", "FAMILY_DRUMSTICK", "FAMILY_GOLD_BAR",
    "FAMILY_SILVER_BAR", "FAMILY_MAGIC_POTION", "FAMILY_INVIS_POTION",
    "FAMILY_INVULNERABLE_POTION", "FAMILY_FLIGHT_POTION", "FAMILY_EXIT",
    "FAMILY_TELEPORTER", "FAMILY_LIFE_GEM", "FAMILY_KEY",
    "FAMILY_SPEED_POTION",
};

constexpr std::string_view kGeneratorFamilies[] = {
    "FAMILY_TENT", "FAMILY_TOWER", "FAMILY_BONES", "FAMILY_TREEHOUSE",
};

constexpr std::string_view kEffectFamilies[] = {
    "FAMILY_EXPAND", "FAMILY_GHOST_SCARE", "FAMILY_BOMB",
    "FAMILY_EXPLOSION", "FAMILY_FLASH", "FAMILY_MAGIC_SHIELD",
    "FAMILY_KNIFE_BACK", "FAMILY_BOOMERANG", "FAMILY_CLOUD",
    "FAMILY_MARKER", "FAMILY_CHAIN", "FAMILY_DOOR_OPEN", "FAMILY_HIT",
};

template <typename Entry>
void sort_by_family_id(std::vector<Entry>& entries)
{
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) {
                  if (a.family != b.family) return a.family < b.family;
                  return a.id < b.id;
              });
}

void collect_walkers(const std::list<walker*>& list,
                     std::vector<WalkerEntry>& out,
                     std::uint32_t& running_seq)
{
    for (walker* w : list)
    {
        if (w == nullptr) continue;
        WalkerEntry entry;
        entry.id     = ++running_seq;
        entry.family = family_symbol_by_order(
            static_cast<std::int32_t>(w->query_order()),
            static_cast<std::int32_t>(w->query_family()));
        entry.team = static_cast<std::uint32_t>(w->team_num);
        entry.xpos = static_cast<std::int32_t>(w->xpos);
        entry.ypos = static_cast<std::int32_t>(w->ypos);
        if (w->stats != nullptr)
        {
            entry.hp     = w->stats->hitpoints;
            entry.max_hp = w->stats->max_hitpoints;
        }
        entry.weapons_left = static_cast<std::int32_t>(w->weapons_left);
        entry.alive        = w->dead == 0;
        out.push_back(std::move(entry));
    }
}

void collect_effects(const std::list<walker*>& list,
                     std::vector<EffectEntry>& out,
                     std::uint32_t& running_seq)
{
    for (walker* w : list)
    {
        if (w == nullptr) continue;
        EffectEntry entry;
        entry.id       = ++running_seq;
        entry.family   = family_symbol_by_order(
            static_cast<std::int32_t>(w->query_order()),
            static_cast<std::int32_t>(w->query_family()));
        entry.xpos     = static_cast<std::int32_t>(w->xpos);
        entry.ypos     = static_cast<std::int32_t>(w->ypos);
        entry.lifetime = static_cast<std::int32_t>(w->lifetime);
        out.push_back(std::move(entry));
    }
}

void collect_weapons(const std::list<walker*>& list,
                     std::vector<WeaponEntry>& out,
                     std::uint32_t& running_seq)
{
    for (walker* w : list)
    {
        if (w == nullptr) continue;
        WeaponEntry entry;
        entry.id       = ++running_seq;
        entry.family   = family_symbol_by_order(
            static_cast<std::int32_t>(w->query_order()),
            static_cast<std::int32_t>(w->query_family()));
        entry.team     = static_cast<std::uint32_t>(w->team_num);
        entry.xpos     = static_cast<std::int32_t>(w->xpos);
        entry.ypos     = static_cast<std::int32_t>(w->ypos);
        entry.lifetime = static_cast<std::int32_t>(w->lifetime);
        out.push_back(std::move(entry));
    }
}

} // namespace

std::string family_symbol_by_order(std::int32_t order, std::int32_t family_id)
{
    const std::string_view* table = nullptr;
    std::size_t size = 0;
    switch (order)
    {
        case ORDER_LIVING:
            table = kLivingFamilies;
            size = sizeof(kLivingFamilies) / sizeof(kLivingFamilies[0]);
            break;
        case ORDER_WEAPON:
            table = kWeaponFamilies;
            size = sizeof(kWeaponFamilies) / sizeof(kWeaponFamilies[0]);
            break;
        case ORDER_TREASURE:
            table = kTreasureFamilies;
            size = sizeof(kTreasureFamilies) / sizeof(kTreasureFamilies[0]);
            break;
        case ORDER_GENERATOR:
            table = kGeneratorFamilies;
            size = sizeof(kGeneratorFamilies) / sizeof(kGeneratorFamilies[0]);
            break;
        case ORDER_FX:
            table = kEffectFamilies;
            size = sizeof(kEffectFamilies) / sizeof(kEffectFamilies[0]);
            break;
        default:
            break;
    }
    if (table != nullptr && family_id >= 0 &&
        static_cast<std::size_t>(family_id) < size)
        return std::string(table[family_id]);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "FAMILY_UNKNOWN_%d_%d", order, family_id);
    return std::string(buf);
}

std::string event_kind_symbol(std::uint32_t kind_raw)
{
    switch (kind_raw)
    {
        case kEventNone:                    return "none";
        case kEventPlaySound:               return "play_sound";
        case kEventNotification:            return "notification";
        case kEventSetPalette:              return "set_palette";
        case kEventRequestRedraw:           return "request_redraw";
        case kEventEndGame:                 return "end_game";
        case kEventSetEnd:                  return "set_end";
        case kEventRequestExitConfirmation: return "request_exit_confirmation";
        case kEventWithdrawToLevel:         return "withdraw_to_level";
        case kEventScoreChange:             return "score_change";
        case kEventDamageTile:              return "damage_tile";
        default:
        {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "kind_%u", kind_raw);
            return std::string(buf);
        }
    }
}

StateDump capture_state_dump(const screen& game,
                             std::uint32_t tick,
                             std::vector<WeaponTrackSample> weapon_tracks)
{
    StateDump dump;
    dump.weapon_tracks    = std::move(weapon_tracks);
    dump.tick             = tick;
    dump.level_tick_count = tick;
    dump.rng_state        = rng_observable_state();
    dump.level_done       = static_cast<std::int32_t>(game.level_done);

    for (std::size_t i = 0; i < 4; ++i)
        dump.score_per_team[i] = game.save_data.m_score[i];

    std::uint32_t fallback_id = 0;
    collect_walkers(game.level_data.oblist, dump.walkers, fallback_id);
    collect_effects(game.level_data.fxlist, dump.effects, fallback_id);
    collect_weapons(game.level_data.weaplist, dump.weapons, fallback_id);

    for (const auto& ev : event_log())
    {
        EventEntry entry;
        entry.kind     = event_kind_symbol(ev.kind);
        entry.tick     = ev.tick;
        entry.a        = ev.a;
        entry.b        = ev.b;
        entry.text     = ev.text;
        entry.sequence = ev.sequence;
        dump.events.push_back(std::move(entry));
    }

    std::sort(dump.walkers.begin(), dump.walkers.end(),
              [](const WalkerEntry& a, const WalkerEntry& b) {
                  if (a.team != b.team) return a.team < b.team;
                  return a.id < b.id;
              });
    sort_by_family_id(dump.effects);
    std::sort(dump.events.begin(), dump.events.end(),
              [](const EventEntry& a, const EventEntry& b) {
                  if (a.tick != b.tick) return a.tick < b.tick;
                  return a.sequence < b.sequence;
              });
    sort_by_family_id(dump.weapons);
    std::sort(dump.weapon_tracks.begin(), dump.weapon_tracks.end(),
              [](const WeaponTrackSample& a, const WeaponTrackSample& b) {
                  if (a.family != b.family) return a.family < b.family;
                  if (a.seq != b.seq) return a.seq < b.seq;
                  return a.tick < b.tick;
              });

    return dump;
}

std::string canonical_serialize(const StateDump& dump)
{
    std::string out;
    out.reserve(256 + dump.walkers.size() * 96 + dump.effects.size() * 64 +
                dump.events.size() * 48);

    out.append("{\"effects\":[");
    for (std::size_t i = 0; i < dump.effects.size(); ++i)
    {
        if (i != 0) out.push_back(',');
        const auto& e = dump.effects[i];
        out.append("{\"family\":");
        append_escaped_string(out, e.family);
        out.append(",\"id\":");
        append_uint(out, e.id);
        out.append(",\"xpos\":");
        append_int(out, e.xpos);
        out.append(",\"ypos\":");
        append_int(out, e.ypos);
        out.push_back('}');
    }
    out.append("],\"events\":[");
    for (std::size_t i = 0; i < dump.events.size(); ++i)
    {
        if (i != 0) out.push_back(',');
        const auto& ev = dump.events[i];
        out.append("{\"a\":");
        append_uint(out, ev.a);
        out.append(",\"b\":");
        append_uint(out, ev.b);
        out.append(",\"kind\":");
        append_escaped_string(out, ev.kind);
        out.append(",\"sequence\":");
        append_uint(out, ev.sequence);
        out.append(",\"text\":");
        append_escaped_string(out, ev.text);
        out.append(",\"tick\":");
        append_uint(out, ev.tick);
        out.push_back('}');
    }
    out.append("],\"level_done\":");
    append_int(out, dump.level_done);
    out.append(",\"level_tick_count\":");
    append_uint(out, dump.level_tick_count);
    out.append(",\"rng_state\":");
    if (dump.rng_observable)
        append_escaped_string(out, format_hex32(dump.rng_state));
    else
        append_escaped_string(out, std::string_view("unobservable"));
    out.append(",\"schema_version\":");
    append_escaped_string(out, dump.schema_version);
    out.append(",\"score_per_team\":[");
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (i != 0) out.push_back(',');
        append_uint(out, dump.score_per_team[i]);
    }
    out.append("],\"tick\":");
    append_uint(out, dump.tick);
    out.append(",\"walkers\":[");
    for (std::size_t i = 0; i < dump.walkers.size(); ++i)
    {
        if (i != 0) out.push_back(',');
        const auto& w = dump.walkers[i];
        out.append("{\"alive\":");
        append_bool(out, w.alive);
        out.append(",\"family\":");
        append_escaped_string(out, w.family);
        out.append(",\"hp\":");
        out.append(format_float(w.hp));
        out.append(",\"id\":");
        append_uint(out, w.id);
        out.append(",\"max_hp\":");
        out.append(format_float(w.max_hp));
        out.append(",\"team\":");
        append_uint(out, w.team);
        out.append(",\"weapons_left\":");
        append_int(out, w.weapons_left);
        out.append(",\"xpos\":");
        append_int(out, w.xpos);
        out.append(",\"ypos\":");
        append_int(out, w.ypos);
        out.push_back('}');
    }
    out.append("],\"weapon_tracks\":[");
    for (std::size_t i = 0; i < dump.weapon_tracks.size(); ++i)
    {
        if (i != 0) out.push_back(',');
        const auto& s = dump.weapon_tracks[i];
        out.append("{\"family\":");
        append_escaped_string(out, s.family);
        out.append(",\"seq\":");
        append_int(out, s.seq);
        out.append(",\"tick\":");
        append_uint(out, s.tick);
        out.append(",\"xpos\":");
        append_int(out, s.xpos);
        out.append(",\"ypos\":");
        append_int(out, s.ypos);
        out.push_back('}');
    }
    out.append("],\"weapons\":[");
    for (std::size_t i = 0; i < dump.weapons.size(); ++i)
    {
        if (i != 0) out.push_back(',');
        const auto& w = dump.weapons[i];
        out.append("{\"family\":");
        append_escaped_string(out, w.family);
        out.append(",\"id\":");
        append_uint(out, w.id);
        out.append(",\"team\":");
        append_uint(out, w.team);
        out.append(",\"xpos\":");
        append_int(out, w.xpos);
        out.append(",\"ypos\":");
        append_int(out, w.ypos);
        out.push_back('}');
    }
    out.append("]}\n");
    return out;
}

} // namespace og::parity
