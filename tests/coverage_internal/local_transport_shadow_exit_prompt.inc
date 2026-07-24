// Included by local_transport_shadow.cpp inside namespace og::runtime and only
// for TESTING builds. This drives the same event emitted by an exit/withdraw
// treasure while keeping orchestration code attributed to the test tree.
bool local_transport_shadow_testing_open_exit_prompt(
    GameSession& session,
    std::size_t player_index,
    int destination_level,
    bool withdraw)
{
    const auto runtime = session.local_transport_runtime_;
    if (runtime == nullptr || !runtime->authoritative_mode() ||
        runtime->server == nullptr ||
        runtime->server_session == nullptr ||
        runtime->server_session->game_.sim_events == nullptr)
        return false;

    walker* const control = runtime->server->player_control(player_index);
    if (control == nullptr)
        return false;
    control->set_skip_exit(10);
    runtime->server_session->game_.sim_events->push_with_text(
        og::sim::EventKind::RequestExitConfirmation,
        withdraw ? "Withdraw?" : "Exit Field?",
        static_cast<std::uint32_t>(destination_level),
        withdraw ? 1u : 0u);
    return true;
}
