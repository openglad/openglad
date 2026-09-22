#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// A cast either succeeds or supplies a short, single-line HUD reason.
// There is no boolean constructor: native callbacks must name their refusal
// just as Lua casts must return false, "reason".
class SpecialResult {
public:
    // The HUD uses six pixels per character; this fits a 160-pixel view.
    static constexpr std::size_t max_reason_length = 24;

    static SpecialResult success() { return SpecialResult(std::string{}); }

    static SpecialResult failure(std::string reason)
    {
        if (!valid_reason(reason))
            throw std::invalid_argument(
                "special failure reason must contain visible text, use "
                "printable ASCII, and fit in 24 bytes");
        return SpecialResult(std::move(reason));
    }

    static bool valid_reason(std::string_view reason)
    {
        if (reason.empty() || reason.size() > max_reason_length)
            return false;
        bool visible = false;
        for (const char byte : reason) {
            const auto c = static_cast<unsigned char>(byte);
            if (c < 32 || c > 126)
                return false;
            visible = visible || c != ' ';
        }
        return visible;
    }

    bool succeeded() const { return reason_.empty(); }
    const std::string& reason() const { return reason_; }
    explicit operator bool() const { return succeeded(); }

private:
    explicit SpecialResult(std::string reason) : reason_(std::move(reason)) {}

    std::string reason_;
};
