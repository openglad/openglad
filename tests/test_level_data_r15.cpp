#include <openglad/data/level_data.h>
#include <openglad/data/level_data_hooks.h>
#include <openglad/data/level_render.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/core/constants.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>

#include "unit/unit.h"

namespace {

int g_wire_calls = 0;
int g_render_calls = 0;

void wire_count(walker*)
{
    g_wire_calls++;
}

std::unique_ptr<LevelRender> make_render(PixieData[])
{
    g_render_calls++;
    return std::make_unique<LevelRender>();
}

} // namespace

OG_UNIT_TEST(test_level_data_r15_campaign_wrappers_and_description_iteration)
{
    CampaignData c("missing_campaign_r15");
    c.description.clear();
    c.description.push_back("line0");
    c.description.push_back("line1");
    c.description.push_back("line2");

    OG_ASSERT(c.get_description_line(2) == "line2");
    OG_ASSERT(c.getDescriptionLine(1) == "line1");
    OG_ASSERT(c.get_description_line(-1).empty());
    OG_ASSERT(c.get_description_line(9).empty());

    (void)c.load_with_error();
    (void)c.save_with_error();
    (void)c.save_as_with_error("missing_campaign_r15_copy");
}

OG_UNIT_TEST(test_level_data_r15_ctor_hooks_add_paths_and_clear)
{
    g_wire_calls = 0;
    g_render_calls = 0;

    LevelDataHooks hooks;
    hooks.wire_entity_from_screen = wire_count;
    hooks.create_level_render = make_render;

    LevelData level_non_headless(9415, &hooks);
    LevelData level_headless(9416, true, &hooks);
    OG_ASSERT(g_render_calls >= 1);

    SaveData save;
    std::int32_t freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    level_non_headless.set_sim_context(&save, &freeze, &events, &rng, &cfg);

    walker* living = level_non_headless.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fxob = level_non_headless.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    walker* weap = level_non_headless.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    OG_ASSERT(living && fxob && weap);
    OG_ASSERT(g_wire_calls >= 3);
    OG_ASSERT(level_non_headless.numobs >= 1);

    level_non_headless.title = "Mutated";
    level_non_headless.type = 7;
    level_non_headless.par_value = 9;
    level_non_headless.time_bonus_limit = 123;
    level_non_headless.topx = 44;
    level_non_headless.topy = 55;
    level_non_headless.clear();

    OG_ASSERT(level_non_headless.title == "New Level");
    OG_ASSERT(level_non_headless.type == 0);
    OG_ASSERT(level_non_headless.par_value == 1);
    OG_ASSERT(level_non_headless.time_bonus_limit == 4000);
    OG_ASSERT(level_non_headless.topx == 0);
    OG_ASSERT(level_non_headless.topy == 0);

    // Exercise delegating constructor overloads.
    LevelData plain_ctor(9417);
    LevelData hooks_ctor(9418, &hooks);
    OG_ASSERT(plain_ctor.id == 9417);
    OG_ASSERT(hooks_ctor.id == 9418);

    (void)level_headless;
}

