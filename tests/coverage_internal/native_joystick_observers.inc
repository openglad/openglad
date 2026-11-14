namespace
{
// Bit i = "device i enumerates but its node will not open" (the udev
// permission case). See set_joystick_open_failure_mask.
unsigned int g_joystick_open_failure_mask = 0;
}

void set_joystick_open_failure_mask(unsigned int mask)
{
    g_joystick_open_failure_mask = mask;
}
