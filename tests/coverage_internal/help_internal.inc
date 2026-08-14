void help_testing_set_page_state(bool page_up, bool page_down)
{
	s_help_test_page_up.store(page_up, std::memory_order_release);
	s_help_test_page_down.store(page_down, std::memory_order_release);
}

// #156: held-key seams for the new line/jump scroll keys (SDL's keyboard
// state array cannot be written from pushed events).
void help_testing_set_line_key_state(bool line_up, bool line_down)
{
	s_help_test_line_up.store(line_up, std::memory_order_release);
	s_help_test_line_down.store(line_down, std::memory_order_release);
}

void help_testing_set_jump_key_state(bool home_down, bool end_down)
{
	s_help_test_home.store(home_down, std::memory_order_release);
	s_help_test_end.store(end_down, std::memory_order_release);
}

// #156: scroll-state probes for the injector thread — the current scroll
// offset in scanlines, and whether the viewer computed scroll chrome.
Sint32 help_testing_query_linesdown()
{
	return s_help_last_linesdown.load(std::memory_order_acquire);
}

bool help_testing_query_scroll_chrome()
{
	return s_help_scroll_chrome.load(std::memory_order_acquire);
}

// #168: the full-screen help flows the Controls table to the frame's
// full-width budget (help_char_budget(kHelpTextBoxWidth) = 50 chars). Every
// authored line already fits that budget, so the flow must not wrap anything
// (-1 if it did); report the widest flowed line for the budget pin.
Sint32 help_testing_controls_flowed_max_width()
{
	std::list<std::string> source;
	for (int i = 0; i < NUM_CONTROLS_LINES; i++)
		source.push_back(controls_help_lines[i]);
	const std::vector<std::string> flowed =
		og::core::wrap_lines(source, help_char_budget(kHelpTextBoxWidth));
	if (flowed.size() != static_cast<std::size_t>(NUM_CONTROLS_LINES))
		return -1;
	std::size_t max_len = 0;
	for (const std::string& line : flowed)
	{
		if (line.size() > max_len)
			max_len = line.size();
	}
	return static_cast<Sint32>(max_len);
}

// #168: injector probes for the full-screen help screen — the active tab and
// the active tab's current page, published by the engine hooks.
Sint32 help_testing_query_active_tab()
{
	return s_help_active_tab.load(std::memory_order_acquire);
}

Sint32 help_testing_query_help_page()
{
	return s_help_page.load(std::memory_order_acquire);
}

// #168: engine-hook exerciser (exact-check-count pattern): drives the help
// spec hooks through the installed-state seam without a blocking loop.
// Returns 0, or the negated index of the first failed check.
Sint32 help_testing_exercise_menu_hooks()
{
	int check_index = 0;
	int failed_check = 0;
	const auto check = [&check_index, &failed_check](bool condition) {
		++check_index;
		if (!condition && failed_check == 0)
			failed_check = check_index;
	};

	load_help_files();
	og::ui::MenuLabelContext context;
	std::vector<button> rows;
	og::ui::materialize_menu_buttons(og::ui::help_menu_screen_spec(), rows);
	check(static_cast<int>(rows.size()) == kHelpMenuNextIndex + 1);
	int highlighted = kHelpMenuBackIndex;

	// Null state (bare engine-sweep shape): pagers hidden, BACK's right-link
	// closed, dispatch and the wheel tick no-op.
	g_help_screen_state = nullptr;
	check(help_engine_pager_row_state(context) == og::ui::RowState::Hidden);
	help_engine_rewire(rows.data(), static_cast<int>(rows.size()),
	                   highlighted);
	check(rows[kHelpMenuBackIndex].nav.right == -1);
	check(help_engine_on_spec_row(kHelpMenuClassesTabIndex, nullptr) == 0);
	check(help_engine_frame_tick(nullptr, 0));

	// Installed state: Controls flows multi-page at the 50-char budget.
	HelpScreenState state;
	help_switch_tab(state, HelpTab::Controls, true);
	g_help_screen_state = &state;
	check(state.pager.multi_page());
	check(help_engine_pager_row_state(context) == og::ui::RowState::Visible);
	help_engine_rewire(rows.data(), static_cast<int>(rows.size()),
	                   highlighted);
	check(rows[kHelpMenuBackIndex].nav.right == kHelpMenuPrevIndex);

	// NEXT/PREV dispatch steps the pager (clamped at the first page).
	check(help_engine_on_spec_row(kHelpMenuNextIndex, nullptr) == 0);
	check(state.pager.page == 1);
	check(help_engine_on_spec_row(kHelpMenuPrevIndex, nullptr) == 0);
	check(state.pager.page == 0);
	check(help_engine_on_spec_row(kHelpMenuPrevIndex, nullptr) == 0);
	check(state.pager.page == 0);

	// Re-selecting the active tab keeps the page; switching tabs reflows
	// and resets the pager; an unknown row no-ops.
	check(help_engine_on_spec_row(kHelpMenuNextIndex, nullptr) == 0);
	check(help_engine_on_spec_row(kHelpMenuControlsTabIndex, nullptr) == 0);
	check(state.tab == HelpTab::Controls && state.pager.page == 1);
	check(help_engine_on_spec_row(kHelpMenuClassesTabIndex, nullptr) == 0);
	check(state.tab == HelpTab::Classes && state.pager.page == 0);
	check(help_testing_query_active_tab() ==
	      static_cast<int>(HelpTab::Classes));
	check(help_engine_on_spec_row(kHelpMenuEditorTabIndex, nullptr) == 0);
	check(state.tab == HelpTab::Editor && state.pager.page == 0);
	check(help_engine_on_spec_row(999, nullptr) == 0);
	check(state.tab == HelpTab::Editor && state.pager.page == 0);

	// Wheel: a negative accumulated delta pages down, positive pages back.
	input_scroll_amount_ref() = -3;
	check(help_engine_frame_tick(nullptr, 1));
	check(state.pager.page == 1);
	check(help_testing_query_help_page() == 1);
	input_scroll_amount_ref() = 2;
	check(help_engine_frame_tick(nullptr, 2));
	check(state.pager.page == 0);
	check(help_engine_frame_tick(nullptr, 3));
	check(state.pager.page == 0);

	g_help_screen_state = nullptr;
	return failed_check == 0 ? 0 : -failed_check;
}

Sint32 help_testing_exercise_internal_paths()
{
	struct HelpCacheGuard
	{
		bool files_loaded = help_files_loaded;
		std::vector<std::string> classes = classes_help_lines;
		std::vector<std::string> editor = editor_help_lines;

		~HelpCacheGuard()
		{
			help_files_loaded = files_loaded;
			classes_help_lines = std::move(classes);
			editor_help_lines = std::move(editor);
		}
	} cache_guard;

	int check_index = 0;
	int failed_check = 0;
	const auto check = [&check_index, &failed_check](bool condition) {
		++check_index;
		if (!condition && failed_check == 0)
			failed_check = check_index;
	};

	std::vector<std::string> lines;
	check(!load_help_file("__missing_help_file__.txt", lines) &&
	      !lines.empty());

	// Parse an actual user-file through the production loader.  This pins all
	// three line-ending shapes: lone CR (requiring one-byte pushback), CRLF, and
	// a final unterminated line that must not be lost at EOF.
	constexpr const char* line_endings_file =
		"help_internal_line_endings.txt";
	(void)remove_user_file(line_endings_file);
	{
		auto outfile = og::io::og_open_write(line_endings_file);
		check(outfile != nullptr);
		if (outfile)
		{
			constexpr char contents[] = "one\rtwo\r\nthree";
			check(og::io::og_write_exact(
				*outfile, contents, 1, sizeof(contents) - 1));
		}
	}
	lines.clear();
	check(load_help_file(line_endings_file, lines));
	check(lines.size() == 3);
	if (lines.size() == 3)
	{
		check(lines[0] == "one");
		check(lines[1] == "two");
		check(lines[2] == "three");
	}
	check(remove_user_file(line_endings_file));

	help_files_loaded = false;
	load_help_files();
	check(help_files_loaded);
	load_help_files();

	classes_help_lines = {"Class line 1", "Class line 2"};
	editor_help_lines = {"Editor line 1"};
	const TabContent controls = get_tab_content(HelpTab::Controls);
	const TabContent classes = get_tab_content(HelpTab::Classes);
	const TabContent editor = get_tab_content(HelpTab::Editor);
	const TabContent fallback =
		get_tab_content(static_cast<HelpTab>(99));
	check(!controls.is_dynamic && controls.num_lines == NUM_CONTROLS_LINES);
	check(classes.is_dynamic && classes.num_lines == 2);
	check(editor.is_dynamic && editor.num_lines == 1);
	check(!fallback.is_dynamic && fallback.num_lines == NUM_CONTROLS_LINES);
	check(std::strcmp(get_content_line(controls, 0),
	                  controls_help_lines[0]) == 0);
	check(std::strcmp(get_content_line(classes, 1), "Class line 2") == 0);
	check(std::strcmp(get_content_line(editor, 0), "Editor line 1") == 0);
	check(std::strcmp(get_content_line(editor, 3), "") == 0);
	check(std::strcmp(get_content_line(editor, -1), "") == 0);

	screen* const scr = og::runtime::current_session->myscreen_;
	const std::string saved_campaign = scr->save_data.current_campaign;
	scr->save_data.current_campaign = "__missing_campaign__";
	check(read_campaign_intro(scr) == 1);
	scr->save_data.current_campaign = saved_campaign;
	const auto saved_description = scr->level_description();
	scr->level_description().clear();
	check(read_scenario(scr) == 1);
	scr->level_description() = saved_description;

	return failed_check == 0 ? 0 : -failed_check;
}
