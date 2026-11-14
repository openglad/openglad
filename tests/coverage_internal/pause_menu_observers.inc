void pause_menu_testing_queue_outcome(PauseMenuResult outcome,
                                      bool release_pause)
{
    s_outcome_queue.push_back(
        QueuedPauseOutcome{.outcome = outcome,
                           .release_pause = release_pause});
}

void pause_menu_testing_clear_queue()
{
    s_outcome_queue.clear();
}

int pause_menu_testing_queue_remaining()
{
    return static_cast<int>(s_outcome_queue.size());
}

void pause_menu_testing_set_force_real(bool force)
{
    s_force_real_menu = force;
}

void pause_menu_testing_set_keepalive_interval_ms(std::uint32_t interval_ms)
{
    s_keepalive_interval_override_ms = interval_ms;
}
