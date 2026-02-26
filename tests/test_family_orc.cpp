#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/resources/save_io.h>
#include <openglad/resources/gparser.h>
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/core/constants.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include "unit/unit.h"

const FamilyDescriptor& describe_family_orc();
const FamilyDescriptor& describe_family_big_orc();

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

struct OrcR15Fixture {
    og::gameplay::GameWorld level;
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;
    og::gameplay::GameplayContext gameplay_ctx;
    og::gameplay::GameplayContext* prev_gameplay_ctx = nullptr;

    OrcR15Fixture()
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
        gc.rng = &rng;
        set_global_context(&gc);
    }

    ~OrcR15Fixture()
    {
        set_global_context(nullptr);
        og::gameplay::current_game = prev_gameplay_ctx;
    }
};

living* add_living(OrcR15Fixture& fx, unsigned char team, char family, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);

    w->setxy(x, y);
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

walker* add_stain(OrcR15Fixture& fx, short x, short y, unsigned char team, char old_family, std::int32_t level)
{
    walker* stain = fx.level.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    stain->set_order_family(Order::Treasure, FAMILY_STAIN);
    stain->team_num = team;
    stain->setxy(x, y);
    stain->sizex = 16;
    stain->sizey = 16;
    stain->dead = 0;
    stain->stats()->old_family = old_family;
    stain->stats()->level = level;
    return stain;
}

} // namespace

OG_UNIT_TEST(test_family_big_orc_r15_level_up_and_orc_descriptor_hooks)
{
    const FamilyDescriptor& big_orc = describe_family_big_orc();
    OG_ASSERT(big_orc.family_id == FAMILY_BIG_ORC);
    OG_ASSERT(big_orc.level_up != nullptr);

    guy captain(FAMILY_BIG_ORC);
    const short old_str = captain.strength;
    const short old_dex = captain.dexterity;
    const short old_con = captain.constitution;
    const short old_int = captain.intelligence;
    const short old_arm = captain.armor;
    big_orc.level_up(&captain, 2);
    OG_ASSERT(captain.strength > old_str);
    OG_ASSERT(captain.dexterity > old_dex);
    OG_ASSERT(captain.constitution > old_con);
    OG_ASSERT(captain.intelligence > old_int);
    OG_ASSERT(captain.armor > old_arm);

    const FamilyDescriptor& orc = describe_family_orc();
    OG_ASSERT(orc.family_id == FAMILY_ORC);
    OG_ASSERT(orc.do_special != nullptr);
    OG_ASSERT(orc.check_special_ai != nullptr);
    OG_ASSERT(orc.set_difficulty != nullptr);
    OG_ASSERT(orc.level_up != nullptr);
    OG_ASSERT(orc.promotion_new_level != nullptr);

    living w;
    w.damage = 0.0f;
    const float old_hp = w.stats()->max_hitpoints;
    const float old_mp = w.stats()->max_magicpoints;
    const float old_damage = w.damage;
    const float old_armor = w.stats()->armor;
    orc.set_difficulty(&w, 2);
    OG_ASSERT(w.stats()->max_hitpoints > old_hp);
    OG_ASSERT(w.stats()->max_magicpoints > old_mp);
    OG_ASSERT(w.damage >= old_damage);
    OG_ASSERT(w.stats()->armor >= old_armor);

    guy grunt(FAMILY_ORC);
    const short grunt_old_str = grunt.strength;
    orc.level_up(&grunt, 1);
    OG_ASSERT(grunt.strength > grunt_old_str);
    OG_ASSERT(orc.promotion_new_level(5) == 1);
}

OG_UNIT_TEST(test_family_orc_r15_special_howl_and_eat_paths)
{
    const FamilyDescriptor& orc = describe_family_orc();
    {
        OrcR15Fixture fx;
        living* self = add_living(fx, 1, FAMILY_ORC, 96, 96);
        living* foe = add_living(fx, 0, FAMILY_SOLDIER, 112, 96);
        OG_ASSERT(self && foe);

        foe->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        foe->myguy->constitution = 18;

        self->stats()->level = 6;
        self->busy = 0;
        self->current_special = 1; // howl/freeze
        OG_ASSERT(orc.do_special(self));
        OG_ASSERT(self->busy > 0);
        OG_ASSERT(foe->stats()->frozen_delay >= 0);
        OG_ASSERT(fx.events.size() > 0);
    }

    {
        OrcR15Fixture fx;
        living* self = add_living(fx, 1, FAMILY_ORC, 96, 96);
        OG_ASSERT(self != nullptr);
        self->current_special = 2; // eat corpse
        self->stats()->max_hitpoints = 200.0f;
        self->stats()->hitpoints = 30.0f;
        self->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
        self->myguy->name = "R15 ORC";
        cfg.apply_setting("effects", "heal_numbers", "on");

        walker* stain = add_stain(fx, 96, 96, 0, FAMILY_SOLDIER, 4);
        OG_ASSERT(stain != nullptr);
        OG_ASSERT(og::gameplay::current_game != nullptr);
        OG_ASSERT(og::gameplay::current_game->world == &fx.level);
        OG_ASSERT(fx.level.find_nearest_blood(self) == stain);
        OG_ASSERT(self->distance_to_ob_center(stain) <= 24);
        OG_ASSERT(self->stats()->hitpoints < self->stats()->max_hitpoints);
        OG_ASSERT(orc.do_special(self));
        OG_ASSERT(stain->dead == 1);
        OG_ASSERT(self->stats()->hitpoints <= self->stats()->max_hitpoints);
    }
}

OG_UNIT_TEST(test_family_orc_r15_check_ai_and_guard_failures)
{
    const FamilyDescriptor& orc = describe_family_orc();
    OrcR15Fixture fx;

    living* self = add_living(fx, 1, FAMILY_ORC, 50, 50);
    OG_ASSERT(self != nullptr);

    self->foe = nullptr;
    OG_ASSERT(!orc.check_special_ai(self));

    living* near_foe = add_living(fx, 0, FAMILY_SOLDIER, 60, 50);
    OG_ASSERT(near_foe != nullptr);
    OG_ASSERT(orc.check_special_ai(self));

    near_foe->setxy(800, 800);
    self->foe = near_foe;
    OG_ASSERT(!orc.check_special_ai(self));

    self->current_special = 1;
    self->busy = 1;
    OG_ASSERT(!orc.do_special(self));

    self->current_special = 2;
    self->busy = 0;
    self->stats()->hitpoints = self->stats()->max_hitpoints;
    OG_ASSERT(!orc.do_special(self));
}
