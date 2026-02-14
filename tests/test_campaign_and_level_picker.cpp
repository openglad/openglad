#include <openglad/ui/campaign_picker.h>
#include <openglad/ui/level_picker.h>
#include <openglad/ui/results_screen.h>
#include <openglad/core/stats.h>
#include <openglad/entities/walker.h>
#include <openglad/input/input.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include <openglad/platform/io.h>
#include "test_framework.h"
#include "test_input_helpers.h"

#include <map>
#include <string>

extern screen* myscreen;

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

struct ViewportGuard
{
    float ow, oh, ovw, ovh, ox, oy;
    ViewportGuard()
    {
        ow = window_w;
        oh = window_h;
        ovw = viewport_w;
        ovh = viewport_h;
        ox = viewport_offset_x;
        oy = viewport_offset_y;
    }
    ~ViewportGuard()
    {
        window_w = ow;
        window_h = oh;
        viewport_w = ovw;
        viewport_h = ovh;
        viewport_offset_x = ox;
        viewport_offset_y = oy;
    }
};

static int picker_choose_injector(void* data)
{
    (void)data;
    SDL_Delay(120);
    inject_click(211, 40, 20); // NEXT
    SDL_Delay(120);
    inject_click(109, 40, 20); // PREV
    SDL_Delay(120);
    inject_click(195, 190, 20); // OK
    return 0;
}

static int picker_cancel_injector(void* data)
{
    (void)data;
    SDL_Delay(120);
    inject_click(121, 190, 20); // CANCEL
    return 0;
}

static int campaign_delete_then_cancel_injector(void* data)
{
    (void)data;
    SDL_Delay(120);
    inject_click(280, 15, 20);  // DELETE
    SDL_Delay(120);
    inject_click(121, 190, 20); // CANCEL
    return 0;
}

static int campaign_reset_then_cancel_injector(void* data)
{
    (void)data;
    SDL_Delay(120);
    inject_click(280, 15, 20);  // RESET (same location as delete)
    SDL_Delay(120);
    inject_click(121, 190, 20); // CANCEL
    return 0;
}

static int level_picker_choose_injector(void* data)
{
    (void)data;
    SDL_Delay(140);
    inject_click(175, 150, 20); // NEXT
    SDL_Delay(140);
    inject_click(24, 23, 20);   // Select entry 1
    SDL_Delay(140);
    inject_click(280, 175, 20); // OK
    return 0;
}

static int level_picker_delete_then_cancel_injector(void* data)
{
    (void)data;
    SDL_Delay(140);
    inject_click(24, 23, 20);   // Select entry 1
    SDL_Delay(140);
    inject_click(280, 15, 20);  // DELETE
    SDL_Delay(140);
    inject_click(239, 175, 20); // CANCEL
    return 0;
}

void test_campaign_picker_cancel_esc_does_not_crash()
{
    cleanup_leftover_test_campaigns();

    TEST_ASSERT(isDir("."), "isDir should report current directory as directory");
    TEST_ASSERT(!isDir("./definitely_missing_openglad_path"), "isDir should report missing path as not directory");

    TEST_ASSERT(sort_scen("level2", "level10"), "sort_scen should order numeric suffixes");
    TEST_ASSERT(!sort_scen("abc9", "abc2"), "sort_scen should not invert numeric suffix ordering");
    TEST_ASSERT_EQ(42, toInt("42"), "toInt should parse decimal text");

    std::map<std::string, int> current_levels;
    const std::string mounted = get_mounted_campaign();
    current_levels[mounted] = 7;
    TEST_ASSERT_EQ(7, load_campaign(mounted, current_levels, 1), "load_campaign should use tracked current level");
    current_levels.clear();
    TEST_ASSERT_EQ(4, load_campaign(mounted, current_levels, 4), "load_campaign should fall back to first level");

    show_ending_popup(1, -1);
    show_ending_popup(1, 3);
    show_ending_popup(SCEN_TYPE_SAVE_ALL, 2);
    show_ending_popup(0, 2);

    // Keep picker exit deterministic in headless CI while still exercising setup paths.
    char old_end = myscreen->end;
    myscreen->end = 1;
    CampaignResult canceled = pick_campaign(&myscreen->save_data, false);
    myscreen->end = old_end;
    TEST_ASSERT(canceled.id.empty(), "campaign picker early-exit should return empty campaign id");
}
REGISTER_TEST(test_campaign_picker_cancel_esc_does_not_crash);

void test_campaign_picker_draw_loop_exits_on_q()
{
    cleanup_leftover_test_campaigns();

    char old_end = myscreen->end;
    myscreen->end = 0;

    SDL_Thread* thread = SDL_CreateThread(hold_q_key_for_picker, "picker_q_hold", nullptr);
    TEST_ASSERT(thread != nullptr, "failed to create picker q-hold thread");

    CampaignResult out = pick_campaign(&myscreen->save_data, false);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    myscreen->end = old_end;

    TEST_ASSERT(out.id.empty(), "q exit path should not select a campaign");
}
REGISTER_TEST(test_campaign_picker_draw_loop_exits_on_q);

void test_campaign_picker_mouse_choose_and_cancel_paths()
{
    cleanup_leftover_test_campaigns();

    const std::string old_campaign = get_mounted_campaign();

    ViewportGuard guard;
    window_w = 320;
    window_h = 200;
    viewport_offset_x = 0;
    viewport_offset_y = 0;
    viewport_w = 320;
    viewport_h = 200;

    char old_end = myscreen->end;
    myscreen->end = 0;

    SDL_Thread* choose_thread = SDL_CreateThread(picker_choose_injector, "picker_choose", nullptr);
    TEST_ASSERT(choose_thread != nullptr, "failed to create choose injector");
    CampaignResult chosen = pick_campaign(&myscreen->save_data, false);
    int choose_rc = 0;
    SDL_WaitThread(choose_thread, &choose_rc);
    TEST_ASSERT(!chosen.id.empty(), "choose path should return a selected campaign id");
    // Ensure later tests run against the baseline default campaign that has scenarios.
    TEST_ASSERT(mount_campaign_package(old_campaign), "failed to restore mounted campaign after choose path");

    SDL_Thread* cancel_thread = SDL_CreateThread(picker_cancel_injector, "picker_cancel", nullptr);
    TEST_ASSERT(cancel_thread != nullptr, "failed to create cancel injector");
    CampaignResult canceled = pick_campaign(&myscreen->save_data, false);
    int cancel_rc = 0;
    SDL_WaitThread(cancel_thread, &cancel_rc);
    TEST_ASSERT(canceled.id.empty(), "cancel path should not return a campaign id");
    TEST_ASSERT(mount_campaign_package(old_campaign), "failed to restore mounted campaign after cancel path");

    myscreen->end = old_end;
}
REGISTER_TEST(test_campaign_picker_mouse_choose_and_cancel_paths);

void test_campaign_picker_delete_and_reset_prompt_paths()
{
    cleanup_leftover_test_campaigns();

    ViewportGuard guard;
    window_w = 320;
    window_h = 200;
    viewport_offset_x = 0;
    viewport_offset_y = 0;
    viewport_w = 320;
    viewport_h = 200;

    char old_end = myscreen->end;
    myscreen->end = 0;

    SDL_Thread* delete_thread = SDL_CreateThread(campaign_delete_then_cancel_injector, "picker_delete_cancel", nullptr);
    TEST_ASSERT(delete_thread != nullptr, "failed to create campaign delete injector");
    CampaignResult after_delete_prompt = pick_campaign(&myscreen->save_data, true);
    int delete_rc = 0;
    SDL_WaitThread(delete_thread, &delete_rc);
    TEST_ASSERT(after_delete_prompt.id.empty(), "delete+cancel path should return empty campaign id");

    SDL_Thread* reset_thread = SDL_CreateThread(campaign_reset_then_cancel_injector, "picker_reset_cancel", nullptr);
    TEST_ASSERT(reset_thread != nullptr, "failed to create campaign reset injector");
    CampaignResult after_reset_prompt = pick_campaign(&myscreen->save_data, false);
    int reset_rc = 0;
    SDL_WaitThread(reset_thread, &reset_rc);
    TEST_ASSERT(after_reset_prompt.id.empty(), "reset+cancel path should return empty campaign id");

    myscreen->end = old_end;
}
REGISTER_TEST(test_campaign_picker_delete_and_reset_prompt_paths);

void test_load_campaign_invalid_id_reports_error()
{
    std::map<std::string, int> current_levels;
    const std::string old_campaign = get_mounted_campaign();

    int rv = load_campaign("org.openglad.this_campaign_should_not_exist", current_levels, 1);
    TEST_ASSERT_EQ(-2, rv, "load_campaign should return -2 when mount fails");

    // Restore environment for tests that expect a mounted campaign.
    TEST_ASSERT(mount_campaign_package(old_campaign), "failed to remount original campaign");
}
REGISTER_TEST(test_load_campaign_invalid_id_reports_error);

void test_load_campaign_with_error_typed_result_paths()
{
    std::map<std::string, int> current_levels;
    current_levels["org.openglad.gladiator"] = 7;

    const std::string old_campaign = get_mounted_campaign();
    CampaignLoadResult typed = load_campaign_with_error("org.openglad.gladiator", current_levels, 1);
    TEST_ASSERT_EQ(static_cast<int>(CampaignLoadError::None), static_cast<int>(typed.error),
        "typed load_campaign should succeed for mounted campaign");
    TEST_ASSERT_EQ(7, typed.current_level, "typed load_campaign should return mapped current level");

    typed = load_campaign_with_error("org.openglad.this_campaign_should_not_exist", current_levels, 1);
    TEST_ASSERT_EQ(static_cast<int>(CampaignLoadError::MountFailed), static_cast<int>(typed.error),
        "typed load_campaign should report MountFailed for invalid campaign");

    // Restore environment for tests that expect a mounted campaign.
    TEST_ASSERT(mount_campaign_package(old_campaign), "failed to remount original campaign");
}
REGISTER_TEST(test_load_campaign_with_error_typed_result_paths);

void test_level_picker_cancel_esc_returns_default()
{
    LevelData ld(1);
    ld.create_new_grid();
    walker* e1 = ld.add_ob(Order::Living, FAMILY_ORC);
    walker* e2 = ld.add_ob(Order::Living, FAMILY_BIG_ORC);
    walker* ally = ld.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(e1 && e2 && ally, "level test walkers should be created");
    if (e1 && e2 && ally) {
        e1->team_num = 1;
        e2->team_num = 1;
        ally->team_num = 0;
        e1->stats()->level = 4;
        e2->stats()->level = 2;
        ally->stats()->level = 3;
    }
    walker* x1 = ld.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    walker* x2 = ld.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    walker* x3 = ld.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    TEST_ASSERT(x1 && x2 && x3, "exit markers should be created");
    if (x1 && x2 && x3) {
        x1->stats()->level = 9;
        x2->stats()->level = 5;
        x3->stats()->level = 9;
    }

    int max_enemy = 0;
    float avg_enemy = 0.0f;
    int num_enemy = 0;
    float difficulty = 0.0f;
    std::list<int> exits;
    getLevelStats(ld, &max_enemy, &avg_enemy, &num_enemy, &difficulty, exits);
    TEST_ASSERT_EQ(2, num_enemy, "getLevelStats should count enemy team members");
    TEST_ASSERT_EQ(4, max_enemy, "getLevelStats should report max enemy level");
    TEST_ASSERT(avg_enemy > 2.9f && avg_enemy < 3.1f, "getLevelStats should report average enemy level");
    TEST_ASSERT(difficulty > 8.9f && difficulty < 9.1f, "getLevelStats should subtract ally difficulty");
    TEST_ASSERT_EQ(2, (int)exits.size(), "getLevelStats should sort and uniquify exits");
    TEST_ASSERT_EQ(5, exits.front(), "getLevelStats exits should be sorted");
    TEST_ASSERT_EQ(9, exits.back(), "getLevelStats exits should include highest exit level");
    ld.delete_objects();

    char old_end = myscreen->end;
    myscreen->end = 1;
    int canceled = pick_level(myscreen, 1, false);
    myscreen->end = old_end;
    TEST_ASSERT_EQ(1, canceled, "level cancel should return default level");
}
REGISTER_TEST(test_level_picker_cancel_esc_returns_default);

void test_level_picker_choose_and_delete_prompt_paths()
{
    ViewportGuard guard;
    window_w = 320;
    window_h = 200;
    viewport_offset_x = 0;
    viewport_offset_y = 0;
    viewport_w = 320;
    viewport_h = 200;

    char old_end = myscreen->end;
    myscreen->end = 0;

    SDL_Thread* choose_thread = SDL_CreateThread(level_picker_choose_injector, "level_picker_choose", nullptr);
    TEST_ASSERT(choose_thread != nullptr, "failed to create level picker choose injector");
    int chosen = pick_level(myscreen, 1, false);
    int choose_rc = 0;
    SDL_WaitThread(choose_thread, &choose_rc);
    TEST_ASSERT(chosen > 0, "choose path should return a valid level id");

    SDL_Thread* delete_thread = SDL_CreateThread(level_picker_delete_then_cancel_injector, "level_picker_delete_cancel", nullptr);
    TEST_ASSERT(delete_thread != nullptr, "failed to create level picker delete injector");
    int canceled_after_delete_prompt = pick_level(myscreen, 1, true);
    int delete_rc = 0;
    SDL_WaitThread(delete_thread, &delete_rc);
    TEST_ASSERT_EQ(1, canceled_after_delete_prompt, "delete prompt + cancel should keep default level");

    myscreen->end = old_end;
}
REGISTER_TEST(test_level_picker_choose_and_delete_prompt_paths);
