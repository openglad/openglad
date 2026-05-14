// scenario_facts_dump — emit tests/parity/scenario_facts_generated.json
// at CMake configure / build time so scripts/parity/evaluate_facts.py
// has a single source of truth for the per-scenario predicate set.
//
// The JSON shape is:
//   { "scenarios": [
//       { "id": "...",
//         "compare_mode": "ByteEqual"|"SemanticParity"|"Invariant",
//         "predicates": [
//           { "kind": "WalkerFamilyCount",
//             "arg0": 0, "arg1": 1, "arg2": 99,
//             "arg3": 0, "arg4": 0, "label": "..." },
//           ...
//         ] },
//       ...
//     ] }

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
        out.append("    { \"id\": ");
        append_escaped(out, s.id);
        out.append(", \"compare_mode\": ");
        append_escaped(out, mode_name(s.compare_mode));
        out.append(", \"predicates\": [");
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
        out.append(" ] }");
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
