void level_picker_testing_input_reset()
{
    s_level_picker_entered_count.store(0, std::memory_order_release);
    s_level_picker_action_count.store(0, std::memory_order_release);
    s_level_picker_click_pending.store(false, std::memory_order_release);
    s_level_picker_abort_pending.store(false, std::memory_order_release);
}

void level_picker_testing_click(int x, int y)
{
    const bool abort_requested = x < 0 || y < 0;
    s_level_picker_click_x.store(x, std::memory_order_relaxed);
    s_level_picker_click_y.store(y, std::memory_order_relaxed);
    s_level_picker_click_pending.store(
        !abort_requested, std::memory_order_release);
    s_level_picker_abort_pending.store(
        abort_requested, std::memory_order_release);
}

std::uint64_t level_picker_testing_entered_count()
{
    return s_level_picker_entered_count.load(std::memory_order_acquire);
}

std::uint64_t level_picker_testing_action_count()
{
    return s_level_picker_action_count.load(std::memory_order_acquire);
}
