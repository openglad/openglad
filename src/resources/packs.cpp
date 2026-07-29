/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/resources/packs.h>

#include <openglad/core/family_presentation.h>
#include <openglad/core/order.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_coverage.h>
#include <openglad/resources/classpack_yaml.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/physfs_api.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace og::resources {

namespace {
// Counters behind pack_install_stats(). Plain increments on paths that
// already do file I/O; nothing reads them but tests and diagnostics.
PackInstallStats g_pack_install_stats;
}  // namespace

PackInstallStats pack_install_stats()
{
    return g_pack_install_stats;
}

void reset_pack_install_stats()
{
    g_pack_install_stats = PackInstallStats{};
}

int refresh_pack_scripts()
{
    g_pack_install_stats.refreshes++;
    og::script::clear_pack_scripts();
    og::script::clear_pack_lib_modules();
    const int scripts = register_mounted_pack_scripts();
    // Mount changes can add/remove classpack.yaml data too (campaign-
    // embedded packs); reinstall keeps registries in step with scripts.
    install_classpacks();
    return scripts;
}

int register_mounted_pack_scripts()
{
    int registered = 0;
    int modules = 0;
    // Sorted enumeration keeps the replay order identical on every peer.
    for (const std::string& pack_id :
         og::io::physfs_enumerate_files_sorted("packs")) {
        // lib/ modules first (og.use): same deterministic order contract as
        // scripts — pack-id-lexicographic, then filename-lexicographic —
        // and the same coverage-inventory declaration, because a module is
        // engine-loaded pack Lua exactly like a script (every VM compiles
        // and runs each one once, before any script).
        const std::string lib_dir = "packs/" + pack_id + "/lib";
        for (const std::string& file :
             og::io::physfs_enumerate_files_sorted(lib_dir)) {
            if (file.size() < 4 ||
                file.compare(file.size() - 4, 4, ".lua") != 0)
                continue;
            const std::string vpath = lib_dir + "/" + file;
            std::vector<std::uint8_t> bytes = read_file(vpath.c_str());
            if (bytes.empty()) {
                LogWarn("class pack lib module unreadable: {}\n", vpath);
                continue;
            }
            std::string source(reinterpret_cast<const char*>(bytes.data()),
                               bytes.size());
            if (og::script::coverage::enabled()) {
                og::script::coverage::declare_pack_source(
                    vpath, source, og::io::physfs_real_dir(vpath));
            }
            og::script::register_pack_lib_module(
                {pack_id, file.substr(0, file.size() - 4), vpath,
                 std::move(source)});
            modules++;
        }
        const std::string scripts_dir = "packs/" + pack_id + "/scripts";
        for (const std::string& file :
             og::io::physfs_enumerate_files_sorted(scripts_dir)) {
            if (file.size() < 4 ||
                file.compare(file.size() - 4, 4, ".lua") != 0)
                continue;
            const std::string vpath = scripts_dir + "/" + file;
            std::vector<std::uint8_t> bytes = read_file(vpath.c_str());
            if (bytes.empty()) {
                LogWarn("class pack script unreadable: {}\n", vpath);
                continue;
            }
            std::string source(reinterpret_cast<const char*>(bytes.data()),
                               bytes.size());
            // THE coverage inventory. This loop — not a scan of the host
            // filesystem — is the definition of "a pack script the engine can
            // load": the file may live in packs/ on disk, inside a campaign
            // .glad, or in a generated archive that only ever existed as a
            // C++ string literal, and PhysFS makes all three look the same
            // here. Recording the source at the one place they converge is
            // what stops such a script from being invisible to the gate.
            // The real dir is what tells a shipped pack from a synthetic one
            // a test mounted out of a temp directory. Guarded rather than
            // relying on declare_pack_source's own early-out, so that with
            // the recorder off this whole thing costs one bool load and
            // never asks PhysFS anything.
            if (og::script::coverage::enabled()) {
                og::script::coverage::declare_pack_source(
                    vpath, source, og::io::physfs_real_dir(vpath));
            }
            og::script::register_pack_script(
                {pack_id, vpath, std::move(source)});
            registered++;
        }
    }
    if (registered > 0)
        Log("Registered {} class pack script(s)\n", registered);
    if (modules > 0)
        Log("Registered {} class pack lib module(s)\n", modules);
    return registered;
}

// ---------------------------------------------------------------------------
// classpack.yaml → registry install
// ---------------------------------------------------------------------------

namespace {

// Owns every installed ClasspackData for the life of the process, plus the
// name-pool pointer arrays the FamilyDescriptor name_pool field borrows.
// Descriptor const char* fields point straight into the stored structs'
// std::strings (stable: the unique_ptr fixes each ClasspackData's address
// and entries are never mutated after install; the deque never moves
// existing pools). DELIBERATELY leaked — never destroyed — because the
// gameplay registries keep these borrows through static teardown.
// (LeakSanitizer counts a reachable singleton as "still reachable", not a
// leak.)
struct ClasspackStore {
    std::vector<std::unique_ptr<og::data::ClasspackData>> packs;
    std::deque<std::vector<const char*>> name_pools;
    // Pack-defined animation tables. anim_rows holds one -1-terminated
    // frame list per row; anim_tables holds the row-pointer arrays the
    // descriptors' anim_table field points at. Both deques are append-only
    // and their elements are never mutated after the push, so every pointer
    // handed to a descriptor stays valid for the life of the process —
    // exactly the contract the borrowed string fields already rely on.
    std::deque<std::vector<signed char>> anim_rows;
    std::deque<std::vector<const signed char*>> anim_tables;
};

ClasspackStore& classpack_store()
{
    static ClasspackStore* store = new ClasspackStore();
    return *store;
}

// One pack's YAML text, already parsed.
//
// WHY: install_classpacks() runs on EVERY mount and EVERY unmount (see
// refresh_pack_scripts), and a single campaign switch is four of those —
// CampaignData::load() unmounts the old campaign, mounts the new one, then
// puts the old one back. Re-READING the tree each time is the point (a
// campaign .glad may carry its own packs/, so the bytes really can change);
// re-PARSING identical bytes is pure waste, and after the core pack moved
// from one classpack.yaml to 73 families/*.yaml it became the dominant cost
// of the whole call: ~9.6 ms of a 14.7 ms install under ASan, ~1.0 of 2.2
// plain. It was also an unbounded leak — every pass pushed another copy of
// the parsed data into the process-lifetime store, whose entries are never
// freed, so a session that switched campaigns kept accumulating megabytes of
// identical descriptor text.
//
// The memo caches ONLY the parse. Every install still resets the registries
// and reinstalls every entry from scratch, in the same order, with the same
// wire-id counter — so a hit is observationally identical to a miss except
// for the missing work. `data` points into classpack_store(), which is never
// destroyed and whose entries never move, so the pointer (and every
// descriptor field borrowing its strings) stays valid for the process.
//
// The one deliberate difference: a doc that parses successfully but logs a
// warning while doing so (an unresolved reference, say) logs it once rather
// than once per mount. A doc that FAILS to parse rejects its pack and is
// never memoized, so the rejection warning still fires on every pass.
struct ParsedPack {
    std::string signature;  // length-framed vpath+bytes of every doc, in order
    const og::data::ClasspackData* data = nullptr;  // owned by the store
};

// Leaked with the store it points into, for the same reason.
std::map<std::string, ParsedPack>& parsed_pack_memo()
{
    static auto* memo = new std::map<std::string, ParsedPack>();
    return *memo;
}

// Deterministic wire-id assignment for one install run: core packs pin
// explicit ids; wire_id auto/absent takes the next id >= 21 per order in
// encounter order (packs are visited pack-id-lexicographically, entries in
// YAML order). Ids 0..20 are reserved for the core pins, and every install
// pass starts from a registry whose mod slots were just freed, so the
// counter reproduces the same assignment on every peer. A pack that pins an
// id >= 21 explicitly can still collide with an auto id — pin the whole
// pack or none of it. Entries past a registry's capacity (256 ids per
// order) are rejected by the slot lookup.
struct AutoWireIds {
    std::int32_t next[8] = {21, 21, 21, 21, 21, 21, 21, 21};

    int take(Order order)
    {
        return next[static_cast<int>(order)]++;
    }
};

int resolve_wire_id(const std::string& wire_id, Order order,
                    AutoWireIds& autos, const std::string& id)
{
    if (wire_id.empty() || wire_id == "auto")
        return autos.take(order);
    const auto parsed = parse_int_strict(wire_id);
    if (!parsed || *parsed < 0 || *parsed > 255) {
        LogWarn("classpack {}: bad wire_id '{}' — entry skipped\n", id,
                wire_id);
        return -1;
    }
    return *parsed;
}

// Every wire id one pack claims, resolved BEFORE anything installs.
//
// Two jobs. First, the ids are taken in a fixed sequence (entries in YAML
// order, per order counter), which is what makes auto ids reproducible on
// every peer — so they must be taken exactly once, here, and handed to the
// installers rather than re-derived. Second, and the reason this exists at
// all: a family may reference another family THE SAME PACK DECLARES LOWER
// DOWN THE FILE. The core pack does it (core:mage promotes_to core:archmage,
// fourteen entries below it), and a registry lookup cannot see a slot that
// has not been installed yet, so without this table the reference would
// resolve or not depending on where the target happens to sit in the file.
struct PackWireIds {
    std::vector<int> ids[8];  // per order, parallel to the pack's entry lists
    std::vector<std::pair<std::string, int>> declared[8];

    void take(Order order, const std::string& declared_id,
              const std::string& wire_id, AutoWireIds& autos)
    {
        const int id = resolve_wire_id(wire_id, order, autos, declared_id);
        ids[static_cast<int>(order)].push_back(id);
        if (id >= 0 && !declared_id.empty())
            declared[static_cast<int>(order)].emplace_back(declared_id, id);
    }

    // The wire id this pack gave `ref`, or -1. Matches the fully-qualified
    // `id:` exactly; a bare-name reference still goes through the registry.
    [[nodiscard]] int find(Order order, const std::string& ref) const
    {
        for (const auto& [declared_id, id] : declared[static_cast<int>(order)]) {
            if (declared_id == ref)
                return id;
        }
        return -1;
    }
};

// Resolves a family reference ("core:knife") against the pack's own
// declarations first, then the given order's registry. Unresolved references
// keep the current descriptor value.
int resolve_ref(Order order, const std::string& ref, const char* field,
                const std::string& id, const PackWireIds& wire_ids)
{
    const int same_pack = wire_ids.find(order, ref);
    if (same_pack >= 0)
        return same_pack;
    const int target =
        og::families::resolve_family_string_id(order, ref.c_str());
    if (target < 0)
        LogWarn("classpack {}: unresolved {} '{}' — field kept\n", id, field,
                ref);
    return target;
}

// Folds init_bit_flags names into a mask. An unknown name keeps the
// current descriptor mask (warn, no partial fold).
bool fold_bit_flags(const std::vector<std::string>& names,
                    std::int32_t& out, const std::string& id)
{
    std::int32_t flags = 0;
    for (const std::string& name : names) {
        const std::int32_t bit = og::families::bit_flag_from_name(name);
        if (bit == 0) {
            LogWarn(
                "classpack {}: unknown bit flag '{}' — init_bit_flags "
                "kept\n",
                id, name);
            return false;
        }
        flags |= bit;
    }
    out = flags;
    return true;
}

// The stored entry's string (owned by the ClasspackStore) or nullptr for
// an explicit YAML null.
const char* nullable_cstr(const og::data::NullableString& s)
{
    return s.is_null ? nullptr : s.value.c_str();
}

// Converts a parsed `tuning:` map into the gameplay-side store's shape and
// installs it for the slot. ALWAYS called for an installed entry: tuning
// belongs to the family definition occupying the wire slot, so an entry
// without tuning clears whatever a previously installed pack left there
// (set_family_tuning erases on empty). Scripts read the result through
// og.tuning(self) as a frozen table.
void install_family_tuning(
    Order order, int id,
    const std::vector<og::data::ClasspackTuningPair>& tuning)
{
    og::script::TuningMap map;
    map.reserve(tuning.size());
    for (const og::data::ClasspackTuningPair& pair : tuning) {
        og::script::TuningValue value;
        switch (pair.value.kind) {
            case og::data::ClasspackTuningValue::Kind::Integer:
                value.kind = og::script::TuningValue::Kind::Integer;
                value.integer = pair.value.integer;
                break;
            case og::data::ClasspackTuningValue::Kind::Number:
                value.kind = og::script::TuningValue::Kind::Number;
                value.number = pair.value.number;
                break;
            case og::data::ClasspackTuningValue::Kind::Boolean:
                value.kind = og::script::TuningValue::Kind::Boolean;
                value.boolean = pair.value.boolean;
                break;
            case og::data::ClasspackTuningValue::Kind::String:
                value.kind = og::script::TuningValue::Kind::String;
                value.string = pair.value.string;
                break;
        }
        map.push_back({pair.key, std::move(value)});
    }
    og::script::set_family_tuning(order, id, std::move(map));
}

// Stamps the entry's fully-qualified `id:` onto the descriptor — the id
// og::families::resolve_family_string_id matches FIRST, which is what makes
// two packs' same-named families addressable ("alpha:warlock" vs
// "beta:warlock"). The returned pointer borrows the stored entry's string,
// same process-lifetime contract as `name` and every other const char*
// field (see ClasspackStore).
//
// The wire slot is the identity, so installing over a slot that was already
// declared under a DIFFERENT id retires the old id: it stops resolving
// (except through the bare-name fallback, if the display name survives).
// That is the supported way to REPLACE a stock family, and a common wire_id
// typo, so it warns. Tweaking a stock family in place — the reskin case —
// means keeping its id: declare `id: core:soldier` with `wire_id: 0` and
// nothing about resolution changes.
const char* claim_declared_id(const std::string& id, const char* current,
                              const char* order_name)
{
    if (id.empty())
        return current;  // no id declared: keep whatever the slot had
    if (current != nullptr && id != current)
        LogWarn(
            "classpack {}: {} wire slot was declared as '{}' — that family "
            "id no longer resolves\n",
            id, order_name, current);
    return id.c_str();
}

// Builds the name_pool pointer array for one entry in the store; the
// pointers reference the stored entry's names vector.
void apply_name_pool(const std::vector<std::string>& names,
                     FamilyDescriptor& d)
{
    if (names.empty()) {
        d.name_pool = nullptr;
        d.name_pool_size = 0;
        return;
    }
    std::vector<const char*> pool;
    pool.reserve(names.size());
    for (const std::string& name : names)
        pool.push_back(name.c_str());
    classpack_store().name_pools.push_back(std::move(pool));
    d.name_pool = classpack_store().name_pools.back().data();
    d.name_pool_size =
        static_cast<int>(classpack_store().name_pools.back().size());
}

// --- pack-defined animation sets ---------------------------------------

// Bounds on a pack table. kMaxAnimRows keeps ani_count (and therefore the
// animate() index arithmetic it bounds) inside a sane range; kMaxAnimFrames
// caps one row's length; frame indices must fit a signed char and stay
// clear of the -1 end sentinel.
inline constexpr std::size_t kMaxAnimRows = 256;
inline constexpr std::size_t kMaxAnimFrames = 255;
inline constexpr std::int32_t kMaxAnimFrameIndex = 127;

// One pack's materialized `anims:` sets, looked up by name.
struct PackAnims {
    struct Entry {
        std::string name;
        const signed char* const* table = nullptr;
        int rows = 0;
    };
    std::vector<Entry> entries;

    const Entry* find(const std::string& name) const
    {
        for (const Entry& e : entries)
            if (e.name == name)
                return &e;
        return nullptr;
    }
};

// Builds the row storage + row-pointer array for every set the pack
// declares. A malformed set warns and is dropped; families naming it then
// keep whatever animation they already had.
PackAnims materialize_anims(const og::data::ClasspackData& pack)
{
    PackAnims out;
    for (const og::data::ClasspackAnimSet& set : pack.anims) {
        if (set.frames.empty()) {
            LogWarn("classpack anims '{}': no frame rows — set skipped\n",
                    set.name);
            continue;
        }
        std::size_t rows = set.frames.size();
        if (set.rows) {
            if (*set.rows < static_cast<std::int32_t>(set.frames.size())) {
                LogWarn(
                    "classpack anims '{}': rows {} is below the {} declared "
                    "rows — set skipped\n",
                    set.name, *set.rows, set.frames.size());
                continue;
            }
            rows = static_cast<std::size_t>(*set.rows);
        }
        if (rows > kMaxAnimRows) {
            LogWarn("classpack anims '{}': {} rows exceeds the {} cap — set "
                    "skipped\n",
                    set.name, rows, kMaxAnimRows);
            continue;
        }
        if (out.find(set.name) != nullptr) {
            LogWarn("classpack anims '{}': duplicate set name — kept the "
                    "first\n",
                    set.name);
            continue;
        }

        bool ok = true;
        std::vector<const signed char*> declared;
        declared.reserve(set.frames.size());
        for (const og::data::ClasspackAnimRow& row : set.frames) {
            if (row.is_null) {
                declared.push_back(nullptr);
                continue;
            }
            if (row.frames.empty() || row.frames.size() > kMaxAnimFrames) {
                LogWarn("classpack anims '{}': a row has {} frames (1..{})\n",
                        set.name, row.frames.size(), kMaxAnimFrames);
                ok = false;
                break;
            }
            std::vector<signed char> data;
            data.reserve(row.frames.size() + 1);
            for (const std::int32_t frame : row.frames) {
                if (frame < 0 || frame > kMaxAnimFrameIndex) {
                    LogWarn("classpack anims '{}': frame {} outside 0..{}\n",
                            set.name, frame, kMaxAnimFrameIndex);
                    ok = false;
                    break;
                }
                data.push_back(static_cast<signed char>(frame));
            }
            if (!ok)
                break;
            data.push_back(-1);  // end-of-row sentinel
            classpack_store().anim_rows.push_back(std::move(data));
            declared.push_back(classpack_store().anim_rows.back().data());
        }
        if (!ok) {
            LogWarn("classpack anims '{}': set skipped\n", set.name);
            continue;
        }

        // Expand to the requested row count; declared rows repeat cyclically
        // so `rows: 16` over one row reproduces the legacy uniform tables.
        std::vector<const signed char*> table;
        table.reserve(rows);
        for (std::size_t i = 0; i < rows; i++)
            table.push_back(declared[i % declared.size()]);
        classpack_store().anim_tables.push_back(std::move(table));
        out.entries.push_back(
            PackAnims::Entry{set.name,
                             classpack_store().anim_tables.back().data(),
                             static_cast<int>(rows)});
    }
    return out;
}

// Points a descriptor at one of this pack's animation sets. Used by the
// four non-living orders, whose `animation:` can only name a pack set (the
// built-in FamilyAnimationType tables are living-shaped).
void apply_pack_animation(const PackAnims& anims, const std::string& name,
                          const std::string& id,
                          const signed char* const*& table, int& row_count)
{
    if (const PackAnims::Entry* set = anims.find(name)) {
        table = set->table;
        row_count = set->rows;
        return;
    }
    LogWarn("classpack {}: no animation set '{}' in this pack — kept\n", id,
            name);
}

// Folds a declared presentation block onto a descriptor's glyph/radar.
// Presentation is cosmetic: a malformed value warns and keeps the current
// setting instead of sinking the whole pack.
void apply_presentation(const og::data::ClasspackPresentation& p,
                        og::FamilyGlyph& glyph, og::RadarBlip& radar,
                        const std::string& id)
{
    if (p.glyph) {
        char32_t codepoint = U' ';
        if (og::glyph_from_utf8(*p.glyph, codepoint))
            glyph.codepoint = codepoint;
        else
            LogWarn(
                "classpack {}: glyph '{}' is not one UTF-8 character — "
                "kept\n",
                id, *p.glyph);
    }
    if (p.glyph_ascii) {
        if (p.glyph_ascii->size() == 1)
            glyph.ascii = (*p.glyph_ascii)[0];
        else
            LogWarn("classpack {}: glyph_ascii '{}' is not one byte — kept\n",
                    id, *p.glyph_ascii);
    }
    if (p.glyph_color) {
        og::GlyphColor color{};
        if (og::glyph_color_from_name(*p.glyph_color, color))
            glyph.color = color;
        else
            LogWarn("classpack {}: unknown glyph_color '{}' — kept\n", id,
                    *p.glyph_color);
    }
    if (p.glyph_bold)
        glyph.bold = *p.glyph_bold;
    if (p.glyph_transparent)
        glyph.transparent = *p.glyph_transparent;
    if (p.radar_color)
        radar.color = *p.radar_color;
    if (p.radar_jitter) {
        if (*p.radar_jitter >= 0)
            radar.jitter = *p.radar_jitter;
        else
            LogWarn("classpack {}: negative radar_jitter {} — kept\n", id,
                    *p.radar_jitter);
    }
}

// --- per-order installers (entries must already live in the store) ------

bool install_living(const og::data::ClasspackLivingEntry& e, int id,
                    const PackAnims& anims, const PackWireIds& wire_ids)
{
    if (id < 0)
        return false;
    // The install slot: an occupied slot hands back its live descriptor, a
    // free one the order's defaults. nullptr means the id is past capacity.
    const FamilyDescriptor* current = get_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no living registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    // Copying the live descriptor preserves every behavior callback
    // pointer; only the data fields the YAML declares are overwritten.
    FamilyDescriptor d = *current;
    d.declared_id = claim_declared_id(e.id, d.declared_id, "living");
    if (e.name)
        d.name = e.name->c_str();
    if (e.short_name.present)
        d.short_name = nullable_cstr(e.short_name);
    if (e.base_stats) {
        const std::size_t n = std::min<std::size_t>(6, e.base_stats->size());
        for (std::size_t i = 0; i < n; i++)
            d.base_stats[i] = (*e.base_stats)[i];
    }
    if (e.hiring_cost)
        d.hiring_cost = *e.hiring_cost;
    if (e.derived_bonuses) {
        const std::size_t n =
            std::min<std::size_t>(8, e.derived_bonuses->size());
        for (std::size_t i = 0; i < n; i++)
            d.derived_bonuses[i] = (*e.derived_bonuses)[i];
    }
    if (e.stat_costs) {
        const std::size_t n = std::min<std::size_t>(6, e.stat_costs->size());
        for (std::size_t i = 0; i < n; i++)
            d.stat_costs[i] = (*e.stat_costs)[i];
    }
    if (e.special_costs) {
        const std::size_t n = std::min<std::size_t>(
            FD_NUM_SPECIALS, e.special_costs->size());
        for (std::size_t i = 0; i < n; i++)
            d.special_cost[i] =
                static_cast<unsigned short>((*e.special_costs)[i]);
    }
    if (e.weapon_cost)
        d.weapon_cost = static_cast<short>(*e.weapon_cost);
    if (e.default_weapon) {
        const int weapon = resolve_ref(Order::Weapon, *e.default_weapon,
                                       "default_weapon", e.id, wire_ids);
        if (weapon >= 0)
            d.default_weapon = weapon;
    }
    if (e.init_bit_flags) {
        std::int32_t flags = 0;
        if (fold_bit_flags(*e.init_bit_flags, flags, e.id))
            d.init_bit_flags = flags;
    }
    if (e.init_ani_type)
        d.init_ani_type = static_cast<char>(*e.init_ani_type);
    if (e.init_max_magicpoints)
        d.init_max_magicpoints = *e.init_max_magicpoints;
    if (e.special_names) {
        const std::size_t n = std::min<std::size_t>(
            FD_NUM_SPECIALS, e.special_names->size());
        for (std::size_t i = 0; i < n; i++)
            d.special_names[i] = (*e.special_names)[i].c_str();
    }
    if (e.alternate_names) {
        const std::size_t n = std::min<std::size_t>(
            FD_NUM_SPECIALS, e.alternate_names->size());
        for (std::size_t i = 0; i < n; i++)
            d.alternate_names[i] = (*e.alternate_names)[i].c_str();
    }
    if (e.leaves_bloodspot)
        d.leaves_bloodspot = *e.leaves_bloodspot;
    if (e.magic_damage_modifier)
        d.magic_damage_modifier = *e.magic_damage_modifier;
    if (e.is_stationary)
        d.is_stationary = *e.is_stationary;
    if (e.has_returning_weapon)
        d.has_returning_weapon = *e.has_returning_weapon;
    if (e.is_undead)
        d.is_undead = *e.is_undead;
    if (e.promotes_to.present) {
        if (e.promotes_to.is_null)
            d.promotes_to = -1;
        else {
            const int target =
                resolve_ref(Order::Living, e.promotes_to.value, "promotes_to",
                            e.id, wire_ids);
            if (target >= 0)
                d.promotes_to = target;
        }
    }
    if (e.promotion_level_req)
        d.promotion_level_req = *e.promotion_level_req;
    if (e.death_message.present)
        d.death_message = nullable_cstr(e.death_message);
    if (e.sprite.present)
        d.pix_filename = nullable_cstr(e.sprite);
    if (e.animation) {
        // Built-in names are reserved and win, so the seven legacy tables
        // keep working unchanged; anything else names a pack set.
        FamilyAnimationType type{};
        if (og::families::animation_type_from_name(*e.animation, type)) {
            d.animation_type = type;
            d.anim_table = nullptr;  // back to the built-in table
            d.anim_row_count = 0;
        } else if (const PackAnims::Entry* set = anims.find(*e.animation)) {
            d.anim_table = set->table;
            d.anim_row_count = set->rows;
        } else {
            LogWarn("classpack {}: unknown animation '{}' — kept\n", e.id,
                    *e.animation);
        }
    }
    if (e.ai_line_of_sight)
        d.ai_line_of_sight = *e.ai_line_of_sight;
    if (e.description.present)
        d.description = nullable_cstr(e.description);
    if (e.names)
        apply_name_pool(*e.names, d);
    if (e.playable)
        d.is_playable = *e.playable;
    if (e.playable_order)
        d.playable_order = *e.playable_order;
    apply_presentation(e.presentation, d.glyph, d.radar, e.id);

    if (!set_family_descriptor(id, d))
        return false;
    install_family_tuning(Order::Living, id, e.tuning);
    return true;
}

bool install_weapon(const og::data::ClasspackWeaponEntry& e, int id,
                    const PackAnims& anims)
{
    if (id < 0)
        return false;
    const WeaponFamilyDescriptor* current =
        get_weapon_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no weapon registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    WeaponFamilyDescriptor d = *current;
    d.declared_id = claim_declared_id(e.id, d.declared_id, "weapon");
    if (e.name)
        d.name = e.name->c_str();
    if (e.fire_sound)
        d.fire_sound = *e.fire_sound;
    if (e.skip_sit_notify)
        d.skip_sit_notify = *e.skip_sit_notify;
    if (e.is_auto_attackable)
        d.is_auto_attackable = *e.is_auto_attackable;
    if (e.init_bit_flags) {
        std::int32_t flags = 0;
        if (fold_bit_flags(*e.init_bit_flags, flags, e.id))
            d.init_bit_flags = flags;
    }
    if (e.init_lifetime)
        d.init_lifetime = static_cast<short>(*e.init_lifetime);
    if (e.init_ani_type)
        d.init_ani_type = static_cast<char>(*e.init_ani_type);
    if (e.vz)
        d.init_vz = *e.vz;
    if (e.gravity)
        d.gravity = *e.gravity;
    if (e.sizez)
        d.init_sizez = static_cast<short>(*e.sizez);
    if (e.can_drop_floors)
        d.can_drop_floors = *e.can_drop_floors;
    if (e.sprite.present)
        d.pix_filename = nullable_cstr(e.sprite);
    if (e.animation)
        apply_pack_animation(anims, *e.animation, e.id, d.anim_table,
                             d.anim_row_count);
    apply_presentation(e.presentation, d.glyph, d.radar, e.id);

    if (!set_weapon_family_descriptor(id, d))
        return false;
    install_family_tuning(Order::Weapon, id, e.tuning);
    return true;
}

bool install_effect(const og::data::ClasspackEffectEntry& e, int id,
                    const PackAnims& anims)
{
    if (id < 0)
        return false;
    const EffectFamilyDescriptor* current =
        get_effect_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no effect registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    EffectFamilyDescriptor d = *current;
    d.declared_id = claim_declared_id(e.id, d.declared_id, "effect");
    if (e.name)
        d.name = e.name->c_str();
    if (e.loops_animation)
        d.loops_animation = *e.loops_animation;
    if (e.creates_hit_effect)
        d.creates_hit_effect = *e.creates_hit_effect;
    if (e.init_bit_flags) {
        std::int32_t flags = 0;
        if (fold_bit_flags(*e.init_bit_flags, flags, e.id))
            d.init_bit_flags = flags;
    }
    if (e.sprite.present)
        d.pix_filename = nullable_cstr(e.sprite);
    if (e.animation)
        apply_pack_animation(anims, *e.animation, e.id, d.anim_table,
                             d.anim_row_count);
    apply_presentation(e.presentation, d.glyph, d.radar, e.id);

    if (!set_effect_family_descriptor(id, d))
        return false;
    install_family_tuning(Order::FX, id, e.tuning);
    return true;
}

bool install_treasure(const og::data::ClasspackTreasureEntry& e, int id,
                      const PackAnims& anims)
{
    if (id < 0)
        return false;
    const TreasureFamilyDescriptor* current =
        get_treasure_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no treasure registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    TreasureFamilyDescriptor d = *current;
    d.declared_id = claim_declared_id(e.id, d.declared_id, "treasure");
    if (e.name)
        d.name = e.name->c_str();
    if (e.init_ignore)
        d.init_ignore = *e.init_ignore;
    if (e.init_frame)
        d.init_frame = static_cast<short>(*e.init_frame);
    if (e.sprite.present)
        d.pix_filename = nullable_cstr(e.sprite);
    if (e.animation)
        apply_pack_animation(anims, *e.animation, e.id, d.anim_table,
                             d.anim_row_count);
    apply_presentation(e.presentation, d.glyph, d.radar, e.id);

    if (!set_treasure_family_descriptor(id, d))
        return false;
    install_family_tuning(Order::Treasure, id, e.tuning);
    return true;
}

bool install_generator(const og::data::ClasspackGeneratorEntry& e, int id,
                       const PackAnims& anims, const PackWireIds& wire_ids)
{
    if (id < 0)
        return false;
    const GeneratorFamilyDescriptor* current =
        get_generator_family_descriptor_install_slot(id);
    if (current == nullptr) {
        LogWarn("classpack {}: no generator registry slot {} (capacity)\n",
                e.id, id);
        return false;
    }

    GeneratorFamilyDescriptor d = *current;
    d.declared_id = claim_declared_id(e.id, d.declared_id, "generator");
    if (e.name)
        d.name = e.name->c_str();
    if (e.default_weapon) {
        // Generator default_weapon names the LIVING family produced.
        const int living = resolve_ref(Order::Living, *e.default_weapon,
                                       "default_weapon", e.id, wire_ids);
        if (living >= 0)
            d.default_weapon = living;
    }
    if (e.has_lifetime)
        d.has_lifetime = *e.has_lifetime;
    if (e.spawn_ani_type)
        d.spawn_ani_type = static_cast<char>(*e.spawn_ani_type);
    if (e.clear_owner)
        d.clear_owner = *e.clear_owner;
    if (e.sprite.present)
        d.pix_filename = nullable_cstr(e.sprite);
    if (e.animation)
        apply_pack_animation(anims, *e.animation, e.id, d.anim_table,
                             d.anim_row_count);
    if (e.editor_label)
        d.editor_label = e.editor_label->c_str();
    apply_presentation(e.presentation, d.glyph, d.radar, e.id);

    if (!set_generator_family_descriptor(id, d))
        return false;
    install_family_tuning(Order::Generator, id, e.tuning);
    return true;
}

// Installs every entry of a STORED pack. Wire ids are claimed first, for the
// whole pack, so a family may name any other family the pack declares no
// matter where in the file it sits (PackWireIds). Then the animation sets
// (families reference them by name), then the entries themselves.
int install_pack_families(const og::data::ClasspackData& pack,
                          AutoWireIds& autos)
{
    // Same sequence the installers used to take them in — entries in YAML
    // order, one counter per order — so auto ids land exactly where they did.
    PackWireIds wire_ids;
    for (const auto& e : pack.weapons)
        wire_ids.take(Order::Weapon, e.id, e.wire_id, autos);
    for (const auto& e : pack.effects)
        wire_ids.take(Order::FX, e.id, e.wire_id, autos);
    for (const auto& e : pack.treasures)
        wire_ids.take(Order::Treasure, e.id, e.wire_id, autos);
    for (const auto& e : pack.living)
        wire_ids.take(Order::Living, e.id, e.wire_id, autos);
    for (const auto& e : pack.generators)
        wire_ids.take(Order::Generator, e.id, e.wire_id, autos);

    const PackAnims anims = materialize_anims(pack);
    int installed = 0;
    std::size_t n = 0;
    for (const auto& e : pack.weapons)
        installed += install_weapon(
            e, wire_ids.ids[static_cast<int>(Order::Weapon)][n++], anims) ? 1 : 0;
    n = 0;
    for (const auto& e : pack.effects)
        installed += install_effect(
            e, wire_ids.ids[static_cast<int>(Order::FX)][n++], anims) ? 1 : 0;
    n = 0;
    for (const auto& e : pack.treasures)
        installed += install_treasure(
            e, wire_ids.ids[static_cast<int>(Order::Treasure)][n++], anims)
            ? 1 : 0;
    n = 0;
    for (const auto& e : pack.living)
        installed += install_living(
            e, wire_ids.ids[static_cast<int>(Order::Living)][n++], anims,
            wire_ids) ? 1 : 0;
    n = 0;
    for (const auto& e : pack.generators)
        installed += install_generator(
            e, wire_ids.ids[static_cast<int>(Order::Generator)][n++], anims,
            wire_ids) ? 1 : 0;
    return installed;
}

} // namespace

int install_classpacks()
{
    int packs_seen = 0;
    int installed = 0;
    AutoWireIds autos;
    // A full re-install: free the slots the previous pass claimed so the
    // registries end up describing exactly the packs mounted right now (an
    // unmounted pack must not leave a family behind, and auto ids must
    // restart from the same place on every peer). Core pins are untouched.
    // Tuning follows the same all-or-nothing rebuild: cleared here, then
    // reinstalled from every mounted pack's classpack data.
    reset_all_registry_mod_slots();
    og::script::clear_all_family_tuning();
    g_pack_install_stats.installs++;
    // Same deterministic pack order as script registration.
    for (const std::string& pack_id :
         og::io::physfs_enumerate_files_sorted("packs")) {
        // One document of a pack: packs/<id>/classpack.yaml, or one
        // packs/<id>/families/*.yaml. `name` is empty for the former, which
        // is also what selects its rejection message.
        struct Doc {
            std::string vpath;
            std::string name;
            std::string text;
        };
        std::vector<Doc> docs;
        // Exact identity of what was read, framed so no combination of
        // paths and contents can alias another: <vpath>\n<len>\n<bytes>.
        // Compared byte-for-byte rather than hashed — the whole point is
        // that a memo hit must mean the SAME text, and 60 KB of memcmp is
        // three orders of magnitude cheaper than the parse it skips.
        std::string signature;
        bool rejected = false;

        // READ phase. Always runs: mounts really can change these bytes
        // (a campaign .glad may carry its own packs/), and the read is what
        // discovers that. Only the parse below is memoized.
        const std::string vpath = "packs/" + pack_id + "/classpack.yaml";
        std::vector<std::uint8_t> bytes = read_file(vpath.c_str());
        if (!bytes.empty())
            docs.push_back({vpath, std::string(),
                            std::string(reinterpret_cast<const char*>(
                                            bytes.data()),
                                        bytes.size())});
        // Split layout: packs/<id>/families/*.yaml, each an ordinary
        // classpack document (usually one family entry), parsed into the
        // SAME ClasspackData AFTER classpack.yaml in sorted filename
        // order. parse_classpack_yaml appends per-order entries and
        // last-wins the header scalars, so the split set loads exactly
        // like the same text concatenated — and a pack may ship either
        // layout or both (classpack.yaml first, then families/; a later
        // entry pinning an already-declared WIRE slot overwrites just
        // the data fields it declares — ordinary sparse-entry semantics
        // — and replaces that slot's tuning map whole, since every
        // installed entry installs its tuning). Any unusable family
        // file rejects the whole pack, matching the classpack.yaml
        // contract above.
        const std::string families_dir = "packs/" + pack_id + "/families";
        for (const std::string& name :
             og::io::physfs_enumerate_files_sorted(families_dir)) {
            const std::string fpath = families_dir + "/" + name;
            if (!name.ends_with(".yaml") ||
                og::io::physfs_is_directory(fpath))
                continue; // non-YAML notes and subdirs are fine
            bytes = read_file(fpath.c_str());
            if (bytes.empty()) {
                LogWarn("class pack '{}' rejected: family file '{}' "
                        "unusable\n", pack_id, name);
                rejected = true;
                break;
            }
            docs.push_back({fpath, name,
                            std::string(reinterpret_cast<const char*>(
                                            bytes.data()),
                                        bytes.size())});
        }
        auto& memo = parsed_pack_memo();
        if (rejected || docs.empty()) {
            // No classpack data at all (scripts-only) is fine. Either way
            // this pack has no valid parse to remember.
            memo.erase(pack_id);
            continue;
        }
        for (const Doc& doc : docs) {
            signature += doc.vpath;
            signature += '\n';
            signature += std::to_string(doc.text.size());
            signature += '\n';
            signature += doc.text;
        }

        // PARSE phase, memoized on the exact bytes just read.
        const og::data::ClasspackData* stored = nullptr;
        const auto hit = memo.find(pack_id);
        if (hit != memo.end() && hit->second.signature == signature) {
            stored = hit->second.data;
            g_pack_install_stats.pack_parse_reuses++;
        } else {
            auto parsed = std::make_unique<og::data::ClasspackData>();
            for (const Doc& doc : docs) {
                if (og::data::parse_classpack_yaml(doc.text, *parsed,
                                                   doc.vpath.c_str()))
                    continue;
                if (doc.name.empty())
                    LogWarn("class pack '{}' rejected: classpack.yaml "
                            "unusable\n", pack_id);
                else
                    LogWarn("class pack '{}' rejected: family file '{}' "
                            "unusable\n", pack_id, doc.name);
                rejected = true;
                break;
            }
            if (rejected) {
                memo.erase(pack_id);
                continue;
            }
            // Move into the process-lifetime store BEFORE installing:
            // descriptors borrow the stored strings.
            stored = parsed.get();
            classpack_store().packs.push_back(std::move(parsed));
            memo[pack_id] = ParsedPack{std::move(signature), stored};
            g_pack_install_stats.pack_parses++;
        }
        // INSTALL phase. Unconditional, hit or miss: the registries were
        // just reset, and every entry is reinstalled in the same order with
        // the same wire-id counter.
        installed += install_pack_families(*stored, autos);
        packs_seen++;
    }
    if (packs_seen > 0)
        Log("Installed {} class pack(s): {} family descriptor(s)\n",
            packs_seen, installed);
    return installed;
}

int install_classpack_data(og::data::ClasspackData&& data)
{
    auto parsed =
        std::make_unique<og::data::ClasspackData>(std::move(data));
    og::data::ClasspackData& stored = *parsed;
    classpack_store().packs.push_back(std::move(parsed));
    AutoWireIds autos;
    return install_pack_families(stored, autos);
}

}  // namespace og::resources
