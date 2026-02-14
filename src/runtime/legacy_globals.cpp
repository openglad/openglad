#include <openglad/legacy/base.h>

// Legacy global shims (transitional). Ownership is being moved into
// og::runtime::GameSession, but these variables must remain linkable for
// non-app binaries (tests/tools).
screen* myscreen = nullptr;

