namespace {
// The family the open DETAILS page shows, -1 while none is open (an
// injector reads it on the menu thread).
int g_detail_menu_testing_family = -1;
int g_detail_menu_testing_page = -1;
int g_detail_menu_testing_pages = -1;
}  // namespace

int detail_menu_testing_family()
{
    return g_detail_menu_testing_family;
}

int detail_menu_testing_page()
{
    return g_detail_menu_testing_page;
}

int detail_menu_testing_pages()
{
    return g_detail_menu_testing_pages;
}
