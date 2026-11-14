#pragma once

#ifdef TESTING

#include "../../../tests/support/test_trace.h"

#define TRACE(cat, ...) trace_write(cat, __VA_ARGS__)

#else

#define TRACE(cat, ...) ((void)0)

#endif // TESTING
