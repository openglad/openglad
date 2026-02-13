#include "graph.h"
#include "entities/guy.h"
#include "data/gloader.h"
#include "test_framework.h"

#include <cstring>
#include <filesystem>
#include <memory>

extern screen* myscreen;

static std::unique_ptr<walker> make_walker_at(char family, short x, short y, unsigned char team)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(2, true);
    auto w = g.create_walker_owned(myscreen);
    if (w) w->setxy(x, y);
    return w;
}

// ---------------------------------------------------------------------------
// screen::act smoke test - exercises the main game loop tick
// ---------------------------------------------------------------------------

void test_screen_act_empty()
{
    myscreen->act();
}
REGISTER_TEST(test_screen_act_empty);

// ---------------------------------------------------------------------------
// add_ob / remove_ob
// ---------------------------------------------------------------------------

void test_screen_add_ob_living()
{
    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "add_ob should succeed");
    w->setxy(50, 50);
    w->dead = 1; // mark for cleanup
}
REGISTER_TEST(test_screen_add_ob_living);

void test_screen_add_ob_weapon()
{
    walker* w = myscreen->level_data.add_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(w != nullptr, "add_ob weapon should succeed");
    w->dead = 1;
}
REGISTER_TEST(test_screen_add_ob_weapon);

void test_screen_add_ob_treasure()
{
    walker* w = myscreen->level_data.add_ob(Order::Treasure, FAMILY_STAIN, 1);
    TEST_ASSERT(w != nullptr, "add_ob treasure should succeed");
    w->dead = 1;
}
REGISTER_TEST(test_screen_add_ob_treasure);

void test_screen_add_ob_effect()
{
    walker* w = myscreen->level_data.add_ob(Order::FX, FAMILY_EXPLOSION);
    TEST_ASSERT(w != nullptr, "add_ob effect should succeed");
    w->dead = 1;
}
REGISTER_TEST(test_screen_add_ob_effect);

// ---------------------------------------------------------------------------
// query_grid_passable - extended tests for all terrain types
// ---------------------------------------------------------------------------

void test_screen_query_grid_passable_walking()
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l) return;
    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER, myscreen);
    if (!w) return;
    w->setxy(100, 100);

    // Test various grid positions
    short result = myscreen->query_grid_passable(100, 100, w.get());
    (void)result;

    result = myscreen->query_grid_passable(50, 50, w.get());
    (void)result;

    result = myscreen->query_grid_passable(200, 150, w.get());
    (void)result;

}
REGISTER_TEST(test_screen_query_grid_passable_walking);

void test_screen_query_grid_passable_weapon()
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l) return;
    auto w = l->create_walker_owned(Order::Weapon, FAMILY_KNIFE, myscreen);
    if (!w) return;
    w->setxy(100, 100);

    short result = myscreen->query_grid_passable(100, 100, w.get());
    (void)result;

}
REGISTER_TEST(test_screen_query_grid_passable_weapon);

// ---------------------------------------------------------------------------
// query_passable
// ---------------------------------------------------------------------------

void test_screen_query_passable_living()
{
    auto w = make_walker_at(FAMILY_SOLDIER, 100, 100, 0);
    if (!w) return;

    short result = myscreen->query_passable(100, 100, w.get());
    (void)result;

    result = myscreen->query_passable(50, 50, w.get());
    (void)result;

}
REGISTER_TEST(test_screen_query_passable_living);

// ---------------------------------------------------------------------------
// first_of extended (various order types)
// ---------------------------------------------------------------------------

void test_screen_first_of_weapon()
{
    walker* result = myscreen->first_of(Order::Weapon, FAMILY_KNIFE);
    (void)result; // may be null if no knives exist
}
REGISTER_TEST(test_screen_first_of_weapon);

void test_screen_first_of_treasure()
{
    walker* result = myscreen->first_of(Order::Treasure, FAMILY_STAIN);
    (void)result;
}
REGISTER_TEST(test_screen_first_of_treasure);

// ---------------------------------------------------------------------------
// save_data access
// ---------------------------------------------------------------------------

void test_screen_save_data_score()
{
    Uint32 old_score = myscreen->save_data.m_score[0];
    myscreen->save_data.m_score[0] += 100;
    TEST_ASSERT(myscreen->save_data.m_score[0] > old_score, "score should increase");
    myscreen->save_data.m_score[0] = old_score; // restore
}
REGISTER_TEST(test_screen_save_data_score);

// ---------------------------------------------------------------------------
// do_notify
// ---------------------------------------------------------------------------

void test_screen_do_notify_with_walker()
{
    auto w = make_walker_at(FAMILY_SOLDIER, 100, 100, 0);
    if (!w) return;
    myscreen->do_notify("Test notification", w.get());
}
REGISTER_TEST(test_screen_do_notify_with_walker);

// ---------------------------------------------------------------------------
// find functions with populated lists
// ---------------------------------------------------------------------------

void test_screen_find_near_foe_with_enemies()
{
    auto seeker = make_walker_at(FAMILY_SOLDIER, 50, 50, 0);
    auto enemy = make_walker_at(FAMILY_ORC, 70, 50, 1);
    if (!seeker || !enemy) {
        return;
    }

    myscreen->level_data.oblist.push_back(std::move(enemy));

    walker* found = myscreen->find_near_foe(seeker.get());
    (void)found;

    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_screen_find_near_foe_with_enemies);

void test_screen_find_far_foe_with_enemies()
{
    auto seeker = make_walker_at(FAMILY_SOLDIER, 50, 50, 0);
    auto enemy = make_walker_at(FAMILY_ORC, 200, 150, 1);
    if (!seeker || !enemy) {
        return;
    }

    myscreen->level_data.oblist.push_back(std::move(enemy));

    walker* found = myscreen->find_far_foe(seeker.get());
    (void)found;

    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_screen_find_far_foe_with_enemies);

// ---------------------------------------------------------------------------
// damage_tile extended
// ---------------------------------------------------------------------------

void test_screen_damage_tile_various()
{
    // Test various positions
    char r1 = myscreen->damage_tile(50, 50);
    char r2 = myscreen->damage_tile(100, 100);
    char r3 = myscreen->damage_tile(200, 150);
    (void)r1; (void)r2; (void)r3;
}
REGISTER_TEST(test_screen_damage_tile_various);

void test_screen_multiview_lifecycle_paths()
{
    myscreen->ready_for_battle(2);
    TEST_ASSERT(myscreen->viewob[0] != nullptr && myscreen->viewob[1] != nullptr,
                "ready_for_battle(2) should initialize two views");

    myscreen->ready_for_battle(3);
    TEST_ASSERT(myscreen->viewob[0] != nullptr && myscreen->viewob[1] != nullptr && myscreen->viewob[2] != nullptr,
                "ready_for_battle(3) should initialize three views");

    myscreen->reset(4);
    TEST_ASSERT(myscreen->viewob[0] != nullptr && myscreen->viewob[1] != nullptr &&
                myscreen->viewob[2] != nullptr && myscreen->viewob[3] != nullptr,
                "reset(4) should initialize four views");

    myscreen->reset(1);
}
REGISTER_TEST(test_screen_multiview_lifecycle_paths);

void test_screen_find_nearest_player_and_draw_panels()
{
    auto seeker = make_walker_at(FAMILY_SOLDIER, 20, 20, 1);
    auto p1 = make_walker_at(FAMILY_ARCHER, 24, 20, 0);
    auto p2 = make_walker_at(FAMILY_MAGE, 200, 160, 0);
    TEST_ASSERT(seeker && p1 && p2, "test walkers should be created");
    if (!seeker || !p1 || !p2)
    {
        return;
    }

    walker* p1p = p1.get();

    p1p->user = 0;
    p2->user = 1;
    myscreen->level_data.oblist.push_back(std::move(p1));
    myscreen->level_data.oblist.push_back(std::move(p2));

    walker* nearest = myscreen->find_nearest_player(seeker.get());
    TEST_ASSERT(nearest == p1p, "nearest player should be the closest user-controlled walker");

    myscreen->draw_panels(1);

    myscreen->level_data.oblist.pop_back();
    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_screen_find_nearest_player_and_draw_panels);

void test_screen_get_scen_title_paths_and_null_foe_guards()
{
    const char* missing = myscreen->get_scen_title("definitely_missing_scen_file", myscreen);
    TEST_ASSERT((std::string(missing) == "ERROR" || std::string(missing) == "none"),
                "missing scenario title should return a fallback title");

    // Existing campaign levels may have varying metadata, but this path should not crash.
    (void)myscreen->get_scen_title("level1", myscreen);

    TEST_ASSERT(myscreen->find_near_foe(nullptr) == nullptr, "find_near_foe should guard nullptr");
    TEST_ASSERT(myscreen->find_far_foe(nullptr) == nullptr, "find_far_foe should guard nullptr");
}
REGISTER_TEST(test_screen_get_scen_title_paths_and_null_foe_guards);

namespace
{
struct ScenTitleFixture
{
    std::filesystem::path scen_dir;
    std::filesystem::path valid_file;
    std::filesystem::path invalid_header_file;
    std::filesystem::path truncated_file;
} g_scen_title_fixture;

void write_bytes(const std::filesystem::path& file, const void* data, size_t size)
{
    SDL_RWops* out = SDL_RWFromFile(file.string().c_str(), "wb");
    if(out == nullptr)
        return;
    SDL_RWwrite(out, data, 1, size);
    SDL_RWclose(out);
}

void setup_scen_title_fixture()
{
    g_scen_title_fixture.scen_dir = std::filesystem::path("scen");
    std::filesystem::create_directories(g_scen_title_fixture.scen_dir);
    g_scen_title_fixture.valid_file = g_scen_title_fixture.scen_dir / "typed_title_valid.fss";
    g_scen_title_fixture.invalid_header_file = g_scen_title_fixture.scen_dir / "typed_title_bad_header.fss";
    g_scen_title_fixture.truncated_file = g_scen_title_fixture.scen_dir / "typed_title_truncated.fss";

    // Valid minimal v6+ scenario title payload.
    char valid_payload[42] = {};
    std::memcpy(valid_payload, "FSS", 3);
    valid_payload[3] = 6;
    std::memcpy(valid_payload + 4, "gridname", 8);
    std::memcpy(valid_payload + 12, "Typed Test Title", 16);
    write_bytes(g_scen_title_fixture.valid_file, valid_payload, sizeof(valid_payload));

    char bad_header[42] = {};
    std::memcpy(bad_header, "BAD", 3);
    bad_header[3] = 6;
    write_bytes(g_scen_title_fixture.invalid_header_file, bad_header, sizeof(bad_header));

    char truncated[4] = {'F', 'S', 'S', 6};
    write_bytes(g_scen_title_fixture.truncated_file, truncated, sizeof(truncated));
}

void teardown_scen_title_fixture()
{
    std::error_code ec;
    std::filesystem::remove(g_scen_title_fixture.valid_file, ec);
    std::filesystem::remove(g_scen_title_fixture.invalid_header_file, ec);
    std::filesystem::remove(g_scen_title_fixture.truncated_file, ec);
}
} // namespace

void test_screen_get_scen_title_with_error_typed_paths()
{
    std::string title;
    screen::ScenarioTitleError err = myscreen->get_scen_title_with_error("typed_title_valid", title);
    TEST_ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::None), static_cast<int>(err),
        "valid scenario file should return typed None");
    TEST_ASSERT(title == "Typed Test Title", "valid scenario file should return title string");

    title.clear();
    err = myscreen->get_scen_title_with_error("typed_title_bad_header", title);
    TEST_ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::InvalidHeader), static_cast<int>(err),
        "invalid header should return InvalidHeader");

    title.clear();
    err = myscreen->get_scen_title_with_error("typed_title_truncated", title);
    TEST_ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::ReadFailed), static_cast<int>(err),
        "truncated file should return ReadFailed");

    title.clear();
    err = myscreen->get_scen_title_with_error("typed_title_missing_file", title);
    TEST_ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::OpenReadFailed), static_cast<int>(err),
        "missing file should return OpenReadFailed");
}
REGISTER_TEST_WITH_FIXTURE(
    test_screen_get_scen_title_with_error_typed_paths,
    setup_scen_title_fixture,
    teardown_scen_title_fixture);
