/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/core/constants.h>

// Common boilerplate for family registries: a fixed 256-slot array (one per
// wire byte) + init-once + bounds-checked getter + bounds-checked overwrite
// (classpack install).
//
// EVERY SLOT STARTS FREE. init() only lays down the order's defaults; nothing
// is populated until a mounted class pack installs it. The engine carries no
// family data of its own: core families arrive from packs/core/families/*.lua
// through exactly the same door as a mod's. A free
// slot is *never* visible through get() — it answers nullptr, exactly as an
// out-of-range id did before the registries grew — so every "descriptor ==
// nullptr means this family does not exist" check in the codebase keeps
// working. install_slot() is the one accessor that sees a free slot, because
// the classpack installer needs the defaults-initialised descriptor to patch
// and set() back.
//
// CORE_NUM is the span of wire ids the engine's own constants name
// (FAMILY_SOLDIER..FAMILY_TOWER1 and the
// other orders' equivalents). Two things use it: reset_mod_slots(), which
// frees only the ids above it, and first_unpopulated_core_slot(), the
// startup check that turns "the core pack is missing or malformed" into one
// diagnosable error instead of a half-populated registry.
//
// Usage:
//   static FamilyRegistryBase<MyDescriptor, NUM_CORE> s_registry;
//   s_registry.init(apply_defaults);            // once at startup
//   return s_registry.get(id);                  // in getter
//   return s_registry.install_slot(id);         // classpack install (read)
//   s_registry.set(id, descriptor);             // classpack install (write)
template <typename Descriptor, int CORE_NUM, int CAPACITY = NUM_FAMILY_SLOTS>
class FamilyRegistryBase {
public:
    static_assert(CORE_NUM >= 0, "core family count cannot be negative");
    static_assert(CORE_NUM <= CAPACITY,
                  "core families must fit inside the registry capacity");

    using DefaultsFn = void(*)(Descriptor&);
    using SeedFn = void(*)(Descriptor*);

    // `seed` is optional and writes fields that the pack format cannot
    // express yet (see family_registry.cpp's promotion formulas). It runs on
    // the defaults-initialised entries and does NOT populate anything: the
    // classpack installer copies the install slot, patches the declared
    // fields and sets it back, so a seeded field rides through the install
    // untouched.
    void init(DefaultsFn apply_defaults, SeedFn seed = nullptr) {
        if (initialized_)
            return;
        defaults_ = apply_defaults;
        for (int i = 0; i < CAPACITY; i++)
            reset_slot(i);
        if (seed != nullptr)
            seed(entries_);
        initialized_ = true;
    }

    [[nodiscard]] const Descriptor* get(int family_id) const {
        if (family_id < 0 || family_id >= CAPACITY)
            return nullptr;
        return populated_[family_id] ? &entries_[family_id] : nullptr;
    }

    // The slot a classpack install builds on: the live descriptor when the
    // slot is populated (so callbacks and undeclared data fields survive the
    // copy-patch-set round trip), the defaults-initialised blank when the
    // slot is still free. nullptr only for an id outside the capacity.
    [[nodiscard]] const Descriptor* install_slot(int family_id) const {
        if (family_id < 0 || family_id >= CAPACITY)
            return nullptr;
        return &entries_[family_id];
    }

    // Overwrites one slot (classpack install) and marks it populated, which
    // is what makes a mod family at an id >= CORE_NUM visible to get().
    // Callers copy the current descriptor, patch data fields, and set() it
    // back — entry ADDRESSES never change, so descriptor pointers held
    // elsewhere stay valid.
    // Returns false when family_id is outside the registry's capacity.
    bool set(int family_id, const Descriptor& d) {
        if (family_id < 0 || family_id >= CAPACITY)
            return false;
        entries_[family_id] = d;
        entries_[family_id].family_id = family_id;
        populated_[family_id] = true;
        return true;
    }

    // The lowest core id no pack has declared, or -1 when the whole core
    // span is installed. The startup check reports it: a missing or
    // malformed core pack must fail with one diagnostic naming the gap, not
    // hand the game a registry that answers nullptr for SOLDIER.
    [[nodiscard]] int first_unpopulated_core_slot() const {
        for (int i = 0; i < CORE_NUM; i++) {
            if (!populated_[i])
                return i;
        }
        return -1;
    }

    // Drops every pack-installed slot back to "free", leaving the core span
    // untouched. Called before a fresh install pass so the registries mirror
    // exactly the packs that are mounted now (a pack that went away must not
    // leave a family behind). The core span is deliberately sticky: it is
    // re-installed from the core pack moments later, and holding the last
    // good copy in the meantime keeps a stray reinstall (a campaign mount
    // that happens while packs/ is not mounted) from blanking the game.
    void reset_mod_slots() {
        for (int i = CORE_NUM; i < CAPACITY; i++) {
            if (!populated_[i])
                continue;
            reset_slot(i);
        }
    }

    [[nodiscard]] bool is_initialized() const { return initialized_; }

private:
    void reset_slot(int i) {
        entries_[i] = Descriptor{};
        entries_[i].family_id = i;
        if (defaults_ != nullptr)
            defaults_(entries_[i]);
        populated_[i] = false;
    }

    bool initialized_ = false;
    DefaultsFn defaults_ = nullptr;
    bool populated_[static_cast<unsigned>(CAPACITY)]{};
    Descriptor entries_[static_cast<unsigned>(CAPACITY)]{};
};
