void picker_testing_set_force_changelog(bool force)
{
    og::ui::g_force_changelog = force;
}

bool picker_testing_changelog_forced()
{
    return og::ui::g_force_changelog;
}
