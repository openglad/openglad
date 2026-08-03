#include <SDL3/SDL.h>

#include <atomic>

void level_picker_testing_input_reset();
void level_picker_testing_click(int x, int y);
std::uint64_t level_picker_testing_entered_count();
std::uint64_t level_picker_testing_action_count();
std::vector<std::string>& level_editor_testing_prompt_queue_ref();
void picker_testing_yes_or_no_queue_clear();
int picker_testing_yes_or_no_queue_remaining();

namespace
{
bool level_editor_wait_for_counter(std::uint64_t (*counter)(),
                                   std::uint64_t baseline)
{
    const std::uint64_t deadline =
        static_cast<std::uint64_t>(og::input_native::ticks_ms()) + 5000u;
    while (counter() <= baseline)
    {
        if (static_cast<std::uint64_t>(og::input_native::ticks_ms()) >= deadline)
        {
            level_picker_testing_click(-1, -1);
            return false;
        }
        og::input_native::sleep_ms(1);
    }
    return true;
}

bool level_editor_click_picker_action(int x, int y)
{
    const std::uint64_t action_before =
        level_picker_testing_action_count();
    level_picker_testing_click(x, y);
    return level_editor_wait_for_counter(
        level_picker_testing_action_count, action_before);
}

struct LevelEditorDialogDismissState
{
    std::atomic<bool> stop{false};
};

bool level_editor_push_dialog_dismissal_event()
{
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.key = SDLK_SPACE;
    down.key.down = true;
    if (!SDL_PushEvent(&down))
        return false;

    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    return SDL_PushEvent(&up);
}

int level_editor_stream_dialog_dismissal_events(
    LevelEditorDialogDismissState& state)
{
    // timed_dialog() clears pre-existing input. Keep producing real key events
    // until the main thread reports that the blocking action returned, so a
    // scheduler pause cannot let the complete train arrive before that clear.
    // The generous deadline remains a harness fail-safe; timed_dialog itself
    // has a three-second product timeout once entered.
    const std::uint64_t deadline =
        static_cast<std::uint64_t>(og::input_native::ticks_ms()) + 30000u;
    while (!state.stop.load(std::memory_order_acquire))
    {
        if (static_cast<std::uint64_t>(og::input_native::ticks_ms()) >= deadline)
            return 1;
        if (!level_editor_push_dialog_dismissal_event())
            return 1;
        og::input_native::sleep_ms(5);
    }
    return 0;
}

void level_editor_clear_dialog_dismissal_events()
{
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
    clear_keyboard();
    clear_key_press_event();
    clear_text_input_event();
}

int level_editor_picker_driver(void* data)
{
    auto* state = static_cast<LevelEditorDialogDismissState*>(data);
    og::runtime::ensure_thread_session();
    bool ok = level_editor_wait_for_counter(
        level_picker_testing_entered_count, 0);
    if (ok)
        ok = level_editor_click_picker_action(225, 15);
    if (!ok || state == nullptr)
        return 1;
    return level_editor_stream_dialog_dismissal_events(*state);
}

int level_editor_dialog_dismiss_driver(void* data)
{
    auto* state = static_cast<LevelEditorDialogDismissState*>(data);
    og::runtime::ensure_thread_session();
    if (state == nullptr)
        return 1;
    return level_editor_stream_dialog_dismissal_events(*state);
}

class LevelEditorTestingThread
{
public:
    LevelEditorTestingThread(
        SDL_Thread* thread, LevelEditorDialogDismissState& state)
        : thread_(thread), state_(state) {}

    ~LevelEditorTestingThread()
    {
        (void)stop_and_join();
    }

    bool valid() const { return thread_ != nullptr; }

    int stop_and_join()
    {
        state_.stop.store(true, std::memory_order_release);
        if (thread_ == nullptr)
            return result_;
        SDL_WaitThread(thread_, &result_);
        thread_ = nullptr;
        return result_;
    }

private:
    SDL_Thread* thread_;
    LevelEditorDialogDismissState& state_;
    int result_ = 1;
};

class LevelEditorTestingStateGuard
{
public:
    LevelEditorTestingStateGuard()
        : state_(eds()), saved_state_(state_), mouse_(query_mouse_no_poll()),
          saved_mouse_(mouse_)
    {
        trace_clear();
    }

    ~LevelEditorTestingStateGuard()
    {
        state_ = std::move(saved_state_);
        mouse_ = saved_mouse_;
        trace_clear();
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

class LevelEditorTestingKeyGuard
{
public:
    LevelEditorTestingKeyGuard()
        : session_(*og::runtime::current_session),
          saved_keystates_(session_.keystates_)
    {
        keys_.fill(false);
        session_.keystates_ = keys_.data();
    }

    ~LevelEditorTestingKeyGuard()
    {
        session_.keystates_ = saved_keystates_;
    }

    void set(int key, bool down)
    {
        if (key >= 0 && key < static_cast<int>(keys_.size()))
            keys_[static_cast<std::size_t>(key)] = down;
    }

private:
    og::runtime::SessionState& session_;
    const bool* saved_keystates_;
    std::array<bool, SDL_SCANCODE_COUNT> keys_{};
};

class LevelEditorTestingPromptGuard
{
public:
    LevelEditorTestingPromptGuard()
        : queue_(level_editor_testing_prompt_queue_ref()), saved_(queue_)
    {
        queue_.clear();
    }

    ~LevelEditorTestingPromptGuard()
    {
        queue_ = std::move(saved_);
    }

private:
    std::vector<std::string>& queue_;
    std::vector<std::string> saved_;
};

class LevelEditorTestingYesNoGuard
{
public:
    LevelEditorTestingYesNoGuard()
    {
        picker_testing_yes_or_no_queue_clear();
    }

    ~LevelEditorTestingYesNoGuard()
    {
        picker_testing_yes_or_no_queue_clear();
    }
};

class LevelEditorTestingMouseEventGuard
{
public:
    LevelEditorTestingMouseEventGuard()
    {
        SDL_FlushEvents(
            SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP);
    }

    ~LevelEditorTestingMouseEventGuard()
    {
        SDL_FlushEvents(
            SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP);
    }
};
} // namespace

int level_editor_test_mouse_release_workflows()
{
    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
        {
            failed_check = check_index;
            Log("level-editor internal check {} failed\n", check_index);
        }
    };

    LevelEditorTestingStateGuard editor_state_guard;
    LevelEditorTestingMountGuard mount_guard;
    LevelEditorTestingKeyGuard key_guard;
    LevelEditorTestingPromptGuard prompt_guard;
    LevelEditorTestingMouseEventGuard mouse_event_guard;

    check(mount_guard.switch_to("org.openglad.gladiator"));
    LevelEditorData data;
    check(data.loadCampaign("org.openglad.gladiator"));
    check(data.loadLevel(1));
    data.level->delete_objects();
    data.level->create_new_grid();
    data.level->set_draw_pos(0, 0);
    eds().current_floor = 0;
    eds().rowsdown = 0;
    eds().decor_mode = false;
    backgrounds().assign(
        kDefaultBackgrounds.begin(), kDefaultBackgrounds.end());
    object_pane().clear();
    object_pane().push_back({Order::Living, FAMILY_SOLDIER});

    screen* scr = og::runtime::current_session->myscreen_;
    check(scr != nullptr && scr->viewob[0] != nullptr);
    if (scr != nullptr && scr->viewob[0] != nullptr)
    {
        MouseState& mouse = query_mouse_no_poll();
        data.myradar.start(data.level.get());
        const auto queue_left_release = [&mouse](int x, int y) {
            mouse.x = static_cast<float>(x);
            mouse.y = static_cast<float>(y);
            mouse.left = true;
            SDL_Event release{};
            release.type = SDL_EVENT_MOUSE_BUTTON_UP;
            release.button.button = SDL_BUTTON_LEFT;
            release.button.down = false;
            release.button.x = static_cast<float>(x);
            release.button.y = static_cast<float>(y);
            const bool queued = SDL_PushEvent(&release);
            if (!queued)
                mouse.left = false;
            return queued;
        };

        // The menu helpers wait for a genuine release before acting. Queue
        // that release and assert each helper's distinct menu contract.
        SimpleButton release_button("Release", 180, 150, 40, 12);
        std::list<std::pair<SimpleButton*, std::set<SimpleButton*>>>
            release_menu;
        const bool submenu_release_queued = queue_left_release(181, 151);
        check(submenu_release_queued);
        if (submenu_release_queued)
        {
            check(activate_sub_menu_button(
                181, 151, release_menu, release_button, true));
            check(!mouse.left);
        }

        data.current_menu.push_back(
            std::make_pair(
                &release_button, std::set<SimpleButton*>{&release_button}));
        const bool choice_release_queued = queue_left_release(181, 151);
        check(choice_release_queued);
        if (choice_release_queued)
        {
            check(activate_menu_choice(
                181, 151, data, release_button, true));
            check(!mouse.left);
            check(data.current_menu.empty());
        }
        else
            data.current_menu.clear();

        data.current_menu.push_back(
            std::make_pair(
                &release_button, std::set<SimpleButton*>{&release_button}));
        const bool toggle_release_queued = queue_left_release(181, 151);
        check(toggle_release_queued);
        if (toggle_release_queued)
        {
            check(activate_menu_toggle_choice(
                181, 151, data, release_button, true));
            check(!mouse.left);
            check(data.current_menu.size() == 1);
        }
        data.current_menu.clear();

        const int world_x = GRID_SIZE * 8;
        const int world_y = GRID_SIZE * 8;
        const int screen_x =
            world_x - data.level->level_visuals().topx + scr->viewob[0]->xloc;
        const int screen_y =
            world_y - data.level->level_visuals().topy + scr->viewob[0]->yloc;
        bool done = false;

        walker* target =
            data.level->add_ob(Order::Living, FAMILY_ORC);
        check(target != nullptr);
        if (target != nullptr)
        {
            target->setxy(world_x, world_y);
            target->set_floor(0);
            target->set_team_num(3);
            target->stats()->set_level(4);
            target->stats()->name = "Before release rename";
            const std::size_t original_count =
                data.level->world().oblist.size();

            // The R+click workflow renames the exact collided object and
            // removes its temporary collision probe.
            level_editor_testing_prompt_queue_push("After release rename");
            key_guard.set(KEYSTATE_r, true);
            eds().levelchanged = 0;
            data.mode = Mode::Select;
            data.object_brush.snap_to_grid = true;
            data.mouse_up(
                screen_x, screen_y, screen_x, screen_y, done);
            key_guard.set(KEYSTATE_r, false);
            check(data.level->world().oblist.size() == original_count);
            const bool target_present_after_rename = std::any_of(
                data.level->world().oblist.begin(),
                data.level->world().oblist.end(),
                [target](const auto& candidate) {
                    return candidate.get() == target;
                });
            check(target_present_after_rename);
            if (target_present_after_rename)
            {
                check(target->stats()->name == "After release rename");
                check(eds().levelchanged == 1);
                if (!data.level->world().oblist.empty())
                    check(data.level->world().oblist.front().get() == target);

                // Ctrl+click toggles the same object by identity/geometry:
                // selected -> absent -> selected, without touching the world.
                data.selection.clear();
                data.selection.push_back(SelectionInfo(target));
                key_guard.set(KEYSTATE_LCTRL, true);
                data.mouse_up(
                    screen_x, screen_y, screen_x, screen_y, done);
                check(data.selection.empty());
                check(data.level->world().oblist.size() == original_count);
                data.mouse_up(
                    screen_x, screen_y, screen_x, screen_y, done);
                check(data.selection.size() == 1);
                if (!data.selection.empty())
                {
                    check(data.selection.front().target == target);
                    check(data.selection.front().valid);
                }
                key_guard.set(KEYSTATE_LCTRL, false);

                // A snapped placement colliding with the authored object is
                // rejected atomically: no duplicate remains in the level.
                data.mode = Mode::Object;
                data.object_brush.order = Order::Living;
                data.object_brush.family = FAMILY_SOLDIER;
                data.object_brush.team = 2;
                data.object_brush.level = 5;
                data.object_brush.worldz = GRID_SIZE / 2;
                data.object_brush.snap_to_grid = true;
                data.object_brush.picking = false;
                eds().levelchanged = 0;
                data.mouse_up(
                    screen_x, screen_y, screen_x, screen_y, done);
                check(data.level->world().oblist.size() == original_count);
                const bool target_present_after_collision = std::any_of(
                    data.level->world().oblist.begin(),
                    data.level->world().oblist.end(),
                    [target](const auto& candidate) {
                        return candidate.get() == target;
                    });
                check(target_present_after_collision);
                if (target_present_after_collision)
                {
                    if (!data.level->world().oblist.empty())
                        check(data.level->world().oblist.front().get() == target);
                    check(target->family() == FAMILY_ORC);
                    check(target->stats()->name == "After release rename");
                    check(eds().levelchanged == 1);

                    // Pick mode consumes itself and copies every authored brush
                    // field from the collided object.
                    data.object_brush.picking = true;
                    data.pickerButton.set_colors_active();
                    data.mouse_up(
                        screen_x, screen_y, screen_x, screen_y, done);
                    check(!data.object_brush.picking);
                    check(data.object_brush.order == Order::Living);
                    check(data.object_brush.family == FAMILY_ORC);
                    check(data.object_brush.team == 3);
                    check(data.object_brush.level == 4);
                    check(data.level->world().oblist.size() == original_count);
                    if (!data.level->world().oblist.empty())
                        check(
                            data.level->world().oblist.front().get() == target);
                }
            }
        }

        // Out-of-grid writes and reads are strict no-ops/fallbacks.
        const unsigned char tile_before = data.get_terrain(0, 0);
        data.set_terrain(-1, -1, PIX_WATER1);
        check(data.get_terrain(0, 0) == tile_before);
        check(data.get_decor(-1, -1) == DECOR_NONE);

        // A stale decor stride is never read or written. The writer drops it
        // and creates a correctly-sized plane; the explicit cleanup path
        // likewise frees a stale plane.
        GameWorld& world = data.level->world();
        PixieData& decor = world.decor_for_floor(0);
        decor = PixieData(
            1, 1, 1, new unsigned char[1]{DECOR_TORCH1});
        check(data.get_decor(0, 0) == DECOR_NONE);
        check(data.set_decor(0, 0, DECOR_TORCH2));
        check(decor.valid());
        check(decor.w == world.grid.w && decor.h == world.grid.h);
        check(data.get_decor(0, 0) == DECOR_TORCH2);
        decor = PixieData(
            1, 1, 1, new unsigned char[1]{DECOR_BRAZIER});
        data.drop_mismatched_decor_planes();
        check(!decor.valid());

        // The cursor suppression test uses the real started radar bounds in
        // both paint modes and leaves the authored object list unchanged.
        mouse.x = static_cast<float>(
            scr->viewob[0]->endx - data.myradar.xview - 3);
        mouse.y = static_cast<float>(
            scr->viewob[0]->endy - data.myradar.yview - 3);
        const int radar_x = static_cast<int>(mouse.x);
        const int radar_y = static_cast<int>(mouse.y);
        const Sint32 draw_x_before_radar =
            data.level->level_visuals().topx;
        const Sint32 draw_y_before_radar =
            data.level->level_visuals().topy;
        bool radar_done = false;
        data.mode = Mode::Terrain;
        data.mouse_up(
            radar_x, radar_y, radar_x, radar_y, radar_done);
        check(!radar_done);
        check(data.level->level_visuals().topx ==
              draw_x_before_radar);
        check(data.level->level_visuals().topy ==
              draw_y_before_radar);
        const std::size_t before_panel =
            data.level->world().oblist.size();
        check(data.display_panel(scr) == 1);
        check(data.level->world().oblist.size() == before_panel);
        data.mode = Mode::Object;
        data.object_brush.order = Order::Living;
        data.object_brush.family = FAMILY_SOLDIER;
        check(data.display_panel(scr) == 1);
        check(data.level->world().oblist.size() == before_panel);
    }

    check(level_editor_testing_prompt_queue_ref().empty());
    check(mount_guard.restore());
    check(get_mounted_campaign() == mount_guard.initially_mounted());
    return failed_check == 0 ? 0 : -failed_check;
}

int level_editor_test_exercise_internal_helpers()
{
    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
        {
            failed_check = check_index;
            Log("level-editor internal check {} failed\n", check_index);
        }
    };
    LevelEditorTestingStateGuard editor_state_guard;
    LevelEditorTestingPromptGuard prompt_guard;
    LevelEditorTestingYesNoGuard yes_no_guard;
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

	            // "Delay" prompt: authors the per-object wave timer in sim
	            // ticks. It must never dormant-ize while editing — a dormant
	            // walker is skipped by the obmap update, so it would stop
	            // being selectable and draggable in the editor.
	            check(data.mode_buttons.find(&data.delayButton) != data.mode_buttons.end());
	            level_editor_testing_prompt_queue_clear();
	            level_editor_testing_prompt_queue_push("900");
	            data.activate_mode_button(&data.delayButton);
	            check(selected->spawn_delay() == 900 && !selected->dormant());
	            // Out-of-range input clamps into the uint16 field either way.
	            level_editor_testing_prompt_queue_push("70000");
	            data.activate_mode_button(&data.delayButton);
	            check(selected->spawn_delay() == 65535);
	            level_editor_testing_prompt_queue_push("-5");
	            data.activate_mode_button(&data.delayButton);
	            check(selected->spawn_delay() == 0);
	            level_editor_testing_prompt_queue_push("360");
	            data.activate_mode_button(&data.delayButton);
	            check(selected->spawn_delay() == 360);
	            // 0 clears the delay again: the par-value prompt's `v > 0`
	            // guard would have made it unclearable.
	            level_editor_testing_prompt_queue_push("0");
	            data.activate_mode_button(&data.delayButton);
	            check(selected->spawn_delay() == 0 && !selected->dormant());
	            check(level_editor_testing_prompt_queue_ref().empty());
	            // A weapon selection neither offers the button nor takes a
	            // delay (a delay there would upgrade the saved file to v10
	            // for a field the loader ignores).
	            walker* delay_weap = data.level->add_weap_ob(Order::Weapon, FAMILY_KNIFE);
	            check(delay_weap != nullptr);
	            if (delay_weap != nullptr)
	            {
	                delay_weap->setxy(GRID_SIZE * 5, GRID_SIZE * 5);
	                data.selection.clear();
	                data.selection.push_back(SelectionInfo(delay_weap));
	                data.reset_mode_buttons();
	                check(data.mode_buttons.find(&data.delayButton) == data.mode_buttons.end());
	                level_editor_testing_prompt_queue_push("450");
	                data.activate_mode_button(&data.delayButton);
	                check(delay_weap->spawn_delay() == 0);
	                data.level->remove_ob(delay_weap);
	                data.selection.clear();
	                data.selection.push_back(SelectionInfo(selected));
	                data.reset_mode_buttons();
	            }
	            // Object mode edits the brush instead, and the brush stamps
	            // placed Livings/Generators (weapon brushes are left alone).
	            data.mode = Mode::Object;
	            data.object_brush.order = Order::Living;
	            data.object_brush.family = FAMILY_SOLDIER;
	            data.reset_mode_buttons();
	            check(data.mode_buttons.find(&data.delayButton) != data.mode_buttons.end());
	            level_editor_testing_prompt_queue_push("240");
	            data.activate_mode_button(&data.delayButton);
	            check(data.object_brush.spawn_delay == 240);
	            data.object_brush.order = Order::Treasure;
	            data.object_brush.family = FAMILY_GOLD_BAR;
	            data.reset_mode_buttons();
	            check(data.mode_buttons.find(&data.delayButton) == data.mode_buttons.end());
	            data.object_brush.order = Order::Living;
	            data.object_brush.family = FAMILY_SOLDIER;
	            data.object_brush.spawn_delay = 0;
	            data.mode = Mode::Select;
	            data.reset_mode_buttons();

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

        // Build an isolated two-step sequential chain of blank scenarios.
        // Level 1 falls through to 2; level 2 has no successor and reports
        // the terminal diagnostic.
        check(data.saveLevelAs(2));
        check(data.saveLevelAs(3));
        const std::list<int> helper_levels = list_levels();
        check(helper_levels ==
              std::list<int>({1, 2, 3}));

        std::set<int> sequential_connected;
        std::list<std::string> sequential_problems;
        get_connected_level_exits(
            1, std::list<int>({1, 2}), sequential_connected,
            sequential_problems);
        check(sequential_connected == std::set<int>({1, 2}));
        check(sequential_problems ==
              std::list<std::string>({"Level 2 has no exits."}));

	        const auto drive_level_picker = [&](SimpleButton& choice) {
	                level_picker_testing_input_reset();
	                LevelEditorDialogDismissState dismiss_state;
	                LevelEditorTestingThread driver(
	                    SDL_CreateThread(
	                        level_editor_picker_driver,
	                        "level_editor_picker_driver", &dismiss_state),
	                    dismiss_state);
	                check(driver.valid());
	                if (!driver.valid())
	                    return false;

	                open_file_level_menu();
	                click_button(choice);

	                const int thread_result = driver.stop_and_join();
	                level_editor_clear_dialog_dismissal_events();
	                check(thread_result == 0);
                return thread_result == 0;
            };

        // Save As through the actual level picker and ENTER ID. The new ID
        // must be persisted, become current, and appear exactly once.
        level_editor_testing_prompt_queue_push("7");
        eds().levelchanged = 1;
        check(drive_level_picker(data.fileLevelSaveAsButton));
        check(data.level->world().id == 7);
        check(!eds().levelchanged);
        {
            const std::list<int> levels_after_save_as = list_levels();
            check(std::count(
                levels_after_save_as.begin(), levels_after_save_as.end(), 7) ==
                1);
        }

        // Load an exact listed scenario through the real ENTER ID flow.
        level_editor_testing_prompt_queue_push("1");
        eds().levelchanged = 0;
        const bool loaded_first =
            drive_level_picker(data.fileLevelLoadButton);
        check(loaded_first);
        check(data.level->world().id == 1);
        check(!eds().levelchanged);
        check(data.level->world().grid.w == 40);
        check(data.level->world().grid.h == 60);

        // An explicit missing positive ID exercises the failed-load outcome:
        // the requested ID remains visible, authored dirty state is retained,
        // and a direct valid reload restores the exact scenario.
        level_editor_testing_prompt_queue_push("9999");
        eds().levelchanged = 1;
        picker_testing_yes_or_no_queue_push(true);
        check(drive_level_picker(data.fileLevelLoadButton));
        check(data.level->world().id == 9999);
        check(eds().levelchanged == 1);
        check(data.loadLevel(1));
        check(data.level->world().id == 1);
        check(data.level->world().grid.w == 40);
        check(data.level->world().grid.h == 60);

        // Re-entering an existing campaign ID and declining overwrite must
        // leave the mounted package, editor model, and all level files exact.
        const std::list<int> levels_before_declined_overwrite = list_levels();
        level_editor_testing_prompt_queue_push(helper_campaign_id.c_str());
        eds().levelchanged = 0;
        check(picker_testing_yes_or_no_queue_remaining() == 0);
        open_file_campaign_menu();
        click_button(data.fileCampaignNewButton);
        check(data.campaign->id == helper_campaign_id);
        check(get_mounted_campaign() == helper_campaign_id);
        check(data.level->world().id == 1);
        check(list_levels() == levels_before_declined_overwrite);
        check(picker_testing_yes_or_no_queue_remaining() == 0);

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

        // The legacy icon command remains hidden from the normal submenu, but
        // its action must still report the exact unsupported-operation popup.
        data.current_menu.clear();
        data.current_menu.push_back(std::make_pair(
            &data.campaignProfileButton,
            std::set<SimpleButton*>{&data.campaignProfileIconButton}));
        trace_clear();
        click_button(data.campaignProfileIconButton);
        check(trace_count("popup") == 1);
        check(trace_contains(
            "popup", "Edit Icon: Not yet implemented."));

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

        // An invalid start leaves every real scenario disconnected.
        // The popup must retain the first six concrete diagnostics and report
        // the exact number truncated, while leaving campaign data untouched.
        const std::list<int> validation_levels = list_levels();
        check(validation_levels.size() > 6);
        if (validation_levels.size() > 6)
        {
            const int valid_first_level = data.campaign->first_level;
            data.campaign->first_level = 9999;
            trace_clear();
            open_campaign_menu();
            click_button(data.campaignValidateButton);
            check(trace_count("popup") == 1);
            check(trace_contains(
                "popup",
                "Validate Campaign: Level 9999 failed to load."));
            check(trace_contains(
                "popup",
                std::format(
                    "Level {} is not connected.",
                    validation_levels.front()).c_str()));
            check(trace_contains(
                "popup",
                std::format(
                    "{} more problems...",
                    static_cast<int>(validation_levels.size()) - 5).c_str()));
            check(data.campaign->first_level == 9999);
            check(list_levels() == validation_levels);
            data.campaign->first_level = valid_first_level;
        }

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

        // Both byte-limit diagnostics are emitted together and the grid is
        // byte-for-byte untouched by a rejected oversized request.
        const std::vector<unsigned char> grid_before_oversize(
            data.level->world().grid.data.get(),
            data.level->world().grid.data.get() +
                data.level->world().grid.w * data.level->world().grid.h);
        trace_clear();
        level_editor_testing_prompt_queue_push("256");
        level_editor_testing_prompt_queue_push("300");
        open_level_details_menu();
        click_button(data.levelDetailsMapSizeButton);
        check(data.level->world().grid.w == 6 &&
              data.level->world().grid.h == 6);
        check(std::equal(
            grid_before_oversize.begin(), grid_before_oversize.end(),
            data.level->world().grid.data.get()));
        check(trace_contains("popup", "Width is too big (max 255)."));
        check(trace_contains("popup", "Height is too big (max 255)."));

        // Declining deletion of an out-of-bounds object makes a shrink fully
        // atomic: dimensions, bytes, and object identity all survive.
        walker* outside_resize =
            data.level->add_ob(Order::Living, FAMILY_ORC);
        check(outside_resize != nullptr);
        if (outside_resize != nullptr)
        {
            outside_resize->setxy(GRID_SIZE * 5, GRID_SIZE * 5);
            const std::size_t object_count_before_resize =
                data.level->world().oblist.size();
            check(picker_testing_yes_or_no_queue_remaining() == 0);
            picker_testing_yes_or_no_queue_push(false);
            level_editor_testing_prompt_queue_push("3");
            level_editor_testing_prompt_queue_push("3");
            LevelEditorDialogDismissState dismiss_state;
            LevelEditorTestingThread dismiss_resize(
                SDL_CreateThread(
                    level_editor_dialog_dismiss_driver,
                    "level_editor_dialog_dismiss", &dismiss_state),
                dismiss_state);
            check(dismiss_resize.valid());
            if (dismiss_resize.valid())
            {
                open_level_details_menu();
                click_button(data.levelDetailsMapSizeButton);
                const int dismiss_result = dismiss_resize.stop_and_join();
                level_editor_clear_dialog_dismissal_events();
                check(dismiss_result == 0);
            }
            else
            {
                picker_testing_yes_or_no_queue_clear();
                level_editor_testing_prompt_queue_ref().clear();
            }
            check(picker_testing_yes_or_no_queue_remaining() == 0);
            check(data.level->world().grid.w == 6 &&
                  data.level->world().grid.h == 6);
            check(std::equal(
                grid_before_oversize.begin(), grid_before_oversize.end(),
                data.level->world().grid.data.get()));
            check(data.level->world().oblist.size() ==
                  object_count_before_resize);
            check(std::any_of(
                data.level->world().oblist.begin(),
                data.level->world().oblist.end(),
                [outside_resize](const auto& candidate) {
                    return candidate.get() == outside_resize;
                }));
            data.level->remove_ob(outside_resize);
        }

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
        check(level_editor_testing_prompt_queue_ref().empty());
        check(picker_testing_yes_or_no_queue_remaining() == 0);
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
