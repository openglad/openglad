#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/platform_bridge.h>

#include <gtest/gtest.h>

#include <string>

TEST(SimEventLog, push_and_clear)
{
    og::sim::SimEventLog log;
    ASSERT_TRUE(log.empty());
    ASSERT_TRUE(log.size() == 0);

    log.current_tick_ = 1;
    log.push(og::sim::EventKind::SetPalette, 1, 0);
    ASSERT_TRUE(!log.empty());
    ASSERT_TRUE(log.size() == 1);

    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.tick == 1);
    ASSERT_TRUE(ev.kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(ev.a == 1);
    ASSERT_TRUE(ev.b == 0);

    log.clear();
    ASSERT_TRUE(log.empty());
}

TEST(SimEventLog, push_sound)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 5;
    log.push_sound(42);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::PlaySound);
    ASSERT_TRUE(ev.a == 42);
    ASSERT_TRUE(ev.tick == 5);
    ASSERT_TRUE(ev.target_player == -1) << "world sounds are heard by everyone";

    // A cue's clang carries the same seat its line does (#222/#230).
    log.push_sound(43, 6);
    ASSERT_TRUE(log.size() == 2);
    ASSERT_TRUE(log.events()[1].a == 43);
    ASSERT_TRUE(log.events()[1].target_player == 6);
}

TEST(SimEventLog, push_notification)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 10;
    log.push_notification("Hello, world!");

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::Notification);
    ASSERT_TRUE(ev.text == "Hello, world!");
    ASSERT_TRUE(ev.tick == 10);
}

TEST(SimEventLog, drain)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;
    log.push_sound(1);
    log.push_sound(2);
    log.push_sound(3);

    ASSERT_TRUE(log.size() == 3);

    auto drained = log.drain();
    ASSERT_TRUE(drained.size() == 3);
    ASSERT_TRUE(log.empty());
    ASSERT_TRUE(drained[0].a == 1);
    ASSERT_TRUE(drained[1].a == 2);
    ASSERT_TRUE(drained[2].a == 3);
}

TEST(SimEventLog, suppress_guard_blocks_pushes_and_restores_state)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 12;

    {
        og::sim::SimEventLogSuppressGuard guard(log);
        ASSERT_TRUE(log.suppressed());
        log.push_sound(7);
        log.push_notification("hidden");
        log.push(og::sim::EventKind::SetPalette, 1, 0);
        ASSERT_TRUE(log.empty());
    }

    ASSERT_TRUE(!log.suppressed());
    log.push_sound(9);
    ASSERT_TRUE(log.size() == 1);
    ASSERT_TRUE(log.events()[0].a == 9);
}

TEST(SimEventLog, tick_tracking)
{
    og::sim::SimEventLog log;
    ASSERT_TRUE(log.current_tick_ == 0);

    log.current_tick_ = 42;
    ASSERT_TRUE(log.current_tick_ == 42);

    log.push(og::sim::EventKind::SetPalette, 1, 0);
    ASSERT_TRUE(log.events()[0].tick == 42);

    log.current_tick_ = 43;
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);
    ASSERT_TRUE(log.events()[1].tick == 43);
}

TEST(SimEventLog, multiple_event_types)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;

    log.push_sound(10);
    log.push_notification("test msg");
    log.push(og::sim::EventKind::SetPalette, 1, 0);
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);

    ASSERT_TRUE(log.size() == 4);
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::PlaySound);
    ASSERT_TRUE(log.events()[1].kind == og::sim::EventKind::Notification);
    ASSERT_TRUE(log.events()[2].kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(log.events()[3].kind == og::sim::EventKind::RequestRedraw);
}

TEST(SimEventLog, platform_bridge_set_and_invoke_callbacks)
{
    int present_calls = 0;
    int played_sound = -1;
    const char* music_file = nullptr;
    int stop_calls = 0;
    int create_w = -1;
    int create_h = -1;

    PlatformBridge bridge;
    bridge.present_frame = [&present_calls]() { present_calls++; };
    bridge.play_sound = [&played_sound](int sound_id) { played_sound = sound_id; };
    bridge.play_music = [&music_file](const char* file) { music_file = file; };
    bridge.stop_music = [&stop_calls]() { stop_calls++; };
    bridge.create_surface = [&create_w, &create_h](int w, int h) -> video* {
        create_w = w;
        create_h = h;
        return nullptr;
    };

    set_platform_bridge(bridge);

    const PlatformBridge& active = platform_bridge();
    active.present_frame();
    active.play_sound(42);
    active.play_music("song.ogg");
    active.stop_music();
    ASSERT_TRUE(active.create_surface(320, 200) == nullptr);

    ASSERT_TRUE(present_calls == 1);
    ASSERT_TRUE(played_sound == 42);
    ASSERT_TRUE(music_file != nullptr);
    ASSERT_TRUE(std::string(music_file) == "song.ogg");
    ASSERT_TRUE(stop_calls == 1);
    ASSERT_TRUE(create_w == 320);
    ASSERT_TRUE(create_h == 200);

    set_platform_bridge({});
}
