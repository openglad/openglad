#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <cstring>
#include <filesystem>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> make_walker_at(char family, short x, short y, unsigned char team)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(2, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(x, y);
    return w;
}

// ---------------------------------------------------------------------------
// screen::act smoke test - exercises the main game loop tick
// ---------------------------------------------------------------------------

TEST(ScreenExtended, screen_act_empty)
{
    og::runtime::current_session->myscreen_->act();
}


// ---------------------------------------------------------------------------
// add_ob / remove_ob
// ---------------------------------------------------------------------------

TEST(ScreenExtended, screen_add_ob_living)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "add_ob should succeed";
    w->setxy(50, 50);
    w->dead = 1; // mark for cleanup
}


TEST(ScreenExtended, screen_add_ob_weapon)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(w != nullptr) << "add_ob weapon should succeed";
    w->dead = 1;
}


TEST(ScreenExtended, screen_add_ob_treasure)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Treasure, FAMILY_STAIN, 1);
    ASSERT_TRUE(w != nullptr) << "add_ob treasure should succeed";
    w->dead = 1;
}


TEST(ScreenExtended, screen_add_ob_effect)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_TRUE(w != nullptr) << "add_ob effect should succeed";
    w->dead = 1;
}


// ---------------------------------------------------------------------------
// query_grid_passable - extended tests for all terrain types
// ---------------------------------------------------------------------------

TEST(ScreenExtended, screen_query_grid_passable_walking)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return;
    auto w = l->create_walker_owned(Order::Living, FAMILY_SOLDIER);
    if (!w) return;
    w->setxy(100, 100);

    // Test various grid positions
    short result = og::runtime::current_session->myscreen_->world().query_grid_passable(100, 100, w.get());
    (void)result;

    result = og::runtime::current_session->myscreen_->world().query_grid_passable(50, 50, w.get());
    (void)result;

    result = og::runtime::current_session->myscreen_->world().query_grid_passable(200, 150, w.get());
    (void)result;

}


TEST(ScreenExtended, screen_query_grid_passable_weapon)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return;
    auto w = l->create_walker_owned(Order::Weapon, FAMILY_KNIFE);
    if (!w) return;
    w->setxy(100, 100);

    short result = og::runtime::current_session->myscreen_->world().query_grid_passable(100, 100, w.get());
    (void)result;

}


// ---------------------------------------------------------------------------
// query_passable
// ---------------------------------------------------------------------------

TEST(ScreenExtended, screen_query_passable_living)
{
    auto w = make_walker_at(FAMILY_SOLDIER, 100, 100, 0);
    if (!w) return;

    short result = og::runtime::current_session->myscreen_->world().query_passable(100, 100, w.get());
    (void)result;

    result = og::runtime::current_session->myscreen_->world().query_passable(50, 50, w.get());
    (void)result;

}


// ---------------------------------------------------------------------------
// first_of extended (various order types)
// ---------------------------------------------------------------------------

TEST(ScreenExtended, screen_first_of_weapon)
{
    walker* result = og::runtime::current_session->myscreen_->first_of(Order::Weapon, FAMILY_KNIFE);
    (void)result; // may be null if no knives exist
}


TEST(ScreenExtended, screen_first_of_treasure)
{
    walker* result = og::runtime::current_session->myscreen_->first_of(Order::Treasure, FAMILY_STAIN);
    (void)result;
}


// ---------------------------------------------------------------------------
// save_data access
// ---------------------------------------------------------------------------

TEST(ScreenExtended, screen_save_data_score)
{
    Uint32 old_score = og::runtime::current_session->myscreen_->save_data.m_score[0];
    og::runtime::current_session->myscreen_->save_data.m_score[0] += 100;
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.m_score[0] > old_score) << "score should increase";
    og::runtime::current_session->myscreen_->save_data.m_score[0] = old_score; // restore
}


TEST(ScreenExtended, screen_endgame_clears_mission_score_after_payout)
{
    const char saved_end = og::runtime::current_session->myscreen_->world().end;

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    og::runtime::current_session->myscreen_->save_data.m_totalscore[0] = 1000;
    og::runtime::current_session->myscreen_->save_data.m_score[0] = 250;
    og::runtime::current_session->myscreen_->sync_world_from_save_data();
    og::runtime::current_session->myscreen_->world().end = 0;
    (void)og::runtime::current_session->myscreen_->endgame(0, -1);

    ASSERT_EQ(1250, static_cast<int>(og::runtime::current_session->myscreen_->save_data.m_totalscore[0])) << "win should add current mission score exactly once";
    ASSERT_EQ(0, static_cast<int>(og::runtime::current_session->myscreen_->save_data.m_score[0])) << "mission score should reset after payout";

    og::runtime::current_session->myscreen_->world().end = 0;
    (void)og::runtime::current_session->myscreen_->endgame(0, -1);

    ASSERT_EQ(1250, static_cast<int>(og::runtime::current_session->myscreen_->save_data.m_totalscore[0])) << "subsequent wins must not re-credit previous mission score";

    og::runtime::current_session->myscreen_->world().end = saved_end;
}


// ---------------------------------------------------------------------------
// do_notify
// ---------------------------------------------------------------------------

TEST(ScreenExtended, screen_do_notify_with_walker)
{
    auto w = make_walker_at(FAMILY_SOLDIER, 100, 100, 0);
    if (!w) return;
    og::runtime::current_session->myscreen_->do_notify("Test notification", w.get());
}


// ---------------------------------------------------------------------------
// find functions with populated lists
// ---------------------------------------------------------------------------

TEST(ScreenExtended, screen_find_near_foe_with_enemies)
{
    auto seeker = make_walker_at(FAMILY_SOLDIER, 50, 50, 0);
    auto enemy = make_walker_at(FAMILY_ORC, 70, 50, 1);
    if (!seeker || !enemy) {
        return;
    }

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(enemy));

    walker* found = og::runtime::current_session->myscreen_->world().find_near_foe(seeker.get());
    (void)found;

    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


TEST(ScreenExtended, screen_find_far_foe_with_enemies)
{
    auto seeker = make_walker_at(FAMILY_SOLDIER, 50, 50, 0);
    auto enemy = make_walker_at(FAMILY_ORC, 200, 150, 1);
    if (!seeker || !enemy) {
        return;
    }

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(enemy));

    walker* found = og::runtime::current_session->myscreen_->world().find_far_foe(seeker.get());
    (void)found;

    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


// ---------------------------------------------------------------------------
// damage_tile extended
// ---------------------------------------------------------------------------

TEST(ScreenExtended, screen_damage_tile_various)
{
    // Test various positions
    char r1 = og::runtime::current_session->myscreen_->damage_tile(50, 50);
    char r2 = og::runtime::current_session->myscreen_->damage_tile(100, 100);
    char r3 = og::runtime::current_session->myscreen_->damage_tile(200, 150);
    (void)r1; (void)r2; (void)r3;
}


TEST(ScreenExtended, screen_multiview_lifecycle_paths)
{
    og::runtime::current_session->myscreen_->ready_for_battle(2);
    ASSERT_TRUE(og::runtime::current_session->myscreen_->viewob[0] != nullptr && og::runtime::current_session->myscreen_->viewob[1] != nullptr) << "ready_for_battle(2) should initialize two views";

    og::runtime::current_session->myscreen_->ready_for_battle(3);
    ASSERT_TRUE(og::runtime::current_session->myscreen_->viewob[0] != nullptr && og::runtime::current_session->myscreen_->viewob[1] != nullptr && og::runtime::current_session->myscreen_->viewob[2] != nullptr) << "ready_for_battle(3) should initialize three views";

    og::runtime::current_session->myscreen_->reset(4);
    ASSERT_TRUE(og::runtime::current_session->myscreen_->viewob[0] != nullptr && og::runtime::current_session->myscreen_->viewob[1] != nullptr &&
                og::runtime::current_session->myscreen_->viewob[2] != nullptr && og::runtime::current_session->myscreen_->viewob[3] != nullptr) << "reset(4) should initialize four views";

    og::runtime::current_session->myscreen_->reset(1);
}


TEST(ScreenExtended, screen_find_nearest_player_and_draw_panels)
{
    auto seeker = make_walker_at(FAMILY_SOLDIER, 20, 20, 1);
    auto p1 = make_walker_at(FAMILY_ARCHER, 24, 20, 0);
    auto p2 = make_walker_at(FAMILY_MAGE, 200, 160, 0);
    ASSERT_TRUE(seeker && p1 && p2) << "test walkers should be created";
    if (!seeker || !p1 || !p2)
    {
        return;
    }

    walker* p1p = p1.get();

    p1p->user = 0;
    p2->user = 1;
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(p1));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(p2));

    walker* nearest = og::runtime::current_session->myscreen_->world().find_nearest_player(seeker.get());
    ASSERT_TRUE(nearest == p1p) << "nearest player should be the closest user-controlled walker";

    og::runtime::current_session->myscreen_->draw_panels(1);

    og::runtime::current_session->myscreen_->world().oblist.pop_back();
    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


TEST(ScreenExtended, screen_get_scen_title_paths_and_null_foe_guards)
{
    const char* missing = og::runtime::current_session->myscreen_->get_scen_title("definitely_missing_scen_file", og::runtime::current_session->myscreen_);
    ASSERT_TRUE((std::string(missing) == "ERROR" || std::string(missing) == "none")) << "missing scenario title should return a fallback title";

    // Existing campaign levels may have varying metadata, but this path should not crash.
    (void)og::runtime::current_session->myscreen_->get_scen_title("level1", og::runtime::current_session->myscreen_);

    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().find_near_foe(nullptr) == nullptr) << "find_near_foe should guard nullptr";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world().find_far_foe(nullptr) == nullptr) << "find_far_foe should guard nullptr";
}


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

class ScreenExtended {
public:
    void SetUp()
    {
        setup_scen_title_fixture();
    }

    void TearDown()
    {
        teardown_scen_title_fixture();
    }
};

TEST_F(ScreenExtended, screen_get_scen_title_with_error_typed_paths)
{
    std::string title;
    screen::ScenarioTitleError err = og::runtime::current_session->myscreen_->get_scen_title_with_error("typed_title_valid", title);
    ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::None), static_cast<int>(err)) << "valid scenario file should return typed None";
    ASSERT_TRUE(title == "Typed Test Title") << "valid scenario file should return title string";

    title.clear();
    err = og::runtime::current_session->myscreen_->get_scen_title_with_error("typed_title_bad_header", title);
    ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::InvalidHeader), static_cast<int>(err)) << "invalid header should return InvalidHeader";

    title.clear();
    err = og::runtime::current_session->myscreen_->get_scen_title_with_error("typed_title_truncated", title);
    ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::ReadFailed), static_cast<int>(err)) << "truncated file should return ReadFailed";

    title.clear();
    err = og::runtime::current_session->myscreen_->get_scen_title_with_error("typed_title_missing_file", title);
    ASSERT_EQ(static_cast<int>(screen::ScenarioTitleError::OpenReadFailed), static_cast<int>(err)) << "missing file should return OpenReadFailed";
}

