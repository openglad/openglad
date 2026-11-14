UpperFloorShadowMaskStats upper_floor_shadow_mask_stats_for_testing()
{
	return upper_floor_shadow_mask_stats;
}

void effects_reset_for_testing()
{
	frame_tick = 0;
	wall_clock_cadence = false;
	cadence_pacer = og::core::FrameDeadlinePacer{};
	cloud_noise_ready = false;
	depth_noise_ready = false;
	glow_kernel_ready = false;
	outdoor_memo_valid = false;
	render_store.clear();
	floor_track_store.clear();
	fall_cues.clear();
	floor_change_store.clear();
	fall_track_ran = false;
	fall_track_tick = 0;
}

std::size_t effects_store_size()
{
	return render_store.size();
}

std::size_t effects_store_depth(std::uint32_t entity_id)
{
	const auto it = render_store.find(entity_id);
	return it == render_store.end() ? 0 : it->second.count;
}

std::uint32_t effects_fall_cue_frames_left(std::uint32_t entity_id)
{
	const auto it = fall_cues.find(entity_id);
	if (it == fall_cues.end())
		return 0;
	const std::uint32_t age = frame_tick - it->second.start_tick;
	return age >= kFallCueFrames ? 0 : kFallCueFrames - age;
}
