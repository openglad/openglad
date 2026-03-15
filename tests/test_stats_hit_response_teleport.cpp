#include <openglad/gameplay/guy.h>
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

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

static void remove_new_leveldata_objects(LevelRuntimeData& level,
                                        const std::unordered_set<walker*>& ob_before,
                                        const std::unordered_set<walker*>& fx_before,
                                        const std::unordered_set<walker*>& weap_before)
{
    std::vector<walker*> to_remove;
    to_remove.reserve(level.world().oblist.size() + level.world().fxlist.size() + level.world().weaplist.size());

    for (auto& up : level.world().oblist)
        if (up && !ob_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.world().fxlist)
        if (up && !fx_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.world().weaplist)
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

TEST(StatsHitResponseTeleport, stats_hit_response_mage_and_archmage_teleport_branches)
{
    ASSERT_TRUE(og::runtime::current_session->myscreen_ != nullptr) << "myscreen exists";
    if (!og::runtime::current_session->myscreen_)
        return;

    LevelRuntimeData& level = og::runtime::current_session->myscreen_->level_runtime_data();
    const auto ob_before = snapshot_ptrs(level.world().oblist);
    const auto fx_before = snapshot_ptrs(level.world().fxlist);
    const auto weap_before = snapshot_ptrs(level.world().weaplist);

    FixedRandom fixed_rng(1); // rng(3) => 1 (true)
    RngGuard rng_guard(&fixed_rng);

    walker* foe = level.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    if (!foe)
        return;
    foe->set_team_num(1);
    foe->setxy(160, 100);

    walker* mage = level.add_ob(Order::Living, FAMILY_MAGE);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    if (mage) {
        mage->set_team_num(0);
        mage->setxy(100, 100);
        mage->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
        mage->stats()->set_level(3);
        mage->stats()->set_max_hitpoints(100.0f);
        mage->stats()->set_hitpoints(10.0f);  // < threshold
        mage->stats()->set_magicpoints(100.0f);
        mage->stats()->set_special_cost(1, 0);

        mage->stats()->hit_response(foe);
        ASSERT_TRUE(mage->current_special() == 1) << "mage hit_response should set teleport special";
    }

    walker* arch = level.add_ob(Order::Living, FAMILY_ARCHMAGE);
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    if (arch) {
        arch->set_team_num(0);
        arch->setxy(120, 100);
        arch->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHMAGE));
        arch->stats()->set_level(3);
        arch->stats()->set_max_hitpoints(100.0f);
        arch->stats()->set_hitpoints(10.0f);  // < threshold
        arch->stats()->set_magicpoints(100.0f);
        arch->stats()->set_special_cost(1, 0);

        arch->stats()->hit_response(foe);
        ASSERT_TRUE(arch->current_special() == 1) << "archmage hit_response should set teleport special";
    }

    remove_new_leveldata_objects(level, ob_before, fx_before, weap_before);
}
