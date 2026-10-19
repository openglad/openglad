/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The bot gates of the cleric, the druid and the thief: each special a bot can roll, asked
// whether it pays off, with the Gameplay Fixes setting off (the classic
// answer) and on. The cases use the GameplayFixesBots fixture
// (bot_gate_fixture.h), shared with test_gameplay_fixes_bots.cpp.

#include <gtest/gtest.h>

#include "bot_gate_fixture.h"

using og::test::bot_gates::GameplayFixesBots;
