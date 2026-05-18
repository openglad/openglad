// scenario_facts_dump — emit tests/parity/scenario_facts_generated.json
// at CMake configure / build time so scripts/parity/evaluate_facts.py
// has a single source of truth for the per-scenario predicate set.
//
// The JSON shape is:
//   { "scenarios": [
//       { "id": "...",
//         "scenario_id": "...",
//         "compare_mode": "ByteEqual"|"SemanticParity"|"Invariant",
//         "family_spawns": ["FAMILY_SOLDIER", ...],
//         "spawns": [{ "family": "FAMILY_SOLDIER",
//                      "family_id": 0, "order": "Living",
//                      "order_id": 0, "team": 0, "x": 96, "y": 120 }],
//         "predicates": [
//           { "kind": "WalkerFamilyCount",
//             "arg0": 0, "arg1": 1, "arg2": 99,
//             "arg3": 0, "arg4": 0, "label": "..." },
//           ...
//         ],
//         "predicate_kinds": ["TickReached", "WalkerFamilyCount", ...],
//         "expected_fact_kinds": ["TickReached", "WalkerFamilyCount", ...],
//         "expected_facts": [ ...same shape as predicates...,
//                             plus symbolic arg0 aliases for audits... ],
//         "expected_fact_objects": [ ...same shape as predicates... ] },
//       ...
//     ],
//     "rows": [ ...same row objects as scenarios... ],
//     "kScenarios": [ ...same row objects as scenarios... ] }

#include "fact_predicate.h"
#include "scenario_table.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

// Shim referenced by interface/ui/button.cpp; the og_test_parity binary
// provides its own definition via tests/integration_main.cpp.
std::mutex& get_allbuttons_mutex()
{
    static std::mutex m;
    return m;
}

namespace {

const char* kind_name(og::parity::FactKind k)
{
    using og::parity::FactKind;
    switch (k)
    {
        case FactKind::TickReached:                     return "TickReached";
        case FactKind::LevelDoneEquals:                 return "LevelDoneEquals";
        case FactKind::ScoreDelta:                      return "ScoreDelta";
        case FactKind::WalkerFamilyCount:               return "WalkerFamilyCount";
        case FactKind::WalkerOfTeamAlive:               return "WalkerOfTeamAlive";
        case FactKind::WalkerHpRangeAtFinalTick:        return "WalkerHpRangeAtFinalTick";
        case FactKind::WalkerKeysApplied:               return "WalkerKeysApplied";
        case FactKind::WalkerPositionMoved:             return "WalkerPositionMoved";
        case FactKind::WalkerDiedByFinal:               return "WalkerDiedByFinal";
        case FactKind::WalkerAliveAtFinal:              return "WalkerAliveAtFinal";
        case FactKind::TreasureFamilyRemovedFromOblist: return "TreasureFamilyRemovedFromOblist";
        case FactKind::StatDeltaOnPickup:               return "StatDeltaOnPickup";
        case FactKind::EffectFamilyCount:               return "EffectFamilyCount";
        case FactKind::EventKindAtLeast:                return "EventKindAtLeast";
        case FactKind::EventKindExactly:                return "EventKindExactly";
        case FactKind::WeaponFamilyEmitted:             return "WeaponFamilyEmitted";
        case FactKind::TreasureFamilyOfOrderRemovedFromOblist:
            return "TreasureFamilyOfOrderRemovedFromOblist";
    }
    return "Unknown";
}

const char* mode_name(og::parity::CompareMode m)
{
    using og::parity::CompareMode;
    switch (m)
    {
        case CompareMode::ByteEqual:      return "ByteEqual";
        case CompareMode::Invariant:      return "Invariant";
        case CompareMode::SemanticParity: return "SemanticParity";
    }
    return "Unknown";
}

const char* order_name(std::uint8_t order)
{
    switch (order)
    {
        case og::parity::kOrderLiving:    return "Living";
        case og::parity::kOrderWeapon:    return "Weapon";
        case og::parity::kOrderTreasure:  return "Treasure";
        case og::parity::kOrderGenerator: return "Generator";
        case og::parity::kOrderFX:        return "FX";
    }
    return "Unknown";
}

void append_escaped(std::string& out, std::string_view s)
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
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04X",
                                  static_cast<unsigned char>(c));
                    out.append(buf);
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

void append_predicate_array(std::string& out, const og::parity::ScenarioSpec& s)
{
    out.push_back('[');
    for (std::size_t i = 0; i < s.fact_count; ++i)
    {
        const auto& p = s.expected_facts[i];
        if (i != 0) out.push_back(',');
        out.append("\n      { \"kind\": ");
        append_escaped(out, kind_name(p.kind));
        char nums[128];
        std::snprintf(nums, sizeof(nums),
            ", \"arg0\": %d, \"arg1\": %d, \"arg2\": %d, \"arg3\": %d, \"arg4\": %d, \"label\": ",
            p.arg0, p.arg1, p.arg2, p.arg3, p.arg4);
        out.append(nums);
        append_escaped(out, p.label);
        out.append(", \"applies_to_branch\": ");
        out.append(p.applies_to_branch ? "true" : "false");
        out.append(", \"applies_to_master\": ");
        out.append(p.applies_to_master ? "true" : "false");
        out.append(" }");
    }
    out.append(" ]");
}

int arg0_order_for_symbol(og::parity::FactKind k)
{
    using og::parity::FactKind;
    switch (k)
    {
        case FactKind::WalkerFamilyCount:
        case FactKind::WalkerHpRangeAtFinalTick:
        case FactKind::WalkerPositionMoved:
        case FactKind::WalkerDiedByFinal:
        case FactKind::WalkerAliveAtFinal:
            return og::parity::kOrderLiving;
        case FactKind::TreasureFamilyRemovedFromOblist:
        case FactKind::TreasureFamilyOfOrderRemovedFromOblist:
        case FactKind::StatDeltaOnPickup:
            return og::parity::kOrderTreasure;
        case FactKind::EffectFamilyCount:
            return og::parity::kOrderFX;
        case FactKind::WeaponFamilyEmitted:
            return og::parity::kOrderWeapon;
        case FactKind::TickReached:
        case FactKind::LevelDoneEquals:
        case FactKind::ScoreDelta:
        case FactKind::WalkerKeysApplied:
        case FactKind::EventKindAtLeast:
        case FactKind::EventKindExactly:
            return -1;
    }
    return -1;
}

void append_predicate_audit_array(std::string& out, const og::parity::ScenarioSpec& s)
{
    append_predicate_array(out, s);
    if (s.fact_count == 0) return;

    // Remove the closing " ]" so symbolic aliases can be appended without
    // changing the canonical `predicates` array used by evaluators.
    out.resize(out.size() - 2);
    bool has_any = s.fact_count != 0;
    for (std::size_t i = 0; i < s.fact_count; ++i)
    {
        const auto& p = s.expected_facts[i];
        const int order = arg0_order_for_symbol(p.kind);
        if (order < 0) continue;

        if (has_any) out.push_back(',');
        has_any = true;
        out.append("\n      { \"kind\": ");
        append_escaped(out, kind_name(p.kind));
        out.append(", \"arg0\": ");
        append_escaped(out, og::parity::family_symbol_by_order(
                                 static_cast<std::uint8_t>(order), p.arg0));
        char nums[160];
        std::snprintf(nums, sizeof(nums),
            ", \"arg0_id\": %d, \"arg1\": %d, \"arg2\": %d, \"arg3\": %d, \"arg4\": %d, \"label\": ",
            p.arg0, p.arg1, p.arg2, p.arg3, p.arg4);
        out.append(nums);
        append_escaped(out, p.label);
        out.append(", \"applies_to_branch\": ");
        out.append(p.applies_to_branch ? "true" : "false");
        out.append(", \"applies_to_master\": ");
        out.append(p.applies_to_master ? "true" : "false");
        out.append(" }");
    }
    out.append(" ]");
}

void append_predicate_kind_array(std::string& out, const og::parity::ScenarioSpec& s)
{
    out.push_back('[');
    for (std::size_t i = 0; i < s.fact_count; ++i)
    {
        if (i != 0) out.append(", ");
        append_escaped(out, kind_name(s.expected_facts[i].kind));
    }
    out.push_back(']');
}

void append_living_family_spawns(std::string& out, const og::parity::ScenarioSpec& s)
{
    out.push_back('[');
    bool first = true;
    for (std::size_t i = 0; i < s.spawn_count; ++i)
    {
        const auto& sp = s.spawns[i];
        if (sp.order != og::parity::kOrderLiving) continue;
        if (!first) out.append(", ");
        first = false;
        append_escaped(out, og::parity::family_symbol_by_order(sp.order, sp.family));
    }
    out.push_back(']');
}

void append_living_family_spawn_ids(std::string& out, const og::parity::ScenarioSpec& s)
{
    out.push_back('[');
    bool first = true;
    for (std::size_t i = 0; i < s.spawn_count; ++i)
    {
        const auto& sp = s.spawns[i];
        if (sp.order != og::parity::kOrderLiving) continue;
        if (!first) out.append(", ");
        first = false;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", sp.family);
        out.append(buf);
    }
    out.push_back(']');
}

void append_spawn_objects(std::string& out, const og::parity::ScenarioSpec& s)
{
    out.push_back('[');
    for (std::size_t i = 0; i < s.spawn_count; ++i)
    {
        const auto& sp = s.spawns[i];
        if (i != 0) out.append(", ");
        out.append("{ \"family\": ");
        append_escaped(out, og::parity::family_symbol_by_order(sp.order, sp.family));
        char nums[192];
        std::snprintf(nums, sizeof(nums),
            ", \"family_id\": %d, \"order\": ", sp.family);
        out.append(nums);
        append_escaped(out, order_name(sp.order));
        std::snprintf(nums, sizeof(nums),
            ", \"order_id\": %u, \"team\": %u, \"x\": %d, \"y\": %d }",
            static_cast<unsigned>(sp.order),
            static_cast<unsigned>(sp.team),
            sp.x,
            sp.y);
        out.append(nums);
    }
    out.push_back(']');
}

void append_row_object(std::string& out, const og::parity::ScenarioSpec& s)
{
    out.append("{ \"id\": ");
    append_escaped(out, s.id);
    out.append(", \"scenario_id\": ");
    append_escaped(out, s.id);
    out.append(", \"compare_mode\": ");
    append_escaped(out, mode_name(s.compare_mode));
    out.append(", \"is_branch_internal\": ");
    out.append(s.is_branch_internal ? "true" : "false");
    out.append(", \"tick_budget\": ");
    char nums[128];
    std::snprintf(nums, sizeof(nums), "%u", static_cast<unsigned>(s.tick_budget));
    out.append(nums);
    out.append(", \"fresh_arena\": ");
    out.append(s.fresh_arena ? "true" : "false");
    out.append(", \"exercises\": ");
    std::snprintf(nums, sizeof(nums), "%llu",
                  static_cast<unsigned long long>(s.exercises));
    out.append(nums);
    out.append(", \"family_spawns\": ");
    append_living_family_spawns(out, s);
    out.append(", \"family_spawn_ids\": ");
    append_living_family_spawn_ids(out, s);
    out.append(", \"spawns\": ");
    append_spawn_objects(out, s);
    out.append(", \"predicates\": ");
    append_predicate_array(out, s);
    out.append(", \"predicate_kinds\": ");
    append_predicate_kind_array(out, s);
    out.append(", \"expected_fact_kinds\": ");
    append_predicate_kind_array(out, s);
    out.append(", \"expected_facts\": ");
    append_predicate_audit_array(out, s);
    out.append(", \"expected_fact_objects\": ");
    append_predicate_array(out, s);
    out.append(" }");
}

} // namespace

int main(int argc, char** argv)
{
    std::string out_path = "tests/parity/scenario_facts_generated.json";
    for (int i = 1; i < argc; ++i)
    {
        std::string_view a = argv[i];
        if (a == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (a == "--help" || a == "-h")
        {
            std::fprintf(stderr, "usage: scenario_facts_dump [--out PATH]\n");
            return 0;
        }
    }

    std::string out;
    out.append("{\n  \"scenarios\": [\n");
    bool first_row = true;
    for (const auto& s : og::parity::kScenarios)
    {
        if (!first_row) out.append(",\n");
        first_row = false;
        out.append("    ");
        append_row_object(out, s);
    }
    out.append("\n  ],\n  \"rows\": [\n");
    first_row = true;
    for (const auto& s : og::parity::kScenarios)
    {
        if (!first_row) out.append(",\n");
        first_row = false;
        out.append("    ");
        append_row_object(out, s);
    }
    out.append("\n  ],\n  \"kScenarios\": [\n");
    first_row = true;
    for (const auto& s : og::parity::kScenarios)
    {
        if (!first_row) out.append(",\n");
        first_row = false;
        out.append("    ");
        append_row_object(out, s);
    }
    out.append("\n  ]\n}\n");

    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        std::fprintf(stderr,
            "scenario_facts_dump: cannot open output: %s\n", out_path.c_str());
        return 2;
    }
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    if (!f)
    {
        std::fprintf(stderr,
            "scenario_facts_dump: write failed: %s\n", out_path.c_str());
        return 2;
    }
    return 0;
}
