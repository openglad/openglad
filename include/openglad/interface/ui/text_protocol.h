/* Text protocol session runner for openglad_text client.
 *
 * Runs the headless JSON protocol mode for a single game session.
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class SaveData;

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
//
// This is the CLI shape (--protocol): a fresh default SaveData, a bare level
// load and the CLI crew assembler. It carries no match knobs because the
// caller has none — there is no picker save behind it. A session that DOES
// have one runs the staged shape below instead.
int run_text_protocol_session(const TextProtocolArgs& args);

// The staged shape (#247): the launch inputs of a text session that owns a
// picker SaveData. Everything the match needs — the knobs, the roster, the
// campaign decision book, the replay arm — already lives in that save, so
// this carries the save itself rather than re-deriving scalars from it.
struct TextStagedProtocolArgs {
    // Borrowed for the duration of the call; the session's own save is a
    // staged copy, never this object.
    const SaveData* session_save = nullptr;
    std::string campaign = "gladiator";
    // The session-latched match seed (TextPickerConfig::seed) — the same
    // latch VIEW LEVEL staged its preview with.
    std::uint32_t seed = 42;
    int difficulty = 1;
};

// Run a single headless protocol session over a world built by the ONE
// staged launch pipeline (og::server::MatchStage), the way the curses local
// session and every SDL launch owner build theirs. Emits the same ready line
// and runs the same JSON command loop as run_text_protocol_session().
// Returns 0 on success, non-zero on error.
int run_text_staged_protocol_session(const TextStagedProtocolArgs& args);

} // namespace og::ui
