bool picker_lobby_testing_standalone_client_alive() noexcept
{
    return g_standalone_picker_lobby_client != nullptr;
}

void picker_lobby_testing_drop_standalone_client() noexcept
{
    if (g_active_picker_lobby_client != nullptr)
        return;  // someone else owns the seam right now; not ours to close
    if (g_standalone_picker_lobby_client)
        g_standalone_picker_lobby_client->shutdown();
    g_standalone_picker_lobby_client.reset();
}
