#include "campaign_picker.h"
#include "level_picker.h"
#include "test_framework.h"

extern screen* myscreen;

void test_campaign_picker_cancel_esc_does_not_crash()
{
    // Force the picker to bail out immediately (headless, deterministic).
    // This still executes campaign enumeration + entry construction.
    char old_end = myscreen->end;
    myscreen->end = 1;
    CampaignResult r = pick_campaign(&myscreen->save_data, false);
    myscreen->end = old_end;
    (void)r;
}
REGISTER_TEST(test_campaign_picker_cancel_esc_does_not_crash);

void test_level_picker_cancel_esc_returns_default()
{
    char old_end = myscreen->end;
    myscreen->end = 1;
    int picked = pick_level(myscreen, 1, false);
    myscreen->end = old_end;
    TEST_ASSERT_EQ(1, picked, "cancel should return the default level");
}
REGISTER_TEST(test_level_picker_cancel_esc_returns_default);
