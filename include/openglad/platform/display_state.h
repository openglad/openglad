#pragma once

#include <cstdint>

namespace og::platform
{

// SDL3 exposes requested fullscreen flags/modes while an asynchronous window
// operation is pending. Keep a separately confirmed snapshot so a finite
// SDL_SyncWindow timeout cannot make settings claim an unapplied mode.
enum class DisplayStateMode
{
	Windowed,
	Borderless,
	Exclusive
};

struct DisplayStateSnapshot
{
	DisplayStateMode mode = DisplayStateMode::Windowed;
	// Logical window coordinates for Windowed/Borderless; physical pixels for
	// Exclusive, matching the values persisted in graphics/width + height.
	int width = 640;
	int height = 400;
	// Last confirmed logical Windowed restore, also part of the snapshot: an
	// unconfirmed fullscreen request must not silently rewrite it.
	int windowed_width = 640;
	int windowed_height = 400;

	DisplayStateSnapshot() = default;
	DisplayStateSnapshot(DisplayStateMode state_mode, int state_width, int state_height)
		: mode(state_mode), width(state_width), height(state_height),
		  windowed_width(state_width), windowed_height(state_height)
	{
	}
	DisplayStateSnapshot(DisplayStateMode state_mode, int state_width, int state_height,
	                     int restore_width, int restore_height)
		: mode(state_mode), width(state_width), height(state_height),
		  windowed_width(restore_width), windowed_height(restore_height)
	{
	}

	friend bool operator==(const DisplayStateSnapshot&, const DisplayStateSnapshot&) = default;
};

class DisplayStateTracker
{
public:
	explicit DisplayStateTracker(DisplayStateSnapshot initial = {})
		: confirmed_(initial), target_(initial.mode)
	{
	}

	const DisplayStateSnapshot& confirmed() const { return confirmed_; }
	DisplayStateMode target() const { return target_; }
	bool request_pending() const { return request_pending_; }

	void begin_request(DisplayStateMode target, std::uint64_t timestamp_ns)
	{
		target_ = target;
		request_pending_ = true;
		request_started_ns_ = timestamp_ns;
	}

	// Synthetic tests use timestamp 0. Real events older than the currently
	// serialized request must not confirm SDL's eager getter for that request.
	bool event_is_current(std::uint64_t timestamp_ns) const
	{
		return timestamp_ns == 0 || request_started_ns_ == 0 ||
		       timestamp_ns >= request_started_ns_;
	}

	// Some compound transitions have a real intermediate state (for example
	// Exclusive -> Borderless before LEAVE). It is truthful to record that
	// state without declaring the final Windowed request complete.
	void confirm(DisplayStateSnapshot state, bool completes_request = true)
	{
		confirmed_ = state;
		if (completes_request)
			request_pending_ = false;
	}

	void cancel_request() { request_pending_ = false; }

private:
	DisplayStateSnapshot confirmed_;
	DisplayStateMode target_;
	bool request_pending_ = false;
	std::uint64_t request_started_ns_ = 0;
};

} // namespace og::platform
