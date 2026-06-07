#include <openglad/interface/ui/campaign_picker.h>
#include <openglad/interface/ui/level_picker.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/io.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"

#include <map>
#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

// level_picker.cpp helpers
bool isDir(const std::string& filename);
bool sort_scen(const std::string& first, const std::string& second);
// campaign_picker.cpp helper
int toInt(const std::string& s);
// results_screen.cpp helper
void show_ending_popup(int ending, int nextlevel);

namespace
{
void cleanup_leftover_test_campaigns()
{
    // Prior failed/aborted runs can leave behind test campaign packages in
    // ~/.openglad/campaigns. Some of those are intentionally malformed and can
    // cause picker flows to hang while enumerating/loading campaigns.
    //
    // Keep this narrow: only delete known-hazard prefixes created by tests.
    for (const auto& id : list_campaigns())
    {
        if (id.rfind("org.openglad.test.invalid_yaml.", 0) == 0 ||
            id.rfind("org.openglad.test.missing_yaml.", 0) == 0)
        {
            delete_campaign(id);
        }
    }
}
} // namespace

static int hold_q_key_for_picker(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    int numkeys = 0;
    Uint8* keys = const_cast<Uint8*>(SDL_GetKeyboardState(&numkeys));
    SDL_Scancode sc = SDL_GetScancodeFromKey(SDLK_q);
    if (sc >= 0 && sc < numkeys)
    {
        keys[sc] = 1;
        SDL_Delay(120);
        keys[sc] = 0;
    }
    return 0;
}

static void repeated_click(int x, int y, int attempts = 8)
{
    for (int i = 0; i < attempts; ++i) {
        inject_click(x, y, 100);
        SDL_Delay(150);
    }
}

struct ViewportGuard
{
    float ow, oh, ovw, ovh, ox, oy;
    ViewportGuard()
    {
        ow = og::runtime::current_session->window_w_;
        oh = og::runtime::current_session->window_h_;
        ovw = og::runtime::current_session->viewport_w_;
        ovh = og::runtime::current_session->viewport_h_;
        ox = og::runtime::current_session->viewport_offset_x_;
        oy = og::runtime::current_session->viewport_offset_y_;
    }
    ~ViewportGuard()
    {
        og::runtime::current_session->window_w_ = ow;
        og::runtime::current_session->window_h_ = oh;
        og::runtime::current_session->viewport_w_ = ovw;
        og::runtime::current_session->viewport_h_ = ovh;
        og::runtime::current_session->viewport_offset_x_ = ox;
        og::runtime::current_session->viewport_offset_y_ = oy;
    }
};

static int picker_choose_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(500);
    repeated_click(195, 190); // OK
    return 0;
}

static int picker_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(500);
    repeated_click(121, 190); // CANCEL
    return 0;
}

static int campaign_delete_then_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(500);
    repeated_click(280, 15, 4);  // DELETE
    repeated_click(121, 190, 8); // CANCEL
    return 0;
}

static int campaign_reset_then_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(500);
    repeated_click(280, 15, 4);  // RESET
    repeated_click(121, 190, 8); // CANCEL
    return 0;
}

static int level_picker_choose_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(500);
    for (int i = 0; i < 8; ++i) {
        inject_click(24, 23, 100);   // Select entry 1
        SDL_Delay(150);
        inject_click(280, 175, 100); // OK
        SDL_Delay(150);
    }
    return 0;
}

static int level_picker_delete_then_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(500);
    for (int i = 0; i < 4; ++i) {
        inject_click(24, 23, 100);  // Select entry 1
        SDL_Delay(150);
        inject_click(280, 15, 100); // DELETE
        SDL_Delay(150);
    }
    repeated_click(239, 175, 8); // CANCEL
    return 0;
}

TEST(CampaignAndLevelPicker, campaign_picker_cancel_esc_does_not_crash)
{
    cleanup_leftover_test_campaigns();

    ASSERT_TRUE(isDir(".")) << "isDir should report current directory as directory";
    ASSERT_TRUE(!isDir("./definitely_missing_openglad_path")) << "isDir should report missing path as not directory";

    ASSERT_TRUE(sort_scen("level2", "level10")) << "sort_scen should order numeric suffixes";
    ASSERT_TRUE(!sort_scen("abc9", "abc2")) << "sort_scen should not invert numeric suffix ordering";
    ASSERT_EQ(42, toInt("42")) << "toInt should parse decimal text";

    std::map<std::string, int> current_levels;
    const std::string mounted = get_mounted_campaign();
    current_levels[mounted] = 7;
    ASSERT_EQ(7, load_campaign(mounted, current_levels, 1)) << "load_campaign should use tracked current level";
    current_levels.clear();
    ASSERT_EQ(4, load_campaign(mounted, current_levels, 4)) << "load_campaign should fall back to first level";

    show_ending_popup(1, -1);
    show_ending_popup(1, 3);
    show_ending_popup(SCEN_TYPE_SAVE_ALL, 2);
    show_ending_popup(0, 2);

    // Keep picker exit deterministic in headless CI while still exercising setup paths.
    char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;
    CampaignResult canceled = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);
    og::runtime::current_session->myscreen_->world().end = old_end;
    ASSERT_TRUE(canceled.id.empty()) << "campaign picker early-exit should return empty campaign id";
}


TEST(CampaignAndLevelPicker, campaign_picker_draw_loop_exits_on_q)
{
    cleanup_leftover_test_campaigns();

    char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    SDL_Thread* thread = SDL_CreateThread(hold_q_key_for_picker, "picker_q_hold", nullptr);
    ASSERT_TRUE(thread != nullptr) << "failed to create picker q-hold thread";

    CampaignResult out = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    og::runtime::current_session->myscreen_->world().end = old_end;

    ASSERT_TRUE(out.id.empty()) << "q exit path should not select a campaign";
}


TEST(CampaignAndLevelPicker, campaign_picker_mouse_choose_and_cancel_paths)
{
    cleanup_leftover_test_campaigns();

    const std::string old_campaign = get_mounted_campaign();

    ViewportGuard guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    SDL_Thread* choose_thread = SDL_CreateThread(picker_choose_injector, "picker_choose", nullptr);
    ASSERT_TRUE(choose_thread != nullptr) << "failed to create choose injector";
    CampaignResult chosen = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);
    int choose_rc = 0;
    SDL_WaitThread(choose_thread, &choose_rc);
    ASSERT_TRUE(!chosen.id.empty()) << "choose path should return a selected campaign id";
    // Ensure later tests run against the baseline default campaign that has scenarios.
    ASSERT_TRUE(mount_campaign_package_with_error(old_campaign) == CampaignPackageIoError::None) << "failed to restore mounted campaign after choose path";

    SDL_Thread* cancel_thread = SDL_CreateThread(picker_cancel_injector, "picker_cancel", nullptr);
    ASSERT_TRUE(cancel_thread != nullptr) << "failed to create cancel injector";
    CampaignResult canceled = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);
    int cancel_rc = 0;
    SDL_WaitThread(cancel_thread, &cancel_rc);
    ASSERT_TRUE(canceled.id.empty()) << "cancel path should not return a campaign id";
    ASSERT_TRUE(mount_campaign_package_with_error(old_campaign) == CampaignPackageIoError::None) << "failed to restore mounted campaign after cancel path";

    og::runtime::current_session->myscreen_->world().end = old_end;
}


TEST(CampaignAndLevelPicker, campaign_picker_delete_and_reset_prompt_paths)
{
    cleanup_leftover_test_campaigns();

    ViewportGuard guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    SDL_Thread* delete_thread = SDL_CreateThread(campaign_delete_then_cancel_injector, "picker_delete_cancel", nullptr);
    ASSERT_TRUE(delete_thread != nullptr) << "failed to create campaign delete injector";
    CampaignResult after_delete_prompt = pick_campaign(&og::runtime::current_session->myscreen_->save_data, true);
    int delete_rc = 0;
    SDL_WaitThread(delete_thread, &delete_rc);
    ASSERT_TRUE(after_delete_prompt.id.empty()) << "delete+cancel path should return empty campaign id";

    SDL_Thread* reset_thread = SDL_CreateThread(campaign_reset_then_cancel_injector, "picker_reset_cancel", nullptr);
    ASSERT_TRUE(reset_thread != nullptr) << "failed to create campaign reset injector";
    CampaignResult after_reset_prompt = pick_campaign(&og::runtime::current_session->myscreen_->save_data, false);
    int reset_rc = 0;
    SDL_WaitThread(reset_thread, &reset_rc);
    ASSERT_TRUE(after_reset_prompt.id.empty()) << "reset+cancel path should return empty campaign id";

    og::runtime::current_session->myscreen_->world().end = old_end;
}


TEST(CampaignAndLevelPicker, load_campaign_invalid_id_reports_error)
{
    std::map<std::string, int> current_levels;
    const std::string old_campaign = get_mounted_campaign();

    int rv = load_campaign("org.openglad.this_campaign_should_not_exist", current_levels, 1);
    ASSERT_EQ(-2, rv) << "load_campaign should return -2 when mount fails";

    // Restore environment for tests that expect a mounted campaign.
    ASSERT_TRUE(mount_campaign_package_with_error(old_campaign) == CampaignPackageIoError::None) << "failed to remount original campaign";
}


TEST(CampaignAndLevelPicker, load_campaign_with_error_typed_result_paths)
{
    std::map<std::string, int> current_levels;
    current_levels["org.openglad.gladiator"] = 7;

    const std::string old_campaign = get_mounted_campaign();
    CampaignLoadResult typed = load_campaign_with_error("org.openglad.gladiator", current_levels, 1);
    ASSERT_EQ(static_cast<int>(CampaignLoadError::None), static_cast<int>(typed.error)) << "typed load_campaign should succeed for mounted campaign";
    ASSERT_EQ(7, typed.current_level) << "typed load_campaign should return mapped current level";

    typed = load_campaign_with_error("org.openglad.this_campaign_should_not_exist", current_levels, 1);
    ASSERT_EQ(static_cast<int>(CampaignLoadError::MountFailed), static_cast<int>(typed.error)) << "typed load_campaign should report MountFailed for invalid campaign";

    // Restore environment for tests that expect a mounted campaign.
    ASSERT_TRUE(mount_campaign_package_with_error(old_campaign) == CampaignPackageIoError::None) << "failed to remount original campaign";
}


TEST(CampaignAndLevelPicker, level_picker_cancel_esc_returns_default)
{
    LevelRuntimeData ld(1);
    ld.create_new_grid();
    walker* e1 = ld.add_ob(Order::Living, FAMILY_ORC);
    walker* e2 = ld.add_ob(Order::Living, FAMILY_BIG_ORC);
    walker* ally = ld.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(e1 && e2 && ally) << "level test walkers should be created";
    if (e1 && e2 && ally) {
        e1->set_team_num(1);
        e2->set_team_num(1);
        ally->set_team_num(0);
        e1->stats()->set_level(4);
        e2->stats()->set_level(2);
        ally->stats()->set_level(3);
    }
    walker* x1 = ld.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    walker* x2 = ld.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    walker* x3 = ld.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    ASSERT_TRUE(x1 && x2 && x3) << "exit markers should be created";
    if (x1 && x2 && x3) {
        x1->stats()->set_level(9);
        x2->stats()->set_level(5);
        x3->stats()->set_level(9);
    }

    int max_enemy = 0;
    float avg_enemy = 0.0f;
    int num_enemy = 0;
    float difficulty = 0.0f;
    std::list<int> exits;
    getLevelStats(ld, &max_enemy, &avg_enemy, &num_enemy, &difficulty, exits);
    ASSERT_EQ(2, num_enemy) << "getLevelStats should count enemy team members";
    ASSERT_EQ(4, max_enemy) << "getLevelStats should report max enemy level";
    ASSERT_TRUE(avg_enemy > 2.9f && avg_enemy < 3.1f) << "getLevelStats should report average enemy level";
    ASSERT_TRUE(difficulty > 8.9f && difficulty < 9.1f) << "getLevelStats should subtract ally difficulty";
    ASSERT_EQ(2, (int)exits.size()) << "getLevelStats should sort and uniquify exits";
    ASSERT_EQ(5, exits.front()) << "getLevelStats exits should be sorted";
    ASSERT_EQ(9, exits.back()) << "getLevelStats exits should include highest exit level";
    ld.delete_objects();

    char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;
    int canceled = pick_level(og::runtime::current_session->myscreen_, 1, false);
    og::runtime::current_session->myscreen_->world().end = old_end;
    ASSERT_EQ(1, canceled) << "level cancel should return default level";
}


TEST(CampaignAndLevelPicker, level_picker_choose_and_delete_prompt_paths)
{
    ViewportGuard guard;
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    char old_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 0;

    SDL_Thread* choose_thread = SDL_CreateThread(level_picker_choose_injector, "level_picker_choose", nullptr);
    ASSERT_TRUE(choose_thread != nullptr) << "failed to create level picker choose injector";
    int chosen = pick_level(og::runtime::current_session->myscreen_, 1, false);
    int choose_rc = 0;
    SDL_WaitThread(choose_thread, &choose_rc);
    ASSERT_TRUE(chosen > 0) << "choose path should return a valid level id";

    SDL_Thread* delete_thread = SDL_CreateThread(level_picker_delete_then_cancel_injector, "level_picker_delete_cancel", nullptr);
    ASSERT_TRUE(delete_thread != nullptr) << "failed to create level picker delete injector";
    int canceled_after_delete_prompt = pick_level(og::runtime::current_session->myscreen_, 1, true);
    int delete_rc = 0;
    SDL_WaitThread(delete_thread, &delete_rc);
    ASSERT_EQ(1, canceled_after_delete_prompt) << "delete prompt + cancel should keep default level";

    og::runtime::current_session->myscreen_->world().end = old_end;
}
