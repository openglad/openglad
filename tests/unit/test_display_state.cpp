#include <openglad/platform/display_state.h>

#include <gtest/gtest.h>

namespace
{

using og::platform::DisplayStateMode;
using og::platform::DisplayStateSnapshot;
using og::platform::DisplayStateTracker;

TEST(DisplayStateTracker, unconfirmed_requests_never_replace_the_last_truth)
{
	DisplayStateTracker tracker(
		{DisplayStateMode::Borderless, 960, 600});

	tracker.begin_request(DisplayStateMode::Exclusive, 100);
	EXPECT_EQ((DisplayStateSnapshot{DisplayStateMode::Borderless, 960, 600}),
	          tracker.confirmed());
	EXPECT_TRUE(tracker.request_pending());

	// A timeout performs no confirmation. A superseding caller must keep the
	// request serialized rather than copying SDL's eager Exclusive getter.
	EXPECT_EQ(DisplayStateMode::Exclusive, tracker.target());
	EXPECT_FALSE(tracker.event_is_current(99));
	EXPECT_TRUE(tracker.event_is_current(100));
}

TEST(DisplayStateTracker, confirmed_sync_or_event_commits_exact_mode_and_pixels)
{
	DisplayStateTracker tracker(
		{DisplayStateMode::Borderless, 960, 600});
	tracker.begin_request(DisplayStateMode::Exclusive, 100);
	tracker.confirm({DisplayStateMode::Exclusive, 2560, 1440, 960, 600});

	EXPECT_EQ((DisplayStateSnapshot{
	              DisplayStateMode::Exclusive, 2560, 1440, 960, 600}),
	          tracker.confirmed());
	EXPECT_FALSE(tracker.request_pending());

	tracker.begin_request(DisplayStateMode::Exclusive, 200);
	tracker.confirm({DisplayStateMode::Exclusive, 1920, 1080, 960, 600});
	EXPECT_EQ((DisplayStateSnapshot{
	              DisplayStateMode::Exclusive, 1920, 1080, 960, 600}),
	          tracker.confirmed());
}

TEST(DisplayStateTracker, compound_leave_can_confirm_intermediate_without_completing)
{
	DisplayStateTracker tracker(
		{DisplayStateMode::Exclusive, 2560, 1440, 960, 600});
	tracker.begin_request(DisplayStateMode::Windowed, 300);

	tracker.confirm({DisplayStateMode::Borderless, 960, 600}, false);
	EXPECT_EQ((DisplayStateSnapshot{DisplayStateMode::Borderless, 960, 600}),
	          tracker.confirmed());
	EXPECT_TRUE(tracker.request_pending());
	EXPECT_EQ(DisplayStateMode::Windowed, tracker.target());

	tracker.confirm({DisplayStateMode::Windowed, 960, 600});
	EXPECT_EQ((DisplayStateSnapshot{DisplayStateMode::Windowed, 960, 600}),
	          tracker.confirmed());
	EXPECT_FALSE(tracker.request_pending());
}

TEST(DisplayStateTracker, synthetic_zero_timestamp_remains_testable)
{
	DisplayStateTracker tracker;
	tracker.begin_request(DisplayStateMode::Exclusive, 500);
	EXPECT_TRUE(tracker.event_is_current(0));
	tracker.cancel_request();
	EXPECT_FALSE(tracker.request_pending());
}

} // namespace
