#include <openglad/resources/save_io.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/legacy/base.h>
#include <cstdint>
#include <list>
#include <memory>
#include <vector>
#include "unit/unit.h"

OG_UNIT_TEST(test_save_data_update_guys_clamps_to_max_team_size)
{
    SaveData save;
    std::list<std::unique_ptr<walker>> oblist;

    constexpr int kExtraWalkers = 10;
    const int walker_count = MAX_TEAM_SIZE + kExtraWalkers;
    for (int i = 0; i < walker_count; ++i)
    {
        auto w = std::make_unique<walker>();
        w->dead = 0;
        auto recruited = std::make_unique<guy>(FAMILY_SOLDIER);
        recruited->exp = static_cast<std::uint32_t>(100 + i);
        w->set_owned_myguy(std::move(recruited));
        oblist.push_back(std::move(w));
    }

    std::vector<const guy*> guys;
    guys.reserve(oblist.size());
    for (const auto& uptr : oblist)
    {
        const walker* w = uptr.get();
        if (w && !w->dead && w->myguy)
            guys.push_back(w->myguy);
    }

    save.update_guys(guys);

    OG_ASSERT(static_cast<int>(save.team_size) == MAX_TEAM_SIZE);
    OG_ASSERT(save.team_list.front() != nullptr);
    OG_ASSERT(save.team_list.back() != nullptr);
    OG_ASSERT(save.team_list[MAX_TEAM_SIZE - 1]->exp == static_cast<std::uint32_t>(100 + (MAX_TEAM_SIZE - 1)));
}
