#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/order.h>
#include <openglad/legacy/base.h>

#include <algorithm>
#include <gtest/gtest.h>

namespace {

struct GameWorldEntityIdsFixture : testing::Test
{
    GameWorldEntityIdsFixture()
        : world(123)
    {
        world.entity_factory = [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
            auto entity = std::make_unique<walker>();
            entity->order = order;
            entity->family = static_cast<char>(family);
            entity->sizex = 16;
            entity->sizey = 16;
            return entity;
        };
    }

    GameWorld world;
};

} // namespace

TEST_F(GameWorldEntityIdsFixture, assigns_unique_non_zero_ids_and_finds_entities)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);

    ASSERT_NE(nullptr, living);
    ASSERT_NE(nullptr, fx);
    ASSERT_NE(nullptr, weapon);

    EXPECT_NE(0u, living->entity_id_);
    EXPECT_NE(0u, fx->entity_id_);
    EXPECT_NE(0u, weapon->entity_id_);
    EXPECT_NE(living->entity_id_, fx->entity_id_);
    EXPECT_NE(living->entity_id_, weapon->entity_id_);
    EXPECT_NE(fx->entity_id_, weapon->entity_id_);

    EXPECT_EQ(living, world.find_by_id(living->entity_id_));
    EXPECT_EQ(fx, world.find_by_id(fx->entity_id_));
    EXPECT_EQ(weapon, world.find_by_id(weapon->entity_id_));
    EXPECT_EQ(nullptr, world.find_by_id(0));
    EXPECT_EQ(nullptr, world.find_by_id(999999));
}

TEST_F(GameWorldEntityIdsFixture, remove_ob_erases_entities_from_id_index)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);

    ASSERT_NE(nullptr, living);
    ASSERT_NE(nullptr, fx);
    ASSERT_NE(nullptr, weapon);

    const std::uint32_t living_id = living->entity_id_;
    const std::uint32_t fx_id = fx->entity_id_;
    const std::uint32_t weapon_id = weapon->entity_id_;

    ASSERT_EQ(1, world.remove_ob(weapon));
    EXPECT_EQ(nullptr, world.find_by_id(weapon_id));

    ASSERT_EQ(1, world.remove_ob(fx));
    EXPECT_EQ(nullptr, world.find_by_id(fx_id));

    ASSERT_EQ(1, world.remove_ob(living));
    EXPECT_EQ(nullptr, world.find_by_id(living_id));
}

TEST_F(GameWorldEntityIdsFixture, direct_public_insert_rebuilds_id_index_on_lookup)
{
    auto external = std::make_unique<walker>();
    external->order = Order::Living;
    external->family = FAMILY_SOLDIER;
    external->sizex = 16;
    external->sizey = 16;
    external->entity_id_ = 500;
    walker* raw = external.get();

    world.oblist.push_back(std::move(external));

    EXPECT_EQ(raw, world.find_by_id(500));
}

TEST_F(GameWorldEntityIdsFixture, direct_public_erase_rebuilds_lookup_state)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, living);
    const std::uint32_t living_id = living->entity_id_;

    const auto it = std::find_if(world.oblist.begin(), world.oblist.end(),
                                 [living](const auto& entry) {
                                     return entry.get() == living;
                                 });
    ASSERT_NE(world.oblist.end(), it);

    world.oblist.erase(it);

    EXPECT_EQ(nullptr, world.find_by_id(living_id));
}

TEST_F(GameWorldEntityIdsFixture, moving_entities_between_worlds_preserves_lookup_and_next_id)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);

    ASSERT_NE(nullptr, living);
    ASSERT_NE(nullptr, fx);
    ASSERT_NE(nullptr, weapon);

    const std::uint32_t max_existing_id = weapon->entity_id_;

    GameWorld next_world(456);
    next_world.entity_factory = world.entity_factory;
    next_world.move_entities_from(world);

    EXPECT_EQ(nullptr, world.find_by_id(living->entity_id_));
    EXPECT_EQ(nullptr, world.find_by_id(fx->entity_id_));
    EXPECT_EQ(nullptr, world.find_by_id(weapon->entity_id_));

    EXPECT_EQ(living, next_world.find_by_id(living->entity_id_));
    EXPECT_EQ(fx, next_world.find_by_id(fx->entity_id_));
    EXPECT_EQ(weapon, next_world.find_by_id(weapon->entity_id_));

    walker* fresh = next_world.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, fresh);
    EXPECT_GT(fresh->entity_id_, max_existing_id);
}

TEST_F(GameWorldEntityIdsFixture, direct_public_splice_between_worlds_rebuilds_lookup_state)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, living);
    const std::uint32_t living_id = living->entity_id_;

    GameWorld next_world(456);
    next_world.entity_factory = world.entity_factory;
    next_world.oblist.splice(next_world.oblist.end(), world.oblist);

    EXPECT_EQ(nullptr, world.find_by_id(living_id));
    EXPECT_EQ(living, next_world.find_by_id(living_id));
}
