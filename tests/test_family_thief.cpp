#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/core/stats.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/core/constants.h>
#include "unit/unit.h"
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>

// --- From test_family_thief_coverage_push.cpp ---
const FamilyDescriptor& describe_family_thief();

namespace detail_family_thief_coverage_push {

OG_UNIT_TEST(test_family_thief_descriptor_shape_and_level_up)
{
    const FamilyDescriptor& desc = describe_family_thief();
    OG_ASSERT(desc.family_id == FAMILY_THIEF);
    OG_ASSERT(desc.do_special != nullptr);
    OG_ASSERT(desc.check_special_ai != nullptr);
    OG_ASSERT(desc.level_up != nullptr);

    guy g(FAMILY_THIEF);
    const short old_str = g.strength;
    const short old_dex = g.dexterity;
    const short old_con = g.constitution;
    const short old_int = g.intelligence;
    const short old_armor = g.armor;

    desc.level_up(&g, 2);
    OG_ASSERT(g.strength > old_str);
    OG_ASSERT(g.dexterity > old_dex);
    OG_ASSERT(g.constitution > old_con);
    OG_ASSERT(g.intelligence > old_int);
    OG_ASSERT(g.armor > old_armor);
}

OG_UNIT_TEST(test_family_thief_check_special_ai_foe_distance_paths)
{
    const FamilyDescriptor& desc = describe_family_thief();
    living self;
    living foe;
    self.current_special = 1;
    self.foe = &foe;

    self.setxy(0, 0);
    foe.setxy(60, 0); // >35 and <130 => false
    OG_ASSERT(!desc.check_special_ai(&self));

    foe.setxy(10, 0); // <=35 => true
    OG_ASSERT(desc.check_special_ai(&self));

    self.current_special = 2; // default branch => true
    OG_ASSERT(desc.check_special_ai(&self));
}

OG_UNIT_TEST(test_family_thief_do_special_busy_and_cloak_paths)
{
    const FamilyDescriptor& desc = describe_family_thief();
    og::gameplay::GameWorld world;
    og::sim::SimEventLog events;
    og::gameplay::GameplayContext local_ctx;
    local_ctx.world = &world;
    local_ctx.sim_events = &events;
    og::gameplay::GameplayContext* prev_ctx = og::gameplay::current_game;
    og::gameplay::current_game = &local_ctx;

    living self;
    FixedRandom rng(7);
    self.stats()->level = 3;

    self.current_special = 2; // cloak
    self.invisibility_left = 0;
    OG_ASSERT(desc.do_special(&self));
    OG_ASSERT(self.invisibility_left > 0);

    self.current_special = 3; // taunt/charm
    self.busy = 1;
    OG_ASSERT(!desc.do_special(&self));

    self.current_special = 4; // poison cloud/default
    self.busy = 1;
    OG_ASSERT(!desc.do_special(&self));

    og::gameplay::current_game = prev_ctx;
}
} // namespace detail_family_thief_coverage_push

// --- From test_family_thief_r12.cpp ---
namespace detail_family_thief_r12 {
namespace {

void wire_test_entity_factory(og::gameplay::GameWorld& w)
{
    w.entity_factory = [](Order order, int family) -> std::unique_ptr<walker> {
        std::unique_ptr<walker> ob;
        switch (order)
        {
            case Order::Living: ob = std::make_unique<living>(); break;
            case Order::Weapon: ob = std::make_unique<weap>(); break;
            case Order::Treasure: ob = std::make_unique<treasure>(); break;
            case Order::FX: ob = std::make_unique<effect>(); break;
            default: ob = std::make_unique<walker>(); break;
        }
        ob->set_order_family(order, static_cast<char>(family));
        PixieData stub(8, 1, 1, nullptr);
        ob->set_data(stub);
        return ob;
    };
}

struct ThiefR12Fixture {
    og::gameplay::GameWorld level;
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    og::gameplay::GameplayContext gameplay_ctx;
    og::gameplay::GameplayContext* prev_gameplay_ctx = nullptr;

    ThiefR12Fixture()
    {
        level.myobmap = std::make_unique<obmap>();
        wire_test_entity_factory(level);
        level.id = 1;
        prev_gameplay_ctx = og::gameplay::current_game;
        og::gameplay::current_game = &gameplay_ctx;
        gameplay_ctx.world = &level;
        gameplay_ctx.sim_events = &events;
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }

    ~ThiefR12Fixture()
    {
        og::gameplay::current_game = prev_gameplay_ctx;
    }
};

living* add_living(ThiefR12Fixture& fx, unsigned char team, char family = FAMILY_THIEF)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);

    w->setxy(80, 80);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

} // namespace

OG_UNIT_TEST(test_family_thief_r12_check_ai_and_special_paths)
{
    const FamilyDescriptor& desc = describe_family_thief();
    ThiefR12Fixture fx;

    living* thief = add_living(fx, 0, FAMILY_THIEF);
    living* foe1 = add_living(fx, 1, FAMILY_ORC);
    living* foe2 = add_living(fx, 1, FAMILY_ORC);
    living* foe3 = add_living(fx, 1, FAMILY_ORC);
    OG_ASSERT(thief && foe1 && foe2 && foe3);

    thief->current_special = 1;
    thief->foe = nullptr;
    foe1->setxy(200, 200);
    foe2->setxy(210, 200);
    foe3->setxy(220, 200);
    OG_ASSERT(!desc.check_special_ai(thief));

    foe1->setxy(90, 80);
    foe2->setxy(94, 80);
    foe3->setxy(98, 80);
    OG_ASSERT(desc.check_special_ai(thief));

    thief->current_special = 3;
    thief->shifter_down = 0;
    OG_ASSERT(desc.check_special_ai(thief));
    thief->shifter_down = 1;
    OG_ASSERT(desc.check_special_ai(thief));

    thief->current_special = 1;
    thief->user = -1;
    thief->stats()->level = 4;
    OG_ASSERT(desc.do_special(thief));

    thief->current_special = 3;
    thief->shifter_down = 0;
    thief->busy = 0;
    thief->stats()->name = "Bandit";
    OG_ASSERT(desc.do_special(thief));

    thief->current_special = 3;
    thief->shifter_down = 1;
    thief->busy = 0;
    thief->foe = foe1;
    foe1->real_team_num = 255;
    OG_ASSERT(desc.do_special(thief));

    thief->current_special = 4;
    thief->busy = 0;
    OG_ASSERT(desc.do_special(thief));
}
} // namespace detail_family_thief_r12
