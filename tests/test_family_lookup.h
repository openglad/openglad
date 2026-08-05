/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Read a living family's class-pack-installed descriptor from a test.
// tests/unit/unit_main.cpp mounts the core pack before the first test runs,
// so the registry is populated for every headless unit binary.

#include <gtest/gtest.h>

#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>

inline const FamilyDescriptor& describe_family(int family_id)
{
    const FamilyDescriptor* d = get_family_descriptor(family_id);
    // nullptr means no mounted pack declares this family — with the core
    // pack missing every assertion in the caller would be meaningless, so
    // say so instead of dereferencing null.
    EXPECT_NE(d, nullptr) << "core class pack not installed: living family "
                          << family_id;
    static const FamilyDescriptor kBlank{};
    return d != nullptr ? *d : kBlank;
}
