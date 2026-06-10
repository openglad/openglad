/* Capture-the-flag constants.
 *
 * The CTF treasure families live here rather than under the family headings
 * in constants.h: the parity coverage manifest parses those headings, and the
 * CTF families are intentionally outside its required-coverage universe.
 */
#pragma once

namespace og {

// Order::Treasure families
inline constexpr int FAMILY_FLAG = 13;
inline constexpr int FAMILY_CTF_POINT = 14;

// Scenario type flag (mirrors GameWorld::TYPE_CTF)
inline constexpr int SCEN_TYPE_CTF = 8;

} // namespace og
