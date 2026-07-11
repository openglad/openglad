/* Text protocol session runner for openglad_text client.
 *
 * Runs the headless JSON protocol mode for a single game session.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace og::ui {

struct TextProtocolArgs {
    std::string campaign = "org.openglad.gladiator";
    int level = 1;
    std::vector<int> team_families;
    std::uint32_t seed = 42;
    // When > 0, each spawned team guy is upgraded to this level and its
    // derived walker stats recomputed (playtest crews). 0 keeps the legacy
    // loader-default stats untouched.
    int team_level = 0;
};

// Run a single headless protocol session (until game ends or user quits).
// Returns 0 on success, non-zero on error.
int run_text_protocol_session(const TextProtocolArgs& args);

} // namespace og::ui
