#include "golden_compare.h"

#include <algorithm>
#include <vector>

namespace og::parity {

namespace {

// One frame of the canonical-dump key path. Object frames carry the key most
// recently opened inside them; array frames carry the index of the element the
// scan is inside.
struct Frame
{
    bool        is_array = false;
    std::string key;
    std::size_t index = 0;
};

std::string index_suffix(std::size_t index)
{
    return "[" + std::to_string(index) + "]";
}

} // namespace

std::optional<GoldenDivergence>
first_golden_divergence(std::string_view actual, std::string_view expected)
{
    const std::size_t common = std::min(actual.size(), expected.size());
    const auto mism = std::mismatch(actual.begin(), actual.begin() + static_cast<std::ptrdiff_t>(common),
                                    expected.begin());
    std::size_t offset = static_cast<std::size_t>(mism.first - actual.begin());
    if (offset == common)
    {
        if (actual.size() == expected.size()) return std::nullopt;
        offset = common; // one input is a prefix of the other
    }

    GoldenDivergence div;
    div.offset         = offset;
    div.path           = golden_json_path_at(actual, offset);
    div.branch_excerpt = golden_excerpt_at(actual, offset);
    div.golden_excerpt = golden_excerpt_at(expected, offset);
    return div;
}

std::string golden_json_path_at(std::string_view json, std::size_t offset)
{
    const std::size_t limit = std::min(offset, json.size());
    std::vector<Frame> frames;
    std::size_t pos = 0;

    while (pos < limit)
    {
        const char c = json[pos];
        if (c == '{' || c == '[')
        {
            Frame f;
            f.is_array = (c == '[');
            frames.push_back(f);
            ++pos;
        }
        else if (c == '}' || c == ']')
        {
            if (!frames.empty()) frames.pop_back();
            ++pos;
        }
        else if (c == ',')
        {
            if (!frames.empty() && frames.back().is_array) ++frames.back().index;
            ++pos;
        }
        else if (c == '"')
        {
            // A string token started before `offset` is consumed whole, reading
            // past `offset` if it has to: a divergence inside a key name then
            // reports that key (the branch side's spelling of it).
            std::size_t p = pos + 1;
            std::string text;
            while (p < json.size() && json[p] != '"')
            {
                if (json[p] == '\\' && p + 1 < json.size())
                {
                    text.push_back(json[p]);
                    text.push_back(json[p + 1]);
                    p += 2;
                }
                else
                {
                    text.push_back(json[p]);
                    ++p;
                }
            }
            std::size_t after = (p < json.size()) ? p + 1 : json.size();
            if (after < json.size() && json[after] == ':')
            {
                if (!frames.empty() && !frames.back().is_array)
                    frames.back().key = text;
                ++after;
            }
            pos = after;
        }
        else
        {
            ++pos;
        }
    }

    std::string out;
    for (const Frame& f : frames)
    {
        if (f.is_array)
        {
            out += index_suffix(f.index);
        }
        else if (!f.key.empty())
        {
            if (!out.empty()) out.push_back('.');
            out += f.key;
        }
    }
    if (out.empty()) return "<root>";
    return out;
}

std::string golden_excerpt_at(std::string_view json, std::size_t offset)
{
    const std::size_t start = (offset > kGoldenExcerptBefore)
                                  ? offset - kGoldenExcerptBefore
                                  : 0;
    const std::size_t end = std::min(json.size(), offset + kGoldenExcerptAfter);

    std::string out;
    if (start > 0) out += "...";
    for (std::size_t i = start; i < end; ++i)
    {
        if (json[i] == '\n')
            out += "\\n";
        else
            out.push_back(json[i]);
    }
    if (end < json.size()) out += "...";
    return out;
}

std::string format_golden_divergence(std::string_view scenario_id,
                                     const GoldenDivergence& div)
{
    const std::string id(scenario_id);
    std::string out;
    out += "golden byte-compare FAIL for " + id +
           ": branch dump differs from tests/parity/golden/" + id +
           ".json at byte " + std::to_string(div.offset) + " (" + div.path + ")";
    out += "\n  branch: " + div.branch_excerpt;
    out += "\n  golden: " + div.golden_excerpt;
    out += "\n  triage: mkdir -p build/ci-test/parity && "
           "./build/ci-test/parity_runner_smoke --scenario " + id +
           " --out build/ci-test/parity/" + id +
           ".json && scripts/parity/run_parity_diff.sh " + id;
    out += "\n  adjudicate per tests/parity/golden/DRIFT_LEDGER.md (companion "
           "-> branch -> merge base); a companion recapture is never pasted "
           "over a ledger-blessed golden";
    return out;
}

} // namespace og::parity
