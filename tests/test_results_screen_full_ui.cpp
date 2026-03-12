#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/results_screen.h>

#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"

#include <map>
#include <memory>

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

static int results_ui_injector(void* data)
{
    og::runtime::ensure_thread_session();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    SDL_Delay(140);

    SDL_Event wheel{};
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.y = -1;
    SDL_PushEvent(&wheel);

    // Toggle tabs in the full results UI.
    inject_click(220, 26, 10); // TROOPS
    SDL_Delay(40);
    inject_click(70, 26, 10);  // OVERVIEW
    SDL_Delay(40);
    inject_click(220, 26, 10); // TROOPS again to execute both branches
    SDL_Delay(40);

    // Exit via OK.
    inject_click(130, 170, 10);

    // Failsafe in case click misses.
    SDL_Delay(500);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}

static int results_ui_scroll_injector(void* data)
{
    og::runtime::ensure_thread_session();
    ResultsThreadState* st = static_cast<ResultsThreadState*>(data);
    st->started = true;

    SDL_Delay(140);
    inject_click(225, 26, 10); // TROOPS
    SDL_Delay(40);

    for (int i = 0; i < 12; ++i)
    {
        SDL_Event wheel{};
        wheel.type = SDL_MOUSEWHEEL;
        wheel.wheel.y = -1;
        SDL_PushEvent(&wheel);
        SDL_Delay(15);
    }

    for (int i = 0; i < 4; ++i)
    {
        SDL_Event wheel{};
        wheel.type = SDL_MOUSEWHEEL;
        wheel.wheel.y = 1;
        SDL_PushEvent(&wheel);
        SDL_Delay(15);
    }

    SDL_Delay(80);
    inject_click(132, 171, 10); // OK

    SDL_Delay(400);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}
} // namespace

TEST(ResultsScreenFullUi, overview_and_troops_paths)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

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
    b1.name = "Alpha";
    b1.family = FAMILY_SOLDIER;
    b1.level = 2;
    b1.exp = calculate_exp(2) + 10;

    b2.name = "Beta";
    b2.family = FAMILY_MAGE;
    b2.level = 3;
    b2.exp = calculate_exp(3) + 5;

    before[1] = &b1;
    before[2] = &b2;

    w1->myguy->name = "Alpha";
    w1->myguy->family = FAMILY_SOLDIER;
    w1->myguy->exp = calculate_exp(3) + 20;  // level gain
    w1->myguy->scen_kills = 4;
    w1->myguy->scen_damage = 30;
    w1->myguy->scen_damage_taken = 2;
    w1->myguy->scen_min_hp = 1;
    w1->stats()->max_hitpoints = 10;
    w1->stats()->hitpoints = 6;

    w2->myguy->name = "Beta";
    w2->myguy->family = FAMILY_MAGE;
    w2->myguy->exp = calculate_exp(2);        // level loss
    w2->myguy->scen_kills = 1;
    w2->myguy->scen_damage = 3;
    w2->myguy->scen_damage_taken = 20;
    w2->myguy->scen_min_hp = 0;
    w2->stats()->max_hitpoints = 10;
    w2->stats()->hitpoints = 0;

    w3->myguy->name = "Gamma";
    w3->myguy->family = FAMILY_ARCHER;
    w3->myguy->exp = calculate_exp(1) + 30;   // recruit (no before entry)
    w3->myguy->scen_kills = 2;
    w3->myguy->scen_damage = 8;
    w3->myguy->scen_damage_taken = 1;
    w3->myguy->scen_min_hp = 2;
    w3->stats()->max_hitpoints = 10;
    w3->stats()->hitpoints = 9;

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
        w->stats()->max_hitpoints = 10;
        w->stats()->hitpoints = (i == 2) ? 0 : 7;

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
