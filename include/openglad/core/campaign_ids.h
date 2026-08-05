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
inline constexpr std::string_view kDefaultCampaignId = "gladiator";

// Retired reverse-DNS prefix. Shipped campaign and pack ids were once
// "org.openglad.<name>" / "org.openglad.<campaign>.<pack>"; they are bare
// names now ("gladiator", "modes.core"). Stored data written before the
// rename still carries prefixed ids.
inline constexpr std::string_view kLegacyIdPrefix = "org.openglad.";

// Normalize a campaign or pack id read from stored data (GTL saves and
// their per-campaign progress keys, replay headers, cloud payloads that
// re-enter through the save reader). Strips the literal kLegacyIdPrefix:
//   "org.openglad.gladiator"  -> "gladiator"     (campaign shape)
//   "org.openglad.modes.core" -> "modes.core"    (pack shape)
// Already-plain ids and foreign ids pass through unchanged, so the mapping
// is idempotent. An id that is exactly the prefix (empty tail) also passes
// through unchanged rather than collapsing to "". The result is a view into
// the argument. Writers never emit prefixed ids; this exists only so data
// written before the rename keeps loading.
[[nodiscard]] constexpr std::string_view normalize_legacy_id(std::string_view id)
{
    if (id.size() > kLegacyIdPrefix.size() && id.starts_with(kLegacyIdPrefix))
        return id.substr(kLegacyIdPrefix.size());
    return id;
}

} // namespace og
