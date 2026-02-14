#pragma once

#include <memory>

class screen;

namespace og::runtime {
class GameSession;
}

// Legacy global owner. Prefer creating an explicit og::runtime::GameSession with RAII.
[[nodiscard]] std::unique_ptr<og::runtime::GameSession>& global_session_owner();
og::runtime::GameSession* create_global_session(short numviews = 1);
void destroy_global_session();

// Compatibility shims: keep older call sites working while global state is removed.
// Deletion plan: migrate remaining callers to GameSession and then delete these.
screen* create_global_screen(short numviews = 1);
void destroy_global_screen();
