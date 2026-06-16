/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Common boilerplate for family registries: static array + init-once
// + bounds-checked getter.
//
// Usage:
//   static FamilyRegistryBase<MyDescriptor, NUM> s_registry;
//   s_registry.init(apply_defaults, populate);  // once at startup
//   return s_registry.get(id);                  // in getter
template <typename Descriptor, int NUM>
class FamilyRegistryBase {
public:
    using DefaultsFn = void(*)(Descriptor&);
    using PopulateFn = void(*)(Descriptor*);

    void init(DefaultsFn apply_defaults, PopulateFn populate) {
        if (initialized_)
            return;
        for (int i = 0; i < NUM; i++) {
            entries_[i].family_id = i;
            if (apply_defaults)
                apply_defaults(entries_[i]);
        }
        populate(entries_);
        initialized_ = true;
    }

    [[nodiscard]] const Descriptor* get(int family_id) const {
        return (family_id >= 0 && family_id < NUM) ? &entries_[family_id] : nullptr;
    }

    [[nodiscard]] bool is_initialized() const { return initialized_; }

private:
    bool initialized_ = false;
    Descriptor entries_[NUM]{};
};
