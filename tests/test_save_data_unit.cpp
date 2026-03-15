#include <openglad/resources/save_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/legacy/base.h>
#include <cstdint>
#include <list>
#include <memory>
#include <gtest/gtest.h>

TEST(SaveDataUnit, save_data_update_guys_clamps_to_max_team_size)
{
    SaveData save;
    std::list<std::unique_ptr<walker>> oblist;

    constexpr int kExtraWalkers = 10;
    const int walker_count = MAX_TEAM_SIZE + kExtraWalkers;
    for (int i = 0; i < walker_count; ++i)
    {
        auto w = std::make_unique<walker>();
        w->set_dead(0);
        auto recruited = std::make_unique<guy>(FAMILY_SOLDIER);
        recruited->exp = static_cast<std::uint32_t>(100 + i);
        w->set_owned_myguy(std::move(recruited));
        oblist.push_back(std::move(w));
    }

    save.update_guys(oblist);

    ASSERT_TRUE(static_cast<int>(save.team_size) == MAX_TEAM_SIZE);
    ASSERT_TRUE(save.team_list.front() != nullptr);
    ASSERT_TRUE(save.team_list.back() != nullptr);
    ASSERT_TRUE(save.team_list[MAX_TEAM_SIZE - 1]->exp == static_cast<std::uint32_t>(100 + (MAX_TEAM_SIZE - 1)));
}
