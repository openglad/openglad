/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Family-behavior dispatch for tests.
//
// FamilyDescriptor behavior callbacks are null for pack-installed families;
// their behavior lives in class-pack Lua (packs/core/families/*.lua and the
// lib/ modules they use, for core).
// Tests go through og::script::hooks::*, the same dispatch door the sim uses.
//
// Unit binaries never run io_init, so they mount no packs and would see no
// hooks at all. mount_core_pack() installs the shipped packs/ tree the same
// way tests/unit/test_campaign_packs.cpp does (idempotent; safe to call from
// every helper). Integration binaries already mounted it in io_init, and the
// append mount is a no-op there.

#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/packs.h>

#include <string>

std::string get_asset_path();

namespace og::test {

// Make the core pack's Lua available to hook dispatch. Cheap after the
// first call; re-mounts if something cleared the script registry.
inline void mount_core_pack()
{
    if (!og::script::pack_scripts().empty())
        return;
    (void)og::resources::mount((get_asset_path() + "packs/").c_str(),
                               "packs/", 1);
    og::resources::refresh_pack_scripts();
}

// True when the family registered `hook` in Lua.
inline bool has_hook(const FamilyDescriptor& fd, og::script::FamilyHook hook)
{
    mount_core_pack();
    return og::script::active_world_scripts().has_hook(Order::Living,
                                                       fd.family_id, hook);
}

inline bool has_do_special(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::DoSpecial);
}

inline bool has_check_special_ai(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::CheckSpecialAi);
}

inline bool has_level_up(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::LevelUp);
}

inline bool has_set_difficulty(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::SetDifficulty);
}

inline bool has_on_shoved(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::OnShoved);
}

inline bool has_on_fire_weapon(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::OnFireWeapon);
}

inline bool has_handle_teleport(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::HandleTeleport);
}

inline bool has_customize_weapon(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::CustomizeWeapon);
}

inline bool has_hit_response(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::HitResponse);
}

inline bool has_on_act_living(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::OnActLiving);
}

inline bool has_on_create(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::OnCreate);
}

inline bool has_on_death(const FamilyDescriptor& fd)
{
    return has_hook(fd, og::script::FamilyHook::OnDeath);
}

// --- dispatch -------------------------------------------------------------
// Each helper mirrors the sim's call: hook result, or the caller's default
// when no hook ran. For a core family that means the pack is missing; the
// has_* premises above catch that case loudly.

inline bool do_special(const FamilyDescriptor& fd, walker* self)
{
    mount_core_pack();
    return og::script::hooks::do_special(&fd, self).value_or(false);
}

inline bool check_special_ai(const FamilyDescriptor& fd, living* self)
{
    mount_core_pack();
    return og::script::hooks::check_special_ai(&fd, self).value_or(false);
}

inline void level_up(const FamilyDescriptor& fd, guy* self,
                     std::int32_t level_diff)
{
    mount_core_pack();
    (void)og::script::hooks::level_up(&fd, self, level_diff);
}

inline void set_difficulty(const FamilyDescriptor& fd, living* self,
                           std::uint32_t level)
{
    mount_core_pack();
    (void)og::script::hooks::set_difficulty(&fd, self, level);
}

inline void on_shoved(const FamilyDescriptor& fd, walker* target)
{
    mount_core_pack();
    (void)og::script::hooks::on_shoved(&fd, target);
}

inline void customize_weapon(const FamilyDescriptor& fd, walker* self,
                             walker* weapon)
{
    mount_core_pack();
    (void)og::script::hooks::customize_weapon(&fd, self, weapon);
}

inline bool on_fire_weapon(const FamilyDescriptor& fd, walker* self,
                           walker* weapon)
{
    mount_core_pack();
    return og::script::hooks::on_fire_weapon(&fd, self, weapon)
        .value_or(true);
}

inline bool handle_teleport(const FamilyDescriptor& fd, walker* self)
{
    mount_core_pack();
    return og::script::hooks::handle_teleport(&fd, self).value_or(false);
}

inline void hit_response(const FamilyDescriptor& fd, statistics* stats,
                         walker* who)
{
    mount_core_pack();
    (void)og::script::hooks::hit_response(&fd, stats, who);
}

inline void on_create(const FamilyDescriptor& fd, walker* self)
{
    mount_core_pack();
    (void)og::script::hooks::on_create(&fd, self);
}

inline void on_act_living(const FamilyDescriptor& fd, living* self)
{
    mount_core_pack();
    (void)og::script::hooks::on_act_living(&fd, self);
}

inline bool on_death(const FamilyDescriptor& fd, walker* self)
{
    mount_core_pack();
    return og::script::hooks::on_death(&fd, self).value_or(false);
}

// --- weapon / effect / treasure -------------------------------------------
// The presence helpers below answer "does this family have behavior at all",
// counting either path: three weapon families (wave, door, animate) still
// have no Lua twin and keep their C++ callback, and these tests should not
// care which side implements them.

inline bool has_on_death(const WeaponFamilyDescriptor& wfd)
{
    mount_core_pack();
    return og::script::active_world_scripts().has_hook(
               Order::Weapon, wfd.family_id,
               og::script::FamilyHook::WeaponOnDeath) ||
           wfd.on_death != nullptr;
}

inline bool has_on_hit_target(const WeaponFamilyDescriptor& wfd)
{
    mount_core_pack();
    return og::script::active_world_scripts().has_hook(
               Order::Weapon, wfd.family_id,
               og::script::FamilyHook::WeaponOnHitTarget) ||
           wfd.on_hit_target != nullptr;
}

inline bool has_on_death(const EffectFamilyDescriptor& efd)
{
    mount_core_pack();
    return og::script::active_world_scripts().has_hook(
               Order::FX, efd.family_id,
               og::script::FamilyHook::EffectOnDeath) ||
           efd.on_death != nullptr;
}

inline bool has_on_act(const EffectFamilyDescriptor& efd)
{
    mount_core_pack();
    return og::script::active_world_scripts().has_hook(
               Order::FX, efd.family_id,
               og::script::FamilyHook::EffectOnAct) ||
           efd.on_act != nullptr;
}

inline bool on_death(const WeaponFamilyDescriptor& wfd, weap* self)
{
    mount_core_pack();
    return og::script::hooks::weapon_on_death(&wfd, self).value_or(false);
}

inline bool on_animate(const WeaponFamilyDescriptor& wfd, weap* self)
{
    mount_core_pack();
    return og::script::hooks::weapon_on_animate(&wfd, self).value_or(false);
}

inline bool on_hit_target(const WeaponFamilyDescriptor& wfd, walker* weapon,
                          walker* target, walker* owner)
{
    mount_core_pack();
    return og::script::hooks::weapon_on_hit_target(&wfd, weapon, target,
                                                    owner);
}

inline bool on_death(const EffectFamilyDescriptor& efd, effect* self)
{
    mount_core_pack();
    return og::script::hooks::effect_on_death(&efd, self).value_or(false);
}

inline bool on_act(const EffectFamilyDescriptor& efd, effect* self)
{
    mount_core_pack();
    return og::script::hooks::effect_on_act(&efd, self).value_or(false);
}

inline bool on_eat(const TreasureFamilyDescriptor& tfd, treasure* self,
                   walker* eater)
{
    mount_core_pack();
    return og::script::hooks::treasure_on_eat(&tfd, self, eater)
        .value_or(true);
}

// Loud-failure guard. With no C++ fallback left, an erroring hook is a
// silent no-op; assert on this so a test cannot pass through one.
// Usage: ScopedHookFailureGuard guard; ... ; guard.expect_none();
class ScopedHookFailureGuard {
public:
    ScopedHookFailureGuard() { og::script::hooks::reset_hook_failures(); }

    std::uint64_t count() const
    {
        return og::script::hooks::hook_failures().count;
    }

    const std::string& message() const
    {
        return og::script::hooks::hook_failures().message;
    }
};

}  // namespace og::test
