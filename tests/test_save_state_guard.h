#pragma once

#include <openglad/resources/io_common.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <utility>

namespace og::test
{
class ScopedPhysicalFileState
{
public:
    explicit ScopedPhysicalFileState(std::filesystem::path path)
        : path_(std::move(path))
        , backup_path_(path_.string() + ".openglad-test-backup")
    {
        std::error_code error;
        existed_ = std::filesystem::exists(path_, error);
        if (error)
        {
            error_ = error;
            return;
        }

        std::filesystem::remove(backup_path_, error);
        if (error)
        {
            error_ = error;
            return;
        }

        if (existed_)
        {
            std::filesystem::copy_file(
                path_, backup_path_,
                std::filesystem::copy_options::overwrite_existing, error);
            if (error)
            {
                error_ = error;
                return;
            }
        }
        ready_ = true;
    }

    ~ScopedPhysicalFileState()
    {
        if (!ready_)
            return;

        std::error_code error;
        if (existed_)
        {
            std::filesystem::copy_file(
                backup_path_, path_,
                std::filesystem::copy_options::overwrite_existing, error);
        }
        else
        {
            std::filesystem::remove(path_, error);
        }
        if (error)
        {
            ADD_FAILURE() << "test file-state restore failed for "
                          << path_ << ": " << error.message();
            return;
        }

        error.clear();
        std::filesystem::remove(backup_path_, error);
        if (error)
        {
            ADD_FAILURE() << "test backup cleanup failed for "
                          << backup_path_ << ": " << error.message();
        }
    }

    ScopedPhysicalFileState(const ScopedPhysicalFileState&) = delete;
    ScopedPhysicalFileState& operator=(const ScopedPhysicalFileState&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::error_code& error() const noexcept
    {
        return error_;
    }

private:
    std::filesystem::path path_;
    std::filesystem::path backup_path_;
    bool existed_ = false;
    bool ready_ = false;
    std::error_code error_;
};

class ScopedCampaignMountState
{
public:
    ScopedCampaignMountState() : mounted_before_(get_mounted_campaign()) {}

    ~ScopedCampaignMountState()
    {
        const std::string mounted_after = get_mounted_campaign();
        if (mounted_after == mounted_before_)
            return;

        CampaignPackageIoError error = CampaignPackageIoError::None;
        if (mounted_before_.empty())
        {
            if (!mounted_after.empty())
                error = unmount_campaign_package_with_error(mounted_after);
        }
        else
        {
            error = mount_campaign_package_with_error(mounted_before_);
        }
        if (error != CampaignPackageIoError::None)
        {
            ADD_FAILURE() << "test campaign mount restore failed for "
                          << mounted_before_;
        }
    }

    ScopedCampaignMountState(const ScopedCampaignMountState&) = delete;
    ScopedCampaignMountState& operator=(const ScopedCampaignMountState&) =
        delete;

private:
    std::string mounted_before_;
};
} // namespace og::test
