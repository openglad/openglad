#include "runtime.h"

#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

std::string get_asset_path();
bool write_pixie_png(const char* filepath, const PixieData& data);

namespace og::fuzz {
namespace {

class Environment {
public:
    std::filesystem::path directory;
    std::unique_ptr<og::runtime::GameSession> session;

    explicit Environment(const char* executable)
    {
        std::array<char, 64> pattern{};
        const std::string prefix =
            (std::filesystem::temp_directory_path() / "openglad-fuzz-XXXXXX").string();
        require(prefix.size() < pattern.size(), "temporary path is too long");
        std::copy(prefix.begin(), prefix.end(), pattern.begin());
        char* created = ::mkdtemp(pattern.data());
        require(created != nullptr, "cannot create private fuzz workspace");
        directory = created;
        require(::setenv("OPENGLAD_CONFIG_DIR", created, 1) == 0,
                "cannot set private config directory");
        for (const char* child : {"save", "scen", "pix", "cfg", "campaigns"})
            std::filesystem::create_directories(directory / child);
        require(og::resources::init(executable), "filesystem initialization failed");
        require(og::resources::set_write_dir(directory.string()), "write directory failed");
        require(og::resources::mount(created, nullptr, 0), "private mount failed");
        const std::string assets = get_asset_path();
        require(og::resources::mount(assets.c_str(), nullptr, 1), "asset mount failed");

        og::runtime::GameSession::Config config{};
        config.allocate_screen = false;
        config.allocate_prefs = false;
        config.allocate_seeded_rng = true;
        config.rng_seed = 1;
        session = std::make_unique<og::runtime::GameSession>(config);
        init_all_registries();
        og::resources::refresh_pack_scripts();
        restore_default_campaigns();

        // Real PNG inputs let v10/v11 seeds exercise floor and decor loading.
        // These are fixed auxiliary assets, never a second scenario parser.
        for (const char* name : {"fuzz", "fuzz_f1", "fuzz_d0", "fuzz_d1",
                                 "scen0099", "scen0123", "grid", "grid_fil"}) {
            auto pixels = std::make_unique<unsigned char[]>(16 * 16);
            std::fill_n(pixels.get(), 16 * 16,
                        static_cast<unsigned char>(std::string_view(name).find("_d") !=
                            std::string_view::npos ? DECOR_SHRUB : PIX_GRASS1));
            PixieData grid(1, 16, 16, pixels.release());
            const std::string path = (directory / "pix" / (std::string(name) + ".png")).string();
            require(write_pixie_png(path.c_str(), grid), "cannot create fuzz grid");
        }
    }

    ~Environment()
    {
        session.reset();
        (void)og::resources::deinit();
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

std::unique_ptr<Environment>& environment()
{
    static std::unique_ptr<Environment> value;
    return value;
}

} // namespace

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FUZZ INVARIANT: %s\n", message);
        std::abort();
    }
}

void initialize(const char* executable)
{
    environment() = std::make_unique<Environment>(executable);
}

void write_input(std::string_view relative, const std::uint8_t* data,
                 std::size_t size)
{
    require(environment() != nullptr, "fuzzer was not initialized");
    const auto path = environment()->directory / relative;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    require(file.good(), "cannot open input file");
    if (size != 0)
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    file.close();
    require(file.good(), "cannot write input file");
}

std::vector<std::uint8_t> read_output(std::string_view relative)
{
    std::ifstream file(environment()->directory / relative, std::ios::binary);
    require(file.good(), "cannot read serialized output");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void export_seed(std::string_view relative, const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::copy_file(environment()->directory / relative, destination,
                              std::filesystem::copy_options::overwrite_existing);
}

void check_seed_isolation(const std::filesystem::path& directory,
                          std::string_view canonical,
                          int (*test_input)(const std::uint8_t*, std::size_t))
{
    std::vector<std::vector<std::uint8_t>> inputs;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file())
            paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        std::ifstream file(path, std::ios::binary);
        require(file.good(), "cannot read generated seed");
        inputs.emplace_back(std::istreambuf_iterator<char>(file),
                            std::istreambuf_iterator<char>());
    }
    require(inputs.size() >= 2, "isolation needs distinct complete seeds");
    auto execute = [&](const std::vector<std::uint8_t>& bytes) {
        // A rejected seed must not inherit the previous input's output.
        std::filesystem::remove(environment()->directory / canonical);
        test_input(bytes.data(), bytes.size());
        return read_output(canonical);
    };
    for (const auto& first : inputs) {
        const auto expected = execute(first);
        for (const auto& other : inputs) {
            execute(other);
            require(expected == execute(first), "A-B-A seed replay changed canonical state");
        }
    }
    std::fprintf(stderr, "FUZZ_SEED isolation: %zu seeds pass A-B-A replay\n", inputs.size());
}

Input::Input() : world(std::make_unique<GameWorld>(1))
{
    require(environment() != nullptr, "fuzzer was not initialized");
    auto& game = environment()->session->game_;
    game.world = world.get();
    game.save = &save;
    game.sim_events = &events;
    environment()->session->ctx_.rng = &rng;
    // Use the same loader-backed entity services as the production server.
    // A bare GameWorld has no factory, so scenario entities cannot be loaded.
    headless_level_data_hooks().wire_world_entity_services(world.get(), nullptr);
}

Input::~Input()
{
    world.reset();
    auto& game = environment()->session->game_;
    game.world = nullptr;
    game.save = nullptr;
    game.sim_events = nullptr;
    environment()->session->ctx_.rng = nullptr;
    const auto campaign = get_mounted_campaign();
    if (!campaign.empty())
        require(unmount_campaign_package_with_error(campaign) == CampaignPackageIoError::None,
                "cannot reset campaign mount between inputs");
}

} // namespace og::fuzz
