// What the player sees of the New Specials kits and of the Gameplay Fixes,
// played in the REAL game: glad_init, game_frame_with_result, the transport
// shadow, the seat's input read from faked SDL key states. Every scene stages
// its actors on the AUTHORITATIVE server world (the display is only a mirror
// of it), presses the keys a player would, and asserts on the display what
// the player sees (its tells): the mine on the ground, the wail hopping
// through a pack, the HUD cell counting a rest down, the refusal line.
//
// The New Specials scenes play one new special each (plus three of a bot
// using one, and a classic countdown with the setting off). The Gameplay
// Fixes scenes play one scene twice from the same seed and the same keys,
// the setting off and then on, and assert both sides.
//
// The tools under scripts/media/ run these tests with OG_FX_CAPTURE_DIR set,
// under which the shared play-through also writes every frame; nothing else
// about a test changes.

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <openglad/core/combat_math.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/kit_marker_role.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/timed_effects.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/interface/render/effects.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include "test_ability_select_scope.h"
#include "test_live_game.h"
#include "test_save_state_guard.h"

// ---------------------------------------------------------------------------
// The fixtures. A scene writes the seat's company to save0
// (live_game::build_save); each test holds save0 as it found it and puts it
// back when it ends, whatever the test did.
// ---------------------------------------------------------------------------
struct SceneFixture : ::testing::Test
{
    std::optional<og::test::ScopedPhysicalFileState> save0;

    void SetUp() override
    {
        save0.emplace(std::filesystem::path(get_user_path()) / "save" / "save0.gtl");
        ASSERT_TRUE(save0->ready())
            << "failed to snapshot save0: " << save0->error().message();
    }
    void TearDown() override { save0.reset(); }
};
struct NewSpecials : SceneFixture {};
struct GameplayFixes : SceneFixture {};

// Which scene each test plays, in declaration order (static initialisation
// runs in declaration order within one translation unit). The two
// "the_tests_name_every_scene_in_order" tests hold this against scenes()
// and pairs(): a scene nobody plays, or played out of order, fails.
struct ListedScene
{
    const char* test;
    const char* scene;
};
std::vector<ListedScene>& new_specials_listed()
{
    static std::vector<ListedScene> all;
    return all;
}
std::vector<ListedScene>& gameplay_fixes_listed()
{
    static std::vector<ListedScene> all;
    return all;
}

// ---------------------------------------------------------------------------
// New Specials: one scene per new special, plus three of a bot using one,
// and a classic special's countdown with the setting off.
//
// The caster is the seat's own hero at level 10 with the New Specials setting
// set on explicitly, on the slot the scene plays and with a full mana pool;
// Special and Shift are pressed for a short two-frame tap, the way a player
// taps a key. Every other actor is staged on the AUTHORITATIVE server world
// (the display is only a mirror of it) and must be on the mirror by frame 0.
// Every scene asserts what the special visibly does (its tell), so a cast
// that fizzles fails the test instead of showing nothing.
//
// The scenes keep their order: the render-effects clock is global and runs
// on from one scene to the next, so what a later scene shows (fire glow,
// dust, trails) depends on the scenes before it. Their tells hold in any
// order; their pixels are the ones the tests see in this order.
// ---------------------------------------------------------------------------

namespace new_specials_scenes {

// An integer tuning value of a living family as its pack declares it (0
// when the key is not there), so a scene's tell follows the shipped number.
int living_tuning(int family, const char* key)
{
    const og::script::TuningMap* map =
        og::script::family_tuning(Order::Living, family);
    if (map == nullptr)
        return 0;
    for (const og::script::TuningPair& pair : *map)
        if (pair.key == key)
            return static_cast<int>(pair.value.integer);
    return 0;
}

// The three random streams, pinned per scene the way the cleric clinic test pins
// them: the display world (what a level load seeds the next world from),
// the authoritative world, and libc rand().
inline constexpr std::uint32_t kDisplayRngPin = 0x5eed5bc1u;
inline constexpr std::uint32_t kAuthorityRngPin = 0x5eed5bc2u;
inline constexpr unsigned int kLibcRandPin = 0x5bc3u;
// LEGION raises half its kills on a coin. The Legion scene shows both sides:
// when the skeleton stands back up the authoritative world's generator is
// set to the first state, under which (measured) its first kill rises, and
// when the second soldier is let go to the second, under which (measured)
// the skeleton's second kill stays down.
inline constexpr std::uint32_t kLegionFirstCoinPin = 1u;
inline constexpr std::uint32_t kLegionSecondCoinPin = 1u;

// The seat's keys, rebound to keycodes no shipped layout uses.
inline constexpr SDL_Keycode kKeyUp = SDLK_F13;
inline constexpr SDL_Keycode kKeyDown = SDLK_F14;
inline constexpr SDL_Keycode kKeyLeft = SDLK_F15;
inline constexpr SDL_Keycode kKeyRight = SDLK_F16;
inline constexpr SDL_Keycode kKeySpecial = SDLK_F17;
inline constexpr SDL_Keycode kKeyShift = SDLK_F18;
inline constexpr SDL_Keycode kKeySwitch = SDLK_F19;
inline constexpr SDL_Keycode kKeyFire = SDLK_F20;

// A natural tap: the key is down for two frames.
inline constexpr int kTapFrames = 2;

// A bot scene's bot gets its mana after this many frames, so the scene shows
// the scene before the bot acts.
inline constexpr int kBotLeadIn = 16;

// The siphon scene's ghost taps SIPHON at these frames, ten apart: more
// than a ghost's attack pause, so every tap is one touch. A level-10
// ghost's touch is 10 + 2 per level = 30 before armour (27-32 rolled), so a
// plain orc's 140 hp is gone by the fifth or sixth touch.
inline constexpr std::array<int, 6> kSiphonTaps = {12, 22, 32, 42, 52, 62};

// The game's shipped effect settings (cfg's defaults), so a scene looks like
// a fresh install: the test process starts with none of them set.
inline constexpr std::array<std::pair<const char*, const char*>, 8>
    kShippedEffects = {{
        {"gore", "on"},
        {"hit_flash", "on"},
        {"hit_recoil", "off"},
        {"attack_lunge", "on"},
        {"hit_anim", "on"},
        {"damage_numbers", "off"},
        {"heal_numbers", "on"},
        {"color_cycling", "on"},
    }};

struct Keys
{
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool special = false;
    bool shift = false;
    bool switch_char = false;
    bool fire = false;
};

bool tap(int frame, int at)
{
    return frame >= at && frame < at + kTapFrames;
}

bool during(int frame, int from, int to)
{
    return frame >= from && frame < to;
}

// One run of a scene as it is being played: the display and the server
// screens, the seat's hero, the actors the scene named when it staged them,
// the marks it keeps between frames, and the frame being played.
struct Run
{
    screen* display = nullptr;
    screen* server = nullptr;
    std::uint32_t hero_id = 0;
    std::map<std::string, std::uint32_t> ids;
    std::map<std::string, float> marks;
    int frame = 0;

    walker* on_server(std::uint32_t id) const
    {
        return server->world().find_by_id(id);
    }
    walker* on_mirror(std::uint32_t id) const
    {
        return display->world().find_by_id(id);
    }
    walker* hero() const { return on_server(hero_id); }
    walker* hero_seen() const { return on_mirror(hero_id); }
    walker* actor(const std::string& name) const
    {
        const auto it = ids.find(name);
        return it == ids.end() ? nullptr : on_server(it->second);
    }
    walker* actor_seen(const std::string& name) const
    {
        const auto it = ids.find(name);
        return it == ids.end() ? nullptr : on_mirror(it->second);
    }
    float mark(const std::string& name) const
    {
        const auto it = marks.find(name);
        return it == marks.end() ? 0.0f : it->second;
    }
};

// What the special visibly does, read on the display mirror after a tick.
// It latches: the run keeps the first frame it holds.
struct Tell
{
    std::string what;
    std::function<bool(Run&)> seen;
};

struct Scene
{
    const char* name = "";
    int family = FAMILY_SOLDIER;
    int slot = 1;
    int frames = 120;
    // The level every roster hero starts at, and the New Specials setting
    // the scene is played with (on server and display alike).
    int level = 10;
    short new_specials = 1;
    // The Gameplay Fixes setting the scene is played with, server and
    // display alike. 1 is the shipped default every other test plays.
    short gameplay_fixes = 1;
    // Hash the seat's viewport after every frame into Result::frame_hashes
    // (the off-beside-on runs prove the halves identical until the fix).
    bool hash_frames = false;
    // Further roster families (heroes on the seat's team, AI-driven); the
    // seat drives the first, the caster.
    std::vector<int> roster_extra;
    // Runs once on the server world with its gameplay context installed.
    std::function<void(Run&)> stage;
    // The seat's keys on each frame.
    std::function<Keys(Run&, int)> keys;
    // Optional server-side hold before each tick (a bot scene keeps its bot
    // where the camera can see it until the bot has cast).
    std::function<void(Run&, int)> before_tick;
    std::vector<Tell> tells;
    // The still is the one frame the tools keep as a picture
    // (<scene>-key.png): still_offset frames after tell still_tell first
    // holds, the last tell when still_tell is negative.
    int still_tell = -1;
    int still_offset = 4;
};

int family_of(Order order, const char* id)
{
    return og::families::resolve_family_string_id(order, id);
}

// Live entities of `order`/`family` (and owned by `owner_id` when nonzero)
// anywhere in the world's three lists.
int count_of(GameWorld& w, Order order, int family, std::uint32_t owner_id = 0)
{
    int n = 0;
    for (const auto* list : {&w.oblist, &w.fxlist, &w.weaplist})
        for (const auto& up : *list)
        {
            const walker* ob = up.get();
            if (ob == nullptr || ob->dead() || ob->query_order() != order ||
                ob->family() != family)
                continue;
            if (owner_id != 0 &&
                (ob->owner() == nullptr || ob->owner()->entity_id() != owner_id))
                continue;
            ++n;
        }
    return n;
}

int count_living(GameWorld& w, int family, unsigned char team)
{
    int n = 0;
    for (const auto& up : w.oblist)
        if (up && !up->dead() && up->query_order() == Order::Living &&
            up->family() == family && up->team_num() == team &&
            !up->dormant())
            ++n;
    return n;
}

// Adds a living on the server beside the hero and names it.
walker* add_actor(Run& run, const std::string& name, int family,
                  unsigned char team, int dx, int dy)
{
    walker* const hero = run.hero();
    walker* const a = run.server->world().add_ob(Order::Living, family);
    EXPECT_NE(nullptr, a) << name;
    if (a == nullptr || hero == nullptr)
        return nullptr;
    a->set_team_num(team);
    a->setxy(static_cast<short>(hero->xpos() + dx),
             static_cast<short>(hero->ypos() + dy));
    run.ids[name] = a->entity_id();
    return a;
}

void set_level(walker* a, int level)
{
    a->stats()->set_level(static_cast<short>(level));
    a->set_difficulty(static_cast<std::uint32_t>(level));
    a->stats()->set_hitpoints(a->stats()->max_hitpoints());
    a->stats()->set_magicpoints(a->stats()->max_magicpoints());
}

// Stages a living and kills it where it stands, leaving its corpse stain.
// The body itself is gone, so it is not one of the run's actors.
void add_corpse(Run& run, int family, unsigned char team, int dx, int dy)
{
    walker* const a = add_actor(run, "corpse", family, team, dx, dy);
    if (a == nullptr)
        return;
    run.ids.erase("corpse");
    a->stats()->set_hitpoints(0.0f);
    a->set_dead(1);
    a->death();
}

// Gives `a` a full pool deep enough for `at_least` mana.
void fill_mana(walker* a, float at_least)
{
    if (a->stats()->max_magicpoints() < at_least)
        a->stats()->set_max_magicpoints(at_least);
    a->stats()->set_magicpoints(a->stats()->max_magicpoints());
}

// The corpse stain nearest to `near` (stains live in the effect list).
walker* nearest_stain(GameWorld& w, const walker& near)
{
    const int stain = family_of(Order::Treasure, "core:stain");
    walker* best = nullptr;
    int best_d = 0;
    for (const auto& up : w.fxlist)
    {
        walker* const ob = up.get();
        if (ob == nullptr || ob->dead() || ob->query_order() != Order::Treasure ||
            ob->family() != stain)
            continue;
        const int d = std::abs(ob->xpos() - near.xpos()) +
                      std::abs(ob->ypos() - near.ypos());
        if (best == nullptr || d < best_d)
        {
            best = ob;
            best_d = d;
        }
    }
    return best;
}

std::uint32_t find_roster_hero(GameWorld& w, int family, std::uint32_t not_id)
{
    for (const auto& up : w.oblist)
        if (up && !up->dead() && up->query_order() == Order::Living &&
            up->team_num() == 0 && up->family() == family &&
            up->myguy != nullptr && up->entity_id() != not_id)
            return up->entity_id();
    return 0;
}

struct Result
{
    int frames_run = 0;
    int blank_frame = -1;
    std::vector<int> tell_frames;
    std::vector<std::string> missing_on_mirror;
    std::vector<std::uint64_t> frame_hashes;
};

// A 64-bit FNV-1a over the viewport's RGB bytes, the rect dump_viewport
// writes, so two runs that hash alike would also look alike.
std::uint64_t viewport_hash(screen* s)
{
    viewscreen* const vs = s->viewob[0].get();
    std::uint64_t h = 1469598103934665603ull;
    for (int j = 0; j < vs->yview; ++j)
        for (int i = 0; i < vs->xview; ++i)
        {
            Uint8 rgb[3];
            s->get_pixel(vs->xloc + i, vs->yloc + j, &rgb[0], &rgb[1], &rgb[2]);
            for (Uint8 c : rgb)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
        }
    return h;
}

// What a time stop does to the screen, measured on the viewport: the mean of
// red + green (the two channels the blue palette halves) and the mean of blue
// (the one it keeps), over the pixels that are not a neutral grey. The grey
// ramps (palette entries below 32) are the same in both palettes, so the
// stone of the status panel would only dilute the change.
struct MeanColour
{
    float warm = 0.0f;  // red + green
    float blue = 0.0f;
};

MeanColour viewport_mean(screen* s)
{
    viewscreen* const vs = s->viewob[0].get();
    double warm = 0.0;
    double blue = 0.0;
    int count = 0;
    for (int j = 0; j < vs->yview; ++j)
        for (int i = 0; i < vs->xview; ++i)
        {
            Uint8 r = 0;
            Uint8 g = 0;
            Uint8 b = 0;
            s->get_pixel(vs->xloc + i, vs->yloc + j, &r, &g, &b);
            if (r == g && g == b)
                continue;
            warm += r + g;
            blue += b;
            ++count;
        }
    MeanColour mean;
    if (count > 0)
    {
        mean.warm = static_cast<float>(warm / count);
        mean.blue = static_cast<float>(blue / count);
    }
    return mean;
}

// Plays `sc` in the real game and fills `out`. The scene always plays and
// its tells always count. With OG_FX_CAPTURE_DIR set (the tools under
// scripts/media/ set it; a test run never does) every frame is also written
// there as a PPM and the still's index as still.txt; nothing else reads them.
// (This comment, the file header and the getenv below are the TU's three
// mentions of that variable; add no fourth.)
void play(const Scene& sc, Result& out)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, s);
    // Each scene reads only its own HUD lines (hud_drew below).
    trace_clear();
    std::vector<int> roster = {sc.family};
    roster.insert(roster.end(), sc.roster_extra.begin(), sc.roster_extra.end());
    live_game::build_save(s, "gladiator", 1, 1, roster, sc.level);
    s->save_data.new_specials = sc.new_specials;
    s->save_data.gameplay_fixes = sc.gameplay_fixes;
    s->world().rng_.state_ = kDisplayRngPin;
    std::srand(kLibcRandPin);
    glad_init();
    live_game::all_effects_on();
    for (const auto& [key, value] : kShippedEffects)
        cfg.apply_setting("effects", key, value);
    cfg.apply_setting("effects", "mini_hp_bar", "on");
    // A scene arms its special with set_current_special and casts it with the
    // SPECIAL key (SHIFTER for the alternate): the key model of New Ability
    // Select OFF. With the setting on, SPECIAL is SPEC 1 and casts whatever
    // the hero's belt holds in slot 1, not the special the scene armed.
    ScopedAbilitySelect classic_keys;
    live_game::force_weather(WeatherKind::None);

    Run run;
    run.display = s;
    run.server = live_game::server_screen();
    ASSERT_NE(nullptr, run.server) << "a scene needs the transport shadow";
    ASSERT_EQ(sc.new_specials, run.server->world().new_specials)
        << "the server plays the scene with its own New Specials setting";
    ASSERT_EQ(sc.new_specials, s->world().new_specials)
        << "the display mirrors the scene's New Specials setting";
    ASSERT_EQ(sc.gameplay_fixes, run.server->world().gameplay_fixes)
        << "the server plays the scene with its own Gameplay Fixes setting";
    ASSERT_EQ(sc.gameplay_fixes, s->world().gameplay_fixes)
        << "the display mirrors the scene's Gameplay Fixes setting";
    run.server->world().rng_.state_ = kAuthorityRngPin;

    walker* const hero = live_game::find_seat_control(run.server->world());
    ASSERT_NE(nullptr, hero) << "the seat's hero is the caster";
    ASSERT_EQ(sc.family, hero->family());
    run.hero_id = hero->entity_id();
    std::vector<std::uint32_t> keep = {run.hero_id};
    for (int fam : sc.roster_extra)
    {
        std::uint32_t not_id = run.hero_id;
        for (std::uint32_t id : keep)
            if (run.on_server(id)->family() == fam)
                not_id = id;
        const std::uint32_t id =
            find_roster_hero(run.server->world(), fam, not_id);
        ASSERT_NE(0u, id) << "roster hero of family " << fam;
        keep.push_back(id);
        run.ids["hero" + std::to_string(keep.size() - 1)] = id;
    }
    const std::uint32_t sentinel_id =
        live_game::strip_level(run.server->world(), keep, 0, 0);
    ASSERT_NE(0u, sentinel_id) << "the level keeps one dormant hostile";
    live_game::strip_level(s->world(), keep, 0, sentinel_id);

    {
        ScopedServerWorldContext server_ctx(*run.server);
        walker* const caster = run.hero();
        caster->set_current_special(static_cast<char>(sc.slot));
        fill_mana(caster, 0.0f);
        if (sc.stage)
            sc.stage(run);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
    }

    SessionKeyStateGuard keystates;
    auto& keymap = og::runtime::current_session->player_keys_[0];
    const std::array<int, 8> slots = {KEY_UP,      KEY_DOWN,    KEY_LEFT,
                                      KEY_RIGHT,   KEY_SPECIAL, KEY_SHIFTER,
                                      KEY_SWITCH,  KEY_FIRE};
    const std::array<SDL_Keycode, 8> codes = {kKeyUp,      kKeyDown,
                                              kKeyLeft,    kKeyRight,
                                              kKeySpecial, kKeyShift,
                                              kKeySwitch,  kKeyFire};
    std::array<int, 8> saved{};
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        saved[i] = keymap[slots[i]];
        keymap[slots[i]] = static_cast<int>(codes[i]);
    }
    reset_viewscreen_input_debounce();

    out.tell_frames.assign(sc.tells.size(), -1);
    const bool writing_frames = std::getenv("OG_FX_CAPTURE_DIR") != nullptr;
    std::vector<std::string> messages;
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    for (int f = 0; f < sc.frames; ++f)
    {
        run.frame = f;
        if (sc.before_tick)
        {
            ScopedServerWorldContext server_ctx(*run.server);
            sc.before_tick(run, f);
        }
        const Keys k = sc.keys ? sc.keys(run, f) : Keys{};
        keystates.set(kKeyUp, k.up);
        keystates.set(kKeyDown, k.down);
        keystates.set(kKeyLeft, k.left);
        keystates.set(kKeyRight, k.right);
        keystates.set(kKeySpecial, k.special);
        keystates.set(kKeyShift, k.shift);
        keystates.set(kKeySwitch, k.switch_char);
        keystates.set(kKeyFire, k.fire);

        if (game_frame_with_result(*s, st, deps) != GameFrameResult::Continue)
            break;
        out.frames_run = f + 1;

        if (f == 0)
        {
            if (s->world().find_by_id(run.hero_id) == nullptr)
                out.missing_on_mirror.push_back("hero");
            for (const auto& [name, id] : run.ids)
            {
                walker* const seen = s->world().find_by_id(id);
                if (seen == nullptr)
                    out.missing_on_mirror.push_back(name);
                // "Hurt" means below what the mirror showed on frame 0.
                else if (run.marks.count(name + ".hp") != 0)
                    run.marks[name + ".hp"] = seen->stats()->hitpoints();
            }
        }

        // The viewport's message lines, logged as they appear (a refused
        // cast says why there).
        if (viewscreen* const vs = s->viewob[0].get())
            for (int i = 0; i < MAX_MESSAGES; ++i)
                if (!vs->textlist[i].empty() &&
                    std::find(messages.begin(), messages.end(),
                              vs->textlist[i]) == messages.end())
                {
                    messages.push_back(vs->textlist[i]);
                    printf("  %s frame %d: \"%s\"\n", sc.name, f,
                           vs->textlist[i].c_str());
                }

        s->redraw();
        score_panel(s);
        s->refresh();
        s->swap();
        if (writing_frames)
            live_game::dump_viewport(s, sc.name, f);
        if (sc.hash_frames)
            out.frame_hashes.push_back(viewport_hash(s));
        if (out.blank_frame < 0 && live_game::viewport_is_blank(s))
            out.blank_frame = f;

        for (std::size_t i = 0; i < sc.tells.size(); ++i)
            if (out.tell_frames[i] < 0 && sc.tells[i].seen(run))
                out.tell_frames[i] = f;

        if (::testing::Test::HasFatalFailure())
            break;
    }

    int still = -1;
    const std::size_t still_tell = sc.still_tell < 0
                                       ? out.tell_frames.size() - 1
                                       : static_cast<std::size_t>(sc.still_tell);
    if (still_tell < out.tell_frames.size() && out.tell_frames[still_tell] >= 0)
        still = std::min(out.frames_run - 1, out.tell_frames[still_tell] + sc.still_offset);
    if (writing_frames)
    {
        char dir[512];
        live_game::dump_dir_for(sc.name, dir, sizeof(dir));
        if (FILE* fp = fopen((std::string(dir) + "/still.txt").c_str(), "w"))
        {
            fprintf(fp, "%d\n", still);
            fclose(fp);
        }
    }
    printf("new specials scene %s: frames=%d still=%d", sc.name, out.frames_run,
           still);
    for (std::size_t i = 0; i < sc.tells.size(); ++i)
        printf(" [%s @%d]", sc.tells[i].what.c_str(), out.tell_frames[i]);
    printf("\n");
    fflush(stdout);

    for (std::size_t i = 0; i < slots.size(); ++i)
        keymap[slots[i]] = saved[i];
    reset_viewscreen_input_debounce();
    live_game::finish_scene();
}

// What every scene must have done, read from its Result: it stayed open to
// its last frame, every frame composed, everything staged on the server was
// on the mirror by frame 0, and every tell was seen, early enough to show
// for 20 frames.
void check(const Scene& sc, const Result& r)
{
    EXPECT_EQ(sc.frames, r.frames_run)
        << "the level must stay open for the whole run";
    EXPECT_EQ(-1, r.blank_frame)
        << "frame " << r.blank_frame << " composed nothing";
    EXPECT_TRUE(r.missing_on_mirror.empty())
        << "staged on the server but not on the mirror at frame 0: "
        << (r.missing_on_mirror.empty() ? "" : r.missing_on_mirror[0]);
    for (std::size_t i = 0; i < sc.tells.size(); ++i)
    {
        EXPECT_GE(r.tell_frames[i], 0)
            << "never seen: " << sc.tells[i].what;
        EXPECT_LE(r.tell_frames[i], sc.frames - 20)
            << "seen too late to show for 20 frames: " << sc.tells[i].what;
    }
}

Keys special_at(int f, int at, bool shift = false)
{
    Keys k;
    k.special = tap(f, at);
    k.shift = k.special && shift;
    return k;
}

float hp_of(walker* w)
{
    return w == nullptr ? -1.0f : w->stats()->hitpoints();
}

bool hurt_since_staging(Run& run, const std::string& name)
{
    walker* const w = run.actor_seen(name);
    return w == nullptr || w->dead() ||
           hp_of(w) < run.mark(name + ".hp");
}

void mark_hp(Run& run, const std::string& name)
{
    run.marks[name + ".hp"] = hp_of(run.actor(name));
}

// Centre-to-centre distance in pixels (a huge one when either is gone).
float gap(const walker* a, const walker* b)
{
    if (a == nullptr || b == nullptr || a->dead() || b->dead())
        return 1.0e9f;
    const float dx = static_cast<float>((a->xpos() + a->sizex() / 2) -
                                        (b->xpos() + b->sizex() / 2));
    const float dy = static_cast<float>((a->ypos() + a->sizey() / 2) -
                                        (b->ypos() + b->sizey() / 2));
    return std::sqrt(dx * dx + dy * dy);
}

// The living named actor nearest to `from` on the mirror, or nullptr.
walker* nearest_seen(Run& run, const walker* from,
                     std::initializer_list<const char*> names)
{
    walker* best = nullptr;
    for (const char* n : names)
    {
        walker* const a = run.actor_seen(n);
        if (a != nullptr && !a->dead() && gap(from, a) < gap(from, best))
            best = a;
    }
    return best;
}

// A cast the scene times by what is on screen: the key goes down on the first
// frame `ready` holds (or at `latest`, whichever comes first) and stays down
// for a natural tap. The frame it went down is kept in the run's marks.
bool cue(Run& run, const std::string& tag, int f, bool ready, int latest)
{
    if (run.marks.count(tag) == 0 && (ready || f >= latest))
        run.marks[tag] = static_cast<float>(f);
    return run.marks.count(tag) != 0 && tap(f, static_cast<int>(run.mark(tag)));
}

// True when the seat's view shows a message line starting with `prefix` (a
// refused cast says why there).
bool voiced(const Run& run, const std::string& prefix)
{
    viewscreen* const vs = run.display->viewob[0].get();
    if (vs == nullptr)
        return false;
    for (int i = 0; i < MAX_MESSAGES; ++i)
        if (vs->textlist[i].rfind(prefix, 0) == 0)
            return true;
    return false;
}

int cued(Run& run, const std::string& tag)
{
    return run.marks.count(tag) == 0 ? 1 << 20
                                      : static_cast<int>(run.mark(tag));
}

// The seat's walker on the mirror: the caster, or whoever the seat drives
// after the caster is gone.
walker* seat_seen(Run& run)
{
    viewscreen* const vs = run.display->viewob[0].get();
    return vs == nullptr ? nullptr : vs->control;
}

// The seat's countdown cell drew exactly `text` ("POSSESS: 48s") on a frame
// of this scene: the HUD's own trace of what it put on the screen.
bool hud_drew(const std::string& text)
{
    return trace_contains("hud", (" text=" + text).c_str());
}

// The seat's countdown as the model answers it on the mirror right now, in
// the HUD cell's words ("" when nothing runs).
std::string seat_timer_text(Run& run)
{
    walker* const c = seat_seen(run);
    if (c == nullptr)
        return {};
    const og::sim::SeatTimer t = og::sim::seat_timer(run.display->world(), *c);
    if (t.label == nullptr)
        return {};
    return std::string(t.label) + ": " +
           std::to_string(og::sim::timer_seconds(t.ticks)) + "s";
}

// The keys a player presses to fight `foe`: walk at it (a step into it
// only turns to face it), and hold Fire once it is in reach. A thrower
// (`back_off` > 0) steps back out of point-blank range and throws from
// there, the way a player keeps a melee foe off a skeleton.
Keys fight_keys(const walker* me, const walker* foe, int reach = 24,
                int back_off = 0)
{
    Keys k;
    if (me == nullptr || foe == nullptr || foe->dead())
        return k;
    const int dx = (foe->xpos() + foe->sizex() / 2) - (me->xpos() + me->sizex() / 2);
    const int dy = (foe->ypos() + foe->sizey() / 2) - (me->ypos() + me->sizey() / 2);
    if (back_off > 0 && std::abs(dx) < back_off && std::abs(dy) < back_off)
    {
        // Away along the wider gap, so the next throw lines up.
        if (std::abs(dx) >= std::abs(dy))
        {
            k.left = dx >= 0;
            k.right = dx < 0;
        }
        else
        {
            k.up = dy >= 0;
            k.down = dy < 0;
        }
        return k;
    }
    k.right = dx > 3;
    k.left = dx < -3;
    k.down = dy > 3;
    k.up = dy < -3;
    k.fire = back_off > 0 || (std::abs(dx) <= reach && std::abs(dy) <= reach);
    return k;
}

// ---- the scenes -----------------------------------------------------------

// The 22 scenes, built at call time (family_of needs the packs loaded), in
// the order the tests play them.
std::vector<Scene> scenes()
{
    std::vector<Scene> all;
    const int fx_mine = family_of(Order::FX, "core:mine");
    const int fx_wail = family_of(Order::FX, "core:wail");
    const int fx_hook = family_of(Order::FX, "core:hook_blade");
    const int fx_ember = family_of(Order::FX, "core:ember");
    const int fx_explosion = family_of(Order::FX, "core:explosion");
    const int fx_marker = family_of(Order::FX, "core:kit_marker");
    const int weap_bone_wall = family_of(Order::Weapon, "core:bone_wall");
    const int weap_banner = family_of(Order::Weapon, "core:war_banner");
    const int weap_knife = family_of(Order::Weapon, "core:knife");
    const int weap_bone = family_of(Order::Weapon, "core:bone");
    const int weap_sprinkle = family_of(Order::Weapon, "core:sprinkle");
    (void)fx_marker;

    // THIEF: Shift + Drop Bomb lays a MINE and the thief steps back; the
    // first orc chasing it walks onto the mine. Then a second mine for the
    // second orc.
    {
        Scene sc;
        sc.name = "thief_mine";
        sc.family = FAMILY_THIEF;
        sc.slot = 1;
        sc.stage = [](Run& run) {
            add_actor(run, "orc1", FAMILY_ORC, 1, 64, -4)->set_act_type(ACT_GUARD);
            mark_hp(run, "orc1");
            walker* orc2 = add_actor(run, "orc2", FAMILY_ORC, 1, 96, -10);
            orc2->set_act_type(ACT_GUARD);
            orc2->set_guard_hold_post(true);
            mark_hp(run, "orc2");
        };
        // The second orc holds back until the first mine has gone off.
        sc.before_tick = [fx_mine](Run& run, int f) {
            if (f > 26)
                if (walker* orc2 = run.actor("orc2"))
                    orc2->set_guard_hold_post(false);
            (void)fx_mine;
        };
        sc.keys = [](Run&, int f) {
            const Keys first = special_at(f, 10, true);
            const Keys second = special_at(f, 34, true);
            Keys k;
            k.special = first.special || second.special;
            k.shift = first.shift || second.shift;
            k.left = during(f, 16, 24) || during(f, 38, 46);
            return k;
        };
        sc.tells = {
            {"a mine lies on the ground",
             [fx_mine](Run& run) {
                 return count_of(run.display->world(), Order::FX, fx_mine,
                                 run.hero_id) > 0;
             }},
            {"the first orc walks onto it and it explodes",
             [](Run& run) { return hurt_since_staging(run, "orc1"); }},
            {"the second orc trips the second mine",
             [](Run& run) {
                 return run.frame > 34 && hurt_since_staging(run, "orc2");
             }},
        };
        sc.still_offset = 2;
        all.push_back(sc);
    }

    // GHOST: Shift + Scare is WAIL, a fright that hops from foe to foe. Two
    // packs of orcs come at the ghost one after the other, from the right
    // and from the left; the first wail scatters the first pack across the
    // open grass. SCARE and WAIL rest 8 seconds between casts, so when the
    // second pack reaches the ghost its first press only says "WAIL IN 3S"
    // or so; once the rest is over the second wail scatters that pack too.
    // The packs start further apart than a wail hops (120 px), so each wail
    // takes only the pack in front of the ghost.
    {
        Scene sc;
        sc.name = "ghost_wail";
        sc.family = FAMILY_GHOST;
        sc.slot = 1;
        sc.frames = 134; // the second wail lands by frame ~113; GIF <= 12.5 s
        sc.stage = [](Run& run) {
            fill_mana(run.hero(), 320.0f);
            const int at[6][2] = {{64, -6},    {80, 8},     {84, -22},
                                  {-110, -4},  {-124, 10},  {-128, -20}};
            for (int i = 0; i < 6; ++i)
            {
                walker* o = add_actor(run, "orc" + std::to_string(i + 1),
                                      FAMILY_ORC, 1, at[i][0], at[i][1]);
                o->set_act_type(ACT_GUARD);
                o->set_guard_hold_post(i >= 3); // the second pack waits
            }
        };
        // The second pack sets off while the wail rests, early enough that
        // the refusal has left the screen before the second wail; every orc
        // a wail strikes is remembered.
        sc.before_tick = [](Run& run, int f) {
            for (int i = 0; i < 6; ++i)
            {
                const std::string n = "orc" + std::to_string(i + 1);
                walker* o = run.actor(n);
                if (o == nullptr)
                    continue;
                // Struck by THIS pack's wail, not an earlier one.
                const int pack_of = i / 3;
                if (o->skip_exit() > 0 &&
                    f >= cued(run, "wail" + std::to_string(pack_of + 1)))
                    run.marks["wailed." + n] = 1.0f;
                if (pack_of == 1 && f >= cued(run, "wail1") + 20)
                    o->set_guard_hold_post(false);
            }
        };
        sc.keys = [](Run& run, int f) {
            walker* const me = run.hero_seen();
            Keys k;
            k.special = cue(run, "wail1", f,
                            f >= 8 && gap(me, nearest_seen(run, me, {"orc1", "orc2", "orc3"})) <= 40.0f,
                            24);
            const int w1 = cued(run, "wail1");
            const float second_pack =
                gap(me, nearest_seen(run, me, {"orc4", "orc5", "orc6"}));
            // The second pack is close and the rest still runs: one tap,
            // refused, and the refusal says how long is left.
            const bool early = cue(run, "early", f,
                                   f >= w1 + 20 && second_pack <= 48.0f, w1 + 50);
            k.special = k.special || (early && f == cued(run, "early"));
            // The 96-tick rest is over: the second wail.
            k.special = k.special ||
                        cue(run, "wail2", f,
                            f >= w1 + 97 && second_pack <= 40.0f, w1 + 110);
            k.shift = k.special;
            // After the first wail the ghost goes to meet the next pack.
            k.left = during(f, w1 + 10, w1 + 22);
            return k;
        };
        const auto pack_wailed = [](int pack) {
            return [pack](Run& run) {
                int n = 0;
                for (int i = 3 * pack; i < 3 * pack + 3; ++i)
                    n += run.marks.count("wailed.orc" + std::to_string(i + 1)) != 0;
                return n >= 2;
            };
        };
        sc.tells = {
            {"the wail leaves the ghost",
             [fx_wail](Run& run) {
                 return count_of(run.display->world(), Order::FX, fx_wail) > 0;
             }},
            {"it hops through the first pack (two or more orcs struck)",
             pack_wailed(0)},
            {"a press during the rest says WAIL IN ...S",
             [](Run& run) {
                 return run.frame >= cued(run, "early") &&
                        run.frame < cued(run, "wail2") &&
                        voiced(run, "WAIL IN ");
             }},
            {"after the rest the second wail scatters the second pack",
             pack_wailed(1)},
        };
        // The still: the first wail's cloud among the first pack.
        sc.still_tell = 0;
        sc.still_offset = 4;
        all.push_back(sc);
    }

    // GHOST: SIPHON, a touch that heals the ghost by half the harm it does,
    // once per attack pause. A badly hurt ghost (its bar red) taps it on a
    // plain orc beside it, on its own mana: every tap takes one step off the
    // orc's bar and puts half of it on the ghost's, and the orc is dead by
    // the sixth.
    {
        Scene sc;
        sc.name = "ghost_siphon";
        sc.family = FAMILY_GHOST;
        sc.slot = 2;
        sc.frames = 100;
        sc.stage = [](Run& run) {
            walker* ghost = run.hero();
            ghost->stats()->set_hitpoints(0.28f * ghost->stats()->max_hitpoints());
            live_game::stop_hp_regen(ghost);
            run.marks["ghost.hp"] = ghost->stats()->hitpoints();
            walker* orc = add_actor(run, "orc", FAMILY_ORC, 1, 18, 0);
            orc->set_act_type(ACT_GUARD);
            orc->set_guard_hold_post(true);
            live_game::stop_hp_regen(orc);
            // It stands its ground: a hurt enemy yells and runs only when its
            // yell is ready, and this one has just yelled.
            orc->set_yo_delay(10000);
            mark_hp(run, "orc");
            printf("  ghost_siphon: ghost hp %.1f of %.1f, mp %.1f, orc hp %.1f\n",
                   ghost->stats()->hitpoints(), ghost->stats()->max_hitpoints(),
                   ghost->stats()->magicpoints(), orc->stats()->hitpoints());
        };
        // Every change of the orc's health is a step; the ghost never swings,
        // so each one is a touch.
        sc.before_tick = [](Run& run, int f) {
            walker* o = run.actor("orc");
            const float hp = o == nullptr || o->dead() ? 0.0f : hp_of(o);
            if (run.marks.count("orc.last") != 0 && hp != run.mark("orc.last"))
            {
                run.marks["orc.steps"] += 1.0f;
                printf("  ghost_siphon frame %d: orc hp %.1f -> %.1f, ghost %.1f\n",
                       f, run.mark("orc.last"), hp, hp_of(run.hero()));
            }
            run.marks["orc.last"] = hp;
        };
        // Six taps, ten frames apart, while the orc lives.
        sc.keys = [](Run& run, int f) {
            Keys k;
            walker* o = run.actor_seen("orc");
            if (o == nullptr || o->dead())
                return k;
            for (int at : kSiphonTaps)
                k.special = k.special || tap(f, at);
            return k;
        };
        sc.tells = {
            {"the first tap takes one step off the orc and heals the ghost",
             [](Run& run) {
                 return run.mark("orc.steps") == 1.0f &&
                        hp_of(run.hero_seen()) > run.mark("ghost.hp");
             }},
            {"one step per tap: three taps, three steps",
             [](Run& run) {
                 return run.frame >= kSiphonTaps[2] + 4 &&
                        run.frame < kSiphonTaps[3] &&
                        run.mark("orc.steps") == 3.0f;
             }},
            {"the orc is dead by the sixth touch, and the ghost has climbed",
             [](Run& run) {
                 walker* o = run.actor_seen("orc");
                 walker* g = run.hero_seen();
                 return (o == nullptr || o->dead()) && g != nullptr &&
                        run.mark("orc.steps") <= 6.0f &&
                        hp_of(g) >= run.mark("ghost.hp") + 40.0f;
             }},
        };
        // The still: the first touch, the orc beside the ghost and the
        // heal number over it.
        sc.still_tell = 0;
        sc.still_offset = 2;
        all.push_back(sc);
    }

    // GHOST: POSSESS a mage, walk it, cast the MAGE's own special (it
    // teleports), then Switch Character to leave the body.
    {
        Scene sc;
        sc.name = "ghost_possess";
        sc.family = FAMILY_GHOST;
        sc.slot = 3;
        sc.frames = 130;
        sc.stage = [](Run& run) {
            walker* mage = add_actor(run, "mage", FAMILY_MAGE, 1, 20, 0);
            mage->set_act_type(ACT_GUARD);
            fill_mana(mage, 0.0f);
        };
        sc.keys = [](Run& run, int f) {
            Keys k = special_at(f, 10);
            k.right = during(f, 30, 48);
            k.down = during(f, 48, 60);
            k.special = k.special || tap(f, 72);
            k.switch_char = tap(f, 102);
            if (f == 29)
            {
                if (walker* m = run.actor_seen("mage"))
                {
                    run.marks["mage.x"] = m->xpos();
                    run.marks["mage.y"] = m->ypos();
                }
            }
            if (f == 71)
            {
                if (walker* m = run.actor_seen("mage"))
                {
                    run.marks["mage.x71"] = m->xpos();
                    run.marks["mage.y71"] = m->ypos();
                }
            }
            return k;
        };
        sc.tells = {
            {"the seat drives the mage",
             [](Run& run) {
                 walker* c = run.display->viewob[0]->control;
                 return c != nullptr && c->entity_id() == run.ids["mage"] &&
                        run.hero_seen() != nullptr &&
                        run.hero_seen()->hidden();
             }},
            {"the possessed mage walks",
             [](Run& run) {
                 walker* m = run.actor_seen("mage");
                 return run.frame >= 50 && run.frame < 72 && m != nullptr &&
                        run.display->viewob[0]->control == m &&
                        std::abs(m->xpos() - run.mark("mage.x")) +
                                std::abs(m->ypos() - run.mark("mage.y")) >= 24;
             }},
            {"the mage casts its own special and teleports",
             [](Run& run) {
                 walker* m = run.actor_seen("mage");
                 return run.frame > 72 && run.frame < 100 && m != nullptr &&
                        run.display->viewob[0]->control == m &&
                        std::abs(m->xpos() - run.mark("mage.x71")) +
                                std::abs(m->ypos() - run.mark("mage.y71")) >= 40;
             }},
            {"Switch Character brings the ghost back out",
             [](Run& run) {
                 walker* c = run.display->viewob[0]->control;
                 return run.frame > 102 && c != nullptr &&
                        c->entity_id() == run.hero_id && !c->hidden();
             }},
            // A level-10 ghost on a level-1 mage rides 580 ticks: 48 s to
            // the nearest second.
            {"the seat's HUD reads POSSESS: 48s at the cast",
             [](Run&) { return hud_drew("POSSESS: 48s"); }},
            {"and counts down (POSSESS: 44s four seconds on)",
             [](Run&) { return hud_drew("POSSESS: 44s"); }},
        };
        // The still: the possessed mage walking, its ring and the
        // countdown in frame.
        sc.still_tell = 1;
        sc.still_offset = 2;
        all.push_back(sc);
    }

    // GHOST: PHASE, untouchable and fast for a few seconds. The ghost slips
    // out between two orcs, comes back solid and fights them, then phases
    // out a second time.
    {
        Scene sc;
        sc.name = "ghost_phase";
        sc.family = FAMILY_GHOST;
        sc.slot = 4;
        sc.stage = [](Run& run) {
            fill_mana(run.hero(), 200.0f);
            add_actor(run, "orc1", FAMILY_ORC, 1, 22, 0)->set_act_type(ACT_GUARD);
            add_actor(run, "orc2", FAMILY_ORC, 1, -22, 8)->set_act_type(ACT_GUARD);
        };
        sc.keys = [](Run& run, int f) {
            walker* const me = run.hero_seen();
            Keys k;
            k.special = tap(f, 12);
            k.up = during(f, 16, 26);
            k.left = during(f, 26, 36);
            k.down = during(f, 36, 46);
            k.right = during(f, 46, 54);
            if (f >= 62 && f < 92)
                k = fight_keys(me,
                               nearest_seen(run, me, {"orc1", "orc2"}));
            const bool again = tap(f, 92);
            if (again)
                k = Keys{};
            k.special = k.special || again;
            k.left = k.left || during(f, 96, 112);
            return k;
        };
        sc.before_tick = [](Run& run, int) {
            walker* g = run.hero();
            if (g != nullptr && g->stats()->query_bit_flags(BIT_PHANTOM) != 0)
                run.marks["phased"] = 1.0f;
        };
        sc.tells = {
            {"the ghost is spectral",
             [](Run& run) {
                 walker* g = run.hero_seen();
                 return g != nullptr &&
                        g->stats()->query_bit_flags(BIT_PHANTOM) != 0;
             }},
            {"it comes back solid among the orcs",
             [](Run& run) {
                 walker* g = run.hero_seen();
                 return run.marks.count("phased") != 0 && run.frame < 92 &&
                        g != nullptr &&
                        g->stats()->query_bit_flags(BIT_PHANTOM) == 0;
             }},
            {"and phases out again",
             [](Run& run) {
                 walker* g = run.hero_seen();
                 return run.frame >= 92 && g != nullptr &&
                        g->stats()->query_bit_flags(BIT_PHANTOM) != 0;
             }},
            // PHASE lasts 48 ticks: four seconds on the HUD, counting down.
            {"the seat's HUD reads PHASE: 4s",
             [](Run&) { return hud_drew("PHASE: 4s"); }},
            {"and counts down to PHASE: 1s",
             [](Run&) { return hud_drew("PHASE: 1s"); }},
        };
        sc.still_tell = 2;
        sc.still_offset = 8;
        all.push_back(sc);
    }

    // FAERIE: BLINK (a short hop of a fixed length in a random direction),
    // then a six-frame hold that hops her once (the rest after a hop stops a
    // held key from chaining), then Shift: SWAP places with an orc.
    {
        Scene sc;
        sc.name = "faerie_blink_swap";
        sc.family = FAMILY_FAERIE;
        sc.slot = 1;
        sc.stage = [](Run& run) {
            add_actor(run, "orc", FAMILY_ORC, 1, 60, 10)->set_act_type(ACT_GUARD);
            // A level-10 faerie hops 24 + 2 x 10 = 44 px.
            run.marks["hop"] = static_cast<float>(
                living_tuning(FAMILY_FAERIE, "blink_base") +
                living_tuning(FAMILY_FAERIE, "blink_per_level") * 10);
            ASSERT_EQ(44.0f, run.mark("hop"));
        };
        // Every move of the faerie on the mirror, counted.
        sc.before_tick = [](Run& run, int) {
            walker* h = run.hero_seen();
            if (h == nullptr)
                return;
            if (run.marks.count("hx") != 0 &&
                (h->xpos() != run.mark("hx") || h->ypos() != run.mark("hy")))
                run.marks["moves"] += 1.0f;
            run.marks["hx"] = h->xpos();
            run.marks["hy"] = h->ypos();
        };
        sc.keys = [](Run& run, int f) {
            if (f == 11 || f == 35 || f == 59)
            {
                walker* h = run.hero_seen();
                walker* o = run.actor_seen("orc");
                const std::string at = f == 11 ? "blink" : f == 35 ? "hold" : "swap";
                if (h == nullptr || o == nullptr)
                    return Keys{};
                run.marks[at + ".hx"] = h->xpos();
                run.marks[at + ".hy"] = h->ypos();
                run.marks[at + ".ox"] = o->xpos();
                run.marks[at + ".oy"] = o->ypos();
                run.marks[at + ".moves"] = run.mark("moves");
            }
            Keys k = special_at(f, 12);
            const Keys swap = special_at(f, 60, true);
            k.special = k.special || during(f, 36, 42) || swap.special;
            k.shift = swap.shift;
            return k;
        };
        sc.tells = {
            {"the faerie hops at least 10 px and at most the hop on each axis",
             [](Run& run) {
                 walker* h = run.hero_seen();
                 if (run.frame < 12 || run.frame >= 36 || h == nullptr)
                     return false;
                 const float dx = std::abs(h->xpos() - run.mark("blink.hx"));
                 const float dy = std::abs(h->ypos() - run.mark("blink.hy"));
                 return std::max(dx, dy) >= 10.0f && dx <= run.mark("hop") &&
                        dy <= run.mark("hop");
             }},
            {"a six-frame hold hops her exactly once",
             [](Run& run) {
                 return run.frame >= 56 && run.frame < 60 &&
                        run.mark("moves") - run.mark("hold.moves") == 1.0f;
             }},
            {"the faerie and the orc trade places",
             [](Run& run) {
                 walker* h = run.hero_seen();
                 walker* o = run.actor_seen("orc");
                 if (run.frame < 60 || h == nullptr || o == nullptr)
                     return false;
                 const float before = std::abs(run.mark("swap.hx") - run.mark("swap.ox")) +
                                      std::abs(run.mark("swap.hy") - run.mark("swap.oy"));
                 const float faerie_moved = std::abs(h->xpos() - run.mark("swap.hx")) +
                                            std::abs(h->ypos() - run.mark("swap.hy"));
                 const float orc_moved = std::abs(o->xpos() - run.mark("swap.ox")) +
                                         std::abs(o->ypos() - run.mark("swap.oy"));
                 return before >= 20 && faerie_moved >= before / 2 &&
                        orc_moved >= before / 2;
             }},
        };
        all.push_back(sc);
    }

    // FAERIE: GLIMMER, freezing powder in all eight directions. Four orcs
    // walk in along four of the eight lines and the powder stops them
    // mid-stride. They thaw and come after her as she flits away; she
    // presses GLIMMER again, but it rests 15 seconds between casts, so the
    // second press only says "GLIMMER IN 12S" or so, and she fights them
    // with her plain sprinkle instead.
    {
        Scene sc;
        sc.name = "faerie_glimmer";
        sc.family = FAMILY_FAERIE;
        sc.slot = 2;
        sc.stage = [](Run& run) {
            fill_mana(run.hero(), 100.0f);
            // Open grass only: the trees up and to the right hide a walker,
            // and the cobbles below are a wall top.
            const int at[4][2] = {{110, 0}, {-110, 0}, {0, -100}, {-76, -76}};
            for (int i = 0; i < 4; ++i)
            {
                const std::string n = "orc" + std::to_string(i + 1);
                add_actor(run, n, FAMILY_ORC, 1, at[i][0], at[i][1])
                    ->set_act_type(ACT_GUARD);
            }
        };
        // Per orc: was it frozen, and has it thawed and walked since.
        sc.before_tick = [](Run& run, int) {
            for (int i = 0; i < 4; ++i)
            {
                const std::string n = "orc" + std::to_string(i + 1);
                walker* o = run.actor(n);
                if (o == nullptr)
                    continue;
                const float frozen = static_cast<float>(o->stats()->frozen_delay());
                if (frozen > 0.0f)
                    run.marks["was_frozen." + n] = 1.0f;
                if (run.marks.count("was_frozen." + n) != 0 && frozen == 0.0f &&
                    run.marks.count(n + ".x") != 0 &&
                    (o->xpos() != run.mark(n + ".x") || o->ypos() != run.mark(n + ".y")))
                    run.marks["thawed." + n] = 1.0f;
                run.marks[n + ".x"] = o->xpos();
                run.marks[n + ".y"] = o->ypos();
            }
        };
        sc.keys = [](Run& run, int f) {
            walker* const me = run.hero_seen();
            walker* const near_orc =
                nearest_seen(run, me, {"orc1", "orc2", "orc3", "orc4"});
            int thawed = 0;
            for (const char* n : {"orc1", "orc2", "orc3", "orc4"})
                thawed += run.marks.count(std::string("thawed.") + n) != 0;
            Keys k;
            // Orcs stand off a flying faerie at about 60 px: the powder
            // catches them while they are still walking in.
            k.special = cue(run, "glimmer1", f, f >= 4 && gap(me, near_orc) <= 70.0f, 30);
            const int g1 = cued(run, "glimmer1");
            // While they thaw she flits away to the left, so they have to
            // walk after her; she presses again as they reach her.
            k.left = during(f, g1 + 18, g1 + 32);
            // A one-frame tap, pressed once her attack pause has run out, so
            // the refusal shown is the rest's, not SPECIAL BUSY.
            const walker* const caster = run.hero();
            const bool rested = caster != nullptr && caster->busy() <= 0.0f;
            const bool g2 = cue(run, "glimmer2", f,
                                f >= g1 + 34 && thawed >= 2 && rested &&
                                    gap(me, near_orc) <= 66.0f,
                                g1 + 70);
            k.special = k.special || (g2 && f == cued(run, "glimmer2"));
            if (f >= cued(run, "glimmer2") + 6)
                k = fight_keys(me, near_orc, 40);
            return k;
        };
        const auto count_marked = [](Run& run, const char* what) {
            int n = 0;
            for (const char* o : {"orc1", "orc2", "orc3", "orc4"})
                n += run.marks.count(std::string(what) + o) != 0;
            return n;
        };
        sc.tells = {
            {"powder flies eight ways",
             [weap_sprinkle](Run& run) {
                 return count_of(run.display->world(), Order::Weapon,
                                 weap_sprinkle) >= 6;
             }},
            {"the orcs are frozen mid-stride",
             [count_marked](Run& run) { return count_marked(run, "was_frozen.") >= 2; }},
            {"they thaw and walk again",
             [count_marked](Run& run) { return count_marked(run, "thawed.") >= 2; }},
            {"the second press waits for the rest: GLIMMER IN ...S",
             [](Run& run) {
                 return run.frame >= cued(run, "glimmer2") &&
                        voiced(run, "GLIMMER IN ");
             }},
        };
        sc.still_tell = 1;
        sc.still_offset = 1;
        all.push_back(sc);
    }

    // FAERIE: HASTEN the soldier beside her; both soldiers then charge the
    // orcs and the hastened one gets there first. Then Shift: HASTE SELF.
    {
        Scene sc;
        sc.name = "faerie_hasten";
        sc.family = FAMILY_FAERIE;
        sc.slot = 3;
        sc.stage = [](Run& run) {
            for (const char* n : {"ally", "other"})
            {
                walker* a = add_actor(run, n, FAMILY_SOLDIER, 0, 18,
                                      std::strcmp(n, "ally") == 0 ? 0 : -30);
                a->set_act_type(ACT_GUARD);
                a->set_guard_hold_post(true);
            }
            add_actor(run, "orc1", FAMILY_ORC, 1, 120, 0)->set_act_type(ACT_GUARD);
            add_actor(run, "orc2", FAMILY_ORC, 1, 124, -30)->set_act_type(ACT_GUARD);
        };
        // Both soldiers hold their post until the haste lands, then charge.
        sc.before_tick = [](Run& run, int) {
            walker* ally = run.actor("ally");
            if (ally == nullptr || ally->speed_bonus_left() == 0 ||
                run.marks.count("charge") != 0)
                return;
            run.marks["charge"] = 1.0f;
            for (const char* n : {"ally", "other"})
                if (walker* a = run.actor(n))
                {
                    a->set_guard_hold_post(false);
                    a->set_act_type(ACT_RANDOM);
                }
        };
        sc.keys = [](Run&, int f) {
            Keys k = special_at(f, 12);
            const Keys self = special_at(f, 56, true);
            k.special = k.special || self.special;
            k.shift = self.shift;
            k.right = during(f, 62, 100);
            return k;
        };
        sc.tells = {
            {"the soldier beside her is hastened",
             [](Run& run) {
                 walker* a = run.actor_seen("ally");
                 walker* b = run.actor_seen("other");
                 return a != nullptr && b != nullptr &&
                        a->speed_bonus_left() > 0 && b->speed_bonus_left() == 0;
             }},
            {"the faerie hastens herself",
             [](Run& run) {
                 walker* h = run.hero_seen();
                 return h != nullptr && h->speed_bonus_left() > 0;
             }},
            {"and her HUD counts the haste down (HASTE: Ns)",
             [](Run& run) {
                 const std::string t = seat_timer_text(run);
                 return t.rfind("HASTE: ", 0) == 0 && hud_drew(t);
             }},
        };
        sc.still_tell = 1;
        all.push_back(sc);
    }

    // FAERIE: WISH. The faerie pops; a fallen soldier rises at full health
    // and the wounded heroes beside it are healed. Then an orc pack arrives,
    // and the healed party fights it.
    {
        Scene sc;
        sc.name = "faerie_wish";
        sc.family = FAMILY_FAERIE;
        sc.slot = 4;
        sc.roster_extra = {FAMILY_SOLDIER, FAMILY_ELF};
        sc.stage = [](Run& run) {
            fill_mana(run.hero(), 100.0f);
            add_corpse(run, FAMILY_SOLDIER, 0, 28, 6);
            for (const char* n : {"hero1", "hero2"})
            {
                walker* h = run.actor(n);
                ASSERT_NE(nullptr, h);
                h->stats()->set_hitpoints(0.3f * h->stats()->max_hitpoints());
                live_game::stop_hp_regen(h);
                h->set_act_type(ACT_GUARD);
                mark_hp(run, n);
            }
            walker* h1 = run.actor("hero1");
            walker* h2 = run.actor("hero2");
            walker* faerie = run.hero();
            h1->setxy(static_cast<short>(faerie->xpos() - 26), faerie->ypos());
            h2->setxy(faerie->xpos(), static_cast<short>(faerie->ypos() + 26));
            run.marks["soldiers"] = static_cast<float>(
                count_living(run.server->world(), FAMILY_SOLDIER, 0));
            for (int i = 0; i < 3; ++i)
            {
                const std::string n = "orc" + std::to_string(i + 1);
                walker* o = add_actor(run, n, FAMILY_ORC, 1, 120 + 10 * i,
                                      -30 + 18 * i);
                o->set_act_type(ACT_GUARD);
                o->set_guard_hold_post(true);
                mark_hp(run, n);
            }
        };
        // The pack holds back until the wish has landed, then comes on.
        sc.before_tick = [](Run& run, int f) {
            if (f != 16)
                return;
            for (const char* n : {"orc1", "orc2", "orc3"})
                if (walker* o = run.actor(n))
                    o->set_guard_hold_post(false);
        };
        // After the wish the seat drives the next hero into the pack.
        sc.keys = [](Run& run, int f) {
            if (f < 24)
                return special_at(f, 12);
            walker* const me = seat_seen(run);
            return fight_keys(me,
                              nearest_seen(run, me, {"orc1", "orc2", "orc3"}));
        };
        sc.tells = {
            {"the faerie pops",
             [](Run& run) {
                 walker* h = run.hero_seen();
                 return h == nullptr || h->dead();
             }},
            {"the fallen soldier stands again",
             [](Run& run) {
                 return count_living(run.display->world(), FAMILY_SOLDIER, 0) >
                        static_cast<int>(run.mark("soldiers"));
             }},
            {"the wounded heroes are healed",
             [](Run& run) {
                 walker* a = run.actor_seen("hero1");
                 walker* b = run.actor_seen("hero2");
                 return a != nullptr && b != nullptr &&
                        hp_of(a) >= 0.95f * a->stats()->max_hitpoints() &&
                        hp_of(b) >= 0.95f * b->stats()->max_hitpoints();
             }},
            {"the healed party fights the orcs",
             [](Run& run) {
                 return run.frame >= 24 && (hurt_since_staging(run, "orc1") ||
                        hurt_since_staging(run, "orc2") ||
                        hurt_since_staging(run, "orc3"));
             }},
        };
        sc.still_tell = 2;
        sc.still_offset = 2;
        all.push_back(sc);
    }

    // ORC CAPTAIN at level 1 (a freshly promoted orc): HOWL (slot 1) and
    // EAT CORPSE (slot 2) are both open from level 1. The howl freezes the
    // elves charging him (an elf's constitution is low, so a level-1 howl
    // still stuns it), he steps onto the body at his feet, and EAT CORPSE
    // heals him; the elves thaw and fight on. The scenes hold no "next
    // special" key, so once the captain stands on the corpse the scene puts
    // the seat's hero on slot 2, as a press of that key would. His pool is
    // 25 and the howl spends all of it; a level-1 captain wins back one
    // point in about seven ticks, so the scene refills his pool when he
    // reaches the corpse (the meal costs 20).
    {
        Scene sc;
        sc.name = "captain_howl_eat";
        sc.family = FAMILY_BIG_ORC;
        sc.slot = 1;
        sc.level = 1;
        sc.stage = [](Run& run) {
            walker* captain = run.hero();
            captain->stats()->set_hitpoints(0.5f * captain->stats()->max_hitpoints());
            live_game::stop_hp_regen(captain);
            add_corpse(run, FAMILY_SOLDIER, 1, 10, 4);
            walker* stain = nearest_stain(run.server->world(), *captain);
            ASSERT_NE(nullptr, stain) << "the fallen soldier leaves a corpse";
            run.marks["stain"] = static_cast<float>(stain->entity_id());
            run.marks["stain.cx"] =
                static_cast<float>(stain->xpos() + stain->sizex() / 2);
            // On the grass: the cobbles below the captain are a wall top.
            add_actor(run, "foe1", FAMILY_ELF, 1, 84, -10)->set_act_type(ACT_GUARD);
            add_actor(run, "foe2", FAMILY_ELF, 1, 100, 12)->set_act_type(ACT_GUARD);
            add_actor(run, "foe3", FAMILY_ELF, 1, -80, -6)->set_act_type(ACT_GUARD);
            for (const char* n : {"foe1", "foe2", "foe3"})
                mark_hp(run, n);
        };
        sc.before_tick = [](Run& run, int) {
            walker* const captain = run.hero();
            if (captain == nullptr)
                return;
            if (run.marks.count("arrived") != 0 &&
                run.marks.count("slot2") == 0)
            {
                captain->set_current_special(2);
                fill_mana(captain, 0.0f);
                run.marks["slot2"] = 1.0f;
            }
            // The slot the captain is on when the corpse goes.
            walker* stain = run.on_server(
                static_cast<std::uint32_t>(run.mark("stain")));
            if (run.marks.count("meal.slot") == 0 &&
                (stain == nullptr || stain->dead()))
                run.marks["meal.slot"] =
                    static_cast<float>(captain->current_special());
        };
        sc.keys = [](Run& run, int f) {
            walker* const captain = run.hero_seen();
            const float near =
                gap(captain, nearest_seen(run, captain, {"foe1", "foe2", "foe3"}));
            Keys k;
            // One-frame taps: the pool pays for one cast, and a held second
            // frame would only put "NOT ENOUGH MP" on the screen.
            k.special = cue(run, "howl", f, f >= 8 && near <= 36.0f, 40) &&
                        f == cued(run, "howl");
            const int howl = cued(run, "howl");
            if (f < howl + 4 || captain == nullptr)
                return k;
            if (run.marks.count("arrived") == 0)
            {
                const int cx = captain->xpos() + captain->sizex() / 2;
                if (static_cast<float>(cx) < run.mark("stain.cx") - 1.0f)
                {
                    k.right = true;
                    return k;
                }
                run.marks["arrived"] = static_cast<float>(f);
                run.marks["captain.hp"] = captain->stats()->hitpoints();
            }
            const int arrived = static_cast<int>(run.mark("arrived"));
            if (f < arrived + 8)
            {
                Keys eat;
                eat.special = f == arrived + 2;
                return eat;
            }
            return fight_keys(captain,
                              nearest_seen(run, captain, {"foe1", "foe2", "foe3"}));
        };
        sc.tells = {
            {"the captain is level 1",
             [](Run& run) {
                 walker* c = run.hero_seen();
                 return run.frame == 0 && c != nullptr &&
                        c->stats()->level() == 1;
             }},
            {"the howl freezes a charging elf",
             [](Run& run) {
                 if (run.frame <= cued(run, "howl"))
                     return false;
                 for (const char* n : {"foe1", "foe2", "foe3"})
                     if (walker* o = run.actor_seen(n))
                         if (o->stats()->frozen_delay() > 0)
                             return true;
                 return false;
             }},
            {"the captain eats the corpse",
             [](Run& run) {
                 if (run.marks.count("arrived") == 0)
                     return false;
                 walker* stain = run.on_server(
                     static_cast<std::uint32_t>(run.mark("stain")));
                 return (stain == nullptr || stain->dead()) &&
                        hp_of(run.hero_seen()) > run.mark("captain.hp");
             }},
            {"the meal came from slot 2, EAT CORPSE, unshifted",
             [](Run& run) { return run.mark("meal.slot") == 2.0f; }},
            {"the elves thaw and the fight goes on",
             [](Run& run) {
                 return run.marks.count("arrived") != 0 &&
                        run.frame > static_cast<int>(run.mark("arrived")) + 8 &&
                        (hurt_since_staging(run, "foe1") ||
                         hurt_since_staging(run, "foe2") ||
                         hurt_since_staging(run, "foe3"));
             }},
        };
        sc.still_tell = 2;
        sc.still_offset = 3;
        all.push_back(sc);
    }

    // ORC CAPTAIN: HOOK BLADE (slot 3) spirals out from the captain like a
    // boomerang; the first soldier it touches slides in to his feet over
    // several frames, frozen on the hook. Then Shift: KNIFE FAN at the two
    // that close in.
    {
        Scene sc;
        sc.name = "captain_hook_fan";
        sc.family = FAMILY_BIG_ORC;
        sc.slot = 3;
        sc.frames = 120;
        sc.stage = [](Run& run) {
            // The first soldier stands near the spiral's rim (60 px), so
            // the blade is well out when it touches him and the reel is long
            // enough to watch; two more come on from further out, on the
            // grass.
            walker* first = add_actor(run, "foe1", FAMILY_SOLDIER, 1, 56, 0);
            first->set_act_type(ACT_GUARD);
            first->set_guard_hold_post(true);
            add_actor(run, "foe2", FAMILY_SOLDIER, 1, 120, -30)->set_act_type(ACT_GUARD);
            add_actor(run, "foe3", FAMILY_SOLDIER, 1, 126, 12)->set_act_type(ACT_GUARD);
            for (const char* n : {"foe1", "foe2", "foe3"})
                mark_hp(run, n);
        };
        // How far the blade gets from the captain, and every frame a frozen
        // soldier moves (the reel), on the server.
        sc.before_tick = [fx_hook](Run& run, int) {
            walker* const me = run.hero();
            for (const auto* list : {&run.server->world().oblist,
                                     &run.server->world().fxlist,
                                     &run.server->world().weaplist})
                for (const auto& up : *list)
                    if (up && !up->dead() && up->query_order() == Order::FX &&
                        up->family() == fx_hook && up->owner() == me)
                        run.marks["blade.reach"] =
                            std::max(run.mark("blade.reach"), gap(me, up.get()));
            for (const char* n : {"foe1", "foe2", "foe3"})
            {
                walker* const o = run.actor(n);
                if (o == nullptr || o->dead())
                    continue;
                const std::string at = std::string(n) + ".at";
                const float here = static_cast<float>(o->xpos() * 1000 + o->ypos());
                if (run.marks.count(at) != 0 && here != run.mark(at) &&
                    o->stats()->frozen_delay() > 0)
                {
                    run.marks[std::string(n) + ".slid"] += 1.0f;
                    run.marks["reeled"] = 1.0f;
                }
                run.marks[at] = here;
            }
        };
        // He stands while the blade is out; once a soldier is on the hook
        // he fights it, and when the other two close in he turns to them and
        // throws the fan.
        sc.keys = [](Run& run, int f) {
            walker* const me = run.hero_seen();
            walker* const next = nearest_seen(run, me, {"foe2", "foe3"});
            Keys k = special_at(f, 10);
            if (f >= 24 && (run.marks.count("reeled") != 0 || f >= 70))
                k = fight_keys(me, run.actor_seen("foe1"));
            if (run.marks.count("aim") == 0 && f >= 40 &&
                (gap(me, next) <= 56.0f || f >= 90))
                run.marks["aim"] = static_cast<float>(f);
            if (run.marks.count("aim") != 0)
            {
                const int aim = static_cast<int>(run.mark("aim"));
                if (f < aim + 2)
                    k = fight_keys(me, next, 0); // turn to them, no swing
                else if (f < aim + 6)
                {
                    k = special_at(f, aim + 2, true);
                    run.marks["fan"] = static_cast<float>(aim + 2);
                }
                else
                    k = fight_keys(me, nearest_seen(run, me, {"foe1", "foe2", "foe3"}));
            }
            return k;
        };
        sc.tells = {
            {"the hook blade spirals out from the captain (40 px and more)",
             [](Run& run) { return run.mark("blade.reach") >= 40.0f; }},
            {"a snagged soldier slides in over several frames, frozen",
             [](Run& run) {
                 for (const char* n : {"foe1", "foe2", "foe3"})
                     if (run.mark(std::string(n) + ".slid") >= 3.0f)
                         return gap(run.hero_seen(), run.actor_seen(n)) <= 28.0f;
                 return false;
             }},
            // Point-blank knives can land the tick they are thrown, so the
            // fan shows as three in flight or as a knife and a fresh wound on
            // a soldier the hook did not take.
            {"the knife fan flies and hits",
             [weap_knife](Run& run) {
                 const int fan = cued(run, "fan");
                 if (run.frame < fan || run.frame > fan + 8)
                     return false;
                 const int knives = count_of(run.display->world(), Order::Weapon,
                                             weap_knife, run.hero_id);
                 return knives >= 3 ||
                        (knives >= 1 && (hurt_since_staging(run, "foe2") ||
                                         hurt_since_staging(run, "foe3")));
             }},
        };
        // The still: the blade near its rim, well out from the captain.
        sc.still_tell = 0;
        sc.still_offset = 6;
        all.push_back(sc);
    }

    // ORC CAPTAIN: WAR BANNER (slot 4) on a corpse, then Shift: WARBAND
    // from the edge. Then he walks to a second body and plants again: the
    // first banner is struck quietly and the grunts turn to the new one.
    {
        Scene sc;
        sc.name = "captain_banner_warband";
        sc.family = FAMILY_BIG_ORC;
        sc.slot = 4;
        // Room for the grunts' turn, measured 2026-11-01: they close on the
        // new banner at frame 165 (106 before Gameplay Fixes X). Two grunts
        // stand in a column at x 432 from frame 59. Before X every blocked
        // act of the lower one shoved the one above twice: once from its
        // step up, which met it, and once more from its sideways step east,
        // which met no body and was stopped by the ground. X keeps only the
        // first, the draws and queues go another way, and the column comes
        // apart later. Every other tell lands by frame 66, as before. A tell
        // must show for 20 frames, so 165 + 20 <= 190 leaves 5 frames spare.
        sc.frames = 190;
        sc.stage = [](Run& run) {
            // A held tap calls the warband on both of its frames (two grunts
            // each at level 10, under the cap of six): mana for the banner,
            // both warband frames and the second banner, and one banner's
            // price over (3 x 80 + 2 x 120), so the plant's held second frame
            // is the quiet "no corpse" refusal and not a price one, whether
            // WARBAND costs 80 or 120.
            fill_mana(run.hero(), 480.0f);
            add_corpse(run, FAMILY_SOLDIER, 1, 6, 6);
            add_corpse(run, FAMILY_SOLDIER, 1, -64, 2);
            walker* captain = run.hero();
            walker* second = nullptr;
            for (const auto& up : run.server->world().fxlist)
            {
                walker* const ob = up.get();
                if (ob != nullptr && !ob->dead() &&
                    ob->query_order() == Order::Treasure &&
                    ob->family() == family_of(Order::Treasure, "core:stain") &&
                    ob->xpos() < captain->xpos() - 30)
                    second = ob;
            }
            ASSERT_NE(nullptr, second) << "the second body leaves a corpse";
            run.marks["stain2.cx"] =
                static_cast<float>(second->xpos() + second->sizex() / 2);
            run.marks["stain2.cy"] =
                static_cast<float>(second->ypos() + second->sizey() / 2);
            add_actor(run, "ally1", FAMILY_ORC, 0, -24, 26)->set_act_type(ACT_GUARD);
            add_actor(run, "ally2", FAMILY_SOLDIER, 0, -16, -30)->set_act_type(ACT_GUARD);
            // No foe on the field: the grunts have nothing to chase, so the
            // scene shows where they go, to the first banner and then to the
            // second.
        };
        // The grunts' distance to the second banner when it goes up, and the
        // closest any of them gets afterwards.
        sc.before_tick = [weap_banner](Run& run, int) {
            if (run.marks.count("plant2") == 0)
                return;
            walker* banner = nullptr;
            for (const auto& up : run.server->world().weaplist)
                if (up && !up->dead() && up->family() == weap_banner &&
                    up->owner() == run.hero())
                    banner = up.get();
            if (banner == nullptr)
                return;
            float nearest = 1.0e9f;
            for (const auto& up : run.server->world().oblist)
                if (up && !up->dead() && up->query_order() == Order::Living &&
                    up->family() == FAMILY_ORC && up->owner() == run.hero())
                    nearest = std::min(nearest, gap(up.get(), banner));
            if (run.marks.count("grunts.at_plant2") == 0)
                run.marks["grunts.at_plant2"] = nearest;
            run.marks["grunts.nearest2"] = nearest;
        };
        sc.keys = [](Run& run, int f) {
            Keys k = special_at(f, 12);
            const Keys band = special_at(f, 30, true);
            k.special = k.special || band.special;
            k.shift = band.shift;
            walker* const captain = run.hero_seen();
            if (f < 44 || captain == nullptr)
                return k;
            if (run.marks.count("on2") == 0)
            {
                const float cx = static_cast<float>(captain->xpos() + captain->sizex() / 2);
                const float cy = static_cast<float>(captain->ypos() + captain->sizey() / 2);
                const float dx = run.mark("stain2.cx") - cx;
                const float dy = run.mark("stain2.cy") - cy;
                if (std::abs(dx) > 2.0f || std::abs(dy) > 2.0f)
                {
                    k.left = dx < -2.0f;
                    k.right = dx > 2.0f;
                    k.up = dy < -2.0f;
                    k.down = dy > 2.0f;
                    return k;
                }
                run.marks["on2"] = static_cast<float>(f);
            }
            const int on2 = static_cast<int>(run.mark("on2"));
            if (f == on2 + 2)
                run.marks["plant2"] = static_cast<float>(f);
            return special_at(f, on2 + 2);
        };
        sc.tells = {
            {"the banner stands on the corpse",
             [weap_banner](Run& run) {
                 return count_of(run.display->world(), Order::Weapon,
                                 weap_banner, run.hero_id) > 0;
             }},
            {"the warband answers",
             [](Run& run) {
                 int grunts = 0;
                 for (const auto& up : run.display->world().oblist)
                     if (up && !up->dead() && up->query_order() == Order::Living &&
                         up->family() == FAMILY_ORC && up->owner() != nullptr &&
                         up->owner()->entity_id() == run.hero_id)
                         ++grunts;
                 return grunts >= 2;
             }},
            {"a second plant strikes the first banner: one stands, on the second body",
             [weap_banner](Run& run) {
                 if (run.frame <= cued(run, "plant2"))
                     return false;
                 walker* banner = nullptr;
                 int banners = 0;
                 for (const auto& up : run.display->world().weaplist)
                     if (up && !up->dead() && up->family() == weap_banner &&
                         up->owner() != nullptr &&
                         up->owner()->entity_id() == run.hero_id)
                     {
                         banner = up.get();
                         ++banners;
                     }
                 return banners == 1 &&
                        std::abs(banner->xpos() + banner->sizex() / 2 -
                                 run.mark("stain2.cx")) <= 12.0f;
             }},
            {"the grunts turn to the new banner",
             [](Run& run) {
                 return run.marks.count("grunts.at_plant2") != 0 &&
                        run.mark("grunts.nearest2") <=
                            run.mark("grunts.at_plant2") - 16.0f;
             }},
        };
        sc.still_tell = 2;
        sc.still_offset = 1;
        all.push_back(sc);
    }

    // SKELETON: DIG IN. It sinks (four frames) and hides; an orc going for
    // the soldier on the far side walks straight across the spot, and the
    // skeleton pops up under it swinging.
    {
        Scene sc;
        sc.name = "skeleton_dig_in";
        sc.family = FAMILY_SKELETON;
        sc.slot = 2;
        sc.frames = 98; // the fight is over by frame ~72
        sc.stage = [](Run& run) {
            walker* skel = run.hero();
            skel->set_ani_type(ANI_WALK);
            skel->set_cycle(0);
            walker* ally = add_actor(run, "ally", FAMILY_SOLDIER, 0, -44, 0);
            ally->set_act_type(ACT_GUARD);
            ally->set_guard_hold_post(true);
            walker* orc = add_actor(run, "orc", FAMILY_ORC, 1, 80, 0);
            orc->set_act_type(ACT_GUARD);
            orc->set_guard_hold_post(true);
            mark_hp(run, "orc");
        };
        // The orc waits until the skeleton is under, then goes for the ally.
        sc.before_tick = [](Run& run, int f) {
            walker* h = run.hero();
            walker* orc = run.actor("orc");
            walker* ally = run.actor("ally");
            if (h != nullptr && h->hidden())
                run.marks["hid"] = 1.0f;
            if (orc == nullptr || ally == nullptr || f < 14)
                return;
            orc->set_guard_hold_post(false);
            if (h != nullptr && h->hidden())
                orc->set_foe(ally);
        };
        sc.keys = [](Run& run, int f) {
            if (run.marks.count("popped") == 0)
                return special_at(f, 10);
            walker* const me = run.hero_seen();
            return fight_keys(me, run.actor_seen("orc"), 24, 28);
        };
        sc.tells = {
            {"the skeleton is under the ground",
             [](Run& run) {
                 walker* h = run.hero_seen();
                 return h != nullptr && h->hidden();
             }},
            {"it pops up under the orc walking over it",
             [](Run& run) {
                 walker* h = run.hero_seen();
                 const bool up = run.frame > 16 && h != nullptr && !h->hidden() &&
                                 run.marks.count("hid") != 0 &&
                                 gap(h, run.actor_seen("orc")) <= 24.0f;
                 if (up)
                     run.marks["popped"] = 1.0f;
                 return up;
             }},
            {"and fights it",
             [](Run& run) {
                 return run.marks.count("popped") != 0 &&
                        hurt_since_staging(run, "orc");
             }},
            // On its own pool: one mana every four buried ticks.
            {"buried, it pays mana (its bar steps down)",
             [](Run& run) {
                 walker* h = run.hero_seen();
                 if (h == nullptr || !h->hidden())
                     return false;
                 if (run.marks.count("mp.hid") == 0)
                     run.marks["mp.hid"] = h->stats()->magicpoints();
                 return h->stats()->magicpoints() <= run.mark("mp.hid") - 2.0f;
             }},
            {"the seat's HUD counts the burrow down (DIG IN: Ns)",
             [](Run& run) {
                 const std::string t = seat_timer_text(run);
                 return t.rfind("DIG IN: ", 0) == 0 && hud_drew(t);
             }},
        };
        sc.still_tell = 1;
        sc.still_offset = 2;
        all.push_back(sc);
    }

    // SKELETON: BONE WALL across the foes' path, then Shift: BONE STORM.
    {
        Scene sc;
        sc.name = "skeleton_wall_storm";
        sc.family = FAMILY_SKELETON;
        sc.slot = 3;
        sc.frames = 98; // the last soldier falls by frame ~76
        sc.stage = [](Run& run) {
            walker* skel = run.hero();
            skel->set_ani_type(ANI_WALK);
            skel->set_cycle(0);
            add_actor(run, "foe1", FAMILY_SOLDIER, 1, 90, -12)->set_act_type(ACT_GUARD);
            add_actor(run, "foe2", FAMILY_SOLDIER, 1, 96, 16)->set_act_type(ACT_GUARD);
        };
        // The soldiers walk into the wall; the storm meets them there, and
        // the skeleton fights on with thrown bones.
        sc.keys = [](Run& run, int f) {
            Keys k;
            k.right = during(f, 4, 6);
            const Keys wall = special_at(f, 10);
            const Keys storm = special_at(f, 40, true);
            k.special = wall.special || storm.special;
            k.shift = storm.shift;
            if (f >= 50)
            {
                walker* const me = run.hero_seen();
                k = fight_keys(me, nearest_seen(run, me, {"foe1", "foe2"}), 24, 28);
            }
            return k;
        };
        sc.tells = {
            {"a bone wall stands",
             [weap_bone_wall](Run& run) {
                 return count_of(run.display->world(), Order::Weapon,
                                 weap_bone_wall, run.hero_id) >= 2;
             }},
            {"the storm shatters the wall into bones",
             [weap_bone_wall, weap_bone](Run& run) {
                 return run.frame >= 40 &&
                        count_of(run.display->world(), Order::Weapon,
                                 weap_bone_wall, run.hero_id) == 0 &&
                        count_of(run.display->world(), Order::Weapon,
                                 weap_bone) >= 12;
             }},
            // The bursts are the walls' own: on the storm's frame there are
            // eight bones for every wall that stood, and none more (a burst
            // from the skeleton would add eight).
            {"eight bones a wall and none from the skeleton",
             [weap_bone_wall, weap_bone](Run& run) {
                 GameWorld& w = run.display->world();
                 const int walls = count_of(w, Order::Weapon, weap_bone_wall,
                                            run.hero_id);
                 const int bones = count_of(w, Order::Weapon, weap_bone);
                 if (walls > 0)
                 {
                     run.marks["walls"] = static_cast<float>(walls);
                     return false;
                 }
                 if (run.frame < 40 || run.marks.count("storm.bones") != 0)
                     return false;
                 run.marks["storm.bones"] = static_cast<float>(bones);
                 printf("  skeleton_wall_storm frame %d: %d walls stood, %d bones\n",
                        run.frame, static_cast<int>(run.mark("walls")), bones);
                 return bones > 0 &&
                        bones <= 8 * static_cast<int>(run.mark("walls")) &&
                        bones >= 8 * static_cast<int>(run.mark("walls")) - 6;
             }},
        };
        sc.still_tell = 1;
        sc.still_offset = 2;
        all.push_back(sc);
    }

    // SKELETON: REASSEMBLE, then Shift: LEGION, on the skeleton's own pool.
    // The HUD counts the ward down from 30 s; LEGION's price leaves the ward
    // only what the rest of the mana buys, and the clock says so. A soldier
    // cuts the skeleton down and it stands back up at a quarter health; it
    // fells him, then a second soldier, who guards a post on the skeleton's
    // other side until the first is down. LEGION raises half its kills on a
    // coin: the first rises as a skeleton on its side, the second stays
    // where it fell (both coins are pinned, kLegionFirstCoinPin and
    // kLegionSecondCoinPin).
    {
        Scene sc;
        sc.name = "skeleton_reassemble_legion";
        sc.family = FAMILY_SKELETON;
        sc.slot = 4;
        sc.frames = 130;
        sc.stage = [](Run& run) {
            walker* skel = run.hero();
            skel->set_ani_type(ANI_WALK);
            skel->set_cycle(0);
            skel->stats()->set_hitpoints(3.0f);
            live_game::stop_hp_regen(skel);
            add_actor(run, "foe", FAMILY_SOLDIER, 1, 70, 0);
            // The second soldier keeps its post on the far side of the
            // hero from the first. The first one's skeleton rises where he
            // fell, so the hero walks to the second with the risen skeleton
            // behind it, not in between: the hero lands the kill, and a
            // soldier bot's CHARGE, which lands on whatever living foe
            // stands in the path the rush runs, finds the hero first.
            walker* second = add_actor(run, "foe2", FAMILY_SOLDIER, 1, -120, 24);
            second->set_act_type(ACT_GUARD);
            second->set_guard_hold_post(true);
            run.marks["skeletons"] = static_cast<float>(
                count_living(run.server->world(), FAMILY_SKELETON, 0));
            printf("  skeleton_reassemble_legion: mp %.1f of %.1f\n",
                   skel->stats()->magicpoints(), skel->stats()->max_magicpoints());
        };
        sc.keys = [](Run& run, int f) {
            const Keys ward = special_at(f, 6);
            // A quick one-frame tap: LEGION leaves too little mana for a
            // held second frame, whose price refusal would sit on screen.
            Keys legion;
            legion.special = f == 18;
            legion.shift = legion.special;
            Keys k;
            k.special = ward.special || legion.special;
            k.shift = legion.shift;
            if (run.marks.count("rose") != 0)
            {
                walker* h = run.hero_seen();
                walker* o = nearest_seen(run, h, {"foe", "foe2"});
                if (h && o && !o->dead())
                {
                    const int dx = o->xpos() - h->xpos();
                    const int dy = o->ypos() - h->ypos();
                    const int frame_in = f - static_cast<int>(run.mark("rose"));
                    if (frame_in >= 4 && (frame_in < 6 || std::abs(dx) > 20 ||
                                          std::abs(dy) > 20))
                    {
                        k.right = dx > 8;
                        k.left = dx < -8;
                        k.down = dy > 8;
                        k.up = dy < -8;
                    }
                    k.fire = frame_in >= 6;
                }
            }
            return k;
        };
        // The stand-up is the ward spent by a death: the bit goes and the
        // health jumps to a quarter (a ward that fades leaves both alone).
        // The second soldier comes on once the first is down, with the
        // second coin pinned. Each soldier's fall is read one tick later:
        // whether the skeleton's own tally gained the kill, whether a
        // skeleton rose on its side, and whether the body still lies there.
        sc.before_tick = [](Run& run, int f) {
            walker* h = run.hero();
            if (h == nullptr)
                return;
            if ((h->kit_state() & KIT_WARD) != 0)
                run.marks["warded"] = 1.0f;
            else if (run.marks.count("warded") != 0 &&
                     run.marks.count("rose") == 0 && !h->dead() &&
                     hp_of(h) >= 0.25f * h->stats()->max_hitpoints() - 1.0f)
            {
                run.marks["rose"] = static_cast<float>(f);
                // The first coin is pinned here, as the second is when its
                // soldier is let go: what the soldiers' own bot gates draw
                // before this moment is theirs, not LEGION's. This pin came
                // with the soldier's payoff gates (before them the first kill
                // rode the scene's authority pin): the gates draw differently,
                // and without it the first kill stayed down. With the second
                // soldier posted west it is still needed: the first kill
                // rises without it, but the second soldier then falls on a
                // later frame and its coin raises him too.
                run.server->world().rng_.state_ = kLegionFirstCoinPin;
            }
            GameWorld& w = run.server->world();
            const float kills =
                h->myguy != nullptr ? static_cast<float>(h->myguy->scen_kills) : 0.0f;
            const float risen =
                static_cast<float>(count_living(w, FAMILY_SKELETON, 0));
            for (const char* n : {"foe", "foe2"})
            {
                const std::string name = n;
                walker* o = run.actor(name);
                if (o != nullptr && !o->dead())
                {
                    run.marks[name + ".x"] =
                        static_cast<float>(o->xpos() + o->sizex() / 2);
                    run.marks[name + ".y"] =
                        static_cast<float>(o->ypos() + o->sizey() / 2);
                    continue;
                }
                if (run.marks.count(name + ".fell") != 0 ||
                    run.marks.count(name + ".x") == 0)
                    continue;
                run.marks[name + ".fell"] = static_cast<float>(f);
                run.marks[name + ".by_skeleton"] =
                    kills > run.mark("kills") ? 1.0f : 0.0f;
                run.marks[name + ".rose"] =
                    risen > run.mark("risen") ? 1.0f : 0.0f;
                const int stain = family_of(Order::Treasure, "core:stain");
                for (const auto& up : w.fxlist)
                    if (up && !up->dead() && up->query_order() == Order::Treasure &&
                        up->family() == stain &&
                        std::abs(up->xpos() + up->sizex() / 2 -
                                 run.mark(name + ".x")) <= 12.0f &&
                        std::abs(up->ypos() + up->sizey() / 2 -
                                 run.mark(name + ".y")) <= 12.0f)
                        run.marks[name + ".body"] = 1.0f;
                printf("  skeleton_reassemble_legion frame %d: %s fell, "
                       "skeleton's kill %d, rose %d, body left %d\n",
                       f, n, static_cast<int>(run.mark(name + ".by_skeleton")),
                       static_cast<int>(run.mark(name + ".rose")),
                       run.marks.count(name + ".body") != 0 ? 1 : 0);
            }
            run.marks["kills"] = kills;
            run.marks["risen"] = risen;
            walker* first = run.actor("foe");
            if (first == nullptr || first->dead())
                if (walker* second = run.actor("foe2"))
                {
                    second->set_guard_hold_post(false);
                    if (run.marks.count("coin2.pinned") == 0)
                    {
                        w.rng_.state_ = kLegionSecondCoinPin;
                        run.marks["coin2.pinned"] = static_cast<float>(f);
                    }
                }
        };
        sc.tells = {
            {"the seat's HUD reads REASSEMBLE: 30s",
             [](Run&) { return hud_drew("REASSEMBLE: 30s"); }},
            {"after LEGION's price the ward's clock reads what the mana buys",
             [](Run& run) {
                 const std::string t = seat_timer_text(run);
                 if (run.frame < 20 || t.rfind("REASSEMBLE: ", 0) != 0 ||
                     !hud_drew(t))
                     return false;
                 return std::stoi(t.substr(12)) <= 10;
             }},
            {"cut down, the skeleton stands back up at a quarter health",
             [](Run& run) {
                 walker* h = run.hero_seen();
                 return run.marks.count("rose") != 0 && h != nullptr &&
                        !h->dead() && (h->kit_state() & KIT_WARD) == 0;
             }},
            {"its first kill rises as a skeleton on its side",
             [](Run& run) {
                 return run.mark("foe.by_skeleton") == 1.0f &&
                        run.mark("foe.rose") == 1.0f &&
                        count_living(run.display->world(), FAMILY_SKELETON, 0) >
                            static_cast<int>(run.mark("skeletons"));
             }},
            {"its second kill stays down where it fell (the coin)",
             [](Run& run) {
                 return run.mark("foe2.by_skeleton") == 1.0f &&
                        run.mark("foe2.rose") == 0.0f &&
                        run.marks.count("foe2.body") != 0;
             }},
        };
        sc.still_tell = 2;
        sc.still_offset = 6;
        all.push_back(sc);
    }

    // FIRE ELEMENTAL: IMMOLATE, then walk: burning footprints behind it.
    {
        Scene sc;
        sc.name = "elemental_immolate";
        sc.family = FAMILY_FIREELEMENTAL;
        sc.slot = 2;
        sc.frames = 106; // a panning camera costs GIF bytes: stay under 3 MB
        sc.stage = [](Run& run) {
            fill_mana(run.hero(), 400.0f);
            add_actor(run, "orc", FAMILY_ORC, 1, -70, 0);
            mark_hp(run, "orc");
        };
        sc.keys = [](Run&, int f) {
            Keys k = special_at(f, 10);
            k.right = during(f, 18, 40);
            k.down = during(f, 40, 52);
            k.left = during(f, 52, 70);
            // It keeps walking after the kill: the burning trail is the
            // special.
            k.up = during(f, 70, 82);
            k.right = k.right || during(f, 82, 94);
            return k;
        };
        sc.tells = {
            {"embers burn where it walked",
             [fx_ember](Run& run) {
                 return count_of(run.display->world(), Order::FX, fx_ember,
                                 run.hero_id) >= 3;
             }},
            {"the orc is burned",
             [](Run& run) { return hurt_since_staging(run, "orc"); }},
            {"the seat's HUD counts the fire down (IMMOLATE: Ns)",
             [](Run& run) {
                 const std::string t = seat_timer_text(run);
                 return t.rfind("IMMOLATE: ", 0) == 0 && hud_drew(t);
             }},
        };
        sc.still_tell = 1;
        all.push_back(sc);
    }

    // FIRE ELEMENTAL: METEOR RAIN on a pack of orcs.
    {
        Scene sc;
        sc.name = "elemental_meteor_rain";
        sc.family = FAMILY_FIREELEMENTAL;
        sc.slot = 3;
        sc.stage = [](Run& run) {
            fill_mana(run.hero(), 140.0f);
            for (int i = 0; i < 3; ++i)
            {
                const std::string n = "orc" + std::to_string(i + 1);
                add_actor(run, n, FAMILY_ORC, 1, 96 + 14 * i, -20 + 20 * i)
                    ->set_act_type(ACT_GUARD);
                mark_hp(run, n);
            }
        };
        sc.keys = [](Run&, int f) { return special_at(f, 12); };
        sc.tells = {
            {"meteors strike",
             [fx_explosion](Run& run) {
                 return count_of(run.display->world(), Order::FX,
                                 fx_explosion) > 0;
             }},
            {"a meteor hits an orc",
             [fx_explosion](Run& run) {
                 bool hit = false;
                 for (const char* n : {"orc1", "orc2", "orc3"})
                 {
                     walker* o = run.actor_seen(n);
                     const float hp = o == nullptr || o->dead() ? 0.0f : hp_of(o);
                     const std::string key = std::string(n) + ".prev";
                     if (run.frame > 12 && run.marks.count(key) != 0 &&
                         hp < run.mark(key) &&
                         count_of(run.display->world(), Order::FX,
                                  fx_explosion) > 0)
                         hit = true;
                     run.marks[key] = hp;
                 }
                 return hit;
             }},
        };
        all.push_back(sc);
    }

    // FIRE ELEMENTAL: REKINDLE mana into health, then Shift: SUPERNOVA.
    {
        Scene sc;
        sc.name = "elemental_rekindle_supernova";
        sc.family = FAMILY_FIREELEMENTAL;
        sc.slot = 4;
        sc.frames = 100; // the blast is over by frame ~80
        sc.roster_extra = {FAMILY_SOLDIER};
        sc.stage = [](Run& run) {
            walker* el = run.hero();
            fill_mana(el, 100.0f);
            el->stats()->set_hitpoints(0.15f * el->stats()->max_hitpoints());
            live_game::stop_hp_regen(el);
            run.marks["el.hp"] = el->stats()->hitpoints();
            walker* friend_ = run.actor("hero1");
            ASSERT_NE(nullptr, friend_);
            // An ally takes each nova blast at half (the blast's nova mark).
            // He stands at the ring's edge, where only the nearest ring
            // blasts still reach him, so he outlives the elemental (the
            // level ends when the last hero falls).
            friend_->setxy(static_cast<short>(el->xpos() - 128), el->ypos());
            friend_->set_act_type(ACT_GUARD);
            friend_->set_guard_hold_post(true);
            live_game::stop_hp_regen(friend_);
            run.marks["friend.hp"] = hp_of(friend_);
            printf("  supernova: elemental hp %.1f of %.1f, soldier hp %.1f\n",
                   hp_of(el), el->stats()->max_hitpoints(), hp_of(friend_));
            for (int i = 0; i < 3; ++i)
            {
                const std::string n = "orc" + std::to_string(i + 1);
                add_actor(run, n, FAMILY_ORC, 1, 40 + 6 * i, -28 + 18 * i); // on grass
                mark_hp(run, n);
            }
        };
        sc.keys = [](Run&, int f) {
            Keys k = special_at(f, 12);
            const Keys nova = special_at(f, 56, true);
            k.special = k.special || nova.special;
            k.shift = nova.shift;
            return k;
        };
        sc.tells = {
            {"rekindled, the elemental's health rises",
             [](Run& run) {
                 return hp_of(run.hero_seen()) > run.mark("el.hp");
             }},
            {"the supernova spends the elemental",
             [](Run& run) {
                 walker* h = run.hero_seen();
                 return run.frame >= 56 && (h == nullptr || h->dead());
             }},
            {"and burns the orcs",
             [](Run& run) {
                 return run.frame >= 56 &&
                        (hurt_since_staging(run, "orc1") ||
                         hurt_since_staging(run, "orc2") ||
                         hurt_since_staging(run, "orc3"));
             }},
            {"the blast and its ring of eight go off together",
             [fx_explosion](Run& run) {
                 return run.frame >= 56 &&
                        count_of(run.display->world(), Order::FX,
                                 fx_explosion) >= 9;
             }},
            // An ally takes each nova blast at half: the soldier at the
            // ring's edge (382 hp) loses 98, where a blast at full took 218,
            // and lives. Between a fifth and two fifths of his health holds
            // the half and refuses the full blast (218 is 57 %), at every
            // frame of the drop.
            {"the soldier 128 px off, at the ring's edge, takes the blast at "
             "half and lives",
             [](Run& run) {
                 walker* a = run.actor_seen("hero1");
                 if (a != nullptr && run.frame >= 56 && run.frame <= 60)
                     printf("  supernova frame %d: soldier hp %.1f\n", run.frame,
                            hp_of(a));
                 if (run.frame < 56 || a == nullptr || a->dead())
                     return false;
                 const float start = run.mark("friend.hp");
                 const float lost = start - hp_of(a);
                 return lost >= 0.2f * start && lost <= 0.4f * start;
             }},
        };
        sc.still_tell = 3;
        sc.still_offset = 0; // the whole nova, in the elemental's own view
        all.push_back(sc);
    }

    // BOT GHOST: the seat's soldier strikes an enemy ghost; struck, the bot
    // ghost possesses the seat's orc ally beside it, and the orc turns on the
    // soldier. The ghost is held on its mark until it acts.
    {
        Scene sc;
        sc.name = "bot_ghost_possess";
        sc.family = FAMILY_SOLDIER;
        sc.slot = 1;
        sc.stage = [](Run& run) {
            walker* ghost = add_actor(run, "ghost", FAMILY_GHOST, 1, 24, 0);
            set_level(ghost, 10);
            ghost->set_act_type(ACT_GUARD);
            ghost->set_guard_hold_post(true);
            ghost->set_current_special(3);
            walker* orc = add_actor(run, "orc", FAMILY_ORC, 0, 40, 0);
            orc->set_act_type(ACT_GUARD);
            orc->set_guard_hold_post(true);
            run.marks["gx"] = ghost->xpos();
            run.marks["gy"] = ghost->ypos();
            run.marks["hero.hp"] = run.hero()->stats()->hitpoints();
        };
        sc.before_tick = [](Run& run, int f) {
            walker* ghost = run.actor("ghost");
            walker* orc = run.actor("orc");
            if (orc != nullptr && orc->possess_link() != 0)
            {
                orc->set_guard_hold_post(false); // the body goes after the soldier
                if (run.marks.count("rode") == 0)
                {
                    run.marks["rode"] = static_cast<float>(f);
                    run.marks["hero.hp"] = hp_of(run.hero());
                }
            }
            if (ghost != nullptr && run.marks.count("rode") != 0)
                ghost->set_guard_hold_post(false); // cast out, it fights
            if (ghost == nullptr || orc == nullptr || ghost->hidden() ||
                orc->possess_link() != 0)
                return;
            ghost->setxy(static_cast<short>(run.mark("gx")),
                         static_cast<short>(run.mark("gy")));
            // A struck bot tries the slot it is on; the scene keeps it on
            // POSSESS. Its own rule still decides (a foe within reach that it
            // out-levels and that is no hero: the orc, not the soldier).
            ghost->set_current_special(3);
            // An empty pool for the first second gives the scene a lead-in:
            // the bot cannot cast anything until its mana arrives.
            if (f < kBotLeadIn)
                ghost->stats()->set_magicpoints(0.0f);
            else
                fill_mana(ghost, 0.0f);
        };
        // The soldier strikes the ghost until it vanishes into the orc; once
        // the orc has been at him a while, he fights back.
        sc.keys = [](Run& run, int f) {
            walker* const me = run.hero_seen();
            if (run.marks.count("rode") == 0)
            {
                Keys k;
                k.right = f == 12;
                k.fire = f > 14 && f < 100;
                return k;
            }
            if (f < static_cast<int>(run.mark("rode")) + 36)
                return Keys{};
            // The orc first; once it falls and the ghost is cast out, the
            // ghost.
            walker* const ghost = run.actor_seen("ghost");
            walker* const orc = run.actor_seen("orc");
            return fight_keys(me, orc != nullptr && !orc->dead() ? orc : ghost);
        };
        sc.tells = {
            {"struck, the bot ghost rides the orc",
             [](Run& run) {
                 walker* ghost = run.actor_seen("ghost");
                 walker* orc = run.actor_seen("orc");
                 return ghost != nullptr && orc != nullptr && ghost->hidden() &&
                        orc->possess_link() == ghost->entity_id();
             }},
            {"the orc fights for the ghost's side",
             [](Run& run) {
                 walker* orc = run.actor_seen("orc");
                 return orc != nullptr && orc->team_num() == 1;
             }},
            {"the possessed orc's blows land on the soldier",
             [](Run& run) {
                 return run.marks.count("rode") != 0 &&
                        hp_of(run.hero_seen()) < run.mark("hero.hp");
             }},
        };
        sc.still_tell = 0;
        sc.still_offset = 4;
        all.push_back(sc);
    }

    // BOT SKELETON: an enemy skeleton, hurt and outnumbered, digs in. The
    // seat's soldier walks over the spot and it pops up swinging.
    {
        Scene sc;
        sc.name = "bot_skeleton_dig_in";
        sc.family = FAMILY_SOLDIER;
        sc.slot = 1;
        sc.stage = [](Run& run) {
            walker* skel = add_actor(run, "skeleton", FAMILY_SKELETON, 1, 44, 0);
            set_level(skel, 4);
            skel->set_ani_type(ANI_WALK);
            skel->set_cycle(0);
            skel->stats()->set_hitpoints(0.45f * skel->stats()->max_hitpoints());
            live_game::stop_hp_regen(skel);
            skel->set_current_special(2);
            // On the grass behind the seat's soldier, out of the
            // skeleton's reach, so the soldier lands the first blows.
            walker* ally = add_actor(run, "ally", FAMILY_SOLDIER, 0, -22, -14);
            ally->set_act_type(ACT_GUARD);
            ally->set_guard_hold_post(true);
        };
        sc.before_tick = [](Run& run, int) {
            walker* skel = run.actor("skeleton");
            walker* hero = run.hero();
            if (skel == nullptr || hero == nullptr)
                return;
            if (skel->hidden())
                run.marks["hid"] = 1.0f;
            if (run.marks.count("hid") != 0 ||
                (skel->kit_state() & (KIT_CHANNEL | KIT_HIDDEN)) != 0)
                return;
            skel->setxy(static_cast<short>(hero->xpos() + 44), hero->ypos());
            // A bot picks the slot it tries at random; the scene picks DIG IN
            // for it. Its own rule still decides: hurt and outnumbered, it
            // digs in when it is struck.
            skel->set_current_special(2);
            fill_mana(skel, 0.0f);
        };
        sc.keys = [](Run& run, int f) {
            Keys k;
            if (run.marks.count("hid") == 0)
            {
                // The seat's soldier turns to the skeleton and attacks it.
                k.right = f == 14;
                k.fire = f > 16;
                return k;
            }
            if (run.marks.count("popped") != 0)
                return k;
            walker* skel = run.actor("skeleton");
            walker* hero = run.hero();
            if (skel == nullptr || hero == nullptr)
                return k;
            if (!skel->hidden())
            {
                run.marks["popped"] = 1.0f;
                return k;
            }
            if (run.frame < static_cast<int>(run.mark("hid_frame")) + 12)
                return k;
            const int dx = skel->xpos() - hero->xpos();
            const int dy = skel->ypos() - hero->ypos();
            k.right = dx > 2;
            k.left = dx < -2;
            k.down = dy > 2;
            k.up = dy < -2;
            return k;
        };
        sc.tells = {
            {"the bot skeleton digs in",
             [](Run& run) {
                 walker* skel = run.actor_seen("skeleton");
                 const bool hid = skel != nullptr && skel->hidden();
                 if (hid && run.marks.count("hid_frame") == 0)
                     run.marks["hid_frame"] = static_cast<float>(run.frame);
                 return hid;
             }},
            {"it pops up under the seat's soldier",
             [](Run& run) {
                 walker* skel = run.actor_seen("skeleton");
                 return run.marks.count("hid_frame") != 0 && skel != nullptr &&
                        !skel->dead() && !skel->hidden();
             }},
        };
        sc.still_offset = 3;
        all.push_back(sc);
    }

    // BOT FAERIE: an allied faerie swaps a dying soldier out from under the
    // orc that is killing him, across a clear gap, and the orc turns on her.
    // She is level 1, where BLINK / SWAP is her only slot, so the slot a bot
    // rolls is always that one.
    {
        Scene sc;
        sc.name = "bot_faerie_swap";
        sc.family = FAMILY_SOLDIER;
        sc.slot = 1;
        sc.stage = [](Run& run) {
            walker* faerie = add_actor(run, "faerie", FAMILY_FAERIE, 0, -70, -30);
            set_level(faerie, 1);
            faerie->set_act_type(ACT_RANDOM);
            fill_mana(faerie, 60.0f);
            walker* ally = add_actor(run, "ally", FAMILY_SOLDIER, 0, -6, -30);
            ally->stats()->set_hitpoints(0.25f * ally->stats()->max_hitpoints());
            live_game::stop_hp_regen(ally);
            walker* orc = add_actor(run, "orc", FAMILY_ORC, 1, 14, -30);
            ally->set_foe(orc);
            orc->set_foe(ally);
            mark_hp(run, "orc");
            run.marks["fx"] = faerie->xpos();
            run.marks["fy"] = faerie->ypos();
            run.marks["ax"] = ally->xpos();
            run.marks["ay"] = ally->ypos();
        };
        sc.before_tick = [](Run& run, int f) {
            if (run.marks.count("swapped") != 0)
                return;
            walker* faerie = run.actor("faerie");
            walker* ally = run.actor("ally");
            walker* orc = run.actor("orc");
            if (faerie == nullptr || ally == nullptr || orc == nullptr)
                return;
            if (std::abs(ally->xpos() - run.mark("fx")) +
                    std::abs(ally->ypos() - run.mark("fy")) <= 4)
            {
                run.marks["swapped"] = static_cast<float>(f);
                return;
            }
            faerie->setxy(static_cast<short>(run.mark("fx")),
                          static_cast<short>(run.mark("fy")));
            ally->setxy(static_cast<short>(run.mark("ax")),
                        static_cast<short>(run.mark("ay")));
            if (ally->foe() == nullptr)
                ally->set_foe(orc);
            // Held on her mark she stays out of the fight, so every tick is
            // one where a bot weighs its special; whether she casts, and
            // which arm, is still her own rule (a dying ally beset).
            faerie->stats()->clear_command();
            faerie->set_foe(nullptr);
            if (f < kBotLeadIn)
                faerie->stats()->set_magicpoints(0.0f);
            else
                fill_mana(faerie, 0.0f);
        };
        // The orc has the faerie to itself for a while; then the seat's
        // soldier walks up and fights it.
        sc.keys = [](Run& run, int f) {
            if (run.marks.count("swapped") == 0 ||
                f < static_cast<int>(run.mark("swapped")) + 34)
                return Keys{};
            walker* const me = run.hero_seen();
            return fight_keys(me, run.actor_seen("orc"));
        };
        sc.tells = {
            {"the faerie takes the dying soldier's place",
             [](Run& run) {
                 walker* faerie = run.actor_seen("faerie");
                 walker* ally = run.actor_seen("ally");
                 return faerie != nullptr && ally != nullptr && !ally->dead() &&
                        std::abs(ally->xpos() - run.mark("fx")) +
                                std::abs(ally->ypos() - run.mark("fy")) <= 4 &&
                        std::abs(faerie->xpos() - run.mark("ax")) +
                                std::abs(faerie->ypos() - run.mark("ay")) <= 8;
             }},
            {"the orc turns on the faerie",
             [](Run& run) {
                 walker* orc = run.actor("orc");
                 walker* faerie = run.actor("faerie");
                 return run.marks.count("swapped") != 0 && orc != nullptr &&
                        faerie != nullptr && orc->foe() == faerie;
             }},
        };
        sc.still_tell = 0;
        sc.still_offset = 4;
        all.push_back(sc);
    }

    // THE CLASSIC TIMERS, with New Specials OFF on server and display: a
    // level-10 thief casts CLOAK (slot 2) and stands. The countdown cell
    // reads INVISIBLE and counts the cloak down, beside the classic HUD.
    // CLOAK costs 125; a level-10 thief's own pool (256) pays for it, and
    // the scene starts that pool full. The cloak lasts 20 + rand(20) * 10
    // ticks; the pinned generator rolls the same length every run, so the
    // two readings below are the run's own.
    {
        Scene sc;
        sc.name = "classic_cloak_countdown";
        sc.family = FAMILY_THIEF;
        sc.slot = 2;
        sc.new_specials = 0;
        sc.stage = [](Run& run) {
            walker* thief = run.hero();
            fill_mana(thief, 0.0f);
            printf("  classic_cloak_countdown: thief mp %.1f of %.1f\n",
                   thief->stats()->magicpoints(),
                   thief->stats()->max_magicpoints());
        };
        // A one-frame tap: the pool pays for one cloak.
        sc.keys = [](Run&, int f) {
            Keys k;
            k.special = f == 4;
            return k;
        };
        sc.tells = {
            {"the display plays with New Specials off",
             [](Run& run) {
                 return run.frame == 0 &&
                        run.display->world().new_specials == 0;
             }},
            {"the thief is cloaked",
             [](Run& run) {
                 walker* t = run.hero_seen();
                 return t != nullptr && t->invisibility_left() > 0;
             }},
            // This run's roll: 20 + 9 * 10 = 110 ticks, 109 left once the
            // cast frame has ticked, 108 on the frame after (9 s).
            {"the cell reads INVISIBLE: 9s the frame after the cast",
             [](Run& run) {
                 return run.frame == 5 &&
                        seat_timer_text(run) == "INVISIBLE: 9s" &&
                        hud_drew("INVISIBLE: 9s");
             }},
            {"and counts down (INVISIBLE: 4s five seconds on)",
             [](Run& run) {
                 return run.frame == 65 &&
                        seat_timer_text(run) == "INVISIBLE: 4s" &&
                        hud_drew("INVISIBLE: 4s");
             }},
            {"the cloak runs out and the cell clears",
             [](Run& run) {
                 walker* t = run.hero_seen();
                 return run.frame > 65 && t != nullptr &&
                        t->invisibility_left() == 0 &&
                        seat_timer_text(run).empty();
             }},
            {"the thief's own pool is deep enough for the cloak",
             [](Run& run) {
                 walker* t = run.hero();
                 return run.frame == 0 && t != nullptr &&
                        t->stats()->max_magicpoints() >= 125.0f;
             }},
        };
        sc.frames = 133;
        // The still: the cloaked thief and the cell a few seconds on.
        sc.still_tell = 3;
        sc.still_offset = 0;
        all.push_back(sc);
    }

    (void)weap_bone;
    return all;
}

// Plays the scene scenes() names `name` and checks it.
void play_scene_named(const char* name)
{
    for (const Scene& sc : scenes())
        if (std::strcmp(sc.name, name) == 0)
        {
            SCOPED_TRACE(sc.name);
            Result r;
            play(sc, r);
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
            check(sc, r);
            return;
        }
    FAIL() << "scenes() has no scene named " << name;
}

} // namespace new_specials_scenes

#define NEW_SPECIALS_SCENE(test_name, scene)                                   \
    static const bool test_name##_is_listed =                                  \
        (new_specials_listed().push_back({#test_name, scene}), true);          \
    TEST_F(NewSpecials, test_name)                                             \
    {                                                                          \
        new_specials_scenes::play_scene_named(scene);                          \
    }

// In scenes() order (see the namespace comment: the order is part of what
// the tests see).
NEW_SPECIALS_SCENE(a_thief_lays_mines_that_orcs_walk_onto, "thief_mine")
NEW_SPECIALS_SCENE(a_ghost_wail_hops_through_a_pack_and_rests, "ghost_wail")
NEW_SPECIALS_SCENE(a_ghost_siphons_an_orc_one_step_a_tap, "ghost_siphon")
NEW_SPECIALS_SCENE(a_ghost_possesses_a_mage_and_the_seat_drives_it, "ghost_possess")
NEW_SPECIALS_SCENE(a_ghost_phases_out_and_back_among_orcs, "ghost_phase")
NEW_SPECIALS_SCENE(a_faerie_blinks_and_trades_places_with_an_orc, "faerie_blink_swap")
NEW_SPECIALS_SCENE(a_faerie_glimmer_freezes_four_orcs_mid_stride, "faerie_glimmer")
NEW_SPECIALS_SCENE(a_faerie_hastens_a_soldier_and_herself, "faerie_hasten")
NEW_SPECIALS_SCENE(a_faerie_wish_raises_the_fallen_and_heals_the_party, "faerie_wish")
NEW_SPECIALS_SCENE(a_captain_howl_freezes_an_elf_then_eats_a_corpse, "captain_howl_eat")
NEW_SPECIALS_SCENE(a_captain_hook_blade_drags_a_soldier_in_then_a_knife_fan, "captain_hook_fan")
NEW_SPECIALS_SCENE(a_captain_plants_a_banner_and_a_warband_answers, "captain_banner_warband")
NEW_SPECIALS_SCENE(a_skeleton_digs_in_and_pops_up_under_an_orc, "skeleton_dig_in")
NEW_SPECIALS_SCENE(a_skeleton_bone_wall_shatters_into_a_bone_storm, "skeleton_wall_storm")
NEW_SPECIALS_SCENE(a_skeleton_reassembles_and_its_first_kill_rises_as_a_legion, "skeleton_reassemble_legion")
NEW_SPECIALS_SCENE(an_elemental_immolates_and_leaves_burning_embers, "elemental_immolate")
NEW_SPECIALS_SCENE(an_elemental_meteor_rain_strikes_orcs, "elemental_meteor_rain")
NEW_SPECIALS_SCENE(an_elemental_rekindles_then_spends_itself_in_a_supernova, "elemental_rekindle_supernova")
NEW_SPECIALS_SCENE(a_bot_ghost_rides_an_orc_against_a_soldier, "bot_ghost_possess")
NEW_SPECIALS_SCENE(a_bot_skeleton_digs_in_under_the_seats_soldier, "bot_skeleton_dig_in")
NEW_SPECIALS_SCENE(a_bot_faerie_swaps_with_a_dying_soldier, "bot_faerie_swap")
NEW_SPECIALS_SCENE(a_classic_cloak_counts_down_with_the_setting_off, "classic_cloak_countdown")

TEST_F(NewSpecials, the_tests_name_every_scene_in_order)
{
    std::vector<std::string> listed;
    for (const ListedScene& t : new_specials_listed())
        listed.push_back(t.scene);
    std::vector<std::string> defined;
    for (const new_specials_scenes::Scene& sc : new_specials_scenes::scenes())
        defined.push_back(sc.name);
    EXPECT_EQ(defined, listed)
        << "one test per scene of scenes(), declared in its order";
}

// ---------------------------------------------------------------------------
// Gameplay Fixes, off beside on.
//
// Every fix of docs/gameplay-fixes.md, played twice from the same seed and
// the same keys: the setting off (the classic game), then on. The scenes
// reuse the New Specials play-through above; only the setting differs between
// the two runs, and the frame hashes prove the two runs identical until the
// fix parts them. Each side asserts what the player sees and what must never
// show: the classic bug on the off side, the fix on the on side.
// ---------------------------------------------------------------------------

namespace gameplay_fixes_scenes {

using namespace new_specials_scenes;

// A bot scene's bot gets its mana and its dice on this frame, and on no other.
inline constexpr int kCue = 24;
// The two halves must have parted by fork + kGrace (a refusal line, a bar
// and a message all paint on the cast's own frame through the shadow).
inline constexpr int kGrace = 2;
inline constexpr std::uint32_t kNeverDraws = 1u << 20;
// hold_fire: walker::init_fire refuses while busy > 0, before it even starts
// the attack animation, and living::act only counts busy down by one a tick.
inline constexpr float kHeldBusy = 1000.0f;
// blunt: a blow of 6 lands about 5 HP against any armour (a 1 would land 0:
// base 0.5 minus the armour expectation rounds down).
inline constexpr float kBluntDamage = 6.0f;
// h2 and i: the tower a blocked throw swings at must outlast the scene (one
// classic swing a press takes 8 to 12 from it).
inline constexpr float kBlockerHp = 500.0f;
// k1 to k4: a soldier foe throws its knives from about 45 px and never
// closes to melee, so "arrived" is that distance (centre to centre), inside
// every one of the four specials' reach.
inline constexpr float kInReach = 50.0f;

// The off side must latch tell `tell` at least `by` frames before the on
// side does (a3: the staggered foe swings sooner with the setting off).
struct Lead
{
    int tell = 0;
    int by = 3;
};

// One scene and the two runs it must produce: `scene` is played with the
// setting off as "<name>-off" and with it on as "<name>-on"; the same
// staging, the same keys, the same dice. Each side has what it must show and
// what it must never show. `fork` is the first frame the runs may differ
// (the cast, the cue, or in m2 the frame the scene strikes the thief down);
// `still_frame` is the still, the same index on both sides.
struct Pair
{
    const char* name = "";
    Scene scene;
    std::vector<Tell> off_shows;
    std::vector<Tell> on_shows;
    std::vector<Tell> off_never;
    std::vector<Tell> on_never;
    int fork = -1;
    int still_frame = 0;
    std::vector<Lead> leads;
};

// ---- small helpers (the NS ones are reused unchanged) ---------------------

// A message line containing `needle` anywhere (a thief's "'Nyah Nyah!'" is
// prefixed by whatever the thief is called).
bool said(const Run& run, const std::string& needle)
{
    viewscreen* const vs = run.display->viewob[0].get();
    if (vs == nullptr)
        return false;
    for (int i = 0; i < MAX_MESSAGES; ++i)
        if (vs->textlist[i].find(needle) != std::string::npos)
            return true;
    return false;
}

float mp(const walker* w)
{
    return w == nullptr ? -1.0f : w->stats()->magicpoints();
}

bool has_mark(const Run& run, const std::string& name)
{
    return run.marks.count(name) != 0;
}

// A guard that stays on its mark until the scene releases it.
void hold_post(walker* a)
{
    a->set_act_type(ACT_GUARD);
    a->set_guard_hold_post(true);
}

// Held from throwing: a posted guard still acquires a foe and queues
// COMMAND_FIRE every tick (walker::act_guard), and since 2026-08-11 it fires
// in every direction. Call on EVERY held frame in before_tick (busy counts
// down by one a tick); towers are held for the whole scene.
void hold_fire(walker* a)
{
    if (a != nullptr)
        a->set_busy(kHeldBusy);
}

// Off its post and walking: a posted guard never converts by itself unless
// it sights its foe within lineofsight*16 px (soldier: 112), so the walk-in
// is ordered here. No foe is set: act_random re-acquires the nearest foe when
// its own is null, and f's off-side orc must find none while it wears the
// archmage's colour.
void release(walker* a)
{
    if (a == nullptr || a->dead())
        return;
    a->set_guard_hold_post(false);
    a->set_act_type(ACT_RANDOM);
    a->set_busy(0.0f);
}

// A foe that fights a seat hero for more than a few seconds is blunted so
// the run never loses its last team-0 body. After set_level.
void blunt(walker* a)
{
    a->set_damage(kBluntDamage);
}

// A pinned ACT_RANDOM bot is mid-turn on about a third of its ticks, and
// living::act returns before the act-type switch while curdir != enddir or
// a command runs: on the cue frame the bot is made ready to decide, after
// its position pin.
void ready_bot(walker* a)
{
    a->set_enddir(static_cast<char>(a->curdir()));
    a->set_ani_type(ANI_WALK);
    a->set_busy(0.0f);
    a->stats()->clear_command();
}

// The caster's magic stops regenerating, so the MP bar shows only what the
// casts do (the MP twin of live_game::stop_hp_regen). The scene's, not the
// game's.
void stop_mp_regen(walker* a)
{
    a->stats()->set_magic_per_round(0.0f);
    a->stats()->set_max_magic_delay(static_cast<std::int32_t>(kNeverDraws));
    a->stats()->set_current_magic_delay(0);
}

// Sets the authoritative stream so the next draws are `want` under
// `bounds`: the Legion scene's device, used in before_tick of the cast or
// cue frame, where the cast is the tick's first draw (the seat's input is
// processed before the act loop; a bot scene adds its bot first).
void pin_draws(Run& run, const std::vector<std::uint32_t>& bounds,
               const std::vector<std::uint32_t>& want)
{
    run.server->world().rng_.state_ =
        live_game::state_where_draws_are(bounds, want);
}

// The walker's front command is a forced walk (a whirlwind's shove, a
// scare's flee: statistics::force_command).
bool forced_walk(const walker* w)
{
    if (w == nullptr || w->dead() || w->stats() == nullptr ||
        w->stats()->commands.empty())
        return false;
    const command& front = w->stats()->commands.front();
    return front.forced && front.commandtype == COMMAND_WALK;
}

// ONE-frame presses, the only way a Special key goes down in these scenes: a
// two-frame tap runs the held arm on its second frame, which casts a second
// time where the script sets no busy and voices NOT ENOUGH MP when the pool
// has dropped under the price. `special_at` is that two-frame tap and is not
// used here.
Keys press_at(int f, int at, bool shift = false)
{
    Keys k;
    k.special = f == at;
    k.shift = k.special && shift;
    return k;
}

Keys presses_every(int f, int from, int step)
{
    Keys k;
    k.special = f >= from && (f - from) % step == 0;
    return k;
}

// The both-sides pool guard: nothing spent the hero's magic before the cast
// (a facing step into a blocker would be a melee swing that charges the
// weapon's cost; facing is face_delta in stage instead).
Tell pool_guard(int frame, float start)
{
    return {"the pool is untouched the frame before the cast",
            [frame, start](Run& f) { return f.frame == frame && mp(f.hero()) == start; }};
}

// A message tell, the side's own words.
Tell says(const std::string& what)
{
    return {"\"" + what + "\" is on screen",
            [what](Run& f) { return voiced(f, what); }};
}

// The exact pool on one frame (server pool: exact float arithmetic, regen
// stopped; the mirror carries the same value that tick).
Tell pool_at(int frame, float value)
{
    return {"the pool reads " + std::to_string(static_cast<int>(value)) +
                " on frame " + std::to_string(frame),
            [frame, value](Run& f) { return f.frame == frame && mp(f.hero()) == value; }};
}

// A walked-in foe has come inside the special's reach (both sides; centre
// to centre, so a little inside the game's own top-left distance).
Tell arrived(float within)
{
    return {"the foe has walked into reach",
            [within](Run& f) { return gap(f.hero_seen(), f.actor_seen("foe")) <= within; }};
}

// A walked-in foe stands inside `reach` as the game itself measures it for
// the special (walker::distance_to_ob, |dx| + |dy| between the top-left
// corners, the measure find_foes_in_range uses), on or before frame `by`
// (both sides; the mirror's positions).
Tell arrived_by(int reach, int by)
{
    return {"the foe has walked into the special's reach by frame " + std::to_string(by),
            [reach, by](Run& f) {
                const walker* const me = f.hero_seen();
                const walker* const foe = f.actor_seen("foe");
                return f.frame <= by && me != nullptr && foe != nullptr && !me->dead() &&
                       !foe->dead() && me->distance_to_ob(foe) <= reach;
            }};
}

// A bot kept on its mark, foe-less and with nothing queued, so every tick is
// one where it weighs its special; its mana is zero except on the cue frame.
walker* steer_bot(Run& run, const std::string& name)
{
    walker* const bot = run.actor(name);
    if (bot == nullptr || bot->dead())
        return nullptr;
    bot->setxy(static_cast<short>(run.mark(name + ".x")),
               static_cast<short>(run.mark(name + ".y")));
    bot->stats()->clear_command();
    bot->set_foe(nullptr);
    return bot;
}

walker* add_bot(Run& run, const std::string& name, int family,
                unsigned char team, int dx, int dy, int level)
{
    walker* const bot = add_actor(run, name, family, team, dx, dy);
    if (bot == nullptr)
        return nullptr;
    set_level(bot, level);
    bot->set_act_type(ACT_RANDOM);
    bot->stats()->set_magicpoints(0.0f);
    run.marks[name + ".x"] = bot->xpos();
    run.marks[name + ".y"] = bot->ypos();
    return bot;
}

// A foe posted where it stands (it may not walk, and is held from fighting
// while the scene holds its fire).
walker* add_posted(Run& run, const std::string& name, int family,
                   unsigned char team, int dx, int dy, int level)
{
    walker* const a = add_actor(run, name, family, team, dx, dy);
    if (a == nullptr)
        return nullptr;
    if (level > 0)
        set_level(a, level);
    hold_post(a);
    hold_fire(a);
    return a;
}

struct PairResult
{
    Result off;
    Result on;
    int first_diff = -1;
};

void play_side(const Pair& pair, int side, Result& out)
{
    Scene sc = pair.scene;
    const std::string name = std::string(pair.name) + (side ? "-on" : "-off");
    sc.name = name.c_str();   // play() reads it only during the call
    sc.gameplay_fixes = static_cast<short>(side);
    sc.new_specials = 0;      // every Shift is the classic one
    sc.hash_frames = true;
    sc.tells = side ? pair.on_shows : pair.off_shows;
    const std::vector<Tell>& never = side ? pair.on_never : pair.off_never;
    sc.tells.insert(sc.tells.end(), never.begin(), never.end());
    const int still_frame = pair.still_frame;
    sc.tells.push_back({"the still frame",
                        [still_frame](Run& r) { return r.frame == still_frame; }});
    sc.still_tell = static_cast<int>(sc.tells.size()) - 1;
    sc.still_offset = 0;
    effects_reset_for_testing();   // the render clock is global; each run starts it fresh
    // Each half starts as a real mission does: the level's own colours, the
    // bands at rotation 0, palette id 0. A level load reloads the palette
    // but leaves the display world's palette id alone, so a time stop still
    // running when the setting-off run ended (b) would read as blue at the
    // start of the next run. And a run ends in live_game::finish_scene, not
    // through the mission's end, so nothing sets the water and lava bands
    // back: do_cycle turns them in the session's palette, and since the
    // palette is painted only when its id changes (#414) the next run starts
    // with them where the last run left them. The halves then differ from
    // frame 0 or 4 whatever the setting does (measured 2026-11-01: 19 pairs
    // red, each one whose two halves started on different rotations; the
    // four whose halves started on the same rotation passed). The game makes
    // this forced write on every way out of a mission
    // (restore_mission_palette, src/platform/sdl/game_loop.cpp); it sets the
    // palette id back to 0 as well (screen::show_palette_id).
    og::runtime::current_session->myscreen_->show_palette_id(0, /*force=*/true);
    play(sc, out);
}

void check_side(const Pair& pair, int side, const Result& r)
{
    const char* const word = side ? "on" : "off";
    const std::vector<Tell>& shows = side ? pair.on_shows : pair.off_shows;
    const std::vector<Tell>& never = side ? pair.on_never : pair.off_never;
    EXPECT_EQ(pair.scene.frames, r.frames_run)
        << "the level must stay open for the whole run (setting " << word << ")";
    EXPECT_EQ(-1, r.blank_frame)
        << "frame " << r.blank_frame << " composed nothing (setting " << word << ")";
    EXPECT_TRUE(r.missing_on_mirror.empty())
        << "staged on the server but not on the mirror at frame 0 (setting "
        << word << "): " << (r.missing_on_mirror.empty() ? "" : r.missing_on_mirror[0]);
    ASSERT_EQ(shows.size() + never.size() + 1, r.tell_frames.size());
    for (std::size_t i = 0; i < shows.size(); ++i)
    {
        EXPECT_GE(r.tell_frames[i], 0)
            << "never seen with the setting " << word << ": " << shows[i].what;
        EXPECT_LE(r.tell_frames[i], pair.scene.frames - 20)
            << "seen too late to show for 20 frames (setting " << word
            << "): " << shows[i].what;
    }
    for (std::size_t i = 0; i < never.size(); ++i)
        EXPECT_EQ(-1, r.tell_frames[shows.size() + i])
            << "seen with the setting " << word << " at frame "
            << r.tell_frames[shows.size() + i] << ", must never: " << never[i].what;
}

void play_pair(const Pair& pair, PairResult& out)
{
    play_side(pair, 0, out.off);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    play_side(pair, 1, out.on);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    check_side(pair, 0, out.off);
    check_side(pair, 1, out.on);

    ASSERT_GE(pair.fork, 0) << "the scene names no fork";
    ASSERT_EQ(out.off.frame_hashes.size(), out.on.frame_hashes.size());
    for (std::size_t f = 0; f < out.off.frame_hashes.size(); ++f)
        if (out.off.frame_hashes[f] != out.on.frame_hashes[f])
        {
            out.first_diff = static_cast<int>(f);
            break;
        }
    EXPECT_NE(-1, out.first_diff) << "the setting changed nothing on screen";
    EXPECT_GE(out.first_diff, pair.fork)
        << "the halves differ at frame " << out.first_diff
        << ", before the fix could part them (fork " << pair.fork << ")";
    EXPECT_LE(out.first_diff, pair.fork + kGrace)
        << "the fix had not parted the halves by frame " << pair.fork + kGrace;

    for (const Lead& lead : pair.leads)
    {
        const int off_at = out.off.tell_frames[static_cast<std::size_t>(lead.tell)];
        const int on_at = out.on.tell_frames[static_cast<std::size_t>(lead.tell)];
        printf("gameplay fixes scene %s: lead of \"%s\": off @%d, on @%d\n",
               pair.name, pair.off_shows[static_cast<std::size_t>(lead.tell)].what.c_str(),
               off_at, on_at);
        EXPECT_LE(off_at + lead.by, on_at)
            << "with the setting off this must come " << lead.by
            << " frames sooner: " << pair.off_shows[static_cast<std::size_t>(lead.tell)].what;
    }

    printf("gameplay fixes scene %s: fork=%d first_diff=%d\n", pair.name,
           pair.fork, out.first_diff);
    fflush(stdout);
}

// ---- the scenes ------------------------------------------------------------

// The 23 scenes, each with both sides' tells, in the order the tests play
// them.
std::vector<Pair> pairs()
{
    std::vector<Pair> all;
    const int fx_boomerang = family_of(Order::FX, "core:boomerang");
    const int fx_explosion = family_of(Order::FX, "core:explosion");
    const int fx_scare = family_of(Order::FX, "core:ghost_scare");
    const int weap_rock = family_of(Order::Weapon, "core:rock");
    const int weap_fireball = family_of(Order::Weapon, "core:fireball");

    // A1. DISARM against a tower two levels up, a LOST roll (the dice are
    // set): the classic game prints "Fighter Disarmed Enemy!" and takes the
    // full 150 for a disarm that did nothing; the fix says DISARM RESISTED
    // and takes 75. Then BOOMERANG (100): the classic fighter cannot pay.
    {
        Pair c;
        c.name = "a1_disarm_resisted";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.slot = 4;
        c.scene.frames = 96;   // the last frame still carries NOT ENOUGH MP on the left
        c.scene.stage = [](Run& run) {
            walker* hero = run.hero();
            hero->stats()->set_magicpoints(200.0f);   // of 250: the bar starts at 48 px
            stop_mp_regen(hero);
            hero->face_delta(0, 1);                   // faces the blocker below; no step, no draw
            walker* tower = add_actor(run, "tower", FAMILY_TOWER1, 1, 0, 15);
            set_level(tower, 12);
            hold_fire(tower);
        };
        c.scene.before_tick = [](Run& run, int f) {
            hold_fire(run.actor("tower"));              // a passive blocker: it never shoots
            if (f == 20)
                pin_draws(run, {10, 12}, {0, 11});      // the fighter rolls 0, the tower 11
            if (f == 40)
                run.hero()->set_current_special(2);     // BOOMERANG (the cycle key is not in Keys)
        };
        // After the disarm the fighter walks left onto the open grass, so the
        // boomerang circles him in plain sight (beside the tower it would hit
        // the tower at once and vanish inside its sprite).
        c.scene.keys = [](Run&, int f) {
            Keys k = f == 44 ? press_at(f, 44) : press_at(f, 20);
            k.left = f >= 26 && f < 38;
            return k;
        };
        c.off_shows = {
            pool_guard(19, 200.0f),
            says("Fighter Disarmed Enemy!"),
            pool_at(20, 50.0f),
            {"so BOOMERANG is NOT ENOUGH MP",
             [](Run& f) { return f.frame >= 44 && voiced(f, "NOT ENOUGH MP"); }},
            // The cast's front probe bumps the tower: the swing's knife
            // takes its 2 MP on the next frame (50 -> 48).
            pool_at(30, 48.0f),
        };
        c.off_never = {
            says("DISARM RESISTED"),
            {"a boomerang flies", [fx_boomerang](Run& f) {
                 return count_of(f.display->world(), Order::FX, fx_boomerang) > 0; }},
        };
        c.on_shows = {
            pool_guard(19, 200.0f),
            says("DISARM RESISTED"),
            pool_at(20, 125.0f),
            // Gameplay Fixes U: the front probe is asked quietly, no swing,
            // so the pool stays at 125 (before the fix the knife took 2).
            pool_at(30, 125.0f),
            {"the boomerang flies", [fx_boomerang](Run& f) {
                 return count_of(f.display->world(), Order::FX, fx_boomerang, f.hero_id) >= 1; }},
        };
        c.on_never = {
            says("Fighter Disarmed"),
            says("NOT ENOUGH MP"),
        };
        c.fork = 20;
        c.still_frame = 62;   // the boomerang circles the fighter on open grass
        all.push_back(c);
    }

    // A2. DISARM with only a friend in front: the classic NO FOE IN RANGE,
    // the fix's NO ONE TO DISARM; free on both sides.
    {
        Pair c;
        c.name = "a2_disarm_nobody";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.slot = 4;
        c.scene.frames = 96;
        c.scene.stage = [](Run& run) {
            walker* hero = run.hero();
            stop_mp_regen(hero);
            hero->face_delta(0, 1);
            walker* ally = add_actor(run, "ally", FAMILY_SOLDIER, 0, 0, 15);
            hold_post(ally);
        };
        c.scene.keys = [](Run&, int f) { return press_at(f, 20); };
        c.off_shows = {
            pool_guard(19, 250.0f),
            says("NO FOE IN RANGE"),
            pool_at(21, 250.0f),
        };
        c.off_never = {
            says("NO ONE TO DISARM"),
            {"the pool drops", [](Run& f) { return mp(f.hero()) < 250.0f; }},
            says("FACE FOE AT CLOSE RANGE"),
        };
        c.on_shows = {
            pool_guard(19, 250.0f),
            says("NO ONE TO DISARM"),
            pool_at(21, 250.0f),
        };
        c.on_never = {
            says("NO FOE IN RANGE"),
            {"the pool drops", [](Run& f) { return mp(f.hero()) < 250.0f; }},
            says("Fighter Disarmed"),
            says("FACE FOE AT CLOSE RANGE"),
        };
        c.fork = 20;
        c.still_frame = 24;
        all.push_back(c);
    }

    // A3. A WON disarm against a soldier two levels up: the classic stagger
    // is negative (-6), which speeds the foe up for the rest of the fight;
    // the fix calls it resisted at half price and the foe keeps its pace.
    {
        Pair c;
        c.name = "a3_disarm_stagger";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.slot = 4;
        c.scene.frames = 120;
        c.scene.stage = [](Run& run) {
            walker* hero = run.hero();
            stop_mp_regen(hero);
            hero->face_delta(0, 1);
            walker* foe = add_posted(run, "foe", FAMILY_SOLDIER, 1, 0, 15, 12);
            blunt(foe);
        };
        c.scene.before_tick = [](Run& run, int f) {
            walker* foe = run.actor("foe");
            if (f < 20)
                hold_fire(foe);
            if (f == 20)
            {
                if (foe != nullptr)
                    foe->set_busy(0.0f);                 // its first swing comes with the cast
                run.marks["hp"] = hp_of(run.hero_seen());
                pin_draws(run, {10, 12}, {9, 0});       // a WON roll: penalty 6 * (10 - 12 + 1)
            }
        };
        c.scene.keys = [](Run&, int f) { return press_at(f, 20); };
        // The foe's swings since the cast, counted on the mirror: each frame
        // its attack animation starts (a swing is two frames of ANI_ATTACK,
        // and the foe stands at least one frame between swings).
        const auto third_swing = [](Run& f) {
            walker* foe = f.actor_seen("foe");
            const bool swinging = foe != nullptr && foe->ani_type() == ANI_ATTACK;
            const bool was = f.mark("swing.prev") != 0.0f;
            f.marks["swing.prev"] = swinging ? 1.0f : 0.0f;
            if (f.frame > 20 && swinging && !was)
                f.marks["swings"] = f.mark("swings") + 1.0f;
            return f.mark("swings") >= 3.0f;
        };
        const auto blows_land = [](Run& f) {
            return has_mark(f, "hp") && hp_of(f.hero_seen()) < f.mark("hp");
        };
        c.off_shows = {
            says("Fighter Disarmed Enemy!"),
            pool_at(20, 100.0f),
            {"the foe's third swing since the cast", third_swing},
            {"the foe's blows land", blows_land},
            pool_guard(19, 250.0f),
            pool_at(30, 98.0f),   // the front probe's swing: the knife's 2 MP
        };
        c.off_never = {says("DISARM RESISTED")};
        c.on_shows = {
            says("DISARM RESISTED"),
            pool_at(20, 175.0f),
            {"the foe's third swing since the cast", third_swing},
            {"the foe's blows land", blows_land},
            pool_guard(19, 250.0f),
            pool_at(30, 175.0f),  // the quiet front probe swings at nothing (fix U)
        };
        c.on_never = {says("Fighter Disarmed")};
        c.leads = {{2, 3}};
        c.fork = 20;
        c.still_frame = 40;
        all.push_back(c);
    }

    // B. An allied mage bot with no foe within 110 px: the classic gate lets
    // every slot through on "no foe within 110 px", so it freezes time at
    // nobody; the fix keeps that rule for TELEPORT only, and FREEZE TIME
    // waits for the allies' fight (three foes at blows with allies, or an
    // ally at blows near death). A soldier walks in from the right, 120 px
    // from the fighter at the cue on both sides and at blows with nobody:
    // the classic time stop turns the screen blue and freezes him mid-stride
    // for the rest of the scene, while with the fix nothing is cast and he
    // walks up and strikes the fighter.
    {
        Pair c;
        c.name = "b_mage_bot_freeze";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.frames = 120;
        c.scene.stage = [](Run& run) {
            // The mage first, so its draws stay the cue tick's first.
            add_bot(run, "mage", FAMILY_MAGE, 0, -70, -30, 7);
            walker* foe = add_posted(run, "foe", FAMILY_SOLDIER, 1, 140, 0, 1);
            blunt(foe);
        };
        c.scene.before_tick = [](Run& run, int f) {
            if (f < 16)
                hold_fire(run.actor("foe"));
            if (f == 16)
                release(run.actor("foe"));
            walker* mage = steer_bot(run, "mage");
            if (mage == nullptr)
                return;
            if (f == kCue)
            {
                // The screen as frame kCue - 1 left it, before any time stop.
                const MeanColour before = viewport_mean(run.display);
                run.marks["warm"] = before.warm;
                run.marks["blue"] = before.blue;
                ready_bot(mage);
                fill_mana(mage, 600.0f);
                run.marks["hp"] = hp_of(run.hero_seen());
                printf("  b_mage_bot_freeze: at the cue the foe is %.0f px from the fighter\n",
                       gap(run.hero(), run.actor("foe")));
                pin_draws(run, {5, 3}, {0, 2});         // special; slot 3 = FREEZE TIME
            }
            else
                mage->stats()->set_magicpoints(0.0f);
        };
        c.scene.keys = [](Run&, int) { return Keys{}; };
        const auto struck = [](Run& f) {
            return has_mark(f, "hp") && hp_of(f.hero_seen()) < f.mark("hp");
        };
        // Read from the pixels, not the palette id: red + green fall to under
        // 0.6 of what the screen showed before the cue, blue keeps 0.9 of it.
        const auto blue_screen = [](Run& f) {
            if (!has_mark(f, "warm") || f.mark("warm") <= 0.0f)
                return false;
            const MeanColour now = viewport_mean(f.display);
            return now.warm < 0.6f * f.mark("warm") &&
                   now.blue >= 0.9f * f.mark("blue");
        };
        c.off_shows = {
            {"the time stop is on (the palette switches to the blue one)",
             [](Run& f) { return f.display->world().current_palette_id == 1; }},
            {"the screen turns blue", blue_screen},
            {"the server banks a time stop",
             [](Run& f) { return f.server->world().enemy_freeze > 0; }},
            {"the foe stands frozen far off at frame 60",
             [](Run& f) { return f.frame == 60 && gap(f.hero_seen(), f.actor_seen("foe")) > 90.0f; }},
        };
        c.off_never = {
            {"the foe strikes the fighter", struck},
        };
        c.on_shows = {
            {"the palette is still its own well after the cue",
             [](Run& f) { return f.frame == 60 && f.display->world().current_palette_id == 0; }},
            {"the foe walks up to the fighter", [](Run& f) {
                 return f.frame <= 90 && gap(f.hero_seen(), f.actor_seen("foe")) <= kInReach; }},
            {"the foe strikes the fighter", [struck](Run& f) { return f.frame <= 100 && struck(f); }},
        };
        c.on_never = {
            {"the blue palette", [](Run& f) { return f.display->world().current_palette_id == 1; }},
            {"the screen turns blue", blue_screen},
            {"a time stop", [](Run& f) { return f.server->world().enemy_freeze > 0; }},
            {"the mage throws a fireball", [weap_fireball](Run& f) {
                 return count_of(f.display->world(), Order::Weapon, weap_fireball,
                                 f.ids.at("mage")) > 0; }},
        };
        c.fork = kCue;
        c.still_frame = 70;
        all.push_back(c);
    }

    // C. An enemy thief bot whose coin says Shift: the classic gate only asks
    // for a foe in charm reach and tries to charm a soldier two levels up
    // (resisted, a free swing at the thief); the fix chooses Shift only for
    // a foe it can take and taunts instead. The thief's posted ally 60 px
    // south is the backing a bot's taunt is cast for with the setting on
    // (staged after the thief, so the pinned draws stay the thief's).
    {
        Pair c;
        c.name = "c_thief_bot_shift";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.frames = 94;   // the messages fade at 96: the hold keeps them
        c.scene.stage = [](Run& run) {
            add_bot(run, "thief", FAMILY_THIEF, 1, 80, 0, 7);
            add_posted(run, "backer", FAMILY_SOLDIER, 1, 80, 60, 1);
            add_posted(run, "soldier", FAMILY_SOLDIER, 0, 120, 0, 9);
        };
        c.scene.before_tick = [](Run& run, int f) {
            hold_fire(run.actor("soldier"));
            hold_fire(run.actor("backer"));
            walker* thief = steer_bot(run, "thief");
            if (thief == nullptr)
                return;
            if (f == kCue)
            {
                ready_bot(thief);
                fill_mana(thief, 100.0f);
                run.marks["thp"] = hp_of(thief);
                // The bot's act: special, slot 3, coin = Shift. Then, on the
                // classic side only, the free swing's damage roll, the
                // thief's hit-response coin and its 1-in-3 (not 0: no nested
                // cast).
                walker* soldier = run.actor("soldier");
                const auto bound = static_cast<std::uint32_t>(
                    std::floor(std::sqrt(soldier->damage())));
                printf("  c_thief_bot_shift: the free swing's damage roll bound %u\n", bound);
                pin_draws(run, {5, 3, 2, bound, 2, 3}, {0, 2, 1, 0, 0, 1});
            }
            else
                thief->stats()->set_magicpoints(0.0f);
        };
        c.scene.keys = [](Run&, int) { return Keys{}; };
        c.off_shows = {
            {"Thief failed to charm!", [](Run& f) { return said(f, "failed to charm"); }},
            {"the soldier's free swing lands on the thief", [](Run& f) {
                 return has_mark(f, "thp") && hp_of(f.actor_seen("thief")) < f.mark("thp"); }},
        };
        c.off_never = {
            {"'Nyah Nyah!'", [](Run& f) { return said(f, "Nyah Nyah"); }},
            {"a second message from a nested cast", [](Run& f) { return said(f, "charmed an opponent"); }},
        };
        c.on_shows = {
            {"the thief taunts", [](Run& f) { return said(f, "Nyah Nyah"); }},
            {"the thief is not struck", [](Run& f) {
                 return f.frame == 28 && has_mark(f, "thp") &&
                        hp_of(f.actor_seen("thief")) == f.mark("thp"); }},
        };
        c.on_never = {
            {"failed to charm", [](Run& f) { return said(f, "failed to charm"); }},
            {"charmed an opponent", [](Run& f) { return said(f, "charmed an opponent"); }},
        };
        c.fork = kCue;
        c.still_frame = 24;   // the message and the struck thief's flash
        all.push_back(c);
    }

    // D. SUMMON ELEMENTAL with no room: the classic game takes its surcharge
    // (half the pool above the price) before it looks for room; the fix only
    // after the elemental stands. The HEARTBURST that follows draws on what
    // is left.
    {
        Pair c;
        c.name = "d_summon_no_room";
        c.scene.family = FAMILY_ARCHMAGE;
        c.scene.slot = 3;
        c.scene.frames = 110;
        c.scene.stage = [](Run& run) {
            walker* hero = run.hero();
            fill_mana(hero, 1000.0f);
            stop_mp_regen(hero);
            const int ring[8][2] = {{-17, -17}, {0, -17}, {17, -17}, {-17, 0},
                                    {17, 0},    {-17, 17}, {0, 17},  {17, 17}};
            for (int i = 0; i < 8; ++i)
                hold_fire(add_actor(run, "ring" + std::to_string(i), FAMILY_TOWER1, 0,
                                    ring[i][0], ring[i][1]));
            hold_fire(add_actor(run, "west", FAMILY_TOWER1, 1, -60, 0));
            hold_fire(add_actor(run, "east", FAMILY_TOWER1, 1, 60, 0));
        };
        c.scene.before_tick = [](Run& run, int f) {
            for (int i = 0; i < 8; ++i)
                hold_fire(run.actor("ring" + std::to_string(i)));
            hold_fire(run.actor("west"));
            hold_fire(run.actor("east"));
            if (f == 46)
                run.hero()->set_current_special(2);     // HEARTBURST
            if (f == 51)
                printf("  d_summon_no_room: pool after the burst %.0f\n", mp(run.hero()));
        };
        c.scene.keys = [](Run&, int f) {
            return f == 50 ? press_at(f, 50) : press_at(f, 20, true);
        };
        const auto bursts = [fx_explosion](Run& f) {
            return count_of(f.display->world(), Order::FX, fx_explosion) > 0;
        };
        c.off_shows = {
            pool_guard(19, 1000.0f),
            says("NO ROOM TO SUMMON"),
            pool_at(21, 750.0f),
            {"the heartburst bursts", bursts},
            pool_at(51, 336.0f),
        };
        c.off_never = {
            {"the pool is still full after the refusal",
             [](Run& f) { return f.frame == 21 && mp(f.hero()) == 1000.0f; }},
        };
        c.on_shows = {
            pool_guard(19, 1000.0f),
            says("NO ROOM TO SUMMON"),
            pool_at(21, 1000.0f),
            {"the heartburst bursts", bursts},
            pool_at(51, 460.0f),
        };
        c.on_never = {
            {"the pool drops before the heartburst",
             [](Run& f) { return f.frame < 50 && mp(f.hero()) < 1000.0f; }},
        };
        c.fork = 20;
        c.still_frame = 26;
        all.push_back(c);
    }

    // E1. TURN UNDEAD with a skeleton 50 px either side of a level-7 cleric:
    // the classic reach (4 per level, 28 px) holds nobody; the fixed 60 px
    // turns both.
    {
        Pair c;
        c.name = "e1_turn_reach";
        c.scene.family = FAMILY_CLERIC;
        c.scene.slot = 2;
        c.scene.level = 7;
        c.scene.frames = 110;
        c.scene.stage = [](Run& run) {
            add_posted(run, "west", FAMILY_SKELETON, 1, -50, 0, 1);
            add_posted(run, "east", FAMILY_SKELETON, 1, 50, 0, 1);
        };
        c.scene.before_tick = [](Run& run, int f) {
            if (f < 24)
            {
                hold_fire(run.actor("west"));
                hold_fire(run.actor("east"));
            }
            if (f == 20)
                run.marks["hp"] = hp_of(run.hero_seen());
            if (f == 24)
            {
                release(run.actor("west"));
                release(run.actor("east"));
            }
        };
        c.scene.keys = [](Run&, int f) { return press_at(f, 20, true); };
        c.off_shows = {
            says("NO FOE IN RANGE"),
            {"both skeletons still stand the frame after", [](Run& f) {
                 return f.frame == 21 && count_living(f.display->world(), FAMILY_SKELETON, 1) == 2; }},
            {"the skeletons reach the cleric", [](Run& f) {
                 return has_mark(f, "hp") && hp_of(f.hero_seen()) < f.mark("hp"); }},
        };
        c.off_never = {
            {"turned", [](Run& f) { return said(f, "turned"); }},
        };
        c.on_shows = {
            {"both skeletons fall", [](Run& f) {
                 return count_living(f.display->world(), FAMILY_SKELETON, 1) == 0; }},
            {"turned 2 undead.", [](Run& f) { return said(f, "turned 2 undead"); }},
        };
        c.on_never = {
            says("NO FOE IN RANGE"),
            {"the cleric is hurt", [](Run& f) {
                 return has_mark(f, "hp") && hp_of(f.hero_seen()) < f.mark("hp"); }},
        };
        c.fork = 20;
        c.still_frame = 30;
        all.push_back(c);
    }

    // E2. TURN UNDEAD with only a living soldier in reach: the classic game
    // takes the 20 for nothing; the fix says NO UNDEAD IN RANGE.
    {
        Pair c;
        c.name = "e2_turn_no_undead";
        c.scene.family = FAMILY_CLERIC;
        c.scene.slot = 2;
        c.scene.frames = 96;
        c.scene.stage = [](Run& run) {
            walker* hero = run.hero();
            hero->stats()->set_max_magicpoints(20.0f);   // exactly one TURN UNDEAD's worth
            hero->stats()->set_magicpoints(20.0f);
            stop_mp_regen(hero);
            add_posted(run, "foe", FAMILY_SOLDIER, 1, 28, 0, 1);
        };
        c.scene.before_tick = [](Run& run, int) { hold_fire(run.actor("foe")); };
        c.scene.keys = [](Run&, int f) { return press_at(f, 20, true); };
        c.off_shows = {
            pool_guard(19, 20.0f),
            {"the pool is spent", [](Run& f) { return mp(f.hero()) < 1.0f; }},
            {"and stays spent", [](Run& f) { return f.frame == 60 && mp(f.hero()) < 1.0f; }},
        };
        c.off_never = {
            says("NO UNDEAD IN RANGE"),
            says("NOT ENOUGH MP"),
        };
        c.on_shows = {
            pool_guard(19, 20.0f),
            says("NO UNDEAD IN RANGE"),
            pool_at(21, 20.0f),
        };
        c.on_never = {
            {"the pool drops", [](Run& f) { return mp(f.hero()) < 20.0f; }},
        };
        c.fork = 20;
        c.still_frame = 26;
        all.push_back(c);
    }

    // F. MIND CONTROL resisted, the berserk colour rolls the archmage's own
    // team: the classic orc joins the caster for the berserk; the fix moves
    // the colour on, so it stays an enemy and keeps fighting.
    {
        Pair c;
        c.name = "f_berserk_colour";
        c.scene.family = FAMILY_ARCHMAGE;
        c.scene.slot = 4;
        c.scene.level = 12;
        c.scene.frames = 120;
        c.scene.stage = [](Run& run) {
            walker* orc = add_posted(run, "orc", FAMILY_ORC, 1, 60, 0, 10);
            blunt(orc);
        };
        c.scene.before_tick = [](Run& run, int f) {
            if (f < 24)
                hold_fire(run.actor("orc"));
            if (f == 20)
            {
                run.marks["hp"] = hp_of(run.hero_seen());
                // resisted (rand(20) == 0), colour 0, 25 + 39 ticks
                pin_draws(run, {20, 8, 40}, {0, 0, 39});
            }
            if (f == 24)
                release(run.actor("orc"));
        };
        c.scene.keys = [](Run&, int f) { return press_at(f, 20); };
        c.off_shows = {
            {"the orc wears the archmage's colour", [](Run& f) {
                 walker* o = f.actor_seen("orc"); return o != nullptr && o->team_num() == 0; }},
            {"has controlled 1 men", [](Run& f) { return said(f, "has controlled 1 men"); }},
            {"the orc walks away from the archmage", [](Run& f) {
                 return f.frame > 24 && gap(f.hero_seen(), f.actor_seen("orc")) > 100.0f; }},
            {"the archmage is untouched while it lasts", [](Run& f) {
                 return f.frame == 70 && has_mark(f, "hp") && hp_of(f.hero_seen()) == f.mark("hp"); }},
        };
        c.off_never = {
            {"the archmage is hurt while the orc is controlled", [](Run& f) {
                 return f.frame < 84 && has_mark(f, "hp") && hp_of(f.hero_seen()) < f.mark("hp"); }},
        };
        c.on_shows = {
            {"has controlled 1 men", [](Run& f) { return said(f, "has controlled 1 men"); }},
            {"the orc walks up to the archmage", [](Run& f) {
                 return f.frame > 24 && gap(f.hero_seen(), f.actor_seen("orc")) <= 30.0f; }},
            {"the orc swings at the archmage once the control runs out", [](Run& f) {
                 walker* o = f.actor_seen("orc");
                 return f.frame > 24 && o != nullptr && o->ani_type() == ANI_ATTACK &&
                        gap(f.hero_seen(), o) <= 30.0f; }},
        };
        c.on_never = {
            {"the orc wears the archmage's colour", [](Run& f) {
                 walker* o = f.actor_seen("orc"); return o != nullptr && o->team_num() == 0; }},
        };
        c.fork = 20;
        c.still_frame = 36;
        all.push_back(c);
    }

    // G. CHARM with two foes in reach: the classic thief takes the first in
    // the world's list (the far soldier); the fix takes the nearest (the orc).
    {
        Pair c;
        c.name = "g_charm_nearest";
        c.scene.family = FAMILY_THIEF;
        c.scene.slot = 3;
        c.scene.level = 7;
        c.scene.frames = 110;
        c.scene.stage = [](Run& run) {
            add_posted(run, "far", FAMILY_SOLDIER, 1, 40, 0, 1);   // earlier in the list
            add_posted(run, "near", FAMILY_ORC, 1, 24, 0, 1);
        };
        c.scene.before_tick = [](Run& run, int f) {
            if (f <= 20)
            {
                hold_fire(run.actor("far"));
                hold_fire(run.actor("near"));
            }
            if (f == 20)
                pin_draws(run, {20}, {1});              // the resist roll passes
            if (f == 21)
            {
                for (const char* n : {"far", "near"})
                    if (walker* a = run.actor(n))
                        a->set_busy(0.0f);               // still posted, they now fight
            }
        };
        c.scene.keys = [](Run&, int f) { return press_at(f, 20, true); };
        const auto joins = [](const char* who) {
            return [who](Run& f) { walker* a = f.actor_seen(who); return a != nullptr && a->team_num() == 0; };
        };
        c.off_shows = {
            {"the far soldier takes the thief's colour", joins("far")},
            {"charmed an opponent", [](Run& f) { return said(f, "charmed an opponent"); }},
        };
        c.off_never = {
            {"the near orc takes the thief's colour", joins("near")},
            says("NOT ENOUGH MP"),
        };
        c.on_shows = {
            {"the near orc takes the thief's colour", joins("near")},
            {"charmed an opponent", [](Run& f) { return said(f, "charmed an opponent"); }},
        };
        c.on_never = {
            {"the far soldier takes the thief's colour", joins("far")},
            says("NOT ENOUGH MP"),
        };
        c.fork = 20;
        c.still_frame = 36;
        all.push_back(c);
    }

    // H1. BOUNCING ROCKS: the classic volley refunds 3 for two rocks, the
    // fix refunds the two it throws. Five volleys later the classic elf can
    // still pay for a sixth.
    {
        Pair c;
        c.name = "h1_volley_refund";
        c.scene.family = FAMILY_ELF;
        c.scene.slot = 2;
        c.scene.level = 4;
        c.scene.frames = 110;
        c.scene.stage = [](Run& run) {
            walker* hero = run.hero();
            stop_mp_regen(hero);
            hero->face_delta(0, -1);
            hold_fire(add_actor(run, "tower", FAMILY_TOWER1, 1, 0, -80));
        };
        c.scene.before_tick = [](Run& run, int) { hold_fire(run.actor("tower")); };
        c.scene.keys = [](Run&, int f) {
            Keys k;
            k.special = f == 20 || f == 24 || f == 28 || f == 32 || f == 36 || f == 44;
            return k;
        };
        c.off_shows = {
            pool_guard(19, 118.0f),
            pool_at(37, 23.0f),
            {"the sixth volley flies", [weap_rock](Run& f) {
                 return f.frame >= 44 &&
                        count_of(f.display->world(), Order::Weapon, weap_rock, f.hero_id) >= 2; }},
            pool_at(45, 4.0f),
        };
        c.off_never = {
            says("NOT ENOUGH MP"),
            says("COULD NOT FIRE"),
        };
        c.on_shows = {
            pool_guard(19, 118.0f),
            pool_at(37, 18.0f),
            says("NOT ENOUGH MP"),
            pool_at(45, 18.0f),
        };
        c.on_never = {
            {"a sixth volley is paid for", [](Run& f) { return f.frame >= 44 && mp(f.hero()) != 18.0f; }},
            says("COULD NOT FIRE"),
        };
        c.fork = 20;
        c.still_frame = 48;
        all.push_back(c);
    }

    // H2 and I share their shape: a caster touching a tower presses its
    // first Special every third frame; the blocked throw becomes a swing.
    const auto blocked_stage = [](float pool) {
        return [pool](Run& run) {
            walker* hero = run.hero();
            hero->stats()->set_magicpoints(pool);
            stop_mp_regen(hero);
            hero->face_delta(0, -1);
            walker* tower = add_actor(run, "tower", FAMILY_TOWER1, 1, 0, -16);
            hold_fire(tower);
            // Deep enough to take a swing every third frame for the whole
            // scene: a fallen tower would let the throws fly free.
            tower->stats()->set_max_hitpoints(kBlockerHp);
            tower->stats()->set_hitpoints(kBlockerHp);
            mark_hp(run, "tower");
        };
    };
    const auto blocked_tick = [](Run& run, int f) {
        hold_fire(run.actor("tower"));
        if (f == 22)
            run.marks["t22"] = hp_of(run.actor_seen("tower"));
    };
    const auto tower_beaten = [](Run& f) {
        return hp_of(f.actor_seen("tower")) < f.mark("tower.hp") - 20.0f;
    };
    const auto hit_again = [](Run& f) {
        return has_mark(f, "t22") && hp_of(f.actor_seen("tower")) < f.mark("t22");
    };

    // H2. ROCKS against a tower touching the elf: the classic refund of two
    // for a swing that costs one nets a magic point a press; the fix takes
    // the two back.
    {
        Pair c;
        c.name = "h2_rocks_blocked";
        c.scene.family = FAMILY_ELF;
        c.scene.slot = 1;
        c.scene.level = 1;
        c.scene.frames = 124;
        c.scene.stage = blocked_stage(10.0f);
        c.scene.before_tick = blocked_tick;
        c.scene.keys = [](Run&, int f) { return presses_every(f, 20, 3); };
        c.off_shows = {
            pool_guard(19, 10.0f),
            {"the pool climbs press after press", [](Run& f) { return mp(f.hero()) >= 25.0f; }},
            {"the tower is beaten down", tower_beaten},
        };
        c.off_never = {
            {"the pool drops under the start", [](Run& f) { return f.frame > 22 && mp(f.hero()) < 10.0f; }},
            says("NOT ENOUGH MP"),
        };
        c.on_shows = {
            pool_guard(19, 10.0f),
            pool_at(21, 9.0f),
            says("NOT ENOUGH MP"),
        };
        c.on_never = {
            {"the pool grows", [](Run& f) { return mp(f.hero()) > 10.0f; }},
            {"the tower is hit a second time", hit_again},
        };
        c.fork = 20;
        c.still_frame = 72;
        all.push_back(c);
    }

    // I. GROW TREE against a tower touching the druid: the classic refund
    // pays for the swing (net nothing); the fix takes it back.
    {
        Pair c;
        c.name = "i_tree_blocked";
        c.scene.family = FAMILY_DRUID;
        c.scene.slot = 1;
        c.scene.level = 1;
        c.scene.frames = 124;
        c.scene.stage = blocked_stage(15.0f);
        c.scene.before_tick = blocked_tick;
        c.scene.keys = [](Run&, int f) { return presses_every(f, 20, 3); };
        c.off_shows = {
            pool_guard(19, 15.0f),
            pool_at(60, 15.0f),
            {"the tower is beaten down", tower_beaten},
        };
        c.off_never = {
            {"the pool moves", [](Run& f) { return f.frame > 21 && mp(f.hero()) != 15.0f; }},
            says("NOT ENOUGH MP"),
        };
        c.on_shows = {
            pool_guard(19, 15.0f),
            pool_at(21, 11.0f),
            says("NOT ENOUGH MP"),
        };
        c.on_never = {
            {"the pool grows back", [](Run& f) { return f.frame > 20 && mp(f.hero()) > 11.0f; }},
            {"the tower is hit a second time", hit_again},
        };
        c.fork = 20;
        c.still_frame = 72;
        all.push_back(c);
    }

    // J. HEAL on a soldier 40 HP down: the classic heal lands its whole
    // amount, far over his maximum; the fix stops at full. The seat then
    // switches to the soldier, whose own bar shows it.
    {
        Pair c;
        c.name = "j_heal_cap";
        c.scene.family = FAMILY_CLERIC;
        c.scene.slot = 1;
        c.scene.frames = 110;
        c.scene.roster_extra = {FAMILY_SOLDIER};
        c.scene.stage = [](Run& run) {
            walker* hero = run.hero();
            walker* ally = run.actor("hero1");
            ally->setxy(static_cast<short>(hero->xpos() + 30), hero->ypos());
            hold_post(ally);
            ally->stats()->set_hitpoints(ally->stats()->max_hitpoints() - 40.0f);
            live_game::stop_hp_regen(ally);
            run.marks["max"] = ally->stats()->max_hitpoints();
        };
        c.scene.before_tick = [](Run& run, int f) {
            if (f == 21)
                printf("  j_heal_cap: max %.0f, the soldier now %.0f\n", run.mark("max"),
                       hp_of(run.actor("hero1")));
        };
        c.scene.keys = [](Run&, int f) {
            Keys k = press_at(f, 20);
            k.switch_char = tap(f, 32);
            return k;
        };
        const auto seat_on_soldier = [](Run& f) {
            walker* s = seat_seen(f);
            return s != nullptr && s == f.actor_seen("hero1");
        };
        c.off_shows = {
            {"the soldier is healed past his maximum", [](Run& f) {
                 return hp_of(f.actor_seen("hero1")) > f.mark("max"); }},
            says("Cleric healed 1 man!"),
            {"the seat is on the soldier", seat_on_soldier},
        };
        c.off_never = {
            {"the soldier is at or under his maximum after the heal", [](Run& f) {
                 return f.frame >= 21 && hp_of(f.actor_seen("hero1")) <= f.mark("max"); }},
        };
        c.on_shows = {
            {"the soldier is exactly at his maximum", [](Run& f) {
                 return f.frame == 21 && hp_of(f.actor_seen("hero1")) == f.mark("max"); }},
            says("Cleric healed 1 man!"),
            {"the seat is on the soldier", seat_on_soldier},
        };
        c.on_never = {
            {"the soldier is over his maximum", [](Run& f) {
                 return hp_of(f.actor_seen("hero1")) > f.mark("max"); }},
        };
        c.fork = 20;
        c.still_frame = 40;
        all.push_back(c);
    }

    // K1 to K4: the four specials that classic players could cast at nobody.
    // A foe posted far off is let go early and walks up to the hero; the
    // hero casts once at nobody and once when the foe is beside him.
    const auto walk_in_stage = [](int dx) {
        return [dx](Run& run) {
            walker* foe = add_posted(run, "foe", FAMILY_SOLDIER, 1, dx, 0, 1);
            blunt(foe);
        };
    };

    // K1. WHIRLWIND at nobody.
    {
        Pair c;
        c.name = "k1_whirlwind_nobody";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.slot = 3;
        c.scene.level = 7;
        c.scene.frames = 130;
        const auto foe_stage = walk_in_stage(140);
        c.scene.stage = [foe_stage](Run& run) {
            stop_mp_regen(run.hero());
            foe_stage(run);
        };
        c.scene.before_tick = [](Run& run, int f) {
            if (f < 24)
                hold_fire(run.actor("foe"));
            if (f == 24)
                release(run.actor("foe"));
        };
        c.scene.keys = [](Run&, int f) { return f == 104 ? press_at(f, 104) : press_at(f, 20); };
        c.off_shows = {
            pool_guard(19, 178.0f),
            arrived(kInReach),
            pool_at(21, 58.0f),
            {"the second spin is NOT ENOUGH MP",
             [](Run& f) { return f.frame >= 104 && voiced(f, "NOT ENOUGH MP"); }},
        };
        c.off_never = {
            says("NO FOE IN RANGE"),
            {"the foe is shoved away", [](Run& f) { return f.frame > 104 && forced_walk(f.actor("foe")); }},
        };
        c.on_shows = {
            pool_guard(19, 178.0f),
            arrived(kInReach),
            says("NO FOE IN RANGE"),
            pool_at(21, 178.0f),
            {"the second spin shoves the foe away",
             [](Run& f) { return f.frame >= 104 && forced_walk(f.actor("foe")); }},
        };
        c.on_never = {says("NOT ENOUGH MP")};
        c.fork = 20;
        c.still_frame = 108;
        all.push_back(c);
    }

    // K2. TAUNT at nobody.
    {
        Pair c;
        c.name = "k2_taunt_nobody";
        c.scene.family = FAMILY_THIEF;
        c.scene.slot = 3;
        c.scene.level = 7;
        c.scene.frames = 130;
        const auto foe_stage = walk_in_stage(140);
        c.scene.stage = [foe_stage](Run& run) {
            stop_mp_regen(run.hero());
            foe_stage(run);
        };
        c.scene.before_tick = [](Run& run, int f) {
            if (f < 24)
                hold_fire(run.actor("foe"));
            if (f == 24)
                release(run.actor("foe"));
        };
        c.scene.keys = [](Run&, int f) { return f == 104 ? press_at(f, 104) : press_at(f, 20); };
        c.off_shows = {
            pool_guard(19, 184.0f),
            arrived(kInReach),
            {"'Nyah Nyah!' at nobody", [](Run& f) { return said(f, "Nyah Nyah"); }},
            pool_at(21, 84.0f),
            {"the second taunt is NOT ENOUGH MP",
             [](Run& f) { return f.frame >= 104 && voiced(f, "NOT ENOUGH MP"); }},
        };
        c.off_never = {says("NO FOE IN RANGE")};
        c.on_shows = {
            pool_guard(19, 184.0f),
            arrived(kInReach),
            says("NO FOE IN RANGE"),
            pool_at(21, 184.0f),
            {"the taunt lands on the foe", [](Run& f) { return f.frame >= 104 && said(f, "Nyah Nyah"); }},
        };
        c.on_never = {
            {"a taunt before the foe is there", [](Run& f) { return f.frame < 104 && said(f, "Nyah Nyah"); }},
            says("NOT ENOUGH MP"),
        };
        c.fork = 20;
        c.still_frame = 108;
        all.push_back(c);
    }

    // K3. SCARE at nobody: the classic scare starts its 8-second rest.
    {
        Pair c;
        c.name = "k3_scare_nobody";
        c.scene.family = FAMILY_GHOST;
        c.scene.slot = 1;
        c.scene.level = 1;
        c.scene.frames = 130;
        const auto foe_stage = walk_in_stage(140);
        c.scene.stage = [foe_stage](Run& run) {
            fill_mana(run.hero(), 120.0f);              // four scares' worth
            stop_mp_regen(run.hero());
            foe_stage(run);
        };
        c.scene.before_tick = [fx_scare](Run& run, int f) {
            if (f < 16)
                hold_fire(run.actor("foe"));
            if (f == 16)
                release(run.actor("foe"));
            // The carrier's life, printed once per side.
            const int n = count_of(run.server->world(), Order::FX, fx_scare);
            if (n > 0 && !has_mark(run, "scare.born"))
                run.marks["scare.born"] = static_cast<float>(f);
            if (n == 0 && has_mark(run, "scare.born") && !has_mark(run, "scare.gone"))
            {
                run.marks["scare.gone"] = static_cast<float>(f);
                printf("  k3_scare_nobody: the scare carrier lived from %.0f to %d\n",
                       run.mark("scare.born"), f);
            }
        };
        c.scene.keys = [](Run&, int f) { return f == 94 ? press_at(f, 94) : press_at(f, 12); };
        const auto carrier = [fx_scare](Run& f) {
            return count_of(f.display->world(), Order::FX, fx_scare) >= 1;
        };
        c.off_shows = {
            arrived(kInReach),
            {"the scare goes off around the ghost at nobody", carrier},
            pool_at(13, 90.0f),
            says("SCARE IN"),
        };
        c.off_never = {
            says("NO FOE IN RANGE"),
            {"a second scare", [carrier](Run& f) { return f.frame > 94 && carrier(f); }},
        };
        // The foe's walk differs with the setting on: the soldier bot's
        // refused special rolls now take its ordinary act (fix W, #371), so
        // its pauses on the way in fall on other ticks and from frame 81 it
        // stands 54 px from the ghost centre to centre (52 by the game's
        // measure), never as close as the 50 px arrived() asks for (with
        // only that fix undone it reaches 50 on frame 79). What the scene
        // claims is that the second scare goes off with the foe in its
        // reach, so this side asks exactly that, at the measured closest
        // approach: within 52 px by the game's measure, inside the scare's
        // radius (og::combat::scare_radius, 60 px at level 1, the radius the
        // cast's own no-foe refusal reads), by the press on frame 94. The
        // number is its own claim: the cast's refusal two tells below reads
        // the radius, and this one would red first at 51.
        c.on_shows = {
            arrived_by(52, 94),
            says("NO FOE IN RANGE"),
            pool_at(13, 120.0f),
            {"the scare goes off at the foe", [carrier](Run& f) { return f.frame >= 94 && carrier(f); }},
            {"the foe flees", [](Run& f) { return forced_walk(f.actor("foe")); }},
        };
        c.on_never = {says("SCARE IN")};
        c.fork = 12;
        c.still_frame = 100;
        all.push_back(c);
    }

    // K4. HOWL at nobody: the classic howl starts its 10-second rest.
    {
        Pair c;
        c.name = "k4_howl_nobody";
        c.scene.family = FAMILY_ORC;
        c.scene.slot = 1;
        c.scene.level = 1;
        c.scene.frames = 130;
        const auto foe_stage = walk_in_stage(-190);
        c.scene.stage = [foe_stage](Run& run) {
            fill_mana(run.hero(), 120.0f);
            stop_mp_regen(run.hero());
            foe_stage(run);
        };
        c.scene.before_tick = [](Run& run, int f) {
            if (f < 16)
                hold_fire(run.actor("foe"));
            if (f == 16)
                release(run.actor("foe"));
            if (f == 96)
            {
                // The stun is 10 + rand0(level*10) - rand0(con*10), con from
                // the NPC foe's live hp: pin a long one on both sides.
                walker* foe = run.actor("foe");
                const int con = foe == nullptr ? 0 :
                    std::max(0, static_cast<int>(std::trunc(foe->stats()->hitpoints() / 30.0f)));
                const auto bound = static_cast<std::uint32_t>(
                    con * living_tuning(FAMILY_ORC, "yell_con_roll_mult"));
                const auto level_bound = static_cast<std::uint32_t>(
                    living_tuning(FAMILY_ORC, "yell_level_roll_mult"));
                printf("  k4_howl_nobody: stun bounds %u and %u\n", level_bound, bound);
                if (bound > 0)
                    pin_draws(run, {level_bound, bound}, {level_bound - 1, 0});
                else
                    pin_draws(run, {level_bound}, {level_bound - 1});
            }
        };
        c.scene.keys = [](Run&, int f) { return f == 96 ? press_at(f, 96) : press_at(f, 12); };
        c.off_shows = {
            pool_guard(11, 120.0f),
            arrived(kInReach),
            pool_at(13, 95.0f),
            says("HOWL IN"),
        };
        c.off_never = {
            says("NO FOE IN RANGE"),
            {"the foe is stunned", [](Run& f) {
                 walker* foe = f.actor("foe"); return foe != nullptr && foe->stats()->frozen_delay() > 0; }},
        };
        c.on_shows = {
            pool_guard(11, 120.0f),
            arrived(kInReach),
            says("NO FOE IN RANGE"),
            pool_at(13, 120.0f),
            {"the howl stuns the foe", [](Run& f) {
                 walker* foe = f.actor_seen("foe"); return foe != nullptr && foe->stats()->frozen_delay() > 0; }},
        };
        c.on_never = {says("HOWL IN")};
        c.fork = 12;
        c.still_frame = 110;   // the left foe walks in, the right one stands stunned
        all.push_back(c);
    }

    // L. A level-13 enemy mage bot rolls slot 5 with three towers close by:
    // the classic roll folds 5 into TELEPORT, whose gate says no (and the
    // 110 px rule shuts every slot anyway); the fix casts HEARTBURST.
    {
        Pair c;
        c.name = "l_slot_roll";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.frames = 110;
        c.scene.stage = [](Run& run) {
            add_bot(run, "mage", FAMILY_MAGE, 1, -110, -20, 13);
            const int at[3][2] = {{-70, -20}, {-110, -56}, {-150, -20}};
            for (int i = 0; i < 3; ++i)
            {
                const std::string n = "tower" + std::to_string(i);
                hold_fire(add_actor(run, n, FAMILY_TOWER1, 0, at[i][0], at[i][1]));
                mark_hp(run, n);
            }
        };
        c.scene.before_tick = [](Run& run, int f) {
            for (int i = 0; i < 3; ++i)
                hold_fire(run.actor("tower" + std::to_string(i)));
            walker* mage = steer_bot(run, "mage");
            if (mage == nullptr)
                return;
            if (f == kCue)
            {
                ready_bot(mage);
                fill_mana(mage, 600.0f);
                pin_draws(run, {5, 5}, {0, 4});         // special; slot roll 4 -> slot 5
            }
            else
                mage->stats()->set_magicpoints(0.0f);
        };
        c.scene.keys = [](Run&, int) { return Keys{}; };
        const auto bursts = [fx_explosion](Run& f) {
            return count_of(f.display->world(), Order::FX, fx_explosion) > 0;
        };
        const auto tower_hit = [](Run& f) {
            for (int i = 0; i < 3; ++i)
            {
                const std::string n = "tower" + std::to_string(i);
                walker* t = f.actor_seen(n);
                if (t == nullptr || t->dead() || hp_of(t) < f.mark(n + ".hp"))
                    return true;
            }
            return false;
        };
        c.off_shows = {
            {"nothing has burst well after the cue",
             [bursts](Run& f) { return f.frame == 60 && !bursts(f); }},
        };
        c.off_never = {
            {"an explosion", bursts},
            {"a tower is hit", tower_hit},
        };
        c.on_shows = {
            {"HEARTBURST explodes around the mage", bursts},
            {"a tower is hit", tower_hit},
        };
        c.fork = kCue;
        c.still_frame = 28;
        all.push_back(c);
    }

    // M1. An enemy thief bot tries to charm the hero the seat drives: the
    // classic game charms him; the fix skips a driven hero and the thief
    // taunts instead. The thief's posted ally 60 px south is the backing a
    // bot's taunt is cast for with the setting on (staged after the thief,
    // so the pinned draws stay the thief's).
    {
        Pair c;
        c.name = "m1_hero_resists_charm";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.level = 5;
        c.scene.frames = 120;
        c.scene.stage = [](Run& run) {
            add_bot(run, "thief", FAMILY_THIEF, 1, 40, 0, 7);
            add_posted(run, "backer", FAMILY_SOLDIER, 1, 40, 60, 1);
        };
        c.scene.before_tick = [](Run& run, int f) {
            hold_fire(run.actor("backer"));
            walker* thief = steer_bot(run, "thief");
            if (thief == nullptr)
                return;
            if (f == kCue)
            {
                ready_bot(thief);
                fill_mana(thief, 100.0f);
                // special (1 in 5), slot 3, coin = Shift, the resist roll passes
                pin_draws(run, {5, 3, 2, 20}, {0, 2, 1, 1});
            }
            else
                thief->stats()->set_magicpoints(0.0f);
        };
        c.scene.keys = [](Run&, int) { return Keys{}; };
        c.off_shows = {
            {"the hero is charmed onto the thief's side",
             [](Run& f) { walker* h = f.hero_seen(); return h != nullptr && h->team_num() == 1; }},
            {"the cell reads CHARMED: 10s",
             [](Run& f) { return seat_timer_text(f).rfind("CHARMED", 0) == 0 && hud_drew("CHARMED: 10s"); }},
            {"Thief charmed an opponent!", [](Run& f) { return said(f, "charmed an opponent"); }},
        };
        c.off_never = {
            {"'Nyah Nyah!'", [](Run& f) { return said(f, "Nyah Nyah"); }},
        };
        c.on_shows = {
            {"the thief taunts instead", [](Run& f) { return said(f, "Nyah Nyah"); }},
            {"the hero keeps his colour well after the cue",
             [](Run& f) { walker* h = f.hero_seen(); return f.frame == 60 && h != nullptr && h->team_num() == 0; }},
        };
        c.on_never = {
            {"the hero changes side", [](Run& f) { walker* h = f.hero_seen(); return h != nullptr && h->team_num() != 0; }},
            {"a CHARMED cell", [](Run& f) { return seat_timer_text(f).rfind("CHARMED", 0) == 0; }},
            {"charmed an opponent", [](Run& f) { return said(f, "charmed an opponent"); }},
        };
        c.fork = kCue;
        c.still_frame = 32;
        all.push_back(c);
    }

    // M2. The thief charms an orc and the scene strikes the thief down: the
    // classic seat takes the charmed orc; the fix skips it for the soldier.
    {
        Pair c;
        c.name = "m2_seat_skips_charmed";
        c.scene.family = FAMILY_THIEF;
        c.scene.slot = 3;
        c.scene.frames = 110;
        c.scene.stage = [](Run& run) {
            add_posted(run, "orc", FAMILY_ORC, 1, 24, 0, 1);       // earlier in the list
            add_posted(run, "ally", FAMILY_SOLDIER, 0, -110, 0, 0);
        };
        c.scene.before_tick = [](Run& run, int f) {
            hold_fire(run.actor("orc"));
            hold_fire(run.actor("ally"));
            if (f == 20)
                pin_draws(run, {20}, {1});              // the charm passes
            if (f == 40)
            {
                walker* hero = run.hero();
                if (hero != nullptr && !hero->dead())
                {
                    hero->stats()->set_hitpoints(0.0f);  // the scene strikes the thief down
                    hero->set_dead(1);
                    hero->death();
                }
            }
        };
        c.scene.keys = [](Run&, int f) { return press_at(f, 20, true); };
        const Tell orc_joins = {"the orc takes the thief's colour", [](Run& f) {
            walker* o = f.actor_seen("orc"); return o != nullptr && o->team_num() == 0; }};
        const Tell thief_down = {"the thief is dead", [](Run& f) {
            walker* h = f.hero_seen(); return f.frame > 0 && (h == nullptr || h->dead()); }};
        const Tell new_seat = {"the seat drives a new walker", [](Run& f) {
            walker* s = seat_seen(f); return s != nullptr && s != f.hero_seen(); }};
        const auto seat_is = [](const char* who) {
            return [who](Run& f) { walker* s = seat_seen(f); return s != nullptr && s == f.actor_seen(who); };
        };
        const auto charmed_cell = [](Run& f) { return seat_timer_text(f).rfind("CHARMED", 0) == 0; };
        c.off_shows = {
            orc_joins, thief_down, new_seat,
            {"the seat takes the charmed orc", seat_is("orc")},
            {"its cell reads CHARMED", charmed_cell},
        };
        c.off_never = {{"the seat takes the soldier", seat_is("ally")}};
        c.on_shows = {
            orc_joins, thief_down, new_seat,
            {"the seat takes the soldier", seat_is("ally")},
        };
        c.on_never = {
            {"the seat takes the charmed orc", seat_is("orc")},
            {"a CHARMED cell", charmed_cell},
        };
        c.fork = 40;
        c.still_frame = 48;
        all.push_back(c);
    }

    // S. The hero throws once and then spins at a foe beside him. A hero's
    // fire delay is fractional (dexterity / 47 off the class's delay), so
    // the throw leaves busy a fraction below zero for good; the classic
    // WHIRLWIND refuses SPECIAL BUSY while busy is not zero.
    {
        Pair c;
        c.name = "s_busy_after_a_throw";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.slot = 3;
        c.scene.level = 7;
        c.scene.frames = 100;
        c.scene.stage = [](Run& run) {
            stop_mp_regen(run.hero());
            add_posted(run, "foe", FAMILY_SOLDIER, 1, 30, 0, 1);
        };
        c.scene.before_tick = [](Run& run, int) { hold_fire(run.actor("foe")); };
        c.scene.keys = [](Run&, int f) {
            Keys k = press_at(f, 50);
            k.left = tap(f, 10);   // a step gives the throw its heading
            k.fire = tap(f, 20);
            return k;
        };
        const Tell rests_below_zero = {"the throw leaves busy a fraction below zero", [](Run& f) {
            walker* h = f.hero();
            return f.frame == 49 && h != nullptr && h->busy() < 0.0f && h->busy() > -1.0f; }};
        const Tell spins = {"the spin shoves the foe away", [](Run& f) {
            return f.frame >= 50 && forced_walk(f.actor("foe")); }};
        c.off_shows = {rests_below_zero, says("SPECIAL BUSY")};
        c.off_never = {spins};
        c.on_shows = {rests_below_zero, spins};
        c.on_never = {says("SPECIAL BUSY")};
        c.fork = 50;
        c.still_frame = 56;
        all.push_back(c);
    }

    // U. CHARGE pressed into a foe that stands against the fighter's
    // front. The cast's "is my front blocked?" probe meets the foe and the
    // cast refuses PATH BLOCKED on both sides. In the classic game that
    // probe is a bump: the fighter swings, and on the next frames the knife
    // lands (the foe's HP falls, the knife's 2 MP are spent). With the fix
    // the question is asked quietly and nothing happens. The foe is a posted
    // soldier held from fighting, so nothing else ever touches its HP.
    {
        Pair c;
        c.name = "u_charge_front_foe";
        c.scene.family = FAMILY_SOLDIER;
        c.scene.slot = 1;
        c.scene.frames = 100; // every tell lands by frame 40; the GIF wants 8 s
        c.scene.stage = [](Run& run) {
            walker* hero = run.hero();
            stop_mp_regen(hero);
            hero->face_delta(0, 1);                   // faces the foe below; no step, no draw
            walker* foe = add_posted(run, "foe", FAMILY_SOLDIER, 1, 0, 15, 1);
            run.marks["foe.hp"] = hp_of(foe);
        };
        c.scene.before_tick = [](Run& run, int) {
            hold_fire(run.actor("foe"));                // a passive blocker: it never swings
        };
        c.scene.keys = [](Run&, int f) { return press_at(f, 20); };
        const Tell blow_lands = {"the fighter's knife lands on the foe", [](Run& f) {
            return f.frame > 20 && hp_of(f.actor_seen("foe")) < f.mark("foe.hp"); }};
        const Tell knife_paid = {"the knife's 2 MP are spent", [](Run& f) {
            return f.frame > 20 && mp(f.hero()) == 248.0f; }};
        const Tell pool_drops = {"the pool drops", [](Run& f) { return mp(f.hero()) < 250.0f; }};
        c.off_shows = {
            pool_guard(19, 250.0f),
            says("PATH BLOCKED"),
            blow_lands,
            knife_paid,
        };
        c.on_shows = {
            pool_guard(19, 250.0f),
            says("PATH BLOCKED"),
            pool_at(40, 250.0f),
            {"the foe is untouched on frame 40", [](Run& f) {
                 return f.frame == 40 && hp_of(f.actor_seen("foe")) == f.mark("foe.hp"); }},
        };
        c.on_never = {blow_lands, pool_drops};
        c.fork = 20;
        c.still_frame = 24;
        all.push_back(c);
    }

    return all;
}

// Plays both sides of the scene pairs() names `name` and checks them.
void play_pair_named(const char* name)
{
    for (const Pair& pair : pairs())
        if (std::strcmp(pair.name, name) == 0)
        {
            SCOPED_TRACE(pair.name);
            PairResult r;
            play_pair(pair, r);
            return;
        }
    FAIL() << "pairs() has no scene named " << name;
}

} // namespace gameplay_fixes_scenes

#define GAMEPLAY_FIXES_SCENE(test_name, scene)                                 \
    static const bool test_name##_is_listed =                                  \
        (gameplay_fixes_listed().push_back({#test_name, scene}), true);        \
    TEST_F(GameplayFixes, test_name)                                           \
    {                                                                          \
        gameplay_fixes_scenes::play_pair_named(scene);                         \
    }

// In pairs() order.
GAMEPLAY_FIXES_SCENE(a_resisted_disarm_costs_half_and_the_boomerang_still_flies, "a1_disarm_resisted")
GAMEPLAY_FIXES_SCENE(disarm_with_nobody_in_reach_says_no_one_to_disarm, "a2_disarm_nobody")
GAMEPLAY_FIXES_SCENE(a_won_disarm_no_longer_hastens_a_stronger_foe, "a3_disarm_stagger")
GAMEPLAY_FIXES_SCENE(a_mage_bot_no_longer_freezes_time_at_nobody, "b_mage_bot_freeze")
GAMEPLAY_FIXES_SCENE(a_thief_bot_taunts_instead_of_a_doomed_charm, "c_thief_bot_shift")
GAMEPLAY_FIXES_SCENE(summon_with_no_room_costs_nothing, "d_summon_no_room")
GAMEPLAY_FIXES_SCENE(turn_undead_reaches_60_px, "e1_turn_reach")
GAMEPLAY_FIXES_SCENE(turn_undead_with_no_undead_costs_nothing, "e2_turn_no_undead")
GAMEPLAY_FIXES_SCENE(a_resisted_mind_control_never_hands_the_orc_to_the_caster, "f_berserk_colour")
GAMEPLAY_FIXES_SCENE(charm_takes_the_nearest_foe, "g_charm_nearest")
GAMEPLAY_FIXES_SCENE(a_rock_volley_refunds_only_what_it_threw, "h1_volley_refund")
GAMEPLAY_FIXES_SCENE(a_blocked_rock_volley_costs_its_swing, "h2_rocks_blocked")
GAMEPLAY_FIXES_SCENE(a_blocked_grow_tree_costs_its_swing, "i_tree_blocked")
GAMEPLAY_FIXES_SCENE(heal_stops_at_full_health, "j_heal_cap")
GAMEPLAY_FIXES_SCENE(whirlwind_at_nobody_is_refused_for_free, "k1_whirlwind_nobody")
GAMEPLAY_FIXES_SCENE(taunt_at_nobody_is_refused_for_free, "k2_taunt_nobody")
GAMEPLAY_FIXES_SCENE(scare_at_nobody_is_refused_and_starts_no_rest, "k3_scare_nobody")
GAMEPLAY_FIXES_SCENE(howl_at_nobody_is_refused_and_starts_no_rest, "k4_howl_nobody")
GAMEPLAY_FIXES_SCENE(a_mage_bot_slot_roll_of_five_casts_heartburst, "l_slot_roll")
GAMEPLAY_FIXES_SCENE(a_driven_hero_resists_a_bot_charm, "m1_hero_resists_charm")
GAMEPLAY_FIXES_SCENE(a_seat_never_lands_on_a_charmed_walker, "m2_seat_skips_charmed")
GAMEPLAY_FIXES_SCENE(a_hero_that_threw_can_still_whirlwind, "s_busy_after_a_throw")
GAMEPLAY_FIXES_SCENE(a_charge_into_a_foe_at_the_front_no_longer_swings, "u_charge_front_foe")

TEST_F(GameplayFixes, the_tests_name_every_scene_in_order)
{
    std::vector<std::string> listed;
    for (const ListedScene& t : gameplay_fixes_listed())
        listed.push_back(t.scene);
    std::vector<std::string> defined;
    for (const gameplay_fixes_scenes::Pair& p : gameplay_fixes_scenes::pairs())
        defined.push_back(p.name);
    EXPECT_EQ(defined, listed)
        << "one test per scene of pairs(), declared in its order";
}
