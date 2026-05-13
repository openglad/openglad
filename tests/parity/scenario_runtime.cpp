#include "scenario_runtime.h"

#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/walker.h>

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

std::vector<const walker*>
apply_post_load_spawns(GameWorld& world, const ScenarioSpec& spec)
{
    std::vector<const walker*> spawned;
    spawned.reserve(spec.spawn_count);
    for (std::size_t i = 0; i < spec.spawn_count; ++i)
    {
        const SpawnSpec& s = spec.spawns[i];
        walker* w = world.add_ob(static_cast<Order>(s.order), s.family,
                                 /*atstart=*/true);
        if (w == nullptr) continue;
        spawned.push_back(w);

        // setxy runs the passability check and updates the spatial index;
        // set_xpos / set_ypos alone leave the walker in the obmap at its
        // construction-time origin and the simulator immediately relocates
        // it back to (-1, -1) on the first act() pass.
        w->setxy(static_cast<short>(s.x), static_cast<short>(s.y));
        w->set_team_num(s.team);
        w->set_real_team_num(s.team);
        // Leave user_ at the SimEntity default (-1, NPC). The first call to
        // sim_process_player_input for the player_team walker takes ownership
        // and sets user_ = player_num + act_type = ACT_CONTROL — exactly the
        // takeover sequence the production game uses when a player picks up
        // a control. The harness never sets user_ directly.
        if (s.default_weapon != 0)
            w->set_default_weapon(s.default_weapon);
        if (s.current_weapon != 0)
            w->set_current_weapon(s.current_weapon);
    }
    return spawned;
}

namespace {

walker* find_player_walker(GameWorld& world, std::uint8_t player_team)
{
    for (const auto& uptr : world.oblist)
    {
        if (uptr && uptr->team_num() == player_team)
            return uptr.get();
    }
    return nullptr;
}

PlayerInput build_player_input(std::uint32_t held_mask,
                               std::uint32_t prev_mask)
{
    PlayerInput pi;
    const std::uint32_t pressed_mask = held_mask & ~prev_mask;
    for (int i = 0; i < NUM_INPUT_KEYS; ++i)
    {
        const std::uint32_t bit = (1u << i);
        pi.held[i]    = (held_mask    & bit) != 0;
        pi.pressed[i] = (pressed_mask & bit) != 0;
    }
    return pi;
}

} // namespace

void apply_inputs_at_tick(GameWorld& world,
                          const ScenarioSpec& spec,
                          std::uint32_t tick,
                          ScenarioInputDriver& driver,
                          og::sim::SimEventLog* sim_events)
{
    // Update the driver's held mask from any input event at this tick.
    // Multiple matching events collapse to the last one (callers should
    // not write the same tick twice; if they do, last write wins).
    bool had_event = false;
    if (spec.inputs != nullptr)
    {
        for (std::size_t i = 0; i < spec.input_count; ++i)
        {
            if (spec.inputs[i].tick == tick && spec.inputs[i].player_id == 0)
            {
                driver.held_mask = spec.inputs[i].key_mask;
                had_event = true;
            }
        }
    }

    // Bind the control walker the first time we have any work to do.
    if (driver.control == nullptr)
        driver.control = find_player_walker(world, spec.player_team);
    if (driver.control == nullptr)
    {
        driver.prev_mask = driver.held_mask;
        return;
    }

    // If there is no event and nothing is held, skip the sim pass — it
    // would still run movement / animation, but with held=pressed=0 the
    // result is identical to the simulator's own per-tick behaviour and
    // we save the call overhead. This also keeps the no-input smoke
    // scenario byte-identical against the corresponding inputs-script
    // version when both scripts happen to be idle at the same tick.
    if (!had_event && driver.held_mask == 0 && driver.prev_mask == 0)
        return;

    // Route the input through the real player-input pipeline. This is the
    // same call the SDL game loop makes for each player every tick, and
    // it is what mutates the walker (fire, walkstep, special, animate,
    // etc.) — the harness never reaches past this call to mutate state
    // directly.
    PlayerInput pi = build_player_input(driver.held_mask, driver.prev_mask);
    walker*     control_io   = driver.control;
    const short player_num   = 0;
    const short my_team      = static_cast<short>(driver.control->team_num());
    sim_process_player_input(pi,
                             control_io,
                             world,
                             player_num,
                             my_team,
                             driver.debounce,
                             /*special_names=*/nullptr,
                             sim_events);
    // sim_process_player_input may reassign control (character switch /
    // death). Keep the driver pointed at the live one.
    driver.control   = control_io;
    driver.prev_mask = driver.held_mask;
    driver.initialised = true;
}

void clear_world_entities(GameWorld& world)
{
    world.delete_objects();
}

} // namespace og::parity
