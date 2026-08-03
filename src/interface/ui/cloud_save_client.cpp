/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* Cloud saves (issue #155), the pure client half. SDL-free: compiled into
 * og_interface and both terminal clients. HTTP and dialogs arrive through
 * CloudHooks; the platform installs real transports on PlatformBridge and
 * the SDL screen (menu_screen_specs.cpp) adapts them into hooks. See
 * include/openglad/interface/ui/cloud_save_client.h for the contract and
 * relay/src/save-vault.ts for the server side.
 */

#include <openglad/interface/ui/cloud_save_client.h>

#include <openglad/core/fnv1a.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>
#include <optional>
#include <stdexcept>

namespace og::ui::cloud {

namespace {

// Mirrors kDefaultRelayBaseUrl in the two platform lobby-network TUs; the
// cloud endpoints live under the same base so a fresh device needs zero
// configuration.
constexpr std::string_view kDefaultRelayBaseUrl =
    "https://openglad.pages.dev/relay";

constexpr std::string_view kCloudKeyDomainPrefix = "og-cloud-save:";

constexpr const char* kCloudDialogTitle = "CLOUD SAVE";

bool is_ascii_space(unsigned char ch)
{
    return std::isspace(ch) != 0;
}

// --- minimal JSON field extraction -----------------------------------------
// Private to this TU (the platform file's extractors are static and stay
// where they are). Enough for the relay's flat JSON objects: find `"key"`,
// skip `: `, then read a string or an integer.

std::optional<std::size_t> find_json_field_value(std::string_view text,
                                                 std::string_view key)
{
    const std::string quoted_key = std::format("\"{}\"", key);
    std::size_t search_pos = 0;
    while (search_pos < text.size())
    {
        const std::size_t key_pos = text.find(quoted_key, search_pos);
        if (key_pos == std::string_view::npos)
            return std::nullopt;
        std::size_t cursor = key_pos + quoted_key.size();
        while (cursor < text.size() &&
               is_ascii_space(static_cast<unsigned char>(text[cursor])))
            ++cursor;
        if (cursor < text.size() && text[cursor] == ':')
        {
            ++cursor;
            while (cursor < text.size() &&
                   is_ascii_space(static_cast<unsigned char>(text[cursor])))
                ++cursor;
            if (cursor < text.size())
                return cursor;
            return std::nullopt;
        }
        // The match was a value or a substring of another key; keep looking.
        search_pos = key_pos + 1;
    }
    return std::nullopt;
}

std::optional<std::string> extract_json_string_field(std::string_view text,
                                                     std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value() || text[*value_pos] != '"')
        return std::nullopt;

    std::string value;
    bool escape = false;
    for (std::size_t index = *value_pos + 1; index < text.size(); ++index)
    {
        const char ch = text[index];
        if (escape)
        {
            value.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\')
        {
            escape = true;
            continue;
        }
        if (ch == '"')
            return value;
        value.push_back(ch);
    }
    return std::nullopt;
}

std::optional<std::int64_t> extract_json_i64_field(std::string_view text,
                                                   std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value())
        return std::nullopt;

    std::size_t end = *value_pos;
    if (end < text.size() && text[end] == '-')
        ++end;
    while (end < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[end])))
        ++end;
    if (end == *value_pos)
        return std::nullopt;

    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data() + *value_pos,
                                           text.data() + end, value);
    if (ec != std::errc{} || ptr != text.data() + end)
        return std::nullopt;
    return value;
}

// JSON string escaping for the POST body (save names are user text).
std::string json_escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text)
    {
        switch (ch)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
                out += std::format("\\u{:04x}", static_cast<unsigned>(ch));
            else
                out.push_back(ch);
            break;
        }
    }
    return out;
}

CloudSaveMeta parse_meta_fields(std::string_view body)
{
    CloudSaveMeta meta;
    meta.slot = extract_json_string_field(body, "slot").value_or("");
    meta.save_name = extract_json_string_field(body, "save_name").value_or("");
    meta.scen_num = static_cast<int>(
        extract_json_i64_field(body, "scen_num").value_or(0));
    meta.last_played =
        extract_json_i64_field(body, "last_played").value_or(0);
    return meta;
}

// "<name>, played <date>" (date omitted when never stamped); "(unnamed)"
// keeps the confirm prompts honest for empty names.
std::string identity_line(const std::string& name, std::int64_t last_played)
{
    std::string line = name.empty() ? std::string("(unnamed)") : name;
    const std::string played = format_played_date_utc(last_played);
    if (!played.empty())
        line += std::format(", played {}", played);
    return line;
}

bool hooks_available(const CloudHooks& hooks)
{
    return static_cast<bool>(hooks.http_get) &&
           static_cast<bool>(hooks.http_post);
}

void notify(const CloudHooks& hooks, const std::string& title,
            const std::string& message)
{
    if (hooks.notify)
        hooks.notify(title, message);
}

bool confirm_no_first(const CloudHooks& hooks, const std::string& title,
                      const std::string& message)
{
    if (!hooks.confirm)
        return false;
    return hooks.confirm(title, message);
}

// Shared status+popup tail for transport/HTTP upload/download errors.
std::string http_error_status(const CloudHooks& hooks, const char* verb,
                              const CloudHttpResult& result)
{
    if (result.status == 0)
    {
        const std::string reason =
            result.error.empty() ? std::string("network error")
                                 : result.error;
        notify(hooks, kCloudDialogTitle,
               std::format("{} failed:\n{}", verb, reason));
        return std::format("{} failed: network error.", verb);
    }
    if (result.status == 429)
    {
        notify(hooks, kCloudDialogTitle, "Too many uploads; wait a minute.");
        return "Too many uploads; wait a minute.";
    }
    if (result.status == 413)
    {
        notify(hooks, kCloudDialogTitle, "Save too large for cloud sync.");
        return "Save too large for cloud sync.";
    }
    notify(hooks, kCloudDialogTitle,
           std::format("{} failed (HTTP {}).", verb, result.status));
    return std::format("{} failed (HTTP {}).", verb, result.status);
}

} // namespace

std::string normalize_cloud_passphrase(const std::string& passphrase)
{
    std::string normalized;
    normalized.reserve(passphrase.size());
    bool pending_space = false;
    for (const char raw : passphrase)
    {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (is_ascii_space(ch))
        {
            // Leading runs never emit; internal runs collapse to one ' '.
            pending_space = !normalized.empty();
            continue;
        }
        if (pending_space)
        {
            normalized.push_back(' ');
            pending_space = false;
        }
        normalized.push_back(
            static_cast<char>(ch < 0x80 ? std::tolower(ch) : ch));
    }
    return normalized;
}

std::string derive_cloud_save_key(const std::string& passphrase)
{
    const std::string normalized = normalize_cloud_passphrase(passphrase);
    if (normalized.size() < kMinPassphraseChars ||
        normalized.size() > kMaxPassphraseChars)
        return {};
    std::string hashed(kCloudKeyDomainPrefix);
    hashed += normalized;
    const std::uint64_t hash = og::core::fnv1a64(
        reinterpret_cast<const std::uint8_t*>(hashed.data()), hashed.size());
    return std::format("{:016x}", hash);
}

std::string hex_encode(std::span<const std::uint8_t> bytes)
{
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes)
    {
        hex.push_back(kHexDigits[byte >> 4]);
        hex.push_back(kHexDigits[byte & 0x0f]);
    }
    return hex;
}

bool hex_decode(std::string_view hex, std::vector<std::uint8_t>& out)
{
    out.clear();
    if (hex.size() % 2 != 0)
        return false;
    out.reserve(hex.size() / 2);
    const auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return ch - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < hex.size(); i += 2)
    {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
        {
            out.clear();
            return false;
        }
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

std::string build_cloud_save_url(std::string base_url, std::string_view key)
{
    // Trim.
    while (!base_url.empty() &&
           is_ascii_space(static_cast<unsigned char>(base_url.front())))
        base_url.erase(base_url.begin());
    while (!base_url.empty() &&
           is_ascii_space(static_cast<unsigned char>(base_url.back())))
        base_url.pop_back();

    // Empty -> the env override, then the shipped default (the tail of the
    // relay_base_url_or_default chain; the SDL caller passes
    // default_relay_base_url(), which also consults the web test hook).
    if (base_url.empty())
    {
        if (const char* env_value = std::getenv("OPENGLAD_RELAY_BASE_URL");
            env_value != nullptr && env_value[0] != '\0')
            base_url = env_value;
        else
            base_url = std::string(kDefaultRelayBaseUrl);
    }

    while (!base_url.empty() && base_url.back() == '/')
        base_url.pop_back();

    // The save endpoints are plain HTTP even when the configured relay URL
    // uses a websocket scheme.
    if (base_url.rfind("ws://", 0) == 0)
        base_url.replace(0, 5, "http://");
    else if (base_url.rfind("wss://", 0) == 0)
        base_url.replace(0, 6, "https://");

    if (base_url.size() < 4 ||
        base_url.substr(base_url.size() - 4) != "/api")
        base_url += "/api";

    return std::format("{}/save/{}", base_url, key);
}

CloudSaveGetResponse parse_cloud_save_get_response(std::string_view body)
{
    CloudSaveGetResponse response;
    const auto revision = extract_json_i64_field(body, "revision");
    if (!revision.has_value() || *revision < 0)
        throw std::runtime_error("Cloud reply is missing its revision.");
    response.revision = *revision;
    response.uploaded_at_ms =
        extract_json_i64_field(body, "uploaded_at").value_or(0);
    response.meta = parse_meta_fields(body);

    const auto data_hex = extract_json_string_field(body, "data_hex");
    if (!data_hex.has_value() || data_hex->empty())
        throw std::runtime_error("Cloud reply is missing its save data.");
    if (data_hex->size() > kMaxCloudSaveBytes * 2)
        throw std::runtime_error("Cloud reply is over the size cap.");
    if (!hex_decode(*data_hex, response.data))
        throw std::runtime_error("Cloud reply has corrupt save data.");
    return response;
}

std::string build_cloud_save_post_body(std::int64_t expected_revision,
                                       const CloudSaveMeta& meta,
                                       std::span<const std::uint8_t> data)
{
    return std::format(
        "{{\"expected_revision\":{},\"slot\":\"{}\",\"save_name\":\"{}\","
        "\"scen_num\":{},\"last_played\":{},\"data_hex\":\"{}\"}}",
        expected_revision, json_escape(meta.slot),
        json_escape(meta.save_name), meta.scen_num, meta.last_played,
        hex_encode(data));
}

std::int64_t parse_cloud_save_post_ok(std::string_view body)
{
    const auto revision = extract_json_i64_field(body, "revision");
    if (!revision.has_value() || *revision < 1)
        throw std::runtime_error("Cloud reply is missing its revision.");
    return *revision;
}

CloudSaveConflict parse_cloud_save_post_conflict(std::string_view body)
{
    CloudSaveConflict conflict;
    const auto revision = extract_json_i64_field(body, "revision");
    if (!revision.has_value() || *revision < 0)
        throw std::runtime_error("Cloud reply is missing its revision.");
    conflict.revision = *revision;
    conflict.uploaded_at_ms =
        extract_json_i64_field(body, "uploaded_at").value_or(0);
    conflict.meta = parse_meta_fields(body);
    return conflict;
}

std::string stored_cloud_key()
{
    return cfg.get_setting("cloud", "key");
}

void store_cloud_key(const std::string& key_hex)
{
    cfg.apply_setting("cloud", "key", key_hex);
    // A new key targets a different vault: the old optimistic revision is
    // meaningless there (worst case one extra 409 prompt, design D9).
    cfg.apply_setting("cloud", "revision", "0");
    cfg.save_settings();
}

std::int64_t stored_cloud_revision()
{
    const std::string value = cfg.get_setting("cloud", "revision");
    std::int64_t revision = 0;
    const auto [ptr, ec] =
        std::from_chars(value.data(), value.data() + value.size(), revision);
    if (ec != std::errc{} || ptr != value.data() + value.size() ||
        revision < 0)
        return 0;
    return revision;
}

void store_cloud_revision(std::int64_t revision)
{
    cfg.apply_setting("cloud", "revision", std::format("{}", revision));
    cfg.save_settings();
}

std::string run_cloud_upload(const std::string& base_url,
                             const CloudHooks& hooks)
{
    if (!hooks_available(hooks))
    {
        notify(hooks, kCloudDialogTitle,
               "Cloud sync is not available\nin this client.");
        return "Cloud sync is not available.";
    }
    const std::string key = stored_cloud_key();
    if (key.empty())
    {
        notify(hooks, kCloudDialogTitle, "Set a passphrase first.");
        return "Set a passphrase first.";
    }

    const std::string slot = og::data::active_company_slot();
    const std::optional<std::vector<std::uint8_t>> bytes =
        og::data::export_company_bytes(slot);
    if (!bytes.has_value())
    {
        notify(hooks, kCloudDialogTitle, "No company to upload.");
        return "No company to upload.";
    }
    if (bytes->size() > kMaxCloudSaveBytes)
    {
        notify(hooks, kCloudDialogTitle, "Save too large for cloud sync.");
        return "Save too large for cloud sync.";
    }
    const std::optional<og::data::CompanyInfo> header =
        og::data::read_company_header(slot);
    if (!header.has_value() || !header->valid)
    {
        notify(hooks, kCloudDialogTitle,
               "Company file is damaged;\nrestore a backup first.");
        return "Company file is damaged.";
    }

    CloudSaveMeta meta;
    meta.slot = slot;
    meta.save_name = header->display_name;
    meta.scen_num = header->scen_num;
    meta.last_played = header->last_played_unix_s;

    const std::string url = build_cloud_save_url(base_url, key);
    const std::string display_name =
        meta.save_name.empty() ? slot : meta.save_name;

    CloudHttpResult result = hooks.http_post(
        url, build_cloud_save_post_body(stored_cloud_revision(), meta,
                                        *bytes));
    if (result.status == 409)
    {
        // Someone (another device, or a passphrase collision) holds a
        // different revision. Show its identity; NO-first (design D5).
        CloudSaveConflict conflict;
        try
        {
            conflict = parse_cloud_save_post_conflict(result.body);
        }
        catch (const std::exception& error)
        {
            notify(hooks, kCloudDialogTitle, error.what());
            return "Upload failed: bad cloud reply.";
        }
        const std::string message = std::format(
            "Cloud has: {}\nLevel {}, played {}\nReplace it?",
            conflict.meta.save_name.empty() ? "(unnamed)"
                                            : conflict.meta.save_name,
            conflict.meta.scen_num,
            format_played_date_utc(conflict.meta.last_played));
        if (!confirm_no_first(hooks, "OVERWRITE CLOUD SAVE?", message))
            return "Upload cancelled.";
        result = hooks.http_post(
            url,
            build_cloud_save_post_body(conflict.revision, meta, *bytes));
    }
    if (result.status != 200)
        return http_error_status(hooks, "Upload", result);

    std::int64_t revision = 0;
    try
    {
        revision = parse_cloud_save_post_ok(result.body);
    }
    catch (const std::exception& error)
    {
        notify(hooks, kCloudDialogTitle, error.what());
        return "Upload failed: bad cloud reply.";
    }
    store_cloud_revision(revision);
    const std::string status = std::format("Uploaded '{}'.", display_name);
    notify(hooks, kCloudDialogTitle, status);
    return status;
}

std::string run_cloud_download(
    const std::string& base_url,
    const CloudHooks& hooks,
    const std::function<bool(const std::string& slot)>& open_company)
{
    if (!hooks_available(hooks))
    {
        notify(hooks, kCloudDialogTitle,
               "Cloud sync is not available\nin this client.");
        return "Cloud sync is not available.";
    }
    const std::string key = stored_cloud_key();
    if (key.empty())
    {
        notify(hooks, kCloudDialogTitle, "Set a passphrase first.");
        return "Set a passphrase first.";
    }

    const CloudHttpResult result =
        hooks.http_get(build_cloud_save_url(base_url, key));
    if (result.status == 404)
    {
        notify(hooks, kCloudDialogTitle,
               "No cloud save found\nfor this passphrase.");
        return "No cloud save for this passphrase.";
    }
    if (result.status != 200)
        return http_error_status(hooks, "Download", result);

    CloudSaveGetResponse response;
    try
    {
        response = parse_cloud_save_get_response(result.body);
    }
    catch (const std::exception& error)
    {
        notify(hooks, kCloudDialogTitle, error.what());
        return "Download failed: bad cloud reply.";
    }

    // Downloaded metadata is untrusted: the slot must be a safe basename and
    // never the "netsession" scratch — refused BEFORE any confirm.
    const std::string& slot = response.meta.slot;
    if (slot.empty() || slot == "netsession" ||
        !is_safe_virtual_basename(slot))
    {
        notify(hooks, kCloudDialogTitle,
               "Cloud save has an invalid\nslot name.");
        return "Cloud save refused: bad slot.";
    }

    const std::string cloud_name = response.meta.save_name.empty()
        ? slot
        : response.meta.save_name;

    if (user_file_exists("save/" + slot + ".gtl"))
    {
        // NO-first, both identities shown (design D5). A fresh backup is
        // also taken inside install_company_bytes before the swap.
        const std::optional<og::data::CompanyInfo> local =
            og::data::read_company_header(slot);
        const std::string local_line = identity_line(
            local.has_value() ? local->display_name : std::string(),
            local.has_value() ? local->last_played_unix_s : 0);
        const std::string message = std::format(
            "Local: {}\nCloud: {}\nReplace local?", local_line,
            identity_line(response.meta.save_name,
                          response.meta.last_played));
        if (!confirm_no_first(hooks, "OVERWRITE COMPANY?", message))
            return "Download cancelled.";
    }

    const og::data::CompanyInstallError install_error =
        og::data::install_company_bytes(slot, response.data);
    if (install_error != og::data::CompanyInstallError::None)
    {
        if (install_error == og::data::CompanyInstallError::InvalidBytes)
        {
            notify(hooks, kCloudDialogTitle,
                   "Cloud save is damaged;\nnothing was changed.");
            return "Cloud save is damaged.";
        }
        notify(hooks, kCloudDialogTitle,
               "Could not install the\ndownloaded company.");
        return "Download failed: install error.";
    }
    store_cloud_revision(response.revision);

    if (open_company && !open_company(slot))
    {
        // D16: the company stays installed on disk; name the campaign so the
        // user knows what to install ([SAVE-R6] surface-don't-switch).
        const std::optional<og::data::CompanyInfo> installed =
            og::data::read_company_header(slot);
        notify(hooks, kCloudDialogTitle,
               std::format("Downloaded, but campaign\n'{}' is not installed.",
                           installed.has_value() ? installed->campaign_id
                                                 : std::string("unknown")));
        return "Downloaded; campaign missing.";
    }

    const std::string status = std::format("Downloaded '{}'.", cloud_name);
    notify(hooks, kCloudDialogTitle, status);
    return status;
}

} // namespace og::ui::cloud
