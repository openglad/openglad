// Fuzz SaveData::load itself, including roster, campaign state, and the current
// belt format. Accepted saves must have a stable canonical disk representation.
#include "runtime.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t);

namespace {
using og::fuzz::require;

void seeds(const std::filesystem::path& directory)
{
    for (const bool populated : {false, true}) {
        og::fuzz::Input input;
        auto& save = input.save;
        save.save_name = "Fuzz company";
        if (populated) {
            save.team_size = 2;
            save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
            save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
            save.team_list[0]->name = "Guard";
            save.team_list[1]->name = "Mage";
            save.team_list[0]->belt = {1, 2, 3, 0, 0, 0};
            save.team_list[1]->upgrade_to_level(5);
            save.team_list[1]->deployed = false;
            save.last_played_unix_s = 1700000000;
            save.completed_levels["gladiator"] = {1, 2, 3};
            require(save.campaign_state_set("gladiator", "gate_open", 1), "seed state invalid");
            save.arena_lineup_dealt_campaign = "arenas";
            save.arena_lineup_dealt_scen = 2;
        }
        require(save.save("seed"), "production save writer rejected seed");
        const auto bytes = og::fuzz::read_output("save/seed.gtl");
        require(bytes.at(3) == 21, "writer emitted wrong save version");
        require(save.load("seed"), "production save reader rejected generated seed");
        require(save.team_size == (populated ? 2 : 0), "seed did not reach roster loading");
        require(!populated || save.campaign_state_get("gladiator", "gate_open") == 1,
                "seed did not reach campaign state loading");
        og::fuzz::export_seed("save/seed.gtl", directory / (populated ? "v21-roster.gtl" : "v21-empty.gtl"));
        std::fprintf(stderr, "FUZZ_SEED savefile v21 accepted: roster=%u state=%d\n",
                     save.team_size, populated);
    }
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    og::fuzz::initialize((*argv)[0]);
    if (*argc == 3 && std::strcmp((*argv)[1], "--generate-seeds") == 0) {
        seeds((*argv)[2]);
        og::fuzz::check_seed_isolation((*argv)[2], "save/canonical.gtl", LLVMFuzzerTestOneInput);
        std::exit(0);
    }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    og::fuzz::Input input;
    og::fuzz::write_input("save/input.gtl", data, size);
    if (!input.save.load("input")) {
        require(input.save.last_io_error() != SaveDataIoError::None, "failed save lacks error");
        return 0;
    }
    require(input.save.last_io_error() == SaveDataIoError::None, "accepted save carries error");
    require(input.save.team_size <= MAX_TEAM_SIZE, "loaded roster exceeds capacity");
    for (std::size_t i = 0; i < input.save.team_size; ++i)
        require(input.save.team_list[i] != nullptr, "loaded roster has empty slot");
    require(input.save.save("canonical"), "accepted save cannot be serialized");
    const auto canonical = og::fuzz::read_output("save/canonical.gtl");
    require(input.save.load("canonical"), "serialized save cannot be reloaded");
    require(input.save.save("again"), "reloaded save cannot be serialized");
    require(canonical == og::fuzz::read_output("save/again.gtl"),
            "canonical save serialization is not stable");
    return 0;
}
