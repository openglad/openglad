void picker_testing_mark_game_start()
{
    g_test_game_epoch.fetch_add(1, std::memory_order_release);
    g_test_game_frame_ticks.store(0, std::memory_order_release);
    g_test_in_game.store(true, std::memory_order_release);
}

void picker_testing_mark_frame_advance()
{
    g_test_game_frame_ticks.fetch_add(1, std::memory_order_release);
    std::function<void()> observer;
    {
        std::lock_guard<std::mutex> lock(g_test_frame_observer_mutex);
        observer.swap(g_test_frame_observer);
    }
    if (observer)
        observer();
}

void picker_testing_observe_next_game_frame(std::function<void()> observer)
{
    std::lock_guard<std::mutex> lock(g_test_frame_observer_mutex);
    g_test_frame_observer = std::move(observer);
}

void picker_testing_mark_game_end()
{
    g_test_in_game.store(false, std::memory_order_release);
}
