#include <gtest/gtest.h>

#include <openglad/core/frame_rate_config.h>
#include <openglad/resources/gparser.h>

#include <cstdint>

namespace {

class FrameRateConfigTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        saved_value_ = cfg.get_setting("graphics", "target_fps");
        had_value_ = !saved_value_.empty();
        cfg.data["graphics"].erase("target_fps");
    }

    void TearDown() override
    {
        if (had_value_)
            cfg.apply_setting("graphics", "target_fps", saved_value_);
        else
            cfg.data["graphics"].erase("target_fps");
    }

    std::string saved_value_;
    bool had_value_ = false;
};

TEST_F(FrameRateConfigTest, DefaultsWhenMissing)
{
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kDefaultTargetFps);
}

TEST_F(FrameRateConfigTest, DefaultsWhenUnparseable)
{
    cfg.apply_setting("graphics", "target_fps", "not-a-number");
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kDefaultTargetFps);
}

TEST_F(FrameRateConfigTest, ClampsLow)
{
    cfg.apply_setting("graphics", "target_fps", "1");
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kMinTargetFps);
}

TEST_F(FrameRateConfigTest, ClampsHigh)
{
    cfg.apply_setting("graphics", "target_fps", "9999");
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kMaxTargetFps);
}

TEST_F(FrameRateConfigTest, AcceptsExactBounds)
{
    cfg.apply_setting("graphics", "target_fps",
                      std::to_string(og::core::kMinTargetFps));
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kMinTargetFps);

    cfg.apply_setting("graphics", "target_fps",
                      std::to_string(og::core::kMaxTargetFps));
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kMaxTargetFps);
}

TEST_F(FrameRateConfigTest, AcceptsTypicalValue)
{
    cfg.apply_setting("graphics", "target_fps", "120");
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), 120);
}

TEST_F(FrameRateConfigTest, ApplyWritesClampedValue)
{
    og::core::apply_target_fps_to_cfg(cfg, 9999);
    EXPECT_EQ(cfg.get_setting("graphics", "target_fps"),
              std::to_string(og::core::kMaxTargetFps));

    og::core::apply_target_fps_to_cfg(cfg, 1);
    EXPECT_EQ(cfg.get_setting("graphics", "target_fps"),
              std::to_string(og::core::kMinTargetFps));

    og::core::apply_target_fps_to_cfg(cfg, 90);
    EXPECT_EQ(cfg.get_setting("graphics", "target_fps"), "90");
}

TEST_F(FrameRateConfigTest, RoundTrip)
{
    og::core::apply_target_fps_to_cfg(cfg, 144);
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), 144);

    og::core::apply_target_fps_to_cfg(cfg, og::core::kDefaultTargetFps);
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kDefaultTargetFps);
}

TEST_F(FrameRateConfigTest, UncappedSentinelParses)
{
    cfg.apply_setting("graphics", "target_fps", "0");
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kUncappedTargetFps);

    cfg.apply_setting("graphics", "target_fps", "-3");
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kUncappedTargetFps);

    // Adjacent positive values still clamp up to the minimum cap.
    cfg.apply_setting("graphics", "target_fps", "1");
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kMinTargetFps);
}

TEST_F(FrameRateConfigTest, ApplyPreservesUncappedSentinel)
{
    og::core::apply_target_fps_to_cfg(cfg, 0);
    EXPECT_EQ(cfg.get_setting("graphics", "target_fps"), "0");
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kUncappedTargetFps);

    og::core::apply_target_fps_to_cfg(cfg, -5);
    EXPECT_EQ(cfg.get_setting("graphics", "target_fps"), "0");
    EXPECT_EQ(og::core::target_fps_from_cfg(cfg), og::core::kUncappedTargetFps);
}

TEST(FrameRateConfigInterval, RoundsHalfUp)
{
    EXPECT_EQ(og::core::target_frame_interval_ms(72), 14u);
    EXPECT_EQ(og::core::target_frame_interval_ms(60), 17u);
    EXPECT_EQ(og::core::target_frame_interval_ms(30), 33u);
    EXPECT_EQ(og::core::target_frame_interval_ms(120), 8u);
    EXPECT_EQ(og::core::target_frame_interval_ms(144), 7u);
}

TEST(FrameRateConfigInterval, ClampsToAtLeastOne)
{
    EXPECT_EQ(og::core::target_frame_interval_ms(1000), 1u);
    EXPECT_EQ(og::core::target_frame_interval_ms(2000), 1u);
    EXPECT_EQ(og::core::target_frame_interval_ms(0), 1u);
    EXPECT_EQ(og::core::target_frame_interval_ms(-5), 1u);
}

} // namespace
