std::vector<std::string>& level_editor_testing_prompt_queue_ref();
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);

namespace {

std::atomic<std::uint64_t> s_prompt_block_entered_count{0};
std::atomic<std::uint64_t> s_prompt_block_input_observed_count{0};
std::atomic<std::uint64_t> s_prompt_block_input_completed_count{0};
std::atomic<int> s_prompt_block_held_key{-1};
std::atomic<int> s_prompt_block_click_x{0};
std::atomic<int> s_prompt_block_click_y{0};
std::atomic<bool> s_prompt_block_click_pending{false};
std::atomic<bool> s_prompt_force_real{false};
std::atomic<bool> s_prompt_real_active{false};
std::atomic<std::uint64_t> s_prompt_real_entered_count{0};
std::atomic<std::uint64_t> s_prompt_testing_call_count{0};

void prompt_block_testing_input_observed()
{
    s_prompt_block_input_observed_count.fetch_add(1, std::memory_order_release);
}

void prompt_block_testing_input_completed()
{
    s_prompt_block_input_completed_count.fetch_add(1, std::memory_order_release);
}

} // namespace

void level_editor_testing_prompt_block_input_reset()
{
    s_prompt_block_entered_count.store(0, std::memory_order_release);
    s_prompt_block_input_observed_count.store(0, std::memory_order_release);
    s_prompt_block_input_completed_count.store(0, std::memory_order_release);
    s_prompt_block_held_key.store(-1, std::memory_order_release);
    s_prompt_block_click_pending.store(false, std::memory_order_release);
}

void level_editor_testing_prompt_block_set_held_key(int key_state)
{
    s_prompt_block_held_key.store(key_state, std::memory_order_release);
}

void level_editor_testing_prompt_block_click(int x, int y)
{
    s_prompt_block_click_x.store(x, std::memory_order_relaxed);
    s_prompt_block_click_y.store(y, std::memory_order_relaxed);
    s_prompt_block_click_pending.store(true, std::memory_order_release);
}

std::uint64_t level_editor_testing_prompt_block_entered_count()
{
    return s_prompt_block_entered_count.load(std::memory_order_acquire);
}

std::uint64_t level_editor_testing_prompt_block_input_observed_count()
{
    return s_prompt_block_input_observed_count.load(std::memory_order_acquire);
}

std::uint64_t level_editor_testing_prompt_block_input_completed_count()
{
    return s_prompt_block_input_completed_count.load(std::memory_order_acquire);
}

void level_editor_testing_prompt_force_real(bool enabled)
{
    s_prompt_force_real.store(enabled, std::memory_order_release);
}

bool level_editor_testing_prompt_real_active()
{
    return s_prompt_real_active.load(std::memory_order_acquire);
}

std::uint64_t level_editor_testing_prompt_real_entered_count()
{
    return s_prompt_real_entered_count.load(std::memory_order_acquire);
}
