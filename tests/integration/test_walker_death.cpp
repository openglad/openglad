#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/gloader.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

static GameWorld& death_world()
{
    return og::runtime::current_session->myscreen_->world();
}

static std::unique_ptr<walker> make_guy(char family, unsigned char team = 0, short level = 3)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(100, 100);
    return w;
}

static std::unique_ptr<walker> make_loaded(char family)
{
    auto w = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Living, family);
    if (w) w->setxy(100, 100);
    return w;
}

// Counts (and finds the newest of) the entities walker::death() is supposed to
// leave behind. add_ob/add_fx_ob append, so the LAST match is the fresh one.
static int count_of(const GameWorld::EntityList& list, Order order, int family)
{
    int found = 0;
    for (const auto& uptr : list)
        if (uptr && uptr->query_order() == order && uptr->family() == family)
            ++found;
    return found;
}

static walker* newest_of(const GameWorld::EntityList& list, Order order, int family)
{
    walker* found = nullptr;
    for (const auto& uptr : list)
        if (uptr && uptr->query_order() == order && uptr->family() == family)
            found = uptr.get();
    return found;
}

static int gem_count()   { return count_of(death_world().oblist, Order::Treasure, FAMILY_LIFE_GEM); }
static int stain_count() { return count_of(death_world().fxlist, Order::Treasure, FAMILY_STAIN); }

// The hearts, bloodspots, slime offspring and parting shots a death drops are
// real entities in the SHARED world. Every check below is delta-based and so
// survives residue, but residue left in oblist/fxlist/weaplist is order
// coupling waiting to happen for every later test in this binary: each test
// takes its own spawn back out. add_ob/add_fx_ob append and nothing here
// removes entities, so the tail past the recorded size IS what this test
// spawned.
class ScopedDeathSpawns
{
public:
    ScopedDeathSpawns()
        : obs_(death_world().oblist.size()),
          fxs_(death_world().fxlist.size()),
          weaps_(death_world().weaplist.size()),
          deads_(death_world().dead_list.size())
    {}
    ScopedDeathSpawns(const ScopedDeathSpawns&) = delete;
    ScopedDeathSpawns& operator=(const ScopedDeathSpawns&) = delete;
    ~ScopedDeathSpawns()
    {
        trim(death_world().oblist, obs_);
        trim(death_world().fxlist, fxs_);
        trim(death_world().weaplist, weaps_);
        trim(death_world().dead_list, deads_);
    }

private:
    static void trim(GameWorld::EntityList& list, std::size_t keep)
    {
        while (list.size() > keep)
            list.pop_back();
    }

    std::size_t obs_;
    std::size_t fxs_;
    std::size_t weaps_;
    std::size_t deads_;
};

// The one rule every ordinary family shares: a guy-owned walker's death drops
// exactly one team-colored life gem into oblist, and the family descriptor's
// leaves_bloodspot decides whether exactly one FAMILY_STAIN joins fxlist.
static void check_family_death(const char* label, char family, unsigned char team,
                               int expected_stain_delta)
{
    SCOPED_TRACE(label);
    ScopedDeathSpawns spawns;
    auto w = make_guy(family, team);
    ASSERT_NE(nullptr, w.get()) << label << ": guy_create_walker_owned must build a walker";
    ASSERT_NE(nullptr, w->myguy) << label << ": a guy-owned walker carries its myguy";

    const int gems_before = gem_count();
    const int stains_before = stain_count();
    const float heart = static_cast<float>(w->myguy->query_heart_value());

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << label << ": death() reports it ran the death effects";

    ASSERT_EQ(gems_before + 1, gem_count())
        << label << ": exactly one FAMILY_LIFE_GEM drops for a guy-owned walker";
    ASSERT_EQ(stains_before + expected_stain_delta, stain_count())
        << label << ": leaves_bloodspot decides the FAMILY_STAIN count";
    ASSERT_NE(0, static_cast<int>(w->death_called()))
        << label << ": death_called latches so a second death() is a no-op";

    walker* gem = newest_of(death_world().oblist, Order::Treasure, FAMILY_LIFE_GEM);
    ASSERT_NE(nullptr, gem) << label << ": the dropped gem must be findable in oblist";
    ASSERT_FLOAT_EQ(heart * 0.375f, gem->stats()->hitpoints())
        << label << ": gem value is query_heart_value() * 0.75 / 2";
    ASSERT_EQ(static_cast<int>(team), static_cast<int>(gem->team_num()))
        << label << ": the gem inherits the dead walker's team";
    ASSERT_EQ(static_cast<int>(w->floor()), static_cast<int>(gem->floor()))
        << label << ": the gem drops on the floor we died on";
    ASSERT_EQ(w->xpos() + w->sizex() / 2 - gem->sizex() / 2, gem->xpos())
        << label << ": the gem is centred on the corpse (x)";
    ASSERT_EQ(w->ypos() + w->sizey() / 2 - gem->sizey() / 2, gem->ypos())
        << label << ": the gem is centred on the corpse (y)";

    if (expected_stain_delta > 0)
    {
        walker* stain = newest_of(death_world().fxlist, Order::Treasure, FAMILY_STAIN);
        ASSERT_NE(nullptr, stain) << label << ": the bloodspot must be findable in fxlist";
        ASSERT_EQ(static_cast<int>(team), static_cast<int>(stain->team_num()))
            << label << ": the bloodspot inherits the dead walker's team";
        ASSERT_EQ(w->xpos(), stain->xpos()) << label << ": the bloodspot lands on the corpse (x)";
        ASSERT_EQ(w->ypos(), stain->ypos()) << label << ": the bloodspot lands on the corpse (y)";
        ASSERT_EQ(0, static_cast<int>(stain->dead())) << label << ": the bloodspot is a live prop";
        ASSERT_NE(0, static_cast<int>(stain->ignore()))
            << label << ": the bloodspot is walked over, not collided with";
    }
}

// ---------------------------------------------------------------------------
// walker::death - various family-specific death behaviors
// ---------------------------------------------------------------------------

// One table for every family whose descriptor sets leaves_bloodspot = true and
// registers no on_death hook: soldier, elf, archer, mage, cleric, faerie,
// druid, barbarian, thief. Each must drop a heart AND one bloodspot.
TEST(WalkerDeath, soldier_and_kin_drop_heart_and_bloodspot)
{
    struct Row { const char* label; char family; unsigned char team; };
    const Row rows[] = {
        {"soldier",   FAMILY_SOLDIER,   0},
        {"mage",      FAMILY_MAGE,      0},
        {"faerie",    FAMILY_FAERIE,    0},
        {"archer",    FAMILY_ARCHER,    0},
        {"cleric",    FAMILY_CLERIC,    0},
        {"druid",     FAMILY_DRUID,     0},
        {"thief",     FAMILY_THIEF,     0},
        {"elf",       FAMILY_ELF,       0},
        {"barbarian", FAMILY_BARBARIAN, 1},
    };
    for (const Row& row : rows)
        check_family_death(row.label, row.family, row.team, 1);
}


// leaves_bloodspot = false: a skeleton's bones leave no blood, but the heart
// still drops.
TEST(WalkerDeath, skeleton)
{
    ScopedDeathSpawns spawns;
    auto w = make_guy(FAMILY_SKELETON, 0);
    ASSERT_NE(nullptr, w.get()) << "skeleton walker must be built";
    const int gems_before = gem_count();
    const int stains_before = stain_count();

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() runs the death effects";

    ASSERT_EQ(stains_before, stain_count())
        << "skeleton has leaves_bloodspot = false: no FAMILY_STAIN is added";
    ASSERT_EQ(gems_before + 1, gem_count())
        << "the life gem still drops for a guy-owned skeleton";
}


// living-06-elemental.lua on_death: un-kill, refund the special, fire the
// default weapon in all eight directions, re-kill. The hook consumes the death,
// so no bloodspot is generated.
TEST(WalkerDeath, fire_elemental2)
{
    ScopedDeathSpawns spawns;
    auto w = make_guy(FAMILY_FIREELEMENTAL, 0);
    ASSERT_NE(nullptr, w.get()) << "fire elemental walker must be built";
    w->stats()->set_magicpoints(999.0f);
    const unsigned short weapon_family = w->current_weapon();
    const int shots_before = count_of(death_world().weaplist, Order::Weapon,
                                     static_cast<int>(weapon_family));
    const int stains_before = stain_count();
    const int gems_before = gem_count();

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() runs the elemental's parting starburst";

    ASSERT_EQ(shots_before + 8, count_of(death_world().weaplist, Order::Weapon,
                                        static_cast<int>(weapon_family)))
        << "the parting starburst fires the default weapon in all 8 directions";
    ASSERT_EQ(stains_before, stain_count())
        << "an on_death hook consumes the death: no bloodspot";
    ASSERT_EQ(1, static_cast<int>(w->dead()))
        << "the hook un-kills to cast and re-kills afterwards";
    ASSERT_EQ(gems_before + 1, gem_count()) << "a guy-owned elemental still drops its heart";
}


// living-08-slime.lua registers on_death for SLIME and MEDIUM_SLIME only: the
// small slime is the end of the chain and splits into nothing.
TEST(WalkerDeath, small_slime)
{
    ScopedDeathSpawns spawns;
    auto w = make_guy(FAMILY_SMALL_SLIME, 0);
    ASSERT_NE(nullptr, w.get()) << "small slime walker must be built";
    const int small_before = count_of(death_world().oblist, Order::Living, FAMILY_SMALL_SLIME);
    const int medium_before = count_of(death_world().oblist, Order::Living, FAMILY_MEDIUM_SLIME);
    const int stains_before = stain_count();
    const int gems_before = gem_count();

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() runs the death effects";

    ASSERT_EQ(small_before, count_of(death_world().oblist, Order::Living, FAMILY_SMALL_SLIME))
        << "the small slime is the last size: it spawns no offspring";
    ASSERT_EQ(medium_before, count_of(death_world().oblist, Order::Living, FAMILY_MEDIUM_SLIME))
        << "the small slime never splits upward";
    ASSERT_EQ(stains_before + 1, stain_count())
        << "with no on_death hook the small slime leaves a bloodspot";
    ASSERT_EQ(gems_before + 1, gem_count()) << "guy-owned: the heart drops";
}


// medium_slime_on_death -> split_on_death(LIVING_SMALL_SLIME): exactly one
// small-slime offspring, team/level/floor copied, centred on the corpse, and
// the dying blob's hp restored to max.
TEST(WalkerDeath, medium_slime)
{
    ScopedDeathSpawns spawns;
    auto w = make_loaded(FAMILY_MEDIUM_SLIME);
    ASSERT_NE(nullptr, w.get()) << "medium slime walker must be built";
    w->set_team_num(2);
    w->stats()->set_level(4);
    w->stats()->set_hitpoints(1.0f);
    const float max_hp = w->stats()->max_hitpoints();
    const int small_before = count_of(death_world().oblist, Order::Living, FAMILY_SMALL_SLIME);
    const int stains_before = stain_count();
    const int gems_before = gem_count();

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() runs the medium slime's split";

    ASSERT_EQ(small_before + 1, count_of(death_world().oblist, Order::Living, FAMILY_SMALL_SLIME))
        << "a medium slime is replaced by exactly one small slime";
    walker* child = newest_of(death_world().oblist, Order::Living, FAMILY_SMALL_SLIME);
    ASSERT_NE(nullptr, child) << "the offspring must be in oblist";
    ASSERT_EQ(2, static_cast<int>(child->team_num())) << "the offspring inherits the team";
    ASSERT_EQ(4, child->stats()->level()) << "the offspring inherits the level";
    ASSERT_EQ(static_cast<int>(w->floor()), static_cast<int>(child->floor()))
        << "the offspring stays on our floor";
    ASSERT_EQ(w->xpos() + w->sizex() / 2 - child->sizex() / 2, child->xpos())
        << "the offspring is centred on the corpse";
    ASSERT_FLOAT_EQ(max_hp, w->stats()->hitpoints())
        << "split_on_death restores the dying blob's hp to max";
    ASSERT_EQ(stains_before, stain_count()) << "the on_death hook consumes the death: no bloodspot";
    ASSERT_EQ(gems_before, gem_count()) << "a loader-built slime has no myguy: no heart";
}


// slime_on_death -> split_on_death(LIVING_MEDIUM_SLIME).
TEST(WalkerDeath, large_slime)
{
    ScopedDeathSpawns spawns;
    auto w = make_loaded(FAMILY_SLIME);
    ASSERT_NE(nullptr, w.get()) << "large slime walker must be built";
    w->set_team_num(1);
    const int medium_before = count_of(death_world().oblist, Order::Living, FAMILY_MEDIUM_SLIME);
    const int small_before = count_of(death_world().oblist, Order::Living, FAMILY_SMALL_SLIME);
    const int stains_before = stain_count();

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() runs the large slime's split";

    ASSERT_EQ(medium_before + 1, count_of(death_world().oblist, Order::Living, FAMILY_MEDIUM_SLIME))
        << "a large slime is replaced by exactly one medium slime";
    ASSERT_EQ(small_before, count_of(death_world().oblist, Order::Living, FAMILY_SMALL_SLIME))
        << "the large slime skips no size: it never drops straight to small";
    walker* child = newest_of(death_world().oblist, Order::Living, FAMILY_MEDIUM_SLIME);
    ASSERT_NE(nullptr, child) << "the offspring must be in oblist";
    ASSERT_EQ(static_cast<int>(w->team_num()), static_cast<int>(child->team_num()))
        << "the offspring inherits the team";
    ASSERT_EQ(stains_before, stain_count()) << "the on_death hook consumes the death: no bloodspot";
}


// living-12-ghost.lua: leaves_bloodspot = false.
TEST(WalkerDeath, ghost)
{
    ScopedDeathSpawns spawns;
    auto w = make_guy(FAMILY_GHOST, 0);
    ASSERT_NE(nullptr, w.get()) << "ghost walker must be built";
    const int stains_before = stain_count();
    const int gems_before = gem_count();

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() runs the death effects";

    ASSERT_EQ(stains_before, stain_count())
        << "ghost has leaves_bloodspot = false: no FAMILY_STAIN is added";
    ASSERT_EQ(gems_before + 1, gem_count()) << "the life gem still drops for a guy-owned ghost";
}


// The heart is the `if (myguy)` arm of walker::death(): value, team, floor and
// position, plus the keep_fallen_heroes halving and the no-myguy control.
TEST(WalkerDeath, myguy_present)
{
    ScopedDeathSpawns spawns;
    auto w = make_guy(FAMILY_SOLDIER, 3);
    ASSERT_NE(nullptr, w.get()) << "soldier walker must be built";
    ASSERT_TRUE(w->myguy != nullptr) << "should have myguy from guy::create_walker_owned";

    const short saved_keep = death_world().keep_fallen_heroes;
    death_world().keep_fallen_heroes = 0;  // permadeath on: full-value salvage

    const float heart = static_cast<float>(w->myguy->query_heart_value());
    int gems_before = gem_count();
    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() drops the heart";
    ASSERT_EQ(gems_before + 1, gem_count()) << "a myguy-owned walker drops exactly one heart";
    walker* gem = newest_of(death_world().oblist, Order::Treasure, FAMILY_LIFE_GEM);
    ASSERT_NE(nullptr, gem) << "the heart must be in oblist";
    ASSERT_FLOAT_EQ(heart * 0.375f, gem->stats()->hitpoints())
        << "permadeath on: heart value is query_heart_value() * 0.75 / 2";
    ASSERT_EQ(3, static_cast<int>(gem->team_num())) << "the heart inherits our team";
    ASSERT_EQ(static_cast<int>(w->floor()), static_cast<int>(gem->floor()))
        << "the heart drops on the floor we died on";
    ASSERT_EQ(w->xpos() + w->sizex() / 2 - gem->sizex() / 2, gem->xpos())
        << "the heart is centred on the corpse (x)";
    ASSERT_EQ(w->ypos() + w->sizey() / 2 - gem->sizey() / 2, gem->ypos())
        << "the heart is centred on the corpse (y)";

    // Permadeath off: the fallen hero comes back with their growth, so the
    // consolation heart is worth half.
    death_world().keep_fallen_heroes = 1;
    auto w2 = make_guy(FAMILY_SOLDIER, 3);
    ASSERT_NE(nullptr, w2.get()) << "second soldier walker must be built";
    const float heart2 = static_cast<float>(w2->myguy->query_heart_value());
    gems_before = gem_count();
    w2->set_dead(1);
    ASSERT_TRUE(w2->death()) << "death() drops the heart";
    ASSERT_EQ(gems_before + 1, gem_count()) << "still exactly one heart";
    walker* gem2 = newest_of(death_world().oblist, Order::Treasure, FAMILY_LIFE_GEM);
    ASSERT_NE(nullptr, gem2) << "the second heart must be in oblist";
    ASSERT_FLOAT_EQ(heart2 * 0.375f * 0.5f, gem2->stats()->hitpoints())
        << "keep_fallen_heroes halves the heart";
    death_world().keep_fallen_heroes = saved_keep;

    // Control: no myguy, no heart.
    auto plain = make_loaded(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, plain.get()) << "loader-built soldier must be built";
    plain->clear_myguy();
    ASSERT_TRUE(plain->myguy == nullptr) << "the control walker owns no guy";
    gems_before = gem_count();
    plain->set_dead(1);
    ASSERT_TRUE(plain->death()) << "death() still runs for an anonymous walker";
    ASSERT_EQ(gems_before, gem_count()) << "no myguy means no heart at all";
}


// The gem/stain inherit team_num(), not a hardcoded team 0.
TEST(WalkerDeath, orc)
{
    ScopedDeathSpawns spawns;
    auto w = make_guy(FAMILY_ORC, 1);
    ASSERT_NE(nullptr, w.get()) << "orc walker must be built";
    const int stains_before = stain_count();
    const int gems_before = gem_count();

    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "death() runs the death effects";

    ASSERT_EQ(gems_before + 1, gem_count()) << "the orc's heart drops";
    walker* gem = newest_of(death_world().oblist, Order::Treasure, FAMILY_LIFE_GEM);
    ASSERT_NE(nullptr, gem) << "the heart must be in oblist";
    ASSERT_EQ(1, static_cast<int>(gem->team_num())) << "a team-1 orc drops a team-1 heart";
    ASSERT_EQ(stains_before + 1, stain_count()) << "orc leaves_bloodspot = true";
    walker* stain = newest_of(death_world().fxlist, Order::Treasure, FAMILY_STAIN);
    ASSERT_NE(nullptr, stain) << "the bloodspot must be in fxlist";
    ASSERT_EQ(1, static_cast<int>(stain->team_num())) << "the bloodspot is team-1 too";
}


// ---------------------------------------------------------------------------
// walker::death double-call protection
// ---------------------------------------------------------------------------

TEST(WalkerDeath, double_call)
{
    ScopedDeathSpawns spawns;
    auto w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w.get()) << "soldier walker must be built";
    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "the first death() runs the effects";
    const int gems_after_first = gem_count();
    const int stains_after_first = stain_count();
    bool result = w->death();
    ASSERT_TRUE(!result) << "second death call returns false";
    ASSERT_EQ(gems_after_first, gem_count()) << "the second death() drops no second heart";
    ASSERT_EQ(stains_after_first, stain_count()) << "the second death() leaves no second bloodspot";
}


// ---------------------------------------------------------------------------
// walker::compute_outline
// ---------------------------------------------------------------------------

TEST(WalkerDeath, walker_compute_outline_invulnerable)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w.get()) << "soldier walker must be built";
    w->set_outline(0);
    w->set_invulnerable_left(10);
    w->set_invisibility_left(0);
    w->set_flight_left(0);

    w->compute_outline(nullptr);

    ASSERT_EQ(static_cast<int>(OUTLINE_INVULNERABLE), static_cast<int>(w->outline()))
        << "invulnerability alone paints the OUTLINE_INVULNERABLE ramp";
}


TEST(WalkerDeath, walker_compute_outline_flying)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w.get()) << "soldier walker must be built";
    w->set_outline(0);
    w->set_flight_left(10);
    w->set_invisibility_left(0);
    w->set_invulnerable_left(0);

    w->compute_outline(nullptr);

    ASSERT_EQ(static_cast<int>(OUTLINE_FLYING), static_cast<int>(w->outline()))
        << "flight alone paints the OUTLINE_FLYING ramp";
}


TEST(WalkerDeath, walker_compute_outline_invisible)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w.get()) << "soldier walker must be built";
    w->set_outline(0);
    w->set_invisibility_left(10);
    w->set_flight_left(0);
    w->set_invulnerable_left(0);

    w->compute_outline(nullptr);

    ASSERT_EQ(static_cast<int>(w->query_team_color()), static_cast<int>(w->outline()))
        << "invisibility paints the walker's own team ramp";
    ASSERT_EQ(40, static_cast<int>(w->outline()))
        << "team 0's ramp base is 40 (team * 16 + 40)";
}


TEST(WalkerDeath, walker_compute_outline_no_status)
{
    auto w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w.get()) << "soldier walker must be built";
    w->set_outline(OUTLINE_FLYING);  // a stale outline from a lapsed status
    w->set_invisibility_left(0);
    w->set_flight_left(0);
    w->set_invulnerable_left(0);

    w->compute_outline(nullptr);

    ASSERT_EQ(0, static_cast<int>(w->outline()))
        << "no status and no BIT_NAMED clears the outline back to 0";
}
