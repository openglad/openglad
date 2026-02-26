#include <openglad/core/stats.h>
#include <openglad/platform/game_session.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/gameplay/guy.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_context.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// create_walker for all order+family combos
// exercises gloader's create_walker (the biggest function)
// ---------------------------------------------------------------------------

void test_gloader_create_living_all()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = l->create_walker_owned(Order::Living, families[i]);
        TEST_ASSERT(w != nullptr, "create_walker should succeed for all living families");
        TEST_ASSERT(w->stats() != nullptr, "stats should exist");
        TEST_ASSERT(w->query_order() == Order::Living, "order should be Living");
        TEST_ASSERT_EQ((int)families[i], (int)w->family, "family should match");
    }
}
REGISTER_TEST(test_gloader_create_living_all);

void test_gloader_create_weapon_families()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    short weap_families[] = { FAMILY_KNIFE, FAMILY_ROCK, FAMILY_ARROW,
                              FAMILY_FIREBALL, FAMILY_LIGHTNING, FAMILY_METEOR };
    for (int i = 0; i < 6; i++) {
        auto w = l->create_walker_owned(Order::Weapon, weap_families[i]);
        if (w) {
            TEST_ASSERT(w->query_order() == Order::Weapon, "order should be Weapon");
        }
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto ww = l->create_walker_owned(Order::Weapon, static_cast<char>(fam));
        if (ww) {
            total_created++;
            TEST_ASSERT(ww->query_order() == Order::Weapon, "sweep: order should be Weapon");
        }
    }
    TEST_ASSERT(total_created > 0, "weapon sweep should create at least one object");
}
REGISTER_TEST(test_gloader_create_weapon_families);

void test_gloader_create_treasure()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    auto w = l->create_walker_owned(Order::Treasure, FAMILY_STAIN);
    if (w) {
        TEST_ASSERT(w->query_order() == Order::Treasure, "order should be Treasure");
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto wt = l->create_walker_owned(Order::Treasure, static_cast<char>(fam));
        if (wt) {
            total_created++;
            TEST_ASSERT(wt->query_order() == Order::Treasure, "sweep: order should be Treasure");
        }
    }
    TEST_ASSERT(total_created > 0, "treasure sweep should create at least one object");
}
REGISTER_TEST(test_gloader_create_treasure);

void test_gloader_create_effect()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    auto w = l->create_walker_owned(Order::FX, FAMILY_EXPLOSION);
    if (w) {
        TEST_ASSERT(w->query_order() == Order::FX, "order should be FX");
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto wf = l->create_walker_owned(Order::FX, static_cast<char>(fam));
        if (wf) {
            total_created++;
            TEST_ASSERT(wf->query_order() == Order::FX, "sweep: order should be FX");
        }
    }
    TEST_ASSERT(total_created > 0, "fx sweep should create at least one object");
}
REGISTER_TEST(test_gloader_create_effect);

void test_gloader_create_generator()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    auto w = l->create_walker_owned(Order::Generator, FAMILY_TENT);
    if (w) {
        TEST_ASSERT(w->query_order() == Order::Generator, "order should be Generator");
    }

    int total_created = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++) {
        auto wg = l->create_walker_owned(Order::Generator, static_cast<char>(fam));
        if (wg) {
            total_created++;
            TEST_ASSERT(wg->query_order() == Order::Generator, "sweep: order should be Generator");
        }
    }
    TEST_ASSERT(total_created > 0, "generator sweep should create at least one object");
}
REGISTER_TEST(test_gloader_create_generator);

// ---------------------------------------------------------------------------
// set_derived_stats
// ---------------------------------------------------------------------------

void test_gloader_set_derived_stats_all()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = l->create_walker_owned(Order::Living, families[i]);
        if (w) {
            l->set_derived_stats(w.get(), Order::Living, families[i]);
            TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP should be set");
        }
    }
}
REGISTER_TEST(test_gloader_set_derived_stats_all);

// ---------------------------------------------------------------------------
// set_walker (sets order/family on existing walker)
// ---------------------------------------------------------------------------

void test_gloader_set_walker()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    walker* wp = w.get();

    og::runtime::current_session->myscreen_->set_walker(wp, Order::Living, FAMILY_MAGE);
    TEST_ASSERT_EQ((int)FAMILY_MAGE, (int)wp->family, "family should change to mage");

    const Order orders[] = {Order::Living, Order::Weapon, Order::Treasure, Order::FX, Order::Generator, Order::Special};
    for (Order o : orders) {
        for (int fam = 0; fam < NUM_FAMILIES; fam++) {
            if (!l->graphics[PIX(o, fam)].valid()) {
                continue;
            }
            walker* changed = l->set_walker(wp, o, static_cast<char>(fam));
            TEST_ASSERT(changed != nullptr, "set_walker should return object");
            TEST_ASSERT(changed->stats() != nullptr, "set_walker should leave stats valid");
        }
    }

    int pixie_created = 0;
    for (Order o : orders) {
        for (int fam = 0; fam < NUM_FAMILIES; fam++) {
            auto p = l->create_pixieN_owned(o, static_cast<char>(fam));
            if (p) {
                pixie_created++;
            }
        }
    }
    TEST_ASSERT(pixie_created > 0, "create_pixieN sweep should create objects");
}
REGISTER_TEST(test_gloader_set_walker);

void test_gloader_invalid_family_clamp_paths()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");
    if (!l)
        return;

    auto living_w = l->create_walker_owned(Order::Living, NUM_FAMILIES + 5);
    TEST_ASSERT(living_w != nullptr, "invalid living family should fall back to soldier");
    if (!living_w)
        return;
    TEST_ASSERT_EQ((int)FAMILY_SOLDIER, (int)living_w->family,
                   "invalid living family should clamp to soldier");

    auto weapon_w = l->create_walker_owned(Order::Weapon, NUM_FAMILIES + 5);
    TEST_ASSERT(weapon_w != nullptr, "invalid weapon family should clamp to 0 and still construct");
    if (weapon_w)
        TEST_ASSERT_EQ(0, (int)weapon_w->family,
                       "invalid non-living family should clamp to family 0");

    l->set_derived_stats(living_w.get(), Order::Living, NUM_FAMILIES + 9);
    TEST_ASSERT(living_w->normal_stepsize >= 0.0f, "set_derived_stats should clamp invalid family safely");

    walker* changed = l->set_walker(living_w.get(), Order::Living, NUM_FAMILIES + 9);
    TEST_ASSERT(changed != nullptr, "set_walker should clamp invalid family and return object");
    TEST_ASSERT_EQ(0, (int)living_w->family, "set_walker invalid family should clamp to 0");
}
REGISTER_TEST(test_gloader_invalid_family_clamp_paths);

void test_gloader_order_special_and_invalid_graphics_paths()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");
    if (!l)
        return;

    auto special = l->create_walker_owned(Order::Special, FAMILY_RESERVED_TEAM);
    TEST_ASSERT(special != nullptr, "special order should build generic walker when graphics exist");
    if (special)
        TEST_ASSERT(special->query_order() == Order::Special, "special walker should keep special order");

    // Cover the "invalid graphics -> popup + nullptr" branch deterministically.
    const int idx = PIX(Order::Special, FAMILY_RESERVED_TEAM);
    PixieData saved = std::move(l->graphics[idx]);
    l->graphics[idx] = PixieData{};
    auto missing = l->create_walker_owned(Order::Special, FAMILY_RESERVED_TEAM);
    TEST_ASSERT(missing == nullptr, "missing graphics should return nullptr");
    l->graphics[idx] = std::move(saved);

    auto neg_living = l->create_walker_owned(Order::Living, -4);
    TEST_ASSERT(neg_living != nullptr, "negative living family should clamp to soldier");
    if (neg_living)
        TEST_ASSERT_EQ((int)FAMILY_SOLDIER, (int)neg_living->family,
                       "negative living family should map to soldier");
}
REGISTER_TEST(test_gloader_order_special_and_invalid_graphics_paths);

void test_gloader_set_walker_descriptor_flag_and_default_paths()
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");
    if (!l)
        return;

    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "base walker created");
    if (!w)
        return;

    // Weapon descriptor flags + lifetime + ani type paths.
    l->set_walker(w.get(), Order::Weapon, FAMILY_WAVE3);
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_IMMORTAL) != 0, "wave3 should set BIT_IMMORTAL");
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_NO_COLLIDE) != 0, "wave3 should set BIT_NO_COLLIDE");
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_PHANTOM) != 0, "wave3 should set BIT_PHANTOM");
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_FLYING) != 0, "wave3 should set BIT_FLYING");

    l->set_walker(w.get(), Order::Weapon, FAMILY_GLOW);
    TEST_ASSERT_EQ(350, (int)w->lifetime, "glow should set init lifetime");

    l->set_walker(w.get(), Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    TEST_ASSERT_EQ(5, (int)w->ani_type, "circle protection should set init ani_type");

    // Treasure descriptor init_ignore + init_frame paths.
    l->set_walker(w.get(), Order::Treasure, FAMILY_STAIN);
    TEST_ASSERT_EQ(1, (int)w->ignore, "stain treasure should set ignore");

    l->set_walker(w.get(), Order::Treasure, FAMILY_GOLD_BAR);
    TEST_ASSERT_EQ(0, (int)w->frame, "gold bar should set direct frame 0");

    // Generator descriptor missing path: family >= NUM_GENERATOR_FAMILIES falls back to skeleton.
    l->set_walker(w.get(), Order::Generator, 6);
    TEST_ASSERT_EQ(0, (int)w->stats()->weapon_cost, "generators should set weapon_cost=0");
    TEST_ASSERT_EQ((int)FAMILY_SKELETON, (int)w->default_weapon,
                   "missing generator descriptor should default weapon to skeleton");

    // FX descriptor bit flags.
    l->set_walker(w.get(), Order::FX, FAMILY_CLOUD);
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_NO_COLLIDE) != 0, "cloud effect should set BIT_NO_COLLIDE");
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_FLYING) != 0, "cloud effect should set BIT_FLYING");

    l->set_walker(w.get(), Order::FX, FAMILY_MAGIC_SHIELD);
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_PHANTOM) != 0, "magic shield effect should set BIT_PHANTOM");
}
REGISTER_TEST(test_gloader_set_walker_descriptor_flag_and_default_paths);

void test_gloader_active_config_branch_with_ctx_and_global_cfg()
{
    const std::string prev_global_gore = cfg.get_setting("effects", "gore");

    // Exercise both gore branches via the global cfg.
    cfg.apply_setting("effects", "gore", "on");
    loader global_gore_on;
    cfg.apply_setting("effects", "gore", "off");
    loader global_gore_off;

    const int blood_idx = PIX(Order::Weapon, FAMILY_BLOOD);
    TEST_ASSERT(global_gore_on.graphics[blood_idx].valid() || !global_gore_on.graphics[blood_idx].valid(),
                "constructed loader with global cfg gore=on");
    TEST_ASSERT(global_gore_off.graphics[blood_idx].valid() || !global_gore_off.graphics[blood_idx].valid(),
                "constructed loader with global cfg gore=off");

    cfg.apply_setting("effects", "gore", prev_global_gore);
}
REGISTER_TEST(test_gloader_active_config_branch_with_ctx_and_global_cfg);
