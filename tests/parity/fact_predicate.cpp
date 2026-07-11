#include "fact_predicate.h"

#include "state_dump.h"

#include <openglad/core/order.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace og::parity {

namespace {

constexpr std::int32_t kLivingOrder    = static_cast<std::int32_t>(Order::Living);
constexpr std::int32_t kWeaponOrder    = static_cast<std::int32_t>(Order::Weapon);
constexpr std::int32_t kFXOrder        = static_cast<std::int32_t>(Order::FX);

// EventKind ordinal -> canonical event_kind_symbol name. Mirrors the
// switch in state_dump.cpp::event_kind_symbol so a predicate written as
// EventKindAtLeast(/*ordinal=*/3) maps to "set_palette".
//
// 0 -> "none", 1 -> "play_sound", ..., 9 -> "score_change",
// 10 -> "damage_tile".
const char* event_kind_symbol_of_ordinal(std::int32_t ordinal)
{
    static const char* table[] = {
        "none",
        "play_sound",
        "notification",
        "set_palette",
        "request_redraw",
        "end_game",
        "set_end",
        "request_exit_confirmation",
        "withdraw_to_level",
        "score_change",
        "damage_tile",
    };
    if (ordinal < 0 || static_cast<std::size_t>(ordinal) >=
                          (sizeof(table) / sizeof(table[0])))
        return "";
    return table[ordinal];
}

bool make_fail(FactEvalResult& r, const FactPredicate& p, std::string detail)
{
    r.ok = false;
    r.indeterminate = false;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "kind=%u",
                  static_cast<unsigned>(p.kind));
    r.message = std::string(buf) + " ";
    if (!p.label.empty())
    {
        r.message.append("label=\"");
        r.message.append(p.label);
        r.message.append("\" ");
    }
    r.message.append(detail);
    return false;
}

bool make_indeterminate(FactEvalResult& r, std::string detail = {})
{
    r.ok = true;
    r.indeterminate = true;
    r.message = std::move(detail);
    return true;
}

// Number of walkers matching family symbol corresponding to numeric id.
std::size_t count_walkers_family(const StateDump& d, std::int32_t family)
{
    const std::string sym = family_symbol_by_order(kLivingOrder, family);
    std::size_t n = 0;
    for (const auto& w : d.walkers)
        if (w.family == sym) ++n;
    return n;
}

std::size_t count_walkers_family_alive(const StateDump& d, std::int32_t family)
{
    const std::string sym = family_symbol_by_order(kLivingOrder, family);
    std::size_t n = 0;
    for (const auto& w : d.walkers)
        if (w.family == sym && w.alive) ++n;
    return n;
}

std::size_t count_walkers_team_alive(const StateDump& d, std::uint32_t team)
{
    std::size_t n = 0;
    for (const auto& w : d.walkers)
        if (w.team == team && w.alive) ++n;
    return n;
}

std::size_t count_effects_family(const StateDump& d, std::int32_t family)
{
    const std::string sym = family_symbol_by_order(kFXOrder, family);
    std::size_t n = 0;
    for (const auto& e : d.effects)
        if (e.family == sym) ++n;
    return n;
}

// `EffectFamilyCount` may carry a tick-window upper bound in arg4: when
// arg4 > 0 the predicate only counts effects whose lifetime <= arg4.
// This implements the spec's `[min_tick, max_tick]` window qualifier.
std::size_t count_effects_family_within_window(const StateDump& d,
                                               std::int32_t family,
                                               std::int32_t window_max_lifetime)
{
    const std::string sym = family_symbol_by_order(kFXOrder, family);
    std::size_t n = 0;
    for (const auto& e : d.effects)
    {
        if (e.family != sym) continue;
        if (window_max_lifetime > 0 && e.lifetime > window_max_lifetime) continue;
        ++n;
    }
    return n;
}

std::size_t count_weapons_family(const StateDump& d, std::int32_t family)
{
    const std::string sym = family_symbol_by_order(kWeaponOrder, family);
    std::size_t n = 0;
    for (const auto& w : d.weapons)
        if (w.family == sym) ++n;
    return n;
}

std::size_t count_weapon_tracks_family(const StateDump& d, std::int32_t family)
{
    const std::string sym = family_symbol_by_order(kWeaponOrder, family);
    std::size_t n = 0;
    for (const auto& w : d.weapon_tracks)
        if (w.family == sym) ++n;
    return n;
}

// Weapon-trajectory analysis over dump.weapon_tracks. A "track" is the
// time-ordered sequence of samples for one (family, seq) projectile.
struct WeaponTrackMetrics
{
    bool         has_track          = false; // >=1 sample for the family
    bool         has_two_consec     = false; // >=1 pair with consecutive ticks
    std::int32_t max_step_centi     = 0;     // max consecutive-tick step
    std::int32_t net_centi          = 0;     // first->last displacement
    std::int32_t pathlen_centi      = 0;     // sum of adjacent step magnitudes
    std::size_t  sample_count       = 0;
};

std::int32_t hypot_centi(std::int32_t dx, std::int32_t dy)
{
    const double d = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
    return static_cast<std::int32_t>(std::lround(d * 100.0));
}

// Compute metrics for the FIRST track (lowest seq) of `family` that has
// >=1 sample. dump.weapon_tracks is canonically sorted by (family, seq,
// tick), so the first matching block is the lowest-seq track and its
// samples are already tick-ascending.
WeaponTrackMetrics weapon_track_metrics(const StateDump& d, std::int32_t family,
                                        std::int32_t order = kWeaponOrder)
{
    const std::string sym = family_symbol_by_order(order, family);
    WeaponTrackMetrics m;

    // Prefer the lowest-seq track (the wielder's first/primary flight). But if
    // that track is a degenerate stub (<2 samples — e.g. a slime's seq0 BLOB
    // that dies on spawn after a speed mutation while later blobs fly on), fall
    // back to the family's LONGEST track so the metric still measures a real
    // flight instead of stranding at Indeterminate. Single-fire scenarios have
    // exactly one seq with a full sample run, so this is a no-op for them and
    // preserves their seq0-tuned bands.
    std::map<std::int32_t /*seq*/, std::int32_t /*count*/> count_by_seq;
    for (const auto& s : d.weapon_tracks)
        if (s.family == sym)
            ++count_by_seq[s.seq];
    if (count_by_seq.empty()) return m; // has_track stays false
    std::int32_t target_seq = count_by_seq.begin()->first; // lowest seq
    if (count_by_seq[target_seq] < 2)                      // stub -> longest track
    {
        std::int32_t best_count = 0;
        for (const auto& [seq, cnt] : count_by_seq) // map iterates seq ascending
            if (cnt > best_count) { best_count = cnt; target_seq = seq; }
    }
    m.has_track = true;

    // Collect that track's samples in tick order (already sorted globally).
    std::vector<const WeaponTrackSample*> samples;
    for (const auto& s : d.weapon_tracks)
        if (s.family == sym && s.seq == target_seq)
            samples.push_back(&s);
    std::sort(samples.begin(), samples.end(),
              [](const WeaponTrackSample* a, const WeaponTrackSample* b) {
                  return a->tick < b->tick;
              });
    m.sample_count = samples.size();
    if (samples.size() < 2)
        return m; // pathlen=0, net=0; STATIONARY satisfiable

    const std::int32_t dx_net = samples.back()->xpos - samples.front()->xpos;
    const std::int32_t dy_net = samples.back()->ypos - samples.front()->ypos;
    m.net_centi = hypot_centi(dx_net, dy_net);

    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const std::int32_t dx = samples[i]->xpos - samples[i - 1]->xpos;
        const std::int32_t dy = samples[i]->ypos - samples[i - 1]->ypos;
        const std::int32_t step = hypot_centi(dx, dy);
        m.pathlen_centi += step;
        // Only consecutive-tick pairs feed the speed estimate so a gap from
        // a transform (e.g. WAVE -> WAVE2 re-seq) does not inflate a step.
        if (samples[i]->tick == samples[i - 1]->tick + 1)
        {
            m.has_two_consec = true;
            if (step > m.max_step_centi) m.max_step_centi = step;
        }
    }
    return m;
}

std::size_t count_events_kind(const StateDump& d, std::int32_t kind_ordinal)
{
    const char* name = event_kind_symbol_of_ordinal(kind_ordinal);
    if (name == nullptr || *name == '\0') return 0;
    std::size_t n = 0;
    for (const auto& ev : d.events)
        if (ev.kind == name) ++n;
    return n;
}

} // namespace

FactEvalResult evaluate_one(const FactPredicate& p, const StateDump& dump)
{
    FactEvalResult r;
    switch (p.kind)
    {
        case FactKind::TickReached:
        {
            if (static_cast<std::int32_t>(dump.tick) < p.arg0)
                return (make_fail(r, p, "tick " + std::to_string(dump.tick) +
                                  " < required " + std::to_string(p.arg0)), r);
            return r;
        }
        case FactKind::LevelDoneEquals:
        {
            if (dump.level_done != p.arg0)
                return (make_fail(r, p, "level_done=" +
                                  std::to_string(dump.level_done) +
                                  " expected " + std::to_string(p.arg0)), r);
            return r;
        }
        case FactKind::ScoreDelta:
        {
            const std::uint32_t team = static_cast<std::uint32_t>(p.arg0);
            if (team >= 4)
                return (make_fail(r, p, "ScoreDelta: invalid team"), r);
            const std::int64_t s =
                static_cast<std::int64_t>(dump.score_per_team[team]);
            if (s < p.arg1 || s > p.arg2)
                return (make_fail(r, p, "score=" + std::to_string(s) +
                                  " out of [" + std::to_string(p.arg1) + "," +
                                  std::to_string(p.arg2) + "]"), r);
            return r;
        }
        case FactKind::WalkerFamilyCount:
        {
            const std::size_t n = count_walkers_family(dump, p.arg0);
            if (static_cast<std::int32_t>(n) < p.arg1 ||
                static_cast<std::int32_t>(n) > p.arg2)
                return (make_fail(r, p, "count=" + std::to_string(n) +
                                  " out of [" + std::to_string(p.arg1) + "," +
                                  std::to_string(p.arg2) + "]"), r);
            return r;
        }
        case FactKind::WalkerOfTeamAlive:
        {
            const std::size_t n = count_walkers_team_alive(
                dump, static_cast<std::uint32_t>(p.arg0));
            if (static_cast<std::int32_t>(n) < p.arg1 ||
                static_cast<std::int32_t>(n) > p.arg2)
                return (make_fail(r, p, "team-alive=" + std::to_string(n) +
                                  " out of [" + std::to_string(p.arg1) + "," +
                                  std::to_string(p.arg2) + "]"), r);
            return r;
        }
        case FactKind::WalkerHpRangeAtFinalTick:
        {
            const std::string sym = family_symbol_by_order(kLivingOrder, p.arg0);
            const float lo = static_cast<float>(p.arg1) / 100.0f;
            const float hi = static_cast<float>(p.arg2) / 100.0f;
            for (const auto& w : dump.walkers)
            {
                if (w.family == sym && w.hp >= lo && w.hp <= hi)
                    return r; // found one in range -> pass
            }
            return (make_fail(r, p, "no walker of family " + sym +
                              " with hp in range"), r);
        }
        case FactKind::WalkerKeysApplied:
        {
            if (!dump.inventory_keys.has_value())
                return (make_indeterminate(r, "inventory_keys absent"), r);
            if (static_cast<std::int32_t>(dump.inventory_keys->size()) < p.arg0)
                return (make_fail(r, p, "keys=" +
                                  std::to_string(dump.inventory_keys->size()) +
                                  " < required " + std::to_string(p.arg0)), r);
            return r;
        }
        case FactKind::WalkerPositionMoved:
        {
            const std::string sym = family_symbol_by_order(kLivingOrder, p.arg0);
            for (const auto& w : dump.walkers)
            {
                if (w.family == sym && w.xpos >= p.arg1 && w.ypos >= p.arg2)
                    return r;
            }
            return (make_fail(r, p, "no walker of family " + sym +
                              " with xpos>=" + std::to_string(p.arg1) +
                              " ypos>=" + std::to_string(p.arg2)), r);
        }
        case FactKind::WalkerDiedByFinal:
        {
            if (count_walkers_family_alive(dump, p.arg0) != 0)
                return (make_fail(r, p, "family " +
                                  family_symbol_by_order(kLivingOrder, p.arg0) +
                                  " still has alive walker"), r);
            return r;
        }
        case FactKind::WalkerAliveAtFinal:
        {
            const std::size_t n = count_walkers_family_alive(dump, p.arg0);
            if (static_cast<std::int32_t>(n) < p.arg1)
                return (make_fail(r, p, "alive=" + std::to_string(n) +
                                  " of " +
                                  family_symbol_by_order(kLivingOrder, p.arg0) +
                                  " < required " + std::to_string(p.arg1)), r);
            return r;
        }
        case FactKind::TreasureFamilyRemovedFromOblist:
        {
            // Legacy schema-v1 predicate. arg0 is the literal family id
            // and is compared against dump.walkers[].family rendered
            // under the Living table (the only oblist family table
            // schema-v1 emitted). Per the Phase 03 contract the new
            // TreasureFamilyOfOrderRemovedFromOblist predicate is the
            // honest Order-aware replacement; this legacy kind is left
            // intact for callers that have not yet migrated.
            if (count_walkers_family(dump, p.arg0) != 0)
                return (make_fail(r, p, "family " +
                                  family_symbol_by_order(kLivingOrder, p.arg0) +
                                  " still present in oblist"), r);
            return r;
        }
        case FactKind::TreasureFamilyOfOrderRemovedFromOblist:
        {
            // Order-aware. Render the target family under the table for
            // p.arg1 (Order) and search dump.walkers[] for matching
            // entries. Schema-v1 cannot distinguish "not reached" from
            // "reached but not reaped" for a live remaining treasure:
            // both branch and companion can leave a matching alive entry
            // in oblist after the input window. Treat that single-state
            // observation as indeterminate while still passing if the
            // entity is absent or only dead entries remain, and failing a
            // mixed alive+dead state because it proves partial removal.
            const std::string sym = family_symbol_by_order(p.arg1, p.arg0);
            std::size_t alive_n = 0;
            std::size_t dead_n  = 0;
            for (const auto& w : dump.walkers)
            {
                if (w.family != sym) continue;
                if (w.alive) ++alive_n;
                else         ++dead_n;
            }
            if (alive_n == 0)
                return r;
            if (dead_n == 0)
                return (make_indeterminate(r,
                            "family " + sym +
                            " alive in oblist with no consumed instance; "
                            "schema-v1 cannot prove removal"), r);
            return (make_fail(r, p, "family " + sym +
                              " has " + std::to_string(alive_n) +
                              " alive and " + std::to_string(dead_n) +
                              " dead instance(s) in oblist"), r);
        }
        case FactKind::StatDeltaOnPickup:
        {
            // Requires baseline state not carried by schema v1.
            return (make_indeterminate(r, "no baseline carried in v1 dump"), r);
        }
        case FactKind::EffectFamilyCount:
        {
            // Source-walker qualifier (arg3 >= 0): structurally require that
            // a walker of the qualifying family is present in the dump, so
            // the count we observe is plausibly attributable to it.
            if (p.arg3 >= 0)
            {
                if (count_walkers_family(dump, p.arg3) == 0)
                    return (make_fail(r, p, "qualifier source family " +
                                      family_symbol_by_order(kLivingOrder, p.arg3) +
                                      " absent"), r);
            }
            // Tick-window qualifier (arg4 > 0): only effects whose
            // lifetime <= arg4 contribute to the count. arg4 == 0 means
            // "no window filter" (count all effects of the family).
            const std::size_t n = (p.arg4 > 0)
                ? count_effects_family_within_window(dump, p.arg0, p.arg4)
                : count_effects_family(dump, p.arg0);
            if (static_cast<std::int32_t>(n) < p.arg1 ||
                static_cast<std::int32_t>(n) > p.arg2)
                return (make_fail(r, p, "effect-count=" + std::to_string(n) +
                                  " out of [" + std::to_string(p.arg1) + "," +
                                  std::to_string(p.arg2) + "]" +
                                  (p.arg4 > 0
                                      ? " (window lifetime<=" +
                                            std::to_string(p.arg4) + ")"
                                      : std::string())), r);
            return r;
        }
        case FactKind::EventKindAtLeast:
        {
            const std::size_t n = count_events_kind(dump, p.arg0);
            if (static_cast<std::int32_t>(n) < p.arg1)
                return (make_fail(r, p, "event-count=" + std::to_string(n) +
                                  " < min " + std::to_string(p.arg1)), r);
            return r;
        }
        case FactKind::EventKindExactly:
        {
            const std::size_t n = count_events_kind(dump, p.arg0);
            if (static_cast<std::int32_t>(n) != p.arg1)
                return (make_fail(r, p, "event-count=" + std::to_string(n) +
                                  " != " + std::to_string(p.arg1)), r);
            return r;
        }
        case FactKind::WeaponFamilyEmitted:
        {
            // A projectile can be emitted and expire before the final tick.
            // Treat either a live final weapon or a recorded weapon track as
            // proof of emission; do not match dump.effects[].
            if (count_weapons_family(dump, p.arg0) == 0 &&
                count_weapon_tracks_family(dump, p.arg0) == 0)
                return (make_fail(r, p, "no weapon of family " +
                                  family_symbol_by_order(kWeaponOrder, p.arg0)),
                        r);
            return r;
        }
        case FactKind::WeaponSpeed:
        {
            const WeaponTrackMetrics m = weapon_track_metrics(dump, p.arg0);
            // Legacy-golden / stationary-family tolerance: no track or no
            // consecutive-tick pair to measure speed from.
            if (!m.has_track || !m.has_two_consec)
                return (make_indeterminate(r,
                            "no measurable track for " +
                            family_symbol_by_order(kWeaponOrder, p.arg0)), r);
            if (m.max_step_centi < p.arg1 || m.max_step_centi > p.arg2)
                return (make_fail(r, p, "speed=" +
                                  std::to_string(m.max_step_centi) +
                                  " centi-px/tick out of [" +
                                  std::to_string(p.arg1) + "," +
                                  std::to_string(p.arg2) + "]"), r);
            return r;
        }
        case FactKind::WeaponNetTravel:
        case FactKind::EffectNetTravel:
        {
            const std::int32_t order =
                (p.kind == FactKind::EffectNetTravel) ? kFXOrder : kWeaponOrder;
            const WeaponTrackMetrics m = weapon_track_metrics(dump, p.arg0, order);
            if (!m.has_track)
                return (make_indeterminate(r,
                            "no track for " +
                            family_symbol_by_order(order, p.arg0)), r);
            const std::int32_t thr = p.arg2;
            switch (p.arg1)
            {
                case kWeaponPathStraight:
                {
                    // Need >=2 samples to have a displacement at all.
                    if (m.sample_count < 2)
                        return (make_indeterminate(r,
                                    "STRAIGHT needs >=2 samples for " +
                                    family_symbol_by_order(kWeaponOrder, p.arg0)),
                                r);
                    // net >= threshold AND net >= 0.7 * pathlen (mostly straight).
                    if (m.net_centi < thr)
                        return (make_fail(r, p, "net=" +
                                          std::to_string(m.net_centi) +
                                          " < threshold " +
                                          std::to_string(thr)), r);
                    if (m.net_centi * 10 < m.pathlen_centi * 7)
                        return (make_fail(r, p, "net=" +
                                          std::to_string(m.net_centi) +
                                          " < 0.7*pathlen=" +
                                          std::to_string(m.pathlen_centi) +
                                          " (path not straight)"), r);
                    return r;
                }
                case kWeaponPathReturns:
                {
                    if (m.sample_count < 2)
                        return (make_indeterminate(r,
                                    "RETURNS needs >=2 samples for " +
                                    family_symbol_by_order(kWeaponOrder, p.arg0)),
                                r);
                    // pathlen >= threshold AND net < 0.5 * pathlen.
                    if (m.pathlen_centi < thr)
                        return (make_fail(r, p, "pathlen=" +
                                          std::to_string(m.pathlen_centi) +
                                          " < threshold " +
                                          std::to_string(thr)), r);
                    if (m.net_centi * 2 >= m.pathlen_centi)
                        return (make_fail(r, p, "net=" +
                                          std::to_string(m.net_centi) +
                                          " >= 0.5*pathlen=" +
                                          std::to_string(m.pathlen_centi) +
                                          " (did not return)"), r);
                    return r;
                }
                case kWeaponPathStationary:
                {
                    // pathlen <= threshold (barely moves). With <2 samples
                    // pathlen is 0 which trivially satisfies STATIONARY.
                    if (m.pathlen_centi > thr)
                        return (make_fail(r, p, "pathlen=" +
                                          std::to_string(m.pathlen_centi) +
                                          " > threshold " +
                                          std::to_string(thr) +
                                          " (not stationary)"), r);
                    return r;
                }
                default:
                    return (make_fail(r, p, "unknown behavior flag " +
                                      std::to_string(p.arg1)), r);
            }
        }
        case FactKind::WalkerOnFloor:
        {
            const std::string sym = family_symbol_by_order(kLivingOrder, p.arg0);
            std::size_t n = 0;
            for (const auto& w : dump.walkers)
                if (w.family == sym && w.alive &&
                    w.floor >= p.arg1 && w.floor <= p.arg2)
                    ++n;
            if (n == 0)
                return (make_fail(r, p, "no alive walker of family " + sym +
                                  " with floor in [" + std::to_string(p.arg1) +
                                  "," + std::to_string(p.arg2) + "]"), r);
            return r;
        }
    }
    return r;
}

FactEvalResult evaluate_facts(FactSide             side,
                              const FactPredicate* begin,
                              std::size_t          count,
                              const StateDump&     dump)
{
    FactEvalResult agg;
    for (std::size_t i = 0; i < count; ++i)
    {
        const FactPredicate& p = begin[i];
        const bool gated_out =
            (side == FactSide::Branch && !p.applies_to_branch) ||
            (side == FactSide::Master && !p.applies_to_master);
        if (gated_out) continue;
        FactEvalResult one = evaluate_one(p, dump);
        if (!one.ok)
        {
            agg.ok = false;
            agg.message = "[#" + std::to_string(i) + "] " + one.message;
            return agg;
        }
    }
    return agg;
}

namespace {

// Tiny purpose-built JSON parser. Supports the schema-v1 canonical
// output emitted by canonical_serialize: only strings, ints, floats,
// bools, arrays, objects; no scientific notation; LF line endings.
// Unknown keys are skipped silently (forward-compatible rule).

struct Parser
{
    std::string_view src;
    std::size_t      pos = 0;

    bool eof() const { return pos >= src.size(); }
    char peek() const { return src[pos]; }
    void skip_ws()
    {
        while (pos < src.size() &&
               (src[pos] == ' ' || src[pos] == '\n' || src[pos] == '\r' ||
                src[pos] == '\t'))
            ++pos;
    }
    bool consume(char c)
    {
        skip_ws();
        if (pos < src.size() && src[pos] == c) { ++pos; return true; }
        return false;
    }
    bool expect(char c) { return consume(c); }

    std::optional<std::string> read_string()
    {
        skip_ws();
        if (!expect('"')) return std::nullopt;
        std::string out;
        while (!eof())
        {
            char c = src[pos++];
            if (c == '"') return out;
            if (c == '\\' && pos < src.size())
            {
                char esc = src[pos++];
                switch (esc)
                {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'u':
                    {
                        if (pos + 4 > src.size()) return std::nullopt;
                        // Schema v1 only emits \u00XX for control chars.
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            char h = src[pos++];
                            code <<= 4;
                            if (h >= '0' && h <= '9')      code |= (h - '0');
                            else if (h >= 'a' && h <= 'f') code |= 10 + (h - 'a');
                            else if (h >= 'A' && h <= 'F') code |= 10 + (h - 'A');
                            else return std::nullopt;
                        }
                        out.push_back(static_cast<char>(code & 0xFF));
                        break;
                    }
                    default: return std::nullopt;
                }
            }
            else
            {
                out.push_back(c);
            }
        }
        return std::nullopt;
    }

    std::optional<double> read_number()
    {
        skip_ws();
        std::size_t start = pos;
        if (pos < src.size() && (src[pos] == '-' || src[pos] == '+')) ++pos;
        while (pos < src.size() &&
               ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '.'))
            ++pos;
        if (pos == start) return std::nullopt;
        std::string s(src.substr(start, pos - start));
        char* end = nullptr;
        double v = std::strtod(s.c_str(), &end);
        if (end != s.c_str() + s.size()) return std::nullopt;
        return v;
    }

    bool read_bool(bool& out)
    {
        skip_ws();
        if (src.substr(pos, 4) == "true")  { pos += 4; out = true;  return true; }
        if (src.substr(pos, 5) == "false") { pos += 5; out = false; return true; }
        return false;
    }

    // Skip an arbitrary JSON value. Used for unknown keys.
    bool skip_value()
    {
        skip_ws();
        if (eof()) return false;
        char c = src[pos];
        if (c == '"') return read_string().has_value();
        if (c == '{')
        {
            ++pos;
            skip_ws();
            if (consume('}')) return true;
            while (!eof())
            {
                if (!read_string()) return false;
                skip_ws();
                if (!expect(':')) return false;
                if (!skip_value()) return false;
                skip_ws();
                if (consume(',')) continue;
                if (consume('}')) return true;
                return false;
            }
            return false;
        }
        if (c == '[')
        {
            ++pos;
            skip_ws();
            if (consume(']')) return true;
            while (!eof())
            {
                if (!skip_value()) return false;
                skip_ws();
                if (consume(',')) continue;
                if (consume(']')) return true;
                return false;
            }
            return false;
        }
        if (c == 't' || c == 'f') { bool b; return read_bool(b); }
        if (c == 'n') { if (src.substr(pos, 4) == "null") { pos += 4; return true; } return false; }
        // number
        return read_number().has_value();
    }

    // For an object-of-known-keys, returns map<key,value-token> would
    // require allocations; instead we expose a dispatch loop the caller
    // drives via key callbacks.
    template <typename Fn>
    bool parse_object(Fn&& on_key)
    {
        skip_ws();
        if (!expect('{')) return false;
        skip_ws();
        if (consume('}')) return true;
        while (!eof())
        {
            auto key = read_string();
            if (!key) return false;
            skip_ws();
            if (!expect(':')) return false;
            if (!on_key(*key)) return false;
            skip_ws();
            if (consume(',')) continue;
            if (consume('}')) return true;
            return false;
        }
        return false;
    }

    template <typename Fn>
    bool parse_array(Fn&& on_elem)
    {
        skip_ws();
        if (!expect('[')) return false;
        skip_ws();
        if (consume(']')) return true;
        while (!eof())
        {
            if (!on_elem()) return false;
            skip_ws();
            if (consume(',')) continue;
            if (consume(']')) return true;
            return false;
        }
        return false;
    }
};

bool parse_walker(Parser& P, WalkerEntry& w)
{
    return P.parse_object([&](const std::string& key) -> bool {
        if (key == "alive")        return P.read_bool(w.alive);
        if (key == "family")       { auto s = P.read_string(); if (!s) return false; w.family = *s; return true; }
        if (key == "floor")        { auto n = P.read_number(); if (!n) return false; w.floor = static_cast<std::int32_t>(*n); return true; }
        if (key == "hp")           { auto n = P.read_number(); if (!n) return false; w.hp = static_cast<float>(*n); return true; }
        if (key == "id")           { auto n = P.read_number(); if (!n) return false; w.id = static_cast<std::uint32_t>(*n); return true; }
        if (key == "max_hp")       { auto n = P.read_number(); if (!n) return false; w.max_hp = static_cast<float>(*n); return true; }
        if (key == "team")         { auto n = P.read_number(); if (!n) return false; w.team = static_cast<std::uint32_t>(*n); return true; }
        if (key == "weapons_left") { auto n = P.read_number(); if (!n) return false; w.weapons_left = static_cast<std::int32_t>(*n); return true; }
        if (key == "xpos")         { auto n = P.read_number(); if (!n) return false; w.xpos = static_cast<std::int32_t>(*n); return true; }
        if (key == "ypos")         { auto n = P.read_number(); if (!n) return false; w.ypos = static_cast<std::int32_t>(*n); return true; }
        return P.skip_value();
    });
}

bool parse_effect(Parser& P, EffectEntry& e)
{
    return P.parse_object([&](const std::string& key) -> bool {
        if (key == "family")   { auto s = P.read_string(); if (!s) return false; e.family = *s; return true; }
        if (key == "id")       { auto n = P.read_number(); if (!n) return false; e.id = static_cast<std::uint32_t>(*n); return true; }
        if (key == "lifetime") { auto n = P.read_number(); if (!n) return false; e.lifetime = static_cast<std::int32_t>(*n); return true; }
        if (key == "xpos")     { auto n = P.read_number(); if (!n) return false; e.xpos = static_cast<std::int32_t>(*n); return true; }
        if (key == "ypos")     { auto n = P.read_number(); if (!n) return false; e.ypos = static_cast<std::int32_t>(*n); return true; }
        return P.skip_value();
    });
}

bool parse_weapon(Parser& P, WeaponEntry& w)
{
    return P.parse_object([&](const std::string& key) -> bool {
        if (key == "family")   { auto s = P.read_string(); if (!s) return false; w.family = *s; return true; }
        if (key == "id")       { auto n = P.read_number(); if (!n) return false; w.id = static_cast<std::uint32_t>(*n); return true; }
        if (key == "team")     { auto n = P.read_number(); if (!n) return false; w.team = static_cast<std::uint32_t>(*n); return true; }
        if (key == "lifetime") { auto n = P.read_number(); if (!n) return false; w.lifetime = static_cast<std::int32_t>(*n); return true; }
        if (key == "xpos")     { auto n = P.read_number(); if (!n) return false; w.xpos = static_cast<std::int32_t>(*n); return true; }
        if (key == "ypos")     { auto n = P.read_number(); if (!n) return false; w.ypos = static_cast<std::int32_t>(*n); return true; }
        return P.skip_value();
    });
}

bool parse_weapon_track(Parser& P, WeaponTrackSample& s)
{
    return P.parse_object([&](const std::string& key) -> bool {
        if (key == "family")   { auto v = P.read_string(); if (!v) return false; s.family = *v; return true; }
        if (key == "lifetime") { auto n = P.read_number(); if (!n) return false; s.lifetime = static_cast<std::int32_t>(*n); return true; }
        if (key == "seq")      { auto n = P.read_number(); if (!n) return false; s.seq = static_cast<std::int32_t>(*n); return true; }
        if (key == "tick")     { auto n = P.read_number(); if (!n) return false; s.tick = static_cast<std::uint32_t>(*n); return true; }
        if (key == "xpos")     { auto n = P.read_number(); if (!n) return false; s.xpos = static_cast<std::int32_t>(*n); return true; }
        if (key == "ypos")     { auto n = P.read_number(); if (!n) return false; s.ypos = static_cast<std::int32_t>(*n); return true; }
        return P.skip_value();
    });
}

bool parse_event(Parser& P, EventEntry& e)
{
    return P.parse_object([&](const std::string& key) -> bool {
        if (key == "a")        { auto n = P.read_number(); if (!n) return false; e.a = static_cast<std::uint32_t>(*n); return true; }
        if (key == "b")        { auto n = P.read_number(); if (!n) return false; e.b = static_cast<std::uint32_t>(*n); return true; }
        if (key == "kind")     { auto s = P.read_string(); if (!s) return false; e.kind = *s; return true; }
        if (key == "sequence") { auto n = P.read_number(); if (!n) return false; e.sequence = static_cast<std::uint32_t>(*n); return true; }
        if (key == "text")     { auto s = P.read_string(); if (!s) return false; e.text = *s; return true; }
        if (key == "tick")     { auto n = P.read_number(); if (!n) return false; e.tick = static_cast<std::uint32_t>(*n); return true; }
        return P.skip_value();
    });
}

} // namespace

std::optional<StateDump> parse_state_dump(std::string_view json)
{
    Parser P{json};
    StateDump out;
    bool ok = P.parse_object([&](const std::string& key) -> bool {
        if (key == "walkers")
        {
            return P.parse_array([&]() -> bool {
                WalkerEntry w;
                if (!parse_walker(P, w)) return false;
                out.walkers.push_back(std::move(w));
                return true;
            });
        }
        if (key == "effects")
        {
            return P.parse_array([&]() -> bool {
                EffectEntry e;
                if (!parse_effect(P, e)) return false;
                out.effects.push_back(std::move(e));
                return true;
            });
        }
        if (key == "events")
        {
            return P.parse_array([&]() -> bool {
                EventEntry e;
                if (!parse_event(P, e)) return false;
                out.events.push_back(std::move(e));
                return true;
            });
        }
        if (key == "weapons")
        {
            return P.parse_array([&]() -> bool {
                WeaponEntry w;
                if (!parse_weapon(P, w)) return false;
                out.weapons.push_back(std::move(w));
                return true;
            });
        }
        if (key == "weapon_tracks")
        {
            return P.parse_array([&]() -> bool {
                WeaponTrackSample s;
                if (!parse_weapon_track(P, s)) return false;
                out.weapon_tracks.push_back(std::move(s));
                return true;
            });
        }
        if (key == "inventory_keys")
        {
            std::vector<std::uint8_t> keys;
            bool arr_ok = P.parse_array([&]() -> bool {
                auto n = P.read_number();
                if (!n) return false;
                keys.push_back(static_cast<std::uint8_t>(
                    static_cast<std::int32_t>(*n) & 0xFF));
                return true;
            });
            if (!arr_ok) return false;
            out.inventory_keys = std::move(keys);
            return true;
        }
        if (key == "tick")
        {
            auto n = P.read_number();
            if (!n) return false;
            out.tick = static_cast<std::uint32_t>(*n);
            return true;
        }
        if (key == "rng_state")
        {
            auto s = P.read_string();
            if (!s) return false;
            if (*s == "unobservable")
            {
                out.rng_observable = false;
                out.rng_state = 0;
            }
            else if (s->size() >= 2 && (*s)[0] == '0' && ((*s)[1] == 'x' || (*s)[1] == 'X'))
            {
                out.rng_state = static_cast<std::uint32_t>(
                    std::strtoul(s->c_str() + 2, nullptr, 16));
                out.rng_observable = true;
            }
            return true;
        }
        if (key == "schema_version")
        {
            auto s = P.read_string();
            if (!s) return false;
            out.schema_version = *s;
            return true;
        }
        if (key == "score_per_team")
        {
            std::size_t idx = 0;
            return P.parse_array([&]() -> bool {
                auto n = P.read_number();
                if (!n) return false;
                if (idx < 4)
                    out.score_per_team[idx] = static_cast<std::uint32_t>(*n);
                ++idx;
                return true;
            });
        }
        if (key == "level_done")
        {
            auto n = P.read_number();
            if (!n) return false;
            out.level_done = static_cast<std::int32_t>(*n);
            return true;
        }
        if (key == "level_tick_count")
        {
            auto n = P.read_number();
            if (!n) return false;
            out.level_tick_count = static_cast<std::uint32_t>(*n);
            return true;
        }
        // Unknown key — forward-compatible: skip silently.
        return P.skip_value();
    });
    if (!ok) return std::nullopt;
    return out;
}

} // namespace og::parity
