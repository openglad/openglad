#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)
const char* get_family_string(Sint32 family);

// Verify guy constructor produces same stats as registry base_stats
TEST(FamilyData, guy_constructor_matches_registry)
{
    init_family_registry();
    for (int fam = 0; fam <= FAMILY_ARCHMAGE; fam++)
    {
        auto* d = get_family_descriptor(fam);
        guy g(fam);
        char msg[128];

        std::snprintf(msg, sizeof(msg), "family %d STR mismatch", fam);
        ASSERT_EQ(d->base_stats[0], static_cast<Sint32>(g.strength)) << msg;
        std::snprintf(msg, sizeof(msg), "family %d DEX mismatch", fam);
        ASSERT_EQ(d->base_stats[1], static_cast<Sint32>(g.dexterity)) << msg;
        std::snprintf(msg, sizeof(msg), "family %d CON mismatch", fam);
        ASSERT_EQ(d->base_stats[2], static_cast<Sint32>(g.constitution)) << msg;
        std::snprintf(msg, sizeof(msg), "family %d INT mismatch", fam);
        ASSERT_EQ(d->base_stats[3], static_cast<Sint32>(g.intelligence)) << msg;
        std::snprintf(msg, sizeof(msg), "family %d ARMOR mismatch", fam);
        ASSERT_EQ(d->base_stats[4], static_cast<Sint32>(g.armor)) << msg;
        std::snprintf(msg, sizeof(msg), "family %d LVL mismatch", fam);
        ASSERT_EQ(d->base_stats[5], static_cast<Sint32>(g.level)) << msg;
    }
}


// Verify family names from get_family_string match registry names
TEST(FamilyData, names_match_get_family_string)
{
    init_family_registry();
    // These families have explicit names in get_family_string
    struct { int fam; const char* expected; } checks[] = {
        {FAMILY_SOLDIER, "SOLDIER"},
        {FAMILY_ELF, "ELF"},
        {FAMILY_ARCHER, "ARCHER"},
        {FAMILY_MAGE, "MAGE"},
        {FAMILY_SKELETON, "SKELETON"},
        {FAMILY_CLERIC, "CLERIC"},
        {FAMILY_FIREELEMENTAL, "ELEMENTAL"},
        {FAMILY_FAERIE, "FAERIE"},
        {FAMILY_SLIME, "SLIME"},
        {FAMILY_SMALL_SLIME, "SLIME"},
        {FAMILY_MEDIUM_SLIME, "SLIME"},
        {FAMILY_THIEF, "THIEF"},
        {FAMILY_GHOST, "GHOST"},
        {FAMILY_DRUID, "DRUID"},
        {FAMILY_ORC, "ORC"},
        {FAMILY_BIG_ORC, "ORC CAPTAIN"},
        {FAMILY_BARBARIAN, "BARBARIAN"},
        {FAMILY_ARCHMAGE, "ARCHMAGE"},
    };
    for (auto& check : checks)
    {
        auto* d = get_family_descriptor(check.fam);
        const char* gfs = get_family_string(check.fam);
        char msg[128];
        std::snprintf(msg, sizeof(msg), "family %d name: registry='%s' vs get_family_string='%s'",
                      check.fam, d->name, gfs);
        ASSERT_STREQ(check.expected, d->name) << msg;
        ASSERT_STREQ(check.expected, gfs) << msg;
    }
}


// Verify walker init matches registry for special_cost, weapon_cost, default_weapon
static void teardown_family_walker() {
    if (og::runtime::current_session->myscreen_) og::runtime::current_session->myscreen_->world().delete_objects();
}

class FamilyDataFixture : public ::testing::Test {
public:
    void SetUp() override
    {}

    void TearDown() override
    {
        teardown_family_walker();
    }
};

TEST_F(FamilyDataFixture, walker_init_matches_registry)
{
    init_family_registry();
    ASSERT_NE(nullptr, og::runtime::current_session->myscreen_)
        << "the integration screen (and its loader) is the creation path under test";
    int checked = 0;
    for (int fam = 0; fam < NUM_FAMILIES; fam++)
    {
        auto* d = get_family_descriptor(fam);
        guy g(fam);
        g.teamnum = 0;
        auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);

        char msg[128];
        std::snprintf(msg, sizeof(msg), "family %d must create a walker", fam);
        ASSERT_NE(nullptr, w.get()) << msg;
        ++checked;

        std::snprintf(msg, sizeof(msg), "family %d default_weapon mismatch", fam);
        ASSERT_EQ(d->default_weapon, static_cast<int>(w->default_weapon())) << msg;

        for (int s = 0; s < NUM_SPECIALS; s++)
        {
            std::snprintf(msg, sizeof(msg), "family %d special_cost[%d] mismatch", fam, s);
            ASSERT_EQ(d->special_cost[s], w->stats()->special_cost(s)) << msg;
        }

        std::snprintf(msg, sizeof(msg), "family %d fire_mp_cost mismatch", fam);
        ASSERT_EQ(d->combat.fire_mp_cost, w->stats()->weapon_cost()) << msg;
    }
    // Without this, a create_walker_owned that fails for EVERY family would
    // finish the loop having compared nothing and still be green.
    ASSERT_EQ(NUM_FAMILIES, checked)
        << "every core living family must round-trip through guy_create_walker_owned";
    og::runtime::current_session->myscreen_->world().delete_objects();
}


// Switch Special reads the LIVE registry (issue #321): for every core family,
// at a level that unlocks every slot, repeated presses visit exactly the
// registry's named slots in order and then wrap to 1. (This replaces
// special_names_match_screen, whose subject — the screen's snapshot copy of
// the registry names — no longer exists.)
TEST(FamilyData, switch_special_cycles_the_registry_named_slots)
{
    ASSERT_NE(nullptr, og::runtime::current_session->myscreen_);
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    og::sim::SimEventLog log;
    // Presses Switch Special `presses` times on a fresh seated walker of
    // `fam` and returns the slots it visited, starting from slot 1.
    const auto cycle = [&](int fam, int presses) {
        auto w = std::make_unique<walker>();
        w->set_order_family(Order::Living, static_cast<char>(fam));
        w->set_user(0);
        w->set_act_type(ACT_CONTROL);
        w->stats()->set_level(30);  // (5-1)*3+1 = 13: every slot unlocked
        w->set_current_special(1);
        walker* control = w.get();

        std::vector<int> visited{1};
        for (int press = 0; press < presses; press++)
        {
            SimInputDebounce debounce{};
            PlayerInput pi{};
            pi.pressed[static_cast<int>(InputAction::SwitchSpecial)] = true;
            sim_process_player_input(pi, control, world, 0, 0, debounce, &log);
            EXPECT_EQ(w.get(), control);
            visited.push_back(control->current_special());
        }
        return visited;
    };

    // The shipped default, explicit: with New Specials on every declared
    // slot is in play, so the registry's named slots are what a press visits.
    world.new_specials = 1;
    for (int fam = 0; fam < NUM_FAMILIES; fam++)
    {
        const FamilyDescriptor* d = get_family_descriptor(fam);
        ASSERT_NE(nullptr, d) << "core family " << fam << " is installed";
        int named = 0;
        for (int s = 1; s < NUM_SPECIALS; s++)
        {
            if (d->special_names[s] != nullptr &&
                std::strcmp(d->special_names[s], "NONE") != 0)
                named++;
        }

        const std::vector<int> visited = cycle(fam, std::max(named, 1));
        std::vector<int> expected;
        for (int s = 1; s <= std::max(named, 1); s++)
            expected.push_back(s);
        expected.push_back(1);
        EXPECT_EQ(expected, visited)
            << "family " << fam << " (" << d->name << ") declares " << named
            << " named slots";
        if (fam == FAMILY_SOLDIER)
        {
            EXPECT_EQ((std::vector<int>{1, 2, 3, 4, 1}), visited)
                << "anchor: the soldier's four specials";
        }
        if (fam == FAMILY_SKELETON)
        {
            EXPECT_EQ((std::vector<int>{1, 2, 3, 4, 1}), visited)
                << "anchor: with New Specials on, Tunnel, Dig In, Bone Wall "
                   "and Reassemble";
        }
    }

    // With New Specials off the slots the setting hides are skipped: the
    // classic skeleton's one special wraps at once, the soldier is unchanged.
    world.new_specials = 0;
    EXPECT_EQ((std::vector<int>{1, 1}), cycle(FAMILY_SKELETON, 1))
        << "anchor: with New Specials off, the skeleton's one special wraps "
           "at once";
    EXPECT_EQ((std::vector<int>{1, 2, 3, 4, 1}), cycle(FAMILY_SOLDIER, 4))
        << "anchor: the soldier's four specials, whatever the setting";
    world.new_specials = 1;
}


// Verify bit flags match for all families that set them during walker init
TEST(FamilyData, bit_flags_match_walker_init)
{
    init_family_registry();

    struct FlagCheck { int fam; Sint32 flag; bool expected; };
    FlagCheck checks[] = {
        {FAMILY_ELF, BIT_FORESTWALK, true},
        {FAMILY_FAERIE, BIT_ANIMATE, true},
        {FAMILY_FAERIE, BIT_FLYING, true},
        {FAMILY_FIREELEMENTAL, BIT_ANIMATE, true},
        {FAMILY_GHOST, BIT_ANIMATE, true},
        {FAMILY_GHOST, BIT_FLYING, true},
        {FAMILY_GHOST, BIT_ETHEREAL, true},
        {FAMILY_GHOST, BIT_NO_RANGED, true},
        {FAMILY_SLIME, BIT_ANIMATE, true},
        {FAMILY_SMALL_SLIME, BIT_ANIMATE, true},
        {FAMILY_SMALL_SLIME, BIT_NO_RANGED, true},
        {FAMILY_MEDIUM_SLIME, BIT_ANIMATE, true},
        {FAMILY_ORC, BIT_NO_RANGED, true},
        {FAMILY_SOLDIER, BIT_FLYING, false},
        {FAMILY_SOLDIER, BIT_ANIMATE, false},
    };

    for (auto& c : checks)
    {
        auto* d = get_family_descriptor(c.fam);
        bool has_flag = (d->init_bit_flags & c.flag) != 0;
        char msg[128];
        std::snprintf(msg, sizeof(msg), "family %d flag 0x%x expected %s", c.fam, c.flag, c.expected ? "set" : "clear");
        ASSERT_TRUE(has_flag == c.expected) << msg;
    }
}
