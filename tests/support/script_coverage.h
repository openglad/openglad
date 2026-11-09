#pragma once

#include <openglad/gameplay/script/script_coverage.h>

namespace og::script::coverage {

// Test seam: arm/disarm the recorder regardless of the environment. Setting
// it true without an output directory records in memory only.
void set_enabled_for_testing(bool on);

// Test seam: redirect (or, with an empty string, disable) the exit dump.
void set_output_dir_for_testing(std::string dir);

// Drop everything recorded so far.
void reset();

// Test seam: record into a private, empty set for the lifetime of the object,
// then put back exactly what was there before. Tests cannot simply reset()
// the recorder — under an actual coverage run the process-global store is
// already accumulating real pack coverage, and a bare reset would throw its
// own binary's numbers away.
class ScopedRecording {
public:
    ScopedRecording();
    ~ScopedRecording();
    ScopedRecording(const ScopedRecording&) = delete;
    ScopedRecording& operator=(const ScopedRecording&) = delete;

private:
    struct State;
    std::unique_ptr<State> saved_;
    bool previously_enabled_ = false;
};

}  // namespace og::script::coverage
