void screen::testing_reset_window_state()
{
    if (video_impl_ != nullptr)
        video_impl_->testing_reset_window_state();
}

void screen::testing_report_fade_violation(const char* what)
{
    if (video_impl_ != nullptr)
        video_impl_->testing_report_fade_violation(what);
}
