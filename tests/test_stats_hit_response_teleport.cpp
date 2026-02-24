#include <openglad/entities/guy.h>
#include <openglad/runtime/game_context.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <unordered_set>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unordered_set<walker*> snapshot_ptrs(const std::list<std::unique_ptr<walker>>& lst)
{
    std::unordered_set<walker*> out;
    out.reserve(lst.size());
    for (auto& up : lst)
        out.insert(up.get());
    return out;
}

static void remove_new_leveldata_objects(LevelData& level,
                                        const std::unordered_set<walker*>& ob_before,
                                        const std::unordered_set<walker*>& fx_before,
                                        const std::unordered_set<walker*>& weap_before)
{
    std::vector<walker*> to_remove;
    to_remove.reserve(level.oblist.size() + level.fxlist.size() + level.weaplist.size());

    for (auto& up : level.oblist)
        if (up && !ob_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.fxlist)
        if (up && !fx_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.weaplist)
        if (up && !weap_before.contains(up.get()))
            to_remove.push_back(up.get());

    for (walker* w : to_remove)
        level.remove_ob(w);
}

struct RngGuard
{
    IRandom* prev;
    explicit RngGuard(IRandom* next)
        : prev(ctx().rng)
    {
        ctx().rng = next;
    }
    ~RngGuard() { ctx().rng = prev; }
    RngGuard(const RngGuard&) = delete;
    RngGuard& operator=(const RngGuard&) = delete;
};

void test_stats_hit_response_mage_and_archmage_teleport_branches()
{
    TEST_ASSERT(og::runtime::current_session->myscreen_ != nullptr, "myscreen exists");
    if (!og::runtime::current_session->myscreen_)
        return;

    LevelData& level = og::runtime::current_session->myscreen_->level_data;
    const auto ob_before = snapshot_ptrs(level.oblist);
    const auto fx_before = snapshot_ptrs(level.fxlist);
    const auto weap_before = snapshot_ptrs(level.weaplist);

    FixedRandom fixed_rng(1); // rng(3) => 1 (true)
    RngGuard rng_guard(&fixed_rng);

    walker* foe = level.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(foe != nullptr, "foe created");
    if (!foe)
        return;
    foe->team_num = 1;
    foe->setxy(160, 100);

    walker* mage = level.add_ob(Order::Living, FAMILY_MAGE);
    TEST_ASSERT(mage != nullptr, "mage created");
    if (mage) {
        mage->team_num = 0;
        mage->setxy(100, 100);
        mage->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
        mage->stats()->level = 3;
        mage->stats()->max_hitpoints = 100.0f;
        mage->stats()->hitpoints = 10.0f;  // < threshold
        mage->stats()->magicpoints = 100.0f;
        mage->stats()->special_cost[1] = 0;

        mage->stats()->hit_response(foe);
        TEST_ASSERT(mage->current_special == 1, "mage hit_response should set teleport special");
    }

    walker* arch = level.add_ob(Order::Living, FAMILY_ARCHMAGE);
    TEST_ASSERT(arch != nullptr, "archmage created");
    if (arch) {
        arch->team_num = 0;
        arch->setxy(120, 100);
        arch->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHMAGE));
        arch->stats()->level = 3;
        arch->stats()->max_hitpoints = 100.0f;
        arch->stats()->hitpoints = 10.0f;  // < threshold
        arch->stats()->magicpoints = 100.0f;
        arch->stats()->special_cost[1] = 0;

        arch->stats()->hit_response(foe);
        TEST_ASSERT(arch->current_special == 1, "archmage hit_response should set teleport special");
    }

    remove_new_leveldata_objects(level, ob_before, fx_before, weap_before);
}
REGISTER_TEST(test_stats_hit_response_mage_and_archmage_teleport_branches);
