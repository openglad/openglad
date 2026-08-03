/* Shared campaign-id constants.
 *
 * kDefaultCampaignId relocated here when core/ctf_constants.h dissolved
 * with the CTF engine retirement (the CTF campaign id and treasure family
 * bytes died with it; the modes campaign claims wire ids 13/14 through its
 * own pack declarations).
 */
#pragma once

#include <string_view>

namespace og {

// The shipped default campaign id. Shared by the campaign-ordering helper
// and the lobby server's default settings/fallback sites.
inline constexpr std::string_view kDefaultCampaignId = "org.openglad.gladiator";

} // namespace og
