#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <cstdlib>
#include <string>
#include <vector>
#include "test_family_hook_dispatch.h"

// myscreen is now a macro defined in base.h (via game_session.h)

static walker* make_weapon(char family)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, family);
    if (w) {
        w->setxy(100, 100);
        w->set_owner(w);
    }
    return w;
}

static std::unique_ptr<walker> make_living(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
        w->setxy(100, 100);
    return w;
}

static GameWorld& test_world()
{
    return og::runtime::current_session->myscreen_->world();
}

// Paint one tile of the default floor's grid (world/pixel coordinates in).
static void set_world_tile(short world_x, short world_y, unsigned char tile)
{
    auto& level = og::runtime::current_session->myscreen_->level_runtime_data();
    const int gx = world_x / GRID_SIZE;
    const int gy = world_y / GRID_SIZE;
    if (gx < 0 || gy < 0 || gx >= level.world().grid.w || gy >= level.world().grid.h)
        return;
    level.world().grid.data[static_cast<std::size_t>(gx + level.world().grid.w * gy)] = tile;
}

// Every walker of one order+family currently in the world. Effect family ids
// collide numerically with weapon/living ids, so the ORDER is what identifies
// a spawned FX; and the spawning hooks use all three lists (og.add_ob lands in
// oblist, og.add_fx_ob in fxlist, og.add_weap_ob in weaplist), so all three
// are scanned.
static std::vector<walker*> collect_entities(Order order, int family)
{
    std::vector<walker*> found;
    const GameWorld::EntityList* lists[] = {&test_world().oblist,
                                            &test_world().fxlist,
                                            &test_world().weaplist};
    for (const auto* list : lists)
        for (const auto& e : *list)
            if (e->query_order() == order
                && static_cast<int>(static_cast<unsigned char>(e->family())) == family)
                found.push_back(e.get());
    return found;
}

// The single entity present in `after` but not in `before` (nullptr when the
// count did not grow by exactly one).
static walker* only_new_entity(const std::vector<walker*>& before,
                               const std::vector<walker*>& after)
{
    walker* found = nullptr;
    for (walker* w : after)
    {
        if (std::find(before.begin(), before.end(), w) != before.end())
            continue;
        if (found != nullptr)
            return nullptr;
        found = w;
    }
    return found;
}

static og::sim::SimEventLog& sim_log()
{
    return *current_game->sim_events;
}

// Fresh, readable sim event log. Weapon sit/random announcements land here.
static void reset_sim_log()
{
    ASSERT_NE(nullptr, current_game) << "gameplay context installed";
    ASSERT_NE(nullptr, current_game->sim_events) << "sim event log available";
    ASSERT_FALSE(current_game->sim_events->suppressed())
        << "sim event log must not be suppressed or every oracle below is vacuous";
    current_game->sim_events->clear();
}

static int count_notifications(const std::string& text)
{
    int n = 0;
    for (const auto& ev : sim_log().events())
        if (ev.kind == og::sim::EventKind::Notification && ev.text == text)
            ++n;
    return n;
}

static int count_sounds(int sound_id)
{
    int n = 0;
    for (const auto& ev : sim_log().events())
        if (ev.kind == og::sim::EventKind::PlaySound
            && ev.a == static_cast<std::uint32_t>(sound_id))
            ++n;
    return n;
}

// Shared body for the three silent-sit families (tree/blood/door): the family
// declares skip_sit_notify=true, so weap::act's ACT_SIT arm must announce
// nothing at all. weap_act_sit_with_non_skipping_family_and_act_animate_shortcut
// is the positive twin.
static void expect_silent_sit(char family, const char* what)
{
    walker* w = make_weapon(family);
    ASSERT_NE(nullptr, w) << what << " weapon created";
    const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(family);
    ASSERT_NE(nullptr, wfd) << what << " weapon family descriptor exists";
    ASSERT_TRUE(wfd->skip_sit_notify) << what << " declares skip_sit_notify=true";

    reset_sim_log();
    w->set_ani_type(ANI_WALK); // otherwise act() short-circuits into animate()
    w->set_act_type(ACT_SIT);
    ASSERT_TRUE(w->act()) << what << " sit returns 1";
    EXPECT_EQ(0, count_notifications("Weapon sitting"))
        << what << " sits silently (skip_sit_notify)";
    EXPECT_EQ(0u, sim_log().size())
        << what << " sitting pushes no sim event of any kind";

    test_world().remove_ob(w);
}

// ---------------------------------------------------------------------------
// weap::act - various act types
// ---------------------------------------------------------------------------

TEST(WeapBehavior, weap_act_fire)
{
    test_world().create_new_grid();
    walker* w = make_weapon(FAMILY_KNIFE);
    ASSERT_NE(nullptr, w) << "knife weapon created";
    w->setxy(100, 100);
    w->set_act_type(ACT_FIRE);
    w->set_ani_type(ANI_WALK); // otherwise act() short-circuits into animate()
    w->set_lastx(1);
    w->set_lasty(0);
    w->set_dead(0);
    w->set_death_called(0);
    w->set_lineofsight(5);

    // act_fire() spends exactly one point of range per tick, before it walks,
    // so this holds whether or not the knife collides with anything.
    ASSERT_TRUE(w->act()) << "ACT_FIRE must route through act_fire() and return 1";
    ASSERT_EQ(4, (int)w->lineofsight()) << "act_fire spends one point of range per tick";

    // Range already exhausted: the projectile dies on this tick.
    w->set_ani_type(ANI_WALK);
    w->set_dead(0);
    w->set_death_called(0);
    w->set_lineofsight(0);
    ASSERT_TRUE(w->act()) << "ACT_FIRE returns 1 even on the killing tick";
    ASSERT_EQ(1, (int)w->dead()) << "a projectile whose range ran out dies";

    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_sit_tree)
{
    expect_silent_sit(FAMILY_TREE, "core:tree");
}


TEST(WeapBehavior, weap_act_sit_blood)
{
    expect_silent_sit(FAMILY_BLOOD, "core:blood");
}


TEST(WeapBehavior, weap_act_sit_door)
{
    expect_silent_sit(FAMILY_DOOR, "core:door");
}


TEST(WeapBehavior, weap_act_die)
{
    walker* w = make_weapon(FAMILY_KNIFE);
    ASSERT_NE(nullptr, w) << "knife weapon created";
    w->set_ani_type(ANI_WALK); // otherwise act() short-circuits into animate()
    w->set_dead(0);
    w->set_act_type(ACT_DIE);
    ASSERT_TRUE(w->act()) << "ACT_DIE returns 1";
    ASSERT_EQ(1, (int)w->dead()) << "weap act die sets dead";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_random)
{
    walker* w = make_weapon(FAMILY_KNIFE);
    ASSERT_NE(nullptr, w) << "knife weapon created";
    w->set_team_num(3);
    w->set_ani_type(ANI_WALK); // otherwise act() short-circuits into animate()
    w->set_act_type(ACT_RANDOM);

    reset_sim_log();
    ASSERT_TRUE(w->act()) << "ACT_RANDOM returns 1";
    ASSERT_EQ(1u, sim_log().size()) << "ACT_RANDOM pushes exactly one sim event";
    const og::sim::Event& ev = sim_log().events()[0];
    EXPECT_EQ(og::sim::EventKind::Notification, ev.kind) << "the act_random report is a Notification";
    EXPECT_EQ("Weapon 0 doing act random?", ev.text) << "the report names the weapon family number";
    EXPECT_EQ((std::uint32_t)FAMILY_KNIFE, ev.a) << "payload a is the weapon family";
    EXPECT_EQ((std::uint32_t)3, ev.b) << "payload b is the weapon's team";

    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


// ---------------------------------------------------------------------------
// weap::death - various weapon families
// ---------------------------------------------------------------------------

TEST(WeapBehavior, weap_death_knife_soldier_owner)
{
    auto owner = make_living(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, owner.get()) << "soldier owner created";

    walker* knife = make_weapon(FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife) << "knife weapon created";
    knife->set_owner(owner.get());
    knife->setxy(120, 144);
    knife->set_lastx(3);
    knife->set_lasty(-2);
    knife->set_stepsize(4);
    knife->set_damage(9.0f);
    knife->set_dead(1);

    const auto before = collect_entities(Order::FX, FAMILY_KNIFE_BACK);
    ASSERT_TRUE(knife->death()) << "weap::death dispatches the family hook and reports handled";
    const auto after = collect_entities(Order::FX, FAMILY_KNIFE_BACK);
    ASSERT_EQ(before.size() + 1, after.size())
        << "a returning-weapon owner's knife spawns exactly one core:knife_back";
    walker* blade = only_new_entity(before, after);
    ASSERT_NE(nullptr, blade) << "exactly one new knife_back effect";

    EXPECT_EQ(owner.get(), blade->owner()) << "the returning blade belongs to the thrower";
    EXPECT_EQ(ANI_ATTACK, (int)blade->ani_type()) << "the returning blade animates as an attack";
    EXPECT_FLOAT_EQ(3.0f, blade->lastx()) << "the blade inherits the knife's X velocity";
    EXPECT_FLOAT_EQ(-2.0f, blade->lasty()) << "the blade inherits the knife's Y velocity";
    EXPECT_FLOAT_EQ(4.0f, blade->stepsize()) << "the blade inherits the knife's stepsize";
    EXPECT_FLOAT_EQ(9.0f, blade->damage()) << "the blade inherits the knife's damage";
    EXPECT_EQ((int)knife->floor(), (int)blade->floor()) << "return flight starts on the knife's floor";
    EXPECT_EQ((int)knife->xpos() + (int)knife->sizex() / 2,
              (int)blade->xpos() + (int)blade->sizex() / 2)
        << "the blade is centered on the knife (X)";
    EXPECT_EQ((int)knife->ypos() + (int)knife->sizey() / 2,
              (int)blade->ypos() + (int)blade->sizey() / 2)
        << "the blade is centered on the knife (Y)";

    og::runtime::current_session->myscreen_->world().remove_ob(blade);
    og::runtime::current_session->myscreen_->world().remove_ob(knife);
}


TEST(WeapBehavior, weap_death_knife_non_soldier)
{
    auto owner = make_living(FAMILY_ARCHER, 0);
    ASSERT_NE(nullptr, owner.get()) << "archer owner created";

    walker* knife = make_weapon(FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife) << "knife weapon created";
    knife->set_owner(owner.get());
    knife->set_dead(1);

    const auto before = collect_entities(Order::FX, FAMILY_KNIFE_BACK);
    ASSERT_TRUE(knife->death()) << "weap::death still reports handled (the hook's false is its own)";
    const auto after = collect_entities(Order::FX, FAMILY_KNIFE_BACK);
    ASSERT_EQ(before.size(), after.size())
        << "an owner whose family lacks has_returning_weapon gets no knife_back";

    og::runtime::current_session->myscreen_->world().remove_ob(knife);
}


TEST(WeapBehavior, weap_death_fire_arrow_exploding)
{
    auto owner = make_living(FAMILY_ARCHER, 0);
    ASSERT_NE(nullptr, owner.get()) << "archer owner created";

    walker* arrow = make_weapon(FAMILY_FIRE_ARROW);
    ASSERT_NE(nullptr, arrow) << "fire arrow created";
    arrow->set_owner(owner.get());
    arrow->set_skip_exit(1); // means it's supposed to explode
    arrow->set_damage(7.0f);
    arrow->set_dead(1);

    reset_sim_log();
    const auto before = collect_entities(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(arrow->death()) << "weap::death dispatches the family hook";
    const auto after = collect_entities(Order::FX, FAMILY_EXPLOSION);
    ASSERT_EQ(before.size() + 1, after.size())
        << "an armed fire arrow spawns exactly one core:explosion";
    walker* boom = only_new_entity(before, after);
    ASSERT_NE(nullptr, boom) << "exactly one new explosion effect";

    EXPECT_EQ(owner.get(), boom->owner()) << "the explosion is credited to the archer";
    EXPECT_EQ(ANI_EXPLODE, (int)boom->ani_type()) << "the explosion plays its explode animation";
    EXPECT_FLOAT_EQ(14.0f, boom->damage()) << "explosion damage is twice the projectile's";
    EXPECT_FLOAT_EQ(0.0f, boom->stats()->hitpoints()) << "the explosion starts at hp 0";
    EXPECT_EQ((int)owner->stats()->level(), (int)boom->stats()->level())
        << "the explosion inherits the owner's level";
    EXPECT_EQ((int)arrow->floor(), (int)boom->floor()) << "the explosion stays on the arrow's floor";
    EXPECT_EQ(1, count_sounds(SOUND_EXPLODE)) << "an explosion is heard exactly once";

    og::runtime::current_session->myscreen_->world().remove_ob(boom);
    og::runtime::current_session->myscreen_->world().remove_ob(arrow);
}


TEST(WeapBehavior, weap_death_fire_arrow_no_explode)
{
    walker* arrow = make_weapon(FAMILY_FIRE_ARROW);
    ASSERT_NE(nullptr, arrow) << "fire arrow created";
    arrow->set_skip_exit(0); // not supposed to explode
    arrow->set_damage(7.0f);
    arrow->set_dead(1);

    reset_sim_log();
    const auto before = collect_entities(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(arrow->death()) << "weap::death still reports handled";
    const auto after = collect_entities(Order::FX, FAMILY_EXPLOSION);
    ASSERT_EQ(before.size(), after.size())
        << "skip_exit==0 means the arrow just dies: no explosion";
    EXPECT_EQ(0, count_sounds(SOUND_EXPLODE)) << "and nothing is heard";

    og::runtime::current_session->myscreen_->world().remove_ob(arrow);
}


TEST(WeapBehavior, weap_death_wave_transforms)
{
    walker* wave = make_weapon(FAMILY_WAVE);
    ASSERT_NE(nullptr, wave) << "wave weapon created";
    ASSERT_EQ((int)FAMILY_WAVE, (int)wave->family()) << "starts life as core:wave";
    wave->stats()->set_hitpoints(1);
    wave->set_dead(1);

    ASSERT_TRUE(wave->death()) << "weap::death dispatches the family hook";
    ASSERT_EQ(0, (int)wave->dead()) << "wave should un-dead on transform";
    ASSERT_EQ((int)FAMILY_WAVE2, (int)wave->family()) << "wave is promoted to core:wave2";
    EXPECT_FLOAT_EQ(wave->stats()->max_hitpoints(), wave->stats()->hitpoints())
        << "the new stage starts at full strength";
    EXPECT_GT(wave->stats()->hitpoints(), 1.0f) << "the hp refill actually raised hitpoints";

    og::runtime::current_session->myscreen_->world().remove_ob(wave);
}


TEST(WeapBehavior, weap_death_wave2_transforms)
{
    walker* wave = make_weapon(FAMILY_WAVE2);
    ASSERT_NE(nullptr, wave) << "wave2 weapon created";
    ASSERT_EQ((int)FAMILY_WAVE2, (int)wave->family()) << "starts life as core:wave2";
    wave->stats()->set_hitpoints(1);
    wave->set_dead(1);

    ASSERT_TRUE(wave->death()) << "weap::death dispatches the family hook";
    ASSERT_EQ(0, (int)wave->dead()) << "wave2 should un-dead on transform";
    ASSERT_EQ((int)FAMILY_WAVE3, (int)wave->family()) << "wave2 is promoted to core:wave3";
    EXPECT_FLOAT_EQ(wave->stats()->max_hitpoints(), wave->stats()->hitpoints())
        << "the last stage starts at full strength";
    EXPECT_GT(wave->stats()->hitpoints(), 1.0f) << "the hp refill actually raised hitpoints";

    og::runtime::current_session->myscreen_->world().remove_ob(wave);
}


TEST(WeapBehavior, weap_death_door)
{
    test_world().create_new_grid();

    // No wall above: the opening effect keeps its default facing and the
    // verbatim-legacy else arm writes FACE_UP onto the DOOR itself.
    walker* door = make_weapon(FAMILY_DOOR);
    ASSERT_NE(nullptr, door) << "door created";
    door->setxy(100, 100);
    door->set_curdir(static_cast<signed char>(FACE_UP_RIGHT));
    door->set_team_num(3);
    door->stats()->set_level(7);
    set_world_tile(100, 100 - GRID_SIZE, PIX_GRASS1);
    door->set_dead(1);

    auto before = collect_entities(Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_TRUE(door->death()) << "weap::death dispatches the family hook";
    auto after = collect_entities(Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_EQ(before.size() + 1, after.size()) << "a broken door spawns exactly one core:door_open";
    walker* opened = only_new_entity(before, after);
    ASSERT_NE(nullptr, opened) << "exactly one new door_open effect";

    EXPECT_EQ(ANI_DOOR_OPEN, (int)opened->ani_type()) << "the effect plays the opening animation";
    EXPECT_EQ(100, (int)opened->xpos()) << "the effect takes the door's X";
    EXPECT_EQ(100, (int)opened->ypos()) << "the effect takes the door's Y";
    EXPECT_EQ((int)door->floor(), (int)opened->floor()) << "the opened door stays on its floor";
    EXPECT_EQ(7, (int)opened->stats()->level()) << "the effect inherits the door's level";
    EXPECT_EQ(3, (int)opened->team_num()) << "the effect inherits the door's team";
    EXPECT_EQ(FACE_UP, (int)door->curdir())
        << "verbatim legacy quirk: with no wall above, FACE_UP is written onto the DOOR";
    EXPECT_EQ(FACE_DOWN, (int)opened->curdir())
        << "... and the effect keeps the spawn default facing (never FACE_RIGHT)";

    // A wall directly above picks FACE_RIGHT, and that goes on the EFFECT.
    walker* door2 = make_weapon(FAMILY_DOOR);
    ASSERT_NE(nullptr, door2) << "second door created";
    door2->setxy(100, 100);
    door2->set_curdir(static_cast<signed char>(FACE_UP_RIGHT));
    set_world_tile(100, 100 - GRID_SIZE, PIX_H_WALL1);
    door2->set_dead(1);

    before = collect_entities(Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_TRUE(door2->death()) << "weap::death dispatches the family hook";
    after = collect_entities(Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_EQ(before.size() + 1, after.size()) << "the walled door also spawns one core:door_open";
    walker* opened2 = only_new_entity(before, after);
    ASSERT_NE(nullptr, opened2) << "exactly one new door_open effect";
    EXPECT_EQ(FACE_RIGHT, (int)opened2->curdir()) << "a wall above faces the opening effect right";
    EXPECT_EQ(FACE_UP_RIGHT, (int)door2->curdir()) << "the wall arm leaves the door's own facing alone";

    og::runtime::current_session->myscreen_->world().remove_ob(opened);
    og::runtime::current_session->myscreen_->world().remove_ob(opened2);
    og::runtime::current_session->myscreen_->world().remove_ob(door);
    og::runtime::current_session->myscreen_->world().remove_ob(door2);
}


TEST(WeapBehavior, weap_death_boulder_exploding)
{
    walker* boulder = make_weapon(FAMILY_BOULDER);
    ASSERT_NE(nullptr, boulder) << "boulder created";
    boulder->set_skip_exit(1);
    boulder->set_damage(5.0f);
    boulder->set_dead(1);

    reset_sim_log();
    const auto before = collect_entities(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(boulder->death()) << "weap::death dispatches the family hook";
    const auto after = collect_entities(Order::FX, FAMILY_EXPLOSION);
    ASSERT_EQ(before.size() + 1, after.size())
        << "an armed boulder shares explode_on_death: exactly one core:explosion";
    walker* boom = only_new_entity(before, after);
    ASSERT_NE(nullptr, boom) << "exactly one new explosion effect";

    EXPECT_EQ(ANI_EXPLODE, (int)boom->ani_type()) << "the explosion plays its explode animation";
    EXPECT_FLOAT_EQ(10.0f, boom->damage()) << "explosion damage is twice the boulder's";
    EXPECT_EQ(1, count_sounds(SOUND_EXPLODE)) << "an explosion is heard exactly once";

    og::runtime::current_session->myscreen_->world().remove_ob(boom);
    og::runtime::current_session->myscreen_->world().remove_ob(boulder);
}


// ---------------------------------------------------------------------------
// weap::animate
// ---------------------------------------------------------------------------

// Merged: weap_animate_arrow ran the identical hookless default arm of
// weap::animate, so ARROW is a row in this table instead of its own case.
TEST(WeapBehavior, weap_animate_knife)
{
    for (const int family : {FAMILY_KNIFE, FAMILY_ARROW})
    {
        SCOPED_TRACE(family == FAMILY_KNIFE ? "core:knife" : "core:arrow");
        walker* w = make_weapon(static_cast<char>(family));
        ASSERT_NE(nullptr, w) << "weapon created";
        ASSERT_NE(nullptr, w->ani) << "real weapon carries an animation table";
        ASSERT_GT(w->ani_count, FACE_RIGHT) << "real weapon records its table length";

        const signed char* row = w->ani[FACE_RIGHT];
        ASSERT_NE(nullptr, row) << "facing row exists";
        ASSERT_NE(-1, (int)row[0]) << "facing row is non-empty";
        int seq_len = 0;
        while (seq_len < 128 && row[seq_len] != -1)
            ++seq_len;
        ASSERT_LT(seq_len, 128) << "facing row is sentinel-terminated";

        w->set_curdir(static_cast<signed char>(FACE_RIGHT));
        w->set_cycle(0);
        w->set_ani_type(ANI_ATTACK);

        ASSERT_TRUE(w->animate()) << "weap::animate returns 1";
        EXPECT_EQ(0, (int)w->ani_type()) << "the default arm forces ani_type back to 0";
        EXPECT_EQ((int)row[0], (int)w->frame()) << "frame comes from ani[facing][cycle]";
        EXPECT_EQ(1 % seq_len, (int)w->cycle()) << "cycle advances one step (wrapping at the sentinel)";

        // Walk the rest of the row: every step advances by one and the
        // sentinel wraps the cycle back to 0.
        for (int step = 1; step < seq_len; ++step)
        {
            ASSERT_TRUE(w->animate()) << "weap::animate returns 1 at step " << step;
            EXPECT_EQ((int)row[step], (int)w->frame()) << "frame at step " << step;
            EXPECT_EQ((step + 1) % seq_len, (int)w->cycle()) << "cycle at step " << step;
        }
        EXPECT_EQ(0, (int)w->cycle()) << "cycle wraps to 0 after the last frame of the row";

        og::runtime::current_session->myscreen_->world().remove_ob(w);
    }
}


TEST(WeapBehavior, weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_weapon(FAMILY_KNIFE);
    auto dead_living = make_living(FAMILY_SOLDIER, 1);
    ASSERT_TRUE(w && dead_living) << "weapon and dead living created";
    if (!(w && dead_living))
        return;

    dead_living->set_dead(1);
    w->set_foe(dead_living.get());
    w->set_leader(dead_living.get());
    w->set_owner(dead_living.get());
    w->setxy(0, 0);
    w->set_lineofsight(5);
    og::runtime::current_session->myscreen_->world().grid.data[0] = PIX_TREE_M1;
    w->set_act_type(ACT_RANDOM);

    (void)w->act();
    ASSERT_TRUE(w->foe() == nullptr && w->leader() == nullptr) << "dead foe/leader should be cleared";
    ASSERT_TRUE(w->owner() == w) << "dead owner should be cleared then default to self";
    ASSERT_EQ(4, (int)w->lineofsight()) << "trees tile should decrement lineofsight";

    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_act_control_generate_guard_and_default_paths)
{
    walker* control = make_weapon(FAMILY_KNIFE);
    walker* gen = make_weapon(FAMILY_KNIFE);
    walker* guard = make_weapon(FAMILY_KNIFE);
    walker* unknown = make_weapon(FAMILY_KNIFE);
    ASSERT_TRUE(control && gen && guard && unknown) << "weapons created";
    if (!(control && gen && guard && unknown))
        return;

    control->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(control->act()) << "ACT_CONTROL should return true";

    gen->set_act_type(ACT_GENERATE);
    ASSERT_TRUE(!gen->act()) << "ACT_GENERATE path should fall through to return false";

    guard->set_act_type(ACT_GUARD);
    ASSERT_TRUE(!guard->act()) << "ACT_GUARD path should fall through to return false";

    unknown->set_act_type(123);
    ASSERT_TRUE(!unknown->act()) << "unknown act should return false";

    og::runtime::current_session->myscreen_->world().remove_ob(control);
    og::runtime::current_session->myscreen_->world().remove_ob(gen);
    og::runtime::current_session->myscreen_->world().remove_ob(guard);
    og::runtime::current_session->myscreen_->world().remove_ob(unknown);
}


TEST(WeapBehavior, weap_death_is_idempotent)
{
    walker* w = make_weapon(FAMILY_KNIFE);
    ASSERT_TRUE(w != nullptr) << "weapon created";
    if (!w)
        return;
    w->set_dead(1);
    ASSERT_TRUE(w->death()) << "first death() call should succeed";
    ASSERT_TRUE(!w->death()) << "second death() call should short-circuit";
    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


TEST(WeapBehavior, weap_headless_default_ctor_and_setxy_path)
{
    weap headless;
    ASSERT_EQ(0, (int)headless.do_bounce()) << "default weap ctor should initialize do_bounce=0";
    ASSERT_EQ((int)Order::Weapon, (int)headless.query_order()) << "headless weap should report weapon order";

    headless.setxy(12, 34);
    ASSERT_EQ(12, (int)headless.xpos()) << "weap::setxy override should update xpos";
    ASSERT_EQ(34, (int)headless.ypos()) << "weap::setxy override should update ypos";
}


TEST(WeapBehavior, weapon_family_rock_death_bounce_matrix)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* rock_w = make_weapon(FAMILY_ROCK);
    ASSERT_TRUE(rock_w != nullptr) << "rock weapon created";
    if (!rock_w)
        return;
    auto* rock = static_cast<weap*>(rock_w);

    const WeaponFamilyDescriptor* rock_desc = get_weapon_family_descriptor(FAMILY_ROCK);
    ASSERT_TRUE(rock_desc != nullptr && og::test::has_on_death(*rock_desc)) << "rock descriptor callback exists";
    if (!(rock_desc && og::test::has_on_death(*rock_desc)))
    {
        og::runtime::current_session->myscreen_->world().remove_ob(rock_w);
        return;
    }

    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_collide_ob(nullptr);
    rock->set_lineofsight(1);

    // Guard: do_bounce disabled.
    rock->set_dead(1);
    rock->set_do_bounce(0);
    ASSERT_TRUE(!og::test::on_death(*rock_desc, rock)) << "rock on_death should short-circuit when do_bounce=0";
    // Folded in from weap_death_rock_no_bounce: the short-circuit leaves the
    // rock dead (no un-dead, no bounce).
    ASSERT_EQ(1, (int)rock->dead()) << "do_bounce=0 short-circuit leaves the rock dead";

    // First probe passable => no bounce, die normally.
    rock->set_do_bounce(1);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_GRASS1);
    ASSERT_TRUE(!og::test::on_death(*rock_desc, rock)) << "rock on_death should return false when forward tile is passable";
    ASSERT_EQ(1, (int)rock->dead()) << "forward-passable path should leave rock dead";

    // First blocked, second passable => bounce down-left (flip X only).
    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_GRASS1);
    ASSERT_TRUE(og::test::on_death(*rock_desc, rock)) << "rock on_death should bounce down-left";
    ASSERT_EQ(-GRID_SIZE, (int)rock->lastx()) << "down-left bounce should invert X velocity";
    ASSERT_EQ(GRID_SIZE, (int)rock->lasty()) << "down-left bounce should preserve Y velocity";

    // First+second blocked, third passable => bounce up-right (flip Y only).
    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 + GRID_SIZE, 64 - GRID_SIZE, PIX_GRASS1);
    ASSERT_TRUE(og::test::on_death(*rock_desc, rock)) << "rock on_death should bounce up-right";
    ASSERT_EQ(GRID_SIZE, (int)rock->lastx()) << "up-right bounce should preserve X velocity";
    ASSERT_EQ(-GRID_SIZE, (int)rock->lasty()) << "up-right bounce should invert Y velocity";

    // First+second+third blocked, fourth passable => bounce up-left (flip both).
    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 + GRID_SIZE, 64 - GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 - GRID_SIZE, PIX_GRASS1);
    ASSERT_TRUE(og::test::on_death(*rock_desc, rock)) << "rock on_death should bounce up-left";
    ASSERT_EQ(-GRID_SIZE, (int)rock->lastx()) << "up-left bounce should invert X velocity";
    ASSERT_EQ(-GRID_SIZE, (int)rock->lasty()) << "up-left bounce should invert Y velocity";

    // All blocked => remain dead.
    rock->setxy(64, 64);
    rock->set_lastx(GRID_SIZE);
    rock->set_lasty(GRID_SIZE);
    rock->set_dead(1);
    set_world_tile(64 + GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 + GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 + GRID_SIZE, 64 - GRID_SIZE, PIX_H_WALL1);
    set_world_tile(64 - GRID_SIZE, 64 - GRID_SIZE, PIX_H_WALL1);
    ASSERT_TRUE(!og::test::on_death(*rock_desc, rock)) << "rock on_death should fail when all bounce probes are blocked";
    ASSERT_EQ(1, (int)rock->dead()) << "all-blocked path should leave rock dead";

    og::runtime::current_session->myscreen_->world().remove_ob(rock_w);
}


TEST(WeapBehavior, weapon_animate_handles_out_of_range_facing_and_cycle)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();

    // weap::animate() and the weapon-family on_animate callbacks index the
    // animation table with curdir/cycle, which can arrive out of range from a
    // snapshot. These must be bounded (facing clamp + sequence sentinel) rather
    // than reading out of bounds. ARROW exercises the default branch; TREE and
    // GLOW exercise the on_animate callbacks.
    walker* arrow_w = make_weapon(FAMILY_ARROW);
    walker* tree_w = make_weapon(FAMILY_TREE);
    walker* glow_w = make_weapon(FAMILY_GLOW);
    ASSERT_TRUE(arrow_w && tree_w && glow_w);
    if (!(arrow_w && tree_w && glow_w))
        return;

    for (walker* w : {arrow_w, tree_w, glow_w})
    {
        ASSERT_GT(w->ani_count, 0) << "real weapon records its table length";
        w->set_curdir(static_cast<char>(100));
        w->set_cycle(static_cast<signed char>(120));
        w->set_ani_type(static_cast<char>(40));
        (void)w->animate(); // must not crash / read OOB (verified under sanitizers)
        w->set_curdir(static_cast<char>(-7));
        w->set_cycle(static_cast<signed char>(-3));
        (void)w->animate();
    }

    og::runtime::current_session->myscreen_->world().remove_ob(arrow_w);
    og::runtime::current_session->myscreen_->world().remove_ob(tree_w);
    og::runtime::current_session->myscreen_->world().remove_ob(glow_w);
}


TEST(WeapBehavior, weapon_family_animate_callbacks_and_sprinkle_hit_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();

    walker* tree_w = make_weapon(FAMILY_TREE);
    walker* glow_w = make_weapon(FAMILY_GLOW);
    walker* circle_w = make_weapon(FAMILY_CIRCLE_PROTECTION);
    auto owner = make_living(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(tree_w && glow_w && circle_w && owner) << "tree/glow/circle and owner created";
    if (!(tree_w && glow_w && circle_w && owner))
        return;

    // TREE/BLOOD callback: ani_type clamp + sentinel reset.
    tree_w->set_curdir(0);
    tree_w->set_ani_type(5);
    tree_w->set_cycle(0);
    // Create local mutable animation data (global tables are const)
    static signed char tree_test_seq[] = {10, -1};
    static const signed char * tree_test_rows[] = {tree_test_seq, tree_test_seq, tree_test_seq, tree_test_seq,
                                                    tree_test_seq, tree_test_seq, tree_test_seq, tree_test_seq,
                                                    tree_test_seq, tree_test_seq, tree_test_seq, tree_test_seq,
                                                    tree_test_seq, tree_test_seq, tree_test_seq, tree_test_seq};
    tree_w->ani = tree_test_rows;
    ASSERT_TRUE(tree_w->animate()) << "tree animate should succeed";
    ASSERT_EQ(0, (int)tree_w->ani_type()) << "tree animate should clamp ani_type >1 to 0";
    ASSERT_EQ(0, (int)tree_w->cycle()) << "tree animate should reset cycle at -1 sentinel";

    // CIRCLE_PROTECTION callback: no owner/invalid owner path.
    circle_w->set_owner(nullptr);
    circle_w->set_dead(0);
    ASSERT_TRUE(circle_w->animate()) << "circle animate should still return via death handling";
    ASSERT_EQ(1, (int)circle_w->dead()) << "circle animate should mark dead when owner is missing";

    // CIRCLE_PROTECTION callback: valid owner centers on owner.
    circle_w->set_dead(0);
    circle_w->set_death_called(0);
    circle_w->set_owner(owner.get());
    owner->set_dead(0);
    circle_w->stats()->set_hitpoints(5);
    owner->setxy(180, 188);
    ASSERT_TRUE(circle_w->animate()) << "circle animate should succeed with valid owner";
    ASSERT_TRUE(std::abs((int)circle_w->xpos() - (int)owner->xpos()) <= GRID_SIZE) << "circle animate should center near owner on X";
    ASSERT_TRUE(std::abs((int)circle_w->ypos() - (int)owner->ypos()) <= GRID_SIZE) << "circle animate should center near owner on Y";

    // GLOW callback: illegal ani_type clamp + sentinel reset + lifetime death.
    glow_w->set_curdir(0);
    glow_w->set_ani_type(9);
    glow_w->set_cycle(0);
    glow_w->set_lifetime(0);
    glow_w->set_dead(0);
    glow_w->set_death_called(0);
    // Create local mutable animation data for glow (global tables are const)
    static signed char glow_test_seq[] = {12, -1};
    static const signed char * glow_test_rows[32] = {};
    // Fill all rows with the test sequence
    for (int i = 0; i < 32; i++)
        glow_test_rows[i] = glow_test_seq;
    glow_w->ani = glow_test_rows;
    ASSERT_TRUE(glow_w->animate()) << "glow animate should succeed";
    ASSERT_EQ(2, (int)glow_w->ani_type()) << "glow animate should clamp ani_type >2 to pulse state";
    ASSERT_EQ(0, (int)glow_w->cycle()) << "glow animate should reset cycle at sentinel";
    ASSERT_EQ(1, (int)glow_w->dead()) << "glow animate should mark dead when lifetime expires";

    // SPRINKLE callback: non-living target no-op; living target with null myguy uses con=0.
    const WeaponFamilyDescriptor* sprinkle_desc = get_weapon_family_descriptor(FAMILY_SPRINKLE);
    ASSERT_TRUE(sprinkle_desc != nullptr && og::test::has_on_hit_target(*sprinkle_desc)) << "sprinkle descriptor callback exists";
    walker* non_living_target = og::runtime::current_session->myscreen_->world().add_ob(Order::Treasure, FAMILY_STAIN);
    walker* living_target = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SLIME);
    ASSERT_TRUE(non_living_target != nullptr && living_target != nullptr) << "sprinkle targets created";
    if (sprinkle_desc && og::test::has_on_hit_target(*sprinkle_desc) && non_living_target && living_target)
    {
        short before_non_living = non_living_target->stats()->frozen_delay();
        ASSERT_TRUE(og::test::on_hit_target(*sprinkle_desc, tree_w, non_living_target, owner.get())) << "sprinkle hit callback should return true for non-living targets";
        ASSERT_EQ((int)before_non_living, (int)non_living_target->stats()->frozen_delay()) << "sprinkle should not change frozen_delay for non-living targets";

        living_target->myguy = nullptr;
        living_target->stats()->set_frozen_delay(0);
        owner->stats()->set_level(6);
        // og.freeze_duration draws from the world rng: state 1 -> first draw
        // (1*1103515245+12345)>>16 == 16838, and 16838 % (40 + 2*6 - 0) == 42,
        // which is below og::combat::kSprinkleRollKnee (79) so soften() is the
        // identity. Owner level 6 < 21, so the refresh gate is open.
        og::runtime::current_session->myscreen_->world().rng_.state_ = 1;
        ASSERT_TRUE(og::test::on_hit_target(*sprinkle_desc, tree_w, living_target, owner.get())) << "sprinkle hit callback should return true for living targets";
        ASSERT_EQ(42, (int)living_target->stats()->frozen_delay())
            << "sprinkle freezes a living target for freeze_duration(owner level 6, con 0) = rng(52) = 42";
    }

    og::runtime::current_session->myscreen_->world().remove_ob(non_living_target);
    og::runtime::current_session->myscreen_->world().remove_ob(living_target);
    og::runtime::current_session->myscreen_->world().remove_ob(tree_w);
    og::runtime::current_session->myscreen_->world().remove_ob(glow_w);
    og::runtime::current_session->myscreen_->world().remove_ob(circle_w);
}


TEST(WeapBehavior, weap_act_sit_with_non_skipping_family_and_act_animate_shortcut)
{
    walker* sit_weapon = make_weapon(FAMILY_KNIFE);
    walker* anim_weapon = make_weapon(FAMILY_ARROW);
    ASSERT_TRUE(sit_weapon && anim_weapon) << "weapons created";
    if (!(sit_weapon && anim_weapon))
        return;

    const WeaponFamilyDescriptor* knife_fd = get_weapon_family_descriptor(FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife_fd) << "knife weapon family descriptor exists";
    ASSERT_FALSE(knife_fd->skip_sit_notify) << "core:knife declares skip_sit_notify=false";

    sit_weapon->set_team_num(2);
    sit_weapon->set_ani_type(ANI_WALK); // otherwise act() short-circuits into animate()
    sit_weapon->set_act_type(ACT_SIT);
    reset_sim_log();
    ASSERT_TRUE(sit_weapon->act()) << "non-skip sit family should still return true";
    ASSERT_EQ(1u, sim_log().size()) << "a non-skipping family announces its sit exactly once";
    const og::sim::Event& sit_ev = sim_log().events()[0];
    EXPECT_EQ(og::sim::EventKind::Notification, sit_ev.kind) << "the sit report is a Notification";
    EXPECT_EQ("Weapon sitting", sit_ev.text) << "the sit report text";
    EXPECT_EQ((std::uint32_t)FAMILY_KNIFE, sit_ev.a) << "payload a is the weapon family";
    EXPECT_EQ((std::uint32_t)2, sit_ev.b) << "payload b is the weapon's team";

    // Cover act() early return path when previous animation is still active.
    anim_weapon->set_ani_type(ANI_ATTACK);
    anim_weapon->set_cycle(0);
    anim_weapon->set_curdir(0);
    ASSERT_TRUE(anim_weapon->act()) << "non-walk ani_type should route through animate() and return true";
    EXPECT_EQ(0, (int)anim_weapon->ani_type())
        << "the animate() shortcut ran weap::animate's default arm (ani_type forced to 0)";

    og::runtime::current_session->myscreen_->world().remove_ob(sit_weapon);
    og::runtime::current_session->myscreen_->world().remove_ob(anim_weapon);
}
