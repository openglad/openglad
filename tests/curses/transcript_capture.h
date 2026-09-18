/* Transcript capture for curses regression tests (PR #292 P8 media).
 *
 * A regression test that already drives two real lobbies through
 * HeadlessTerminals is also the only honest picture of what those terminals
 * SHOW. capture_transcript() writes one terminal's dump() to
 * $OG_FX_CAPTURE_DIR/<name>.txt so the media recipe can render it; with the
 * env unset it does nothing at all, so the calls stay inert in a normal run.
 */
#pragma once

#include <openglad/platform/curses/headless_terminal.h>

#include <cstdlib>
#include <fstream>
#include <string>

// Writes `term.dump()` verbatim to $OG_FX_CAPTURE_DIR/<name>.txt. Returns true
// only when a file was written (false when the env is unset/empty, or on any
// filesystem failure) so a test can pin both arms.
inline bool capture_transcript(const og::curses::HeadlessTerminal& term,
                               const std::string& name)
{
    const char* const dir = std::getenv("OG_FX_CAPTURE_DIR");
    if (dir == nullptr || *dir == '\0')
        return false;
    const std::string path = std::string(dir) + "/" + name + ".txt";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << term.dump();
    out.flush();
    return out.good();
}

// The phase tag the capture names carry: "before" (base tree), "after" (merged
// tree), or "run" when nobody asked for one.
inline std::string transcript_phase()
{
    const char* const phase = std::getenv("OG_FX_PHASE");
    return (phase != nullptr && *phase != '\0') ? std::string(phase) : std::string("run");
}
