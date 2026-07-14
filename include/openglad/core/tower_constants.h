/* Tower Climb constants.
 *
 * Mirrors ctf_constants.h: the tower campaign id and its scen-id block live
 * here so the UI gates, the progression seam, and the HUD label cannot drift
 * apart. Floor N of a run is scen id kTowerGateLevel + N (monotonic — see
 * docs/tower-triple-design.md D2); "floors climbed" is always DERIVED from a
 * level id, never stored as a separate run counter.
 */
#pragma once

#include <string_view>

namespace og {

// The shipped tower campaign id (campaign.yaml `mode: tower`).
inline constexpr std::string_view kTowerCampaignId = "org.openglad.tower";

// The authored antechamber ("The Gate"); generated floors start just above.
inline constexpr int kTowerGateLevel = 700;
inline constexpr int kTowerFirstFloorLevel = 701;

} // namespace og
