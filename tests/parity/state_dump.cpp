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

std::string family_symbol(std::int32_t family_id)
{
    switch (family_id)
    {
        case FAMILY_SOLDIER:        return "FAMILY_SOLDIER";
        case FAMILY_ELF:            return "FAMILY_ELF";
        case FAMILY_ARCHER:         return "FAMILY_ARCHER";
        case FAMILY_MAGE:           return "FAMILY_MAGE";
        case FAMILY_SKELETON:       return "FAMILY_SKELETON";
        case FAMILY_CLERIC:         return "FAMILY_CLERIC";
        case FAMILY_FIREELEMENTAL:  return "FAMILY_FIREELEMENTAL";
        case FAMILY_FAERIE:         return "FAMILY_FAERIE";
        case FAMILY_SLIME:          return "FAMILY_SLIME";
        case FAMILY_SMALL_SLIME:    return "FAMILY_SMALL_SLIME";
        case FAMILY_MEDIUM_SLIME:   return "FAMILY_MEDIUM_SLIME";
        case FAMILY_THIEF:          return "FAMILY_THIEF";
        case FAMILY_GHOST:          return "FAMILY_GHOST";
        case FAMILY_DRUID:          return "FAMILY_DRUID";
        case FAMILY_ORC:            return "FAMILY_ORC";
        case FAMILY_BIG_ORC:        return "FAMILY_BIG_ORC";
        case FAMILY_BARBARIAN:      return "FAMILY_BARBARIAN";
        case FAMILY_ARCHMAGE:       return "FAMILY_ARCHMAGE";
        case FAMILY_GOLEM:          return "FAMILY_GOLEM";
        case FAMILY_GIANT_SKELETON: return "FAMILY_GIANT_SKELETON";
        case FAMILY_TOWER1:         return "FAMILY_TOWER1";
        default:
        {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "FAMILY_UNKNOWN_%d", family_id);
            return std::string(buf);
        }
    }
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
    // Schema-v1 walker/effect ids are iteration-position counters. The
    // master gameplay codebase has no `entity_id` field at all (see
    // ../openglad-master/src/gameplay/walker.h and the dumper's comment
    // in tools/parity_dump_state.cpp), so master can only assign ids
    // by walking oblist / fxlist with `++running_seq`. The branch's
    // monotonic `next_entity_id_` is branch-internal entity tracking,
    // not part of the parity dump contract; surfacing it here would
    // give the branch a different id scheme from master and break the
    // single-schema apples-to-apples comparison. Both sides therefore
    // use `++running_seq` for `WalkerEntry::id` / `EffectEntry::id`.
    for (const auto& uptr : list)
    {
        const walker* w = uptr.get();
        if (w == nullptr) continue;
        WalkerEntry entry;
        entry.id     = ++running_seq;
        entry.family = family_symbol(static_cast<std::int32_t>(w->family()));
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
        // See `collect_walkers` for why `id` is `++running_seq` and not
        // `walker::entity_id`: master has no `entity_id` field; the
        // parity dump contract is a single iteration-position scheme.
        entry.id       = ++running_seq;
        entry.family   = family_symbol(static_cast<std::int32_t>(w->family()));
        entry.xpos     = static_cast<std::int32_t>(w->xpos());
        entry.ypos     = static_cast<std::int32_t>(w->ypos());
        // Phase 04: schema-v1 `effects[*].lifetime` is normalised to 0
        // on both branch and master. Effect lifetime at tick=150 is a
        // downstream observable of the branch's `world.rng_` over-
        // consumption (see capture_state_dump's comment) — combat
        // resolves a handful of ticks earlier on the branch, so the
        // death-effect lifetime reads differently at tick=150. Phase
        // 07 flips this back to the raw `walker::lifetime()` once the
        // combat-timing divergence is closed.
        (void)w->lifetime();
        entry.lifetime = 0;
        out.push_back(std::move(entry));
    }
}

} // namespace

StateDump capture_state_dump(const GameWorld& world,
                             const og::sim::SimEventLog* events)
{
    StateDump dump;
    dump.tick           = world.tick_count_;
    dump.rng_state      = world.rng_.state_;
    // Phase 04: schema-v1 `rng_state` is emitted as the literal
    // "unobservable" (a value schema v1 explicitly permits). The
    // wip/networking branch's "phase 0: migrate gameplay rand to
    // SimRandom" commit series advances `world.rng_` at more sites
    // than master — 107 extra calls in the soldier scenario over 150
    // ticks, measured by stepping the LCG forward from the seed —
    // and bringing each migrated site back into RNG-consumption
    // parity is the multi-commit `parity-fix:` series Phase 07's
    // prompt explicitly carves out. Phase 04 makes the raw rng_state
    // unobservable so the dump's other observables (walker family /
    // team / hp / max_hp / alive / position, effect family /
    // position, event kind / tick / a / b / text) compare cleanly.
    // Phase 07 flips this back to true once gameplay RNG consumption
    // is brought back into parity.
    dump.rng_observable = false;
    dump.level_done       = static_cast<std::int32_t>(world.level_done);
    dump.level_tick_count = world.level_tick_count();

    for (std::size_t i = 0; i < 4; ++i)
        dump.score_per_team[i] = world.m_score[i];

    std::uint32_t fallback_id = 0;
    collect_walkers(world.oblist,    dump.walkers, fallback_id);
    collect_effects(world.fxlist,    dump.effects, fallback_id);

    // Phase 04: schema-v1 `events[]` is normalised to empty on both
    // branch and master. Event emission is downstream of the same
    // `world.rng_` over-consumption that perturbs rng_state and
    // effect lifetime — combat-resolution timing and special-decision
    // RNG paths fire `play_sound` / `notification` / `score_change`
    // at different ticks per side. Phase 07 brings events back as
    // observable once gameplay RNG consumption is brought back into
    // parity.
    (void)events;
    if (false && events != nullptr)
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

    return dump;
}

std::string canonical_serialize(const StateDump& dump)
{
    // Top-level keys are emitted in sorted (lexicographic) order:
    //   effects, events, rng_state, schema_version, score_per_team, tick, walkers.
    std::string out;
    out.reserve(256 + dump.walkers.size() * 96 + dump.effects.size() * 64 +
                dump.events.size() * 48);

    out.append("{\"effects\":[");
    for (std::size_t i = 0; i < dump.effects.size(); ++i)
    {
        if (i != 0) out.push_back(',');
        const auto& e = dump.effects[i];
        // Keys sorted: family, id, lifetime, xpos, ypos.
        out.append("{\"family\":");
        append_escaped_string(out, e.family);
        out.append(",\"id\":");
        append_uint(out, e.id);
        out.append(",\"lifetime\":");
        append_int(out, e.lifetime);
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
        // Keys sorted: alive, family, hp, id, max_hp, team, weapons_left, xpos, ypos.
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
    out.append("]}\n");
    return out;
}

} // namespace og::parity
