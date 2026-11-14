std::string text_protocol_testing_format_event_text(std::string_view text)
{
    og::sim::Event event;
    event.tick = 7;
    event.kind = og::sim::EventKind::Notification;
    event.a = 1;
    event.b = 2;
    event.target_player = 3;
    event.text = text;

    std::ostringstream os;
    json_event(os, event);
    return os.str();
}

// Deterministic literal-shape probe for the "mode" block (the json_ctf
// literal pin's scripted twin): tests hand-build a ModeState and byte-pin
// the emitted JSON without driving a whole session.
std::string text_protocol_testing_json_mode(const GameWorld& world)
{
    std::ostringstream os;
    json_mode(os, world);
    return os.str();
}

// One entity's "state" line, as the state command prints it: tests stage a
// possessed body or a ward by hand, which no protocol command can.
std::string text_protocol_testing_json_entity(const GameWorld& world,
                                              const walker& w, int index)
{
    std::ostringstream os;
    json_entity(os, world, &w, index);
    return os.str();
}
