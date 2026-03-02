#pragma once

#include <memory>
#include <openglad/interface/screen_lifecycle.h>

namespace og::runtime {
class GameSession;
}

// Legacy global owner. Prefer creating an explicit og::runtime::GameSession with RAII.
[[nodiscard]] std::unique_ptr<og::runtime::GameSession>& global_session_owner();
og::runtime::GameSession* create_global_session(short numviews = 1);
void destroy_global_session();
