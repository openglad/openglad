// Test helper API (used by deterministic coverage-driving tests).
void level_editor_testing_prompt_queue_clear()
{
    level_editor_testing_prompt_queue_ref().clear();
}

void level_editor_testing_prompt_queue_push(const char* s)
{
    level_editor_testing_prompt_queue_ref().push_back(s ? std::string(s) : std::string());
}

std::uint64_t level_editor_testing_prompt_call_count()
{
    return s_prompt_testing_call_count.load(std::memory_order_acquire);
}

// Provide access to the prompt queue without exposing it globally in non-test builds.
std::vector<std::string>& level_editor_testing_prompt_queue_ref()
{
    static std::vector<std::string> s_prompt_queue;
    return s_prompt_queue;
}
