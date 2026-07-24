void results_screen_testing_set_force_full(bool enabled)
{
    s_force_full_results_ui = enabled;
    s_results_ui_loop_live.store(false);
    s_results_ui_frames.store(0);
}

bool results_screen_testing_loop_live()
{
    return s_results_ui_loop_live.load();
}

int results_screen_testing_frame_count()
{
    return s_results_ui_frames.load();
}

int results_screen_test_exercise_internal()
{
    // Exercise the core computation helpers in this TU without entering any
    // interactive UI loops (safe for CI).
    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
            failed_check = check_index;
    };

    // walker/pixie store raw pointers to PixieData buffers; keep the data alive
    // for the duration of this helper.
    auto one_px_buf = std::make_unique<unsigned char[]>(1);
    one_px_buf[0] = 0;
    PixieData one_px(1, 1, 1, one_px_buf.release());

    screen* const scr = og::runtime::current_session->myscreen_;
    SaveData& save = scr->save_data;
    struct SharedStateGuard
    {
        SaveData& save;
        short scen_num = save.scen_num;
        std::string current_campaign = save.current_campaign;
        std::map<std::string, int> current_levels = save.current_levels;
        std::map<std::string, std::set<int>> completed_levels =
            save.completed_levels;
        std::vector<unsigned char> framebuffer =
            og::runtime::current_session->myscreen_->videobuffer;

        ~SharedStateGuard()
        {
            save.scen_num = scen_num;
            save.current_campaign = std::move(current_campaign);
            save.current_levels = std::move(current_levels);
            save.completed_levels = std::move(completed_levels);
            og::runtime::current_session->myscreen_->videobuffer =
                std::move(framebuffer);
        }
    } shared_state{save};
    const auto capture_guy_region = [&] {
        std::vector<unsigned char> pixels;
        pixels.reserve(80u * 80u * 3u);
        for (int y = 60; y < 140; ++y)
        {
            for (int x = 120; x < 200; ++x)
            {
                Uint8 r = 0;
                Uint8 g = 0;
                Uint8 b = 0;
                scr->get_pixel(x, y, &r, &g, &b);
                pixels.push_back(r);
                pixels.push_back(g);
                pixels.push_back(b);
            }
        }
        return pixels;
    };

    // Ending popup branches. Assert the title and body captured by the dialog
    // trace, rather than merely rechecking state arranged by this helper.
    save.scen_num = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels.clear();
    save.completed_levels.clear();

    trace_clear();
    show_ending_popup(1, -1); // generic defeat
    check(trace_count("popup") == 1 &&
          trace_contains("popup", "Defeat!: YOUR MEN ARE CRUSHED!"));

    trace_clear();
    show_ending_popup(1, 2);  // retreat
    const std::string retreat_popup = std::format(
        "Retreat!: Retreating to\n{}\n(You may take this field later)",
        og::data::scenario_display_name(2));
    check(trace_count("popup") == 1 &&
          trace_contains("popup", retreat_popup.c_str()));

    trace_clear();
    show_ending_popup(SCEN_TYPE_SAVE_ALL, -1);
    check(trace_count("popup") == 1 &&
          trace_contains("popup",
              "Defeat!: YOU ARE DEFEATED!\nYOU FAILED TO KEEP YOUR ALLY ALIVE"));

    trace_clear();
    show_ending_popup(0, 2);  // victory not completed
    check(trace_count("popup") == 1 &&
          trace_contains("popup", "Victory!: You have won the battle!"));

    save.completed_levels[save.current_campaign].insert(save.scen_num);
    trace_clear();
    show_ending_popup(0, 2);  // victory completed
    const std::string traveling_popup = std::format(
        "Traveling on...: Moving on to\n{}",
        og::data::scenario_display_name(2));
    check(trace_count("popup") == 1 &&
          trace_contains("popup", traveling_popup.c_str()));

    // TroopResult logic paths.
    TroopResult empty_result(nullptr, nullptr);
    check(empty_result.get_name().empty() &&
        empty_result.get_class_name().empty() &&
        empty_result.get_level() == 0 &&
        empty_result.get_tallies() == 0 &&
        empty_result.get_HP() == 0.0f &&
        empty_result.get_XP_base() == 0.0f &&
        empty_result.is_dead() &&
        !empty_result.is_new());
    scr->clearbuffer();
    const std::vector<unsigned char> empty_draw_before = capture_guy_region();
    empty_result.draw_guy(160, 100, 0);
    check(capture_guy_region() == empty_draw_before);
    show_guy(0, nullptr, 160, 100);
    check(capture_guy_region() == empty_draw_before);

    guy before{};
    before.name = "Before";
    before.family = FAMILY_MAGE;
    before.level = 3;
    before.exp = calculate_exp(before.level) + 10;

    // after with null myguy should be ignored by ctor.
    walker after_null(one_px);
    after_null.clear_myguy();
    TroopResult t0(&before, &after_null);
    check(t0.get_name() == "Before");
    check(!t0.get_class_name().empty());
    check(t0.get_XP_base() > 0.0f &&
        t0.get_XP_gain() == 0.0f &&
        t0.get_gained_specials().empty() &&
        !t0.is_new());

    // after with owned myguy to cover gained/lost/no-change XP branches and HP paths.
    walker after_owned(one_px);
    auto owned = std::make_unique<guy>(FAMILY_MAGE);
    owned->name = "After";
    owned->family = FAMILY_MAGE;
    owned->level = 4;
    owned->exp = calculate_exp(owned->level) + 5; // gained level vs before.level=3
    owned->scen_min_hp = 1;
    after_owned.set_owned_myguy(std::move(owned));
    after_owned.stats()->set_max_hitpoints(10);
    after_owned.stats()->set_hitpoints(5);

    TroopResult t1(&before, &after_owned);
    check(t1.gained_level());
    check(t1.get_XP_base() == 0.0f &&
        t1.get_XP_gain() > 0.0f &&
        t1.get_HP() > 0.0f &&
        t1.get_HP() < 1.0f &&
        t1.get_tallies() == 0);
    const std::vector<std::string> gained_specials = t1.get_gained_specials();
    check(!gained_specials.empty() &&
        std::all_of(gained_specials.begin(), gained_specials.end(),
                    [](const std::string& name) { return !name.empty(); }));

    // lost level branch
    before.level = 6;
    before.exp = calculate_exp(before.level);
    after_owned.myguy->exp = calculate_exp(3); // lost
    TroopResult t2(&before, &after_owned);
    check(t2.lost_level());
    check(t2.get_XP_gain() < 0.0f);

    // unchanged level branch
    before.level = 3;
    before.exp = calculate_exp(before.level) + 20;
    after_owned.myguy->exp = calculate_exp(before.level) + 25;
    TroopResult t3(&before, &after_owned);
    check(!t3.gained_level() && !t3.lost_level() && t3.get_XP_gain() > 0.0f);

    // HP edge: scen_min_hp > current hitpoints returns 1.0
    after_owned.myguy->scen_min_hp = 1000;
    after_owned.stats()->set_hitpoints(1);
    TroopResult t4(nullptr, &after_owned);
    check(t4.get_HP() >= 0.99f);
    check(t4.get_name() == "After" &&
        !t4.get_class_name().empty() &&
        t4.get_level() == calculate_level(after_owned.myguy->exp) &&
        t4.is_new());
    scr->clearbuffer();
    const std::vector<unsigned char> live_draw_before = capture_guy_region();
    t4.draw_guy(160, 100, 0);
    check(capture_guy_region() != live_draw_before);

    // Draw helper should no-op when dead.
    after_owned.myguy->scen_min_hp = 0;
    after_owned.stats()->set_hitpoints(0);
    TroopResult t5(nullptr, &after_owned);
    check(t5.is_dead());
    const std::vector<unsigned char> dead_draw_before = capture_guy_region();
    t5.draw_guy(160, 100, 0);
    check(capture_guy_region() == dead_draw_before);

    trace_clear();
    return failed_check == 0 ? 0 : -failed_check;
}
