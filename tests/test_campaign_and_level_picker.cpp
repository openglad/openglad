#include "campaign_picker.h"
#include "level_picker.h"
#include "results_screen.h"
#include "walker.h"
#include "test_framework.h"

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

void test_campaign_picker_cancel_esc_does_not_crash()
{
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
