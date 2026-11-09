void sdl_video::testing_reset_window_state()
{
    if (E_Screen != nullptr)
        E_Screen->testing_reset_window_state();
}

void sdl_video::testing_report_fade_violation(const char* what)
{
    og::video_testing::report_fade_violation(what);
}
