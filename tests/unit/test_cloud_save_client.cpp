// Cloud-save pure client tests (issue #155): key derivation pins, the hex
// codec, URL/JSON builders + parsers, and the two hook-driven flows against
// fake CloudHooks. Headless: no SDL, no network — the flows exercise the
// real og::data byte IO inside the unit sandbox user dir (unit_main points
// OPENGLAD_CONFIG_DIR at a per-process temp dir).

#include <openglad/interface/ui/cloud_save_client.h>

#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace og::ui::cloud;

namespace {

// --- fixtures ---------------------------------------------------------------

// Minimal valid GTL v14 header (the [SAVE-R1] ladder), enough for
// read_company_header/install validation. Mirrors test_company.cpp's
// HeaderFixture at the fields the cloud flows read.
std::string valid_gtl_bytes(const std::string& name = "FIXTURE COMPANY",
                            std::int16_t scen_num = 3,
                            std::int64_t last_played = 0,
                            const std::string& campaign =
                                "org.openglad.gladiator")
{
    std::string out;
    const auto append = [&out](const void* data, std::size_t size) {
        out.append(static_cast<const char*>(data), size);
    };
    out += "GTL";
    const std::uint8_t version = 14;
    append(&version, 1);
    const std::int16_t registered = 1;
    append(&registered, 2);
    std::string padded_name = name;
    padded_name.resize(40, '\0');
    out += padded_name;
    std::string padded_campaign = campaign;
    padded_campaign.resize(40, '\0');
    out += padded_campaign;
    append(&scen_num, 2);
    const std::uint32_t cash = 1234;
    const std::uint32_t score = 777;
    append(&cash, 4);
    append(&score, 4);
    for (int team = 0; team < 4; ++team)
    {
        const std::uint32_t team_cash = 5000;
        const std::uint32_t team_score = 0;
        append(&team_cash, 4);
        append(&team_score, 4);
    }
    const std::int16_t allied = 1;
    append(&allied, 2);
    const std::int16_t listsize = 2;
    append(&listsize, 2);
    const std::uint8_t legacy_numplayers = 1;
    append(&legacy_numplayers, 1);
    append(&last_played, 8);
    out.append(23, '\0');
    return out;
}

void write_save_file(const std::string& slot, const std::string& bytes)
{
    const std::filesystem::path path =
        std::filesystem::path(get_user_path()) / "save" / (slot + ".gtl");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(out.good());
}

void remove_save_file(const std::string& slot)
{
    std::error_code ec;
    std::filesystem::remove(
        std::filesystem::path(get_user_path()) / "save" / (slot + ".gtl"),
        ec);
}

std::optional<std::string> read_save_file(const std::string& slot)
{
    std::ifstream in(
        std::filesystem::path(get_user_path()) / "save" / (slot + ".gtl"),
        std::ios::binary);
    if (!in.good())
        return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// Resets the persisted cloud cfg keys and the active slot around each flow
// test (the [SAVE-R8] discipline; unit_main also resets the slot).
struct CloudCfgGuard
{
    CloudCfgGuard() { cfg.data.erase("cloud"); }
    ~CloudCfgGuard()
    {
        cfg.data.erase("cloud");
        (void)og::data::set_active_company_slot("save0");
    }
};

// Recording fake hooks: canned HTTP results, scripted confirm verdicts.
struct FakeHooks
{
    std::vector<CloudHttpResult> post_results;
    CloudHttpResult get_result;
    bool confirm_verdict = false;

    std::vector<std::string> post_urls;
    std::vector<std::string> post_bodies;
    std::vector<std::string> get_urls;
    std::vector<std::string> confirms; // "title|message"
    std::vector<std::string> notifies; // "title|message"

    CloudHooks hooks()
    {
        CloudHooks result;
        result.http_get = [this](const std::string& url) {
            get_urls.push_back(url);
            return get_result;
        };
        result.http_post = [this](const std::string& url,
                                  const std::string& body) {
            post_urls.push_back(url);
            post_bodies.push_back(body);
            CloudHttpResult reply;
            if (!post_results.empty())
            {
                reply = post_results.front();
                post_results.erase(post_results.begin());
            }
            return reply;
        };
        result.confirm = [this](const std::string& title,
                                const std::string& message) {
            confirms.push_back(title + "|" + message);
            return confirm_verdict;
        };
        result.notify = [this](const std::string& title,
                               const std::string& message) {
            notifies.push_back(title + "|" + message);
        };
        return result;
    }
};

CloudHttpResult http(int status, std::string body)
{
    CloudHttpResult result;
    result.status = status;
    result.body = std::move(body);
    return result;
}

} // namespace

// --- passphrase normalization + key derivation -------------------------------

TEST(CloudSaveKey, normalize_trims_collapses_and_lowercases)
{
    EXPECT_EQ("correct horse battery",
              normalize_cloud_passphrase("  Correct   HORSE\tbattery  "));
    EXPECT_EQ("abc def", normalize_cloud_passphrase("abc\n\r def"));
    EXPECT_EQ("", normalize_cloud_passphrase("   \t\n  "));
    // Non-ASCII bytes pass through untouched (no locale-dependent folding).
    EXPECT_EQ("caf\xc3\xa9 latte",
              normalize_cloud_passphrase("  Caf\xc3\xa9   LATTE "));
}

TEST(CloudSaveKey, derive_pins_the_documented_vectors)
{
    // Compute-verified pins (design D2): fnv1a64 of
    // "og-cloud-save:" + normalized passphrase.
    EXPECT_EQ("73270125791ba273",
              derive_cloud_save_key("correct horse battery"));
    EXPECT_EQ("9a9bba29704ed608", derive_cloud_save_key("gladiator"));
    EXPECT_EQ("a7a7a42f5ae27426", derive_cloud_save_key("my save 123"));
    // Normalization feeds the hash: case/whitespace variants collide by
    // design (typo tolerance).
    EXPECT_EQ(derive_cloud_save_key("correct horse battery"),
              derive_cloud_save_key("  CORRECT   Horse battery "));
}

TEST(CloudSaveKey, derive_enforces_the_length_gate_after_normalization)
{
    EXPECT_EQ("", derive_cloud_save_key("short12"));         // 7 -> reject
    EXPECT_NE("", derive_cloud_save_key("short123"));        // 8 -> accept
    EXPECT_NE("", derive_cloud_save_key(std::string(64, 'a')));
    EXPECT_EQ("", derive_cloud_save_key(std::string(65, 'a'))); // 65 -> reject
    // 8 raw chars that normalize to 7 are rejected.
    EXPECT_EQ("", derive_cloud_save_key(" short12"));
    EXPECT_EQ("", derive_cloud_save_key(""));
}

// --- hex codec ---------------------------------------------------------------

TEST(CloudSaveHex, roundtrip_and_rejections)
{
    const std::vector<std::uint8_t> bytes = {0x47, 0x54, 0x4c, 0x0e, 0x00,
                                             0xff, 0x10, 0xab};
    const std::string hex = hex_encode(bytes);
    EXPECT_EQ("47544c0e00ff10ab", hex);

    std::vector<std::uint8_t> decoded;
    ASSERT_TRUE(hex_decode(hex, decoded));
    EXPECT_EQ(bytes, decoded);

    ASSERT_TRUE(hex_decode("", decoded));
    EXPECT_TRUE(decoded.empty());
    EXPECT_EQ("", hex_encode({}));

    EXPECT_FALSE(hex_decode("abc", decoded)) << "odd length";
    EXPECT_FALSE(hex_decode("0xab", decoded)) << "0x prefix";
    EXPECT_FALSE(hex_decode("ABCD", decoded)) << "uppercase";
    EXPECT_FALSE(hex_decode("ab cd", decoded)) << "spaces";
    EXPECT_FALSE(hex_decode("zz", decoded)) << "non-hex";
    EXPECT_TRUE(decoded.empty()) << "rejects clear the output";
}

// --- URL builder -------------------------------------------------------------

TEST(CloudSaveUrl, build_normalizes_bases_and_appends_the_route)
{
    const char* key = "73270125791ba273";
    EXPECT_EQ("https://relay.example/api/save/73270125791ba273",
              build_cloud_save_url("https://relay.example", key));
    EXPECT_EQ("https://relay.example/api/save/73270125791ba273",
              build_cloud_save_url("  https://relay.example/  ", key));
    EXPECT_EQ("https://relay.example/api/save/73270125791ba273",
              build_cloud_save_url("https://relay.example/api", key));
    // Websocket schemes swap to HTTP for the save endpoints.
    EXPECT_EQ("https://relay.example/api/save/73270125791ba273",
              build_cloud_save_url("wss://relay.example", key));
    EXPECT_EQ("http://relay.example/api/save/73270125791ba273",
              build_cloud_save_url("ws://relay.example", key));
}

TEST(CloudSaveUrl, empty_base_falls_back_to_env_then_default)
{
    const char* saved = std::getenv("OPENGLAD_RELAY_BASE_URL");
    const std::string saved_value = saved != nullptr ? saved : "";

    setenv("OPENGLAD_RELAY_BASE_URL", "http://127.0.0.1:8787", 1);
    EXPECT_EQ("http://127.0.0.1:8787/api/save/9a9bba29704ed608",
              build_cloud_save_url("", "9a9bba29704ed608"));

    unsetenv("OPENGLAD_RELAY_BASE_URL");
    EXPECT_EQ("https://openglad.pages.dev/relay/api/save/9a9bba29704ed608",
              build_cloud_save_url("", "9a9bba29704ed608"));

    if (saved != nullptr)
        setenv("OPENGLAD_RELAY_BASE_URL", saved_value.c_str(), 1);
}

// --- JSON builders + parsers ---------------------------------------------------

TEST(CloudSaveJson, post_body_builder_emits_the_documented_shape)
{
    CloudSaveMeta meta;
    meta.slot = "save0";
    meta.save_name = "The \"Iron\" Band\\";
    meta.scen_num = 7;
    meta.last_played = 1754190000;
    const std::vector<std::uint8_t> data = {0x47, 0x54, 0x4c};
    EXPECT_EQ(
        "{\"expected_revision\":2,\"slot\":\"save0\","
        "\"save_name\":\"The \\\"Iron\\\" Band\\\\\",\"scen_num\":7,"
        "\"last_played\":1754190000,\"data_hex\":\"47544c\"}",
        build_cloud_save_post_body(2, meta, data));
}

TEST(CloudSaveJson, get_response_parser_happy_path)
{
    const CloudSaveGetResponse response = parse_cloud_save_get_response(
        R"({"revision":3,"uploaded_at":1754200000000,"slot":"save0",)"
        R"("save_name":"The Iron Band","scen_num":7,)"
        R"("last_played":1754190000,"data_hex":"47544c0e"})");
    EXPECT_EQ(3, response.revision);
    EXPECT_EQ(1754200000000, response.uploaded_at_ms);
    EXPECT_EQ("save0", response.meta.slot);
    EXPECT_EQ("The Iron Band", response.meta.save_name);
    EXPECT_EQ(7, response.meta.scen_num);
    EXPECT_EQ(1754190000, response.meta.last_played);
    EXPECT_EQ((std::vector<std::uint8_t>{0x47, 0x54, 0x4c, 0x0e}),
              response.data);
}

TEST(CloudSaveJson, get_response_parser_rejects_malformed_bodies)
{
    EXPECT_THROW((void)parse_cloud_save_get_response("not json"),
                 std::runtime_error);
    EXPECT_THROW(
        (void)parse_cloud_save_get_response(R"({"data_hex":"47"})"),
        std::runtime_error)
        << "missing revision";
    EXPECT_THROW(
        (void)parse_cloud_save_get_response(R"({"revision":1})"),
        std::runtime_error)
        << "missing data_hex";
    EXPECT_THROW(
        (void)parse_cloud_save_get_response(
            R"({"revision":1,"data_hex":"4"})"),
        std::runtime_error)
        << "odd-length hex";
    EXPECT_THROW(
        (void)parse_cloud_save_get_response(
            R"({"revision":1,"data_hex":"GG"})"),
        std::runtime_error)
        << "non-hex data";
    const std::string oversize(kMaxCloudSaveBytes * 2 + 2, 'a');
    EXPECT_THROW(
        (void)parse_cloud_save_get_response(
            R"({"revision":1,"data_hex":")" + oversize + R"("})"),
        std::runtime_error)
        << "over the size cap";
}

TEST(CloudSaveJson, post_ok_and_conflict_parsers)
{
    EXPECT_EQ(5, parse_cloud_save_post_ok(R"({"revision":5})"));
    EXPECT_THROW((void)parse_cloud_save_post_ok(R"({"ok":true})"),
                 std::runtime_error);
    EXPECT_THROW((void)parse_cloud_save_post_ok(R"({"revision":0})"),
                 std::runtime_error)
        << "a stored revision is always >= 1";

    const CloudSaveConflict conflict = parse_cloud_save_post_conflict(
        R"({"revision":3,"uploaded_at":1754200000000,"slot":"save0",)"
        R"("save_name":"Someone's Band","scen_num":12,)"
        R"("last_played":1754100000})");
    EXPECT_EQ(3, conflict.revision);
    EXPECT_EQ(1754200000000, conflict.uploaded_at_ms);
    EXPECT_EQ("Someone's Band", conflict.meta.save_name);
    EXPECT_EQ(12, conflict.meta.scen_num);
    EXPECT_THROW((void)parse_cloud_save_post_conflict("garbage"),
                 std::runtime_error);
}

// --- cfg persistence -----------------------------------------------------------

TEST(CloudSaveCfg, key_store_resets_revision_and_revision_roundtrips)
{
    CloudCfgGuard guard;
    EXPECT_EQ("", stored_cloud_key());
    EXPECT_EQ(0, stored_cloud_revision());

    store_cloud_revision(7);
    EXPECT_EQ(7, stored_cloud_revision());

    store_cloud_key("73270125791ba273");
    EXPECT_EQ("73270125791ba273", stored_cloud_key());
    EXPECT_EQ(0, stored_cloud_revision())
        << "a new key targets a different vault: revision resets (D9)";

    cfg.apply_setting("cloud", "revision", "garbage");
    EXPECT_EQ(0, stored_cloud_revision());
    cfg.apply_setting("cloud", "revision", "-3");
    EXPECT_EQ(0, stored_cloud_revision());
}

// --- flows -----------------------------------------------------------------------

TEST(CloudSaveFlows, upload_happy_path_sends_verbatim_hex_and_meta)
{
    CloudCfgGuard guard;
    const std::string bytes = valid_gtl_bytes("The Iron Band", 7, 1754190000);
    write_save_file("cloudco", bytes);
    ASSERT_TRUE(og::data::set_active_company_slot("cloudco"));
    store_cloud_key(derive_cloud_save_key("correct horse battery"));

    FakeHooks fake;
    fake.post_results = {http(200, R"({"revision":1})")};
    const std::string status =
        run_cloud_upload("https://relay.example", fake.hooks());
    EXPECT_EQ("Uploaded 'The Iron Band'.", status);

    ASSERT_EQ(1u, fake.post_urls.size());
    EXPECT_EQ("https://relay.example/api/save/73270125791ba273",
              fake.post_urls[0]);
    const std::string& body = fake.post_bodies[0];
    EXPECT_NE(std::string::npos, body.find("\"expected_revision\":0"));
    EXPECT_NE(std::string::npos, body.find("\"slot\":\"cloudco\""));
    EXPECT_NE(std::string::npos, body.find("\"save_name\":\"The Iron Band\""));
    EXPECT_NE(std::string::npos, body.find("\"scen_num\":7"));
    EXPECT_NE(std::string::npos, body.find("\"last_played\":1754190000"));
    std::vector<std::uint8_t> raw(bytes.begin(), bytes.end());
    EXPECT_NE(std::string::npos,
              body.find("\"data_hex\":\"" + hex_encode(raw) + "\""))
        << "the wire blob must be the on-disk file VERBATIM";
    EXPECT_EQ(1, stored_cloud_revision());
    ASSERT_EQ(1u, fake.notifies.size());
    EXPECT_EQ("CLOUD SAVE|Uploaded 'The Iron Band'.", fake.notifies[0]);
    EXPECT_TRUE(fake.confirms.empty());

    remove_save_file("cloudco");
}

TEST(CloudSaveFlows, upload_conflict_confirm_yes_retries_with_server_revision)
{
    CloudCfgGuard guard;
    write_save_file("cloudco", valid_gtl_bytes());
    ASSERT_TRUE(og::data::set_active_company_slot("cloudco"));
    store_cloud_key(derive_cloud_save_key("correct horse battery"));

    FakeHooks fake;
    fake.confirm_verdict = true;
    fake.post_results = {
        http(409,
             R"({"revision":3,"uploaded_at":1754200000000,"slot":"save0",)"
             R"("save_name":"Someone's Band","scen_num":12,)"
             R"("last_played":1754100000})"),
        http(200, R"({"revision":4})"),
    };
    const std::string status =
        run_cloud_upload("https://relay.example", fake.hooks());
    EXPECT_EQ("Uploaded 'FIXTURE COMPANY'.", status);

    ASSERT_EQ(2u, fake.post_bodies.size());
    EXPECT_NE(std::string::npos,
              fake.post_bodies[1].find("\"expected_revision\":3"))
        << "the retry carries the server's revision";
    ASSERT_EQ(1u, fake.confirms.size());
    EXPECT_NE(std::string::npos,
              fake.confirms[0].find("OVERWRITE CLOUD SAVE?"));
    EXPECT_NE(std::string::npos, fake.confirms[0].find("Someone's Band"));
    EXPECT_NE(std::string::npos, fake.confirms[0].find("Level 12"));
    EXPECT_EQ(4, stored_cloud_revision());

    remove_save_file("cloudco");
}

TEST(CloudSaveFlows, upload_conflict_confirm_no_sends_no_second_post)
{
    CloudCfgGuard guard;
    write_save_file("cloudco", valid_gtl_bytes());
    ASSERT_TRUE(og::data::set_active_company_slot("cloudco"));
    store_cloud_key(derive_cloud_save_key("correct horse battery"));
    store_cloud_revision(2);

    FakeHooks fake;
    fake.confirm_verdict = false;
    fake.post_results = {http(409, R"({"revision":5})")};
    const std::string status =
        run_cloud_upload("https://relay.example", fake.hooks());
    EXPECT_EQ("Upload cancelled.", status);
    EXPECT_EQ(1u, fake.post_bodies.size()) << "NO must not retry";
    EXPECT_NE(std::string::npos,
              fake.post_bodies[0].find("\"expected_revision\":2"));
    EXPECT_EQ(2, stored_cloud_revision()) << "revision unchanged on cancel";

    remove_save_file("cloudco");
}

TEST(CloudSaveFlows, upload_guards_key_company_hooks_and_transport)
{
    CloudCfgGuard guard;
    FakeHooks fake;

    // Empty hooks: unavailable (D8) — checked before anything else.
    const std::string unavailable =
        run_cloud_upload("https://relay.example", CloudHooks{});
    EXPECT_EQ("Cloud sync is not available.", unavailable);

    // No passphrase yet.
    EXPECT_EQ("Set a passphrase first.",
              run_cloud_upload("https://relay.example", fake.hooks()));

    // Passphrase but no company file on the active slot.
    store_cloud_key(derive_cloud_save_key("correct horse battery"));
    remove_save_file("cloudmissing");
    ASSERT_TRUE(og::data::set_active_company_slot("cloudmissing"));
    EXPECT_EQ("No company to upload.",
              run_cloud_upload("https://relay.example", fake.hooks()));

    // Transport failure surfaces as a status line, not an exception.
    write_save_file("cloudco", valid_gtl_bytes());
    ASSERT_TRUE(og::data::set_active_company_slot("cloudco"));
    FakeHooks failing;
    failing.post_results = {CloudHttpResult{0, "", "connection refused"}};
    EXPECT_EQ("Upload failed: network error.",
              run_cloud_upload("https://relay.example", failing.hooks()));

    // Damaged local header refuses before any HTTP.
    write_save_file("cloudco", "GTX garbage");
    FakeHooks untouched;
    EXPECT_EQ("Company file is damaged.",
              run_cloud_upload("https://relay.example", untouched.hooks()));
    EXPECT_TRUE(untouched.post_urls.empty());

    remove_save_file("cloudco");
}

TEST(CloudSaveFlows, download_404_and_transport_errors)
{
    CloudCfgGuard guard;
    store_cloud_key(derive_cloud_save_key("correct horse battery"));

    FakeHooks fake;
    fake.get_result = http(404, "No cloud save");
    EXPECT_EQ("No cloud save for this passphrase.",
              run_cloud_download("https://relay.example", fake.hooks(), {}));
    ASSERT_EQ(1u, fake.get_urls.size());
    EXPECT_EQ("https://relay.example/api/save/73270125791ba273",
              fake.get_urls[0]);

    FakeHooks failing;
    failing.get_result = CloudHttpResult{0, "", "dns failure"};
    EXPECT_EQ("Download failed: network error.",
              run_cloud_download("https://relay.example", failing.hooks(),
                                 {}));
}

TEST(CloudSaveFlows, download_happy_path_installs_and_opens)
{
    CloudCfgGuard guard;
    store_cloud_key(derive_cloud_save_key("correct horse battery"));
    remove_save_file("cloudfresh");

    const std::string bytes = valid_gtl_bytes("The Iron Band", 7, 1754190000);
    std::vector<std::uint8_t> raw(bytes.begin(), bytes.end());
    FakeHooks fake;
    fake.get_result = http(
        200,
        R"({"revision":3,"uploaded_at":1754200000000,"slot":"cloudfresh",)"
        R"("save_name":"The Iron Band","scen_num":7,)"
        R"("last_played":1754190000,"data_hex":")" + hex_encode(raw) +
            R"("})");

    std::vector<std::string> opened;
    const std::string status = run_cloud_download(
        "https://relay.example", fake.hooks(),
        [&opened](const std::string& slot) {
            opened.push_back(slot);
            return true;
        });
    EXPECT_EQ("Downloaded 'The Iron Band'.", status);
    EXPECT_TRUE(fake.confirms.empty()) << "no local file -> no overwrite ask";
    ASSERT_EQ(1u, opened.size());
    EXPECT_EQ("cloudfresh", opened[0]);
    EXPECT_EQ(3, stored_cloud_revision());
    const std::optional<std::string> installed = read_save_file("cloudfresh");
    ASSERT_TRUE(installed.has_value());
    EXPECT_EQ(bytes, *installed) << "installed bytes are the decoded blob";

    remove_save_file("cloudfresh");
}

TEST(CloudSaveFlows, download_refuses_unsafe_slots_before_any_confirm)
{
    CloudCfgGuard guard;
    store_cloud_key(derive_cloud_save_key("correct horse battery"));

    for (const char* bad_slot : {"../x", "netsession", ""})
    {
        FakeHooks fake;
        fake.get_result = http(
            200,
            std::string(R"({"revision":1,"slot":")") + bad_slot +
                R"(","save_name":"X","scen_num":1,"last_played":0,)"
                R"("data_hex":"47544c0e"})");
        const std::string status = run_cloud_download(
            "https://relay.example", fake.hooks(),
            [](const std::string&) { return true; });
        EXPECT_EQ("Cloud save refused: bad slot.", status) << bad_slot;
        EXPECT_TRUE(fake.confirms.empty()) << bad_slot;
    }
}

TEST(CloudSaveFlows, download_overwrite_confirm_no_changes_nothing)
{
    CloudCfgGuard guard;
    store_cloud_key(derive_cloud_save_key("correct horse battery"));

    const std::string local = valid_gtl_bytes("Local Band", 2, 1700000000);
    write_save_file("cloudco", local);
    const std::string remote = valid_gtl_bytes("Cloud Band", 9, 1754190000);
    std::vector<std::uint8_t> raw(remote.begin(), remote.end());

    FakeHooks fake;
    fake.confirm_verdict = false;
    fake.get_result = http(
        200,
        R"({"revision":2,"slot":"cloudco","save_name":"Cloud Band",)"
        R"("scen_num":9,"last_played":1754190000,"data_hex":")" +
            hex_encode(raw) + R"("})");
    const std::string status = run_cloud_download(
        "https://relay.example", fake.hooks(),
        [](const std::string&) { return true; });
    EXPECT_EQ("Download cancelled.", status);
    ASSERT_EQ(1u, fake.confirms.size());
    EXPECT_NE(std::string::npos, fake.confirms[0].find("OVERWRITE COMPANY?"));
    EXPECT_NE(std::string::npos, fake.confirms[0].find("Local Band"));
    EXPECT_NE(std::string::npos, fake.confirms[0].find("Cloud Band"));
    EXPECT_EQ(local, read_save_file("cloudco").value_or(""))
        << "NO leaves the local company byte-identical";
    EXPECT_EQ(0, stored_cloud_revision());

    remove_save_file("cloudco");
}

TEST(CloudSaveFlows, download_damaged_blob_installs_nothing)
{
    CloudCfgGuard guard;
    store_cloud_key(derive_cloud_save_key("correct horse battery"));
    remove_save_file("cloudfresh");

    FakeHooks fake;
    fake.get_result = http(
        200,
        R"({"revision":1,"slot":"cloudfresh","save_name":"X","scen_num":1,)"
        R"("last_played":0,"data_hex":"deadbeef"})");
    const std::string status = run_cloud_download(
        "https://relay.example", fake.hooks(),
        [](const std::string&) { return true; });
    EXPECT_EQ("Cloud save is damaged.", status);
    EXPECT_FALSE(read_save_file("cloudfresh").has_value())
        << "junk never lands on disk ([SAVE-R6])";
}

TEST(CloudSaveFlows, download_open_failure_reports_the_missing_campaign)
{
    CloudCfgGuard guard;
    store_cloud_key(derive_cloud_save_key("correct horse battery"));
    remove_save_file("cloudfresh");

    const std::string bytes = valid_gtl_bytes(
        "The Iron Band", 7, 1754190000, "org.example.missing");
    std::vector<std::uint8_t> raw(bytes.begin(), bytes.end());
    FakeHooks fake;
    fake.get_result = http(
        200,
        R"({"revision":1,"slot":"cloudfresh","save_name":"The Iron Band",)"
        R"("scen_num":7,"last_played":1754190000,"data_hex":")" +
            hex_encode(raw) + R"("})");
    const std::string status = run_cloud_download(
        "https://relay.example", fake.hooks(),
        [](const std::string&) { return false; });
    EXPECT_EQ("Downloaded; campaign missing.", status);
    EXPECT_TRUE(read_save_file("cloudfresh").has_value())
        << "the company stays installed on disk (D16)";
    bool named = false;
    for (const std::string& notice : fake.notifies)
        named = named || notice.find("org.example.missing") != std::string::npos;
    EXPECT_TRUE(named) << "the D16 popup names the campaign id";

    remove_save_file("cloudfresh");
}
