std::atomic<int> s_help_active_tab{0};
std::atomic<int> s_help_page{0};

void help_publish_probes(const HelpScreenState& state)
{
	s_help_active_tab.store(static_cast<int>(state.tab),
	                        std::memory_order_release);
	s_help_page.store(state.pager.page, std::memory_order_release);
}
