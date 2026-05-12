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
        "       parity_runner_smoke --list\n");
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
    const std::string json = og::parity::canonical_serialize(outcome.dump);

    if (out_path == "-")
    {
        std::fwrite(json.data(), 1, json.size(), stdout);
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
        f.write(json.data(), static_cast<std::streamsize>(json.size()));
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
