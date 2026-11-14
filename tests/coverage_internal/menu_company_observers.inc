void set_main_menu_company_view_for_tests(bool present, std::string display_name)
{
    g_main_menu_company_view.present = present;
    g_main_menu_company_view.display_name = std::move(display_name);
}
