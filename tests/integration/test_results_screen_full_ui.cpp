#include <openglad/core/test_trace.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/sound.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/platform/sai2x.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

int get_num_foes(LevelRuntimeData& level, short viewer_team);
Uint32 get_time_bonus(int playernum);

void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

namespace
{
struct ResultsThreadState
{
    bool started = false;
    bool finished = false;
    // The injector synchronised on a LIVE panel loop before its first press,
    // and every press/wheel notch it sent was sampled by that loop: without
    // these the panel's exact pins would read one short on a slow load and
    // blame the product.
    bool loop_seen = false;
    bool clicks_delivered = true;
};

struct CanvasRoutingGuard
{
    int zoom_steps = E_Screen->world_zoom_steps();
    og::WorldScaleMode smoothing = E_Screen->world_scale().mode;
    CanvasTarget target = E_Screen->active_canvas();
    int window_w = static_cast<int>(og::runtime::current_session->window_w_);
    int window_h = static_cast<int>(og::runtime::current_session->window_h_);

    ~CanvasRoutingGuard()
    {
        E_Screen->set_world_zoom(zoom_steps, smoothing, window_w, window_h);
        E_Screen->set_active_canvas(target);
    }
};

// ---------------------------------------------------------------------------
// Injector synchronisation for the modal results panel (no wall-clock pacing).
//
// results_screen sleeps, pumps events and builds a fresh LevelRuntimeData
// before its button loop is live, so a flat delay before the first press
// RACES that load: on a loaded machine (and always on the coverage/ASan lanes)
// a held-for-60 ms press can come and go before the loop ever samples the
// mouse, and every exact pin below then reads one short.  The loop publishes
// two TESTING seams for exactly this (results_screen.h):
// results_screen_testing_loop_live() and results_screen_testing_frame_count(),
// the latter bumped at the TOP of each iteration, before the mouse sample.
// Every wait here is therefore a CONDITION with a failure bound, never a
// settle, and every expiry prints why.
// ---------------------------------------------------------------------------

template <typename Pred>
static bool results_wait(Pred pred, int timeout_ms)
{
    const Uint64 start = SDL_GetTicks();
    while (!pred())
    {
        if (SDL_GetTicks() - start > static_cast<Uint64>(timeout_ms))
            return false;
        SDL_Delay(2);
    }
    return true;
}

// The panel is live and has begun at least one iteration — only then can a
// synthetic press be sampled at all.
static bool results_wait_for_live_loop(ResultsThreadState* st, const char* who)
{
    st->loop_seen = results_wait(
        [] {
            return results_screen_testing_loop_live() &&
                   results_screen_testing_frame_count() >= 1;
        },
        5000);
    if (!st->loop_seen)
    {
        std::fprintf(stderr,
                     "[results injector] %s: the results loop never went live "
                     "(live=%d frames=%d) within 5000 ms\n",
                     who, results_screen_testing_loop_live() ? 1 : 0,
                     results_screen_testing_frame_count());
        std::fflush(stderr);
    }
    return st->loop_seen;
}

// A press is DELIVERED only once the loop has sampled it: write the button
// down, then let the frame counter advance by two.  The counter is bumped at
// the top of an iteration, so an iteration whose bump is observed after the
// write is guaranteed to sample the write.  Then release and let it advance by
// two again, so the release is sampled too and the next press arrives as a
// fresh unpressed->pressed edge for the loop's `was_mouse_down` detector.
// Each leg is bounded at 2 s; the "loop no longer live" escape is what lets
// the OK press — the one that ENDS the loop — return.
static bool inject_results_click(int game_x, int game_y, ResultsThreadState* st,
                                 const char* who)
{
    MouseState& mouse = query_mouse_no_poll();
    mouse.x = static_cast<float>(game_x);
    mouse.y = static_cast<float>(game_y);
    mouse.left = true;
    const int pressed_at = results_screen_testing_frame_count();
    const bool press_seen = results_wait(
        [pressed_at] {
            return results_screen_testing_frame_count() >= pressed_at + 2 ||
                   !results_screen_testing_loop_live();
        },
        2000);
    mouse.left = false;
    const int released_at = results_screen_testing_frame_count();
    const bool release_seen =
        press_seen && results_wait(
                          [released_at] {
                              return results_screen_testing_frame_count() >=
                                         released_at + 2 ||
                                     !results_screen_testing_loop_live();
                          },
                          2000);
    if (!release_seen)
    {
        std::fprintf(stderr,
                     "[results injector] %s: press at (%d,%d) was never sampled "
                     "(press_seen=%d live=%d frames=%d)\n",
                     who, game_x, game_y, press_seen ? 1 : 0,
                     results_screen_testing_loop_live() ? 1 : 0,
                     results_screen_testing_frame_count());
        std::fflush(stderr);
        st->clicks_delivered = false;
    }
    return release_seen;
}

// Same handshake for a wheel notch: push it, then let a whole iteration's
// get_input_events(POLL) run so the loop's scroll accumulator drains it.
static bool push_results_wheel(int notch, ResultsThreadState* st, const char* who)
{
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = static_cast<float>(notch);
    wheel.wheel.integer_y = notch;
    SDL_PushEvent(&wheel);
    const int pushed_at = results_screen_testing_frame_count();
    const bool drained = results_wait(
        [pushed_at] {
            return results_screen_testing_frame_count() >= pushed_at + 2 ||
                   !results_screen_testing_loop_live();
        },
        2000);
    if (!drained)
    {
        std::fprintf(stderr,
                     "[results injector] %s: wheel notch %d was never polled\n",
                     who, notch);
        std::fflush(stderr);
        st->clicks_delivered = false;
    }
    return drained;
}

// Failure bound only: a button press is what must end the panel, so the
// world().end write here is never the expected exit (each flow pins
// "exit ok_click" and the ABSENCE of "exit world_end").
static void results_failsafe_end(ResultsThreadState* st, const char* who)
{
    if (results_wait([] { return !results_screen_testing_loop_live(); }, 5000))
        return;
    std::fprintf(stderr,
                 "[results injector] %s: the loop outlived every injected press; "
                 "ending the world so the test fails on assertions\n",
                 who);
    std::fflush(stderr);
    st->clicks_delivered = false;
    og::runtime::current_session->myscreen_->world().end = 1;
}

static int results_ui_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    if (results_wait_for_live_loop(st, "results_ui_injector"))
    {
        push_results_wheel(1, st, "results_ui_injector");
        push_results_wheel(-1, st, "results_ui_injector");

        // Toggle tabs in the full results UI.
        inject_results_click(220, 26, st, "TROOPS");
        for (int i = 0; i < 3; ++i)
            inject_results_click(70, 26, st, "OVERVIEW");
        inject_results_click(220, 26, st, "TROOPS again");

        // Exit via OK.
        inject_results_click(130, 170, st, "OK");
    }

    results_failsafe_end(st, "results_ui_injector");

    st->finished = true;
    return 0;
}

static int results_ui_scroll_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    if (results_wait_for_live_loop(st, "results_ui_scroll_injector"))
    {
        inject_results_click(225, 26, st, "TROOPS");

        for (int i = 0; i < 12; ++i)
            push_results_wheel(-1, st, "results_ui_scroll_injector");
        for (int i = 0; i < 4; ++i)
            push_results_wheel(1, st, "results_ui_scroll_injector");

        inject_results_click(132, 171, st, "OK");
    }

    results_failsafe_end(st, "results_ui_scroll_injector");

    st->finished = true;
    return 0;
}

static int results_ui_ok_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    if (results_wait_for_live_loop(st, "results_ui_ok_injector"))
        inject_results_click(130, 170, st, "OK");

    results_failsafe_end(st, "results_ui_ok_injector");

    st->finished = true;
    return 0;
}

static int results_ui_retry_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    if (results_wait_for_live_loop(st, "results_ui_retry_injector"))
    {
        for (int i = 0; i < 6; ++i)
            inject_results_click(187, 171, st, "RETRY");
    }

    // The networked flow SUPPRESSES the retry button, so no press there can
    // end the panel and ending the world is that test's expected exit — not a
    // failsafe.  In the local flow the accepted prompt has already ended the
    // loop by now, so this write never runs.
    if (results_screen_testing_loop_live())
        og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}

static int results_ui_troops_then_ok_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    if (results_wait_for_live_loop(st, "results_ui_troops_then_ok_injector"))
    {
        for (int i = 0; i < 4; ++i)
            inject_results_click(220, 26, st, "TROOPS");
        inject_results_click(130, 170, st, "OK");
    }

    results_failsafe_end(st, "results_ui_troops_then_ok_injector");

    st->finished = true;
    return 0;
}

// #237: gameplay -> results is a context switch, so the panel fades — one
// fade-out of the mission frame, one fade-in at the loop's first present, and
// (the ownership rule) one fade-out of its own last frame at exit, so the
// teardown fade that follows in go_menu finds a black window and skips. Under
// TESTING each fadeblack that runs traces exactly one FadeBetween line.
int count_fade_between_traces()
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int fades = 0;
    for (const TraceEntry& entry : g_trace_buffer)
    {
        if (entry.category == "video" &&
            entry.message.find("FadeBetween") != std::string::npos)
        {
            ++fades;
        }
    }
    return fades;
}


// The results panel's pages, rows and totals are drawn text; the TESTING
// traces placed at those draw sites are their only observable, so this counts
// the ones that match a substring.
int count_result_traces(const char* substring)
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int found = 0;
    for (const TraceEntry& entry : g_trace_buffer)
    {
        if (entry.category == "results" &&
            entry.message.find(substring) != std::string::npos)
        {
            ++found;
        }
    }
    return found;
}


// Every button press the results loop CONSUMES answers with SOUND_BOW
// (results_screen.cpp, the do_ok/do_retry/do_overview/do_troops arms), so a
// counting soundob is the free proof that an injected click landed on a
// button rect — as opposed to missing it and letting the injector's
// `world().end = 1` failsafe end the loop with nothing consumed.
class BowCountingSound : public soundob
{
public:
    explicit BowCountingSound(std::unique_ptr<soundob> inner)
        : inner_(std::move(inner))
    {
    }

    void play_sound(short whichsound) override
    {
        if (whichsound == SOUND_BOW)
            bows_.fetch_add(1);
        if (inner_)
            inner_->play_sound(whichsound);
    }

    unsigned char set_sound(bool silent) override
    {
        return inner_ ? inner_->set_sound(silent) : 0;
    }

    int bows() const { return bows_.load(); }
    std::unique_ptr<soundob> release_inner() { return std::move(inner_); }

private:
    std::unique_ptr<soundob> inner_;
    std::atomic<int> bows_{0};
};

struct ScopedBowCounter
{
    ScopedBowCounter()
    {
        screen& s = *og::runtime::current_session->myscreen_;
        auto counter = std::make_unique<BowCountingSound>(std::move(s.soundp));
        counter_ = counter.get();
        s.soundp = std::move(counter);
    }

    ~ScopedBowCounter()
    {
        screen& s = *og::runtime::current_session->myscreen_;
        s.soundp = counter_->release_inner();
    }

    int bows() const { return counter_->bows(); }

    BowCountingSound* counter_ = nullptr;
};

} // namespace

// A click in troops_rect selects the TROOPS page (mode = 1) and a click in
// overview_rect selects the OVERVIEW page (mode = 0) — the two draw different
// pages, and which one a press selected is what the "page mode=N" trace at
// each handler reports. The injector presses TROOPS, OVERVIEW three times and
// TROOPS again, so the panel must report exactly two mode=1 and three mode=0
// selections; the bow counter cannot tell the two tabs apart. OK — not the
// injector's failsafe — must be what ends the loop. #237 ownership rides
// along: the panel is a context switch, so it owns exactly three fades and
// leaves a black window behind.
TEST(ResultsScreenFullUi, overview_and_troops_clicks_select_their_page)
{
    CanvasRoutingGuard canvas_guard;
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    // This test needs a deterministic split 640x400 World buffer regardless
    // of the physical mode left by earlier menu tests.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai, 320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    constexpr Uint32 kWorldPixel = 0x00123456u;
    SDL_FillSurfaceRect(E_Screen->render, nullptr, kWorldPixel);

    // Ensure deterministic campaign/level context used by results_screen internals.
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.current_levels.clear();
    og::runtime::current_session->myscreen_->save_data.m_score[0] = 200;
    og::runtime::current_session->myscreen_->save_data.m_score[1] = 50;

    // Build before/after maps with mixed outcomes: gain level, loss, recruit.
    std::map<int, guy*> before;
    std::map<int, walker*> after;

    auto* w1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    auto* w2 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_MAGE);
    auto* w3 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_TRUE(w1 != nullptr && w2 != nullptr && w3 != nullptr) << "expected walkers for results test";
    w1->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w2->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    w3->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHER));

    static guy b1(FAMILY_SOLDIER);
    static guy b2(FAMILY_MAGE);
    static guy b3(FAMILY_ARCHER);
    b1.name = "Alpha";
    b1.family = FAMILY_SOLDIER;
    b1.level = 2;
    b1.exp = calculate_exp(2) + 10;

    b2.name = "Beta";
    b2.family = FAMILY_MAGE;
    b2.level = 3;
    b2.exp = calculate_exp(3) + 5;

    b3.name = "Delta";
    b3.family = FAMILY_ARCHER;
    b3.level = 2;
    b3.exp = calculate_exp(2) + 25;

    before[1] = &b1;
    before[2] = &b2;
    before[4] = &b3;

    w1->myguy->name = "Alpha";
    w1->myguy->family = FAMILY_SOLDIER;
    w1->myguy->exp = calculate_exp(3) + 20;  // level gain
    w1->myguy->scen_kills = 4;
    w1->myguy->scen_damage = 30;
    w1->myguy->scen_damage_taken = 2;
    w1->myguy->scen_min_hp = 1;
    w1->stats()->set_max_hitpoints(10);
    w1->stats()->set_hitpoints(6);

    w2->myguy->name = "Beta";
    w2->myguy->family = FAMILY_MAGE;
    w2->myguy->exp = calculate_exp(2);        // level loss
    w2->myguy->scen_kills = 1;
    w2->myguy->scen_damage = 3;
    w2->myguy->scen_damage_taken = 20;
    w2->myguy->scen_min_hp = 0;
    w2->stats()->set_max_hitpoints(10);
    w2->stats()->set_hitpoints(0);

    w3->myguy->name = "Gamma";
    w3->myguy->family = FAMILY_ARCHER;
    w3->myguy->exp = calculate_exp(1) + 30;   // recruit (no before entry)
    w3->myguy->scen_kills = 2;
    w3->myguy->scen_damage = 8;
    w3->myguy->scen_damage_taken = 1;
    w3->myguy->scen_min_hp = 2;
    w3->stats()->set_max_hitpoints(10);
    w3->stats()->set_hitpoints(9);

    after[1] = w1;
    after[2] = w2;
    after[3] = w3;

    // Force full UI loop; default TESTING path bypasses it.
    results_screen_testing_set_force_full(true);

    int bows = 0;
    bool retry = true;
    {
        ScopedBowCounter clicks;
        ResultsThreadState st{};
        SDL_Thread* thread = SDL_CreateThread(results_ui_injector, "results_ui_injector", &st);
        ASSERT_TRUE(thread != nullptr) << "failed to create results injector thread";

        trace_clear();
        retry = results_screen(0, 2, before, after);

        int rc = 0;
        SDL_WaitThread(thread, &rc);
        bows = clicks.bows();
        ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the results UI injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    }

    results_screen_testing_set_force_full(false);
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(!retry) << "OK path should not request retry";
    EXPECT_EQ(6, bows)
        << "each injected press (TROOPS, OVERVIEW x3, TROOPS, OK) must be "
           "consumed by a results button, not fall between the rects";
    EXPECT_EQ(2, count_result_traces("page mode=1"))
        << "the two TROOPS presses must each select the troops page";
    EXPECT_EQ(3, count_result_traces("page mode=0"))
        << "the three OVERVIEW presses must each select the overview page — "
           "a tab handler that no longer writes its page still plays its bow";
    EXPECT_TRUE(trace_contains("results", "exit ok_click"))
        << "the OK button is what ends the panel";
    EXPECT_FALSE(trace_contains("results", "exit world_end"))
        << "the injector's world().end failsafe must never be the exit";
    EXPECT_EQ(3, count_fade_between_traces())
        << "#237: the results panel is a context switch — exactly one "
           "fade-out, the first-frame fade-in, and its own exit fade-out";
    EXPECT_TRUE(og::runtime::current_session->myscreen_->window_is_black())
        << "the panel's exit leaves the window black for go_menu's teardown";
    EXPECT_EQ(CanvasTarget::World, E_Screen->active_canvas());
    EXPECT_EQ(640, E_Screen->render->w);
    EXPECT_EQ(400, E_Screen->render->h);
    const auto* world_pixels = reinterpret_cast<const Uint32*>(E_Screen->render->pixels);
    EXPECT_EQ(kWorldPixel, world_pixels[60 + 25 * (E_Screen->render->pitch / 4)])
        << "results chrome must render on the fixed UI canvas, not the world";
}


// The TROOPS tab press must SELECT the troops page (mode = 1, reported by the
// "page mode=N" trace at the handler), the page must then draw troop rows,
// the wheel events must be drained by the loop's scroll accumulator
// (results_screen.cpp `scroll -= get_and_reset_scroll_amount()`), and OK —
// not the failsafe — must end the panel.
TEST(ResultsScreenFullUi, troops_press_opens_the_troop_page_and_drains_the_wheel)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.current_levels.clear();
    og::runtime::current_session->myscreen_->save_data.m_score[0] = 300;
    og::runtime::current_session->myscreen_->save_data.m_score[1] = 75;
    og::runtime::current_session->myscreen_->world().time_bonus_limit = 500;
    og::runtime::current_session->myscreen_->world().par_value = 3;
    og::runtime::current_session->myscreen_->framecount = 10;
    og::runtime::current_session->myscreen_->world().set_level_tick_count(10);
    og::runtime::current_session->myscreen_->special_name[FAMILY_MAGE][2] = "Arcane Burst";

    std::map<int, guy*> before;
    std::map<int, walker*> after;
    std::vector<std::unique_ptr<guy>> before_storage;
    before_storage.reserve(6);

    for (int i = 0; i < 8; ++i)
    {
        auto* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, (i % 2 == 0) ? FAMILY_MAGE : FAMILY_SOLDIER);
        ASSERT_TRUE(w != nullptr) << "expected walker for scrolling results test";
        w->set_owned_myguy(std::make_unique<guy>((i % 2 == 0) ? FAMILY_MAGE : FAMILY_SOLDIER));
        w->myguy->name = std::string("Troop") + std::to_string(i);
        w->myguy->family = (i % 2 == 0) ? FAMILY_MAGE : FAMILY_SOLDIER;
        w->myguy->scen_kills = static_cast<short>(i + 1);
        w->myguy->scen_damage = static_cast<float>(10 + i);
        w->myguy->scen_damage_taken = static_cast<float>(i);
        w->myguy->scen_min_hp = (i == 2) ? 0 : 2;
        w->stats()->set_max_hitpoints(10);
        w->stats()->set_hitpoints((i == 2) ? 0 : 7);

        if (i < 6)
        {
            auto before_guy = std::make_unique<guy>((i % 2 == 0) ? FAMILY_MAGE : FAMILY_SOLDIER);
            before_guy->name = std::string("Troop") + std::to_string(i);
            before_guy->family = (i % 2 == 0) ? FAMILY_MAGE : FAMILY_SOLDIER;
            before_guy->level = (i == 0) ? 3 : ((i == 1) ? 5 : 2);
            before_guy->exp = calculate_exp(before_guy->level) + 5;
            before[i + 1] = before_guy.get();
            before_storage.push_back(std::move(before_guy));
        }

        if (i == 0)
        {
            w->myguy->exp = calculate_exp(4) + 20; // level up + gained special
        }
        else if (i == 1)
        {
            w->myguy->exp = calculate_exp(3); // level down
        }
        else
        {
            w->myguy->exp = calculate_exp(2) + 15; // steady progress
        }

        after[i + 1] = w;
    }

    results_screen_testing_set_force_full(true);

    (void)get_and_reset_scroll_amount(); // start from a drained accumulator

    int bows = 0;
    bool retry = true;
    {
        ScopedBowCounter clicks;
        ResultsThreadState st{};
        SDL_Thread* thread = SDL_CreateThread(results_ui_scroll_injector, "results_ui_scroll_injector", &st);
        ASSERT_TRUE(thread != nullptr) << "failed to create scroll injector thread";

        trace_clear();
        retry = results_screen(0, 2, before, after);

        int rc = 0;
        SDL_WaitThread(thread, &rc);
        bows = clicks.bows();
        ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the scroll injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    }

    results_screen_testing_set_force_full(false);
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(!retry) << "scrolling and OK should not request retry";
    EXPECT_EQ(2, bows)
        << "the TROOPS press and the OK press must both land on a button";
    EXPECT_EQ(1, count_result_traces("page mode=1"))
        << "the single TROOPS press must select the troops page";
    EXPECT_EQ(0, count_result_traces("page mode=0"))
        << "nothing in this flow presses OVERVIEW";
    EXPECT_GT(count_result_traces("troop_row Troop0 "), 0)
        << "the troops page must actually draw its rows";
    EXPECT_EQ(0, get_and_reset_scroll_amount())
        << "the panel loop must have drained every injected wheel event";
    EXPECT_TRUE(trace_contains("results", "exit ok_click"))
        << "the OK button is what ends the panel";
    EXPECT_FALSE(trace_contains("results", "exit world_end"))
        << "the injector's world().end failsafe must never be the exit";
}

// The defeat overview prints "<defeated> of <total> Foes Defeated": total is
// get_num_foes() over a FRESHLY LOADED copy of the level, defeated is that
// total minus get_num_foes() over the LIVE level, and only the ending != 0
// arm prints the total at all (the victory arm prints the defeated count
// alone). The drawn line is text, so the "overview_foes" trace at each arm is
// its observable; this test pins the whole traced triple for a defeat, after
// planting three extra live foes so a total-only or live-only reader is off
// by exactly those three. The counting rule itself is pinned by value first.
TEST(ResultsScreenFullUi, defeat_overview_reports_defeated_of_total_foes)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.current_levels.clear();
    og::runtime::current_session->myscreen_->save_data.m_score[0] = 75;

    std::map<int, guy*> before;
    std::map<int, walker*> after;

    // The overview's foe arithmetic, pinned by value on a hand-built level:
    // only the other team's LIVING, undead walkers count.
    {
        LevelRuntimeData foes(1, /*headless=*/true);
        ASSERT_EQ(0, get_num_foes(foes, 0))
            << "an empty level has no foes at all";
        auto* enemy_a = foes.world().add_ob(Order::Living, FAMILY_SOLDIER);
        auto* enemy_b = foes.world().add_ob(Order::Living, FAMILY_ARCHER);
        auto* friendly = foes.world().add_ob(Order::Living, FAMILY_SOLDIER);
        auto* corpse = foes.world().add_ob(Order::Living, FAMILY_SOLDIER);
        auto* prop = foes.world().add_ob(Order::Treasure, FAMILY_GOLD_BAR);
        ASSERT_TRUE(enemy_a && enemy_b && friendly && corpse && prop)
            << "expected walkers for the foe-count fixture";
        enemy_a->set_team_num(1);
        enemy_b->set_team_num(2);
        friendly->set_team_num(0);
        corpse->set_team_num(1);
        corpse->set_dead(1);
        prop->set_team_num(1);
        ASSERT_EQ(2, get_num_foes(foes, 0))
            << "foes are the other teams' living walkers: not my own team, "
               "not the dead, not treasure";
        ASSERT_EQ(3, get_num_foes(foes, 3))
            << "a viewer on a third team counts every other living walker";
    }

    // The live world carries whatever earlier cases left in it, so the two
    // inputs of the overview line are measured the way results_screen
    // measures them — live level for what is LEFT, a fresh load of the same
    // level for the TOTAL — and then three extra foes are planted, which must
    // move "defeated" down by exactly three.
    screen& live = *og::runtime::current_session->myscreen_;
    const short my_team = live.save_data.my_team;
    const int live_foes_before = get_num_foes(live.level_runtime_data(), my_team);
    int fresh_total = 0;
    {
        LevelRuntimeData original(live.level_runtime_data().world().id);
        original.load();
        fresh_total = get_num_foes(original, my_team);
    }

    std::vector<walker*> planted;
    for (int i = 0; i < 3; ++i)
    {
        walker* extra = live.level_runtime_data().world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, extra) << "expected a planted foe for the overview count";
        extra->set_team_num(static_cast<unsigned char>(my_team + 1));
        planted.push_back(extra);
    }
    ASSERT_EQ(live_foes_before + 3, get_num_foes(live.level_runtime_data(), my_team))
        << "the three planted foes must be visible to the live count";
    const std::string expected_foe_line = std::format(
        "overview_foes ending=1 line={} of {} Foes",
        fresh_total - (live_foes_before + 3), fresh_total);

    results_screen_testing_set_force_full(true);

    int bows = 0;
    bool retry = true;
    {
        ScopedBowCounter clicks;
        ResultsThreadState st{};
        SDL_Thread* thread = SDL_CreateThread(results_ui_ok_injector, "results_ui_ok_injector", &st);
        ASSERT_TRUE(thread != nullptr) << "failed to create OK injector thread";

        trace_clear();
        retry = results_screen(1, -1, before, after);

        int rc = 0;
        SDL_WaitThread(thread, &rc);
        bows = clicks.bows();
        ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the OK injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    }

    results_screen_testing_set_force_full(false);
    for (walker* extra : planted)
        live.level_runtime_data().world().remove_ob(extra);
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(!retry) << "defeat OK path should not request retry";
    EXPECT_EQ(1, bows) << "the OK press must land on the OK button";
    EXPECT_TRUE(trace_contains("results", expected_foe_line.c_str()))
        << "a defeat overview must print <total - live> of <total> foes; "
           "expected \"" << expected_foe_line << "\"";
    EXPECT_EQ(0, count_result_traces("overview_foes ending=0"))
        << "a defeat must not take the victory arm's defeated-only line";
    EXPECT_TRUE(trace_contains("results", "exit ok_click"))
        << "the OK button is what ends the defeat panel";
    EXPECT_FALSE(trace_contains("results", "exit world_end"))
        << "the injector's world().end failsafe must never be the exit";
}

TEST(ResultsScreenFullUi, retry_button_accepts_prompt_and_returns_retry)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.current_levels.clear();

    std::map<int, guy*> before;
    std::map<int, walker*> after;

    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    results_screen_testing_set_force_full(true);

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(results_ui_retry_injector, "results_ui_retry_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create retry injector thread";

    const bool retry = results_screen(0, 2, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);
    picker_testing_yes_or_no_queue_clear();
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the retry injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    ASSERT_TRUE(retry) << "accepted retry prompt should request retry";
}

TEST(ResultsScreenFullUi, networked_results_suppress_local_retry)
{
    auto* const session = og::runtime::current_session;
    ASSERT_NE(nullptr, session);
    const char saved_end = session->myscreen_->world().end;
    const bool saved_networked = session->networked_session_;
    session->myscreen_->world().end = 0;
    session->networked_session_ = true;

    session->myscreen_->save_data.current_campaign =
        "gladiator";
    session->myscreen_->save_data.scen_num = 1;
    session->myscreen_->save_data.current_levels.clear();

    std::map<int, guy*> before;
    std::map<int, walker*> after;

    // If the hidden button were still actionable, this affirmative response
    // would make the first injected RETRY click return true.
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    results_screen_testing_set_force_full(true);

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(
        results_ui_retry_injector, "networked_retry_injector", &st);
    ASSERT_TRUE(thread != nullptr)
        << "failed to create networked retry injector thread";

    const bool retry = results_screen(0, 2, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);
    picker_testing_yes_or_no_queue_clear();
    session->networked_session_ = saved_networked;
    session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the networked retry injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    EXPECT_FALSE(retry)
        << "a display-only network peer cannot roll back the committed result";
}

// Re-winning a level that is already in completed_levels must pay no time
// bonus: results_screen forces every bonuscash[i] and allbonuscash to 0,
// which suppresses the "+ N Time Bonus" line. allbonuscash is a local that
// only leaves the function as drawn text, so the "time_bonus total=U
// completed=D" trace taken right after the zeroing arm is its observable.
// Both legs run on the SAME fixture — a clock that would pay 280 — so the
// already-won run must report total=0 and the identical un-completed run
// must report total=280; a reader that always zeroes fails the second leg.
TEST(ResultsScreenFullUi, completed_victory_zeroes_the_time_bonus)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    auto& screen_ref = *og::runtime::current_session->myscreen_;
    screen_ref.save_data.current_campaign = "gladiator";
    screen_ref.save_data.scen_num = 1;
    screen_ref.save_data.current_levels.clear();
    screen_ref.save_data.completed_levels.clear();
    screen_ref.save_data.completed_levels[screen_ref.save_data.current_campaign].insert(
        screen_ref.save_data.scen_num);
    screen_ref.save_data.m_score[0] = 250;
    screen_ref.save_data.m_score[1] = 50;
    screen_ref.world().time_bonus_limit = 500;
    screen_ref.world().par_value = 4;
    screen_ref.framecount = 100;
    screen_ref.world().set_level_tick_count(100);

    ASSERT_TRUE(screen_ref.save_data.is_level_completed(screen_ref.save_data.scen_num))
        << "the fixture must be a re-win of an already completed level";
    // (1 + par/10) * (limit - ticks)/limit * score = 1.4 * 0.8 * 250 = 280.
    ASSERT_EQ(280u, get_time_bonus(0))
        << "the clock alone would pay a bonus here, so the suppression must "
           "come from the already-completed arm, not from an expired timer";

    std::map<int, guy*> before;
    std::map<int, walker*> after;

    results_screen_testing_set_force_full(true);

    int bows = 0;
    bool retry = true;
    {
        ScopedBowCounter clicks;
        ResultsThreadState st{};
        SDL_Thread* thread = SDL_CreateThread(results_ui_ok_injector, "results_completed_ok_injector", &st);
        ASSERT_TRUE(thread != nullptr) << "failed to create OK injector thread";

        trace_clear();
        retry = results_screen(0, 2, before, after);

        int rc = 0;
        SDL_WaitThread(thread, &rc);
        bows = clicks.bows();
        ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the OK injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    }

    results_screen_testing_set_force_full(false);
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(!retry) << "completed victory OK path should not request retry";
    EXPECT_EQ(1, bows) << "the OK press must land on the OK button";
    EXPECT_TRUE(trace_contains("results", "time_bonus total=0 completed=1"))
        << "a level already in completed_levels pays no time bonus";
    EXPECT_TRUE(trace_contains("results", "exit ok_click"))
        << "the OK button is what ends the panel";
    EXPECT_FALSE(trace_contains("results", "exit world_end"))
        << "the injector's world().end failsafe must never be the exit";

    // Positive control on the identical fixture: with the level no longer
    // completed the same clock must pay its 280, so "always zero" is not a
    // passing reading of the rule either.
    screen_ref.save_data.completed_levels.clear();
    ASSERT_FALSE(screen_ref.save_data.is_level_completed(screen_ref.save_data.scen_num))
        << "the control leg must be a first win";
    ASSERT_EQ(280u, get_time_bonus(0)) << "the control leg keeps the same clock";
    screen_ref.world().end = 0;
    results_screen_testing_set_force_full(true);

    int control_bows = 0;
    bool control_retry = true;
    {
        ScopedBowCounter clicks;
        ResultsThreadState st{};
        SDL_Thread* thread = SDL_CreateThread(results_ui_ok_injector, "results_first_win_ok_injector", &st);
        ASSERT_TRUE(thread != nullptr) << "failed to create control OK injector thread";

        trace_clear();
        control_retry = results_screen(0, 2, before, after);

        int rc = 0;
        SDL_WaitThread(thread, &rc);
        control_bows = clicks.bows();
        ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the control OK injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    }

    results_screen_testing_set_force_full(false);
    og::runtime::current_session->myscreen_->world().end = saved_end;

    EXPECT_FALSE(control_retry) << "the control OK path should not request retry";
    EXPECT_EQ(1, control_bows) << "the control OK press must land on the OK button";
    EXPECT_TRUE(trace_contains("results", "time_bonus total=280 completed=0"))
        << "a first win on the same clock must be paid its 280 time bonus";
    screen_ref.save_data.completed_levels.clear();
}

// Playtest bug C regression (MVP): in a decided CTF match the MVP pool is the
// WINNING team's rostered humans only. When the bots win (no rostered humans
// on the winning team) the MVP line is omitted entirely — the losing player's
// hero must never headline under the winner banner. Observed via the TESTING
// trace placed after the MVP selection (mvp_pick/mvp_none).
TEST(ResultsScreenFullUi, ctf_bots_win_omits_mvp_line)
{
    auto& screen_ref = *og::runtime::current_session->myscreen_;
    const char saved_end = screen_ref.world().end;
    const char saved_type = screen_ref.world().type;
    screen_ref.world().end = 0;

    screen_ref.save_data.current_campaign = "gladiator";
    screen_ref.save_data.scen_num = 1;
    screen_ref.save_data.current_levels.clear();

    screen_ref.world().type |= GameWorld::TYPE_SCRIPTED;
    screen_ref.world().mode = og::sim::ModeState{};
    screen_ref.world().mode.active = true;
    screen_ref.world().mode.init_attempted = true;
    screen_ref.world().mode.win_latched = true;
    screen_ref.world().mode.winner_team = 1; // bots won
    screen_ref.world().mode.winner_is_player = false;
    // One rostered human on the LOSING team with huge classic MVP points.
    std::map<int, guy*> before;
    std::map<int, walker*> after;
    auto* loser = screen_ref.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(loser != nullptr) << "expected walker for CTF MVP test";
    loser->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    loser->set_team_num(0);
    loser->myguy->name = "LOSERHERO";
    loser->myguy->family = FAMILY_SOLDIER;
    loser->myguy->exp = calculate_exp(2) + 5;
    loser->myguy->scen_damage = 40;
    loser->myguy->scen_damage_taken = 100;
    loser->myguy->scen_min_hp = 1;
    loser->stats()->set_max_hitpoints(10);
    loser->stats()->set_hitpoints(5);
    after[1] = loser;

    trace_clear();
    results_screen_testing_set_force_full(true);

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(results_ui_ok_injector, "results_ctf_mvp_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create OK injector thread";

    const bool retry = results_screen(0, 1, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);

    ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the OK injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    ASSERT_TRUE(!retry);
    EXPECT_TRUE(trace_contains("results", "mvp_none"))
        << "bots-win must leave the MVP unset (line omitted)";
    EXPECT_FALSE(trace_contains("results", "mvp_pick"))
        << "a losing-team human must not be picked as MVP";
    EXPECT_FALSE(trace_contains("results", "LOSERHERO"));

    screen_ref.world().mode = og::sim::ModeState{};
    screen_ref.world().type = saved_type;
    screen_ref.world().end = saved_end;
}

// Scripted-mode twin of the CTF bots-win case: in a decided scripted match
// the MVP pool is the WINNING team's rostered humans only, and the overview
// page draws the generic winner banner + the mode's own scoreboard line
// (HUD slot 0 verbatim) at the CTF slot.
TEST(ResultsScreenFullUi, scripted_mode_win_scopes_mvp_and_draws_overview)
{
    auto& screen_ref = *og::runtime::current_session->myscreen_;
    const char saved_end = screen_ref.world().end;
    const char saved_type = screen_ref.world().type;
    screen_ref.world().end = 0;

    screen_ref.save_data.current_campaign = "gladiator";
    screen_ref.save_data.scen_num = 1;
    screen_ref.save_data.current_levels.clear();

    screen_ref.world().type |= GameWorld::TYPE_SCRIPTED;
    screen_ref.world().mode = og::sim::ModeState{};
    screen_ref.world().mode.active = true;
    screen_ref.world().mode.winner_team = 1; // bots won
    screen_ref.world().mode.winner_is_player = false;
    screen_ref.world().mode.hud[0].team = 1;
    std::strncpy(screen_ref.world().mode.hud[0].text.data(), "FRAGS 2:9",
                 screen_ref.world().mode.hud[0].text.size() - 1);

    // One rostered human on the LOSING team with huge classic MVP points.
    std::map<int, guy*> before;
    std::map<int, walker*> after;
    auto* loser = screen_ref.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(loser != nullptr) << "expected walker for mode MVP test";
    loser->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    loser->set_team_num(0);
    loser->myguy->name = "MODELOSER";
    loser->myguy->family = FAMILY_SOLDIER;
    loser->myguy->exp = calculate_exp(2) + 5;
    loser->myguy->scen_damage = 40;
    loser->myguy->scen_damage_taken = 100;
    loser->myguy->scen_min_hp = 1;
    loser->stats()->set_max_hitpoints(10);
    loser->stats()->set_hitpoints(5);
    after[1] = loser;

    trace_clear();
    results_screen_testing_set_force_full(true);

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(
        results_ui_ok_injector, "results_mode_mvp_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create OK injector thread";

    const bool retry = results_screen(0, 1, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);

    ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the OK injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    ASSERT_TRUE(!retry);
    EXPECT_TRUE(trace_contains("results", "mvp_none"))
        << "bots-win must leave the MVP unset (line omitted)";
    EXPECT_FALSE(trace_contains("results", "mvp_pick"))
        << "a losing-team human must not be picked as MVP";
    EXPECT_TRUE(trace_contains("results", "mode_winner_banner team=1"))
        << "the overview page draws the generic winner banner";
    EXPECT_TRUE(trace_contains("results", "mode_winner_banner team=1 color=56"))
        << "a score team keeps its classic team*16+40 banner ramp";
    EXPECT_TRUE(trace_contains("results", "mode_scoreboard FRAGS 2:9"))
        << "the overview page draws the mode's own scoreboard line";

    screen_ref.world().mode = og::sim::ModeState{};
    screen_ref.world().type = saved_type;
    screen_ref.world().end = saved_end;
}

// The FFA twin: a fighter band winner (byte 16+c) banners in its ramp from
// the shared table. The old team*16+40 cast wrapped past the palette for
// every byte from 14 up, so byte 29 would have painted color 24
// (docs/ffa-design.md §4).
TEST(ResultsScreenFullUi, band_winner_banner_takes_its_fighter_ramp)
{
    auto& screen_ref = *og::runtime::current_session->myscreen_;
    const char saved_end = screen_ref.world().end;
    const char saved_type = screen_ref.world().type;
    screen_ref.world().end = 0;

    screen_ref.save_data.current_campaign = "gladiator";
    screen_ref.save_data.scen_num = 1;
    screen_ref.save_data.current_levels.clear();

    screen_ref.world().type |= GameWorld::TYPE_SCRIPTED;
    screen_ref.world().mode = og::sim::ModeState{};
    screen_ref.world().mode.active = true;
    // Byte 29 = fighter color index 13 = TEAL, ramp base 168.
    screen_ref.world().mode.winner_team =
        static_cast<std::int8_t>(kFfaTeamBase + 13);
    screen_ref.world().mode.winner_is_player = false;
    screen_ref.world().mode.hud[0].team =
        static_cast<std::uint8_t>(kFfaTeamBase + 13);
    std::strncpy(screen_ref.world().mode.hud[0].text.data(), "1ST TEAL 9",
                 screen_ref.world().mode.hud[0].text.size() - 1);

    std::map<int, guy*> before;
    std::map<int, walker*> after;

    trace_clear();
    results_screen_testing_set_force_full(true);

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(
        results_ui_ok_injector, "results_band_banner_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create OK injector thread";

    const bool retry = results_screen(0, 1, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);

    ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the OK injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    ASSERT_TRUE(!retry);
    EXPECT_TRUE(trace_contains("results", "mode_winner_banner team=29 color=168"))
        << "the band winner banners in the TEAL ramp, not a wrapped cast";
    EXPECT_TRUE(trace_contains("results", "mode_scoreboard 1ST TEAL 9"))
        << "the band leader line still draws its scoreboard text";

    screen_ref.world().mode = og::sim::ModeState{};
    screen_ref.world().type = saved_type;
    screen_ref.world().end = saved_end;
}

TEST(ResultsScreenFullUi, classic_mvp_ignores_foreign_company_team)
{
    auto& screen_ref = *og::runtime::current_session->myscreen_;
    const char saved_end = screen_ref.world().end;
    const char saved_type = screen_ref.world().type;
    const short saved_my_team = screen_ref.world().my_team;
    screen_ref.world().end = 0;
    screen_ref.world().type = static_cast<char>(
        screen_ref.world().type & ~GameWorld::TYPE_SCRIPTED);
    screen_ref.world().my_team = 0;

    screen_ref.save_data.current_campaign = "gladiator";
    screen_ref.save_data.scen_num = 1;
    screen_ref.save_data.current_levels.clear();

    std::map<int, guy*> before;
    std::map<int, walker*> after;
    auto* red = screen_ref.world().add_ob(Order::Living, FAMILY_SOLDIER);
    auto* yellow = screen_ref.world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_TRUE(red != nullptr && yellow != nullptr)
        << "expected walkers for classic MVP team test";

    red->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    red->set_team_num(0);
    red->myguy->name = "REDMVP";
    red->myguy->scen_damage = 1;
    red->myguy->scen_damage_taken = 1;
    red->myguy->scen_min_hp = 1;
    red->stats()->set_max_hitpoints(10);
    red->stats()->set_hitpoints(5);
    after[1] = red;

    yellow->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHER));
    yellow->set_team_num(1);
    yellow->myguy->name = "YELLOWOPPONENT";
    yellow->myguy->scen_damage = 1000;
    yellow->myguy->scen_damage_taken = 1000;
    yellow->myguy->scen_min_hp = 1;
    yellow->stats()->set_max_hitpoints(10);
    yellow->stats()->set_hitpoints(5);
    after[2] = yellow;

    trace_clear();
    results_screen_testing_set_force_full(true);

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(
        results_ui_ok_injector, "results_classic_mvp_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create OK injector thread";

    const bool retry = results_screen(0, 1, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);
    results_screen_testing_set_force_full(false);

    ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the OK injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    ASSERT_FALSE(retry);
    EXPECT_TRUE(trace_contains("results", "mvp_pick name=REDMVP team=0"));
    EXPECT_FALSE(trace_contains("results", "YELLOWOPPONENT"));

    screen_ref.world().type = saved_type;
    screen_ref.world().my_team = saved_my_team;
    screen_ref.world().end = saved_end;
}

// The TROOPS page draws one row per troop: a troop whose after-exp is below
// its before-exp shows a NEGATIVE XP gain (the red bar drawn leftwards), and
// a troop that gained a level shows the special names it earned from
// screen::special_name. Those rows are text and bars with no other seam, so
// the per-drawn-row "troop_row <name> xp=<+/-N> special=<name>" trace is what
// is pinned here: Bruise (level 5 -> 4) must report a negative gain and Glyph
// (level 3 -> 4) must report the Arcane Burst it gained. OK — not the
// injector's failsafe — must end the page.
TEST(ResultsScreenFullUi, troop_rows_show_negative_xp_and_gained_specials)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    auto& screen_ref = *og::runtime::current_session->myscreen_;
    screen_ref.save_data.current_campaign = "gladiator";
    screen_ref.save_data.scen_num = 1;
    screen_ref.save_data.current_levels.clear();
    screen_ref.save_data.m_score[0] = 150;
    screen_ref.world().time_bonus_limit = 200;
    screen_ref.world().par_value = 2;
    screen_ref.framecount = 30;
    screen_ref.world().set_level_tick_count(30);

    const std::string saved_special = screen_ref.special_name[FAMILY_MAGE][2];
    screen_ref.special_name[FAMILY_MAGE][2] = "Arcane Burst";

    std::map<int, guy*> before;
    std::map<int, walker*> after;
    std::vector<std::unique_ptr<guy>> before_storage;

    auto* gained = screen_ref.world().add_ob(Order::Living, FAMILY_MAGE);
    auto* lost = screen_ref.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(gained != nullptr && lost != nullptr) << "expected walkers for troop detail test";
    gained->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    lost->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));

    auto gained_before = std::make_unique<guy>(FAMILY_MAGE);
    gained_before->name = "Glyph";
    gained_before->family = FAMILY_MAGE;
    gained_before->level = 3;
    gained_before->exp = calculate_exp(3) + 10;
    before[1] = gained_before.get();
    before_storage.push_back(std::move(gained_before));

    gained->myguy->name = "Glyph";
    gained->myguy->family = FAMILY_MAGE;
    gained->myguy->exp = calculate_exp(4) + 40;
    gained->myguy->scen_kills = 1;
    gained->myguy->scen_damage = 12;
    gained->myguy->scen_damage_taken = 1;
    gained->myguy->scen_min_hp = 8;
    gained->stats()->set_max_hitpoints(10);
    gained->stats()->set_hitpoints(8);
    after[1] = gained;

    auto lost_before = std::make_unique<guy>(FAMILY_SOLDIER);
    lost_before->name = "Bruise";
    lost_before->family = FAMILY_SOLDIER;
    lost_before->level = 5;
    lost_before->exp = calculate_exp(5) + 120;
    before[2] = lost_before.get();
    before_storage.push_back(std::move(lost_before));

    lost->myguy->name = "Bruise";
    lost->myguy->family = FAMILY_SOLDIER;
    lost->myguy->exp = calculate_exp(4) + 5;
    lost->myguy->scen_kills = 2;
    lost->myguy->scen_damage = 6;
    lost->myguy->scen_damage_taken = 4;
    lost->myguy->scen_min_hp = 5;
    lost->stats()->set_max_hitpoints(10);
    lost->stats()->set_hitpoints(5);
    after[2] = lost;

    results_screen_testing_set_force_full(true);

    int bows = 0;
    bool retry = true;
    {
        ScopedBowCounter clicks;
        ResultsThreadState st{};
        SDL_Thread* thread = SDL_CreateThread(results_ui_troops_then_ok_injector, "results_troops_ok_injector", &st);
        ASSERT_TRUE(thread != nullptr) << "failed to create troop detail injector thread";

        trace_clear();
        retry = results_screen(0, 2, before, after);

        int rc = 0;
        SDL_WaitThread(thread, &rc);
        bows = clicks.bows();
        ASSERT_TRUE(st.started && st.finished && st.loop_seen && st.clicks_delivered)
        << "the troop detail injector must reach a LIVE results loop and have every "
           "press it sent sampled by that loop";
    }

    results_screen_testing_set_force_full(false);
    screen_ref.special_name[FAMILY_MAGE][2] = saved_special;
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(!retry) << "troop detail OK path should not request retry";
    EXPECT_EQ(5, bows)
        << "four TROOPS presses and the OK press must all land on a button";
    EXPECT_EQ(4, count_result_traces("page mode=1"))
        << "each TROOPS press must select the troops page";
    EXPECT_GT(count_result_traces("troop_row Bruise xp=-"), 0)
        << "a troop that fell from level 5 to level 4 must render a NEGATIVE "
           "XP gain, not its absolute value";
    EXPECT_EQ(0, count_result_traces("troop_row Bruise xp=+"))
        << "the lost level must never read as a gain";
    EXPECT_GT(count_result_traces("troop_row Glyph xp=+"), 0)
        << "the level-up troop must render a positive XP gain";
    EXPECT_EQ(count_result_traces("troop_row Bruise "),
              count_result_traces("troop_row Glyph "))
        << "this flow never scrolls, so both roster rows are inside the "
           "scroll area on every drawn troops frame — a row that stops "
           "drawing (or draws only on some frames) breaks the pair";
    EXPECT_GT(count_result_traces("special=Arcane Burst"), 0)
        << "a gained level must name the special it earned, read from "
           "screen::special_name[family][slot]";
    EXPECT_TRUE(trace_contains("results", "exit ok_click"))
        << "the OK button is what ends the troop page";
    EXPECT_FALSE(trace_contains("results", "exit world_end"))
        << "the injector's world().end failsafe must never be the exit";
}
