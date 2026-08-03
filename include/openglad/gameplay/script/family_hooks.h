/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Scripted family-hook dispatch. Call sites in the sim use the og::script::hooks
// helpers below instead of invoking descriptor function pointers directly:
// each helper tries the current world's registered Lua hook first, then the
// descriptor's optional C++ callback, and returns nullopt when neither exists
// (the caller keeps its default path). A Lua hook that ERRORS counts as absent
// for that dispatch, so an optional descriptor callback runs next. Identical
// on every peer, but it means a hook must fail BEFORE mutating sim state or
// not at all: a partial script run followed by a callback double-executes
// side effects.
// Script errors therefore belong at branch entry (design doc R9).
//
// Pack-installed descriptors carry no C++ callbacks: their Lua hooks are the
// only behavior implementation, so an erroring hook means nothing runs at
// all. The HookFailure latch below makes that failure visible to tests and
// operators.

#include <openglad/core/order.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class walker;
class living;
class statistics;
class guy;
class weap;
class effect;
class treasure;
class GameWorld;
struct FamilyDescriptor;
struct WeaponFamilyDescriptor;
struct EffectFamilyDescriptor;
struct TreasureFamilyDescriptor;

namespace og::script {

class ScriptHost;
struct WorldScriptsDetail;

// One bit position per hook, shared across orders (fits uint32_t).
enum class FamilyHook : std::uint8_t {
    // living
    DoSpecial = 0,
    CheckSpecialAi,
    HitResponse,
    SetDifficulty,
    LevelUp,
    OnDeath,
    OnActLiving,
    OnShoved,
    OnFireWeapon,
    HandleTeleport,
    OnCreate,
    CustomizeWeapon,
    OnAniComplete,
    OnMeleeHit,
    // weapon
    WeaponOnDeath,
    WeaponOnAnimate,
    WeaponOnHitTarget,
    // effect
    EffectOnAct,
    EffectOnDeath,
    // treasure
    TreasureOnEat,
    // generator
    GeneratorCustomizeSpawn,
    // living (appended so existing bit positions stay stable): a scripted
    // act override — returning true means the walker's act is DONE this
    // tick (the effect_on_act "true = handled" contract, for livings).
    OnActOverride,
    Count,
};

// Per-world scripting state: the ScriptHost plus which (order, family)
// pairs registered which hooks during pack-script replay. GameWorld owns
// one lazily; a process-global UI instance serves picker-side hooks that
// run outside any world (guy level-up / promotion math).
class WorldScripts {
public:
    WorldScripts();
    ~WorldScripts();
    WorldScripts(const WorldScripts&) = delete;
    WorldScripts& operator=(const WorldScripts&) = delete;

    ScriptHost& host();
    const ScriptHost& host() const;

    bool has_hook(Order order, int family_id, FamilyHook hook) const;

    // Called by the og.register_hooks binding during replay.
    void note_hook(Order order, int family_id, FamilyHook hook);

    // The pack_scripts_build_generation() (scripts + og.use lib modules)
    // this instance was built against.
    unsigned built_generation() const { return built_generation_; }

private:
    std::unique_ptr<ScriptHost> host_;
    std::unique_ptr<WorldScriptsDetail> detail_;
    // masks_[order][family] — bit per FamilyHook.
    std::array<std::array<std::uint32_t, 256>, 8> masks_{};
    unsigned built_generation_ = 0;
};

// The WorldScripts for hook dispatch right now: the current world's (lazily
// created), or the shared UI instance when no world context exists. Never
// null. The UI instance rebuilds itself when pack scripts change.
WorldScripts& active_world_scripts();

namespace hooks {

// Loud-failure latch for class-pack behavior. Pack-installed descriptors
// carry no C++ callbacks, so a hook error means *nothing happens*. Every
// failed family-hook dispatch is therefore traced
// ("script_error"), logged at error level, and counted here — so a test
// cannot pass while a hook is quietly failing. Assert
// `hook_failures().count == 0`, or diff the count around the code under test.
//
// This is process-global bookkeeping, never sim state: no hook, snapshot or
// peer ever reads it, so production stays deterministic and non-fatal.
struct HookFailure {
    std::uint64_t count = 0;   // failed dispatches since reset_hook_failures()
    std::string where;         // most recent, e.g. "hook:do_special"
    std::string message;       // most recent Lua error text (with traceback)
};

const HookFailure& hook_failures();
void reset_hook_failures();

// Living-family hooks (FamilyDescriptor). Each returns the hook's result,
// or nullopt when no hook (script or C++) ran.
std::optional<bool> do_special(const FamilyDescriptor* fd, walker* self);
std::optional<bool> check_special_ai(const FamilyDescriptor* fd, living* self);
bool hit_response(const FamilyDescriptor* fd, statistics* stats, walker* who);
bool set_difficulty(const FamilyDescriptor* fd, living* self,
                    std::uint32_t level);
bool level_up(const FamilyDescriptor* fd, guy* self, std::int32_t level_diff);
std::optional<bool> on_death(const FamilyDescriptor* fd, walker* self);
bool on_act_living(const FamilyDescriptor* fd, living* self);
// True when a registered on_act_override hook ran and returned true: the
// walker's act is handled for this tick (living::act returns immediately).
// Pure script hook — descriptors carry no C++ callback for it.
bool on_act_override(const FamilyDescriptor* fd, living* self);
bool on_shoved(const FamilyDescriptor* fd, walker* target);
std::optional<bool> on_fire_weapon(const FamilyDescriptor* fd, walker* self,
                                   walker* weapon);
std::optional<bool> handle_teleport(const FamilyDescriptor* fd, walker* self);
bool on_create(const FamilyDescriptor* fd, walker* self);
bool customize_weapon(const FamilyDescriptor* fd, walker* self,
                      walker* weapon);
std::optional<bool> on_ani_complete(const FamilyDescriptor* fd, walker* self);
bool on_melee_hit(const FamilyDescriptor* fd, walker* self, walker* target);

// Weapon-family hooks.
std::optional<bool> weapon_on_death(const WeaponFamilyDescriptor* wfd,
                                    weap* self);
std::optional<bool> weapon_on_animate(const WeaponFamilyDescriptor* wfd,
                                      weap* self);
bool weapon_on_hit_target(const WeaponFamilyDescriptor* wfd, walker* weapon,
                          walker* target, walker* owner);

// Effect-family hooks.
std::optional<bool> effect_on_act(const EffectFamilyDescriptor* efd,
                                  effect* self);
std::optional<bool> effect_on_death(const EffectFamilyDescriptor* efd,
                                    effect* self);

// Treasure-family hooks.
std::optional<bool> treasure_on_eat(const TreasureFamilyDescriptor* tfd,
                                    treasure* self, walker* eater);

// Generator-family hook: post-processes a freshly generated spawn after the
// descriptor-driven setup (level roll, lifetime, ani). Pure script hook —
// the C++ generator descriptors carry no callbacks.
bool generator_customize_spawn(int generator_family, walker* generator,
                               walker* spawn);

// Level-script hooks (og.register_level_hooks / og.set_entity_hooks). All
// are inert (cheap early-out) when no pack registered matching hooks, so
// script-less sims stay byte-identical.
void level_load(GameWorld* world);         // first tick of a level id
void level_tick(GameWorld* world);         // every tick, before entity acts
// Living + generator deaths. Passes the kill-attribution channel to Lua:
// on_entity_death(ent, killer, killer_team) — killer is the owner-chain-root
// Living stamped by the combat path within og::sim::kKillAttributionTicks
// of the death (nil when stale/absent/swept; killer_team -1 = environment).
// Per-entity og.set_entity_hooks on_death receives the same args.
void level_entity_death(walker* self);
void level_entity_spawn(walker* spawned);  // sim-authored living/generator

// Scripted-mode level hooks (TYPE_SCRIPTED):
// on_mode_init(level) dispatch; true iff a hook was registered for this
// level (exact or wildcard) AND it ran without error — the mode activation
// condition.
bool level_mode_init(GameWorld* world);
// on_mode_tick(level, tick) — post-act slot, called by mode_run_tick.
void level_mode_tick(GameWorld* world);
// on_respawn(ent) — dispatched by the respawn engine after an in-place
// scripted-mode revive; the hook repositions via anchors + probes.
void level_respawn(GameWorld* world, walker* revived);
// Pre-damage gate: on_damage(target, attacker, amount) -> nil keep /
// number replace (clamped >= 0) / false cancel. Returns the amount to
// apply, or -1 for a cancelled hit. Early-outs on the level_hook_kinds
// bit, so classic paths stay byte-identical.
short level_damage_gate(walker* target, walker* attacker, short amount);

}  // namespace hooks

}  // namespace og::script
