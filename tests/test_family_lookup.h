/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Reading a living family's descriptor from a test.
//
// Family data used to be compiled in: each family had a
// describe_family_<name>() provider in src/gameplay/families/, and tests
// called it directly. Design doc §9a stage B deleted those — every
// descriptor, core or mod, now arrives from a mounted class pack through
// og::resources::install_classpacks(). The registry lookup below is the
// direct replacement; tests/unit/unit_main.cpp mounts the core pack before
// the first test runs, so it is populated for every headless unit binary.

#include <gtest/gtest.h>

#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>

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
