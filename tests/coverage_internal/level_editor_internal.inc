namespace
{
class LevelEditorTestingStateGuard
{
public:
    LevelEditorTestingStateGuard()
        : state_(eds()), saved_state_(state_), mouse_(query_mouse_no_poll()),
          saved_mouse_(mouse_) {}

    ~LevelEditorTestingStateGuard()
    {
        state_ = std::move(saved_state_);
        mouse_ = saved_mouse_;
    }

private:
    LevelEditorState& state_;
    LevelEditorState saved_state_;
    MouseState& mouse_;
    MouseState saved_mouse_;
};

class LevelEditorTestingMountGuard
{
public:
    LevelEditorTestingMountGuard() : initially_mounted_(get_mounted_campaign()) {}

    ~LevelEditorTestingMountGuard()
    {
        if (get_mounted_campaign() != initially_mounted_)
            (void)switch_to(initially_mounted_);
    }

    bool switch_to(const std::string& campaign_id)
    {
        const std::string mounted = get_mounted_campaign();
        if (mounted == campaign_id)
            return true;

        CampaignPackageIoError error = CampaignPackageIoError::None;
        if (campaign_id.empty())
        {
            if (!mounted.empty())
                error = unmount_campaign_package_with_error(mounted);
        }
        else
        {
            // mount_campaign_package_with_error performs the checked switch
            // from a different currently mounted package itself.
            error = mount_campaign_package_with_error(campaign_id);
        }
        return error == CampaignPackageIoError::None &&
               get_mounted_campaign() == campaign_id;
    }

    bool restore() { return switch_to(initially_mounted_); }
    const std::string& initially_mounted() const { return initially_mounted_; }

private:
    std::string initially_mounted_;
};
} // namespace

int level_editor_test_exercise_internal_helpers()
{
    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
            failed_check = check_index;
    };
    LevelEditorTestingStateGuard editor_state_guard;
    const std::string helper_campaign_id = "org.openglad.coverage.editor_helper";
    LevelEditorTestingMountGuard mount_guard;
    const auto mount_campaign_only = [&](const std::string& campaign_id) {
        const bool success = mount_guard.switch_to(campaign_id);
        check(success);
        return success;
    };

    mount_campaign_only("");
    restore_default_campaigns();
    mount_campaign_only("org.openglad.gladiator");
    delete_campaign(helper_campaign_id);

    Rect r;
    check(!r.contains(1, 1));

    Rectf rf_pos(10.0f, 10.0f, 5.0f, 5.0f);
    Rectf rf_neg(10.0f, 10.0f, -5.0f, -5.0f);
    Rectf rf_neg_w(10.0f, 10.0f, -5.0f, 5.0f);
    Rectf rf_neg_h(10.0f, 10.0f, 5.0f, -5.0f);
    check(rf_pos.contains(12.0f, 12.0f));
    check(rf_neg.contains(8.0f, 8.0f));
    check(rf_neg_w.contains(8.0f, 12.0f));
    check(rf_neg_h.contains(12.0f, 8.0f));
    SimpleButton btn("X", 0, 0, 20, 10);
    check(btn.contains(1, 1));
    btn.set_colors_disabled();
    check(btn.text_color == 3 && btn.base_color == 10 &&
          btn.high_color == 12 && btn.shadow_color == 14);
    btn.set_colors_active();
    check(btn.text_color == static_cast<unsigned char>(WHITE) &&
          btn.base_color == static_cast<unsigned char>(ORANGE_START) &&
          btn.high_color == static_cast<unsigned char>(ORANGE_START + 3) &&
          btn.shadow_color == static_cast<unsigned char>(ORANGE_START + 5));

    EditorObjectBrush brush;
    brush.set(nullptr);
    check(brush.order == Order::Living && brush.family == 0);
    check(std::string(decor_short_label(-1)) == "?");
    check(std::string(decor_short_label(DECOR_MAX)) == "?");
    SelectionInfo null_selection(nullptr);
    check(!null_selection.valid && null_selection.get_object(nullptr) == nullptr);

    LevelEditorData data;
    check(data.level != nullptr);
    if (data.level != nullptr)
    {
        backgrounds().assign(kDefaultBackgrounds.begin(),
                             kDefaultBackgrounds.end());
        object_pane().clear();
        object_pane().push_back({Order::Living, FAMILY_SOLDIER});
        object_pane().push_back({Order::Generator, FAMILY_TENT});
        object_pane().push_back({Order::Treasure, FAMILY_GOLD_BAR});
        object_pane().push_back({Order::Weapon, FAMILY_KNIFE});

        data.level->create_new_grid();
        // Avoid smoother/radar update paths in this helper; those are exercised elsewhere
        // and have global UI dependencies that make this test nondeterministic.
        std::fill_n(data.level->world().grid.data.get(),
                    data.level->world().grid.w * data.level->world().grid.h,
                    static_cast<unsigned char>(1));
        data.set_terrain(0, 0, PIX_GRASS2);
        check(data.get_terrain(0, 0) == PIX_GRASS2);
        check(data.get_terrain(-1, -1) == 0);
        // Z-axis: add a floor, paint it, and confirm per-floor isolation.
        eds().current_floor = 0;
        data.add_floor();
        check(data.level->world().floor_count() == 2);
        check(data.level->world().is_multifloor());
        check(eds().current_floor == 1);
        data.set_terrain(1, 1, PIX_GLASS);
        check(data.get_terrain(1, 1) == PIX_GLASS);     // floor 1
        eds().current_floor = 0;
        check(data.get_terrain(1, 1) != PIX_GLASS);     // floor 0 untouched
        // remove_floor drops the top floor back to single-floor (LIFO).
        eds().current_floor = 1;
        data.remove_floor();
        check(data.level->world().floor_count() == 1);
        check(!data.level->world().is_multifloor());
        check(eds().current_floor == 0);                // clamped off the removed floor
        data.remove_floor();                            // base floor: no-op
        check(data.level->world().floor_count() == 1);
        // Westlands tiles: the default brush palette carries all 8 new ids
        // (grown 108 -> 116) and painting one round-trips the byte.
        check(kDefaultBackgrounds.size() == 116);
        for (const Sint32 pix : {PIX_SNOW1, PIX_SNOW2, PIX_LAVA1, PIX_LAVA2,
                                 PIX_MARSH1, PIX_MARSH2, PIX_ASH1, PIX_ASH2})
            check(std::find(kDefaultBackgrounds.begin(),
                            kDefaultBackgrounds.end(),
                            pix) != kDefaultBackgrounds.end());
        data.set_terrain(2, 2, PIX_LAVA1);
        check(data.get_terrain(2, 2) == PIX_LAVA1);
        // Tile layering: decor brush plumbing.
        // Erase with no plane allocated stays a no-op and does NOT allocate —
        // decor-free levels keep re-saving byte-identical (v9/v10 cascade).
        check(!data.level->world().decor.valid());
        check(!data.set_decor(0, 0, DECOR_NONE));
        check(!data.level->world().decor.valid());
        check(data.get_decor(0, 0) == DECOR_NONE);
        // First real paint lazily allocates the plane and writes the byte.
        check(data.set_decor(3, 3, DECOR_BOULDER_2));
        check(data.level->world().decor.valid());
        check(data.get_decor(3, 3) == DECOR_BOULDER_2);
        check(data.get_decor(4, 3) == DECOR_NONE);
        // Painting base under existing decor keeps the decor byte — the core
        // workflow the layering exists for (autotile ground under a torch).
        data.set_terrain(3, 3, PIX_DIRT_1);
        check(data.get_decor(3, 3) == DECOR_BOULDER_2);
        check(data.get_terrain(3, 3) == PIX_DIRT_1);
        // Guards: out-of-registry ids, and air/Z-stair base cells, refused.
        check(!data.set_decor(3, 3, DECOR_MAX));
        check(!data.set_decor(-1, -1, DECOR_TORCH1));
        data.set_terrain(5, 5, PIX_AIR);
        check(!data.set_decor(5, 5, DECOR_TORCH1));
        check(data.get_decor(5, 5) == DECOR_NONE);
        data.set_terrain(5, 5, PIX_ZSTAIR_UP);
        check(!data.set_decor(5, 5, DECOR_TORCH1));
        data.set_terrain(5, 5, PIX_ZSTAIR_DOWN);
        check(!data.set_decor(5, 5, DECOR_TORCH1));
        // ... but erasing those cells still works (writes 0).
        check(data.set_decor(5, 5, DECOR_NONE));
        data.set_terrain(5, 5, PIX_GRASS1);
        // Erase brings a painted byte back to 0.
        check(data.set_decor(3, 3, DECOR_NONE));
        check(data.get_decor(3, 3) == DECOR_NONE);
        // Per-floor isolation: each floor paints its own plane, lazily.
        check(data.set_decor(3, 3, DECOR_PEBBLES));
        data.add_floor();
        check(eds().current_floor == 1);
        check(!data.level->world().decor_for_floor(1).valid());
        check(data.get_decor(3, 3) == DECOR_NONE);          // floor 1: empty
        // add_floor fills the new floor with PIX_AIR: decor refused until a
        // solid base is painted there.
        check(!data.set_decor(2, 2, DECOR_SHRUB));
        data.set_terrain(2, 2, PIX_GRASS1);
        check(data.set_decor(2, 2, DECOR_SHRUB));
        check(data.get_decor(2, 2) == DECOR_SHRUB);
        eds().current_floor = 0;
        check(data.get_decor(2, 2) == DECOR_NONE);          // floor 0 untouched
        check(data.get_decor(3, 3) == DECOR_PEBBLES);
        walker* upper_floor_object =
            data.level->add_ob(Order::Living, FAMILY_SOLDIER);
        check(upper_floor_object != nullptr);
        if (upper_floor_object != nullptr)
            upper_floor_object->set_floor(1);
        data.remove_floor();  // drops floor 1's decor plane with its ExtraFloor
        check(data.level->world().floor_count() == 1);
        check(std::none_of(data.level->world().oblist.begin(),
                           data.level->world().oblist.end(),
                           [upper_floor_object](const auto& candidate) {
                               return candidate.get() == upper_floor_object;
                           }));
        data.clear_terrain();
	        check(data.get_terrain(0, 0) == 1);
	        // clear_terrain also zeroes this floor's decor plane.
	        check(data.get_decor(3, 3) == DECOR_NONE);
	        data.level->world().id = 1;
	        check(data.loadLevel(1));
	        check(data.reloadLevel());
	        check(data.loadCampaign("org.openglad.gladiator"));
        check(data.reloadCampaign());

        walker* inside = data.level->add_ob(Order::Living, FAMILY_SOLDIER);
        check(inside != nullptr);
        if (inside != nullptr)
        {
            inside->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            SelectionInfo sel(inside);
            check(sel.get_object(data.level.get()) == inside);
            SelectionInfo cleared(inside);
            cleared.clear();
            check(!cleared.valid && cleared.name.empty() && cleared.target == inside);
            walker* effect = data.level->add_fx_ob(Order::FX, FAMILY_KNIFE_BACK);
            check(effect != nullptr);
            if (effect != nullptr)
                effect->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            walker* weapon = data.level->add_weap_ob(Order::Weapon, FAMILY_KNIFE);
            check(weapon != nullptr);
            if (weapon != nullptr)
                weapon->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            std::vector<SelectionInfo> selection;
            Rectf area(static_cast<float>(inside->xpos() - 2),
                       static_cast<float>(inside->ypos() - 2),
                       static_cast<float>(inside->sizex() + 4),
                       static_cast<float>(inside->sizey() + 4));
            add_contained_objects_to_selection(data.level.get(), area, selection);
            check(is_in_selection(inside, selection));
            check(data.get_object(inside->xpos(), inside->ypos()) == inside);
        }

        data.mode = Mode::Terrain;
        data.reset_mode_buttons();
        check(data.mode_buttons.find(&data.terrainSmoothButton) != data.mode_buttons.end());
        data.activate_mode_button(&data.pickerButton);
        check(data.terrain_brush.picking);
        data.activate_mode_button(&data.pickerButton);
        check(!data.terrain_brush.picking);
        data.activate_mode_button(&data.terrainSmoothButton);
        check(!data.terrain_brush.use_smoothing);
        data.activate_mode_button(&data.terrainSmoothButton);
        check(data.terrain_brush.use_smoothing);
        // Decor toggle: Terrain-mode button flipping eds().decor_mode.
        check(data.mode_buttons.find(&data.decorButton) != data.mode_buttons.end());
        eds().decor_mode = false;
        data.activate_mode_button(&data.decorButton);
        check(eds().decor_mode);
        data.activate_mode_button(&data.decorButton);
        check(!eds().decor_mode);
        // Outside Terrain mode the decor toggle is inert.
        data.mode = Mode::Object;
        data.activate_mode_button(&data.decorButton);
        check(!eds().decor_mode);
        data.mode = Mode::Terrain;

        data.mode = Mode::Object;
        data.object_brush.snap_to_grid = true;
        data.reset_mode_buttons();
        data.activate_mode_button(&data.pickerButton);
        check(data.object_brush.picking);
        data.activate_mode_button(&data.pickerButton);
        check(!data.object_brush.picking);
        data.activate_mode_button(&data.gridSnapButton);
        check(!data.object_brush.snap_to_grid);

        data.object_brush.team = 0;
        data.activate_mode_button(&data.prevTeamButton);
        check(data.object_brush.team == MAX_TEAM);
        data.activate_mode_button(&data.nextTeamButton);
        check(data.object_brush.team == 0);
        data.activate_mode_button(&data.nextTeamButton);
        check(data.object_brush.team == 1);
        data.object_brush.team = 3;
        data.activate_mode_button(&data.prevTeamButton);
        check(data.object_brush.team == 2);

        // Per-object Z: the brush's authored worldz transfers to placed objects.
        data.object_brush.worldz = GRID_SIZE;
        walker* zob = data.level->add_ob(Order::Living, FAMILY_SOLDIER);
        check(zob != nullptr);
        if (zob)
        {
            zob->set_worldz(static_cast<float>(data.object_brush.worldz));
            check(zob->worldz() == static_cast<float>(GRID_SIZE));
            data.level->remove_ob(zob);
        }
        data.object_brush.worldz = 0;

        data.mode = Mode::Select;
        data.level->delete_objects();
        data.selection.clear();
        walker* selected = data.level->add_ob(Order::Living, FAMILY_SOLDIER);
        check(selected != nullptr);
        if (selected != nullptr)
        {
            selected->setxy(GRID_SIZE * 3, GRID_SIZE * 3);
            selected->set_team_num(0);
            selected->stats()->name = "Before";
            selected->stats()->set_level(1);
            selected->set_curdir(FACE_UP_LEFT);
            data.selection.push_back(SelectionInfo(selected));

            data.reset_mode_buttons();
            check(data.mode_buttons.find(&data.setNameButton) != data.mode_buttons.end());

            level_editor_testing_prompt_queue_clear();
            level_editor_testing_prompt_queue_push("After");
            data.activate_mode_button(&data.setNameButton);
            check(selected->stats()->name == "After");

            data.activate_mode_button(&data.prevTeamButton);
            check(selected->team_num() == MAX_TEAM);
	            data.activate_mode_button(&data.nextTeamButton);
	            check(selected->team_num() == 0);
	            selected->set_team_num(2);
	            data.activate_mode_button(&data.prevTeamButton);
	            check(selected->team_num() == 1);
	            selected->set_team_num(MAX_TEAM);
	            data.activate_mode_button(&data.nextTeamButton);
	            check(selected->team_num() == 0);
	            selected->set_team_num(2);
	            data.activate_mode_button(&data.nextTeamButton);
	            check(selected->team_num() == 3);
	            data.activate_mode_button(&data.prevLevelButton);
	            check(selected->stats()->level() == 1);
	            data.activate_mode_button(&data.nextLevelButton);
	            check(selected->stats()->level() == 2);
	            data.activate_mode_button(&data.prevLevelButton);
	            check(selected->stats()->level() == 1);
	            selected->stats()->set_level(3);
	            data.selection.front().level = selected->stats()->level();
	            data.activate_mode_button(&data.prevLevelButton);
	            check(selected->stats()->level() == 2);
	            data.selection.front().family = 0;
	            data.activate_mode_button(&data.prevClassButton);
	            check(!data.selection.empty() && data.selection.front().family >= 0);
	            data.selection.front().family = FAMILY_SOLDIER + 1;
	            data.activate_mode_button(&data.prevClassButton);
	            check(data.selection.front().family == FAMILY_SOLDIER);
	            data.selection.front().family = NUM_FAMILIES - 1;
	            data.activate_mode_button(&data.nextClassButton);
	            check(!data.selection.empty() && data.selection.front().family >= 0);
	            data.selection.front().family = FAMILY_SOLDIER;
	            data.activate_mode_button(&data.nextClassButton);
	            check(data.selection.front().family == FAMILY_SOLDIER + 1);
	            data.activate_mode_button(&data.facingButton);
	            check(selected->curdir() == FACE_UP);
	            selected->set_curdir(FACE_UP);
	            data.activate_mode_button(&data.facingButton);
	            check(selected->curdir() != FACE_UP);

	            // AI policy cycle button: ROAM -> GUARD -> HOLD -> ROAM on the
	            // authored act_type/guard_hold_post pair.
	            check(data.mode_buttons.find(&data.aiButton) != data.mode_buttons.end());
	            selected->set_act_type_state(ACT_RANDOM);
	            selected->set_guard_hold_post(false);
	            data.activate_mode_button(&data.aiButton);
	            check(selected->act_type() == ACT_GUARD && !selected->guard_hold_post());
	            data.activate_mode_button(&data.aiButton);
	            check(selected->act_type() == ACT_GUARD && selected->guard_hold_post());
	            data.activate_mode_button(&data.aiButton);
	            check(selected->act_type() == ACT_RANDOM && !selected->guard_hold_post());
	            // The select-panel info box shows the same three states.
	            check(std::string("ROAM") == editor_ai_policy_label(ACT_RANDOM, false));
	            check(std::string("GUARD") == editor_ai_policy_label(ACT_GUARD, false));
	            check(std::string("HOLD") == editor_ai_policy_label(ACT_GUARD, true));
	            // Non-Living selections neither drive nor receive the cycle:
	            // a weapon first in the list is skipped over.
	            walker* ai_weap = data.level->add_weap_ob(Order::Weapon, FAMILY_KNIFE);
	            check(ai_weap != nullptr);
	            if (ai_weap != nullptr)
	            {
	                ai_weap->setxy(GRID_SIZE * 5, GRID_SIZE * 5);
	                data.selection.insert(data.selection.begin(), SelectionInfo(ai_weap));
	                data.activate_mode_button(&data.aiButton);  // first Living drives: ROAM -> GUARD
	                check(selected->act_type() == ACT_GUARD && !selected->guard_hold_post());
	                check(ai_weap->act_type() != ACT_GUARD && !ai_weap->guard_hold_post());
	                // A selection with no Living at all is a no-op.
	                data.selection.erase(data.selection.begin() + 1);
	                data.activate_mode_button(&data.aiButton);
	                check(ai_weap->act_type() != ACT_GUARD && !ai_weap->guard_hold_post());
	                data.selection.clear();
	                data.selection.push_back(SelectionInfo(selected));
	                data.level->remove_ob(ai_weap);
	            }
	            selected->set_act_type_state(ACT_RANDOM);
	            selected->set_guard_hold_post(false);

	            MouseState& mouse = query_mouse_no_poll();
	            mouse.left = true;
	            data.rect_selecting = false;
	            data.dragging = false;
	            screen* motion_screen = og::runtime::current_session->myscreen_;
	            check(motion_screen != nullptr && motion_screen->viewob[0] != nullptr);
	            if (motion_screen != nullptr && motion_screen->viewob[0] != nullptr)
	            {
	                const int drag_screen_x = 160;
	                const int drag_screen_y = 160;
	                const int selected_world_x =
	                    drag_screen_x + data.level->level_visuals().topx - motion_screen->viewob[0]->xloc;
	                const int selected_world_y =
	                    drag_screen_y + data.level->level_visuals().topy - motion_screen->viewob[0]->yloc;
	                selected->setxy(selected_world_x, selected_world_y);
	                data.selection.front().set(selected);
	                check(!data.mouse_on_menus(drag_screen_x, drag_screen_y));
	                check(data.get_object(selected_world_x, selected_world_y) == selected);
	                eds().mouse_last_x = drag_screen_x;
	                eds().mouse_last_y = drag_screen_y;
	                data.mouse_motion(drag_screen_x, drag_screen_y, 3, 4);
	                check(data.dragging);
	                data.dragging = false;
	                data.mouse_motion(80, 80, 20, 20);
	                check(data.rect_selecting);
	            }
            mouse.left = false;

            data.selection.clear();
            data.selection.push_back(SelectionInfo(selected));
            data.dragging = true;
            mouse.left = true;
            data.mouse_motion(90, 90, 5, 6);
            mouse.left = false;
            check(data.selection.front().x == selected->xpos() &&
                data.selection.front().y == selected->ypos());
            data.dragging = false;

            data.rect_selecting = true;
            data.selection_rect = Rectf(static_cast<float>(selected->xpos() - 4),
                static_cast<float>(selected->ypos() - 4),
                static_cast<float>(selected->sizex() + 8),
                static_cast<float>(selected->sizey() + 8));
            bool done = false;
            data.mouse_up(160, 160, 160, 160, done);
            check(!data.rect_selecting && !data.selection.empty());

            data.activate_mode_button(&data.deleteButton);
            check(data.selection.empty());
        }

        data.mode = Mode::Terrain;
        data.reset_mode_buttons();
        data.terrain_brush.picking = true;
        data.set_terrain(2, 2, PIX_WATER1);
        data.pick_by_mouse(GRID_SIZE * 2, GRID_SIZE * 2);
        check(data.terrain_brush.terrain == PIX_WATER1);

        bool done = false;
        data.mouse_up(S_RIGHT + 1, PIX_TOP + 1, S_RIGHT + 1, PIX_TOP + 1, done);
        check(data.terrain_brush.terrain >= 0);

        // Decor mode: right-click pick reads the decor plane, and a click on
        // the picker pane selects a decor id (cell 0 with rowsdown 0 is the
        // eraser). Water base takes decor fine — decor only ever restricts.
        eds().decor_mode = true;
        const Sint32 base_brush_before_decor = data.terrain_brush.terrain;
        check(data.set_decor(2, 2, DECOR_BRAZIER));
        data.terrain_brush.picking = true;
        data.pick_by_mouse(GRID_SIZE * 2, GRID_SIZE * 2);
        check(data.terrain_brush.decor == DECOR_BRAZIER);
        eds().rowsdown = 0;
        data.mouse_up(S_RIGHT + 1, PIX_TOP + 1, S_RIGHT + 1, PIX_TOP + 1, done);
        check(data.terrain_brush.decor == DECOR_NONE);
        // Decor pick + pane click never touch the base brush.
        check(data.terrain_brush.terrain == base_brush_before_decor);
        check(data.set_decor(2, 2, DECOR_NONE)); // clean the loaded level again
        eds().decor_mode = false;

        data.mode = Mode::Object;
        data.reset_mode_buttons();
        data.object_brush.order = Order::Living;
        data.object_brush.family = FAMILY_SOLDIER;
        data.object_brush.snap_to_grid = true;
        data.object_brush.picking = false;
        data.mouse_up(140, 120, 140, 120, done);
        check(eds().levelchanged);
        data.object_brush.picking = true;
        data.pick_by_mouse(140, 120);
        check(!data.object_brush.picking || data.object_brush.family == FAMILY_SOLDIER);

        MouseState& menu_mouse = query_mouse_no_poll();
        menu_mouse.left = false;
        menu_mouse.right = false;
        auto click_button = [&](SimpleButton& button, bool expected_done = false) {
            bool menu_done = false;
            const int x = button.area.x + 1;
            const int y = button.area.y + 1;
            data.mouse_up(x, y, x, y, menu_done);
            check(menu_done == expected_done);
            return menu_done;
        };
        auto open_campaign_menu = [&] {
            data.current_menu.clear();
            click_button(data.campaignButton);
        };
        auto open_campaign_profile_menu = [&] {
            open_campaign_menu();
            click_button(data.campaignProfileButton);
        };
        auto open_campaign_details_menu = [&] {
            open_campaign_menu();
            click_button(data.campaignDetailsButton);
        };
        auto open_level_menu = [&] {
            data.current_menu.clear();
            click_button(data.levelButton);
        };
        auto open_level_profile_menu = [&] {
            open_level_menu();
            click_button(data.levelProfileButton);
        };
        auto open_level_details_menu = [&] {
            open_level_menu();
            click_button(data.levelDetailsButton);
        };
        auto open_level_goals_menu = [&] {
            open_level_menu();
            click_button(data.levelGoalsButton);
        };
        auto open_mode_menu = [&] {
            data.current_menu.clear();
            click_button(data.modeButton);
        };
        auto open_file_menu = [&] {
            data.current_menu.clear();
            click_button(data.fileButton);
        };
        auto open_file_campaign_menu = [&] {
            open_file_menu();
            click_button(data.fileCampaignButton);
        };
        auto open_file_level_menu = [&] {
            open_file_menu();
            click_button(data.fileLevelButton);
        };
        auto expose_hidden_file_campaign_choice = [&](SimpleButton& button) {
            data.current_menu.clear();
            std::set<SimpleButton*> s;
            s.insert(&button);
            data.current_menu.push_back(std::make_pair(&data.fileCampaignButton, s));
        };

        data.mode = Mode::Terrain;
        data.reset_mode_buttons();
        data.update_menu_buttons();
        data.pan_buttons.insert(&data.panLeftButton);
        data.pan_buttons.insert(&data.panRightButton);
        data.pan_buttons.insert(&data.panUpButton);
        data.pan_buttons.insert(&data.panDownButton);
        check(data.mouse_on_menus(data.terrainSmoothButton.area.x + 1, data.terrainSmoothButton.area.y + 1));
        check(data.mouse_on_menus(data.panLeftButton.area.x + 1, data.panUpButton.area.y + 1));
        check(!data.mouse_on_menus(200, 170));

        data.current_menu.clear();
        click_button(data.fileButton);
        click_button(data.fileButton);
        check(data.current_menu.empty());
        open_file_menu();
        open_file_campaign_menu();
        check(!data.current_menu.empty());

        const std::string previously_mounted_campaign = get_mounted_campaign();
        delete_campaign(helper_campaign_id);

        eds().levelchanged = 1;
        picker_testing_yes_or_no_queue_push(true);
        level_editor_testing_prompt_queue_push(helper_campaign_id.c_str());
        open_file_campaign_menu();
        click_button(data.fileCampaignNewButton);
	        check(data.campaign->id == helper_campaign_id);
	        check(data.saveCampaignAs(helper_campaign_id));
	        check(data.saveLevelAs(3));

	        // Decor paint → save → reload round-trip through the editor save
	        // path (paint promotes the file to v11; erasing downgrades it again
	        // and the reloaded level carries no plane). The byte-level format
	        // pins live in test_decor_format; this pins the editor plumbing.
	        eds().current_floor = 0;
	        check(data.set_decor(4, 4, DECOR_TORCH2));
	        check(data.saveLevel());
	        check(data.reloadLevel());
	        check(data.get_decor(4, 4) == DECOR_TORCH2);
	        check(data.get_decor(5, 4) == DECOR_NONE);
	        check(data.set_decor(4, 4, DECOR_NONE));
	        check(data.saveLevel());
	        check(data.reloadLevel());
	        check(!data.level->world().decor.valid());
	        check(data.get_decor(4, 4) == DECOR_NONE);

	        open_file_campaign_menu();
        click_button(data.fileCampaignSaveButton);
        check(!eds().campaignchanged);

        eds().levelchanged = 1;
        eds().campaignchanged = 1;
        picker_testing_yes_or_no_queue_push(true);
        picker_testing_yes_or_no_queue_push(true);
        expose_hidden_file_campaign_choice(data.fileCampaignImportButton);
        click_button(data.fileCampaignImportButton);

        eds().levelchanged = 1;
        eds().campaignchanged = 1;
        picker_testing_yes_or_no_queue_push(true);
        picker_testing_yes_or_no_queue_push(true);
        expose_hidden_file_campaign_choice(data.fileCampaignShareButton);
        click_button(data.fileCampaignShareButton);
        check(!eds().levelchanged && !eds().campaignchanged);

        open_file_level_menu();
        check(!data.current_menu.empty());
        eds().levelchanged = 1;
        picker_testing_yes_or_no_queue_push(true);
        open_file_level_menu();
        click_button(data.fileLevelNewButton);
        check(eds().levelchanged);
        open_file_level_menu();
        click_button(data.fileLevelSaveButton);
        check(!eds().levelchanged);

        open_file_menu();
        bool quit_done = click_button(data.fileQuitButton, true);
        check(quit_done && data.current_menu.empty());

        open_file_menu();
        bool off_menu_done = false;
        data.mouse_up(220, 170, 220, 170, off_menu_done);
        check(!off_menu_done && data.current_menu.empty());

        data.dragging = true;
        bool drag_done = false;
        data.mouse_up(220, 170, 220, 170, drag_done);
        check(!drag_done && !data.dragging && data.current_menu.empty());

        mount_campaign_only(previously_mounted_campaign);

        open_campaign_menu();
        click_button(data.campaignInfoButton);
        open_campaign_menu();
        click_button(data.campaignButton);
        check(data.current_menu.empty());

        level_editor_testing_prompt_queue_clear();
        level_editor_testing_prompt_queue_push("Coverage Campaign");
        open_campaign_profile_menu();
        click_button(data.campaignProfileTitleButton);
        check(data.campaign->title == "Coverage Campaign");

        level_editor_testing_prompt_queue_push("Coverage Campaign Description");
        open_campaign_profile_menu();
        click_button(data.campaignProfileDescriptionButton);
        check(!data.campaign->description.empty());

        level_editor_testing_prompt_queue_push("Coverage Authors");
        open_campaign_profile_menu();
        click_button(data.campaignProfileAuthorsButton);
        check(data.campaign->authors == "Coverage Authors");

        level_editor_testing_prompt_queue_push("Coverage Contributors");
        open_campaign_profile_menu();
        click_button(data.campaignProfileContributorsButton);
        check(data.campaign->contributors == "Coverage Contributors");

        level_editor_testing_prompt_queue_push("9.9");
        open_campaign_details_menu();
        click_button(data.campaignDetailsVersionButton);
        check(data.campaign->version == "9.9");

        level_editor_testing_prompt_queue_push("42");
        open_campaign_details_menu();
        click_button(data.campaignDetailsSuggestedPowerButton);
        check(data.campaign->suggested_power == 42);

        level_editor_testing_prompt_queue_push("1");
        open_campaign_details_menu();
        click_button(data.campaignDetailsFirstLevelButton);
        check(data.campaign->first_level == 1);

        open_campaign_menu();
        click_button(data.campaignValidateButton);

        open_level_menu();
        click_button(data.levelInfoButton);

        level_editor_testing_prompt_queue_push("Coverage Level");
        open_level_profile_menu();
        click_button(data.levelProfileTitleButton);
        check(data.level->world().title == "Coverage Level");

        level_editor_testing_prompt_queue_push("Coverage Level Description");
        open_level_profile_menu();
        click_button(data.levelProfileDescriptionButton);
        check(!data.level->description.empty());

        level_editor_testing_prompt_queue_push("77");
        open_level_details_menu();
        click_button(data.levelDetailsParValueButton);
        check(data.level->world().par_value == 77);

        level_editor_testing_prompt_queue_push("88");
        open_level_details_menu();
        click_button(data.levelDetailsTimeLimitButton);
        check(data.level->world().time_bonus_limit == 88);

        level_editor_testing_prompt_queue_push("2");
        level_editor_testing_prompt_queue_push("2");
        open_level_details_menu();
        click_button(data.levelDetailsMapSizeButton);
        level_editor_testing_prompt_queue_push("6");
        level_editor_testing_prompt_queue_push("6");
        open_level_details_menu();
        click_button(data.levelDetailsMapSizeButton);
        check(data.level->world().grid.w == 6 && data.level->world().grid.h == 6);

        const short type_before = data.level->world().type;
        open_level_goals_menu();
        click_button(data.levelGoalsEnemiesButton);
        click_button(data.levelGoalsGeneratorsButton);
        click_button(data.levelGoalsNPCsButton);
        check(data.level->world().type != type_before);

        open_level_menu();
        click_button(data.levelResmoothButton);
        picker_testing_yes_or_no_queue_push(true);
        open_level_menu();
        click_button(data.levelDeleteTerrainButton);
        picker_testing_yes_or_no_queue_push(true);
        open_level_menu();
        click_button(data.levelDeleteObjectsButton);
        check(data.level->world().oblist.empty());

        open_mode_menu();
        click_button(data.modeObjectButton);
        check(data.mode == Mode::Object);
        open_mode_menu();
        click_button(data.modeSelectButton);
        check(data.mode == Mode::Select);
        open_mode_menu();
        click_button(data.modeTerrainButton);
        check(data.mode == Mode::Terrain);

        screen* scr = og::runtime::current_session->myscreen_;
        check(scr != nullptr && scr->viewob[0] != nullptr);
        if (scr != nullptr && scr->viewob[0] != nullptr)
        {
            struct FramebufferGuard
            {
                screen& scr;
                std::vector<unsigned char> saved;

                explicit FramebufferGuard(screen& scr_)
                    : scr(scr_), saved(scr_.videobuffer) {}

                ~FramebufferGuard() { scr.videobuffer = std::move(saved); }
            } framebuffer_guard(*scr);
            const auto capture_framebuffer = [&] {
                const int width = scr->canvas_w();
                const int height = scr->canvas_h();
                std::vector<unsigned char> pixels;
                pixels.reserve(static_cast<std::size_t>(width) *
                               static_cast<std::size_t>(height) * 3u);
                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        Uint8 red = 0;
                        Uint8 green = 0;
                        Uint8 blue = 0;
                        scr->get_pixel(x, y, &red, &green, &blue);
                        pixels.push_back(red);
                        pixels.push_back(green);
                        pixels.push_back(blue);
                    }
                }
                return pixels;
            };
            const auto changes_framebuffer = [&](const auto& render) {
                scr->clearbuffer();
                const std::vector<unsigned char> before = capture_framebuffer();
                render();
                return capture_framebuffer() != before;
            };

            SimpleButton centered_button("CENTER", 20, 20, 80, 20);
            centered_button.centered = true;
            check(changes_framebuffer([&] { centered_button.draw(scr); }));

            walker* draw_selected = data.level->add_ob(Order::Living, FAMILY_SOLDIER);
            check(draw_selected != nullptr);
            if (draw_selected != nullptr)
            {
                draw_selected->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
                draw_selected->set_team_num(0);
                draw_selected->stats()->name = "Drawn";
                data.selection.clear();
                data.selection.push_back(SelectionInfo(draw_selected));
                data.mode = Mode::Select;
                data.reset_mode_buttons();
                data.rect_selecting = true;
                data.selection_rect = Rectf(40.0f, 40.0f, -20.0f, -20.0f);
                check(changes_framebuffer([&] { data.draw(scr); }));
                data.rect_selecting = false;
                check(changes_framebuffer([&] { data.display_panel(scr); }));
                check(!data.selection.empty());
            }

            data.selection.clear();
            for (int idx = 0; idx < 7; ++idx)
            {
                walker* multi = data.level->add_ob(Order::Living, FAMILY_SOLDIER);
                if (multi != nullptr)
                {
                    multi->setxy(GRID_SIZE * (idx + 1), GRID_SIZE * 2);
                    multi->stats()->name = std::format("Sel{}", idx);
                    data.selection.push_back(SelectionInfo(multi));
                }
            }
            data.mode = Mode::Select;
            data.reset_mode_buttons();
            check(changes_framebuffer([&] { data.display_panel(scr); }));
            data.selection.clear();

            data.mode = Mode::Object;
            data.reset_mode_buttons();
            const auto gore_overrides_before = cfg.overrides;
            cfg.apply_override("effects", "gore", "on");
            data.pan_buttons.insert(&data.panLeftButton);
            data.pan_buttons.insert(&data.panRightButton);
            data.pan_buttons.insert(&data.panUpButton);
            data.pan_buttons.insert(&data.panDownButton);
            MouseState& panel_mouse = query_mouse_no_poll();
            panel_mouse.left = false;
            panel_mouse.right = false;
            panel_mouse.x = 120;
            panel_mouse.y = 90;
            data.object_brush.worldz = GRID_SIZE / 2;
            check(changes_framebuffer([&] { data.display_panel(scr); }));
            data.object_brush.worldz = 0;
            data.object_brush.snap_to_grid = false;
            check(changes_framebuffer([&] { data.display_panel(scr); }));
            cfg.overrides = gore_overrides_before;
            data.mode = Mode::Terrain;
            data.reset_mode_buttons();
            data.terrain_brush.terrain = -1;
            data.terrain_brush.decor = DECOR_MAX;
            check(changes_framebuffer([&] { data.display_panel(scr); }));
            check(data.terrain_brush.terrain == PIX_GRASS1);
            check(data.terrain_brush.decor == DECOR_NONE);
            check(data.mode == Mode::Terrain);

            data.add_floor();
            check(data.level->world().is_multifloor());
            check(changes_framebuffer([&] { data.display_panel(scr); }));
            data.remove_floor();
            check(!data.level->world().is_multifloor());

            // Decor mode: draw the decor picker pane + brush preview (sprite
            // when the decor art is loaded, label fallback otherwise) for
            // both a normal id and the eraser.
            eds().decor_mode = true;
            data.reset_mode_buttons();
            data.terrain_brush.decor = DECOR_TORCH1;
            check(changes_framebuffer([&] { data.display_panel(scr); }));
            data.terrain_brush.decor = DECOR_NONE;
            check(changes_framebuffer([&] { data.display_panel(scr); }));
            eds().decor_mode = false;
            data.reset_mode_buttons();
            check(data.mode == Mode::Terrain);
        }
    }
    check(mount_guard.restore());
    check(get_mounted_campaign() == mount_guard.initially_mounted());
    delete_campaign(helper_campaign_id);
    check(get_mounted_campaign() == mount_guard.initially_mounted());

    return failed_check == 0 ? 0 : -failed_check;
}

// End-to-end round-trip on MIGRATED stock content: load a gladiator level
// whose decor plane was produced by tools/grid_migrate (scen1 carries exactly
// 20 DECOR_BOULDER_1 cells over contextual grass bases), repaint one decor
// cell through the editor brush plumbing, save through the editor's
// unpack/save_level/repack path, reload, and verify. All edits land in an
// isolated COPY campaign: saveCampaignAs never switches the PhysFS mount, so
// the copy is mounted explicitly before the first save — otherwise saveLevel
// would repack the user's gladiator shadow and contaminate every later test.
int level_editor_test_decor_migrated_roundtrip()
{
    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
            failed_check = check_index;
    };
    LevelEditorTestingStateGuard editor_state_guard;
    const std::string copy_id = "org.openglad.coverage.decor_migrated";
    LevelEditorTestingMountGuard mount_guard;
    const auto mount_campaign_only = [&](const std::string& campaign_id) {
        const bool success = mount_guard.switch_to(campaign_id);
        check(success);
        return success;
    };

    mount_campaign_only("");
    restore_default_campaigns();
    mount_campaign_only("org.openglad.gladiator");
    delete_campaign(copy_id);

    {
        LevelEditorData data;
        check(data.level != nullptr);
        if (data.level != nullptr)
        {
            check(data.loadCampaign("org.openglad.gladiator"));
            check(data.loadLevel(1));
            eds().current_floor = 0;
            eds().decor_mode = false;

            // Migrated package pins: scen1's 20 legacy PIX_BOULDER_1 tiles
            // became DECOR_BOULDER_1 cells, the first at tile (10, 0).
            check(data.level->world().decor.valid());
            const auto count_decor = [&data](unsigned char id) {
                const PixieData& g = data.level->world().grid;
                int n = 0;
                for (int y = 0; y < g.h; ++y)
                    for (int x = 0; x < g.w; ++x)
                        if (data.get_decor(x, y) == id)
                            ++n;
                return n;
            };
            check(count_decor(DECOR_BOULDER_1) == 20);
            check(data.get_decor(10, 0) == DECOR_BOULDER_1);
            // The base under the boulder is a mapping product (the boulder
            // contextual-base whitelist), never the legacy combined byte.
            const unsigned char base = data.get_terrain(10, 0);
            check(base == PIX_GRASS2 || base == PIX_GRASS3 ||
                  base == PIX_GRASS_DARK_1 || base == PIX_GRASS_LIGHT_1 ||
                  base == PIX_DIRT_1 || base == PIX_DIRT_DARK_1 ||
                  base == PIX_SNOW1 || base == PIX_ASH1);

            // Isolate, then switch the mount to the copy (see header note).
            check(data.saveCampaignAs(copy_id));
            mount_campaign_only(copy_id);
            check(get_mounted_campaign() == copy_id);

            // Repaint one decor cell, then round-trip the editor save path.
            check(data.set_decor(10, 0, DECOR_BOULDER_2));
            check(data.saveLevel());
            check(data.reloadLevel());
            check(data.get_decor(10, 0) == DECOR_BOULDER_2);
            check(data.get_terrain(10, 0) == base);     // base grid untouched
            check(count_decor(DECOR_BOULDER_1) == 19);  // the rest survived
            check(count_decor(DECOR_BOULDER_2) == 1);
            check(data.get_decor(4, 6) == DECOR_BOULDER_1);

            // Erase the repainted cell: the other 19 keep the file at v11,
            // and the erase itself round-trips.
            check(data.set_decor(10, 0, DECOR_NONE));
            check(data.saveLevel());
            check(data.reloadLevel());
            check(data.get_decor(10, 0) == DECOR_NONE);
            check(count_decor(DECOR_BOULDER_1) == 19);
            check(data.level->world().decor.valid());
        }
    }

    check(mount_guard.restore());
    check(get_mounted_campaign() == mount_guard.initially_mounted());
    delete_campaign(copy_id);
    check(get_mounted_campaign() == mount_guard.initially_mounted());

    return failed_check == 0 ? 0 : -failed_check;
}
