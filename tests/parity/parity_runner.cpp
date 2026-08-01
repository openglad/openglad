#include "parity_runner.h"

#include "scenario_runtime.h"

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// `cfg` is the file-scope cfg_store in data/gparser.cpp; declared here so
// the runner can install it into the ScopedGameplayContext. Do NOT
// default-construct a local cfg_store — gameplay queries it during
// load() and tick().
extern cfg_store cfg;

namespace og::parity {

namespace {

// Test-side libc-rand adapter used to replay e761's single global RNG stream.
// Calls corresponding to e761's random(x) helper update the observable hash
// stored in schema-v1 `rng_state`; direct rand()%N sites share the same libc
// stream but deliberately do not update that hash.
class LibcRandRandom : public IRandom
{
public:
    explicit LibcRandRandom(bool observe_random_calls)
        : observe_random_calls_(observe_random_calls)
    {
    }

    void seed_observable(std::uint32_t seed)
    {
        observable_state_ = seed;
    }

    std::uint32_t observable_state() const
    {
        return observable_state_;
    }

    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0) return 0;
        const std::uint32_t value =
            static_cast<std::uint32_t>(std::rand()) % max_exclusive;
        if (observe_random_calls_)
        {
            observable_state_ ^= max_exclusive + 0x9E3779B9u
                + (observable_state_ << 6) + (observable_state_ >> 2);
            observable_state_ ^= value + 0x85EBCA6Bu
                + (observable_state_ << 13) + (observable_state_ >> 7);
        }
        return value;
    }

private:
    bool observe_random_calls_ = false;
    std::uint32_t observable_state_ = 0;
};

// Test-side ScopedGameplayContext mirror. Cannot reuse the one declared in
// tests/test_gameplay_context_scope.h because including it would pull in the
// rest of the integration-test fixture support; we only need to install the
// current_game pointer for the duration of `level.load()` and the tick loop.
class ScopedGameplayContext
{
public:
    ScopedGameplayContext(GameWorld& world, SaveData& save,
                          og::sim::SimEventLog& events, cfg_store& config,
                          GameplayContext::PositionalSoundVisibleFn visible = nullptr,
                          void* visible_user = nullptr)
        : previous_(current_game)
    {
        context_.world      = &world;
        context_.save       = &save;
        context_.sim_events = &events;
        context_.config     = &config;
        context_.positional_sound_visible = visible;
        context_.positional_sound_visible_user = visible_user;
        current_game        = &context_;
    }

    ~ScopedGameplayContext()
    {
        current_game = previous_;
    }

    ScopedGameplayContext(const ScopedGameplayContext&) = delete;
    ScopedGameplayContext& operator=(const ScopedGameplayContext&) = delete;
    ScopedGameplayContext(ScopedGameplayContext&&) = delete;
    ScopedGameplayContext& operator=(ScopedGameplayContext&&) = delete;

private:
    GameplayContext  context_{};
    GameplayContext* previous_ = nullptr;
};

bool classic_fullscreen_view_contains(const walker* source,
                                      std::uint32_t,
                                      void*)
{
    if (source == nullptr)
        return true;

    constexpr std::int32_t kTopX = 0;
    constexpr std::int32_t kTopY = 0;
    constexpr std::int32_t kXView = 320;
    constexpr std::int32_t kYView = 200;

    if (source->xpos() + source->sizex() < kTopX)
        return false;
    if (source->xpos() > kTopX + kXView)
        return false;
    if (source->ypos() + source->sizey() < kTopY)
        return false;
    if (source->ypos() > kTopY + kYView)
        return false;
    return true;
}

bool is_classic_display_text_notification(std::string_view text)
{
    static constexpr std::array<std::string_view, 15> kExactMessages = {
        "SOLDIER SLAIN",
        "ARCHER DIED",
        "THIEF KILLED",
        "ELF KILLED",
        "MAGE DIED",
        "SKELETON CRUMBLED",
        "CLERIC DIED",
        "FIRE ELEMENTAL EXTINGUISHED",
        "FAERIE POPPED",
        "SLIME DESTROYED",
        "GHOST VANISHED",
        "DRUID VANQUISHED",
        "ORC DIED",
        "SOMEONE DIED",
        "All foes defeated!",
    };

    for (std::string_view message : kExactMessages)
    {
        if (text == message)
            return true;
    }

    constexpr std::string_view enemy_prefix = "ENEMY DEATH: ";
    constexpr std::string_view died_suffix = " DIED!";
    if (text.rfind(enemy_prefix, 0) == 0 &&
        text.size() > enemy_prefix.size() + died_suffix.size() &&
        text.compare(text.size() - died_suffix.size(),
                     died_suffix.size(), died_suffix) == 0)
        return true;

    constexpr std::string_view dispelled_suffix = " Dispelled!";
    if (text.size() > dispelled_suffix.size() &&
        text.compare(text.size() - dispelled_suffix.size(),
                     dispelled_suffix.size(), dispelled_suffix) == 0)
        return true;

    constexpr std::string_view player_died_suffix = " Died!";
    if (text.size() > player_died_suffix.size() &&
        text.compare(text.size() - player_died_suffix.size(),
                     player_died_suffix.size(), player_died_suffix) == 0)
        return true;

    constexpr std::string_view time_left_prefix = "TIME LEFT: ";
    if (text.rfind(time_left_prefix, 0) == 0)
        return true;

    // Master's off-team FREEZE TIME arm (walker.cpp:2984-3000) announces the
    // grant through viewob[0]->set_display_text(), which the master recorder
    // never sees -- only screen::do_notify writes a Notification. The branch
    // emits it as a real sim Notification, so it is dropped here for the same
    // reason as the "TIME LEFT: " countdown above.
    constexpr std::string_view time_frozen_prefix = "TIME IS FROZEN!";
    if (text.rfind(time_frozen_prefix, 0) == 0)
        return true;

    return false;
}

void normalize_classic_notification_metadata(og::sim::Event& ev)
{
    if (ev.text == "THIEF: 'Nyah Nyah!'")
    {
        ev.a = FAMILY_THIEF;
        ev.b = 0;
    }
    else if (ev.text.rfind("ArchMage has controlled ", 0) == 0)
    {
        ev.a = FAMILY_ARCHMAGE;
        ev.b = 0;
    }
    else if (ev.text.rfind("Druid protected ", 0) == 0)
    {
        ev.a = FAMILY_DRUID;
        ev.b = 0;
    }
}

void append_events_for_tick(og::sim::SimEventLog& dst,
                            std::vector<og::sim::Event> events,
                            std::uint32_t tick,
                            bool drop_score_change = false)
{
    dst.current_tick_ = tick;
    std::array<std::uint32_t, 4> score_delta{};
    std::array<bool, 4> score_seen{};
    for (const auto& ev : events)
    {
        if (drop_score_change &&
            ev.kind == og::sim::EventKind::ScoreChange)
            continue;
        og::sim::Event normalized = ev;
        if (normalized.kind == og::sim::EventKind::DamageTile)
            continue;
        // The only sim-side RequestRedraw emitter is mage.lua:200 (the
        // off-team FREEZE TIME arm), standing in for master's bare
        // redraw()/refresh() pair, which the master recorder cannot observe.
        // Drop it for the same reason DamageTile is dropped. Coverage for the
        // kind is registered explicitly on the level_done == 2 path below.
        if (normalized.kind == og::sim::EventKind::RequestRedraw)
            continue;
        if (normalized.kind == og::sim::EventKind::ScoreChange &&
            normalized.a < score_delta.size())
        {
            score_delta[normalized.a] += normalized.b;
            score_seen[normalized.a] = true;
            continue;
        }
        if (normalized.kind == og::sim::EventKind::SetPalette)
        {
            normalized.a = 0;
            normalized.b = 0;
        }
        if (normalized.kind == og::sim::EventKind::Notification)
        {
            if (is_classic_display_text_notification(normalized.text))
                continue;
            normalize_classic_notification_metadata(normalized);

            constexpr std::string_view prefix = "Weapon ";
            constexpr std::string_view suffix = " doing act random?";
            if (normalized.text.rfind(prefix, 0) == 0 &&
                normalized.text.size() > prefix.size() + suffix.size() &&
                normalized.text.compare(normalized.text.size() - suffix.size(),
                                        suffix.size(), suffix) == 0)
            {
                std::uint32_t family = 0;
                bool saw_digit = false;
                const auto begin = prefix.size();
                const auto end = normalized.text.size() - suffix.size();
                for (std::size_t i = begin; i < end; ++i)
                {
                    const unsigned char ch =
                        static_cast<unsigned char>(normalized.text[i]);
                    if (!std::isdigit(ch))
                    {
                        saw_digit = false;
                        break;
                    }
                    saw_digit = true;
                    family = family * 10u + static_cast<std::uint32_t>(ch - '0');
                }
                if (saw_digit)
                {
                    normalized.a = family;
                }
            }
        }
        if (!normalized.text.empty())
            dst.push_with_text(normalized.kind, normalized.text,
                               normalized.a, normalized.b);
        else
            dst.push(normalized.kind, normalized.a, normalized.b);
    }

    for (std::size_t team = 0; team < score_delta.size(); ++team)
    {
        if (!score_seen[team])
            continue;
        dst.push(og::sim::EventKind::ScoreChange,
                 static_cast<std::uint32_t>(team),
                 score_delta[team]);
    }
}

void append_screen_completion_events(og::sim::SimEventLog& dst,
                                     GameWorld& world,
                                     std::uint32_t tick,
                                     bool& screen_end)
{
    if (world.level_done != 2)
        return;

    dst.current_tick_ = tick;
    dst.push(og::sim::EventKind::EndGame,
             static_cast<std::uint32_t>(world.ending),
             static_cast<std::uint32_t>(world.next_level));
    if (!screen_end)
    {
        dst.push(og::sim::EventKind::SetEnd,
                 static_cast<std::uint32_t>(world.ending),
                 static_cast<std::uint32_t>(world.next_level));
        world.end = 1;
        screen_end = true;
    }
}

std::string withdraw_destination_text(const std::string& prompt)
{
    constexpr std::string_view prefix = "Withdraw to ";
    if (prompt.rfind(prefix, 0) != 0)
        return {};
    std::string result = prompt.substr(prefix.size());
    if (!result.empty() && result.back() == '?')
        result.pop_back();
    return result;
}

void mark_livings_dead_for_withdraw(GameWorld& world)
{
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->query_order() != Order::Living)
            continue;
        w->set_dead(1);
        if (world.myobmap)
            world.myobmap->remove(w);
    }
}

void emulate_classic_screen_flow(GameWorld& world,
                                 std::vector<og::sim::Event>& events,
                                 std::uint32_t tick,
                                 bool& screen_end,
                                 short& screen_next_level)
{
    og::sim::Event* first_exit_request = nullptr;
    og::sim::Event* first_withdraw = nullptr;
    for (auto& ev : events)
    {
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation &&
            first_exit_request == nullptr)
            first_exit_request = &ev;
        else if (ev.kind == og::sim::EventKind::WithdrawToLevel &&
                 first_withdraw == nullptr)
            first_withdraw = &ev;
    }
    if (first_exit_request == nullptr)
        return;

    const auto destination =
        static_cast<std::uint32_t>(first_exit_request->a);
    const bool is_withdraw = first_exit_request->b != 0;
    if (first_withdraw != nullptr && first_withdraw->text.empty())
        first_withdraw->text = withdraw_destination_text(first_exit_request->text);

    if (is_withdraw)
    {
        mark_livings_dead_for_withdraw(world);
        world.level_done = 2;
        world.ending = 0;
        world.next_level = static_cast<short>(destination);
        screen_next_level = static_cast<short>(destination);
        og::sim::Event end_event;
        end_event.tick = tick;
        end_event.kind = og::sim::EventKind::EndGame;
        end_event.a = 1;
        end_event.b = destination;
        events.push_back(std::move(end_event));
        screen_end = true;
        return;
    }

    world.level_done = 2;
    world.ending = 0;
    world.next_level = static_cast<short>(destination);
    screen_next_level = static_cast<short>(destination);
}

} // namespace

RunOutcome run_scenario(const ScenarioSpec& spec)
{
    RunOutcome out;

    std::srand(spec.rng_seed);
    LibcRandRandom gameplay_rng_backing(/*observe_random_calls=*/true);
    LibcRandRandom cosmetic_rng_backing(/*observe_random_calls=*/false);
    gameplay_rng_backing.seed_observable(spec.rng_seed);
    IRandom*       gameplay_rng_slot = &gameplay_rng_backing;
    IRandom*       cosmetic_rng_slot = &cosmetic_rng_backing;
    struct RngOverrideGuard {
        explicit RngOverrideGuard(IRandom** gameplay, IRandom** cosmetic)
        {
            set_gameplay_rng_override(gameplay);
            set_cosmetic_rng_override(cosmetic);
#ifdef TESTING
            og::sim::set_sim_random_override(gameplay);
#endif
        }
        ~RngOverrideGuard()
        {
#ifdef TESTING
            og::sim::set_sim_random_override(nullptr);
#endif
            set_cosmetic_rng_override(nullptr);
            set_gameplay_rng_override(nullptr);
        }
    } rng_override_guard{&gameplay_rng_slot, &cosmetic_rng_slot};

    const int level_id = scenario_level_id(spec.scenario_file);
    LevelRuntimeData level(level_id, /*headless=*/true,
                           &sdl_level_data_hooks());

    SaveData save;
    og::sim::SimEventLog events;
    og::sim::SimEventLog parity_events;

    level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                          &level.world().rng_, &::cfg);

    ScopedGameplayContext ctx(level.world(), save, events, ::cfg,
                              classic_fullscreen_view_contains);

    out.loaded = level.load();

    GameWorld& world = level.world();
    world.my_team = spec.player_team;

    if (spec.fresh_arena)
        clear_world_entities(world);

    // Build the multi-floor arena (grids + floor paints) before spawning, so
    // spawns can be relocated onto floors that exist. No-op for single-floor
    // scenarios (floor_count == 1), keeping every existing golden byte-identical.
    apply_floor_setup(world, spec);

    apply_post_load_spawns(world, spec);
    events.clear();
    std::srand(spec.rng_seed);
    gameplay_rng_backing.seed_observable(spec.rng_seed);

    // Phase 03 coverage observation. The schema-v1 dump only carries
    // oblist (without per-walker order) and fxlist, but the coverage
    // gate needs to know which weapon / treasure / generator families
    // were instantiated over the lifetime of the run. Walkers that die
    // mid-run get cleaned out of oblist before end-of-run, so we sample
    // every list at every tick rather than only at the end.
    auto bag_walker = [](const walker* w, CoverageObservation& obs) {
        if (w == nullptr) return;
        const auto family = static_cast<std::int32_t>(w->family());
        switch (w->query_order())
        {
            case Order::Living:    obs.walker_families.insert(family); break;
            case Order::Weapon:    obs.weapon_families.insert(family); break;
            case Order::Treasure:  obs.treasure_families.insert(family); break;
            case Order::Generator: obs.generator_families.insert(family); break;
            case Order::FX:        obs.effect_families.insert(family); break;
            default:               break;
        }
    };
    auto sample_world = [&]() {
        for (const auto& uptr : world.oblist)
            bag_walker(uptr.get(), out.coverage);
        for (const auto& uptr : world.weaplist)
            bag_walker(uptr.get(), out.coverage);
        for (const auto& uptr : world.fxlist)
            bag_walker(uptr.get(), out.coverage);
    };
    auto bag_event_kinds = [&](const std::vector<og::sim::Event>& batch) {
        for (const auto& ev : batch)
            out.coverage.event_kinds.insert(
                event_kind_symbol(static_cast<std::uint32_t>(ev.kind)));
    };

    sample_world(); // post-spawn snapshot

    // Per-tick weapon trajectory sampling. `seq` is the intra-family spawn
    // index, assigned by first-appearance order of each weapon's entity_id
    // within its family so it stays stable across the weapon's lifetime and
    // matches the master side (which assigns seq the same way, keyed by the
    // walker pointer instead of entity_id — both arms spawn weapons in the
    // same deterministic order from the same rng_seed).
    std::vector<WeaponTrackSample> tracks;
    std::unordered_map<std::uint32_t /*entity_id*/, std::int32_t /*seq*/> seq_by_id;
    std::map<std::int32_t /*family*/, std::int32_t /*next seq*/> next_seq_per_family;
    std::map<std::int32_t /*family*/, std::int32_t /*next seq*/> fx_next_seq_per_family;
	    auto sample_weapon_tracks = [&]() {
	        for (const auto& uptr : world.weaplist)
	        {
            const walker* w = uptr.get();
            if (w == nullptr) continue;
            if (w->query_order() != Order::Weapon) continue;
            if (w->dead() != 0) continue;
            const auto fam = static_cast<std::int32_t>(w->family());
            const auto id  = w->entity_id();
            auto it = seq_by_id.find(id);
            std::int32_t seq;
            if (it == seq_by_id.end())
            {
                seq = next_seq_per_family[fam]++;
                seq_by_id.emplace(id, seq);
            }
            else
            {
                seq = it->second;
            }
            WeaponTrackSample s;
            s.tick     = world.tick_count_;
            s.family   = family_symbol_by_order(
                static_cast<std::int32_t>(Order::Weapon), fam);
            s.seq      = seq;
            s.xpos     = static_cast<std::int32_t>(w->xpos());
            s.ypos     = static_cast<std::int32_t>(w->ypos());
            s.lifetime = static_cast<std::int32_t>(w->lifetime());
            tracks.push_back(std::move(s));
        }
        // FX-order projectiles (e.g. FAMILY_BOOMERANG) are summoned via add_ob,
        // which routes every non-weapon Order into oblist (not fxlist) — so they
        // were invisible to weapon_tracks. Sample Order::FX walkers from oblist
        // into the same vector, resolved in the Order::FX family namespace
        // (distinct strings from weapon families, so weapon predicates ignore
        // them). A separate per-family seq counter keeps FX family ids from
        // colliding with weapon family ids; the weaplist loop above is unchanged.
        for (const auto& uptr : world.oblist)
        {
            const walker* w = uptr.get();
            if (w == nullptr) continue;
            if (w->query_order() != Order::FX) continue;
            if (w->dead() != 0) continue;
            const auto fam = static_cast<std::int32_t>(w->family());
            const auto id  = w->entity_id();
            auto it = seq_by_id.find(id);
            std::int32_t seq;
            if (it == seq_by_id.end())
            {
                seq = fx_next_seq_per_family[fam]++;
                seq_by_id.emplace(id, seq);
            }
            else
            {
                seq = it->second;
            }
            WeaponTrackSample s;
            s.tick     = world.tick_count_;
            s.family   = family_symbol_by_order(
                static_cast<std::int32_t>(Order::FX), fam);
            s.seq      = seq;
            s.xpos     = static_cast<std::int32_t>(w->xpos());
            s.ypos     = static_cast<std::int32_t>(w->ypos());
            s.lifetime = static_cast<std::int32_t>(w->lifetime());
            tracks.push_back(std::move(s));
	        }
	    };

	    ScenarioInputDriver input_driver;
	    bool parity_screen_end = false;
	    short parity_screen_next_level = -32768;
	    for (std::uint32_t t = 0; t < spec.tick_budget; ++t)
	    {
	        events.current_tick_ = t;
        apply_inputs_at_tick(world, spec, t, input_driver, &events);
        auto input_events = events.drain();
        emulate_classic_screen_flow(world, input_events, t, parity_screen_end,
                                    parity_screen_next_level);
        bag_event_kinds(input_events);
	        append_events_for_tick(parity_events, std::move(input_events), t,
	                               /*drop_score_change=*/true);
	        world.tick();
	        if (parity_screen_next_level != -32768)
        {
            world.next_level = parity_screen_next_level;
            world.ending = 0;
        }
        auto tick_events = events.drain();
        emulate_classic_screen_flow(world, tick_events, t, parity_screen_end,
                                    parity_screen_next_level);
        bag_event_kinds(tick_events);
        if (parity_screen_next_level != -32768)
        {
            world.next_level = parity_screen_next_level;
            world.ending = 0;
        }
        append_events_for_tick(parity_events, std::move(tick_events), t);
        append_screen_completion_events(parity_events, world, t, parity_screen_end);
        if (world.level_done == 2)
        {
            // Classic completion invalidates/redraws the screen directly.
            // Count the public RequestRedraw event kind for coverage, but keep
            // it out of parity_events so golden dumps remain classic-compatible.
            out.coverage.event_kinds.insert(event_kind_symbol(
                static_cast<std::uint32_t>(og::sim::EventKind::RequestRedraw)));
        }
        sample_world();
        sample_weapon_tracks();
        // Phase 02 redo: do NOT break on level_done. Both sides drive the
        // full tick_budget so cadence comparison stays apples-to-apples;
        // level_done is recorded in the schema-v1 dump as a top-level
        // field for divergence reports to look at.
        if (world.level_done != 0 && !out.early_stopped)
        {
            out.early_stopped   = true;
            out.early_stop_tick = world.tick_count_;
        }
    }
    out.ticked = true;
    world.rng_.state_ = gameplay_rng_backing.observable_state();
    out.dump   = capture_state_dump(world, &parity_events, std::move(tracks));

    for (const auto& ev : parity_events.events())
        out.coverage.event_kinds.insert(
            event_kind_symbol(static_cast<std::uint32_t>(ev.kind)));

    return out;
}

} // namespace og::parity
