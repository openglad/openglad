// Company layer unit tests (docs/company-basecamp-design.md §3.2 / §3.4-3.6 /
// §3.10): the wall-clock seam that stamps GTL v14 `last_played_unix_s`, the
// active-company slot indirection, the slug derivation, the header-only
// company scan + startup selection, the §3.6 IO primitives, and the grep
// tripwire proving the timestamp can never reach the deterministic sim.

#include <openglad/resources/company.h>

#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

// RAII guard so a failing assertion can't leak a pinned clock into other
// tests (the cfg-clobber lesson: convention-only cleanup breaks under
// --gtest_shuffle).
struct ScopedCompanyClock
{
    explicit ScopedCompanyClock(std::int64_t fixed_now_s)
    {
        og::data::set_company_clock_for_tests(fixed_now_s);
    }
    ~ScopedCompanyClock()
    {
        og::data::set_company_clock_for_tests(std::nullopt);
    }
};

// Moves the whole save/ directory aside for the duration of a test so the
// company scan/list/startup tests see EXACTLY the fixtures they create —
// deterministic under --gtest_shuffle regardless of what sibling tests left
// in the shared per-process user dir. Restores everything on destruction.
class SaveDirSandbox
{
public:
    SaveDirSandbox()
        : save_dir_(std::filesystem::path(get_user_path()) / "save")
        , stash_dir_(std::filesystem::path(get_user_path()) /
                     "save_sandbox_stash")
    {
        // The og_unit_data binary contains a deliberate resilience landmine:
        // PhysfsWrappers.og_file_physfs_and_stdio_constructor_paths simulates
        // a fatal assert and leaves the PhysFS write dir redirected and the
        // user-dir mount destroyed ON PURPOSE. Tests that write through
        // PhysFS (SaveData::save / atomic_company_save / list_files) must
        // re-assert the canonical unit filesystem to stay order-independent
        // under --gtest_shuffle.
        if (og::resources::is_initialized())
        {
            const std::string user_path = get_user_path();
            (void)og::resources::set_write_dir(user_path);
            (void)og::resources::mount(user_path.c_str(), nullptr, 1);
        }

        std::error_code ec;
        std::filesystem::remove_all(stash_dir_, ec);
        if (std::filesystem::exists(save_dir_, ec))
            std::filesystem::rename(save_dir_, stash_dir_, ec);
        std::filesystem::create_directories(save_dir_, ec);
    }

    ~SaveDirSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(save_dir_, ec);
        if (std::filesystem::exists(stash_dir_, ec))
            std::filesystem::rename(stash_dir_, save_dir_, ec);
        else
            std::filesystem::create_directories(save_dir_, ec);
    }

    const std::filesystem::path& dir() const { return save_dir_; }

    void write_raw(const std::string& filename, const std::string& bytes) const
    {
        std::ofstream out(save_dir_ / filename,
                          std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good()) << "failed to open " << filename;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(out.good()) << "failed to write " << filename;
    }

private:
    std::filesystem::path save_dir_;
    std::filesystem::path stash_dir_;
};

void append_bytes(std::string& out, const void* data, std::size_t size)
{
    out.append(static_cast<const char*>(data), size);
}

template <typename T>
void append_value(std::string& out, T value)
{
    append_bytes(out, &value, sizeof(value));
}

// Builds a raw GTL header for any format version the reader supports,
// following the version-gated ladder exactly (§3.5 [SAVE-R1]). The scan never
// reads past the header, so the fixture stops at the version's header end.
struct HeaderFixture
{
    std::uint8_t version = 14;
    std::string name = "FIXTURE COMPANY";
    std::string campaign = "org.openglad.gladiator"; // v8+
    std::int16_t scen_num = 3;
    std::uint32_t cash = 1234;
    std::int16_t listsize = 2;
    std::uint8_t numplayers = 1;
    std::int64_t last_played = 0; // v14+

    std::string bytes() const
    {
        std::string out;
        out += "GTL";
        append_value(out, version);
        if (version >= 7)
            append_value<std::int16_t>(out, 1); // registered mark
        if (version >= 2)
        {
            std::string padded = name;
            padded.resize(40, '\0');
            out += padded;
        }
        if (version >= 8)
        {
            std::string padded = campaign;
            padded.resize(40, '\0');
            out += padded;
        }
        append_value(out, scen_num);
        append_value(out, cash);
        append_value<std::uint32_t>(out, 777); // score (skipped by the scan)
        if (version >= 6)
        {
            for (int team = 0; team < 4; ++team)
            {
                append_value<std::uint32_t>(out, 5000);
                append_value<std::uint32_t>(out, 0);
            }
        }
        if (version >= 7)
            append_value<std::int16_t>(out, 1); // allied mode
        append_value(out, listsize);
        append_value(out, numplayers);
        if (version >= 14)
        {
            append_value(out, last_played);
            out.append(23, '\0'); // reserved, zero-filled by v14 writers
        }
        else
        {
            // v13 and older carry 'GTL' filler in the reserved block — the
            // scan must never sniff a timestamp out of it.
            for (int filler_index = 0; filler_index < 31; ++filler_index)
                out.push_back("GTL"[filler_index % 3]);
        }
        return out;
    }
};

} // namespace

TEST(CompanyClock, override_pins_the_clock_and_reset_restores_wall_time)
{
    {
        ScopedCompanyClock pin(1234567890);
        ASSERT_EQ(1234567890, og::data::company_clock_now_s())
            << "pinned clock should return the fixed value";

        og::data::set_company_clock_for_tests(42);
        ASSERT_EQ(42, og::data::company_clock_now_s())
            << "re-pinning should take effect immediately";
    }

    // Guard destroyed -> real wall clock. Any plausible current time is far
    // past 2020-01-01 (1577836800) and the seam must be monotone enough to
    // never return the stale pinned values.
    const std::int64_t now_s = og::data::company_clock_now_s();
    ASSERT_GT(now_s, 1577836800) << "unpinned clock should read real wall time";
}

TEST(CompanyClock, zero_is_a_valid_pinned_value)
{
    // 0 is the v13-payload default for last_played_unix_s; the seam must be
    // able to produce it (optional-engaged, not "falsy = unpinned").
    ScopedCompanyClock pin(0);
    ASSERT_EQ(0, og::data::company_clock_now_s())
        << "a pinned value of 0 must not fall through to wall time";
}

// --- Active-company slot (§3.4) ------------------------------------------

TEST(CompanySlot, defaults_to_save0_and_setter_repoints)
{
    ASSERT_EQ("save0", og::data::active_company_slot())
        << "the default slot must be save0 (WP2 invisibility contract)";
    ASSERT_TRUE(og::data::set_active_company_slot("companyx"));
    ASSERT_EQ("companyx", og::data::active_company_slot());
    ASSERT_TRUE(og::data::set_active_company_slot("save0"));
}

TEST(CompanySlot, rejects_netsession_and_unsafe_names)
{
    ASSERT_EQ("save0", og::data::active_company_slot());
    EXPECT_FALSE(og::data::set_active_company_slot("netsession"))
        << "the server-economy scratch slot can never become the company";
    EXPECT_FALSE(og::data::set_active_company_slot("bad name"));
    EXPECT_FALSE(og::data::set_active_company_slot("../escape"));
    EXPECT_FALSE(og::data::set_active_company_slot(""));
    EXPECT_FALSE(og::data::set_active_company_slot(std::string(65, 'a')));
    EXPECT_EQ("save0", og::data::active_company_slot())
        << "a rejected slot must leave the active company unchanged";
}

TEST(CompanySlot, scoped_guard_restores_previous_slot)
{
    ASSERT_EQ("save0", og::data::active_company_slot());
    {
        og::data::ScopedActiveCompany guard("companyy");
        EXPECT_TRUE(guard.applied());
        EXPECT_EQ("companyy", og::data::active_company_slot());
        {
            og::data::ScopedActiveCompany inner("netsession");
            EXPECT_FALSE(inner.applied());
            EXPECT_EQ("companyy", og::data::active_company_slot())
                << "an invalid scoped slot must not change the active company";
        }
        EXPECT_EQ("companyy", og::data::active_company_slot());
    }
    EXPECT_EQ("save0", og::data::active_company_slot());
}

// [SAVE-R8] Fixture-reset pair: each of these asserts the default at entry
// and then deliberately dirties the slot. Under ANY --gtest_shuffle order,
// both can only pass if the unit_main listener restores "save0" between
// tests — this pins the structural reset, not test convention.
TEST(CompanySlot, main_fixture_resets_slot_between_tests_a)
{
    ASSERT_EQ("save0", og::data::active_company_slot())
        << "unit_main's [SAVE-R8] listener failed to restore the default slot";
    ASSERT_TRUE(og::data::set_active_company_slot("stray-fixture-a"));
}

TEST(CompanySlot, main_fixture_resets_slot_between_tests_b)
{
    ASSERT_EQ("save0", og::data::active_company_slot())
        << "unit_main's [SAVE-R8] listener failed to restore the default slot";
    ASSERT_TRUE(og::data::set_active_company_slot("stray-fixture-b"));
}

// --- Slug derivation (§3.4) ----------------------------------------------

TEST(CompanySlug, derivation_table)
{
    SaveDirSandbox sandbox; // collision probes must see an empty save dir
    EXPECT_EQ("the-iron-company",
              og::data::derive_company_slot("The Iron Company"));
    EXPECT_EQ("a-b", og::data::derive_company_slot("A    B"));
    EXPECT_EQ("weird-name", og::data::derive_company_slot("--Weird__Name--"));
    EXPECT_EQ("sirbadgeralot",
              og::data::derive_company_slot("Sir'Badger,a.lot!"));
    EXPECT_EQ("company", og::data::derive_company_slot("!!!"))
        << "a name with no usable characters falls back to 'company'";
    EXPECT_EQ("company", og::data::derive_company_slot(""));
    EXPECT_EQ("x9", og::data::derive_company_slot("X9"));
}

TEST(CompanySlug, truncates_to_40_without_trailing_dash)
{
    SaveDirSandbox sandbox;
    const std::string long_name(60, 'a');
    const std::string slug = og::data::derive_company_slot(long_name);
    EXPECT_EQ(std::string(40, 'a'), slug);

    // 39 chars + separator right at the cut: the trailing '-' is re-trimmed.
    const std::string edge = std::string(39, 'b') + " " + std::string(10, 'c');
    EXPECT_EQ(std::string(39, 'b'), og::data::derive_company_slot(edge));
}

TEST(CompanySlug, netsession_reserved_and_collisions_suffix)
{
    SaveDirSandbox sandbox;
    EXPECT_EQ("netsession-2", og::data::derive_company_slot("NetSession"))
        << "the reserved slot name must divert to the first suffix";

    sandbox.write_raw("alpha.gtl", "GTL");
    EXPECT_EQ("alpha-2", og::data::derive_company_slot("Alpha"));
    sandbox.write_raw("alpha-2.gtl", "GTL");
    EXPECT_EQ("alpha-3", og::data::derive_company_slot("Alpha"));
}

TEST(CompanySlug, exhausted_suffixes_fall_back_to_epoch)
{
    SaveDirSandbox sandbox;
    sandbox.write_raw("beta.gtl", "GTL");
    for (int suffix = 2; suffix <= 99; ++suffix)
        sandbox.write_raw("beta-" + std::to_string(suffix) + ".gtl", "GTL");

    ScopedCompanyClock pin(987654);
    EXPECT_EQ("beta-987654", og::data::derive_company_slot("Beta"))
        << "the epoch fallback must come from the pinnable company clock";
}

// --- Header-only scan (§3.5) ---------------------------------------------

TEST(CompanyScan, missing_file_is_nullopt_and_unsafe_slot_rejected)
{
    SaveDirSandbox sandbox;
    EXPECT_FALSE(og::data::read_company_header("no-such-company").has_value());
    EXPECT_FALSE(og::data::read_company_header("../escape").has_value());
    EXPECT_FALSE(og::data::read_company_header("netsession").has_value())
        << "netsession.gtl does not exist in the sandbox; missing => nullopt";
}

TEST(CompanyScan, bad_magic_and_truncated_are_invalid_not_missing)
{
    SaveDirSandbox sandbox;
    sandbox.write_raw("corrupt.gtl", "NOT A GTL FILE");
    const auto corrupt = og::data::read_company_header("corrupt");
    ASSERT_TRUE(corrupt.has_value());
    EXPECT_FALSE(corrupt->valid);
    EXPECT_EQ("corrupt", corrupt->slot);
    EXPECT_EQ(0, corrupt->last_played_unix_s);

    // A v14 header cut mid-name: magic+version parse, then a short read.
    HeaderFixture fixture;
    const std::string full = fixture.bytes();
    sandbox.write_raw("truncated.gtl", full.substr(0, 20));
    const auto truncated = og::data::read_company_header("truncated");
    ASSERT_TRUE(truncated.has_value());
    EXPECT_FALSE(truncated->valid);
    EXPECT_EQ(14, truncated->version);

    // Version 0 is UnsupportedVersion in the reader.
    sandbox.write_raw("version0.gtl",
                      std::string("GTL") + std::string(1, '\0'));
    const auto version0 = og::data::read_company_header("version0");
    ASSERT_TRUE(version0.has_value());
    EXPECT_FALSE(version0->valid);
}

TEST(CompanyScan, v14_header_reads_all_fields_at_fixed_offsets)
{
    SaveDirSandbox sandbox;
    HeaderFixture fixture;
    fixture.name = "Ledger And Kettle";
    fixture.campaign = "com.example.westlands";
    fixture.scen_num = 7;
    fixture.cash = 4321;
    fixture.listsize = 5;
    fixture.last_played = 1700000000;
    const std::string bytes = fixture.bytes();
    ASSERT_EQ(164u, bytes.size())
        << "the v14 header must occupy exactly 164 bytes (§3.5)";
    // Spot-check the fixed offsets the design pins: timestamp at 133.
    std::int64_t raw_timestamp = 0;
    std::memcpy(&raw_timestamp, bytes.data() + 133, 8);
    ASSERT_EQ(1700000000, raw_timestamp);

    sandbox.write_raw("modern.gtl", bytes);
    const auto info = og::data::read_company_header("modern");
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->valid);
    EXPECT_EQ("modern", info->slot);
    EXPECT_EQ("Ledger And Kettle", info->display_name);
    EXPECT_EQ("com.example.westlands", info->campaign_id);
    EXPECT_EQ(7, info->scen_num);
    EXPECT_EQ(4321u, info->totalcash);
    EXPECT_EQ(5, info->roster_size);
    EXPECT_EQ(14, info->version);
    EXPECT_EQ(1700000000, info->last_played_unix_s);
}

TEST(CompanyScan, version_ladder_v2_v5_v6_v7_v13)
{
    SaveDirSandbox sandbox;
    // [SAVE-R1] For v < 8 the fixed offsets do not hold; the scan must walk
    // the gated ladder. Every fixture uses distinctive values so a misaligned
    // read cannot accidentally pass.
    const struct
    {
        std::uint8_t version;
        const char* slot;
    } cases[] = {
        {2, "ladder-v2"},
        {5, "ladder-v5"},
        {6, "ladder-v6"},
        {7, "ladder-v7"},
        {13, "ladder-v13"},
    };

    for (const auto& ladder_case : cases)
    {
        HeaderFixture fixture;
        fixture.version = ladder_case.version;
        fixture.name = std::string("NAME V") +
                       std::to_string(ladder_case.version);
        fixture.scen_num = static_cast<std::int16_t>(ladder_case.version + 10);
        fixture.cash = 1000u + ladder_case.version;
        fixture.listsize = 3;
        sandbox.write_raw(std::string(ladder_case.slot) + ".gtl",
                          fixture.bytes());

        const auto info = og::data::read_company_header(ladder_case.slot);
        ASSERT_TRUE(info.has_value()) << "v" << int(ladder_case.version);
        EXPECT_TRUE(info->valid) << "v" << int(ladder_case.version);
        EXPECT_EQ(fixture.name, info->display_name)
            << "v" << int(ladder_case.version);
        EXPECT_EQ("org.openglad.gladiator", info->campaign_id)
            << "pre-v8 files carry no campaign; default applies";
        EXPECT_EQ(fixture.scen_num, info->scen_num)
            << "v" << int(ladder_case.version);
        EXPECT_EQ(fixture.cash, info->totalcash)
            << "v" << int(ladder_case.version);
        EXPECT_EQ(3, info->roster_size) << "v" << int(ladder_case.version);
        EXPECT_EQ(ladder_case.version, info->version);
        EXPECT_EQ(0, info->last_played_unix_s)
            << "pre-v14 reserved filler must never be sniffed as a timestamp";
    }
}

TEST(CompanyScan, invalid_listsize_numplayers_and_unsafe_campaign)
{
    SaveDirSandbox sandbox;

    HeaderFixture oversized;
    oversized.listsize = static_cast<std::int16_t>(MAX_TEAM_SIZE + 1);
    sandbox.write_raw("oversized.gtl", oversized.bytes());
    const auto oversized_info = og::data::read_company_header("oversized");
    ASSERT_TRUE(oversized_info.has_value());
    EXPECT_FALSE(oversized_info->valid)
        << "the reader rejects listsize > MAX_TEAM_SIZE; the scan must agree";

    HeaderFixture crowded;
    crowded.numplayers = 5;
    sandbox.write_raw("crowded.gtl", crowded.bytes());
    const auto crowded_info = og::data::read_company_header("crowded");
    ASSERT_TRUE(crowded_info.has_value());
    EXPECT_FALSE(crowded_info->valid)
        << "the reader rejects numplayers > 4; the scan must agree";

    HeaderFixture hostile;
    hostile.campaign = "../../etc/passwd";
    sandbox.write_raw("hostile.gtl", hostile.bytes());
    const auto hostile_info = og::data::read_company_header("hostile");
    ASSERT_TRUE(hostile_info.has_value());
    EXPECT_TRUE(hostile_info->valid);
    EXPECT_EQ("org.openglad.gladiator", hostile_info->campaign_id)
        << "an unsafe campaign id falls back to the default (reader parity)";
}

TEST(CompanyScan, scan_does_not_mount_and_roundtrips_real_saves)
{
    SaveDirSandbox sandbox;
    // A REAL v14 file produced by the writer must scan to the same identity
    // the full loader would produce.
    SaveData save;
    save.save_name = "Roundtrip Company";
    save.scen_num = 4;
    save.totalcash = 999;
    save.last_played_unix_s = 424242;
    ASSERT_TRUE(save.save("scanround"));

    const std::string mounted_before = get_mounted_campaign();
    const auto info = og::data::read_company_header("scanround");
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->valid);
    EXPECT_EQ("Roundtrip Company", info->display_name);
    EXPECT_EQ(4, info->scen_num);
    EXPECT_EQ(999u, info->totalcash);
    EXPECT_EQ(14, info->version);
    EXPECT_EQ(424242, info->last_played_unix_s);
    EXPECT_EQ(mounted_before, get_mounted_campaign())
        << "the header scan must never mount the save's campaign (§3.5)";
}

// --- Listing + startup selection (§3.5) ----------------------------------

namespace {

std::string timestamped_header(const std::string& name,
                               std::int64_t last_played)
{
    HeaderFixture fixture;
    fixture.name = name;
    fixture.last_played = last_played;
    return fixture.bytes();
}

} // namespace

TEST(CompanyList, orders_by_timestamp_then_save0_then_slot)
{
    SaveDirSandbox sandbox;
    sandbox.write_raw("save0.gtl", timestamped_header("DEFAULT", 100));
    sandbox.write_raw("companya.gtl", timestamped_header("A", 300));
    sandbox.write_raw("companyb.gtl", timestamped_header("B", 200));
    sandbox.write_raw("zebra.gtl", timestamped_header("Z", 200));

    const std::vector<og::data::CompanyInfo> companies =
        og::data::list_companies();
    ASSERT_EQ(4u, companies.size());
    EXPECT_EQ("companya", companies[0].slot);
    EXPECT_EQ("companyb", companies[1].slot)
        << "equal timestamps break ties by slot ascending";
    EXPECT_EQ("zebra", companies[2].slot);
    EXPECT_EQ("save0", companies[3].slot);
}

TEST(CompanyList, save0_wins_timestamp_ties)
{
    SaveDirSandbox sandbox;
    sandbox.write_raw("save0.gtl", timestamped_header("DEFAULT", 300));
    sandbox.write_raw("aardvark.gtl", timestamped_header("A", 300));

    const std::vector<og::data::CompanyInfo> companies =
        og::data::list_companies();
    ASSERT_EQ(2u, companies.size());
    EXPECT_EQ("save0", companies[0].slot)
        << "save0 outranks other slots at equal timestamps";
    EXPECT_EQ("aardvark", companies[1].slot);
}

TEST(CompanyList, excludes_netsession_tmp_and_non_gtl)
{
    SaveDirSandbox sandbox;
    sandbox.write_raw("keeper.gtl", timestamped_header("KEEP", 10));
    sandbox.write_raw("netsession.gtl", timestamped_header("NET", 9999));
    sandbox.write_raw("keeper.tmp.gtl", timestamped_header("STAGING", 8888));
    sandbox.write_raw("notes.txt", "not a save");

    const std::vector<og::data::CompanyInfo> companies =
        og::data::list_companies();
    ASSERT_EQ(1u, companies.size())
        << "netsession, *.tmp.gtl staging files and non-GTL files are not "
           "companies";
    EXPECT_EQ("keeper", companies[0].slot);
}

TEST(CompanyStartup, empty_save_dir_selects_nothing)
{
    SaveDirSandbox sandbox;
    EXPECT_EQ("", og::data::select_startup_company());
}

TEST(CompanyStartup, v13_only_save0_is_selected)
{
    SaveDirSandbox sandbox;
    HeaderFixture legacy;
    legacy.version = 13;
    legacy.name = "UPGRADER";
    sandbox.write_raw("save0.gtl", legacy.bytes());
    EXPECT_EQ("save0", og::data::select_startup_company())
        << "an upgrader's v13 save0 must appear as the startup company";
}

// [SAVE-R5] With save0 absent but other slots present, the newest stray is
// the startup company — the intended behavior change tests must own.
TEST(CompanyStartup, no_save0_with_strays_selects_newest_stray)
{
    SaveDirSandbox sandbox;
    sandbox.write_raw("straya.gtl", timestamped_header("OLD", 5));
    sandbox.write_raw("strayb.gtl", timestamped_header("NEW", 9));
    EXPECT_EQ("strayb", og::data::select_startup_company());
}

// [SAVE-R6] Never-silently-switch: the API surfaces the corrupt most-recent
// entry instead of skipping to a healthy older company.
TEST(CompanyStartup, corrupt_default_company_is_still_selected)
{
    SaveDirSandbox sandbox;
    sandbox.write_raw("save0.gtl", "GTLgarbage-that-is-not-a-header");
    sandbox.write_raw("healthy.gtl", timestamped_header("HEALTHY", 0));

    const std::string startup = og::data::select_startup_company();
    EXPECT_EQ("save0", startup)
        << "a corrupt save0 ties at timestamp 0 and still wins the save0-first "
           "tie-break; only explicit user action may switch companies";
    const auto info = og::data::read_company_header(startup);
    ASSERT_TRUE(info.has_value());
    EXPECT_FALSE(info->valid)
        << "callers detect the damage via valid=false and surface it";
}

// --- IO primitives + atomic company write (§3.6) -------------------------

TEST(CompanyIoPrimitives, exists_copy_and_remove_round_trip)
{
    SaveDirSandbox sandbox;
    ASSERT_FALSE(user_file_exists("save/prim.gtl"));
    sandbox.write_raw("prim.gtl", "PRIMITIVE BYTES");
    ASSERT_TRUE(user_file_exists("save/prim.gtl"));

    ASSERT_TRUE(copy_user_file("save/prim.gtl", "save/prim-copy.gtl"));
    ASSERT_TRUE(user_file_exists("save/prim-copy.gtl"));
    EXPECT_FALSE(user_file_exists("save/prim-copy.gtl.tmp"))
        << "the copy staging file must be renamed away";
    {
        std::ifstream in(sandbox.dir() / "prim-copy.gtl", std::ios::binary);
        std::stringstream contents;
        contents << in.rdbuf();
        EXPECT_EQ("PRIMITIVE BYTES", contents.str());
    }

    EXPECT_FALSE(copy_user_file("save/no-such-src.gtl", "save/nope.gtl"))
        << "a missing source aborts before creating anything";
    EXPECT_FALSE(user_file_exists("save/nope.gtl"));

    EXPECT_TRUE(remove_user_file("save/prim-copy.gtl"));
    EXPECT_FALSE(user_file_exists("save/prim-copy.gtl"));
    EXPECT_FALSE(remove_user_file("save/prim-copy.gtl"))
        << "removing a missing file reports false";
}

TEST(CompanyAtomicSave, writes_via_tmp_and_leaves_no_staging_file)
{
    SaveDirSandbox sandbox;
    SaveData save;
    save.save_name = "Atomic Company";
    save.scen_num = 2;
    save.last_played_unix_s = 555;

    ASSERT_EQ(SaveDataIoError::None,
              og::data::atomic_company_save(save, "atomic"));
    EXPECT_TRUE(user_file_exists("save/atomic.gtl"));
    EXPECT_FALSE(user_file_exists("save/atomic.tmp.gtl"))
        << "the staging file must be renamed over the real slot";

    const auto info = og::data::read_company_header("atomic");
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->valid);
    EXPECT_EQ("Atomic Company", info->display_name);
    EXPECT_EQ(555, info->last_played_unix_s);

    // The staging name is excluded from listings even if a torn write ever
    // leaves one behind.
    sandbox.write_raw("atomic.tmp.gtl", timestamped_header("TORN", 99999));
    const std::vector<og::data::CompanyInfo> companies =
        og::data::list_companies();
    ASSERT_EQ(1u, companies.size());
    EXPECT_EQ("atomic", companies[0].slot);
}

TEST(CompanyAtomicSave, rejects_netsession_and_unsafe_slots)
{
    SaveDirSandbox sandbox;
    SaveData save;
    EXPECT_EQ(SaveDataIoError::OpenWriteFailed,
              og::data::atomic_company_save(save, "netsession"));
    EXPECT_EQ(SaveDataIoError::OpenWriteFailed,
              og::data::atomic_company_save(save, "bad name"));
    EXPECT_FALSE(user_file_exists("save/netsession.gtl"));
    EXPECT_FALSE(user_file_exists("save/netsession.tmp.gtl"));
}

// [§3.10 grep tripwire] The gameplay component must never reference the
// company layer: no gameplay implementation file may mention the
// last-played timestamp or include resources/company.h. This is what keeps
// the wall clock structurally unreachable from the deterministic sim (the
// parity harness does zero save IO; og_gameplay cannot link og_resources'
// company seam). Runs from the repo root (ctest WORKING_DIRECTORY).
TEST(CompanyClock, gameplay_sources_never_reference_company_or_last_played)
{
    namespace fs = std::filesystem;
    const std::vector<fs::path> roots = {
        fs::path("src") / "gameplay",
        fs::path("include") / "openglad" / "gameplay",
    };

    const std::vector<std::string> forbidden = {
        "last_played",
        "resources/company.h",
        "company_clock",
    };

    int scanned = 0;
    for (const auto& root : roots)
    {
        ASSERT_TRUE(fs::exists(root))
            << "tripwire must run from the repo root; missing " << root;
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
                continue;
            const auto ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".hpp")
                continue;
            ++scanned;

            std::ifstream in(entry.path());
            ASSERT_TRUE(in.good()) << "unreadable " << entry.path();
            std::stringstream buffer;
            buffer << in.rdbuf();
            const std::string contents = buffer.str();

            for (const auto& token : forbidden)
            {
                EXPECT_EQ(std::string::npos, contents.find(token))
                    << entry.path()
                    << " references forbidden company-layer token '" << token
                    << "' — the wall-clock/timestamp seam must never reach "
                       "the deterministic sim (design §3.2)";
            }
        }
    }
    ASSERT_GT(scanned, 50) << "tripwire scanned suspiciously few files";
}
