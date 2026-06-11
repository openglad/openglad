/* Capture-the-flag constants.
 *
 * The CTF treasure families live here rather than under the family headings
 * in constants.h: the parity coverage manifest parses those headings, and the
 * CTF families are intentionally outside its required-coverage universe.
 */
#pragma once

#include <string_view>

namespace og {

// Order::Treasure families
inline constexpr int FAMILY_FLAG = 13;
inline constexpr int FAMILY_CTF_POINT = 14;

// Scenario type flag (mirrors GameWorld::TYPE_CTF)
inline constexpr int SCEN_TYPE_CTF = 8;

// The shipped CTF campaign id. Shared by the UI campaign predicate and the
// lobby server's team-sharing rule so client and server gates cannot drift.
inline constexpr std::string_view kCtfCampaignId = "org.openglad.ctf";

// The shipped default campaign id. Shared by the campaign-ordering helper
// and the lobby server's default settings/fallback sites.
inline constexpr std::string_view kDefaultCampaignId = "org.openglad.gladiator";

} // namespace og
