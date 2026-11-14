namespace
{
std::atomic<unsigned long> g_yield_count{0};
}

unsigned long yield_count()
{
    return g_yield_count.load(std::memory_order_relaxed);
}

namespace
{
// Atomic mirror of s_text_input_active for cross-thread readers (injector
// threads). The production flag itself is untouched.
std::atomic<bool> g_testing_text_input_active{false};
}

bool text_input_is_active()
{
    return g_testing_text_input_active.load(std::memory_order_acquire);
}
