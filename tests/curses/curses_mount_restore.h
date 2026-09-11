/* Shared campaign-mount guard for the ncurses test suite.
 *
 * The mounted campaign package is process-global state, and a test that hosts
 * or loads a non-default campaign leaves that package mounted for every test
 * that runs after it. The damage is not local: the next test that builds a
 * host lobby snapshots the leaked package into its pack announcement, the
 * in-process joiner installs it into the virtual tree for the rest of the
 * process, and from then on every campaign that registers a book collides
 * with the installed one ("one campaign, one book: no scripted picker will be
 * served").
 *
 * curses_test_main.cpp's listener records the mount around every test and
 * fails the binary naming any test that changed it. A test that legitimately
 * mounts something declares this guard as its FIRST statement; the guard runs
 * after the test body (and after any assertion the test makes about the mount
 * it established), so it never hides what the test wanted to prove.
 */
#pragma once

#include <openglad/resources/io_common.h>

#include <string>

// Restore the process campaign mount exactly (the .inc's pattern), so
// shuffled neighbors see their original package after a test that hosts a
// non-default campaign.
struct MountRestore {
    std::string before = get_mounted_campaign();
    ~MountRestore()
    {
        const std::string after = get_mounted_campaign();
        if (after == before)
            return;
        if (before.empty())
            (void)unmount_campaign_package_with_error(after);
        else
            (void)mount_campaign_package_with_error(before);
    }
};
