// Test-only definitions included by curses_game_runtime.cpp under TESTING.
// Keeping them in tests/ ensures coverage reports classify orchestration as
// test code while retaining access to the translation unit's private session.

bool LocalCursesSession::inject_exit_prompt_for_testing(
    std::string prompt, std::uint32_t synchronized_score)
{
    const bool transition_hooks_accept =
        server_->on_exit_accepted &&
        server_->on_exit_accepted(2) &&
        server_->on_level_transition &&
        server_->on_level_transition(2);
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

    // Non-terminal events must be ignored by both terminal projections: they
    // neither latch an end nor leak text into the notification queue.
    og::sim::SimEventBatch ignored_batch;
    ignored_batch.events.push_back(og::sim::Event{
        .kind = og::sim::EventKind::PlaySound,
        .a = 1,
        .text = "",
    });
    PendingEnd ignored_end;
    std::vector<std::string> ignored_messages;
    collect_notifications(ignored_batch, ignored_messages);
    apply_game_flow_batch(
        ignored_batch, ignored_end, ignored_messages);
    return transition_hooks_accept &&
           server_save_.m_score[0] == synchronized_score &&
           pending_exit_prompt_ && !messages_.empty() &&
           !ignored_end.ended && ignored_messages.empty();
}

const GameWorld& LocalCursesSession::server_world_for_testing() const
{
    return server_world();
}

// The world the launch adopted from its MatchStage (#218): tests compare it
// against the world the solo picker's VIEW LEVEL preview staged, which is an
// identity only while both consume the same latched match seed.
const GameWorld* curses_game_runtime_testing_local_server_world(
    const CursesGameSession& session)
{
    const auto* const local = dynamic_cast<const LocalCursesSession*>(&session);
    return local != nullptr ? &local->server_world_for_testing() : nullptr;
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
