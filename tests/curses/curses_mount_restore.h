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
//
// restore() is idempotent and reports whether the mount really came back, so
// a test that wants to assert the restore (rather than leave it to the
// destructor) can call it in the body and read the answer.
class MountRestore
{
public:
    MountRestore()
        : mounted_before_(get_mounted_campaign())
    {
    }

    ~MountRestore()
    {
        (void)restore();
    }

    bool restore()
    {
        if (restored_)
            return restore_ok_;

        const std::string mounted_after =
            get_mounted_campaign();
        if (mounted_after != mounted_before_) {
            const CampaignPackageIoError result =
                mounted_before_.empty()
                    ? unmount_campaign_package_with_error(
                          mounted_after)
                    : mount_campaign_package_with_error(
                          mounted_before_);
            restore_ok_ =
                result == CampaignPackageIoError::None;
        }
        restore_ok_ =
            get_mounted_campaign() == mounted_before_ &&
            restore_ok_;
        restored_ = true;
        return restore_ok_;
    }

    const std::string& mounted_before() const
    {
        return mounted_before_;
    }

    MountRestore(const MountRestore&) = delete;
    MountRestore& operator=(const MountRestore&) = delete;

private:
    std::string mounted_before_;
    bool restored_ = false;
    bool restore_ok_ = true;
};
