void set_emscripten_websocket_api_for_testing(
    const EmscriptenWebSocketApi* api) noexcept
{
    g_websocket_api_override = api;
}
