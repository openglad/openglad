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

#include <openglad/resources/save_data.h>

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
// listsize/numplayers all mark the entry corrupt (but still listable, so the
// UI can surface it — the never-silently-switch policy). A corrupt header
// keeps whatever fields were parsed before the failure and last_played 0.
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

} // namespace og::data
