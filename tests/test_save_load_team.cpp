#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/guy_create.h>
#include <openglad/platform/local_transport_shadow.h>
#include <functional>
#include <list>
#include <set>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[i].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

static bool interact_match(
    const std::string& id,
    const std::function<bool(const Interactable&)>& predicate)
{
    const auto interactables = get_interactables();
    for (const auto& item : interactables) {
        if (item.id == id && !item.hidden && predicate(item)) {
            const int game_x = item.x + item.width / 2;
            const int game_y = item.y + item.height / 2;
            const int win_x = static_cast<int>(static_cast<float>(game_x)
                * (og::runtime::current_session->viewport_w_ / 320.0f)
                + og::runtime::current_session->viewport_offset_x_);
            const int win_y = static_cast<int>(static_cast<float>(game_y)
                * (og::runtime::current_session->viewport_h_ / 200.0f)
                + og::runtime::current_session->viewport_offset_y_);
            fprintf(stderr, "  [interact] clicking matched '%s' at game(%d,%d) win(%d,%d)\n",
                    id.c_str(), game_x, game_y, win_x, win_y);
            inject_click(win_x, win_y);
            return true;
        }
    }
    fprintf(stderr, "  [interact] WARNING: matched '%s' not found\n", id.c_str());
    return false;
}

// Test: Save a team to a slot, start a new game (resetting team), then load
// the old team back from the slot.
//
// This tests the save/load roundtrip at the data layer -- the UI save/load
// menus use prompt_for_string which is hard to drive from tests, so we test
// the underlying SaveData::save/load directly with team members.
//
// Verifies:
//   1. Saving a team with guys preserves their attributes
//   2. Resetting save data clears the team
//   3. Loading restores the team and their stats

TEST(SaveLoadTeam, save_team_then_load) {
    trace_clear();

    // Build a team with specific guys
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 3;
    og::runtime::current_session->myscreen_->save_data.totalcash = 77777;
    og::runtime::current_session->myscreen_->save_data.totalscore = 42000;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    soldier->name = "TESTGUY1";
    soldier->strength = 25;
    soldier->dexterity = 15;

    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    archer->name = "TESTGUY2";
    archer->strength = 10;
    archer->intelligence = 20;

    auto mage = std::make_unique<guy>(FAMILY_MAGE);
    mage->name = "TESTGUY3";

    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_list[1] = std::move(archer);
    og::runtime::current_session->myscreen_->save_data.team_list[2] = std::move(mage);
    og::runtime::current_session->myscreen_->save_data.team_size = 3;

    // Save to a non-default slot
    bool saved = og::runtime::current_session->myscreen_->save_data.save("save5");
    ASSERT_TRUE(saved) << "save should succeed";

    // Now reset everything -- simulating starting a new game
    og::runtime::current_session->myscreen_->save_data.reset();
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.team_size) << "team_size should be 0 after reset";
    ASSERT_EQ(0, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalcash)) << "totalcash should be 0 after reset";

    // Load the saved team back
    trace_clear();
    bool loaded = og::runtime::current_session->myscreen_->save_data.load("save5");
    ASSERT_TRUE(loaded) << "load should succeed";

    // Verify team data was restored
    ASSERT_EQ(3, og::runtime::current_session->myscreen_->save_data.team_size) << "team should have 3 members";
    ASSERT_EQ(3, og::runtime::current_session->myscreen_->save_data.scen_num) << "scen_num should be restored";
    ASSERT_EQ(77777, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalcash)) << "totalcash should be restored";
    ASSERT_EQ(42000, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalscore)) << "totalscore should be restored";

    // Verify individual guy data was restored
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[0] != nullptr) << "first guy should exist";
    ASSERT_STREQ("TESTGUY1", og::runtime::current_session->myscreen_->save_data.team_list[0]->name.c_str()) << "first guy name should be restored";
    ASSERT_EQ(25, og::runtime::current_session->myscreen_->save_data.team_list[0]->strength) << "first guy strength should be restored";
    ASSERT_EQ(15, og::runtime::current_session->myscreen_->save_data.team_list[0]->dexterity) << "first guy dexterity should be restored";

    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[1] != nullptr) << "second guy should exist";
    ASSERT_STREQ("TESTGUY2", og::runtime::current_session->myscreen_->save_data.team_list[1]->name.c_str()) << "second guy name should be restored";
    ASSERT_EQ(FAMILY_ARCHER, og::runtime::current_session->myscreen_->save_data.team_list[1]->family) << "second guy should be an archer";
    ASSERT_EQ(20, og::runtime::current_session->myscreen_->save_data.team_list[1]->intelligence) << "second guy intelligence should be restored";

    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[2] != nullptr) << "third guy should exist";
    ASSERT_STREQ("TESTGUY3", og::runtime::current_session->myscreen_->save_data.team_list[2]->name.c_str()) << "third guy name should be restored";
    ASSERT_EQ(FAMILY_MAGE, og::runtime::current_session->myscreen_->save_data.team_list[2]->family) << "third guy should be a mage";
}



// Issue #133 follow-through: a promoted (mage -> archmage) character must
// round-trip through the on-disk save format with the upgraded family AND the
// post-promotion stats intact.
TEST(SaveLoadTeam, promoted_archmage_round_trips_through_save) {
    auto& save_data = og::runtime::current_session->myscreen_->save_data;
    save_data.reset();
    save_data.numplayers = 1;
    save_data.current_campaign = "org.openglad.gladiator";

    auto mage = std::make_unique<guy>(FAMILY_MAGE);
    mage->name = "PROMOTEME";
    mage->upgrade_to_level(6);
    // Promote exactly as create_detail_menu's promote button does it:
    // upgrade_to_level(new_level) first, then flip the family.
    mage->upgrade_to_level(1);
    mage->family = FAMILY_ARCHMAGE;
    const short expect_str = mage->strength;
    const short expect_int = mage->intelligence;
    const short expect_level = mage->level;

    save_data.team_list[0] = std::move(mage);
    save_data.team_size = 1;

    ASSERT_TRUE(save_data.save("save6")) << "save should succeed";
    save_data.reset();
    ASSERT_EQ(0, static_cast<int>(save_data.team_size));

    ASSERT_TRUE(save_data.load("save6")) << "load should succeed";
    ASSERT_EQ(1, static_cast<int>(save_data.team_size));
    ASSERT_TRUE(save_data.team_list[0] != nullptr);
    EXPECT_STREQ("PROMOTEME", save_data.team_list[0]->name.c_str());
    EXPECT_EQ(FAMILY_ARCHMAGE, static_cast<int>(save_data.team_list[0]->family))
        << "the upgraded class must survive the save/load round trip";
    EXPECT_EQ(static_cast<int>(expect_level),
              static_cast<int>(save_data.team_list[0]->level));
    EXPECT_EQ(static_cast<int>(expect_str),
              static_cast<int>(save_data.team_list[0]->strength))
        << "post-promotion stats must survive the round trip";
    EXPECT_EQ(static_cast<int>(expect_int),
              static_cast<int>(save_data.team_list[0]->intelligence));

    save_data.reset();
}

// Test: the networked "as if played alone" per-player merge save.
//
// SaveData::merge_owned_guys_from must write progress back ONLY for characters
// owned by the given player (matched by guy::owner_player_index) into their own
// save slots (guy::owner_save_slot), while:
//   - preserving this player's not-brought (excluded) characters, and
//   - ignoring another player's characters — even one carrying the SAME guy id
//     (the same-save-file clone case the owner tag exists to disambiguate).
TEST(SaveLoadTeam, merge_owned_guys_persists_only_own_characters) {
    screen* scr = og::runtime::current_session->myscreen_;

    // Pre-session roster for THIS player (player 0): slot 0 (MINE_A) and slot 2
    // (MINE_C) are brought to the session; slot 1 (KEEP_B) is excluded and must
    // survive untouched.
    scr->save_data.reset();
    scr->save_data.numplayers = 1;
    scr->save_data.current_campaign = "org.openglad.gladiator";
    {
        auto a = std::make_unique<guy>(FAMILY_SOLDIER); a->name = "MINE_A"; a->id = 1; a->exp = 0;
        auto b = std::make_unique<guy>(FAMILY_ARCHER);  b->name = "KEEP_B"; b->id = 2; b->exp = 0;
        auto c = std::make_unique<guy>(FAMILY_MAGE);    c->name = "MINE_C"; c->id = 3; c->exp = 0;
        scr->save_data.team_list[0] = std::move(a);
        scr->save_data.team_list[1] = std::move(b);
        scr->save_data.team_list[2] = std::move(c);
        scr->save_data.team_size = 3;
    }
    ASSERT_TRUE(scr->save_data.save("save0")) << "pre-session save0 must write";

    // Build an in-session world. Player 0's brought characters carry session
    // progress; a second player (owner 1) owns a character with the SAME guy id
    // as MINE_A — it must never leak into player 0's save0.
    std::list<std::unique_ptr<walker>> oblist;
    auto add_walker = [&](unsigned char family, int guy_id, std::uint8_t owner,
                          std::uint8_t slot, std::uint32_t exp,
                          const char* name) {
        guy g(static_cast<int>(family));
        g.id = guy_id;
        g.name = name;
        std::unique_ptr<walker> w = guy_create_walker_owned(g, scr);
        ASSERT_TRUE(w != nullptr);
        w->set_dead(0);
        w->myguy->owner_player_index = owner;
        w->myguy->owner_save_slot = slot;
        w->myguy->exp = exp;
        oblist.push_back(std::move(w));
    };
    add_walker(FAMILY_SOLDIER, 1, /*owner=*/0, /*slot=*/0, /*exp=*/5000, "MINE_A");
    add_walker(FAMILY_MAGE,    3, /*owner=*/0, /*slot=*/2, /*exp=*/8000, "MINE_C");
    add_walker(FAMILY_SOLDIER, 1, /*owner=*/1, /*slot=*/0, /*exp=*/99999, "THEIR_CLONE");

    // Merge player 0's own characters back into a fresh load of save0.
    SaveData merged;
    ASSERT_EQ(SaveDataIoError::None, merged.load_with_error("save0"));
    merged.merge_owned_guys_from(oblist, /*owner_player_index=*/0);

    // Brought, owned characters gained their session progress.
    ASSERT_TRUE(merged.team_list[0] != nullptr);
    EXPECT_STREQ("MINE_A", merged.team_list[0]->name.c_str());
    EXPECT_EQ(5000u, merged.team_list[0]->exp) << "owned slot 0 should gain session exp";
    ASSERT_TRUE(merged.team_list[2] != nullptr);
    EXPECT_STREQ("MINE_C", merged.team_list[2]->name.c_str());
    EXPECT_EQ(8000u, merged.team_list[2]->exp);

    // Excluded (not brought) character preserved untouched.
    ASSERT_TRUE(merged.team_list[1] != nullptr);
    EXPECT_STREQ("KEEP_B", merged.team_list[1]->name.c_str());

    // The other player's same-id clone (owner 1) must not have leaked in.
    EXPECT_NE(99999u, merged.team_list[0]->exp)
        << "another player's same-id character must not overwrite our slot";

    // Roster stayed dense (no gaps that would crash SaveData::save).
    EXPECT_EQ(3, static_cast<int>(merged.team_size));

    scr->save_data.reset();
}

// A brought character that DIES during a won level must be DROPPED from the
// owner's save0 (death sticks), and the roster must re-densify so SaveData::save
// never hits a gap. Survivors keep their growth; not-brought characters stay.
TEST(SaveLoadTeam, merge_owned_guys_drops_dead_own_characters) {
    screen* scr = og::runtime::current_session->myscreen_;

    scr->save_data.reset();
    scr->save_data.numplayers = 1;
    scr->save_data.current_campaign = "org.openglad.gladiator";
    {
        auto a = std::make_unique<guy>(FAMILY_SOLDIER); a->name = "ALIVE_A"; a->id = 1; a->exp = 0;
        auto b = std::make_unique<guy>(FAMILY_ARCHER);  b->name = "DIES_B";  b->id = 2; b->exp = 0;
        auto c = std::make_unique<guy>(FAMILY_MAGE);    c->name = "ALIVE_C"; c->id = 3; c->exp = 0;
        scr->save_data.team_list[0] = std::move(a);
        scr->save_data.team_list[1] = std::move(b);
        scr->save_data.team_list[2] = std::move(c);
        scr->save_data.team_size = 3;
    }
    ASSERT_TRUE(scr->save_data.save("save0"));

    std::list<std::unique_ptr<walker>> oblist;
    auto add_walker = [&](unsigned char family, int guy_id, std::uint8_t owner,
                          std::uint8_t slot, std::uint32_t exp, bool dead,
                          const char* name) {
        guy g(static_cast<int>(family));
        g.id = guy_id;
        g.name = name;
        std::unique_ptr<walker> w = guy_create_walker_owned(g, scr);
        ASSERT_TRUE(w != nullptr);
        w->set_dead(dead ? 1 : 0);
        w->myguy->owner_player_index = owner;
        w->myguy->owner_save_slot = slot;
        w->myguy->exp = exp;
        oblist.push_back(std::move(w));
    };
    add_walker(FAMILY_SOLDIER, 1, /*owner=*/0, /*slot=*/0, /*exp=*/5000, /*dead=*/false, "ALIVE_A");
    add_walker(FAMILY_ARCHER,  2, /*owner=*/0, /*slot=*/1, /*exp=*/0,    /*dead=*/true,  "DIES_B");
    add_walker(FAMILY_MAGE,    3, /*owner=*/0, /*slot=*/2, /*exp=*/8000, /*dead=*/false, "ALIVE_C");

    SaveData merged;
    ASSERT_EQ(SaveDataIoError::None, merged.load_with_error("save0"));
    merged.merge_owned_guys_from(oblist, /*owner_player_index=*/0);

    // The dead character is gone; the roster compacted to the two survivors.
    EXPECT_EQ(2, static_cast<int>(merged.team_size))
        << "the dead brought character should be dropped";
    bool found_dead = false;
    bool found_a = false;
    bool found_c = false;
    for (int i = 0; i < merged.team_size; ++i)
    {
        ASSERT_TRUE(merged.team_list[i] != nullptr)
            << "roster must stay dense after dropping the dead character";
        if (merged.team_list[i]->name == "DIES_B") found_dead = true;
        if (merged.team_list[i]->name == "ALIVE_A") found_a = true;
        if (merged.team_list[i]->name == "ALIVE_C") found_c = true;
    }
    EXPECT_FALSE(found_dead) << "death must stick — DIES_B must not survive the win";
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_c);

    scr->save_data.reset();
}

// Test: Navigate to Load Team menu via UI, see the load slots, and exit
//
// Flow: Main Menu -> Continue -> Load Team -> Back -> Back

struct LoadMenuState {
    bool started;
    bool finished;
    bool saw_load_menu;
};

static int load_menu_injector(void* data)
{
    og::runtime::ensure_thread_session();
    LoadMenuState* state = static_cast<LoadMenuState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("load_team", 10000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking load_team\n");
    interact("load_team");

    // Load menu has load_slot_1 through load_slot_10 and back
    SDL_Delay(500);
    if (wait_for_interactable("load_slot_1", 10000)) {
        state->saw_load_menu = true;
        SDL_Delay(500);

        fprintf(stderr, "  [test] clicking back from load menu\n");
        interact_match("back", [](const Interactable& item) { return item.y >= 170; });
    }

    const Uint64 deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < deadline)
    {
        if (wait_for_interactable("continue_game", 150))
            break;

        if (wait_for_interactable("load_team", 150))
        {
            fprintf(stderr, "  [test] clicking back from team menu\n");
            interact_match("back", [](const Interactable& item) { return item.y < 170; });
            SDL_Delay(150);
            continue;
        }

        if (wait_for_interactable("back", 150))
        {
            fprintf(stderr, "  [test] retry clicking back from load menu\n");
            interact_match("back", [](const Interactable& item) { return item.y >= 170; });
            SDL_Delay(150);
            continue;
        }

        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->finished = true;
    return 0;
}

TEST(SaveLoadTeam, load_team_menu) {
    trace_clear();

    // Need a save so continue_game works
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    LoadMenuState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(load_menu_injector, "load_menu_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_load_menu) << "should have seen the load team menu";
}

// Permadeath toggle (keep_fallen_heroes): with the flag set, update_guys keeps
// dead heroes on the roster (copied exactly like survivors, order preserved);
// with the default 0, the classic drop still applies.
TEST(SaveLoadTeam, update_guys_keep_fallen_keeps_dead_hero) {
    std::list<std::unique_ptr<walker>> oblist;
    auto add_walker = [&](unsigned char family, bool dead, std::uint32_t exp,
                          const char* name) {
        auto w = std::make_unique<walker>();
        w->set_dead(dead ? 1 : 0);
        auto g = std::make_unique<guy>(static_cast<int>(family));
        g->name = name;
        g->exp = exp;
        w->set_owned_myguy(std::move(g));
        oblist.push_back(std::move(w));
    };
    add_walker(FAMILY_SOLDIER, /*dead=*/false, /*exp=*/5000, "ALIVE_A");
    add_walker(FAMILY_ARCHER,  /*dead=*/true,  /*exp=*/1234, "FALLEN_B");
    add_walker(FAMILY_MAGE,    /*dead=*/false, /*exp=*/8000, "ALIVE_C");

    SaveData keep;
    keep.keep_fallen_heroes = 1;
    keep.update_guys(oblist);

    ASSERT_EQ(3, static_cast<int>(keep.team_size))
        << "keep_fallen_heroes must keep the dead hero on the roster";
    ASSERT_TRUE(keep.team_list[0] != nullptr);
    ASSERT_TRUE(keep.team_list[1] != nullptr);
    ASSERT_TRUE(keep.team_list[2] != nullptr);
    EXPECT_STREQ("ALIVE_A", keep.team_list[0]->name.c_str())
        << "roster order must be preserved";
    EXPECT_STREQ("FALLEN_B", keep.team_list[1]->name.c_str())
        << "the fallen hero must stay in its oblist position";
    EXPECT_STREQ("ALIVE_C", keep.team_list[2]->name.c_str());
    EXPECT_EQ(1234u, keep.team_list[1]->exp)
        << "the fallen hero must be copied like a survivor (exp intact)";

    // Default (permadeath on) still drops the dead hero from the same oblist.
    SaveData drop;
    drop.update_guys(oblist);
    ASSERT_EQ(2, static_cast<int>(drop.team_size))
        << "default keep_fallen_heroes=0 must still drop dead heroes";
    ASSERT_TRUE(drop.team_list[0] != nullptr);
    ASSERT_TRUE(drop.team_list[1] != nullptr);
    EXPECT_STREQ("ALIVE_A", drop.team_list[0]->name.c_str());
    EXPECT_STREQ("ALIVE_C", drop.team_list[1]->name.c_str());
}

// Permadeath toggle in the networked per-player merge: a brought-and-died slot
// is PRESERVED from the pre-merge roster (pre-level stats — the corpse's
// session growth is lost) when keep_fallen_heroes is set on the save being
// persisted. The flag rides through the save0 roundtrip (v12).
TEST(SaveLoadTeam, merge_owned_guys_keep_fallen_preserves_died_slot) {
    screen* scr = og::runtime::current_session->myscreen_;

    scr->save_data.reset();
    scr->save_data.numplayers = 1;
    scr->save_data.current_campaign = "org.openglad.gladiator";
    scr->save_data.keep_fallen_heroes = 1;
    {
        auto a = std::make_unique<guy>(FAMILY_SOLDIER); a->name = "ALIVE_A";  a->id = 1; a->exp = 0;
        auto b = std::make_unique<guy>(FAMILY_ARCHER);  b->name = "FALLEN_B"; b->id = 2; b->exp = 1234;
        auto c = std::make_unique<guy>(FAMILY_MAGE);    c->name = "ALIVE_C";  c->id = 3; c->exp = 0;
        scr->save_data.team_list[0] = std::move(a);
        scr->save_data.team_list[1] = std::move(b);
        scr->save_data.team_list[2] = std::move(c);
        scr->save_data.team_size = 3;
    }
    ASSERT_TRUE(scr->save_data.save("save0"));

    std::list<std::unique_ptr<walker>> oblist;
    auto add_walker = [&](unsigned char family, int guy_id, std::uint8_t owner,
                          std::uint8_t slot, std::uint32_t exp, bool dead,
                          const char* name) {
        guy g(static_cast<int>(family));
        g.id = guy_id;
        g.name = name;
        std::unique_ptr<walker> w = guy_create_walker_owned(g, scr);
        ASSERT_TRUE(w != nullptr);
        w->set_dead(dead ? 1 : 0);
        w->myguy->owner_player_index = owner;
        w->myguy->owner_save_slot = slot;
        w->myguy->exp = exp;
        oblist.push_back(std::move(w));
    };
    add_walker(FAMILY_SOLDIER, 1, /*owner=*/0, /*slot=*/0, /*exp=*/5000, /*dead=*/false, "ALIVE_A");
    // The corpse carries session growth (7777) that must NOT persist — the
    // pre-merge roster entry (exp 1234) is what survives.
    add_walker(FAMILY_ARCHER,  2, /*owner=*/0, /*slot=*/1, /*exp=*/7777, /*dead=*/true,  "FALLEN_B");
    add_walker(FAMILY_MAGE,    3, /*owner=*/0, /*slot=*/2, /*exp=*/8000, /*dead=*/false, "ALIVE_C");

    SaveData merged;
    ASSERT_EQ(SaveDataIoError::None, merged.load_with_error("save0"));
    ASSERT_EQ(1, static_cast<int>(merged.keep_fallen_heroes))
        << "keep_fallen_heroes must roundtrip through save0";
    merged.merge_owned_guys_from(oblist, /*owner_player_index=*/0);

    ASSERT_EQ(3, static_cast<int>(merged.team_size))
        << "the died slot must be preserved, not dropped";
    ASSERT_TRUE(merged.team_list[0] != nullptr);
    ASSERT_TRUE(merged.team_list[1] != nullptr);
    ASSERT_TRUE(merged.team_list[2] != nullptr);
    EXPECT_STREQ("ALIVE_A", merged.team_list[0]->name.c_str());
    EXPECT_EQ(5000u, merged.team_list[0]->exp) << "survivors still gain session exp";
    EXPECT_STREQ("FALLEN_B", merged.team_list[1]->name.c_str())
        << "the fallen hero keeps its original slot";
    EXPECT_EQ(1234u, merged.team_list[1]->exp)
        << "the fallen hero keeps PRE-LEVEL stats, not the corpse's growth";
    EXPECT_STREQ("ALIVE_C", merged.team_list[2]->name.c_str());
    EXPECT_EQ(8000u, merged.team_list[2]->exp);

    scr->save_data.reset();
    // reset() deliberately preserves the negotiated match settings (the v10+
    // precedent), so restore the flag this test set or it leaks into any
    // later test that writes save0 under --gtest_shuffle.
    scr->save_data.keep_fallen_heroes = 0;
}

// The real networked flow keeps each peer's private roster in save0 before the
// mission, while lobby-negotiated settings live in the gameplay session. Thus
// persist_owned_characters_to_save0 must take keep_fallen_heroes from the
// SESSION save — reading the stale disk flag would re-drop the fallen hero
// that Permadeath Off promised to keep.
TEST(SaveLoadTeam, networked_persist_reads_session_keep_fallen_not_disk) {
    screen* scr = og::runtime::current_session->myscreen_;

    // Pre-session on-disk save0: permadeath ON (flag 0, today's default).
    scr->save_data.reset();
    scr->save_data.numplayers = 1;
    scr->save_data.current_campaign = "org.openglad.gladiator";
    scr->save_data.keep_fallen_heroes = 0;
    {
        auto a = std::make_unique<guy>(FAMILY_SOLDIER); a->name = "ALIVE_A";  a->id = 1; a->exp = 0;
        auto b = std::make_unique<guy>(FAMILY_ARCHER);  b->name = "FALLEN_B"; b->id = 2; b->exp = 1234;
        scr->save_data.team_list[0] = std::move(a);
        scr->save_data.team_list[1] = std::move(b);
        scr->save_data.team_size = 2;
    }
    ASSERT_TRUE(scr->save_data.save("save0"));

    // The lobby apply-back set the negotiated flag on the IN-MEMORY session
    // save only; the disk copy above still carries 0.
    scr->save_data.keep_fallen_heroes = 1;

    scr->world().delete_objects();
    auto add_walker = [&](unsigned char family, int guy_id, std::uint8_t slot,
                          bool dead, const char* name) {
        guy g(static_cast<int>(family));
        g.id = guy_id;
        g.name = name;
        std::unique_ptr<walker> w = guy_create_walker_owned(g, scr);
        ASSERT_TRUE(w != nullptr);
        w->set_dead(dead ? 1 : 0);
        w->myguy->owner_player_index = 0;
        w->myguy->owner_save_slot = slot;
        scr->world().oblist.push_back(std::move(w));
    };
    add_walker(FAMILY_SOLDIER, 1, /*slot=*/0, /*dead=*/false, "ALIVE_A");
    add_walker(FAMILY_ARCHER,  2, /*slot=*/1, /*dead=*/true,  "FALLEN_B");

    og::runtime::detail::persist_owned_characters_to_save0(
        *scr, /*own_player_index=*/0);

    SaveData after;
    ASSERT_EQ(SaveDataIoError::None, after.load_with_error("save0"));
    ASSERT_EQ(2, static_cast<int>(after.team_size))
        << "the session's Permadeath Off must keep the fallen hero, even "
           "though the disk save0 still said permadeath";
    ASSERT_TRUE(after.team_list[1] != nullptr);
    EXPECT_STREQ("FALLEN_B", after.team_list[1]->name.c_str());
    EXPECT_EQ(1, static_cast<int>(after.keep_fallen_heroes))
        << "the negotiated match rule persists with the merged save";

    scr->world().delete_objects();
    scr->save_data.reset();
    // reset() deliberately preserves the negotiated match settings (the v10+
    // precedent), so restore the flag this test set or it leaks into any
    // later test that writes save0 under --gtest_shuffle.
    scr->save_data.keep_fallen_heroes = 0;
}

TEST(SaveLoadTeam, networked_persist_copies_only_owned_team_totals)
{
    screen* const scr = og::runtime::current_session->myscreen_;

    SaveData disk;
    disk.reset();
    disk.current_campaign = "org.openglad.gladiator";
    disk.scen_num = 1;
    disk.completed_levels[disk.current_campaign] = {4};
    disk.completed_levels["org.openglad.private-history"] = {8};
    disk.current_levels["org.openglad.private-history"] = 9;
    for (std::size_t team = 0; team < std::size(disk.m_totalcash); ++team)
    {
        disk.m_totalcash[team] = 5100u + static_cast<std::uint32_t>(team);
        disk.m_totalscore[team] = 6100u + static_cast<std::uint32_t>(team);
    }
    ASSERT_TRUE(disk.save("save0"));

    scr->save_data.reset();
    scr->save_data.current_campaign = disk.current_campaign;
    scr->save_data.scen_num = 2;
    scr->save_data.completed_levels[disk.current_campaign].insert(1);
    scr->save_data.completed_levels[disk.current_campaign].insert(7);
    scr->save_data.completed_levels["org.openglad.host-history"] = {11};
    scr->world().id = 1;
    for (std::size_t team = 0;
         team < std::size(scr->save_data.m_totalcash);
         ++team)
    {
        scr->save_data.m_totalcash[team] =
            9100u + static_cast<std::uint32_t>(team);
        scr->save_data.m_totalscore[team] =
            10100u + static_cast<std::uint32_t>(team);
    }

    // Global player ids 6 and 7 share gameplay team 2. They must select the
    // team-2 wallet (never index the four-element totals arrays by player id),
    // and duplicate seats on that team must not add the payout twice.
    const std::array<og::runtime::LocalSeatBinding, 2> seats = {{
        {.player_index = 6, .team = 2},
        {.player_index = 7, .team = 2},
    }};
    og::runtime::detail::persist_owned_characters_to_save0(
        *scr,
        std::span<const og::runtime::LocalSeatBinding>(seats));

    SaveData after;
    ASSERT_EQ(SaveDataIoError::None, after.load_with_error("save0"));
    for (std::size_t team = 0; team < std::size(after.m_totalcash); ++team)
    {
        if (team == 2)
        {
            EXPECT_EQ(scr->save_data.m_totalcash[team],
                      after.m_totalcash[team]);
            EXPECT_EQ(scr->save_data.m_totalscore[team],
                      after.m_totalscore[team]);
        }
        else
        {
            EXPECT_EQ(disk.m_totalcash[team], after.m_totalcash[team])
                << "an unowned team's cash must remain private";
            EXPECT_EQ(disk.m_totalscore[team], after.m_totalscore[team])
                << "an unowned team's score must remain private";
        }
    }
    EXPECT_EQ(scr->save_data.m_totalcash[2], after.totalcash)
        << "the legacy primary-wallet field follows the first local seat";
    EXPECT_EQ(2, static_cast<int>(after.scen_num));
    EXPECT_TRUE(after.is_level_completed(1));
    EXPECT_TRUE(after.is_level_completed(4))
        << "the client's divergent private completion history must survive";
    EXPECT_FALSE(after.is_level_completed(7))
        << "the host/session's unrelated completion history must not leak";
    EXPECT_EQ((std::set<int>{8}),
              after.completed_levels["org.openglad.private-history"]);
    EXPECT_EQ(after.completed_levels.end(),
              after.completed_levels.find("org.openglad.host-history"));

    scr->world().id = 0;
    scr->save_data.reset();
}

TEST(SaveLoadTeam, network_client_withdraw_persists_private_cursor_only)
{
    screen* const scr = og::runtime::current_session->myscreen_;

    SaveData disk;
    disk.reset();
    disk.current_campaign = "org.openglad.gladiator";
    disk.scen_num = 2;
    disk.completed_levels[disk.current_campaign] = {1, 4};
    disk.current_levels[disk.current_campaign] = 2;
    disk.keep_fallen_heroes = 1;
    disk.m_totalcash[0] = 4321u;
    disk.m_totalscore[0] = 1234u;
    disk.totalcash = 4321u;
    disk.totalscore = 1234u;
    auto private_member = std::make_unique<guy>(FAMILY_ARCHER);
    private_member->name = "PRIVATE";
    private_member->exp = 777u;
    disk.team_list[0] = std::move(private_member);
    disk.team_size = 1;
    ASSERT_TRUE(disk.save("save0"));

    // Client session state came from the host and deliberately disagrees with
    // this machine's private cursor, history, wallet, and roster.
    scr->save_data.reset();
    scr->save_data.current_campaign = disk.current_campaign;
    scr->save_data.scen_num = 9;
    scr->save_data.completed_levels[disk.current_campaign] = {7, 8};
    scr->save_data.m_totalcash[0] = 99999u;
    scr->save_data.m_totalscore[0] = 88888u;

    ASSERT_TRUE(
        og::runtime::detail::persist_private_campaign_cursor_to_save0(*scr, 6));

    SaveData after;
    ASSERT_EQ(SaveDataIoError::None, after.load_with_error("save0"));
    EXPECT_EQ(6, static_cast<int>(after.scen_num));
    EXPECT_EQ(6, after.current_levels.at(disk.current_campaign));
    EXPECT_EQ((std::set<int>{1, 4}),
              after.completed_levels[disk.current_campaign]);
    EXPECT_EQ(4321u, after.m_totalcash[0]);
    EXPECT_EQ(1234u, after.m_totalscore[0]);
    EXPECT_EQ(4321u, after.totalcash);
    EXPECT_EQ(1234u, after.totalscore);
    EXPECT_EQ(1, static_cast<int>(after.keep_fallen_heroes));
    ASSERT_EQ(1, static_cast<int>(after.team_size));
    ASSERT_NE(nullptr, after.team_list[0]);
    EXPECT_EQ("PRIVATE", after.team_list[0]->name);
    EXPECT_EQ(777u, after.team_list[0]->exp);

    scr->save_data.reset();
}

TEST(SaveLoadTeam,
     network_spectator_win_advances_cursor_without_roster_or_rewards)
{
    screen* const scr = og::runtime::current_session->myscreen_;

    SaveData disk;
    disk.reset();
    disk.current_campaign = "org.openglad.gladiator";
    disk.scen_num = 1;
    disk.numplayers = 0;
    disk.completed_levels[disk.current_campaign] = {4};
    disk.m_totalcash[0] = 3000u;
    disk.m_totalscore[0] = 2000u;
    auto private_member = std::make_unique<guy>(FAMILY_ARCHER);
    private_member->name = "WATCH_PRIV";
    private_member->exp = 456u;
    disk.team_list[0] = std::move(private_member);
    disk.team_size = 1;
    ASSERT_TRUE(disk.save("save0"));

    scr->save_data.reset();
    scr->save_data.current_campaign = disk.current_campaign;
    scr->save_data.scen_num = 2;
    scr->save_data.completed_levels[disk.current_campaign] = {1, 7};
    scr->save_data.m_totalcash[0] = 93000u;
    scr->save_data.m_totalscore[0] = 92000u;

    ASSERT_TRUE(og::runtime::detail::persist_network_win_to_save0(
        *scr,
        std::span<const og::runtime::LocalSeatBinding>(),
        /*completed_level=*/1));

    SaveData after;
    ASSERT_EQ(SaveDataIoError::None, after.load_with_error("save0"));
    EXPECT_EQ(2, static_cast<int>(after.scen_num))
        << "a zero-owner spectator still follows the next lobby round";
    EXPECT_EQ(0, static_cast<int>(after.numplayers));
    EXPECT_EQ((std::set<int>{4}),
              after.completed_levels[disk.current_campaign])
        << "watching a win must not award completed-level credit";
    EXPECT_EQ(3000u, after.m_totalcash[0]);
    EXPECT_EQ(2000u, after.m_totalscore[0]);
    ASSERT_EQ(1, static_cast<int>(after.team_size));
    ASSERT_NE(nullptr, after.team_list[0]);
    EXPECT_EQ("WATCH_PRIV", after.team_list[0]->name);
    EXPECT_EQ(456u, after.team_list[0]->exp);

    scr->save_data.reset();
}

TEST(SaveLoadTeam,
     network_spectator_host_withdraw_advances_private_cursor_without_rewards)
{
    screen* const scr = og::runtime::current_session->myscreen_;

    SaveData private_save;
    private_save.reset();
    private_save.current_campaign = "org.openglad.gladiator";
    private_save.scen_num = 3;
    private_save.numplayers = 0;
    private_save.completed_levels[private_save.current_campaign] = {1, 2};
    private_save.m_totalcash[0] = 2468u;
    private_save.m_totalscore[0] = 1357u;
    auto private_member = std::make_unique<guy>(FAMILY_SOLDIER);
    private_member->name = "SPEC_PRIV";
    private_member->exp = 321u;
    private_save.team_list[0] = std::move(private_member);
    private_save.team_size = 1;
    ASSERT_TRUE(private_save.save("save0"));

    SaveData network_session;
    network_session.reset();
    network_session.current_campaign = private_save.current_campaign;
    network_session.scen_num = 8;
    network_session.completed_levels[network_session.current_campaign] = {7};
    network_session.m_totalcash[0] = 90000u;
    network_session.m_totalscore[0] = 80000u;
    auto network_member = std::make_unique<guy>(FAMILY_MAGE);
    network_member->name = "NET_ROSTER";
    network_member->exp = 999u;
    network_session.team_list[0] = std::move(network_member);
    network_session.team_size = 1;
    ASSERT_TRUE(network_session.save("netsession"));

    ASSERT_EQ(SaveDataIoError::None,
              scr->save_data.load_with_error("netsession"));
    scr->save_data.m_score[0] = 555u; // abandoned in-level gain

    // This host has no owned-seat argument at all: cursor persistence must be
    // independent of character ownership. Destination 5 differs from both the
    // private cursor (3) and the transient session cursor (8).
    ASSERT_TRUE(og::runtime::local_transport_shadow_testing_finalize_withdraw(
        *scr, 5, true));

    SaveData after_session;
    ASSERT_EQ(SaveDataIoError::None,
              after_session.load_with_error("netsession"));
    EXPECT_EQ(5, static_cast<int>(after_session.scen_num));
    EXPECT_EQ((std::set<int>{7}),
              after_session.completed_levels[network_session.current_campaign]);
    EXPECT_EQ(90000u, after_session.m_totalcash[0]);
    ASSERT_EQ(1, static_cast<int>(after_session.team_size));
    ASSERT_NE(nullptr, after_session.team_list[0]);
    EXPECT_EQ("NET_ROSTER", after_session.team_list[0]->name);

    SaveData after_private;
    ASSERT_EQ(SaveDataIoError::None,
              after_private.load_with_error("save0"));
    EXPECT_EQ(5, static_cast<int>(after_private.scen_num));
    EXPECT_EQ(5,
              after_private.current_levels.at(private_save.current_campaign));
    EXPECT_EQ(0, static_cast<int>(after_private.numplayers));
    EXPECT_EQ((std::set<int>{1, 2}),
              after_private.completed_levels[private_save.current_campaign]);
    EXPECT_EQ(2468u, after_private.m_totalcash[0]);
    EXPECT_EQ(1357u, after_private.m_totalscore[0]);
    ASSERT_EQ(1, static_cast<int>(after_private.team_size));
    ASSERT_NE(nullptr, after_private.team_list[0]);
    EXPECT_EQ("SPEC_PRIV", after_private.team_list[0]->name);
    EXPECT_EQ(321u, after_private.team_list[0]->exp);

    scr->save_data.reset();
}
