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

#pragma once

#include <openglad/core/constants.h>
#include <openglad/resources/save_data.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Company layer (docs/company-basecamp-design.md §3). A save file IS a
// company; this header collects the company-level helpers that sit above the
// raw GTL serialization in SaveData.

namespace og::data {

// Wall-clock seam (§3.2) for the GTL v14 `last_played_unix_s` field.
//
// This is the ONLY place company code reads wall time. It is parity-safe by
// construction: og_gameplay cannot include og_resources (dependency direction
// og_resources -> og_gameplay), the parity harness performs zero save IO, and
// SaveData::save()/load() merely serialize the field — they never call this.
// Nothing in the deterministic sim can ever observe the clock.
//
// Returns the current wall-clock time as unix seconds, or the fixed test
// value when one is installed.
std::int64_t company_clock_now_s();

// Installs (or, with std::nullopt, removes) a fixed wall-clock value so tests
// can pin `last_played_unix_s` deterministically.
void set_company_clock_for_tests(std::optional<std::int64_t> fixed_now_s);

// ---------------------------------------------------------------------------
// Active-company indirection (§3.4)
// ---------------------------------------------------------------------------

// The process-wide save slot every company-level read/write targets (the
// former hard-coded "save0" literals). Defaults to "save0", so every legacy
// flow, test, demo, and wasm-e2e seed is byte-identical until something calls
// the setter. The transient "netsession" server-economy slot stays a literal
// at its call sites and can never become the active company.
const std::string& active_company_slot();

// Repoints the active company. Rejects (returning false, keeping the current
// slot) names that fail is_safe_virtual_basename or the reserved
// "netsession" slot.
bool set_active_company_slot(const std::string& slot);

// RAII test guard: swaps the active company in, restores the previous slot on
// scope exit. An invalid slot leaves the active company unchanged (and
// applied() reports the rejection). Tests should ALSO rely on the test-main
// fixture reset — convention-only RAII is the known cfg-clobber failure class
// under --gtest_shuffle ([SAVE-R8]).
class ScopedActiveCompany
{
public:
    explicit ScopedActiveCompany(const std::string& slot);
    ~ScopedActiveCompany();
    ScopedActiveCompany(const ScopedActiveCompany&) = delete;
    ScopedActiveCompany& operator=(const ScopedActiveCompany&) = delete;
    bool applied() const { return applied_; }

private:
    std::string previous_;
    bool applied_;
};

// Derives a filesystem-safe slot from a display name: [A-Za-z0-9] lowercased,
// space/'-'/'_' become '-', everything else dropped, runs of '-' collapsed and
// trimmed, empty -> "company", truncated to 40. Generated slugs never contain
// dots (keeps backup-name parsing unambiguous) and never collide: "netsession"
// is reserved and an existing save/<slug>.gtl pushes to "-2".."-99", then
// "-<epoch seconds>". The display name itself lives ONLY in the 40-byte
// save_name field.
std::string derive_company_slot(const std::string& display_name);

// ---------------------------------------------------------------------------
// Header-only company scan (§3.5)
// ---------------------------------------------------------------------------

// Company identity summarized from the first <= 164 bytes of a GTL file.
// `valid` mirrors what SaveData::load would decide about the header: bad
// magic, an unsupported version, a truncated header, or out-of-range
// listsize/legacy player-count compatibility byte all mark the entry corrupt
// (but still listable, so the UI can surface it — the
// never-silently-switch policy). A corrupt header keeps whatever fields were
// parsed before the failure and last_played 0.
struct CompanyInfo
{
    std::string slot;
    std::string display_name;
    std::string campaign_id;
    short scen_num = 0;
    std::uint32_t totalcash = 0;
    int roster_size = 0;
    std::uint8_t version = 0;
    std::int64_t last_played_unix_s = 0;
    bool valid = false;
};

// Reads the version-gated header ladder of save/<slot>.gtl (replicating
// SaveData::load's prefix exactly, [SAVE-R1]) without loading the roster and
// WITHOUT mounting the save's campaign. Returns std::nullopt when the file is
// missing (or the slot name is unsafe); a present-but-corrupt file returns a
// CompanyInfo with valid == false.
std::optional<CompanyInfo> read_company_header(const std::string& slot);

// All companies in save/: every *.gtl except netsession.gtl and the
// *.tmp.gtl atomic-write staging files ([SAVE-R6]). Sorted most-recent-first
// by last_played_unix_s; ties break "save0" first, then slot ascending —
// deterministic for every input. Uses the PhysFS listing when the filesystem
// layer is initialized, else falls back to std::filesystem on the user path.
std::vector<CompanyInfo> list_companies();

// The slot Continue should open at startup: the first entry of
// list_companies(), or "" when no company exists. Deliberately returns the
// most recent entry even when it is corrupt — the caller must check
// read_company_header(...)->valid and surface the damage instead of silently
// switching companies ([SAVE-R6]).
std::string select_startup_company();

// ---------------------------------------------------------------------------
// Atomic company write (§3.6)
// ---------------------------------------------------------------------------

// Serializes `save` to save/<slot>.tmp.gtl, then renames it over
// save/<slot>.gtl, so a torn write can never produce a header-invalid
// most-recent company (IndexedDB snapshots either the old or the new whole
// file). Refuses unsafe slot names and the reserved "netsession" slot (the
// server-economy scratch keeps its plain SaveData::save writes).
[[nodiscard]] SaveDataIoError atomic_company_save(SaveData& save,
                                                  const std::string& slot);

// ---------------------------------------------------------------------------
// Backups (§3.7)
// ---------------------------------------------------------------------------

// Retention per company: backup_company_now prunes the LOWEST sequence
// numbers until at most this many snapshots remain.
inline constexpr int kCompanyBackupRetention = 20;

// One snapshot file save/backups/<slot>.<SEQ>.gtl. Every backup is a byte
// copy of a company file, so `header` carries the same identity a company
// row does (header-scanned like a company, never mounted; header.valid ==
// false marks a corrupt snapshot but keeps it listable).
struct CompanyBackupInfo
{
    std::string slot;
    int seq = 0;
    std::string filename; // "<slot>.<SEQ>.gtl", relative to save/backups/
    CompanyInfo header;
};

// All backups of `slot`, newest (highest seq) first. The seq is the
// rightmost all-digit dot-token of the filename, so slots containing dots
// stay unambiguous ("led.7.004.gtl" belongs to slot "led.7", never "led").
// An unsafe slot name yields an empty list.
std::vector<CompanyBackupInfo> list_company_backups(const std::string& slot);

// Byte-copies save/<slot>.gtl to save/backups/<slot>.<next seq>.gtl, where
// next seq = max(existing) + 1 is derived from the directory so a rewind can
// never reuse a seq. The snapshot is synced BEFORE the lowest seqs are
// pruned down to kCompanyBackupRetention (§3.7 torn-write ordering: every
// destructive step is preceded by a persisted copy, so the worst
// interruption outcome is one extra backup). Snapshots are byte copies,
// never re-serializations (save() has a cursor side effect), and are not
// validated here — restore validates. Returns false — leaving no trace —
// for unsafe slots, the reserved "netsession" scratch (the level-win
// producer's no-op contract), and a missing company file.
bool backup_company_now(const std::string& slot);

// Why restore_company_backup stopped (§3.7 [SAVE-R3]). Every error except
// RestampFailed leaves the on-disk slot file — and, for ReloadFailed via the
// rollback, the in-memory SaveData — holding the pre-restore state.
// RestampFailed means the rewind itself finished (disk and memory hold the
// restored company) but the final timestamp write failed.
enum class CompanyRestoreError
{
    None = 0,
    InvalidBackup,          // step 0: missing/corrupt backup — nothing touched
    PreRestoreBackupFailed, // step 1: could not snapshot the current state
    CopyFailed,             // step 2: copy failed — slot file untouched
    ReloadFailed,           // step 3: reload failed — rolled back to the
                            //         step-1 backup on disk and in memory
    RestampFailed,          // step 4: restored, but the re-stamp write failed
};

// Validated rewind-in-place (§3.7 [SAVE-R3]):
//   0. header-validate the CHOSEN backup — the API-level check is the guard
//      (the UI's corrupt-row marking is not); abort touches nothing. The
//      chosen bytes are then staged outside the backups directory, because
//      restoring the OLDEST snapshot at full retention would otherwise see
//      step 1's prune reap the chosen file itself (the rewound state still
//      lands in the slot; only the pruned snapshot file is gone afterward).
//   1. backup_company_now(slot) — the pre-restore state becomes the newest
//      backup (synced) before anything destructive runs.
//   2. tmp+rename the staged backup bytes over save/<slot>.gtl.
//   3. save.load_with_error(slot) — refresh memory + mount the restored
//      campaign. On ANY failure: skip step 4, copy the step-1 backup back
//      over the slot and reload, so disk and memory hold the pre-restore
//      state again.
//   4. re-stamp last_played and atomic-save so Continue still points at this
//      company (a pure byte copy would resurrect the old timestamp).
[[nodiscard]] CompanyRestoreError restore_company_backup(SaveData& save,
                                                         const std::string& slot,
                                                         int seq);

// Deletes one snapshot (deletion synced). False when no such backup exists.
bool delete_company_backup(const std::string& slot, int seq);

// Deletes save/<slot>.gtl plus ALL its backups and any stray staging file
// (each deletion synced; backups are reaped first so an interruption leaves
// the company intact rather than orphaning hidden backups). Refuses the
// currently-active slot — the UI enforces "switch first", and the false
// return is itself popup-surfaced so a UI slip stays visible — and the
// reserved "netsession" scratch. Returns whether the company file itself
// was removed (stray backups of an already-missing company are still
// reaped).
bool delete_company(const std::string& slot);

// ---------------------------------------------------------------------------
// Autosave choke point (§3.8)
// ---------------------------------------------------------------------------

// Why an autosave ran. The kind decides the backup policy: LevelWin also
// snapshots the freshly written company into save/backups/ (exactly once per
// call — the level-win producers fire once per win); BaseCampMutation and
// WindowEvent (minimize/close, the lobby-entry baseline, the local shadow
// finalize) stamp + write only, never snapshot.
enum class CompanyAutosaveKind
{
    BaseCampMutation,
    LevelWin,
    WindowEvent,
};

// [SAVE-F1] session context, provided by the CALLER. og_resources cannot see
// the lobby layer, so the picker/lobby glue reports whether a networked
// lobby currently owns the in-memory save (apply_lobby_state_to_save has
// overwritten campaign/scen_num/settings with the HOST's values) and which
// session team indexes this machine's seats own. og::ui::
// company_autosave_context (picker_common) builds this for menu callers.
struct CompanyAutosaveContext
{
    // True while a networked lobby is active: the write must become an
    // owner-preserving MERGE (below), never a plain save of the host-synced
    // combined state.
    bool networked_lobby = false;
    // Session team indexes whose wallets this machine's seats own; consulted
    // only on the merge path.
    std::array<bool, SCORE_TEAM_COUNT> owned_teams{};
};

// The single autosave choke point (§3.8): every company write that is not an
// explicit user save funnels through here. Stamps `save.last_played_unix_s`
// from the wall-clock seam and atomic-writes (§3.6) to the ACTIVE company
// slot; LevelWin then snapshots the written file via backup_company_now.
// Failures are logged and returned; callers surface them but don't crash —
// EXCEPT the lobby-entry baseline [SAVE-R7], which keeps its hard gate
// (popup + refuse to enter the networked session).
//
// [SAVE-F1] With context.networked_lobby set, the write routes through an
// owner-preserving merge instead (the persist_private_campaign_cursor
// pattern): the private company file is loaded from disk and only (a) the
// machine's own roster (team_list entries incl. their deployed flags — in a
// networked lobby the in-memory roster IS private to this machine) and
// (b) the owned session teams' m_totalcash/m_totalscore plus their legacy
// scalar mirrors are overlaid from `save`; current_campaign, scen_num,
// current_levels, difficulty and all ctf_*/respawn_*/tower_* settings,
// my_team and allied_mode stay as the DISK holds them. The local player-count
// choice is session-only and is never derived from or encoded by the file's
// fixed legacy marker. The timestamp is stamped on the merged copy (the
// in-memory session save is left untouched), and the campaign mount is
// restored afterwards, so the open lobby menu never loses its session
// campaign. A missing/unreadable private file aborts with the load error —
// the merge never clobbers (the [SAVE-R7] baseline guarantees the file exists
// in real flows).
[[nodiscard]] SaveDataIoError company_autosave(SaveData& save,
                                               CompanyAutosaveKind kind);
[[nodiscard]] SaveDataIoError company_autosave(
    SaveData& save,
    CompanyAutosaveKind kind,
    const CompanyAutosaveContext& context);

} // namespace og::data
