#include "unit.h"

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

int main()
{
    int passed = 0;
    int failed = 0;
    for (const auto& tc : og::unit::registry())
    {
        std::fprintf(stderr, "[ RUN      ] %s\n", tc.name);
        try {
            tc.fn();
            ++passed;
            std::fprintf(stderr, "[       OK ] %s\n", tc.name);
        } catch (...) {
            ++failed;
            std::fprintf(stderr, "[  FAILED  ] %s (threw)\n", tc.name);
        }
    }

    std::fprintf(stderr, "\n=== Unit Results: %d passed, %d failed, %d total ===\n\n",
                 passed, failed, passed + failed);
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    return failed == 0 ? 0 : 1;
}
