namespace
{
// Optional test-only override for yes_or_no_prompt(). When empty, the function
// falls back to returning the provided default_value.
std::vector<bool> s_yes_or_no_overrides;
bool s_force_real_dialogs = false;
}

void picker_testing_yes_or_no_queue_clear()
{
    s_yes_or_no_overrides.clear();
}

void picker_testing_yes_or_no_queue_push(bool value)
{
    s_yes_or_no_overrides.push_back(value);
}

void picker_testing_set_force_real_dialogs(bool enabled)
{
    s_force_real_dialogs = enabled;
}

int picker_testing_yes_or_no_queue_remaining()
{
    return static_cast<int>(s_yes_or_no_overrides.size());
}
