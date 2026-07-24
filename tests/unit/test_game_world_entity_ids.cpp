#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/dirty_field_bits.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/order.h>
#include <openglad/legacy/base.h>

#include <algorithm>
#include <gtest/gtest.h>
#include <iterator>

namespace {

struct GameWorldEntityIdsFixture : testing::Test
{
    GameWorldEntityIdsFixture()
        : world(123)
    {
        world.entity_factory = [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
            auto entity = std::make_unique<walker>();
            entity->set_order_family(order, static_cast<char>(family));
            entity->set_sizex(16);
            entity->set_sizey(16);
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

    EXPECT_NE(0u, living->entity_id());
    EXPECT_NE(0u, fx->entity_id());
    EXPECT_NE(0u, weapon->entity_id());
    ASSERT_NE(nullptr, living->stats());
    EXPECT_EQ(living->entity_id(), living->stats()->controller_id());
    EXPECT_NE(living->entity_id(), fx->entity_id());
    EXPECT_NE(living->entity_id(), weapon->entity_id());
    EXPECT_NE(fx->entity_id(), weapon->entity_id());

    EXPECT_EQ(living, world.find_by_id(living->entity_id()));
    EXPECT_EQ(fx, world.find_by_id(fx->entity_id()));
    EXPECT_EQ(weapon, world.find_by_id(weapon->entity_id()));
    EXPECT_EQ(nullptr, world.find_by_id(0));
    EXPECT_EQ(nullptr, world.find_by_id(999999));
}

TEST_F(GameWorldEntityIdsFixture, const_storage_view_preserves_entity_order)
{
    walker* first = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* second = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);

    using Storage = GameWorld::EntityList::Storage;
    const GameWorld::EntityList& entities = world.oblist;
    const Storage& storage = entities;
    ASSERT_EQ(2u, storage.size());
    EXPECT_EQ(first, storage.front().get());
    EXPECT_EQ(second, storage.back().get());
}

TEST_F(GameWorldEntityIdsFixture, remove_ob_erases_entities_from_id_index)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);

    ASSERT_NE(nullptr, living);
    ASSERT_NE(nullptr, fx);
    ASSERT_NE(nullptr, weapon);

    const std::uint32_t living_id = living->entity_id();
    const std::uint32_t fx_id = fx->entity_id();
    const std::uint32_t weapon_id = weapon->entity_id();

    ASSERT_EQ(1, world.remove_ob(weapon));
    EXPECT_EQ(nullptr, world.find_by_id(weapon_id));

    ASSERT_EQ(1, world.remove_ob(fx));
    EXPECT_EQ(nullptr, world.find_by_id(fx_id));

    ASSERT_EQ(1, world.remove_ob(living));
    EXPECT_EQ(nullptr, world.find_by_id(living_id));
}

TEST_F(GameWorldEntityIdsFixture, direct_public_insert_assigns_id_and_rebuilds_id_index_on_lookup)
{
    auto external = std::make_unique<walker>();
    external->set_order_family(Order::Living, FAMILY_SOLDIER);
    external->set_sizex(16);
    external->set_sizey(16);
    walker* raw = external.get();

    world.oblist.push_back(std::move(external));

    EXPECT_NE(0u, raw->entity_id());
    ASSERT_NE(nullptr, raw->stats());
    EXPECT_EQ(raw->entity_id(), raw->stats()->controller_id());
    EXPECT_EQ(raw, world.find_by_id(raw->entity_id()));
}

TEST_F(GameWorldEntityIdsFixture, direct_public_erase_rebuilds_lookup_state)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, living);
    const std::uint32_t living_id = living->entity_id();

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

    const std::uint32_t max_existing_id = weapon->entity_id();

    GameWorld next_world(456);
    next_world.entity_factory = world.entity_factory;
    next_world.move_entities_from(world);

    EXPECT_EQ(nullptr, world.find_by_id(living->entity_id()));
    EXPECT_EQ(nullptr, world.find_by_id(fx->entity_id()));
    EXPECT_EQ(nullptr, world.find_by_id(weapon->entity_id()));

    EXPECT_EQ(living, next_world.find_by_id(living->entity_id()));
    EXPECT_EQ(fx, next_world.find_by_id(fx->entity_id()));
    EXPECT_EQ(weapon, next_world.find_by_id(weapon->entity_id()));

    walker* fresh = next_world.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, fresh);
    EXPECT_GT(fresh->entity_id(), max_existing_id);
}

TEST_F(GameWorldEntityIdsFixture, moving_entities_between_worlds_clears_source_removed_id_log)
{
    walker* removed = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* remaining = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    ASSERT_NE(nullptr, removed);
    ASSERT_NE(nullptr, remaining);

    ASSERT_EQ(1, world.remove_ob(removed));
    ASSERT_FALSE(world.removed_entity_ids().empty());

    GameWorld next_world(456);
    next_world.entity_factory = world.entity_factory;
    next_world.move_entities_from(world);

    EXPECT_TRUE(world.removed_entity_ids().empty());
    EXPECT_EQ(remaining, next_world.find_by_id(remaining->entity_id()));
}

TEST_F(GameWorldEntityIdsFixture, direct_public_splice_between_worlds_rebuilds_lookup_state)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, living);
    const std::uint32_t living_id = living->entity_id();

    GameWorld next_world(456);
    next_world.entity_factory = world.entity_factory;
    next_world.oblist.splice(next_world.oblist.end(), world.oblist);

    EXPECT_EQ(nullptr, world.find_by_id(living_id));
    EXPECT_EQ(living, next_world.find_by_id(living_id));
}

TEST_F(GameWorldEntityIdsFixture, new_entities_start_fully_dirty_and_clear_dirty_resets_mask)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, living);

    EXPECT_EQ(~0ULL, living->dirty_mask_word(0));
    EXPECT_EQ(~0ULL, living->dirty_mask_word(1));

    living->clear_dirty();

    EXPECT_EQ(0ULL, living->dirty_mask_word(0));
    EXPECT_EQ(0ULL, living->dirty_mask_word(1));
}

TEST_F(GameWorldEntityIdsFixture, cross_reference_setters_keep_pointer_and_id_fields_in_sync)
{
    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    walker* leader = world.add_ob(Order::Living, FAMILY_ORC);
    walker* owner = world.add_ob(Order::Living, FAMILY_ORC);
    walker* collide = world.add_ob(Order::Living, FAMILY_ORC);
    walker* controller = world.add_ob(Order::Living, FAMILY_ORC);

    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, leader);
    ASSERT_NE(nullptr, owner);
    ASSERT_NE(nullptr, collide);
    ASSERT_NE(nullptr, controller);

    actor->clear_dirty();
    actor->set_foe(foe);
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_FOE_ID));

    actor->clear_dirty();
    actor->set_leader(leader);
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_LEADER_ID));

    actor->clear_dirty();
    actor->set_owner(owner);
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_OWNER_ID));

    actor->clear_dirty();
    actor->set_collide_ob(collide);
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_COLLIDE_OB_ID));

    actor->clear_dirty();
    actor->stats()->set_controller(controller);
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_CONTROLLER_ID));

    EXPECT_EQ(foe, actor->foe());
    EXPECT_EQ(foe->entity_id(), actor->foe_id());
    EXPECT_EQ(leader, actor->leader());
    EXPECT_EQ(leader->entity_id(), actor->leader_id());
    EXPECT_EQ(owner, actor->owner());
    EXPECT_EQ(owner->entity_id(), actor->owner_id());
    EXPECT_EQ(collide, actor->collide_ob());
    EXPECT_EQ(collide->entity_id(), actor->collide_ob_id());
    EXPECT_EQ(controller, actor->stats()->controller());
    EXPECT_EQ(controller->entity_id(), actor->stats()->controller_id());

    actor->set_foe(nullptr);
    actor->stats()->set_controller(nullptr);

    EXPECT_EQ(nullptr, actor->foe());
    EXPECT_EQ(0u, actor->foe_id());
    EXPECT_EQ(nullptr, actor->stats()->controller());
    EXPECT_EQ(0u, actor->stats()->controller_id());
}

TEST_F(GameWorldEntityIdsFixture, sync_ids_from_pointers_clears_zero_id_cross_references)
{
    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);

    auto orphan_owner = std::make_unique<walker>();
    auto orphan_controller = std::make_unique<walker>();
    ASSERT_EQ(0u, orphan_owner->entity_id());
    ASSERT_EQ(0u, orphan_controller->entity_id());

    actor->clear_dirty();
    actor->set_owner(orphan_owner.get());
    actor->stats()->set_controller(orphan_controller.get());
    actor->clear_dirty();

    actor->sync_ids_from_pointers();

    EXPECT_EQ(nullptr, actor->owner());
    EXPECT_EQ(0u, actor->owner_id());
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_OWNER_ID));
    EXPECT_EQ(nullptr, actor->stats()->controller());
    EXPECT_EQ(0u, actor->stats()->controller_id());
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_CONTROLLER_ID));

    // A stale serialized controller ID with no pointer must be cleared too.
    actor->stats()->set_controller_id(77);
    actor->clear_dirty();
    actor->sync_ids_from_pointers();
    EXPECT_EQ(nullptr, actor->stats()->controller());
    EXPECT_EQ(0u, actor->stats()->controller_id());
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_CONTROLLER_ID));
}

TEST_F(GameWorldEntityIdsFixture,
       sync_ids_from_pointers_clears_links_on_detached_entity)
{
    walker actor;
    walker target;
    ASSERT_NE(nullptr, actor.stats());

    actor.set_foe(&target);
    actor.set_leader(&target);
    actor.set_owner(&target);
    actor.set_collide_ob(&target);
    actor.stats()->set_controller(&target);
    actor.clear_dirty();

    actor.sync_ids_from_pointers();

    EXPECT_EQ(nullptr, actor.foe());
    EXPECT_EQ(nullptr, actor.leader());
    EXPECT_EQ(nullptr, actor.owner());
    EXPECT_EQ(nullptr, actor.collide_ob());
    EXPECT_EQ(nullptr, actor.stats()->controller());
    EXPECT_EQ(0u, actor.foe_id());
    EXPECT_EQ(0u, actor.leader_id());
    EXPECT_EQ(0u, actor.owner_id());
    EXPECT_EQ(0u, actor.collide_ob_id());
    EXPECT_EQ(0u, actor.stats()->controller_id());
    EXPECT_TRUE(actor.is_dirty(og::dirty::BIT_FOE_ID));
    EXPECT_TRUE(actor.is_dirty(og::dirty::BIT_LEADER_ID));
    EXPECT_TRUE(actor.is_dirty(og::dirty::BIT_OWNER_ID));
    EXPECT_TRUE(actor.is_dirty(og::dirty::BIT_COLLIDE_OB_ID));
    EXPECT_TRUE(actor.is_dirty(og::dirty::BIT_CONTROLLER_ID));
}

TEST_F(GameWorldEntityIdsFixture,
       sync_ids_from_pointers_clears_removed_cross_references_without_dereference)
{
    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* target = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, target);

    actor->set_foe(target);
    actor->set_leader(target);
    actor->set_owner(target);
    actor->set_collide_ob(target);
    actor->stats()->set_controller(target);
    actor->clear_dirty();

    ASSERT_EQ(1, world.remove_ob(target));

    actor->sync_ids_from_pointers();

    EXPECT_EQ(nullptr, actor->foe());
    EXPECT_EQ(nullptr, actor->leader());
    EXPECT_EQ(nullptr, actor->owner());
    EXPECT_EQ(nullptr, actor->collide_ob());
    EXPECT_EQ(nullptr, actor->stats()->controller());
    EXPECT_EQ(0u, actor->foe_id());
    EXPECT_EQ(0u, actor->leader_id());
    EXPECT_EQ(0u, actor->owner_id());
    EXPECT_EQ(0u, actor->collide_ob_id());
    EXPECT_EQ(0u, actor->stats()->controller_id());
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_FOE_ID));
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_LEADER_ID));
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_OWNER_ID));
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_COLLIDE_OB_ID));
    EXPECT_TRUE(actor->is_dirty(og::dirty::BIT_CONTROLLER_ID));
}

TEST_F(GameWorldEntityIdsFixture, removing_entities_tracks_removed_entity_ids)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, living);
    const std::uint32_t living_id = living->entity_id();

    ASSERT_TRUE(world.removed_entity_ids().empty());
    ASSERT_EQ(1, world.remove_ob(living));

    ASSERT_EQ(1u, world.removed_entity_ids().size());
    EXPECT_EQ(living_id, world.removed_entity_ids().back());
    EXPECT_EQ(nullptr, world.find_by_id(living_id));
}

TEST_F(GameWorldEntityIdsFixture, take_removed_entity_ids_drains_log)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    ASSERT_NE(nullptr, living);
    ASSERT_NE(nullptr, fx);

    const std::uint32_t living_id = living->entity_id();
    const std::uint32_t fx_id = fx->entity_id();

    ASSERT_EQ(1, world.remove_ob(living));
    ASSERT_EQ(1, world.remove_ob(fx));

    std::vector<std::uint32_t> removed = world.take_removed_entity_ids();
    EXPECT_NE(removed.end(), std::find(removed.begin(), removed.end(), living_id));
    EXPECT_NE(removed.end(), std::find(removed.begin(), removed.end(), fx_id));
    EXPECT_TRUE(world.removed_entity_ids().empty());
}

TEST_F(GameWorldEntityIdsFixture, public_entity_list_removals_track_removed_entity_ids)
{
    walker* erase_target = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* pop_target = world.add_ob(Order::Living, FAMILY_ORC);
    walker* clear_target = world.add_fx_ob(Order::FX, FAMILY_FLASH);

    ASSERT_NE(nullptr, erase_target);
    ASSERT_NE(nullptr, pop_target);
    ASSERT_NE(nullptr, clear_target);

    const std::uint32_t erase_id = erase_target->entity_id();
    const std::uint32_t pop_id = pop_target->entity_id();
    const std::uint32_t clear_id = clear_target->entity_id();

    const auto erase_it = std::find_if(world.oblist.begin(), world.oblist.end(),
                                       [erase_target](const auto& entry) {
                                           return entry.get() == erase_target;
                                       });
    ASSERT_NE(world.oblist.end(), erase_it);

    world.oblist.erase(erase_it);
    world.oblist.pop_back();
    world.fxlist.clear();

    EXPECT_NE(world.removed_entity_ids().end(),
              std::find(world.removed_entity_ids().begin(),
                        world.removed_entity_ids().end(),
                        erase_id));
    EXPECT_NE(world.removed_entity_ids().end(),
              std::find(world.removed_entity_ids().begin(),
                        world.removed_entity_ids().end(),
                        pop_id));
    EXPECT_NE(world.removed_entity_ids().end(),
              std::find(world.removed_entity_ids().begin(),
                        world.removed_entity_ids().end(),
                        clear_id));
}

TEST_F(GameWorldEntityIdsFixture, entity_list_const_reverse_and_front_operations)
{
    auto first = std::make_unique<walker>();
    first->set_order_family(Order::Living, FAMILY_SOLDIER);
    walker* first_raw = first.get();

    auto second = std::make_unique<walker>();
    second->set_order_family(Order::Living, FAMILY_ORC);
    walker* second_raw = second.get();

    world.oblist.push_back(std::move(first));
    world.oblist.push_front(std::move(second));

    const GameWorld::EntityList& list = world.oblist;
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list.front().get(), second_raw);
    EXPECT_EQ(list.back().get(), first_raw);
    EXPECT_EQ(list.cbegin()->get(), second_raw);
    EXPECT_EQ(std::next(list.cbegin())->get(), first_raw);
    EXPECT_EQ(list.cend(), list.end());
    EXPECT_EQ(list.rbegin()->get(), first_raw);
    EXPECT_EQ(std::next(list.rbegin())->get(), second_raw);
    EXPECT_EQ(list.rend(), list.crend());
    EXPECT_EQ(list.crbegin()->get(), first_raw);

    const std::uint32_t second_id = second_raw->entity_id();
    world.oblist.pop_front();
    EXPECT_EQ(nullptr, world.find_by_id(second_id));
    ASSERT_EQ(world.oblist.size(), 1u);
    EXPECT_EQ(world.oblist.front().get(), first_raw);
}

TEST_F(GameWorldEntityIdsFixture, delete_objects_tracks_removed_entity_ids_for_all_live_lists)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* fx = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);

    ASSERT_NE(nullptr, living);
    ASSERT_NE(nullptr, fx);
    ASSERT_NE(nullptr, weapon);

    const std::uint32_t living_id = living->entity_id();
    const std::uint32_t fx_id = fx->entity_id();
    const std::uint32_t weapon_id = weapon->entity_id();

    world.delete_objects();

    EXPECT_NE(world.removed_entity_ids().end(),
              std::find(world.removed_entity_ids().begin(),
                        world.removed_entity_ids().end(),
                        living_id));
    EXPECT_NE(world.removed_entity_ids().end(),
              std::find(world.removed_entity_ids().begin(),
                        world.removed_entity_ids().end(),
                        fx_id));
    EXPECT_NE(world.removed_entity_ids().end(),
              std::find(world.removed_entity_ids().begin(),
                        world.removed_entity_ids().end(),
                        weapon_id));
}

TEST_F(GameWorldEntityIdsFixture, clear_resets_removed_entity_id_log_for_new_world_lifecycle)
{
    walker* living = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, living);

    ASSERT_EQ(1, world.remove_ob(living));
    ASSERT_FALSE(world.removed_entity_ids().empty());

    world.clear();

    EXPECT_TRUE(world.removed_entity_ids().empty());
}
