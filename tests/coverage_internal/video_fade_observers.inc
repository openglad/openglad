namespace og::video_testing
{
std::atomic<int> g_fade_violations{0};
void (*g_fade_midpoint_hook)() = nullptr;

namespace
{
std::mutex s_fade_violation_mutex;
std::vector<std::string> s_fade_violation_messages;
} // namespace

void report_fade_violation(const char* what)
{
    TRACE("video", "FADE VIOLATION: %s", what);
    LogError("FADE VIOLATION: {}\n", what);
    ++g_fade_violations;
    std::lock_guard<std::mutex> lock(s_fade_violation_mutex);
    s_fade_violation_messages.emplace_back(what);
}

std::vector<std::string> fade_violation_messages()
{
    std::lock_guard<std::mutex> lock(s_fade_violation_mutex);
    return s_fade_violation_messages;
}

void reset_fade_violations()
{
    std::lock_guard<std::mutex> lock(s_fade_violation_mutex);
    s_fade_violation_messages.clear();
    g_fade_violations.store(0);
}
} // namespace og::video_testing
