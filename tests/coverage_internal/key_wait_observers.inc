namespace og::input {
void testing_push_key_wait_event(const og::input_native::EventData& event)
{
    const std::lock_guard<std::mutex> lock(g_key_wait_queue_mutex);
    g_key_wait_queue.push_back(event);
}

void testing_clear_key_wait_events()
{
    const std::lock_guard<std::mutex> lock(g_key_wait_queue_mutex);
    g_key_wait_queue.clear();
}

int testing_pending_key_wait_events()
{
    const std::lock_guard<std::mutex> lock(g_key_wait_queue_mutex);
    return static_cast<int>(g_key_wait_queue.size());
}
} // namespace og::input
