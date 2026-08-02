// parity_runner_smoke — standalone driver around the parity runner.
//
// Builds against the same parity bootstrap / runner the og_test_parity
// integration binary uses, but does its own PhysFS init so it can be run
// outside the integration-test environment.
//
// Usage:
//   parity_runner_smoke --scenario <id> [--out <path>]
//   parity_runner_smoke --list      # Phase 07 will populate this; today it
//                                   # prints the master-comparable subset.

#include "fact_predicate.h"
#include "parity_bootstrap.h"
#include "parity_runner.h"
#include "scenario_table.h"
#include "state_dump.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

// Provided by tests/integration_main.cpp in the og_test_parity build; the
// standalone smoke runner needs its own definition because button.cpp
// references it from interface/ui/button.cpp.
std::mutex& get_allbuttons_mutex()
{
    static std::mutex m;
    return m;
}

namespace {

void print_usage(std::FILE* out)
{
    std::fprintf(out,
        "usage: parity_runner_smoke --scenario <id> [--out <path>]\n"
        "       parity_runner_smoke --scenario <id> --evaluate-facts [--out <path>]\n"
        "       parity_runner_smoke --list\n");
}

const char* fact_kind_name(og::parity::FactKind k)
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
        case FactKind::WeaponFamilyCount:               return "WeaponFamilyCount";
        case FactKind::TreasureFamilyOfOrderRemovedFromOblist:
            return "TreasureFamilyOfOrderRemovedFromOblist";
        case FactKind::WeaponSpeed:                     return "WeaponSpeed";
        case FactKind::WeaponNetTravel:                 return "WeaponNetTravel";
        case FactKind::EffectNetTravel:                 return "EffectNetTravel";
        case FactKind::WalkerOnFloor:                   return "WalkerOnFloor";
    }
    return "Unknown";
}

void append_json_escaped(std::string& out, std::string_view s)
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

std::string serialize_fact_evaluation(const og::parity::ScenarioSpec& spec,
                                      const og::parity::StateDump&    dump)
{
    std::string out;
    out.append("{\n  \"scenario\": ");
    append_json_escaped(out, spec.id);
    out.append(",\n  \"facts\": [");
    for (std::size_t i = 0; i < spec.fact_count; ++i)
    {
        const auto& p = spec.expected_facts[i];
        const auto r = og::parity::evaluate_one(p, dump);
        if (i != 0) out.push_back(',');
        out.append("\n    { \"index\": ");
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%zu", i);
        out.append(buf);
        out.append(", \"kind\": ");
        append_json_escaped(out, fact_kind_name(p.kind));
        out.append(", \"ok\": ");
        out.append(r.ok ? "true" : "false");
        out.append(", \"indeterminate\": ");
        out.append(r.indeterminate ? "true" : "false");
        out.append(", \"message\": ");
        append_json_escaped(out, r.message);
        out.append(" }");
    }
    out.append("\n  ]\n}\n");
    return out;
}

const og::parity::ScenarioSpec* find_scenario(std::string_view id)
{
    for (const auto& s : og::parity::kScenarios)
    {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

int list_scenarios()
{
    for (const auto& s : og::parity::kScenarios)
    {
        if (s.is_branch_internal) continue;
        std::printf("%.*s\n", static_cast<int>(s.id.size()), s.id.data());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::string scenario_id;
    std::string out_path = "-";
    bool evaluate_facts = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            print_usage(stdout);
            return 0;
        }
        if (arg == "--list")
        {
            // Bootstrap not needed for --list.
            return list_scenarios();
        }
        if (arg == "--scenario" && i + 1 < argc)
        {
            scenario_id = argv[++i];
            continue;
        }
        if (arg == "--out" && i + 1 < argc)
        {
            out_path = argv[++i];
            continue;
        }
        if (arg == "--evaluate-facts")
        {
            evaluate_facts = true;
            continue;
        }
        std::fprintf(stderr, "parity_runner_smoke: unrecognised argument: %.*s\n",
                     static_cast<int>(arg.size()), arg.data());
        print_usage(stderr);
        return 1;
    }

    if (scenario_id.empty())
    {
        print_usage(stderr);
        return 1;
    }

    const auto* spec = find_scenario(scenario_id);
    if (spec == nullptr)
    {
        std::fprintf(stderr, "parity_runner_smoke: unknown scenario id: %s\n",
                     scenario_id.c_str());
        return 1;
    }

    og::parity::BootstrapScope boot(argv[0]);
    const auto outcome = og::parity::run_scenario(*spec);
    const std::string payload = evaluate_facts
        ? serialize_fact_evaluation(*spec, outcome.dump)
        : og::parity::canonical_serialize(outcome.dump);

    if (out_path == "-")
    {
        std::fwrite(payload.data(), 1, payload.size(), stdout);
    }
    else
    {
        std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            std::fprintf(stderr,
                "parity_runner_smoke: cannot open output file: %s\n",
                out_path.c_str());
            return 2;
        }
        f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        if (!f)
        {
            std::fprintf(stderr,
                "parity_runner_smoke: write failed: %s\n",
                out_path.c_str());
            return 2;
        }
    }
    return 0;
}
