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

#include <openglad/core/campaign_ids.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/guy.h>
#include <openglad/resources/campaign_io.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/filesystem_sync.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <string_view>
#include <system_error>

namespace og::data {

namespace {

constexpr std::string_view kNetsessionSlot = "netsession";
constexpr std::string_view kDefaultCompanySlot = "save0";
constexpr std::size_t kMaxCompanySlugLength = 40;
constexpr std::size_t kMaxVirtualBasenameLength = 64;

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
    // the company clock seam), but keep probing. A timestamp is not unique:
    // two companies can be founded in the same second, and an older save may
    // already carry that suffix. Returning the unchecked timestamp candidate
    // would repoint the active company at an existing file and overwrite it.
    const std::string epoch = std::to_string(company_clock_now_s());
    for (std::uint64_t retry = 1;; ++retry)
    {
        std::string tail = "-" + epoch;
        if (retry > 1)
            tail += "-" + std::to_string(retry);

        std::string base = collapsed;
        if (base.size() + tail.size() > kMaxVirtualBasenameLength)
            base.resize(kMaxVirtualBasenameLength - tail.size());
        while (!base.empty() && base.back() == '-')
            base.pop_back();
        if (base.empty())
            base = "company";

        const std::string candidate = base + tail;
        if (!taken(candidate))
            return candidate;
    }
}

// ---------------------------------------------------------------------------
// Header-only company scan (§3.5)
// ---------------------------------------------------------------------------

namespace {

// The version-gated ladder shared by company files (save/<slot>.gtl) and
// their byte-copy backups (save/backups/<slot>.<seq>.gtl) — a backup IS a
// company file, so the Backups view header-scans it exactly like a company
// (§3.7).
std::optional<CompanyInfo> read_header_from(const char* dir,
                                            const std::string& filename,
                                            const std::string& slot_label)
{
    og::io::OgFilePtr infile = og::io::og_open_read(dir, filename.c_str());
    if (!infile)
        return std::nullopt;

    CompanyInfo info;
    info.slot = slot_label;
    info.campaign_id = "gladiator";

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
            // Pre-rename saves store "org.openglad.<name>"; report the
            // plain id, matching what SaveData::load will mount.
            info.campaign_id =
                std::string(og::normalize_legacy_id(loaded_campaign));
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

    // Player count is session state now, but old GTL readers still require
    // this byte at offset 132. Validate the legacy value exactly as the full
    // reader does; it is not company metadata.
    std::uint8_t legacy_numplayers = 0;
    if (!read_exact(&legacy_numplayers, 1))
        return info;
    if (legacy_numplayers > kMaxLegacySavePlayers)
        return info; // mirrors the reader's reject (save_data.cpp)

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

} // namespace

std::optional<CompanyInfo> read_company_header(const std::string& slot)
{
    if (!is_safe_virtual_basename(slot))
        return std::nullopt;
    return read_header_from("save/", slot + ".gtl", slot);
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
    {
        // Serialization can fail after creating the staging file. Never
        // leave that partial file behind, whether OgFile selected the user
        // directory or its cwd fallback.
        (void)remove_user_file("save/" + tmp_slot + ".gtl");
        std::error_code cleanup_ec;
        std::filesystem::remove(
            std::filesystem::path("save") / (tmp_slot + ".gtl"),
            cleanup_ec);
        return write_error;
    }

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

// ---------------------------------------------------------------------------
// Backups (§3.7)
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kBackupDirPrefix = "save/backups/";

// "<slot>.<SEQ>.gtl" with SEQ zero-padded to at least 3 digits.
std::string backup_filename(const std::string& slot, int seq)
{
    return std::format("{}.{:03}.gtl", slot, seq);
}

// A backup of `slot` is exactly "<slot>.<digits>.gtl": the seq is the
// rightmost all-digit dot-token, so slots containing dots (or digit-only
// dot-tokens of their own) stay unambiguous — "led.7.004.gtl" parses as
// slot "led.7" seq 4 and never as a backup of "led" (its remainder
// "7.004" contains a dot). Anything else in the directory is ignored,
// including the "<name>.gtl.tmp" staging files copy_user_file renames away.
std::optional<int> parse_backup_seq(std::string_view name, std::string_view slot)
{
    if (name.size() <= slot.size() + 1 ||
        name.substr(0, slot.size()) != slot || name[slot.size()] != '.')
    {
        return std::nullopt;
    }
    std::string_view rest = name.substr(slot.size() + 1);
    if (!ends_with(rest, ".gtl"))
        return std::nullopt;
    const std::string_view digits = rest.substr(0, rest.size() - 4);
    // 9-digit cap keeps the parse comfortably inside int range.
    if (digits.empty() || digits.size() > 9)
        return std::nullopt;
    int seq = 0;
    for (const char ch : digits)
    {
        if (ch < '0' || ch > '9')
            return std::nullopt;
        seq = seq * 10 + (ch - '0');
    }
    return seq;
}

struct BackupFile
{
    int seq = 0;
    std::string filename;
};

// Every backup file of `slot` in save/backups/, newest (highest seq) first;
// equal seqs (differently padded duplicates) order canonically padded name
// first. Mirrors list_companies' listing strategy: PhysFS when initialized,
// std::filesystem fallback otherwise; a missing directory is an empty list.
std::vector<BackupFile> scan_company_backups(const std::string& slot)
{
    std::vector<std::string> names;
    if (og::resources::is_initialized())
    {
        for (const std::string& name : og::resources::list_files("save/backups"))
            names.push_back(name);
    }
    else
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path backups_dir =
            fs::path(get_user_path()) / "save" / "backups";
        fs::directory_iterator it(backups_dir, ec);
        if (!ec)
        {
            for (const fs::directory_entry& entry : it)
            {
                std::error_code entry_ec;
                if (entry.is_regular_file(entry_ec))
                    names.push_back(entry.path().filename().string());
            }
        }
    }

    std::vector<BackupFile> found;
    for (const std::string& name : names)
    {
        if (const std::optional<int> seq = parse_backup_seq(name, slot))
            found.push_back({*seq, name});
    }
    std::sort(found.begin(), found.end(),
              [](const BackupFile& a, const BackupFile& b) {
                  if (a.seq != b.seq)
                      return a.seq > b.seq;
                  return a.filename < b.filename;
              });
    return found;
}

// backup_company_now, returning the new snapshot's seq (0 = refused/failed)
// so restore_company_backup can address its step-1 pre-restore snapshot.
int backup_company_capture_seq(const std::string& slot)
{
    // No-op for the reserved "netsession" server-economy scratch: the
    // level-win producer may call this unconditionally and the networked
    // session must never leave snapshots behind.
    if (slot == kNetsessionSlot || !is_safe_virtual_basename(slot))
        return 0;
    const std::string slot_rel = "save/" + slot + ".gtl";
    if (!user_file_exists(slot_rel))
        return 0;

    std::vector<BackupFile> existing = scan_company_backups(slot);
    const int next_seq = (existing.empty() ? 0 : existing.front().seq) + 1;
    const std::string name = backup_filename(slot, next_seq);
    if (!copy_user_file(slot_rel, kBackupDirPrefix + name))
    {
        LogError("backup_company_copy_failed slot={} seq={}\n", slot, next_seq);
        return 0;
    }
    // Torn-write ordering (§3.7): persist the snapshot BEFORE pruning starts
    // deleting older ones, so an interruption can only leave "one extra
    // backup", never fewer. remove_user_file syncs each deletion itself.
    sync_filesystem();

    existing.insert(existing.begin(), {next_seq, name});
    while (existing.size() > static_cast<std::size_t>(kCompanyBackupRetention))
    {
        if (!remove_user_file(kBackupDirPrefix + existing.back().filename))
        {
            LogError("backup_company_prune_failed slot={} file={}\n", slot,
                     existing.back().filename);
        }
        existing.pop_back();
    }
    return next_seq;
}

} // namespace

std::vector<CompanyBackupInfo> list_company_backups(const std::string& slot)
{
    std::vector<CompanyBackupInfo> backups;
    if (!is_safe_virtual_basename(slot))
        return backups;
    for (const BackupFile& file : scan_company_backups(slot))
    {
        CompanyBackupInfo info;
        info.slot = slot;
        info.seq = file.seq;
        info.filename = file.filename;
        if (std::optional<CompanyInfo> header =
                read_header_from(kBackupDirPrefix, file.filename, slot))
        {
            info.header = std::move(*header);
        }
        // An unopenable file keeps the default header (valid == false) but
        // stays listed, mirroring the corrupt-company policy.
        backups.push_back(std::move(info));
    }
    return backups;
}

bool backup_company_now(const std::string& slot)
{
    return backup_company_capture_seq(slot) > 0;
}

CompanyRestoreError restore_company_backup(SaveData& save,
                                           const std::string& slot, int seq)
{
    if (slot == kNetsessionSlot || !is_safe_virtual_basename(slot))
        return CompanyRestoreError::InvalidBackup;

    // Resolve the chosen seq against the directory (never trusting caller
    // padding); a seq with no file is indistinguishable from a missing
    // backup.
    std::string chosen;
    for (const BackupFile& file : scan_company_backups(slot))
    {
        if (file.seq == seq)
        {
            chosen = file.filename;
            break;
        }
    }
    if (chosen.empty())
        return CompanyRestoreError::InvalidBackup;

    // Step 0 [SAVE-R3]: this API-level validation is the guard (the UI's
    // corrupt-row marking is not). Aborting here touches nothing.
    const std::optional<CompanyInfo> header =
        read_header_from(kBackupDirPrefix, chosen, slot);
    if (!header.has_value() || !header->valid)
    {
        LogError("restore_company_backup_invalid slot={} seq={}\n", slot, seq);
        return CompanyRestoreError::InvalidBackup;
    }

    // Stage the chosen bytes OUTSIDE the pruning domain first: when the
    // chosen backup is the oldest snapshot at full retention, step 1's
    // prune would reap exactly that file. The staging name carries no .gtl
    // suffix, so neither the company listing nor the backup scan can ever
    // see it; staging a copy destroys nothing.
    const std::string slot_rel = "save/" + slot + ".gtl";
    const std::string staging_rel = slot_rel + ".restoretmp";
    if (!copy_user_file(kBackupDirPrefix + chosen, staging_rel))
    {
        LogError("restore_company_backup_stage_failed slot={} seq={}\n", slot,
                 seq);
        return CompanyRestoreError::CopyFailed;
    }

    // Step 1: the pre-restore state becomes the newest backup (synced by
    // backup_company_capture_seq), so the destructive steps below are
    // preceded by a persisted copy of what they destroy — and the chosen
    // seq can never be reused by a later snapshot. A company file that is
    // already missing has nothing to preserve; the rewind then simply
    // recreates it.
    int pre_restore_seq = 0;
    if (user_file_exists(slot_rel))
    {
        pre_restore_seq = backup_company_capture_seq(slot);
        if (pre_restore_seq <= 0)
        {
            (void)remove_user_file(staging_rel);
            return CompanyRestoreError::PreRestoreBackupFailed;
        }
    }

    // Step 2: tmp+rename byte copy — a failure leaves the slot file
    // untouched (§3.6).
    const bool copied = copy_user_file(staging_rel, slot_rel);
    (void)remove_user_file(staging_rel);
    if (!copied)
    {
        LogError("restore_company_backup_copy_failed slot={} seq={}\n", slot,
                 seq);
        return CompanyRestoreError::CopyFailed;
    }
    sync_filesystem();

    // Step 3: refresh memory + mount the restored campaign. On ANY failure:
    // skip step 4, copy the step-1 backup back over the slot, and reload so
    // disk and memory both hold the pre-restore state again.
    const SaveDataIoError load_error = save.load_with_error(slot);
    if (load_error != SaveDataIoError::None)
    {
        LogError("restore_company_backup_reload_failed slot={} seq={} err={}\n",
                 slot, seq, static_cast<int>(load_error));
        if (pre_restore_seq > 0)
        {
            (void)copy_user_file(
                kBackupDirPrefix + backup_filename(slot, pre_restore_seq),
                slot_rel);
            sync_filesystem();
            (void)save.load_with_error(slot);
        }
        return CompanyRestoreError::ReloadFailed;
    }

    // Step 4: re-stamp last_played so Continue still points at this company
    // (a pure byte copy would resurrect the old timestamp). Re-serializing
    // right after a fresh load makes save()'s cursor side effect a no-op.
    // Deliberately NOT routed through company_autosave (§3.8): the choke
    // point targets the ACTIVE company, while restore may lawfully target
    // any slot — this inline stamp + atomic write is the same
    // WindowEvent-kind operation, addressed by `slot`.
    save.last_played_unix_s = company_clock_now_s();
    if (atomic_company_save(save, slot) != SaveDataIoError::None)
        return CompanyRestoreError::RestampFailed;
    return CompanyRestoreError::None;
}

bool delete_company_backup(const std::string& slot, int seq)
{
    if (!is_safe_virtual_basename(slot))
        return false;
    for (const BackupFile& file : scan_company_backups(slot))
    {
        if (file.seq == seq)
            return remove_user_file(kBackupDirPrefix + file.filename);
    }
    return false;
}

bool delete_company(const std::string& slot)
{
    if (slot == kNetsessionSlot || !is_safe_virtual_basename(slot))
        return false;
    if (slot == active_company_slot())
    {
        LogError("delete_company_refused_active slot={}\n", slot);
        return false; // the UI enforces "switch first"; surfaced as a popup
    }
    // Backups first: an interrupted delete leaves the company itself intact
    // (minus some backups) instead of orphaning hidden backup strays.
    for (const BackupFile& file : scan_company_backups(slot))
        (void)remove_user_file(kBackupDirPrefix + file.filename);
    // Stray staging files: the atomic-write tmp (§3.6), the restore staging
    // copy (§3.7), and copy_user_file's "<dst>.tmp" halves that an
    // interrupted restore can leave behind for either copy destination.
    (void)remove_user_file("save/" + slot + ".tmp.gtl");
    (void)remove_user_file("save/" + slot + ".gtl.restoretmp");
    (void)remove_user_file("save/" + slot + ".gtl.restoretmp.tmp");
    (void)remove_user_file("save/" + slot + ".gtl.tmp");
    (void)remove_user_file("save/" + slot + ".cloudstage.tmp.gtl");
    return remove_user_file("save/" + slot + ".gtl");
}

// ---------------------------------------------------------------------------
// Cloud-save byte IO (issue #155)
// ---------------------------------------------------------------------------

namespace {

// Best-effort removal of a save/-relative staging file, whether og_open_write
// selected the user directory or its cwd fallback (the atomic_company_save
// cleanup pattern).
void remove_save_relative_file(const std::string& relative_path)
{
    (void)remove_user_file(relative_path);
    std::error_code cleanup_ec;
    std::filesystem::remove(std::filesystem::path(relative_path), cleanup_ec);
}

} // namespace

std::optional<std::vector<std::uint8_t>> export_company_bytes(
    const std::string& slot)
{
    // The reserved "netsession" scratch is session state, never a company;
    // it must not leak to the cloud.
    if (slot == kNetsessionSlot || !is_safe_virtual_basename(slot))
        return std::nullopt;

    const std::string filename = slot + ".gtl";
    og::io::OgFilePtr infile = og::io::og_open_read("save/", filename.c_str());
    if (!infile)
        return std::nullopt;

    // Verbatim read of the on-disk file — NEVER a re-serialization
    // (SaveData::save has cursor side effects and would change bytes).
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 4096> chunk{};
    for (;;)
    {
        const std::size_t got = infile->read(chunk.data(), 1, chunk.size());
        bytes.insert(bytes.end(), chunk.begin(),
                     chunk.begin() + static_cast<std::ptrdiff_t>(got));
        if (got < chunk.size())
            break;
    }
    return bytes;
}

CompanyInstallError install_company_bytes(const std::string& slot,
                                          std::span<const std::uint8_t> bytes)
{
    // Step 1: slot safety — downloaded metadata is untrusted input.
    if (slot == kNetsessionSlot || !is_safe_virtual_basename(slot))
        return CompanyInstallError::InvalidSlot;

    // Step 2: stage the bytes. The ".tmp.gtl" suffix keeps the staging file
    // out of list_companies; "cloudstage" keeps it distinct from the §3.6
    // atomic-write staging name so neither flow can eat the other's file.
    const std::string staging_name = slot + ".cloudstage.tmp.gtl";
    const std::string staging_rel = "save/" + staging_name;
    {
        og::io::OgFilePtr outfile =
            og::io::og_open_write("save/", staging_name.c_str());
        if (!outfile ||
            (!bytes.empty() &&
             !og::io::og_write_exact(*outfile, bytes.data(), 1, bytes.size())))
        {
            LogError("install_company_bytes_stage_failed slot={}\n", slot);
            remove_save_relative_file(staging_rel);
            return CompanyInstallError::StageFailed;
        }
    }
    sync_filesystem();

    // Step 3: header-validate the STAGED file with the [SAVE-R1] ladder. A
    // company is never clobbered by junk ([SAVE-R6]); empty and truncated
    // downloads both land here.
    const std::optional<CompanyInfo> header =
        read_header_from("save/", staging_name, slot);
    if (!header.has_value() || !header->valid)
    {
        LogError("install_company_bytes_invalid slot={}\n", slot);
        remove_save_relative_file(staging_rel);
        return CompanyInstallError::InvalidBytes;
    }

    // Step 4: a fresh backup BEFORE anything destructive (§3.7 ordering).
    const std::string slot_rel = "save/" + slot + ".gtl";
    if (user_file_exists(slot_rel) && !backup_company_now(slot))
    {
        LogError("install_company_bytes_backup_failed slot={}\n", slot);
        remove_save_relative_file(staging_rel);
        return CompanyInstallError::BackupFailed;
    }

    // Step 5: rename staging -> slot (the §3.6 whole-file swap), with the
    // og_open_write cwd fallback handled like atomic_company_save.
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path staging_path = fs::path(get_user_path()) / staging_rel;
    fs::path dst_path = fs::path(get_user_path()) / slot_rel;
    if (!fs::exists(staging_path, ec))
    {
        staging_path = fs::path(staging_rel);
        dst_path = fs::path(slot_rel);
    }
    fs::rename(staging_path, dst_path, ec);
    if (ec)
    {
        LogError("install_company_bytes_rename_failed slot={} error={}\n",
                 slot, ec.message());
        remove_save_relative_file(staging_rel);
        return CompanyInstallError::RenameFailed;
    }
    sync_filesystem();
    return CompanyInstallError::None;
}

// ---------------------------------------------------------------------------
// Autosave choke point (§3.8)
// ---------------------------------------------------------------------------

namespace {

// Restores the ambient campaign mount on scope exit so the merge write is
// mount-neutral on EVERY exit path. The merge's private load() mounts the
// PRIVATE save's campaign — and can UNMOUNT the ambient (host) campaign even
// when it FAILS: load_campaign_with_error unmounts the old package before
// mounting the new one, so an unmountable private campaign leaves nothing
// mounted while the lobby menu is still showing the host's campaign. A plain
// restore block after the write would (and once did) miss the load-failure
// early return.
class ScopedAmbientMountRestore
{
public:
    ScopedAmbientMountRestore() : mounted_before_(get_mounted_campaign()) {}
    ScopedAmbientMountRestore(const ScopedAmbientMountRestore&) = delete;
    ScopedAmbientMountRestore& operator=(const ScopedAmbientMountRestore&) =
        delete;
    ~ScopedAmbientMountRestore()
    {
        const std::string mounted_after = get_mounted_campaign();
        if (mounted_after == mounted_before_)
            return;
        if (mounted_before_.empty())
        {
            (void)unmount_campaign_package_with_error(mounted_after);
        }
        else
        {
            std::map<std::string, int> scratch_levels;
            if (load_campaign(mounted_before_, scratch_levels) < 0)
            {
                LogError("company_autosave_remount_failed campaign={}\n",
                         mounted_before_);
            }
        }
    }

private:
    std::string mounted_before_;
};

// [SAVE-F1] owner-preserving merge write. While a networked lobby is active
// the in-memory save has been overwritten by apply_lobby_state_to_save with
// the HOST's campaign/scen_num/settings; a plain save() would also rewrite
// current_levels[host campaign] to the host's level — silently rewinding the
// client's solo cursor and settings on disk. So: load the private company
// file, overlay ONLY the machine's own roster and its owned teams' wallets,
// stamp, atomic-save. Everything else — current_campaign, scen_num,
// current_levels, difficulty, ctf_*/respawn_*/tower_* settings, my_team,
// allied_mode — stays as the disk holds it, simply by never being touched on
// the loaded copy. The local player-count choice is session-only and is never
// derived from or encoded by the file's fixed legacy marker.
SaveDataIoError company_autosave_merge_networked_lobby(
    const SaveData& session_save,
    const CompanyAutosaveContext& context,
    const std::string& slot)
{
    // load() below mounts the PRIVATE save's campaign (and unmounts the
    // ambient one even on failure); the open lobby menu is showing the
    // HOST's. The guard restores the ambient mount on every exit path,
    // including the load-failure early return below.
    ScopedAmbientMountRestore mount_guard;

    SaveData merged;
    const SaveDataIoError load_error = merged.load_with_error(slot);
    if (load_error != SaveDataIoError::None)
    {
        // No untouched private baseline to merge into: never clobber the
        // company file with host-synced combined state ([SAVE-F1]; the
        // [SAVE-R7] lobby-entry baseline guarantees the file exists in
        // real flows).
        LogError("company_autosave_merge_load_failed slot={} error={}\n",
                 slot, static_cast<int>(load_error));
        return load_error;
    }

    // (a) The machine's own roster. In a networked lobby the in-memory
    // team_list is private to this machine (remote players never enter it),
    // so a wholesale deep copy carries hires, training, renames and the
    // per-guy deployed flags while dropping nothing of anyone else's.
    for (std::size_t index = 0; index < merged.team_list.size(); ++index)
    {
        merged.team_list[index] =
            session_save.team_list[index]
                ? std::make_unique<guy>(*session_save.team_list[index])
                : nullptr;
    }
    merged.team_size = session_save.team_size;

    // (b) The owned session teams' wallets (post-hire/train spend) and their
    // legacy scalar mirrors — same-index overlay, the
    // persist_network_win_to_save0 precedent. Non-owned teams keep the disk
    // values.
    bool primary_team_assigned = false;
    for (std::size_t team = 0; team < context.owned_teams.size(); ++team)
    {
        if (!context.owned_teams[team])
            continue;
        merged.m_totalcash[team] = session_save.m_totalcash[team];
        merged.m_totalscore[team] = session_save.m_totalscore[team];
        if (!primary_team_assigned)
        {
            merged.totalcash = session_save.m_totalcash[team];
            merged.totalscore = session_save.m_totalscore[team];
            primary_team_assigned = true;
        }
    }

    merged.last_played_unix_s = company_clock_now_s();
    return atomic_company_save(merged, slot);
}

} // namespace

SaveDataIoError company_autosave(SaveData& save, CompanyAutosaveKind kind)
{
    return company_autosave(save, kind, CompanyAutosaveContext{});
}

SaveDataIoError company_autosave(SaveData& save,
                                 CompanyAutosaveKind kind,
                                 const CompanyAutosaveContext& context)
{
    // The active slot can never be "netsession" (the setter rejects it), so
    // the server-economy scratch structurally never gains timestamps or
    // backups through this path.
    const std::string slot = active_company_slot();

    SaveDataIoError write_error;
    if (context.networked_lobby)
    {
        write_error = company_autosave_merge_networked_lobby(
            save, context, slot);
    }
    else
    {
        save.last_played_unix_s = company_clock_now_s();
        write_error = atomic_company_save(save, slot);
    }

    if (write_error != SaveDataIoError::None)
    {
        LogError("company_autosave_failed kind={} slot={} error={}\n",
                 static_cast<int>(kind), slot,
                 static_cast<int>(write_error));
        return write_error;
    }

    if (kind == CompanyAutosaveKind::LevelWin)
    {
        // §3.7: every level win snapshots the freshly written company —
        // exactly one retention-pruned byte copy per call. A snapshot
        // failure is logged but does not fail the autosave (the save itself
        // landed; retention is retried on the next win).
        if (!backup_company_now(slot))
            LogError("company_autosave_backup_failed slot={}\n", slot);
    }
    return SaveDataIoError::None;
}

} // namespace og::data
