#include "scenario_runtime.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstring>
#include <string>

// The scenario table mirrors the PIX_* tile ids numerically (it must not
// include pixdefs.h to stay byte-mirrorable to the master companion). Pin the
// mirrors to the real macros here, where pixdefs.h is available.
static_assert(PIX_AIR == 134 && PIX_ZSTAIR_UP == 140 && PIX_GRASS1 == 1,
              "scenario_table.h kPix* mirrors drifted from core/pixdefs.h");

namespace og::parity {

int scenario_level_id(std::string_view scenario_file)
{
    // Strip directory components.
    const std::size_t slash = scenario_file.find_last_of('/');
    std::string_view base = (slash == std::string_view::npos)
                                ? scenario_file
                                : scenario_file.substr(slash + 1);

    // Match "scen<digits>.fss".
    constexpr std::string_view prefix = "scen";
    constexpr std::string_view suffix = ".fss";
    if (base.size() <= prefix.size() + suffix.size()) return -1;
    if (base.substr(0, prefix.size()) != prefix) return -1;
    if (base.substr(base.size() - suffix.size()) != suffix) return -1;

    const std::string_view digits = base.substr(
        prefix.size(), base.size() - prefix.size() - suffix.size());
    int value = 0;
    for (char c : digits)
    {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

void apply_post_load_spawns(GameWorld& world, const ScenarioSpec& spec)
{
    for (std::size_t i = 0; i < spec.spawn_count; ++i)
    {
        const SpawnSpec& s = spec.spawns[i];
        walker* w = world.add_ob(static_cast<Order>(s.order), s.family,
                                 /*atstart=*/true);
        if (w == nullptr) continue;


        // setxy runs the passability check and updates the spatial index;
        // set_xpos / set_ypos alone leave the walker in the obmap at its
        // construction-time origin and the simulator immediately relocates
        // it back to (-1, -1) on the first act() pass.
        w->setxy(static_cast<short>(s.x), static_cast<short>(s.y));
        w->set_team_num(s.team);
        w->set_real_team_num(s.team);
        // Z-axis: relocate to a stacked floor (re-buckets in the floor-keyed
        // obmap). floor_count was already raised by apply_floor_setup, so a
        // non-zero target floor is valid here. No-op for floor 0 (the default).
        if (s.floor != 0)
            w->change_floor(static_cast<short>(s.floor));
        // Leave user_ at the SimEntity default (-1, NPC). The first call to
        // sim_process_player_input for the player_team walker takes ownership
        // and sets user_ = player_num + act_type = ACT_CONTROL — exactly the
        // takeover sequence the production game uses when a player picks up
        // a control. The harness never sets user_ directly.
        if (s.default_weapon != 0)
            w->set_default_weapon(s.default_weapon);
        if (s.current_weapon != 0)
            w->set_current_weapon(s.current_weapon);

        // Phase 01 (semantic-parity): caster preconditions for special slots
        // >= 2. The cycling gate sim_input_handler.cpp:218 requires
        // (N-1)*3+1 <= stats.level(); the firing gate living.cpp:532-533
        // requires magicpoints >= special_cost(current_special). Zero
        // defaults preserve byte-mirror layout for rows that don't need
        // either.
        if (w->stats() != nullptr)
        {
            if (s.stats_level != 0)
                w->stats()->set_level(s.stats_level);
            if (s.magicpoints != 0)
                w->stats()->set_magicpoints(static_cast<float>(s.magicpoints));
        }
        if (s.precompleted_level != 0)
            world.completed_levels.insert(s.precompleted_level);
    }
}

void apply_floor_setup(GameWorld& world, const ScenarioSpec& spec)
{
    if (spec.floor_count <= 1)
        return;

    // scen9301.fss is a 3-byte stub whose load fails, leaving floor 0 with an
    // empty 0x0 grid (the smoother then reads everything as grass and a paint
    // would be both out-of-bounds and invisible). Establish a grass field so
    // the painted Z tiles are in bounds and the smoother reads them live.
    // Single-floor parity rows never reach here (early-return above), so this
    // is parity-neutral.
    if (world.grid.w == 0 || world.grid.h == 0)
    {
        constexpr int kDim = 20;
        auto* buf = new unsigned char[static_cast<std::size_t>(kDim) * kDim];
        std::fill(buf, buf + static_cast<std::size_t>(kDim) * kDim,
                  static_cast<unsigned char>(PIX_GRASS1));
        world.grid = PixieData(1, static_cast<unsigned char>(kDim),
                               static_cast<unsigned char>(kDim), buf);
        world.pixmaxx = kDim * GRID_SIZE;
        world.pixmaxy = kDim * GRID_SIZE;
        world.mysmoother.set_target(world.grid);
    }

    world.set_floor_count(spec.floor_count);

    const int gw = world.grid.w;
    const int gh = world.grid.h;
    for (int f = 1; f < spec.floor_count; ++f)
    {
        auto* buf = new unsigned char[static_cast<std::size_t>(gw) * gh];
        std::fill(buf, buf + static_cast<std::size_t>(gw) * gh,
                  static_cast<unsigned char>(PIX_GRASS1));
        world.grid_for_floor(f) = PixieData(1, static_cast<unsigned char>(gw),
                                            static_cast<unsigned char>(gh), buf);
        world.smoother_for_floor(f).set_target(world.grid_for_floor(f));
    }

    for (std::size_t i = 0; i < spec.floor_paint_count; ++i)
    {
        const FloorPaint& p = spec.floor_paints[i];
        PixieData& g = world.grid_for_floor(p.floor);
        if (p.tile_x >= 0 && p.tile_y >= 0 &&
            p.tile_x < g.w && p.tile_y < g.h)
            g.data[static_cast<std::size_t>(p.tile_x) +
                   static_cast<std::size_t>(p.tile_y) *
                       static_cast<std::size_t>(g.w)] =
                static_cast<unsigned char>(p.pix);
    }
}

namespace {

walker* find_player_walker(GameWorld& world, std::uint8_t player_team)
{
    for (const auto& uptr : world.oblist)
    {
        if (uptr && !uptr->dead() &&
            uptr->query_order() == Order::Living &&
            uptr->team_num() == player_team)
            return uptr.get();
    }
    return nullptr;
}

void claim_control(GameWorld& world,
                   const ScenarioSpec& spec,
                   ScenarioInputDriver& driver)
{
    if (driver.control == nullptr || driver.control->dead())
        driver.control = find_player_walker(world, spec.player_team);
    if (driver.control == nullptr)
        return;
    if (driver.control->user() == -1)
    {
        driver.control->set_act_type(ACT_CONTROL);
        driver.control->set_user(0);
        if (driver.control->stats() != nullptr)
            driver.control->stats()->clear_command();
    }
    driver.initialised = true;
}

bool held(std::uint32_t mask, std::uint32_t bit)
{
    return (mask & bit) != 0;
}

void cycle_next_character(GameWorld& world,
                          const ScenarioSpec& spec,
                          ScenarioInputDriver& driver)
{
    walker* old = driver.control;
    if (old == nullptr) return;

    if (old->user() == 0)
    {
        old->restore_act_type();
        old->set_user(-1);
    }

    bool seen_old = false;
    walker* next = nullptr;
    auto accept = [&](walker* candidate) {
        return candidate != nullptr &&
               candidate->query_order() == Order::Living &&
               candidate->is_friendly(old) &&
               candidate->team_num() == spec.player_team &&
               candidate->real_team_num() == 255 &&
               candidate->user() == -1;
    };

    for (const auto& uptr : world.oblist)
    {
        walker* candidate = uptr.get();
        if (candidate == old)
        {
            seen_old = true;
            continue;
        }
        if (seen_old && accept(candidate))
        {
            next = candidate;
            break;
        }
    }

    if (seen_old && next == nullptr)
    {
        for (const auto& uptr : world.oblist)
        {
            walker* candidate = uptr.get();
            if (candidate == old) break;
            if (accept(candidate))
            {
                next = candidate;
                break;
            }
        }
    }

    if (next == nullptr) next = old;
    driver.control = next;
    claim_control(world, spec, driver);
}

void cycle_special(walker* control)
{
    if (control == nullptr || control->stats() == nullptr) return;

    control->set_current_special(control->current_special() + 1);
    const int special_index = static_cast<int>(control->current_special());
    const auto* descriptor = get_family_descriptor(
        static_cast<int>(static_cast<unsigned char>(control->family())));
    const char* special_name =
        (!descriptor || special_index < 0 || special_index >= NUM_SPECIALS)
            ? nullptr
            : descriptor->special_names[special_index];
    const bool missing_special =
        descriptor == nullptr ||
        special_index < 0 ||
        special_index >= NUM_SPECIALS ||
        special_name == nullptr ||
        std::strcmp(special_name, "NONE") == 0;

    if (special_index > (NUM_SPECIALS - 1) ||
        missing_special ||
        (((control->current_special() - 1) * 3 + 1) >
         control->stats()->level()))
        control->set_current_special(1);
}

} // namespace

void apply_inputs_at_tick(GameWorld& world,
                          const ScenarioSpec& spec,
                          std::uint32_t tick,
                          ScenarioInputDriver& driver,
                          og::sim::SimEventLog* sim_events)
{
    (void)sim_events;

    // Update the driver's held mask from any input event at this tick.
    // Multiple matching events collapse to the last one (callers should
    // not write the same tick twice; if they do, last write wins).
    if (spec.inputs != nullptr)
    {
        for (std::size_t i = 0; i < spec.input_count; ++i)
        {
            if (spec.inputs[i].tick == tick && spec.inputs[i].player_id == 0)
            {
                driver.held_mask = spec.inputs[i].key_mask;
            }
        }
    }

    claim_control(world, spec, driver);
    if (driver.control == nullptr)
    {
        driver.prev_mask = driver.held_mask;
        return;
    }

    const std::uint32_t pressed = driver.held_mask & ~driver.prev_mask;
    if (held(pressed, K_SWITCH))
    {
        cycle_next_character(world, spec, driver);
    }
    walker* control = driver.control;
    if (control == nullptr || control->dead())
    {
        driver.prev_mask = driver.held_mask;
        return;
    }

    control->set_shifter_down(held(driver.held_mask, K_SHIFT) ? 1 : 0);

    if (held(pressed, K_SPECIAL_SWITCH))
        cycle_special(control);

    if (control->stats() != nullptr && control->stats()->commands.empty())
    {
        if (held(driver.held_mask, K_SPECIAL))
            control->special();

        int walkx = 0;
        int walky = 0;
        if (held(driver.held_mask, K_UP) || held(driver.held_mask, K_UP_LEFT) ||
            held(driver.held_mask, K_UP_RIGHT))
            walky = -1;
        else if (held(driver.held_mask, K_DOWN) ||
                 held(driver.held_mask, K_DOWN_LEFT) ||
                 held(driver.held_mask, K_DOWN_RIGHT))
            walky = 1;

        if (held(driver.held_mask, K_LEFT) || held(driver.held_mask, K_UP_LEFT) ||
            held(driver.held_mask, K_DOWN_LEFT))
            walkx = -1;
        else if (held(driver.held_mask, K_RIGHT) ||
                 held(driver.held_mask, K_DOWN_RIGHT) ||
                 held(driver.held_mask, K_UP_RIGHT))
            walkx = 1;

        if (walkx != 0 || walky != 0)
            control->walkstep(static_cast<float>(walkx), static_cast<float>(walky));

        if (held(driver.held_mask, K_FIRE))
            control->init_fire();
    }

    driver.prev_mask = driver.held_mask;
    driver.initialised = true;
}

void clear_world_entities(GameWorld& world)
{
    world.delete_objects();
}

} // namespace og::parity
