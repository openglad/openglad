// Test-only definitions included by curses_game_runtime.cpp under TESTING.
// Keeping them in tests/ ensures coverage reports classify orchestration as
// test code while retaining access to the translation unit's private session.

bool LocalCursesSession::inject_exit_prompt_for_testing(
    std::string prompt, std::uint32_t synchronized_score)
{
    server_world().m_score[0] = synchronized_score;
    if (!server_->on_save_sync)
        return false;
    server_->on_save_sync();

    og::sim::ExitPromptBroadcastMessage message;
    message.destination_level = 2;
    message.prompt_text = std::move(prompt);
    server_transport_->send_exit_prompt_broadcast(
        peer_id_,
        std::make_shared<og::sim::ExitPromptBroadcastMessage>(
            std::move(message)));
    og::sim::SimEventBatch notification_batch;
    notification_batch.sequence = 1;
    notification_batch.events.push_back(og::sim::Event{
        .kind = og::sim::EventKind::Notification,
        .text = "Runtime callback notification",
    });
    server_transport_->send_game_flow_event_batch(
        peer_id_, std::make_shared<og::sim::SimEventBatch>(
                      std::move(notification_batch)));
    current_game = &client_ctx_;
    client_->poll_messages();
    return server_save_.m_score[0] == synchronized_score &&
           pending_exit_prompt_ && !messages_.empty();
}

bool curses_game_runtime_testing_inject_exit_prompt(
    CursesGameSession& session, std::string prompt,
    std::uint32_t synchronized_score)
{
    auto* const local = dynamic_cast<LocalCursesSession*>(&session);
    return local != nullptr &&
           local->inject_exit_prompt_for_testing(
               std::move(prompt), synchronized_score);
}
