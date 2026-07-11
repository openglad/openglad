#include "state_dump.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace og::parity {

namespace {

// "%.6f" but with negative-zero normalised to positive zero so canonical
// output is sign-stable across IEEE-754 quirks.
std::string format_float(float value)
{
    if (value == 0.0f)
        value = 0.0f; // normalises both +0 and -0 to bit pattern 0.0
    if (std::isnan(value) || std::isinf(value))
        value = 0.0f; // schema v1 rejects non-finite floats
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(value));
    // Replace "-0.000000" if it slipped through (e.g. very small negative).
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

} // namespace

// Per-Order family symbol tables. Schema-v1 dumped a single Living-only
// table for every walker regardless of Order, which aliased treasure /
// weapon / generator / effect entries to the wrong human-readable name
// (e.g. a Generator FAMILY_TENT (id 0) dumped as "FAMILY_SOLDIER"). The
// new resolution dispatches on Order so the dumped `family` string
// matches the entity's true family within its Order's namespace.
//
// Out-of-range ids or Orders without a dedicated table fall through to
// the "FAMILY_UNKNOWN_<order>_<id>" form so the dumper stays total.
namespace {

constexpr std::string_view kLivingFamilies[] = {
    "FAMILY_SOLDIER",
    "FAMILY_ELF",
    "FAMILY_ARCHER",
    "FAMILY_MAGE",
    "FAMILY_SKELETON",
    "FAMILY_CLERIC",
    "FAMILY_FIREELEMENTAL",
    "FAMILY_FAERIE",
    "FAMILY_SLIME",
    "FAMILY_SMALL_SLIME",
    "FAMILY_MEDIUM_SLIME",
    "FAMILY_THIEF",
    "FAMILY_GHOST",
    "FAMILY_DRUID",
    "FAMILY_ORC",
    "FAMILY_BIG_ORC",
    "FAMILY_BARBARIAN",
    "FAMILY_ARCHMAGE",
    "FAMILY_GOLEM",
    "FAMILY_GIANT_SKELETON",
    "FAMILY_TOWER1",
};

constexpr std::string_view kWeaponFamilies[] = {
    "FAMILY_KNIFE",
    "FAMILY_ROCK",
    "FAMILY_ARROW",
    "FAMILY_FIREBALL",
    "FAMILY_TREE",
    "FAMILY_METEOR",
    "FAMILY_SPRINKLE",
    "FAMILY_BONE",
    "FAMILY_BLOOD",
    "FAMILY_BLOB",
    "FAMILY_FIRE_ARROW",
    "FAMILY_LIGHTNING",
    "FAMILY_GLOW",
    "FAMILY_WAVE",
    "FAMILY_WAVE2",
    "FAMILY_WAVE3",
    "FAMILY_CIRCLE_PROTECTION",
    "FAMILY_HAMMER",
    "FAMILY_DOOR",
    "FAMILY_BOULDER",
};

constexpr std::string_view kTreasureFamilies[] = {
    "FAMILY_STAIN",
    "FAMILY_DRUMSTICK",
    "FAMILY_GOLD_BAR",
    "FAMILY_SILVER_BAR",
    "FAMILY_MAGIC_POTION",
    "FAMILY_INVIS_POTION",
    "FAMILY_INVULNERABLE_POTION",
    "FAMILY_FLIGHT_POTION",
    "FAMILY_EXIT",
    "FAMILY_TELEPORTER",
    "FAMILY_LIFE_GEM",
    "FAMILY_KEY",
    "FAMILY_SPEED_POTION",
};

constexpr std::string_view kGeneratorFamilies[] = {
    "FAMILY_TENT",
    "FAMILY_TOWER",
    "FAMILY_BONES",
    "FAMILY_TREEHOUSE",
};

constexpr std::string_view kEffectFamilies[] = {
    "FAMILY_EXPAND",
    "FAMILY_GHOST_SCARE",
    "FAMILY_BOMB",
    "FAMILY_EXPLOSION",
    "FAMILY_FLASH",
    "FAMILY_MAGIC_SHIELD",
    "FAMILY_KNIFE_BACK",
    "FAMILY_BOOMERANG",
    "FAMILY_CLOUD",
    "FAMILY_MARKER",
    "FAMILY_CHAIN",
    "FAMILY_DOOR_OPEN",
    "FAMILY_HIT",
};

} // namespace

std::string family_symbol_by_order(std::int32_t order, std::int32_t family_id)
{
    const std::string_view* table = nullptr;
    std::size_t             size  = 0;
    switch (static_cast<Order>(order))
    {
        case Order::Living:
            table = kLivingFamilies;
            size  = sizeof(kLivingFamilies) / sizeof(kLivingFamilies[0]);
            break;
        case Order::Weapon:
            table = kWeaponFamilies;
            size  = sizeof(kWeaponFamilies) / sizeof(kWeaponFamilies[0]);
            break;
        case Order::Treasure:
            table = kTreasureFamilies;
            size  = sizeof(kTreasureFamilies) / sizeof(kTreasureFamilies[0]);
            break;
        case Order::Generator:
            table = kGeneratorFamilies;
            size  = sizeof(kGeneratorFamilies) / sizeof(kGeneratorFamilies[0]);
            break;
        case Order::FX:
            table = kEffectFamilies;
            size  = sizeof(kEffectFamilies) / sizeof(kEffectFamilies[0]);
            break;
        default: break;
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
    using og::sim::EventKind;
    switch (static_cast<EventKind>(kind_raw))
    {
        case EventKind::None:                     return "none";
        case EventKind::PlaySound:                return "play_sound";
        case EventKind::Notification:             return "notification";
        case EventKind::SetPalette:               return "set_palette";
        case EventKind::RequestRedraw:            return "request_redraw";
        case EventKind::EndGame:                  return "end_game";
        case EventKind::DamageTile:               return "damage_tile";
        case EventKind::SetEnd:                   return "set_end";
        case EventKind::RequestExitConfirmation:  return "request_exit_confirmation";
        case EventKind::WithdrawToLevel:          return "withdraw_to_level";
        case EventKind::ScoreChange:              return "score_change";
        default:
        {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "kind_%u", kind_raw);
            return std::string(buf);
        }
    }
}

namespace {

void collect_walkers(const GameWorld::EntityList& list,
                     std::vector<WalkerEntry>& out,
                     std::uint32_t& running_seq)
{
    for (const auto& uptr : list)
    {
        const walker* w = uptr.get();
        if (w == nullptr) continue;
        WalkerEntry entry;
        // Parity id convention: pure list-iteration order (++running_seq),
        // identical to the master companion dumper
        // (../openglad-master/tools/parity_dump_state.cpp). Master has no
        // entity_id; the branch's real spawn-order entity_id() permutes the
        // (team,id)-sorted array relative to master and manufactures
        // pseudo-diffs (family/hp/xpos) over an otherwise-identical entity set.
        // Because add_to_list insertion order is byte-identical on both arms,
        // ++running_seq yields identical ids and eliminates the id-swap noise.
        entry.id     = ++running_seq;
        entry.family = family_symbol_by_order(
            static_cast<std::int32_t>(w->query_order()),
            static_cast<std::int32_t>(w->family()));
        entry.team   = static_cast<std::uint32_t>(w->team_num());
        entry.xpos   = static_cast<std::int32_t>(w->xpos());
        entry.ypos   = static_cast<std::int32_t>(w->ypos());
        if (w->stats() != nullptr)
        {
            entry.hp     = w->stats()->hitpoints();
            entry.max_hp = w->stats()->max_hitpoints();
        }
        entry.weapons_left = static_cast<std::int32_t>(w->weapons_left());
        entry.alive        = w->dead() == 0;
        entry.floor        = static_cast<std::int32_t>(w->floor());
        out.push_back(std::move(entry));
    }
}

void collect_effects(const GameWorld::EntityList& list,
                     std::vector<EffectEntry>& out,
                     std::uint32_t& running_seq)
{
    for (const auto& uptr : list)
    {
        const walker* w = uptr.get();
        if (w == nullptr) continue;
        EffectEntry entry;
        // Pure list-iteration id, matching the master companion dumper.
        entry.id       = ++running_seq;
        entry.family   = family_symbol_by_order(
            static_cast<std::int32_t>(w->query_order()),
            static_cast<std::int32_t>(w->family()));
        entry.xpos     = static_cast<std::int32_t>(w->xpos());
        entry.ypos     = static_cast<std::int32_t>(w->ypos());
        entry.lifetime = static_cast<std::int32_t>(w->lifetime());
        out.push_back(std::move(entry));
    }
}

// Phase 04 — weapon-order entity collector. The schema-v1 producer left
// `dump.weapons[]` empty (header comment: "Phase 04 wires the producer +
// serialiser together"). Phase 04 wires it here so WeaponFamilyEmitted
// predicates evaluate on real weaplist entries.
void collect_weapons(const GameWorld::EntityList& list,
                     std::vector<WeaponEntry>& out,
                     std::uint32_t& running_seq)
{
    for (const auto& uptr : list)
    {
        const walker* w = uptr.get();
        if (w == nullptr) continue;
        WeaponEntry entry;
        // Pure list-iteration id, matching the master companion dumper.
        entry.id       = ++running_seq;
        entry.family   = family_symbol_by_order(
            static_cast<std::int32_t>(w->query_order()),
            static_cast<std::int32_t>(w->family()));
        entry.team     = static_cast<std::uint32_t>(w->team_num());
        entry.xpos     = static_cast<std::int32_t>(w->xpos());
        entry.ypos     = static_cast<std::int32_t>(w->ypos());
        entry.lifetime = static_cast<std::int32_t>(w->lifetime());
        out.push_back(std::move(entry));
    }
}

} // namespace

StateDump capture_state_dump(const GameWorld& world,
                             const og::sim::SimEventLog* events,
                             std::vector<WeaponTrackSample> weapon_tracks)
{
    StateDump dump;
    dump.weapon_tracks  = std::move(weapon_tracks);
    dump.tick           = world.tick_count_;
    dump.rng_state      = world.rng_.state_;
    dump.rng_observable = true;
    dump.level_done       = static_cast<std::int32_t>(world.level_done);
    dump.level_tick_count = world.level_tick_count();

    for (std::size_t i = 0; i < 4; ++i)
        dump.score_per_team[i] = world.m_score[i];

    std::uint32_t fallback_id = 0;
    collect_walkers(world.oblist,    dump.walkers, fallback_id);
    collect_effects(world.fxlist,    dump.effects, fallback_id);
    collect_weapons(world.weaplist,  dump.weapons, fallback_id);

    if (events != nullptr)
    {
        std::uint32_t sequence = 0;
        for (const auto& ev : events->events())
        {
            EventEntry entry;
            entry.kind     = event_kind_symbol(static_cast<std::uint32_t>(ev.kind));
            entry.tick     = ev.tick;
            entry.a        = ev.a;
            entry.b        = ev.b;
            entry.text     = ev.text;
            entry.sequence = sequence++;
            dump.events.push_back(std::move(entry));
        }
    }

    // Canonical orderings (stable across runs and host platforms).
    std::sort(dump.walkers.begin(), dump.walkers.end(),
              [](const WalkerEntry& a, const WalkerEntry& b) {
                  if (a.team != b.team) return a.team < b.team;
                  return a.id < b.id;
              });
    std::sort(dump.effects.begin(), dump.effects.end(),
              [](const EffectEntry& a, const EffectEntry& b) {
                  if (a.family != b.family) return a.family < b.family;
                  return a.id < b.id;
              });
    std::sort(dump.events.begin(), dump.events.end(),
              [](const EventEntry& a, const EventEntry& b) {
                  if (a.tick != b.tick) return a.tick < b.tick;
                  return a.sequence < b.sequence;
              });
    std::sort(dump.weapons.begin(), dump.weapons.end(),
              [](const WeaponEntry& a, const WeaponEntry& b) {
                  if (a.family != b.family) return a.family < b.family;
                  return a.id < b.id;
              });
    // Canonical, stable, identical-on-both-arms order: (family, seq, tick).
    std::sort(dump.weapon_tracks.begin(), dump.weapon_tracks.end(),
              [](const WeaponTrackSample& a, const WeaponTrackSample& b) {
                  if (a.family != b.family) return a.family < b.family;
                  if (a.seq != b.seq)       return a.seq < b.seq;
                  return a.tick < b.tick;
              });

    return dump;
}

std::string canonical_serialize(const StateDump& dump)
{
    // Top-level keys are emitted in sorted (lexicographic) order:
    //   effects, events, level_done, level_tick_count, rng_state,
    //   schema_version, score_per_team, tick, walkers, weapon_tracks, weapons.
    // ("weapon_tracks" sorts after "walkers" and before "weapons": '_' 0x5F
    //  < 's' 0x73.)
    std::string out;
    out.reserve(256 + dump.walkers.size() * 96 + dump.effects.size() * 64 +
                dump.events.size() * 48);

    out.append("{\"effects\":[");
    for (std::size_t i = 0; i < dump.effects.size(); ++i)
    {
        if (i != 0) out.push_back(',');
        const auto& e = dump.effects[i];
        // Keys sorted: family, id, xpos, ypos.
        // `lifetime` is intentionally omitted: walker::lifetime is read off
        // effect-order entities that never assign it, so on master the member
        // is uninitialised (UB garbage) while the branch zero-initialises it.
        // No fact predicate consumes effect lifetime (every EffectFamilyCount
        // row uses window_marker=0, disabling the lifetime filter), so the
        // field is dropped from the schema on both arms rather than baking
        // master's UB into the golden.
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
        // Keys sorted: a, b, kind, sequence, text, tick.
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
        // Keys sorted: alive, family, [floor,] hp, id, max_hp, team,
        // weapons_left, xpos, ypos. `floor` is emitted ONLY when non-zero so
        // single-floor dumps stay byte-identical with pre-Z goldens.
        // ("family" < "floor": 'a' < 'l'; "floor" < "hp": 'f' < 'h'.)
        out.append("{\"alive\":");
        append_bool(out, w.alive);
        out.append(",\"family\":");
        append_escaped_string(out, w.family);
        if (w.floor != 0)
        {
            out.append(",\"floor\":");
            append_int(out, w.floor);
        }
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
        // Keys sorted: family, seq, tick, xpos, ypos.
        // NOTE: weapon_tracks intentionally omits the per-sample `lifetime`
        // field. The underlying walker::lifetime member is read directly off
        // weapon-order entities that never assign it (HIT, melee KNIFE, ...);
        // on master that member is *uninitialised* (walker.h declares it with
        // no default and the ctor never sets it), so the sampled value is
        // undefined-behaviour garbage (e.g. 1434172392) and is not a faithful
        // gameplay signal. The branch zero-initialises the field, so the two
        // can never agree by construction. No fact predicate consumes the
        // track lifetime, so it is dropped from the schema on both arms rather
        // than baking master's UB into the golden.
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
        // Keys sorted: family, id, team, xpos, ypos.
        // `lifetime` is intentionally omitted: walker::lifetime is read off
        // weapon-order entities that never assign it (HIT, melee KNIFE, ...),
        // so on master the member is uninitialised (UB garbage) while the
        // branch zero-initialises it. No fact predicate consumes weapon
        // lifetime, so the field is dropped from the schema on both arms
        // rather than baking master's UB into the golden.
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
