#include <openglad/core/test_trace.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/platform/sai2x.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

namespace
{
struct ResultsThreadState
{
    bool started = false;
    bool finished = false;
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

static void inject_results_click(int game_x, int game_y, int delay_ms = 10)
{
    MouseState& mouse = query_mouse_no_poll();
    mouse.x = static_cast<float>(game_x);
    mouse.y = static_cast<float>(game_y);
    mouse.left = true;
    SDL_Delay(delay_ms < 60 ? 60 : delay_ms);
    mouse.left = false;
    SDL_Delay(20);
}

static int results_ui_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    SDL_Delay(140);

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1;
    wheel.wheel.integer_y = 1;
    SDL_PushEvent(&wheel);

    wheel.wheel.y = -1;
    wheel.wheel.integer_y = -1;
    SDL_PushEvent(&wheel);

    // Toggle tabs in the full results UI.
    inject_results_click(220, 26, 10); // TROOPS
    SDL_Delay(40);
    for (int i = 0; i < 3; ++i)
    {
        inject_results_click(70, 26, 10);  // OVERVIEW
        SDL_Delay(50);
    }
    inject_results_click(220, 26, 10); // TROOPS again to execute both branches
    SDL_Delay(40);

    // Exit via OK.
    inject_results_click(130, 170, 10);

    // Failsafe in case click misses.
    SDL_Delay(500);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}

static int results_ui_scroll_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    SDL_Delay(140);
    inject_results_click(225, 26, 10); // TROOPS
    SDL_Delay(40);

    for (int i = 0; i < 12; ++i)
    {
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.y = -1;
        wheel.wheel.integer_y = -1;
        SDL_PushEvent(&wheel);
        SDL_Delay(15);
    }

    for (int i = 0; i < 4; ++i)
    {
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.y = 1;
        wheel.wheel.integer_y = 1;
        SDL_PushEvent(&wheel);
        SDL_Delay(15);
    }

    SDL_Delay(80);
    inject_results_click(132, 171, 10); // OK

    SDL_Delay(400);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}

static int results_ui_ok_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    SDL_Delay(140);
    inject_results_click(130, 170, 10); // OK

    SDL_Delay(300);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}

static int results_ui_retry_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    SDL_Delay(140);
    for (int i = 0; i < 6; ++i)
    {
        inject_results_click(187, 171, 10); // RETRY
        SDL_Delay(60);
    }

    SDL_Delay(300);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}

static int results_ui_troops_then_ok_injector(void* data)
{
    og::runtime::current_session = og::runtime::primary_session.load();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    SDL_Delay(140);
    for (int i = 0; i < 4; ++i)
    {
        inject_results_click(220, 26, 10); // TROOPS
        SDL_Delay(70);
    }
    SDL_Delay(500);
    inject_results_click(130, 170, 10); // OK

    SDL_Delay(300);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}

} // namespace

TEST(ResultsScreenFullUi, overview_and_troops_paths)
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
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
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

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(results_ui_injector, "results_ui_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create results injector thread";

    const bool retry = results_screen(0, 2, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(st.started && st.finished) << "results UI injector should run";
    ASSERT_TRUE(!retry) << "OK path should not request retry";
    EXPECT_EQ(CanvasTarget::World, E_Screen->active_canvas());
    EXPECT_EQ(640, E_Screen->render->w);
    EXPECT_EQ(400, E_Screen->render->h);
    const auto* world_pixels = reinterpret_cast<const Uint32*>(E_Screen->render->pixels);
    EXPECT_EQ(kWorldPixel, world_pixels[60 + 25 * (E_Screen->render->pitch / 4)])
        << "results chrome must render on the fixed UI canvas, not the world";
}


TEST(ResultsScreenFullUi, troop_scroll_paths_cover_bonus_losses_and_specials)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
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
        if (!w)
            return;
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

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(results_ui_scroll_injector, "results_ui_scroll_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create scroll injector thread";

    const bool retry = results_screen(0, 2, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(st.started && st.finished) << "scroll injector should run";
    ASSERT_TRUE(!retry) << "scrolling and OK should not request retry";
}

TEST(ResultsScreenFullUi, defeat_overview_path_reports_foe_totals)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.current_levels.clear();
    og::runtime::current_session->myscreen_->save_data.m_score[0] = 75;

    std::map<int, guy*> before;
    std::map<int, walker*> after;

    results_screen_testing_set_force_full(true);

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(results_ui_ok_injector, "results_ui_ok_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create OK injector thread";

    const bool retry = results_screen(1, -1, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(st.started && st.finished) << "OK injector should run";
    ASSERT_TRUE(!retry) << "defeat OK path should not request retry";
}

TEST(ResultsScreenFullUi, retry_button_accepts_prompt_and_returns_retry)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
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

    ASSERT_TRUE(st.started && st.finished) << "retry injector should run";
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
        "org.openglad.gladiator";
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

    ASSERT_TRUE(st.started && st.finished)
        << "networked retry injector should run";
    EXPECT_FALSE(retry)
        << "a display-only network peer cannot roll back the committed result";
}

TEST(ResultsScreenFullUi, completed_victory_zeroes_bonus_cash)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    auto& screen_ref = *og::runtime::current_session->myscreen_;
    screen_ref.save_data.current_campaign = "org.openglad.gladiator";
    screen_ref.save_data.scen_num = 1;
    screen_ref.save_data.current_levels.clear();
    screen_ref.save_data.completed_levels.clear();
    screen_ref.save_data.completed_levels[screen_ref.save_data.current_campaign].insert(
        screen_ref.save_data.scen_num);
    screen_ref.save_data.m_score[0] = 250;
    screen_ref.save_data.m_score[1] = 50;
    screen_ref.world().time_bonus_limit = 500;
    screen_ref.world().par_value = 4;
    screen_ref.framecount = screen_ref.world().time_bonus_limit;
    screen_ref.world().set_level_tick_count(
        static_cast<std::uint32_t>(screen_ref.world().time_bonus_limit));

    std::map<int, guy*> before;
    std::map<int, walker*> after;

    results_screen_testing_set_force_full(true);

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(results_ui_ok_injector, "results_completed_ok_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create OK injector thread";

    const bool retry = results_screen(0, 2, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(st.started && st.finished) << "OK injector should run";
    ASSERT_TRUE(!retry) << "completed victory OK path should not request retry";
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

    screen_ref.save_data.current_campaign = "org.openglad.gladiator";
    screen_ref.save_data.scen_num = 1;
    screen_ref.save_data.current_levels.clear();

    screen_ref.world().type |= GameWorld::TYPE_CTF;
    screen_ref.world().ctf = og::sim::CtfState{};
    screen_ref.world().ctf.active = true;
    screen_ref.world().ctf.winner_team = 1; // bots won
    screen_ref.world().ctf.winner_is_player = false;
    screen_ref.world().ctf.team_active[0] = true;
    screen_ref.world().ctf.team_active[1] = true;

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

    ASSERT_TRUE(st.started && st.finished) << "OK injector should run";
    ASSERT_TRUE(!retry);
    EXPECT_TRUE(trace_contains("results", "mvp_none"))
        << "bots-win must leave the MVP unset (line omitted)";
    EXPECT_FALSE(trace_contains("results", "mvp_pick"))
        << "a losing-team human must not be picked as MVP";
    EXPECT_FALSE(trace_contains("results", "LOSERHERO"));

    screen_ref.world().ctf = og::sim::CtfState{};
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

    screen_ref.save_data.current_campaign = "org.openglad.gladiator";
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

    ASSERT_TRUE(st.started && st.finished) << "OK injector should run";
    ASSERT_TRUE(!retry);
    EXPECT_TRUE(trace_contains("results", "mvp_none"))
        << "bots-win must leave the MVP unset (line omitted)";
    EXPECT_FALSE(trace_contains("results", "mvp_pick"))
        << "a losing-team human must not be picked as MVP";
    EXPECT_TRUE(trace_contains("results", "mode_winner_banner team=1"))
        << "the overview page draws the generic winner banner";
    EXPECT_TRUE(trace_contains("results", "mode_scoreboard FRAGS 2:9"))
        << "the overview page draws the mode's own scoreboard line";

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
        screen_ref.world().type & ~GameWorld::TYPE_CTF);
    screen_ref.world().my_team = 0;

    screen_ref.save_data.current_campaign = "org.openglad.gladiator";
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

    ASSERT_TRUE(st.started && st.finished) << "OK injector should run";
    ASSERT_FALSE(retry);
    EXPECT_TRUE(trace_contains("results", "mvp_pick name=REDMVP team=0"));
    EXPECT_FALSE(trace_contains("results", "YELLOWOPPONENT"));

    screen_ref.world().type = saved_type;
    screen_ref.world().my_team = saved_my_team;
    screen_ref.world().end = saved_end;
}

TEST(ResultsScreenFullUi, troop_detail_paths_show_specials_and_negative_xp)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    auto& screen_ref = *og::runtime::current_session->myscreen_;
    screen_ref.save_data.current_campaign = "org.openglad.gladiator";
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

    ResultsThreadState st{};
    SDL_Thread* thread = SDL_CreateThread(results_ui_troops_then_ok_injector, "results_troops_ok_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create troop detail injector thread";

    const bool retry = results_screen(0, 2, before, after);

    int rc = 0;
    SDL_WaitThread(thread, &rc);

    results_screen_testing_set_force_full(false);
    screen_ref.special_name[FAMILY_MAGE][2] = saved_special;
    og::runtime::current_session->myscreen_->world().end = saved_end;

    ASSERT_TRUE(st.started && st.finished) << "troop detail injector should run";
    ASSERT_TRUE(!retry) << "troop detail OK path should not request retry";
}
