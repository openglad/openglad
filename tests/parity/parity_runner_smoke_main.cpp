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
//
// Exit codes:
//   0  dump written
//   1  bad arguments / unknown scenario id
//   2  the output file could not be written
//   3  the tool could not stand up a usable environment, or the scenario's
//      level did not load (broken campaign mount or PhysFS search path).
//      NOTHING is written in this case — an empty arena serialises into
//      perfectly valid JSON that disagrees with every golden, and a silent
//      stub dump is worse than no dump.

#include "fact_predicate.h"
#include "parity_bootstrap.h"
#include "parity_runner.h"
#include "scenario_table.h"
#include "state_dump.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unistd.h>

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

// The scratch config dir this process created, if it created one; removed on
// the way out. Empty whenever OPENGLAD_CONFIG_DIR came from the caller — a
// directory we were handed is not ours to delete.
std::string g_owned_config_dir;

void remove_owned_config_dir()
{
    if (g_owned_config_dir.empty())
        return;
    std::error_code ec;
    std::filesystem::remove_all(g_owned_config_dir, ec);
    g_owned_config_dir.clear();
}

// Where the campaign archives get restored to and the mounted campaign is
// read from. Unset means `get_user_path()` resolves to the developer's real
// ~/.openglad, and every smoke run would rewrite the campaign archives in
// their live install as a side effect of producing a dump. Point it at a
// private scratch directory instead — one PER PROCESS.
//
// Per-process is the whole point, not tidiness: `restore_default_campaigns`
// copies every builtin .glad with `overwrite_existing` on every single run,
// so two runs sharing one directory truncate and rewrite the archives the
// other one has mounted. A deliberately concurrent repro — eight runs, no
// OPENGLAD_CONFIG_DIR — used to red seven of them with exit 3.
//
// On failure the path comes back empty and `failure` says why; the caller
// turns that into the same named refusal a broken mount gets, because a
// discarded error_code here reappears as an unexplained empty arena later.
std::string ensure_private_config_dir(std::string& failure)
{
    if (const char* env = std::getenv("OPENGLAD_CONFIG_DIR");
        env != nullptr && env[0] != '\0')
    {
        return std::string(env);
    }

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("openglad-parity-smoke-config-" + std::to_string(::getpid()));
    // A PID is unique among LIVE processes, so the only way this path already
    // exists is a previous run that died before its cleanup. Start from bare
    // ground rather than inheriting whatever state it left behind.
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ec.clear();
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        failure = "cannot create private config dir " + dir.string() + ": " +
                  ec.message();
        return std::string();
    }
    g_owned_config_dir = dir.string();
    std::atexit(remove_owned_config_dir);
    setenv("OPENGLAD_CONFIG_DIR", g_owned_config_dir.c_str(), 1);
    return g_owned_config_dir;
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

    std::string config_failure;
    const std::string config_dir = ensure_private_config_dir(config_failure);
    if (!config_failure.empty())
    {
        std::fprintf(stderr,
            "parity_runner_smoke: refusing to write a dump for scenario '%s'\n"
            "  %s\n"
            "There is nowhere to restore the campaign archives to, so the run\n"
            "would describe an empty arena rather than the scenario.\n",
            scenario_id.c_str(), config_failure.c_str());
        return 3;
    }

    og::parity::BootstrapScope boot(argv[0]);
    const auto outcome = og::parity::run_scenario(*spec);

    // A scenario whose level never loaded still produces a structurally
    // valid dump — an empty arena serialises just as happily as a real one —
    // so a broken bootstrap used to exit 0 with a plausible-looking file that
    // disagreed with every golden. Refuse to publish anything instead: the
    // callers that matter (scripts/parity/run_mutation_canary.sh and
    // run_mutation_canary_runtime.py) already abort on a nonzero exit, and
    // aborting loudly beats a corpus-wide phantom drift.
    //
    // Exempt from the load half are the rows that build their own arena from
    // the header-only stub fixture, exactly as in test_parity_scenarios: the
    // four scen9301 rows (snapshot dirty bits, the three Z-axis arenas).
    // run_mutation_canary_runtime.py --all walks those rows, so failing them
    // here would break a driver over a stub that is by design. Every other
    // row — branch-internal ones included, since
    // treasure_exit_open_prompt_scen99 loads the real scen1.fss — must load.
    // A failed bootstrap still refuses for every row: that is the signal that
    // says the environment, not the row, is broken.
    if (!boot.ok() ||
        (!outcome.loaded && !og::parity::builds_its_own_arena(*spec)))
    {
        std::fprintf(stderr,
            "parity_runner_smoke: refusing to write a dump for scenario '%s'\n"
            "  scenario file : %.*s\n"
            "  config dir    : %s\n"
            "  bootstrap     : %s\n"
            "  level loaded  : %s\n"
            "The level did not load, so the dump would describe an empty\n"
            "arena rather than the scenario.\n",
            scenario_id.c_str(),
            static_cast<int>(spec->scenario_file.size()),
            spec->scenario_file.data(),
            config_dir.c_str(),
            boot.ok() ? "ok" : boot.failure().c_str(),
            outcome.loaded ? "yes" : "no");
        return 3;
    }

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
