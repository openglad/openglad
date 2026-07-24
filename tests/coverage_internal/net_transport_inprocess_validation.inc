// Included by net_transport_inprocess.cpp inside namespace og::sim and only
// for TESTING builds. Keeping the deliberate mismatch orchestration under
// tests/ prevents it from being counted as production-source coverage.
std::array<std::string, 16>
inprocess_transport_validation_diagnostics_for_testing()
{
    const auto render_failure = []<typename T>(std::string_view field,
                                                const T& expected,
                                                const T& actual) {
        TransportValidationFailure failure;
        if (compare_transport_value(field, expected, actual, failure))
            throw std::logic_error("test mismatch unexpectedly compared equal");

        try
        {
            throw_validation_failure("diagnostic probe", failure);
        }
        catch (const std::runtime_error& error)
        {
            return std::string(error.what());
        }

        throw std::logic_error("validation failure did not throw");
    };

    const auto render_detected_failure = [](std::string_view label,
                                             const auto& failure) {
        if (!failure.has_value())
            throw std::logic_error("test mismatch was not detected");
        try
        {
            throw_validation_failure(label, *failure);
        }
        catch (const std::runtime_error& error)
        {
            return std::string(error.what());
        }
        throw std::logic_error("detected validation failure did not throw");
    };

    InputState input{};
    InputState different_input = input;
    const std::string input_tick = render_detected_failure(
        "input probe", find_first_input_difference(4, input, 5, input));
    different_input.quit_requested = true;
    const std::string input_quit = render_detected_failure(
        "input probe", find_first_input_difference(4, input, 4,
                                                     different_input));
    different_input = input;
    different_input.timer_wait_request = 2;
    const std::string input_timer = render_detected_failure(
        "input probe", find_first_input_difference(4, input, 4,
                                                     different_input));
    different_input = input;
    different_input.players[0].held[0] = true;
    const std::string input_held = render_detected_failure(
        "input probe", find_first_input_difference(4, input, 4,
                                                     different_input));
    different_input = input;
    different_input.players[0].pressed[0] = true;
    const std::string input_pressed = render_detected_failure(
        "input probe", find_first_input_difference(4, input, 4,
                                                     different_input));

    SimEventBatch batch{};
    batch.sequence = 7;
    batch.events.push_back(Event{.tick = 9,
                                 .kind = EventKind::Notification,
                                 .a = 10,
                                 .b = 11,
                                 .text = "expected"});
    SimEventBatch different_batch = batch;
    different_batch.sequence = 8;
    const std::string event_sequence = render_detected_failure(
        "event probe", find_first_event_batch_difference(batch,
                                                           different_batch));
    different_batch = batch;
    different_batch.events.clear();
    const std::string event_size = render_detected_failure(
        "event probe", find_first_event_batch_difference(batch,
                                                           different_batch));
    different_batch = batch;
    different_batch.events[0].tick++;
    const std::string event_tick = render_detected_failure(
        "event probe", find_first_event_batch_difference(batch,
                                                           different_batch));
    different_batch = batch;
    different_batch.events[0].kind = EventKind::SetPalette;
    const std::string event_kind = render_detected_failure(
        "event probe", find_first_event_batch_difference(batch,
                                                           different_batch));
    different_batch = batch;
    different_batch.events[0].a++;
    const std::string event_a = render_detected_failure(
        "event probe", find_first_event_batch_difference(batch,
                                                           different_batch));
    different_batch = batch;
    different_batch.events[0].b++;
    const std::string event_b = render_detected_failure(
        "event probe", find_first_event_batch_difference(batch,
                                                           different_batch));
    different_batch = batch;
    different_batch.events[0].text = "actual";
    const std::string event_text = render_detected_failure(
        "event probe", find_first_event_batch_difference(batch,
                                                           different_batch));

    return {
        render_failure("event.kind", EventKind::Notification,
                       EventKind::SetPalette),
        render_failure("signed_byte", static_cast<signed char>(-2),
                       static_cast<signed char>(7)),
        render_failure("boolean", false, true),
        render_failure("integer", 3, 4),
        input_tick,
        input_quit,
        input_timer,
        input_held,
        input_pressed,
        event_sequence,
        event_size,
        event_tick,
        event_kind,
        event_a,
        event_b,
        event_text,
    };
}
