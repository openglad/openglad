// The game loader owns every byte of parsing. This harness supplies a private
// filesystem and checks that accepted levels survive canonical serialization.
#include "runtime.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/level_file_io.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t);

namespace {
using og::fuzz::require;

PixieData plane(unsigned char fill)
{
    auto pixels = std::make_unique<unsigned char[]>(16 * 16);
    std::fill_n(pixels.get(), 16 * 16, fill);
    return PixieData(1, 16, 16, pixels.release());
}

void seeds(const std::filesystem::path& directory)
{
    for (int version : {9, 10, 11}) {
        og::fuzz::Input input;
        auto& world = *input.world;
        world.grid = plane(PIX_GRASS1);
        world.title = "Fuzz scenario";
        world.set_floor_count(version >= 10 ? 2 : 1);
        if (version >= 10)
            world.grid_for_floor(1) = plane(PIX_GRASS1);
        if (version >= 11) {
            world.decor = plane(DECOR_SHRUB);
            world.decor_for_floor(1) = plane(DECOR_SHRUB);
        }
        walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
        require(actor != nullptr, "core soldier unavailable");
        actor->setxy(32, 48);
        actor->stats()->name = "Fuzz guard";
        actor->set_team_num(1);
        if (version >= 10) {
            actor->set_floor(1);
            actor->set_guard_hold_post(true);
            actor->set_spawn_delay(5);
        }
        og::data::LevelFileMetadata metadata;
        metadata.grid_file = "fuzz";
        metadata.generated = true;
        metadata.description = {"A complete level with a living entity.",
                                "Modern seeds include floors and decor."};
        require(og::data::save_level_scenario_file(world, "scen/seed.fss", metadata),
                "production scenario writer rejected seed");
        auto bytes = og::fuzz::read_output("scen/seed.fss");
        require(bytes.at(3) == version, "writer emitted wrong seed version");
        og::data::LevelFileMetadata loaded_metadata;
        require(og::data::load_level("seed.fss", world, loaded_metadata),
                "production scenario reader rejected generated seed");
        require(world.oblist.size() == 1 && world.floor_count() == (version >= 10 ? 2 : 1),
                "seed did not reach entity and floor loading");
        require(version < 11 || world.decor.valid(), "seed did not reach decor loading");
        og::fuzz::export_seed("scen/seed.fss", directory / ("v" + std::to_string(version) + ".fss"));
        std::fprintf(stderr, "FUZZ_SEED scenario v%d accepted: entity=1 floors=%d decor=%d\n",
                     version, world.floor_count(), world.decor.valid());
    }
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    og::fuzz::initialize((*argv)[0]);
    if (*argc == 3 && std::strcmp((*argv)[1], "--generate-seeds") == 0) {
        seeds((*argv)[2]);
        og::fuzz::check_seed_isolation((*argv)[2], "scen/canonical.fss", LLVMFuzzerTestOneInput);
        std::exit(0);
    }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    og::fuzz::Input input;
    og::fuzz::write_input("scen/input.fss", data, size);
    og::data::LevelFileMetadata metadata;
    og::data::LevelFileIoError error;
    if (!og::data::load_level("input.fss", *input.world, metadata, &error)) {
        require(error != og::data::LevelFileIoError::None, "failed level lacks error");
        return 0;
    }
    require(error == og::data::LevelFileIoError::None, "accepted level carries error");
    require(input.world->grid.valid(), "accepted level has no grid");
    for (const auto* list : {&input.world->oblist, &input.world->fxlist, &input.world->weaplist})
        for (const auto& actor : *list) {
            require(actor != nullptr && actor->stats() != nullptr, "invalid loaded entity");
            require(actor->team_num() >= 0 && actor->team_num() <= MAX_TEAM, "loaded team out of bounds");
        }
    require(og::data::save_level_scenario_file(*input.world, "scen/canonical.fss", metadata),
            "accepted level cannot be serialized");
    const auto canonical = og::fuzz::read_output("scen/canonical.fss");
    require(og::data::load_level("canonical.fss", *input.world, metadata),
            "serialized level cannot be reloaded");
    require(og::data::save_level_scenario_file(*input.world, "scen/again.fss", metadata),
            "reloaded level cannot be serialized");
    require(canonical == og::fuzz::read_output("scen/again.fss"),
            "canonical scenario serialization is not stable");
    return 0;
}
