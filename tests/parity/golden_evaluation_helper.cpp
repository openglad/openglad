// Phase 04-prep — uniform golden-load + per-predicate evaluation helper.

#include "golden_evaluation_helper.h"

#include "fact_predicate.h"
#include "scenario_table.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace og::parity {

bool GoldenEvaluation::any_non_tick_master_pass() const noexcept
{
    for (const auto& pe : predicates)
    {
        if (!pe.applies_to_master)         continue;
        if (pe.kind == FactKind::TickReached) continue;
        if (pe.indeterminate)              continue;
        if (pe.ok)                         return true;
    }
    return false;
}

namespace {

const ScenarioSpec* find_spec_by_id(std::string_view id)
{
    for (const auto& s : kScenarios)
        if (s.id == id) return &s;
    return nullptr;
}

} // namespace

GoldenEvaluation
evaluate_facts_against_golden_for_id(std::string_view scenario_id)
{
    GoldenEvaluation out;

    const ScenarioSpec* spec = find_spec_by_id(scenario_id);
    if (spec == nullptr)
    {
        out.load_error = "scenario id not present in kScenarios";
        return out;
    }
    out.scenario_present = true;

    // Tests run with WORKING_DIRECTORY = CMAKE_SOURCE_DIR (see
    // og_add_test_group in the root CMakeLists.txt), so a relative
    // path resolves under the repo root.
    const std::filesystem::path path =
        std::filesystem::path("tests/parity/golden") /
        (std::string(scenario_id) + ".json");

    std::ifstream in(path, std::ios::binary);
    if (!in.good())
    {
        out.load_error = "golden file missing at " + path.string();
        return out;
    }
    out.golden_present = true;

    std::stringstream ss;
    ss << in.rdbuf();
    auto parsed = parse_state_dump(ss.str());
    if (!parsed.has_value())
    {
        out.load_error = "parse_state_dump failed on " + path.string();
        return out;
    }
    out.parse_ok    = true;
    out.master_dump = std::move(parsed);

    if (spec->expected_facts == nullptr || spec->fact_count == 0)
        return out;

    for (std::size_t i = 0; i < spec->fact_count; ++i)
    {
        const FactPredicate& p = spec->expected_facts[i];

        PredicateEvaluation pe;
        pe.index             = i;
        pe.kind              = p.kind;
        pe.arg0              = p.arg0;
        pe.applies_to_master = p.applies_to_master;
        pe.applies_to_branch = p.applies_to_branch;

        if (!p.applies_to_master)
        {
            pe.ok               = true;
            pe.indeterminate    = false;
            pe.determinate_pass = false;
            out.predicates.push_back(std::move(pe));
            continue;
        }

        const FactEvalResult r = evaluate_one(p, *out.master_dump);
        pe.ok               = r.ok;
        pe.indeterminate    = r.indeterminate;
        pe.message          = r.message;
        pe.determinate_pass = r.ok && !r.indeterminate;
        out.predicates.push_back(std::move(pe));
    }

    return out;
}

} // namespace og::parity
