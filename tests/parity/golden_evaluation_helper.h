// Phase 04-prep — uniform golden-load + per-predicate evaluation helper.
//
// Wraps the schema-v1 parse + evaluate_one loop used by the behavioural
// runtime gate (test_parity_coverage_gate.cpp) so every gate test reads
// `tests/parity/golden/<id>.json` the same way and surfaces a
// per-predicate trace including the master-side evaluation outcome.
//
// Schema-v1 freeze: this helper does NOT touch state_dump.{h,cpp}. It
// only reads the canonical JSON and runs the existing evaluator.

#pragma once

#include "fact_predicate.h"
#include "state_dump.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace og::parity {

struct PredicateEvaluation
{
    std::size_t  index             = 0;
    FactKind     kind              = FactKind::TickReached;
    std::int32_t arg0              = 0;
    bool         applies_to_master = true;
    bool         applies_to_branch = true;
    bool         ok                = true;   // raw FactEvalResult.ok
    bool         indeterminate     = false;  // raw FactEvalResult.indeterminate
    bool         determinate_pass  = false;  // applies_to_master && ok && !indeterminate
    std::string  message;
};

struct GoldenEvaluation
{
    bool                       scenario_present = false;
    bool                       golden_present   = false;
    bool                       parse_ok         = false;
    std::optional<StateDump>   master_dump;
    std::vector<PredicateEvaluation> predicates;
    std::string                load_error;

    // True iff at least one predicate has applies_to_master==true,
    // determinate evaluation, ok=true, and kind != TickReached. This is
    // the contract the Phase 04-prep runtime gate enforces.
    bool any_non_tick_master_pass() const noexcept;
};

GoldenEvaluation
evaluate_facts_against_golden_for_id(std::string_view scenario_id);

} // namespace og::parity
