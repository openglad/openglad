#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include <unordered_set>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace {

std::unordered_set<walker*> snapshot_ptrs(
    const std::list<std::unique_ptr<walker>>& lst)
{
    std::unordered_set<walker*> out;
    out.reserve(lst.size());
    for (auto& up : lst)
        out.insert(up.get());
    return out;
}

// The single walker of `order`/`family` that appeared in `lst` since `before`.
// Returns nullptr when none (or more than one) showed up, so the caller's
// ASSERT_NE names the missing spawn.
walker* only_new(const std::list<std::unique_ptr<walker>>& lst,
                 const std::unordered_set<walker*>& before,
                 Order order, int family)
{
    walker* found = nullptr;
    int count = 0;
    for (auto& up : lst) {
        walker* w = up.get();
        if (!w || before.contains(w))
            continue;
        if (w->query_order() != order || w->family() != family)
            continue;
        found = w;
        ++count;
    }
    return count == 1 ? found : nullptr;
}

} // namespace

// effect_ghost_scare.lua on_death: every LIVING foe inside the caster's scare
// radius (50 + 10*L => 100 px at L5) is pushed into a forced flee-walk away
// from the cloud — a forced COMMAND_WALK at the queue front whose direction is
// the sign of the foe's offset from the cloud and whose count is the L5 scare
// duration (legacy 25*L = 125, below the 325 knee). Allies and foes outside
// the radius are left alone.
TEST(EffectDeathMore, effect_death_ghost_scare_forces_walk_commands_on_foes)
{
    LevelRuntimeData& level =
        og::runtime::current_session->myscreen_->level_runtime_data();
    level.world().create_new_grid();

    walker* ghost = level.world().add_ob(Order::Living, FAMILY_GHOST);
    walker* foe1 = level.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe2 = level.world().add_ob(Order::Living, FAMILY_ORC);
    walker* ally = level.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* far = level.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, ghost) << "ghost created";
    ASSERT_NE(nullptr, foe1) << "foe1 created";
    ASSERT_NE(nullptr, foe2) << "foe2 created";
    ASSERT_NE(nullptr, ally) << "ally created";
    ASSERT_NE(nullptr, far) << "distant foe created";

    ghost->set_team_num(1);
    foe1->set_team_num(2);
    foe2->set_team_num(2);
    ally->set_team_num(1);  // same team as the caster: never frightened
    far->set_team_num(2);

    ghost->stats()->set_level(5);
    ghost->setxy(GRID_SIZE * 10, GRID_SIZE * 10);
    foe1->setxy(GRID_SIZE * 11, GRID_SIZE * 10);  // +32 px east
    foe2->setxy(GRID_SIZE * 9, GRID_SIZE * 10);   // -32 px west
    ally->setxy(GRID_SIZE * 10, GRID_SIZE * 11);
    far->setxy(GRID_SIZE * 10, GRID_SIZE * 20);   // 320 px away: out of radius

    // A fresh queue on every victim, so the merge branch is not in play and
    // the injected entry IS the queue front.
    for (walker* w : {foe1, foe2, ally, far})
        w->stats()->clear_command();

    // Spawn the scare FX and trigger death() directly.
    walker* scare = level.world().add_ob(Order::FX, FAMILY_GHOST_SCARE);
    ASSERT_NE(nullptr, scare) << "scare effect created";
    scare->set_owner(ghost);
    scare->setxy(ghost->xpos(), ghost->ypos());
    scare->set_dead(1);

    ASSERT_TRUE(scare->death()) << "the scare cloud handled its own death";

    // The L5 radius is 100 px; both close foes are 32 px out.
    ASSERT_EQ(100, og::combat::scare_radius(5)) << "L5 scare radius";
    ASSERT_EQ(125, og::combat::scare_duration(5)) << "L5 scare duration";

    struct Expect { walker* victim; int dx; int dy; const char* who; };
    for (const Expect& e : {Expect{foe1, 1, 0, "foe east of the cloud"},
                            Expect{foe2, -1, 0, "foe west of the cloud"}}) {
        ASSERT_TRUE(e.victim->stats()->has_commands())
            << e.who << " must be frightened";
        const command& front = e.victim->stats()->commands.front();
        EXPECT_EQ(COMMAND_WALK, front.commandtype)
            << e.who << ": fright is a forced walk";
        EXPECT_TRUE(front.forced) << e.who << ": the walk is externally forced";
        EXPECT_EQ(125, front.commandcount) << e.who << ": L5 fright duration";
        EXPECT_EQ(e.dx, front.com1) << e.who << ": flees along +x sign";
        EXPECT_EQ(e.dy, front.com2) << e.who << ": flees along +y sign";
    }

    EXPECT_FALSE(ally->stats()->has_commands())
        << "a friendly living beside the cloud is not a foe and is not scared";
    EXPECT_FALSE(far->stats()->has_commands())
        << "a foe 320 px out is past the 100 px radius";

    level.delete_objects();
}

// effect_bomb.lua bomb_on_death: the bomb carries no blast of its own; dying
// spawns the EXPLOSION effect into the oblist, centred on the bomb and
// inheriting owner, the owner's level, the bomb's damage and floor, at hp 0
// with ANI_EXPLODE.
TEST(EffectDeathMore, effect_death_bomb_spawns_explosion_with_owner_and_damage)
{
    LevelRuntimeData& level =
        og::runtime::current_session->myscreen_->level_runtime_data();
    level.world().create_new_grid();

    walker* owner = level.world().add_ob(Order::Living, FAMILY_THIEF);
    ASSERT_NE(nullptr, owner) << "owner created";

    owner->stats()->set_level(3);
    owner->setxy(GRID_SIZE * 8, GRID_SIZE * 8);

    walker* bomb = level.world().add_ob(Order::FX, FAMILY_BOMB);
    ASSERT_NE(nullptr, bomb) << "bomb created";
    bomb->set_owner(owner);
    bomb->set_damage(12.0f);
    bomb->setxy(GRID_SIZE * 8, GRID_SIZE * 8);
    bomb->set_dead(1);

    const auto ob_before = snapshot_ptrs(level.world().oblist);
    ASSERT_TRUE(bomb->death()) << "the bomb handled its own death";

    walker* blast = only_new(level.world().oblist, ob_before, Order::FX,
                             FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, blast)
        << "a dying bomb spawns exactly one EXPLOSION effect into the oblist";
    EXPECT_EQ(owner, blast->owner())
        << "the blast answers to the bomb's owner (kill credit / friendly cuts)";
    EXPECT_FLOAT_EQ(12.0f, blast->damage())
        << "the blast carries the bomb's damage";
    EXPECT_FLOAT_EQ(0.0f, blast->stats()->hitpoints()) << "the blast spawns at hp 0";
    EXPECT_EQ(ANI_EXPLODE, static_cast<int>(blast->ani_type()))
        << "the blast plays the explode animation";
    EXPECT_EQ(3, blast->stats()->level()) << "the blast inherits the owner's level";
    EXPECT_EQ(bomb->floor(), blast->floor())
        << "the blast detonates on the bomb's floor";
    // center_on lands the blast so the two boxes share a centre (integer
    // halves, so the squared centre distance can be 1 or 2 on odd sizes).
    EXPECT_EQ(bomb->xpos() + bomb->sizex() / 2 - blast->sizex() / 2,
              blast->xpos())
        << "the blast is centred on the bomb in x";
    EXPECT_EQ(bomb->ypos() + bomb->sizey() / 2 - blast->sizey() / 2,
              blast->ypos())
        << "the blast is centred on the bomb in y";

    level.delete_objects();
}
