// The ONE golden rule of og_test_parity: the canonical branch dump must equal
// the committed golden byte for byte. Both CompareMode arms call
// first_golden_divergence; everything else in this header is message
// formatting for the failure it reports. scripts/parity/diff_dumps.py is the
// human triage tool (categorised, three diffs); it never decides pass/fail.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace og::parity {

inline constexpr std::size_t kGoldenExcerptBefore = 64; // bytes shown before the divergence
inline constexpr std::size_t kGoldenExcerptAfter  = 32; // bytes shown from the divergence on

struct GoldenDivergence
{
    // 0-based index of the first differing byte; == min(size) when one input
    // is a prefix of the other.
    std::size_t offset = 0;
    // golden_json_path_at(actual, offset), diff_dumps.py spelling
    // ("events[9].b"); "<root>" when no key/array frame is open.
    std::string path;
    std::string branch_excerpt; // golden_excerpt_at(actual, offset)
    std::string golden_excerpt; // golden_excerpt_at(expected, offset)
};

// nullopt iff actual == expected (byte equality, trailing newline included).
std::optional<GoldenDivergence>
first_golden_divergence(std::string_view actual, std::string_view expected);

// Message-only. The JSON key path open at byte `offset` of a canonical dump
// (state_dump.cpp's emitter), computed by a single forward scan.
std::string golden_json_path_at(std::string_view canonical_json,
                                std::size_t offset);

// Message-only. json[offset-64, offset+32) clamped to the string, "..."
// prefixed / suffixed where clamped, '\n' rendered as the two characters "\n".
std::string golden_excerpt_at(std::string_view json, std::size_t offset);

// The gate message: five lines, no trailing newline.
std::string format_golden_divergence(std::string_view scenario_id,
                                     const GoldenDivergence& div);

} // namespace og::parity
