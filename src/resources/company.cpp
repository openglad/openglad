/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <openglad/resources/company.h>

#include <openglad/core/util.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/filesystem_sync.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace og::data {

namespace {

constexpr std::string_view kNetsessionSlot = "netsession";
constexpr std::string_view kDefaultCompanySlot = "save0";
constexpr std::size_t kMaxCompanySlugLength = 40;

std::optional<std::int64_t>& company_clock_override()
{
    static std::optional<std::int64_t> fixed_now_s;
    return fixed_now_s;
}

std::string& active_company_slot_state()
{
    static std::string slot{kDefaultCompanySlot};
    return slot;
}

bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

} // namespace

std::int64_t company_clock_now_s()
{
    const auto& fixed = company_clock_override();
    if (fixed.has_value())
        return *fixed;
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void set_company_clock_for_tests(std::optional<std::int64_t> fixed_now_s)
{
    company_clock_override() = fixed_now_s;
}

// ---------------------------------------------------------------------------
// Active-company indirection (§3.4)
// ---------------------------------------------------------------------------

const std::string& active_company_slot()
{
    return active_company_slot_state();
}

bool set_active_company_slot(const std::string& slot)
{
    if (slot == kNetsessionSlot || !is_safe_virtual_basename(slot))
        return false;
    active_company_slot_state() = slot;
    return true;
}

ScopedActiveCompany::ScopedActiveCompany(const std::string& slot)
    : previous_(active_company_slot_state())
    , applied_(set_active_company_slot(slot))
{
}

ScopedActiveCompany::~ScopedActiveCompany()
{
    // previous_ came out of the state, so it is always re-settable.
    active_company_slot_state() = previous_;
}

std::string derive_company_slot(const std::string& display_name)
{
    std::string slug;
    slug.reserve(display_name.size());
    for (const char raw : display_name)
    {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (std::isalnum(ch))
            slug.push_back(static_cast<char>(std::tolower(ch)));
        else if (ch == ' ' || ch == '-' || ch == '_')
            slug.push_back('-');
        // every other character is dropped
    }

    // Collapse runs of '-' and trim the ends.
    std::string collapsed;
    collapsed.reserve(slug.size());
    for (const char ch : slug)
    {
        if (ch == '-' && (collapsed.empty() || collapsed.back() == '-'))
            continue;
        collapsed.push_back(ch);
    }
    if (!collapsed.empty() && collapsed.back() == '-')
        collapsed.pop_back();

    if (collapsed.size() > kMaxCompanySlugLength)
        collapsed.resize(kMaxCompanySlugLength);
    while (!collapsed.empty() && collapsed.back() == '-')
        collapsed.pop_back();
    if (collapsed.empty())
        collapsed = "company";

    const auto taken = [](const std::string& candidate) {
        return candidate == kNetsessionSlot ||
               user_file_exists("save/" + candidate + ".gtl");
    };

    if (!taken(collapsed))
        return collapsed;
    for (int suffix = 2; suffix <= 99; ++suffix)
    {
        const std::string candidate =
            collapsed + "-" + std::to_string(suffix);
        if (!taken(candidate))
            return candidate;
    }
    // 98 collisions deep: fall back to a wall-clock suffix (test-pinnable via
    // the company clock seam).
    return collapsed + "-" + std::to_string(company_clock_now_s());
}

// ---------------------------------------------------------------------------
// Header-only company scan (§3.5)
// ---------------------------------------------------------------------------

std::optional<CompanyInfo> read_company_header(const std::string& slot)
{
    if (!is_safe_virtual_basename(slot))
        return std::nullopt;

    const std::string filename = slot + ".gtl";
    og::io::OgFilePtr infile = og::io::og_open_read("save/", filename.c_str());
    if (!infile)
        return std::nullopt;

    CompanyInfo info;
    info.slot = slot;
    info.campaign_id = "org.openglad.gladiator";

    const auto read_exact = [&infile](void* dst, std::size_t bytes) {
        return og::io::og_read_exact(*infile, dst, 1, bytes);
    };

    // The ladder below replicates SaveData::load's version-gated header
    // prefix EXACTLY ([SAVE-R1]) — for v < 8 the fixed offsets do not hold,
    // so every field is reached by walking the gated sequence. Total reads
    // never exceed the 164-byte v14 header; the roster and everything after
    // it are never touched, and no campaign is ever mounted.
    std::array<char, 3> magic{};
    if (!read_exact(magic.data(), magic.size()) ||
        std::memcmp(magic.data(), "GTL", 3) != 0)
    {
        return info; // bad magic / truncated -> corrupt, not missing
    }

    if (!read_exact(&info.version, 1))
        return info;
    if (info.version < 1)
        return info; // version 0 is UnsupportedVersion in the reader

    std::int16_t registered = 0;
    if (info.version >= 7 && !read_exact(&registered, 2))
        return info;

    if (info.version >= 2)
    {
        std::array<char, 41> name{};
        if (!read_exact(name.data(), 40))
            return info;
        name[40] = '\0';
        info.display_name = name.data();
    }
    // version 1 carries no name; display_name stays empty.

    if (info.version >= 8)
    {
        std::array<char, 41> campaign{};
        if (!read_exact(campaign.data(), 40))
            return info;
        campaign[40] = '\0';
        const std::string loaded_campaign = campaign.data();
        if (loaded_campaign.size() > 3 && is_safe_campaign_id(loaded_campaign))
            info.campaign_id = loaded_campaign;
    }

    std::int16_t scen = 0;
    if (!read_exact(&scen, 2))
        return info;
    info.scen_num = scen;

    std::uint32_t cash = 0;
    std::uint32_t score = 0;
    if (!read_exact(&cash, 4) || !read_exact(&score, 4))
        return info;
    info.totalcash = cash;

    if (info.version >= 6)
    {
        std::array<char, 32> per_team_scores{};
        if (!read_exact(per_team_scores.data(), per_team_scores.size()))
            return info;
    }

    std::int16_t allied = 0;
    if (info.version >= 7 && !read_exact(&allied, 2))
        return info;

    std::int16_t listsize = 0;
    if (!read_exact(&listsize, 2))
        return info;
    info.roster_size = listsize;
    if (listsize < 0 || listsize > MAX_TEAM_SIZE)
        return info; // the reader rejects this header

    std::uint8_t numplayers = 0;
    if (!read_exact(&numplayers, 1))
        return info;
    if (numplayers > 4)
        return info; // mirrors the reader's kMaxPlayers reject

    if (info.version >= 14)
    {
        std::int64_t last_played = 0;
        if (!read_exact(&last_played, 8))
            return info;
        info.last_played_unix_s = last_played;
    }
    // v13 and older carry 'GTL' filler here — never sniffed (§3.1).

    info.valid = true;
    return info;
}

std::vector<CompanyInfo> list_companies()
{
    std::vector<std::string> names;
    if (og::resources::is_initialized())
    {
        for (const std::string& name : og::resources::list_files("save"))
            names.push_back(name);
    }
    else
    {
        // std::filesystem fallback for contexts that never ran io_init.
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path save_dir = fs::path(get_user_path()) / "save";
        fs::directory_iterator it(save_dir, ec);
        if (!ec)
        {
            for (const fs::directory_entry& entry : it)
            {
                std::error_code entry_ec;
                if (entry.is_regular_file(entry_ec))
                    names.push_back(entry.path().filename().string());
            }
        }
        std::sort(names.begin(), names.end());
    }

    std::vector<CompanyInfo> companies;
    for (const std::string& name : names)
    {
        if (!ends_with(name, ".gtl"))
            continue;
        if (name == "netsession.gtl")
            continue;
        if (ends_with(name, ".tmp.gtl"))
            continue; // atomic-write staging files are never companies
        const std::string slot = name.substr(0, name.size() - 4);
        if (!is_safe_virtual_basename(slot))
            continue;
        if (std::optional<CompanyInfo> info = read_company_header(slot))
            companies.push_back(std::move(*info));
    }

    std::sort(companies.begin(), companies.end(),
              [](const CompanyInfo& a, const CompanyInfo& b) {
                  if (a.last_played_unix_s != b.last_played_unix_s)
                      return a.last_played_unix_s > b.last_played_unix_s;
                  const bool a_default = a.slot == kDefaultCompanySlot;
                  const bool b_default = b.slot == kDefaultCompanySlot;
                  if (a_default != b_default)
                      return a_default;
                  return a.slot < b.slot;
              });
    return companies;
}

std::string select_startup_company()
{
    const std::vector<CompanyInfo> companies = list_companies();
    if (companies.empty())
        return {};
    return companies.front().slot;
}

// ---------------------------------------------------------------------------
// Atomic company write (§3.6)
// ---------------------------------------------------------------------------

SaveDataIoError atomic_company_save(SaveData& save, const std::string& slot)
{
    if (slot == kNetsessionSlot || !is_safe_virtual_basename(slot))
    {
        LogError("atomic_company_save_rejected slot={}\n", slot);
        return SaveDataIoError::OpenWriteFailed;
    }

    // SaveData::save appends ".gtl", so the staging slot lands at
    // save/<slot>.tmp.gtl — excluded from listings above.
    const std::string tmp_slot = slot + ".tmp";
    const SaveDataIoError write_error = save.save_with_error(tmp_slot);
    if (write_error != SaveDataIoError::None)
        return write_error;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp_path = fs::path(get_user_path()) / "save" / (tmp_slot + ".gtl");
    fs::path dst_path = fs::path(get_user_path()) / "save" / (slot + ".gtl");
    if (!fs::exists(tmp_path, ec))
    {
        // og_open_write's cwd fallback (PhysFS uninitialized / no write dir).
        tmp_path = fs::path("save") / (tmp_slot + ".gtl");
        dst_path = fs::path("save") / (slot + ".gtl");
    }

    fs::rename(tmp_path, dst_path, ec);
    if (ec)
    {
        LogError("atomic_company_save_rename_failed slot={} error={}\n",
                 slot, ec.message());
        std::error_code cleanup_ec;
        fs::remove(tmp_path, cleanup_ec);
        return SaveDataIoError::WriteFailed;
    }

    // save_with_error synced the staging file; the rename needs its own sync
    // to persist on IDBFS.
    sync_filesystem();
    return SaveDataIoError::None;
}

} // namespace og::data
