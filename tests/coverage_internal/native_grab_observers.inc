bool testing_keyboard_grab_requested()
{
    return g_testing_keyboard_grab_requested.load(std::memory_order_acquire);
}
