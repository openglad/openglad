#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/interface/button.h>
#include <openglad/resources/save_data.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <cmath>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

TEST(PickerCosts, picker_increase_decrease_stats_and_levels)
{
    // Put a soldier on the team so TrainSession has something to edit
    auto* team0 = new guy(FAMILY_SOLDIER);
    team0->upgrade_to_level(2);
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(team0);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    og::ui::TrainSession session(og::runtime::current_session->myscreen_->save_data);
    ASSERT_TRUE(!session.empty()) << "session should not be empty";

    using S = og::ui::TrainSession::Stat;

    int str0 = session.working_copy().strength;
    session.increase_stat(S::Strength, 1);
    ASSERT_EQ(str0 + 1, (int)session.working_copy().strength) << "increase_stat should increment STR";

    int str1 = session.working_copy().strength;
    session.decrease_stat(S::Strength, 1);
    ASSERT_EQ(str1 - 1, (int)session.working_copy().strength) << "decrease_stat should decrement STR";

    // If a stat has already increased relative to original, level increase should be blocked.
    session.increase_stat(S::Strength, 1);
    int level_before = session.working_copy().level;
    session.increase_stat(S::Level, 1);
    ASSERT_EQ(level_before, (int)session.working_copy().level) << "level increase should be blocked when stats already increased";

    // Reset stat back to original, then level-up should be allowed.
    session.decrease_stat(S::Strength, 1);
    ASSERT_TRUE(!session.stats_increased()) << "stats should match original after undo";
    level_before = session.working_copy().level;
    session.increase_stat(S::Level, 1);
    ASSERT_TRUE(session.working_copy().level >= level_before) << "level should not decrease on increase_stat(level)";

    // When level is higher than original, stat decreases should be blocked.
    ASSERT_TRUE(session.level_increased()) << "level should be marked as increased";
    int dex0 = session.working_copy().dexterity;
    session.decrease_stat(S::Dexterity, 1);
    ASSERT_EQ(dex0, (int)session.working_copy().dexterity) << "stat decrease should be blocked when level increased";

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset();
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
}


TEST(PickerCosts, picker_hire_cost_is_the_family_base_price_at_base_stats)
{
    // HireSession::make_recruit builds create_recruit(kAllowableGuys[n]), i.e.
    // guy(family) seeded from the FamilyDescriptor's base stats. Every stat
    // delta in calculate_hire_cost therefore clamps to 0 and calculate_exp of
    // the family's starting level is 0, so the price is exactly hiring_cost.
    og::ui::HireSession session(og::runtime::current_session->myscreen_->save_data, 0);

    const guy* recruit = session.current_recruit();
    ASSERT_NE(nullptr, recruit) << "a fresh hire session must offer a recruit";
    ASSERT_EQ(og::ui::kAllowableGuys[0], (int)recruit->family)
        << "a fresh session offers the first hireable family (SOLDIER)";
    ASSERT_EQ(0, session.family_index()) << "a fresh session starts on family index 0";

    const FamilyDescriptor* fd = get_family_descriptor(recruit->family);
    ASSERT_NE(nullptr, fd) << "SOLDIER must have a descriptor";

    EXPECT_EQ(fd->base_stats[StatAxis::Strength], (int)recruit->strength)
        << "a recruit starts at the descriptor's base strength";
    EXPECT_EQ(fd->base_stats[StatAxis::Dexterity], (int)recruit->dexterity)
        << "a recruit starts at the descriptor's base dexterity";
    EXPECT_EQ(fd->base_stats[StatAxis::Constitution], (int)recruit->constitution)
        << "a recruit starts at the descriptor's base constitution";
    EXPECT_EQ(fd->base_stats[StatAxis::Intelligence], (int)recruit->intelligence)
        << "a recruit starts at the descriptor's base intelligence";
    EXPECT_EQ(fd->base_stats[StatAxis::Armor], (int)recruit->armor)
        << "a recruit starts at the descriptor's base armor";
    EXPECT_EQ(fd->base_stats[StatAxis::Level], (int)recruit->level)
        << "a recruit starts at the descriptor's base level";

    // The pack's own numbers, so a silent pack edit is visible here too.
    EXPECT_EQ(12, (int)recruit->strength) << "packs/core/families/living-00-soldier.lua: strength 12";
    EXPECT_EQ(6, (int)recruit->dexterity) << "living-00-soldier.lua: dexterity 6";
    EXPECT_EQ(12, (int)recruit->constitution) << "living-00-soldier.lua: constitution 12";
    EXPECT_EQ(8, (int)recruit->intelligence) << "living-00-soldier.lua: intelligence 8";
    EXPECT_EQ(9, (int)recruit->armor) << "living-00-soldier.lua: armor 9";
    EXPECT_EQ(1, (int)recruit->level) << "living-00-soldier.lua: level 1";

    EXPECT_EQ(static_cast<std::uint32_t>(fd->hiring_cost), session.current_cost())
        << "a base-stat recruit pays the flat hire cost: no stat surcharge, no level XP";
    EXPECT_EQ(250u, session.current_cost()) << "a SOLDIER costs 250 gold";

    // The price follows the family the session is cycled to.
    session.next_family();
    const guy* second = session.current_recruit();
    ASSERT_NE(nullptr, second) << "next_family must manufacture a new recruit";
    ASSERT_EQ(og::ui::kAllowableGuys[1], (int)second->family)
        << "next_family steps to kAllowableGuys[1] (BARBARIAN)";
    const FamilyDescriptor* fd2 = get_family_descriptor(second->family);
    ASSERT_NE(nullptr, fd2) << "BARBARIAN must have a descriptor";
    EXPECT_EQ(static_cast<std::uint32_t>(fd2->hiring_cost), session.current_cost())
        << "the cost follows the cycled family";
    EXPECT_EQ(350u, session.current_cost()) << "a BARBARIAN costs 350 gold";
    EXPECT_NE(250u, session.current_cost()) << "and it is not still the soldier price";
}


TEST(PickerCosts, picker_train_cost_prices_stat_deltas_and_level_upgrades_exactly)
{
    auto* team0 = new guy(FAMILY_SOLDIER);
    team0->upgrade_to_level(2);
    og::runtime::current_session->myscreen_->save_data.team_list[0].reset(team0);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    // The member the session edits: soldier base 12/6/12/8/9 plus one
    // kDefaultLevelUpGains step {8,6,8,8,1}, with exp stamped to calculate_exp(2).
    ASSERT_EQ(20, (int)team0->strength) << "level 2 soldier = base 12 + 8 STR per level";
    ASSERT_EQ(2, (int)team0->level) << "the roster member is level 2";
    ASSERT_EQ(calculate_exp(2), team0->exp) << "upgrade_to_level stamps the level's exp";
    // guy.cpp's exp recurrence: 8000 + 2000*(k-1) + 4000*(k-2) summed from
    // k = 2. (The stale excel table in that comment says 9600/20000; the code
    // has said 10000/26000 since the recursion was unrolled.)
    ASSERT_EQ(10000u, calculate_exp(2)) << "guy.cpp exp curve: level 2 costs 10000";
    ASSERT_EQ(26000u, calculate_exp(3)) << "guy.cpp exp curve: level 3 costs 26000";

    og::ui::TrainSession session(og::runtime::current_session->myscreen_->save_data);
    ASSERT_TRUE(!session.empty()) << "session should not be empty";

    using S = og::ui::TrainSession::Stat;

    // Stat half: +2 STR moves the delta above the family base from 8 to 10,
    // priced at pow(delta, kStatCostExponent) * stat_costs[Strength=6] and
    // charged as the DIFFERENCE of the two, not the raw new price.
    session.increase_stat(S::Strength, 2);
    ASSERT_EQ(22, (int)session.working_copy().strength) << "increase_stat(+2) must move the working copy";
    const std::uint32_t cost_stats = session.current_cost();
    const std::uint32_t expected_stats = static_cast<std::uint32_t>(
        static_cast<int>(std::pow(10, og::ui::kStatCostExponent) * 6)
        - static_cast<int>(std::pow(8, og::ui::kStatCostExponent) * 6));
    EXPECT_EQ(expected_stats, cost_stats)
        << "a +2 STR train is pow(10,1.85)*6 - pow(8,1.85)*6, the delta above the family base";
    EXPECT_EQ(143u, cost_stats) << "which is exactly 424 - 281 = 143 gold";

    // Level half: raising the level charges the XP gap and NOTHING else --
    // calculate_train_cost skips its whole stat block when eff_lvl > original.level.
    session.decrease_stat(S::Strength, 2);
    ASSERT_EQ(20, (int)session.working_copy().strength) << "the stat edit must be undone first";
    ASSERT_EQ(0u, session.current_cost()) << "an unedited member trains for free";

    session.increase_stat(S::Level, 1);
    ASSERT_EQ(3, (int)session.working_copy().level) << "increase_stat(Level) must reach level 3";
    const std::uint32_t cost_level = session.current_cost();
    EXPECT_EQ(calculate_exp(3) - calculate_exp(2), cost_level)
        << "a 2->3 upgrade costs exactly the exp gap and adds no stat surcharge";
    EXPECT_EQ(16000u, cost_level) << "26000 - 10000 = 16000 gold";
    EXPECT_NE(cost_stats, cost_level) << "the two pricing branches are not the same number";

    og::runtime::current_session->myscreen_->save_data.team_list[0].reset();
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
}
