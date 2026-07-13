// The shared level-win fold (og::progression::apply_win_fold): fold-of-zeros
// money no-op, the MANDATORY apply-twice == apply-once idempotence property,
// bonus iff !already_completed, the CTF rematch predicate truth table (vs the
// old inline shape), the mode-policy paths via a stub progression
// (marks_level_completed == false, advance_cursor redirect/HOLD), the
// next_level == -1 guard, current_levels assignment, and the update_guys
// roster rebuild.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/game_mode.h>
#include <openglad/resources/progression.h>
#include <openglad/resources/save_data.h>

#include "test_game_world_fixture.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace {

using og::progression::WinFoldContext;
using og::progression::apply_win_fold;
using og::progression::ctf_rematch_shape;

constexpr const char* kCampaign = "org.openglad.test_fold";

// SaveData is non-copyable (owning team_list) — initialize in place.
// SaveData::reset() seeds m_totalcash at the new-game 5000 grubstake; zero
// the money channels so the fold arithmetic below reads in absolutes.
void init_save(SaveData& save, short scen_num)
{
    save.reset();
    save.current_campaign = kCampaign;
    save.scen_num = scen_num;
    for (std::size_t t = 0; t < std::size(save.m_score); ++t)
    {
        save.m_score[t] = 0;
        save.m_totalscore[t] = 0;
        save.m_totalcash[t] = 0;
    }
}

WinFoldContext make_ctx(short next_level,
                        std::array<std::uint32_t, 4> bonus = {})
{
    WinFoldContext ctx;
    ctx.time_bonus = bonus;
    ctx.outcome.ending = 0;
    ctx.outcome.next_level = next_level;
    return ctx;
}

// ---------------------------------------------------------------------------
// Money shapes.

TEST(WinFold, fold_of_zeros_is_a_money_no_op)
{
    TestGameWorld fx;
    SaveData save;
    init_save(save, 3);

    apply_win_fold(save, fx.world(), make_ctx(/*next_level=*/-1));

    for (int t = 0; t < 4; ++t)
    {
        EXPECT_EQ(0u, save.m_score[t]);
        EXPECT_EQ(0u, save.m_totalscore[t]);
        EXPECT_EQ(0u, save.m_totalcash[t]);
    }
    EXPECT_EQ(3, save.scen_num);
}

TEST(WinFold, folds_score_into_totals_with_double_cash)
{
    TestGameWorld fx;
    SaveData save;
    init_save(save, 3);
    save.m_score[0] = 100;
    save.m_score[2] = 7;

    apply_win_fold(save, fx.world(), make_ctx(/*next_level=*/4));

    EXPECT_EQ(100u, save.m_totalscore[0]);
    EXPECT_EQ(200u, save.m_totalcash[0]);
    EXPECT_EQ(7u, save.m_totalscore[2]);
    EXPECT_EQ(14u, save.m_totalcash[2]);
    EXPECT_EQ(0u, save.m_score[0]);
    EXPECT_EQ(0u, save.m_score[2]);
    EXPECT_TRUE(save.is_level_completed(3));
    EXPECT_EQ(4, save.scen_num);
}

TEST(WinFold, bonus_awarded_only_on_first_completion)
{
    TestGameWorld fx;

    // First win: the level is NOT yet completed -> bonus lands.
    SaveData fresh;
    init_save(fresh, 3);
    fresh.m_score[0] = 100;
    apply_win_fold(fresh, fx.world(), make_ctx(4, {50, 0, 0, 0}));
    EXPECT_EQ(250u, fresh.m_totalcash[0]) << "200 score cash + 50 bonus";

    // Replay of an already-completed level: no bonus, score still folds.
    SaveData replay;
    init_save(replay, 3);
    replay.add_level_completed(kCampaign, 3);
    replay.m_score[0] = 100;
    apply_win_fold(replay, fx.world(), make_ctx(4, {50, 0, 0, 0}));
    EXPECT_EQ(200u, replay.m_totalcash[0]) << "no bonus on a replayed level";
}

// ---------------------------------------------------------------------------
// Idempotence — the double-finalize contract (screen::endgame + the shadow
// finalize both fold in local play).

TEST(WinFold, apply_twice_equals_apply_once)
{
    TestGameWorld fx;
    SaveData save;
    init_save(save, 3);
    save.m_score[0] = 100;
    save.m_score[1] = 40;

    // Every real finalizer fills finished_level from the pre-fold cursor;
    // that field is what makes a literal second pass detectable (below).
    WinFoldContext ctx = make_ctx(4, {50, 10, 0, 0});
    ctx.finished_level = 3;
    apply_win_fold(save, fx.world(), ctx);

    const std::uint32_t cash_after_once[4] = {
        save.m_totalcash[0], save.m_totalcash[1],
        save.m_totalcash[2], save.m_totalcash[3]};
    const std::uint32_t score_after_once[4] = {
        save.m_totalscore[0], save.m_totalscore[1],
        save.m_totalscore[2], save.m_totalscore[3]};

    // Second pass with the SAME (stale) context — the worst re-entrant
    // shape: the ORIGINAL bonus array is re-presented and outcome.next_level
    // still points at the destination. The advanced cursor makes the fold's
    // on_finished gate false (scen_num is now 4, finished_level is 3), so
    // the bonus is NOT re-awarded, the DESTINATION level is NOT marked
    // completed, and advance_cursor is not re-run. m_score is already zero,
    // so the money fold adds nothing either.
    //
    // (The live local-play double finalize — the shadow then screen::endgame
    // — folds a mirrored COPY of the same pre-fold state rather than the
    // same object twice; see the call-graph comment above apply_win_fold.
    // This test pins the stronger same-object contract.)
    apply_win_fold(save, fx.world(), ctx);

    for (int t = 0; t < 4; ++t)
    {
        EXPECT_EQ(cash_after_once[t], save.m_totalcash[t]) << "team " << t;
        EXPECT_EQ(score_after_once[t], save.m_totalscore[t]) << "team " << t;
        EXPECT_EQ(0u, save.m_score[t]);
    }
    EXPECT_EQ(4, save.scen_num);
    EXPECT_EQ(4, save.current_levels[kCampaign]);
    EXPECT_TRUE(save.is_level_completed(3));
    EXPECT_FALSE(save.is_level_completed(4))
        << "a re-entrant second fold must never mark the UNPLAYED destination "
           "level completed (it would deny its first-win time bonus and arm "
           "the already-completed entity purge)";

    // The mirrored-copy shape: the fold applied once to an identically
    // initialized save lands on the identical result ("last write wins,
    // both equivalent").
    SaveData mirror;
    init_save(mirror, 3);
    mirror.m_score[0] = 100;
    mirror.m_score[1] = 40;
    WinFoldContext mirror_ctx = make_ctx(4, {50, 10, 0, 0});
    mirror_ctx.finished_level = 3;
    apply_win_fold(mirror, fx.world(), mirror_ctx);
    for (int t = 0; t < 4; ++t)
    {
        EXPECT_EQ(cash_after_once[t], mirror.m_totalcash[t]) << "team " << t;
        EXPECT_EQ(score_after_once[t], mirror.m_totalscore[t]) << "team " << t;
    }
    EXPECT_EQ(4, mirror.scen_num);
    EXPECT_FALSE(mirror.is_level_completed(4));
}

// ---------------------------------------------------------------------------
// CTF rematch predicate + fold gating.

TEST(WinFold, ctf_rematch_shape_truth_table)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    SaveData save;
    init_save(save, 500);

    // All four conditions hold -> rematch shape.
    world.type = static_cast<char>(world.type | GameWorld::TYPE_CTF);
    world.ctf.active = true;
    world.ctf.winner_team = 1;
    EXPECT_TRUE(ctf_rematch_shape(world, save, 500));

    // next_level != current level -> a genuine advance, not a rematch.
    EXPECT_FALSE(ctf_rematch_shape(world, save, 501));

    // No decided winner.
    world.ctf.winner_team = -1;
    EXPECT_FALSE(ctf_rematch_shape(world, save, 500));
    world.ctf.winner_team = 1;

    // CTF state not active.
    world.ctf.active = false;
    EXPECT_FALSE(ctf_rematch_shape(world, save, 500));
    world.ctf.active = true;

    // Not a CTF-typed level at all.
    world.type = static_cast<char>(world.type & ~GameWorld::TYPE_CTF);
    EXPECT_FALSE(ctf_rematch_shape(world, save, 500));

    // The (world, current_level, next_level) core matches the SaveData
    // convenience (the curses is_ctf_rematch_end adapter path).
    world.type = static_cast<char>(world.type | GameWorld::TYPE_CTF);
    EXPECT_TRUE(ctf_rematch_shape(world, static_cast<short>(500), 500));
    EXPECT_FALSE(ctf_rematch_shape(world, static_cast<short>(499), 500));
}

TEST(WinFold, rematch_shape_folds_money_but_never_marks_completed)
{
    TestGameWorld fx;
    SaveData save;
    init_save(save, 500);
    save.m_score[0] = 30;

    WinFoldContext ctx = make_ctx(/*next_level=*/500);
    ctx.rematch_shape = true;
    apply_win_fold(save, fx.world(), ctx);

    EXPECT_FALSE(save.is_level_completed(500))
        << "a decided-but-lost CTF match must not mark the level";
    EXPECT_EQ(30u, save.m_totalscore[0]);
    EXPECT_EQ(60u, save.m_totalcash[0]);
    EXPECT_EQ(500, save.scen_num) << "rematch cursor points back at the level";
}

// ---------------------------------------------------------------------------
// Mode policy paths (via a stub progression — tower-independent).

class StubProgression final : public og::mode::IProgression {
public:
    og::mode::ProgressionKind kind() const override
    {
        return og::mode::ProgressionKind::Tower;
    }
    bool marks_level_completed() const override { return false; }
    short advance_cursor(SaveData& save, const GameWorld&,
                         short next_level) override
    {
        ++advance_calls;
        last_requested = next_level;
        return hold ? save.scen_num : redirect_to.value_or(next_level);
    }

    int advance_calls = 0;
    short last_requested = -99;
    bool hold = false;
    std::optional<short> redirect_to;
};

TEST(WinFold, stub_mode_skips_completion_mark)
{
    TestGameWorld fx;
    SaveData save;
    init_save(save, 701);
    save.m_score[0] = 10;

    StubProgression stub;
    apply_win_fold(save, fx.world(), make_ctx(702), stub);

    EXPECT_FALSE(save.is_level_completed(701))
        << "marks_level_completed()==false must suppress add_level_completed";
    EXPECT_EQ(702, save.scen_num) << "cursor still advances through the mode";
    EXPECT_EQ(702, save.current_levels[kCampaign]);
    EXPECT_EQ(20u, save.m_totalcash[0]) << "money folds regardless of policy";
    EXPECT_EQ(1, stub.advance_calls);
    EXPECT_EQ(702, stub.last_requested);
}

TEST(WinFold, second_pass_is_a_no_op_for_no_mark_modes_too)
{
    // Modes that never mark completion (tower) cannot lean on the `already`
    // gate at all — is_level_completed(finished) stays false forever. The
    // on_finished gate is what keeps a re-entrant second pass (stale bonus
    // and all) from re-awarding money or re-running advance_cursor.
    TestGameWorld fx;
    SaveData save;
    init_save(save, 701);
    save.m_score[0] = 10;

    StubProgression stub;
    WinFoldContext ctx = make_ctx(702, {40, 0, 0, 0});
    ctx.finished_level = 701;
    apply_win_fold(save, fx.world(), ctx, stub);
    const std::uint32_t cash_after_once = save.m_totalcash[0];
    EXPECT_EQ(1, stub.advance_calls);

    apply_win_fold(save, fx.world(), ctx, stub); // same stale context
    EXPECT_EQ(cash_after_once, save.m_totalcash[0])
        << "stale bonus must not be re-awarded on the advanced cursor";
    EXPECT_EQ(702, save.scen_num);
    EXPECT_EQ(1, stub.advance_calls)
        << "advance_cursor must not re-run once the cursor left the level";
    EXPECT_FALSE(save.is_level_completed(702));
}

TEST(WinFold, stub_mode_can_hold_the_cursor)
{
    TestGameWorld fx;
    SaveData save;
    init_save(save, 701);

    StubProgression stub;
    stub.hold = true; // provisioning failure -> replay the current level
    apply_win_fold(save, fx.world(), make_ctx(702), stub);

    EXPECT_EQ(701, save.scen_num);
    EXPECT_EQ(701, save.current_levels[kCampaign]);
}

TEST(WinFold, next_level_negative_leaves_cursor_and_mode_untouched)
{
    TestGameWorld fx;
    SaveData save;
    init_save(save, 3);
    save.m_score[0] = 5;

    StubProgression stub;
    apply_win_fold(save, fx.world(), make_ctx(/*next_level=*/-1), stub);

    EXPECT_EQ(3, save.scen_num);
    EXPECT_EQ(0, stub.advance_calls)
        << "advance_cursor must not run without a destination";
    EXPECT_TRUE(save.current_levels.find(kCampaign) == save.current_levels.end())
        << "no cursor write without a destination";
}

TEST(WinFold, current_levels_records_the_advanced_cursor)
{
    TestGameWorld fx;
    SaveData save;
    init_save(save, 3);

    apply_win_fold(save, fx.world(), make_ctx(7));

    EXPECT_EQ(7, save.scen_num);
    ASSERT_TRUE(save.current_levels.find(kCampaign) != save.current_levels.end());
    EXPECT_EQ(7, save.current_levels[kCampaign]);
}

// ---------------------------------------------------------------------------
// Roster rebuild through the fold.

TEST(WinFold, fold_rebuilds_roster_from_surviving_heroes)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();

    walker* alive = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, alive);
    alive->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    alive->myguy->id = 11;
    alive->myguy->name = "SURVIVOR";

    walker* fallen = world.add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_NE(nullptr, fallen);
    fallen->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHER));
    fallen->myguy->id = 12;
    fallen->set_dead(1);

    SaveData save;
    init_save(save, 3);
    apply_win_fold(save, world, make_ctx(4));

    ASSERT_EQ(1, static_cast<int>(save.team_size))
        << "permadeath default drops the fallen hero";
    ASSERT_NE(nullptr, save.team_list[0]);
    EXPECT_EQ(11, save.team_list[0]->id);
    EXPECT_EQ("SURVIVOR", save.team_list[0]->name);
}

} // namespace
