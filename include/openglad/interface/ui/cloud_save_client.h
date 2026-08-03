/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Cloud saves (issue #155): the pure, SDL-free client half of the
// passphrase-keyed save sync against the relay's /api/save/<KEY> endpoints
// (relay/src/save-vault.ts). Compiled into og_interface AND the terminal
// clients; everything here is headlessly unit-testable. HTTP and UI arrive
// through CloudHooks, so all three clients drive the same two flows.
//
// Key derivation (design D2): the server never sees the passphrase — the
// client derives key = lowercase 16-hex fnv1a64 of
// "og-cloud-save:" + normalize(passphrase). The "og-cloud-save:" prefix
// domain-separates from the pack-hash uses of fnv1a64.

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace og::ui::cloud {

inline constexpr std::size_t kMinPassphraseChars = 8;
inline constexpr std::size_t kMaxPassphraseChars = 64;
/** Decoded blob cap; must match MAX_CLOUD_SAVE_BYTES in relay/src/shared.ts. */
inline constexpr std::size_t kMaxCloudSaveBytes = 64 * 1024;

// trim + collapse internal whitespace runs to one ' ' + ASCII tolower
// (non-ASCII bytes pass through). Typo tolerance beats entropy for a
// game-save-grade credential (mobile keyboards auto-capitalize).
std::string normalize_cloud_passphrase(const std::string& passphrase);

// 16 lowercase hex chars of fnv1a64("og-cloud-save:" + normalized), or ""
// when the normalized passphrase is outside [8, 64] chars.
std::string derive_cloud_save_key(const std::string& passphrase);

// Lowercase hex codec for the wire blob. Decode rejects odd lengths and any
// character outside [0-9a-f] (uppercase included — the relay stores exactly
// what the client sends, so the accepted alphabet stays canonical).
std::string hex_encode(std::span<const std::uint8_t> bytes);
bool hex_decode(std::string_view hex, std::vector<std::uint8_t>& out);

// relay_api_base_url semantics (trim, strip trailing '/', ws->http scheme
// swap, ensure "/api") + "/save/" + key. An empty base falls back to the
// OPENGLAD_RELAY_BASE_URL env override, then the shipped default relay —
// duplicated tiny logic, deliberately not reaching into the platform TU
// (terminal clients do not compile it).
std::string build_cloud_save_url(std::string base_url, std::string_view key);

struct CloudSaveMeta {
    std::string slot;
    std::string save_name;
    int scen_num = 0;
    std::int64_t last_played = 0;
};

struct CloudSaveGetResponse {
    std::int64_t revision = 0;
    std::int64_t uploaded_at_ms = 0;
    CloudSaveMeta meta;
    std::vector<std::uint8_t> data;
};

// Response/request bodies for the two routes. The parsers throw
// std::runtime_error with a user-facing message on malformed bodies.
CloudSaveGetResponse parse_cloud_save_get_response(std::string_view body);
std::string build_cloud_save_post_body(std::int64_t expected_revision,
                                       const CloudSaveMeta& meta,
                                       std::span<const std::uint8_t> data);
std::int64_t parse_cloud_save_post_ok(std::string_view body); // revision
struct CloudSaveConflict {
    std::int64_t revision = 0;
    CloudSaveMeta meta;
    std::int64_t uploaded_at_ms = 0;
};
CloudSaveConflict parse_cloud_save_post_conflict(std::string_view body);

// HTTP seam + UI seam: all three clients drive the same flows. status 0
// means transport failure (error carries the reason).
struct CloudHttpResult {
    int status = 0;
    std::string body;
    std::string error;
};
struct CloudHooks {
    // Empty http functions => the flows report
    // "Cloud sync is not available in this client." (design D8).
    std::function<CloudHttpResult(const std::string& url)> http_get;
    std::function<CloudHttpResult(const std::string& url,
                                  const std::string& json_body)> http_post;
    // NO-first confirm (the §2.0 U3 grammar).
    std::function<bool(const std::string& title,
                       const std::string& message)> confirm;
    std::function<void(const std::string& title,
                       const std::string& message)> notify;
};

// cfg persistence (category "cloud", keys "key" / "revision"); wraps the
// global cfg_store + save_settings(). The DERIVED key is stored, never the
// raw passphrase (design D9). store_cloud_key resets the revision to 0.
std::string stored_cloud_key();
void store_cloud_key(const std::string& key_hex);
std::int64_t stored_cloud_revision();
void store_cloud_revision(std::int64_t revision);

// The two flows (blocking; return a one-line status for the CLOUD SAVE
// screen's status strip):
//   upload: export active-company bytes -> header-scan meta ->
//           POST(expected = stored revision) -> on 409: confirm(remote
//           identity) -> POST(expected = server revision) -> store revision,
//           notify.
//   download: GET -> 404 notify / 200 -> validate slot (safe basename,
//           != "netsession") -> if the local slot file exists: confirm(both
//           identities) -> install_company_bytes -> store revision ->
//           open_company callback (§2.3 OPEN; false => D16 popup) -> notify.
std::string run_cloud_upload(const std::string& base_url,
                             const CloudHooks& hooks);
std::string run_cloud_download(
    const std::string& base_url,
    const CloudHooks& hooks,
    const std::function<bool(const std::string& slot)>& open_company);

} // namespace og::ui::cloud
