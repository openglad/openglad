/* Text protocol session runner for openglad_text client.
 *
 * Runs the headless JSON protocol mode for a single game session.
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace og::ui {

struct TextProtocolArgs {
    std::string campaign = "gladiator";
    int level = 1;
    std::vector<int> team_families;
    std::uint32_t seed = 42;
    // When > 0, each spawned team guy is upgraded to this level and its
    // derived walker stats recomputed (playtest crews). 0 keeps the legacy
    // loader-default stats untouched.
    int team_level = 0;
    // Pre-seeded campaign decision state (--campaign-state key=value,...):
    // written into the session save before load so level hooks see the
    // values through og.campaign_var (playtest/demo harnesses).
    std::vector<std::pair<std::string, std::int32_t>> campaign_state;
};

// Run a single headless protocol session (until game ends or user quits).
// Returns 0 on success, non-zero on error.
int run_text_protocol_session(const TextProtocolArgs& args);

} // namespace og::ui
