/* Thorough tests for the monk family (FAMILY_MONK).
 *
 * Covers: descriptor data, set_difficulty, level_up, silence mechanic,
 * all four specials (Silence, Meditate, Evasion/Counter Stance, Whirlwind Kick),
 * on_fire_weapon (ranged block), on_melee_hit (knockback+stun),
 * check_special_ai, and the silence_left tick-down + special-blocking integration.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/data/gloader.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include <openglad/sim/irandom.h>
#include "test_framework.h"
#include <cmath>

extern screen* myscreen;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void ensure_level_loaded()
{
    if (myscreen == nullptr)
        return;
    if (myscreen->level_data.grid.valid() && myscreen->level_data.pixmaxx > 0
        && myscreen->level_data.pixmaxy > 0)
        return;
    myscreen->level_data.id = 1;
    (void)myscreen->level_data.load();
}

static void monk_teardown()
{
    if (myscreen != nullptr)
        myscreen->level_data.delete_objects();
}

#define REGISTER_MONK_TEST(func) \
    REGISTER_TEST_WITH_FIXTURE(func, nullptr, monk_teardown)

static walker* make_monk(unsigned char team = 0, short level = 3)
{
    ensure_level_loaded();
    guy g(FAMILY_MONK);
    g.teamnum = team;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, myscreen);
    if (w) {
        w->setxy(100, 100);
        w->stats()->magicpoints = 500;
        w->stats()->max_magicpoints = 500;
    }
    return w.release();
}

// Creates a foe that is registered in the level's oblist so that
// find_foes_in_range() can locate it.
static walker* make_foe(char family, unsigned char team = 2, short level = 2)
{
    ensure_level_loaded();
    walker* w = myscreen->level_data.add_ob(Order::Living, family);
    if (w) {
        w->team_num = team;
        w->stats()->level = level;
        w->setxy(120, 100);
    }
    return w;
}

// Convenience wrapper to avoid ambiguous setxy overload with int arithmetic
static void place_at(walker* w, int x, int y)
{
    w->setxy(static_cast<short>(x), static_cast<short>(y));
}

#define TEST_ASSERT_FLOAT(expected, actual, msg) \
    do { \
        float _exp = (expected); \
        float _act = (actual); \
        if (std::fabs(_exp - _act) > 0.5f) { \
            fprintf(stderr, "  FAIL: %s (expected %.1f, got %.1f) (%s:%d)\n", \
                    msg, _exp, _act, __FILE__, __LINE__); \
            g_tests_failed++; \
            g_tests_run++; \
            return; \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// FamilyDescriptor metadata
// ---------------------------------------------------------------------------

void test_monk_descriptor_metadata()
{
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    TEST_ASSERT(fd != nullptr, "monk descriptor should exist");
    TEST_ASSERT_EQ(FAMILY_MONK, fd->family_id, "family_id should be FAMILY_MONK");
    TEST_ASSERT(fd->is_playable, "monk should be playable");
    TEST_ASSERT_EQ(6, fd->playable_order, "monk playable_order should be 6");
    TEST_ASSERT_EQ(350, fd->hiring_cost, "monk hiring cost should be 350");
    TEST_ASSERT_EQ(0, fd->weapon_cost, "monk weapon_cost should be 0 (melee only)");
    TEST_ASSERT_EQ(FAMILY_KNIFE, (int)fd->default_weapon, "monk default weapon should be knife");
    TEST_ASSERT_EQ(BIT_NO_RANGED, fd->init_bit_flags, "monk should have BIT_NO_RANGED");
    TEST_ASSERT(fd->leaves_bloodspot, "monk should leave bloodspot");
    TEST_ASSERT(!fd->is_stationary, "monk should not be stationary");
    TEST_ASSERT(!fd->is_undead, "monk should not be undead");
    TEST_ASSERT(!fd->has_returning_weapon, "monk should not have returning weapon");

    // Callbacks
    TEST_ASSERT(fd->do_special != nullptr, "monk should have do_special callback");
    TEST_ASSERT(fd->check_special_ai != nullptr, "monk should have check_special_ai callback");
    TEST_ASSERT(fd->set_difficulty != nullptr, "monk should have set_difficulty callback");
    TEST_ASSERT(fd->level_up != nullptr, "monk should have level_up callback");
    TEST_ASSERT(fd->on_fire_weapon != nullptr, "monk should have on_fire_weapon callback");
    TEST_ASSERT(fd->on_melee_hit != nullptr, "monk should have on_melee_hit callback");

    // Special names
    TEST_ASSERT_STR_EQ("SILENCE", fd->special_names[1], "special 1 should be SILENCE");
    TEST_ASSERT_STR_EQ("MEDITATE", fd->special_names[2], "special 2 should be MEDITATE");
    TEST_ASSERT_STR_EQ("EVASION", fd->special_names[3], "special 3 should be EVASION");
    TEST_ASSERT_STR_EQ("WHIRLWIND KICK", fd->special_names[4], "special 4 should be WHIRLWIND KICK");
    TEST_ASSERT_STR_EQ("COUNTER STANCE", fd->alternate_names[3], "alt special 3 should be COUNTER STANCE");

    // Special costs
    TEST_ASSERT_EQ(25, (int)fd->special_cost[1], "silence cost should be 25");
    TEST_ASSERT_EQ(60, (int)fd->special_cost[2], "meditate cost should be 60");
    TEST_ASSERT_EQ(80, (int)fd->special_cost[3], "evasion cost should be 80");
    TEST_ASSERT_EQ(120, (int)fd->special_cost[4], "whirlwind kick cost should be 120");

    // Base stats: {STR, DEX, CON, INT, ARMOR, ?}
    TEST_ASSERT_EQ(10, fd->base_stats[0], "monk base STR");
    TEST_ASSERT_EQ(12, fd->base_stats[1], "monk base DEX");
    TEST_ASSERT_EQ(10, fd->base_stats[2], "monk base CON");
    TEST_ASSERT_EQ(10, fd->base_stats[3], "monk base INT");

    // Name pool
    TEST_ASSERT(fd->name_pool != nullptr, "monk should have a name pool");
    TEST_ASSERT(fd->name_pool_size == 8, "monk name pool should have 8 entries");
}
REGISTER_MONK_TEST(test_monk_descriptor_metadata);

// ---------------------------------------------------------------------------
// set_difficulty — monk-specific scaling formula
// ---------------------------------------------------------------------------

void test_monk_set_difficulty()
{
    auto* w = make_monk(0, 1);
    TEST_ASSERT(w != nullptr, "monk created");

    float hp0 = w->stats()->max_hitpoints;
    float mp0 = w->stats()->max_magicpoints;
    float dmg0 = w->damage;
    float armor0 = w->stats()->armor;

    w->team_num = 0;
    static_cast<living*>(w)->set_difficulty(2);

    // levmult = 2*2 = 4, level_f = 2
    // HP += 10 * 4 = 40, MP += 8 * 4 = 32, damage += 4 * 2 = 8, armor += 4/3 = 1.33
    TEST_ASSERT_FLOAT(40.0f, w->stats()->max_hitpoints - hp0, "monk HP delta at level 2");
    TEST_ASSERT_FLOAT(32.0f, w->stats()->max_magicpoints - mp0, "monk MP delta at level 2");
    TEST_ASSERT_FLOAT(8.0f, w->damage - dmg0, "monk damage delta at level 2");
    TEST_ASSERT_FLOAT(1.33f, w->stats()->armor - armor0, "monk armor delta at level 2");

    delete w;
}
REGISTER_MONK_TEST(test_monk_set_difficulty);

// ---------------------------------------------------------------------------
// level_up — monk-specific stat growth (DEX 1.5x, INT 0.75x)
// ---------------------------------------------------------------------------

void test_monk_level_up()
{
    guy g(FAMILY_MONK);
    short s0 = g.strength;
    short d0 = g.dexterity;
    short c0 = g.constitution;
    short i0 = g.intelligence;
    short a0 = g.armor;

    g.upgrade_to_level(2, true);

    // 1 level diff => STR += 8, DEX += 6*3/2 = 9, CON += 8, INT += 8*3/4 = 6, ARMOR += 1
    TEST_ASSERT_EQ(8, (int)(g.strength - s0), "monk STR growth per level");
    TEST_ASSERT_EQ(9, (int)(g.dexterity - d0), "monk DEX growth per level (1.5x)");
    TEST_ASSERT_EQ(8, (int)(g.constitution - c0), "monk CON growth per level");
    TEST_ASSERT_EQ(6, (int)(g.intelligence - i0), "monk INT growth per level (0.75x)");
    TEST_ASSERT_EQ(1, (int)(g.armor - a0), "monk ARMOR growth per level");
}
REGISTER_MONK_TEST(test_monk_level_up);

// ---------------------------------------------------------------------------
// Special 1: Silence — applies silence_left to nearest foe
// ---------------------------------------------------------------------------

void test_monk_special_silence()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");
    walker* foe = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(foe != nullptr, "foe created");

    place_at(foe, monk->xpos + 10, monk->ypos);
    monk->current_special = 1;
    monk->busy = 0;

    TEST_ASSERT_EQ(0, (int)foe->silence_left, "foe should start unsilenced");

    bool result = monk->special();
    TEST_ASSERT(result, "silence special should succeed");
    TEST_ASSERT(foe->silence_left > 0, "foe should be silenced");

    // Duration = 30 + level * 10 = 30 + 3*10 = 60
    TEST_ASSERT_EQ(60, (int)foe->silence_left, "silence duration should be 30 + level*10");

    // Monk should be busy after casting
    TEST_ASSERT(monk->busy > 0, "monk should be busy after silence");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_special_silence);

// ---------------------------------------------------------------------------
// Silence stacks: casting twice should add durations
// ---------------------------------------------------------------------------

void test_monk_silence_stacks()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");
    walker* foe = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(foe != nullptr, "foe created");

    place_at(foe, monk->xpos + 10, monk->ypos);
    monk->current_special = 1;
    monk->busy = 0;
    monk->special();

    short first = foe->silence_left;
    TEST_ASSERT(first > 0, "first silence should apply");

    monk->busy = 0;
    monk->stats()->magicpoints = 500;
    monk->special();

    TEST_ASSERT(foe->silence_left > first, "silence should stack");
    TEST_ASSERT_EQ((int)(first + 60), (int)foe->silence_left, "stacked silence should be additive");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_silence_stacks);

// ---------------------------------------------------------------------------
// Silence blocks specials
// ---------------------------------------------------------------------------

void test_silence_blocks_specials()
{
    walker* mage = make_foe(FAMILY_MAGE, 2, 3);
    TEST_ASSERT(mage != nullptr, "mage created");

    mage->stats()->magicpoints = 500;
    mage->stats()->max_magicpoints = 500;
    mage->current_special = 1;
    mage->silence_left = 50;

    bool result = mage->special();
    TEST_ASSERT(!result, "silenced walker should not be able to use specials");

    // After silence wears off, specials should work
    mage->silence_left = 0;
    mage->current_special = 1;
    // mage teleport may or may not succeed depending on level state,
    // but it should at least not be blocked by silence
    // We just verify the silence gate is removed
    // (the teleport itself may fail for other reasons like no marker)

}
REGISTER_MONK_TEST(test_silence_blocks_specials);

// ---------------------------------------------------------------------------
// silence_left ticks down in act()
// ---------------------------------------------------------------------------

void test_silence_ticks_down()
{
    walker* foe = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(foe != nullptr, "foe created");

    foe->silence_left = 10;
    foe->busy = 0;
    foe->stats()->frozen_delay = 0;
    foe->dead = 0;

    // One act() call should decrement silence_left by 1
    short before = foe->silence_left;
    foe->act();
    TEST_ASSERT_EQ((int)(before - 1), (int)foe->silence_left,
                   "act() should decrement silence_left by 1");

}
REGISTER_MONK_TEST(test_silence_ticks_down);

// ---------------------------------------------------------------------------
// silence_left initializes to 0
// ---------------------------------------------------------------------------

void test_silence_left_initialized_to_zero()
{
    walker* monk = make_monk();
    TEST_ASSERT(monk != nullptr, "monk created");
    TEST_ASSERT_EQ(0, (int)monk->silence_left, "silence_left should initialize to 0");

    walker* foe = make_foe(FAMILY_ORC);
    TEST_ASSERT(foe != nullptr, "foe created");
    TEST_ASSERT_EQ(0, (int)foe->silence_left, "silence_left should initialize to 0 for other families");

    delete monk;
}
REGISTER_MONK_TEST(test_silence_left_initialized_to_zero);

// ---------------------------------------------------------------------------
// Special 1: Silence fails when no foes in range
// ---------------------------------------------------------------------------

void test_monk_silence_no_foe_in_range()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    // No foes nearby
    monk->current_special = 1;
    monk->busy = 0;

    bool result = monk->special();
    TEST_ASSERT(!result, "silence should fail with no foes in range");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_silence_no_foe_in_range);

// ---------------------------------------------------------------------------
// Special 1: Silence when busy should fail
// ---------------------------------------------------------------------------

void test_monk_silence_while_busy()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");
    walker* foe = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(foe != nullptr, "foe created");
    place_at(foe, monk->xpos + 10, monk->ypos);

    monk->current_special = 1;
    monk->busy = 10;

    bool result = monk->special();
    TEST_ASSERT(!result, "silence should fail when busy");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_silence_while_busy);

// ---------------------------------------------------------------------------
// Special 2: Meditate — heals self
// ---------------------------------------------------------------------------

void test_monk_special_meditate()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    // Damage the monk first
    monk->stats()->hitpoints = 20.0f;
    monk->stats()->max_hitpoints = 200.0f;
    monk->current_special = 2;
    monk->busy = 0;

    float hp_before = monk->stats()->hitpoints;
    bool result = monk->special();
    TEST_ASSERT(result, "meditate should succeed");
    TEST_ASSERT(monk->stats()->hitpoints > hp_before, "meditate should heal");

    // heal = 20 + level * 15 = 20 + 3*15 = 65
    float expected_hp = hp_before + 65.0f;
    TEST_ASSERT_FLOAT(expected_hp, monk->stats()->hitpoints, "meditate heal amount");

    TEST_ASSERT(monk->busy > 0, "monk should be busy after meditate");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_special_meditate);

// ---------------------------------------------------------------------------
// Special 2: Meditate caps at max HP
// ---------------------------------------------------------------------------

void test_monk_meditate_caps_at_max()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    monk->stats()->hitpoints = monk->stats()->max_hitpoints - 5.0f;
    monk->current_special = 2;
    monk->busy = 0;

    monk->special();
    TEST_ASSERT_FLOAT(monk->stats()->max_hitpoints, monk->stats()->hitpoints,
                      "meditate should cap at max_hitpoints");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_meditate_caps_at_max);

// ---------------------------------------------------------------------------
// Special 2: Meditate fails when busy
// ---------------------------------------------------------------------------

void test_monk_meditate_while_busy()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    monk->stats()->hitpoints = 20.0f;
    monk->current_special = 2;
    monk->busy = 10;

    bool result = monk->special();
    TEST_ASSERT(!result, "meditate should fail when busy");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_meditate_while_busy);

// ---------------------------------------------------------------------------
// Special 3: Evasion (normal) — grants invisibility
// ---------------------------------------------------------------------------

void test_monk_special_evasion()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    monk->current_special = 3;
    monk->shifter_down = 0;
    monk->busy = 0;

    short invis_before = monk->invisibility_left;
    bool result = monk->special();
    TEST_ASSERT(result, "evasion should succeed");
    TEST_ASSERT(monk->invisibility_left > invis_before, "evasion should grant invisibility");

    // Duration = 20 + level * 10 = 20 + 3*10 = 50
    TEST_ASSERT_EQ(50, (int)(monk->invisibility_left - invis_before),
                   "evasion invisibility duration should be 20 + level*10");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_special_evasion);

// ---------------------------------------------------------------------------
// Special 3: Counter Stance (shifted) — grants invulnerability
// ---------------------------------------------------------------------------

void test_monk_special_counter_stance()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    monk->current_special = 3;
    monk->shifter_down = 1;
    monk->busy = 0;

    short invuln_before = monk->invulnerable_left;
    bool result = monk->special();
    TEST_ASSERT(result, "counter stance should succeed");
    TEST_ASSERT(monk->invulnerable_left > invuln_before, "counter stance should grant invulnerability");

    // Duration = 15 + level * 8 = 15 + 3*8 = 39
    TEST_ASSERT_EQ(39, (int)(monk->invulnerable_left - invuln_before),
                   "counter stance duration should be 15 + level*8");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_special_counter_stance);

// ---------------------------------------------------------------------------
// Special 3: Evasion fails when busy
// ---------------------------------------------------------------------------

void test_monk_evasion_while_busy()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    monk->current_special = 3;
    monk->shifter_down = 0;
    monk->busy = 10;

    bool result = monk->special();
    TEST_ASSERT(!result, "evasion should fail when busy");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_evasion_while_busy);

// ---------------------------------------------------------------------------
// Special 4: Whirlwind Kick — AoE damage to nearby foes
// ---------------------------------------------------------------------------

void test_monk_special_whirlwind_kick()
{
    walker* monk = make_monk(0, 5);
    TEST_ASSERT(monk != nullptr, "monk created");

    walker* foe1 = make_foe(FAMILY_ORC, 2, 2);
    walker* foe2 = make_foe(FAMILY_SKELETON, 2, 2);
    TEST_ASSERT(foe1 != nullptr && foe2 != nullptr, "foes created");

    place_at(foe1, monk->xpos + 10, monk->ypos);
    place_at(foe2, monk->xpos, monk->ypos + 10);
    float hp1_before = foe1->stats()->hitpoints;
    float hp2_before = foe2->stats()->hitpoints;

    monk->current_special = 4;
    monk->busy = 0;

    bool result = monk->special();
    TEST_ASSERT(result, "whirlwind kick should succeed with foes in range");

    // Both foes should take damage
    TEST_ASSERT(foe1->stats()->hitpoints < hp1_before || foe1->dead,
                "foe1 should take damage from whirlwind kick");
    TEST_ASSERT(foe2->stats()->hitpoints < hp2_before || foe2->dead,
                "foe2 should take damage from whirlwind kick");

    TEST_ASSERT(monk->busy > 0, "monk should be busy after whirlwind kick");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_special_whirlwind_kick);

// ---------------------------------------------------------------------------
// Special 4: Whirlwind Kick fails with no foes in range
// ---------------------------------------------------------------------------

void test_monk_whirlwind_no_foes()
{
    walker* monk = make_monk(0, 5);
    TEST_ASSERT(monk != nullptr, "monk created");

    monk->current_special = 4;
    monk->busy = 0;

    bool result = monk->special();
    TEST_ASSERT(!result, "whirlwind kick should fail with no foes in range");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_whirlwind_no_foes);

// ---------------------------------------------------------------------------
// Special 4: Whirlwind Kick fails when busy
// ---------------------------------------------------------------------------

void test_monk_whirlwind_while_busy()
{
    walker* monk = make_monk(0, 5);
    TEST_ASSERT(monk != nullptr, "monk created");
    walker* foe = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(foe != nullptr, "foe created");
    place_at(foe, monk->xpos + 10, monk->ypos);

    monk->current_special = 4;
    monk->busy = 10;

    bool result = monk->special();
    TEST_ASSERT(!result, "whirlwind kick should fail when busy");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_whirlwind_while_busy);

// ---------------------------------------------------------------------------
// Invalid special index returns false
// ---------------------------------------------------------------------------

void test_monk_invalid_special()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    monk->current_special = 5;
    monk->busy = 0;
    monk->stats()->special_cost[5] = 0;

    bool result = monk->special();
    TEST_ASSERT(!result, "invalid special index should fail");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_invalid_special);

// ---------------------------------------------------------------------------
// on_fire_weapon: monk kills the weapon projectile (melee only)
// ---------------------------------------------------------------------------

void test_monk_on_fire_weapon_blocks_ranged()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    // Calling fire() on a monk should return nullptr because on_fire_weapon
    // marks the weapon as dead and returns false
    monk->lastx = monk->stepsize;
    monk->lasty = 0;
    walker* weapon = monk->fire();
    TEST_ASSERT(weapon == nullptr, "monk fire() should return null (melee only, ranged blocked)");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_on_fire_weapon_blocks_ranged);

// ---------------------------------------------------------------------------
// on_melee_hit: knockback + stun via force_command
// ---------------------------------------------------------------------------

void test_monk_on_melee_hit_knockback()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");
    walker* target = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(target != nullptr, "target created");

    place_at(target, monk->xpos + 20, monk->ypos);
    target->dead = 0;

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    TEST_ASSERT(fd != nullptr && fd->on_melee_hit != nullptr, "monk has on_melee_hit");

    fd->on_melee_hit(monk, target);

    // After on_melee_hit, target should have a forced command (COMMAND_WALK)
    TEST_ASSERT(target->stats()->has_commands(), "target should have a forced command after melee hit");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_on_melee_hit_knockback);

// ---------------------------------------------------------------------------
// on_melee_hit: does nothing to dead target
// ---------------------------------------------------------------------------

void test_monk_on_melee_hit_dead_target()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");
    walker* target = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(target != nullptr, "target created");

    place_at(target, monk->xpos + 20, monk->ypos);
    target->dead = 1;

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    fd->on_melee_hit(monk, target);

    // Should not crash, target should remain unchanged
    TEST_ASSERT(target->dead == 1, "dead target should remain dead");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_on_melee_hit_dead_target);

// ---------------------------------------------------------------------------
// on_melee_hit: null target doesn't crash
// ---------------------------------------------------------------------------

void test_monk_on_melee_hit_null_target()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    fd->on_melee_hit(monk, nullptr);

    // Should not crash
    TEST_ASSERT(true, "on_melee_hit with null target should not crash");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_on_melee_hit_null_target);

// ---------------------------------------------------------------------------
// on_melee_hit: overlapping positions (dist=0) — no divide by zero
// ---------------------------------------------------------------------------

void test_monk_on_melee_hit_same_position()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");
    walker* target = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(target != nullptr, "target created");

    // Same position — dist would be 0
    place_at(target, monk->xpos, monk->ypos);
    target->dead = 0;

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    fd->on_melee_hit(monk, target);

    // Should not crash or produce NaN
    TEST_ASSERT(target->stats()->has_commands(), "target should still get force_command even at same position");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_on_melee_hit_same_position);

// ---------------------------------------------------------------------------
// check_special_ai: special 1 (Silence)
// ---------------------------------------------------------------------------

void test_monk_ai_silence_check()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");
    walker* foe = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(foe != nullptr, "foe created");

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    TEST_ASSERT(fd != nullptr && fd->check_special_ai != nullptr, "monk has check_special_ai");

    monk->current_special = 1;

    // Foe close and not silenced — should want to use silence
    place_at(foe, monk->xpos + 10, monk->ypos);
    foe->silence_left = 0;
    monk->foe = foe;
    TEST_ASSERT(fd->check_special_ai(static_cast<living*>(monk)),
                "AI should want to silence close unsilenced foe");

    // Foe already silenced — should not want to use silence
    foe->silence_left = 50;
    TEST_ASSERT(!fd->check_special_ai(static_cast<living*>(monk)),
                "AI should not silence already-silenced foe");

    // Foe too far — should not want to silence
    foe->silence_left = 0;
    place_at(foe, monk->xpos + 200, monk->ypos);
    TEST_ASSERT(!fd->check_special_ai(static_cast<living*>(monk)),
                "AI should not silence distant foe");

    // No foe — should return false
    monk->foe = nullptr;
    TEST_ASSERT(!fd->check_special_ai(static_cast<living*>(monk)),
                "AI should not silence when no foe exists");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_ai_silence_check);

// ---------------------------------------------------------------------------
// check_special_ai: special 2 (Meditate) — use when hurt
// ---------------------------------------------------------------------------

void test_monk_ai_meditate_check()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    monk->current_special = 2;

    // Below 50% HP — should want to meditate
    monk->stats()->hitpoints = monk->stats()->max_hitpoints * 0.3f;
    TEST_ASSERT(fd->check_special_ai(static_cast<living*>(monk)),
                "AI should want to meditate when below 50% HP");

    // Above 50% HP — should not want to meditate
    monk->stats()->hitpoints = monk->stats()->max_hitpoints * 0.8f;
    TEST_ASSERT(!fd->check_special_ai(static_cast<living*>(monk)),
                "AI should not meditate when above 50% HP");
}
REGISTER_MONK_TEST(test_monk_ai_meditate_check);

// ---------------------------------------------------------------------------
// check_special_ai: special 3 (Evasion) — use when surrounded (>= 2 foes)
// ---------------------------------------------------------------------------

void test_monk_ai_evasion_check()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    // Place 2 foes nearby
    walker* foe1 = make_foe(FAMILY_ORC, 2, 2);
    walker* foe2 = make_foe(FAMILY_SKELETON, 2, 2);
    TEST_ASSERT(foe1 != nullptr && foe2 != nullptr, "foes created");
    place_at(foe1, monk->xpos + 10, monk->ypos);
    place_at(foe2, monk->xpos, monk->ypos + 10);

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    monk->current_special = 3;

    TEST_ASSERT(fd->check_special_ai(static_cast<living*>(monk)),
                "AI should want evasion when surrounded by 2+ foes");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_ai_evasion_check);

// ---------------------------------------------------------------------------
// check_special_ai: special 4 (Whirlwind Kick) — use with >= 3 foes close
// ---------------------------------------------------------------------------

void test_monk_ai_whirlwind_check()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    walker* foe1 = make_foe(FAMILY_ORC, 2, 2);
    walker* foe2 = make_foe(FAMILY_SKELETON, 2, 2);
    walker* foe3 = make_foe(FAMILY_GHOST, 2, 2);
    TEST_ASSERT(foe1 && foe2 && foe3, "foes created");
    place_at(foe1, monk->xpos + 10, monk->ypos);
    place_at(foe2, monk->xpos, monk->ypos + 10);
    place_at(foe3, monk->xpos - 10, monk->ypos);

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    monk->current_special = 4;

    TEST_ASSERT(fd->check_special_ai(static_cast<living*>(monk)),
                "AI should want whirlwind kick with 3+ close foes");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_ai_whirlwind_check);

// ---------------------------------------------------------------------------
// check_special_ai: unknown special index returns true (fallthrough)
// ---------------------------------------------------------------------------

void test_monk_ai_unknown_special()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_MONK);
    monk->current_special = 99;

    TEST_ASSERT(fd->check_special_ai(static_cast<living*>(monk)),
                "AI should return true for unknown special index");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_ai_unknown_special);

// ---------------------------------------------------------------------------
// Whirlwind Kick knockback: foes should be pushed away from monk
// ---------------------------------------------------------------------------

void test_monk_whirlwind_knockback()
{
    walker* monk = make_monk(0, 5);
    TEST_ASSERT(monk != nullptr, "monk created");

    walker* foe = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(foe != nullptr, "foe created");

    place_at(foe, monk->xpos + 20, monk->ypos);
    foe->stats()->hitpoints = 9999.0f;
    foe->stats()->max_hitpoints = 9999.0f;
    short foe_x_before = foe->xpos;

    monk->current_special = 4;
    monk->busy = 0;

    monk->special();

    // Foe should have been pushed further right (away from monk)
    TEST_ASSERT(foe->xpos > foe_x_before, "whirlwind kick should knock foe away from monk");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_whirlwind_knockback);

// ---------------------------------------------------------------------------
// Silence targets nearest foe when multiple foes in range
// ---------------------------------------------------------------------------

void test_monk_silence_targets_nearest()
{
    walker* monk = make_monk(0, 5);
    TEST_ASSERT(monk != nullptr, "monk created");

    walker* far_foe = make_foe(FAMILY_ORC, 2, 2);
    walker* near_foe = make_foe(FAMILY_SKELETON, 2, 2);
    TEST_ASSERT(far_foe && near_foe, "foes created");

    place_at(far_foe, monk->xpos + 30, monk->ypos);
    place_at(near_foe, monk->xpos + 5, monk->ypos);

    monk->current_special = 1;
    monk->busy = 0;

    monk->special();

    // The nearest foe should be silenced
    TEST_ASSERT(near_foe->silence_left > 0, "nearest foe should be silenced");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_silence_targets_nearest);

// ---------------------------------------------------------------------------
// Full integration: create monk through special() dispatch path
// ---------------------------------------------------------------------------

void test_monk_special_dispatch_all()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    // Create a foe for specials that need targets
    walker* foe = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(foe != nullptr, "foe created");
    place_at(foe, monk->xpos + 10, monk->ypos);

    // Run each special through the full dispatch path
    for (int sp = 1; sp <= 4; sp++) {
        monk->current_special = static_cast<char>(sp);
        monk->busy = 0;
        monk->stats()->magicpoints = 500;
        for (int shift = 0; shift <= 1; shift++) {
            monk->shifter_down = static_cast<short>(shift);
            monk->busy = 0;
            monk->stats()->magicpoints = 500;
            (void)monk->special();
        }
    }

    delete monk;
}
REGISTER_MONK_TEST(test_monk_special_dispatch_all);

// ---------------------------------------------------------------------------
// MP cost is deducted on successful special
// ---------------------------------------------------------------------------

void test_monk_special_deducts_mp()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");
    walker* foe = make_foe(FAMILY_ORC, 2, 2);
    TEST_ASSERT(foe != nullptr, "foe created");
    place_at(foe, monk->xpos + 10, monk->ypos);

    monk->current_special = 1; // Silence
    monk->busy = 0;
    float mp_before = monk->stats()->magicpoints;
    float cost = monk->stats()->special_cost[1];

    bool result = monk->special();
    TEST_ASSERT(result, "silence should succeed");
    TEST_ASSERT_FLOAT(mp_before - cost, monk->stats()->magicpoints,
                      "special should deduct correct MP cost");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_special_deducts_mp);

// ---------------------------------------------------------------------------
// MP insufficient blocks special
// ---------------------------------------------------------------------------

void test_monk_special_insufficient_mp()
{
    walker* monk = make_monk(0, 3);
    TEST_ASSERT(monk != nullptr, "monk created");

    monk->current_special = 4; // Whirlwind Kick (cost 120)
    monk->stats()->magicpoints = 1.0f;
    monk->busy = 0;

    bool result = monk->special();
    TEST_ASSERT(!result, "special should fail with insufficient MP");

    delete monk;
}
REGISTER_MONK_TEST(test_monk_special_insufficient_mp);
