/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// A test-only class-pack declaration installed over the golem's registry
// slot (a family no New Specials kit uses), so the New Specials view and its
// readers can be exercised before any shipped family declares a new
// special. The slot is put back on the way out (descriptor and tuning), and
// the pack-store guard reinstalls the shipped packs, so the unit census sees
// the registry it started with.

#include "unit_pack_store_guard.h"

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/resources/packs.h>

#include <string>
#include <utility>

namespace og::test {

// The default probe: slot 1 is classic with a priced new-kit alternate (the
// thief's MINE shape), slot 2 is a new-kit slot with a priced alternate,
// slot 3 is classic with an unpriced classic alternate (the cleric's HEAL /
// MYSTIC MACE shape), slot 4 is a new-kit slot with no alternate.
inline constexpr const char* kProbeDecl =
    "og.family('living', { id = 'kitprobe:golem', wire_id = 18,\n"
    "  specials = {\n"
    "    { id = 'bomb', name = 'DROP BOMB', mp_cost = 35,\n"
    "      alternate = { name = 'MINE', mp_cost = 70, new_kit = true } },\n"
    "    { id = 'dig', name = 'DIG IN', mp_cost = 40, new_kit = true,\n"
    "      alternate = { name = 'BONE STORM', mp_cost = 90 } },\n"
    "    { id = 'heal', name = 'HEAL', mp_cost = 2,\n"
    "      alternate = { name = 'MYSTIC MACE' } },\n"
    "    { id = 'phase', name = 'PHASE', mp_cost = 60, new_kit = true },\n"
    "  } })\n";

// A family that has NO classic special at all, only new-kit ones (the
// faerie's and the orc captain's shape): every slot is hidden with the
// setting off.
inline constexpr const char* kAllNewProbeDecl =
    "og.family('living', { id = 'kitprobe:golem', wire_id = 18,\n"
    "  specials = {\n"
    "    { id = 'blink', name = 'BLINK', mp_cost = 10, new_kit = true,\n"
    "      alternate = { name = 'SWAP', mp_cost = 25 } },\n"
    "    { id = 'glimmer', name = 'GLIMMER', mp_cost = 20, new_kit = true },\n"
    "    { id = 'hasten', name = 'HASTEN', mp_cost = 30, new_kit = true },\n"
    "    { id = 'wish', name = 'WISH', mp_cost = 200, new_kit = true },\n"
    "  } })\n";

// Evaluates one declaration chunk the way the loader does. The chunk store
// is cleared on the way in and out; the owner's ScopedPackStoreState puts
// the shipped chunks back.
inline og::script::DeclareResult declare(const std::string& lua,
                                         og::data::ClasspackData& out)
{
    og::script::clear_pack_family_chunks();
    og::script::register_pack_family_chunk(
        {"kitprobe", "kitprobe/families/a.lua", lua});
    og::script::DeclareResult r =
        og::script::declare_pack_families("kitprobe", out);
    og::script::clear_pack_family_chunks();
    return r;
}

class ProbeFamily {
public:
    explicit ProbeFamily(const char* decl = kProbeDecl)
        : saved_(*get_family_descriptor(FAMILY_GOLEM))
    {
        if (const og::script::TuningMap* tuning =
                og::script::family_tuning(Order::Living, FAMILY_GOLEM))
            saved_tuning_ = *tuning;
        og::data::ClasspackData data;
        const og::script::DeclareResult r = declare(decl, data);
        EXPECT_TRUE(r.ok) << r.error;
        installed_ = og::resources::install_classpack_data(std::move(data));
    }
    ~ProbeFamily()
    {
        set_family_descriptor(FAMILY_GOLEM, saved_);
        og::script::set_family_tuning(Order::Living, FAMILY_GOLEM,
                                      saved_tuning_);
    }
    ProbeFamily(const ProbeFamily&) = delete;
    ProbeFamily& operator=(const ProbeFamily&) = delete;

    [[nodiscard]] int installed() const { return installed_; }
    [[nodiscard]] static const FamilyDescriptor* fd()
    {
        return get_family_descriptor(FAMILY_GOLEM);
    }

private:
    og::test::ScopedPackStoreState pack_store_;
    FamilyDescriptor saved_;
    og::script::TuningMap saved_tuning_;
    int installed_ = 0;
};

}  // namespace og::test
