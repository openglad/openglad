namespace
{
std::size_t surface_row_bytes(const SDL_Surface* surface)
{
	const SDL_PixelFormatDetails* details =
		SDL_GetPixelFormatDetails(surface->format);
	return static_cast<std::size_t>(surface->w) *
		(details != nullptr ? details->bytes_per_pixel : 4u);
}
} // namespace

void Screen::testing_snapshot_presented(SDL_Surface* surface, int x, int y,
                                        int w, int h)
{
	// A degenerate surface (zero-sized or pixel-less — the scaler's
	// fail-closed probes present those) shows nothing worth remembering.
	if (surface == nullptr || surface->pixels == nullptr || surface->w <= 0 ||
	    surface->h <= 0)
		return;
	PresentedSnapshot* slot = nullptr;
	for (PresentedSnapshot& candidate : presented_snapshots_)
	{
		if (candidate.surface == surface)
		{
			slot = &candidate;
			break;
		}
	}
	if (slot == nullptr)
	{
		presented_snapshots_.push_back({surface, nullptr});
		slot = &presented_snapshots_.back();
	}
	if (slot->pixels != nullptr &&
	    (slot->pixels->w != surface->w || slot->pixels->h != surface->h ||
	     slot->pixels->format != surface->format))
	{
		SDL_DestroySurface(slot->pixels);
		slot->pixels = nullptr;
	}
	if (slot->pixels == nullptr)
	{
		slot->pixels = SDL_CreateSurface(surface->w, surface->h, surface->format);
		if (slot->pixels == nullptr)
			return;
		if (slot->pixels->pixels == nullptr)
		{
			SDL_DestroySurface(slot->pixels);
			slot->pixels = nullptr;
			return;
		}
		// A fresh snapshot is black everywhere the first present does not
		// cover: nothing outside its rect has been shown yet.
		memset(slot->pixels->pixels, 0,
		       static_cast<std::size_t>(slot->pixels->pitch) *
		           static_cast<std::size_t>(slot->pixels->h));
	}
	// Clip the declared rect to the surface. The rect is what the present
	// vouches for: the nearest path happens to upload the whole surface,
	// but the SAI/Eagle path refreshes only this rect of its scaled scratch,
	// so callers are held to what they declared.
	const int x0 = std::max(0, x);
	const int y0 = std::max(0, y);
	const int x1 = std::min(surface->w, x + w);
	const int y1 = std::min(surface->h, y + h);
	if (x0 >= x1 || y0 >= y1)
		return;
	// Row copies (the two pitches need not match), not a blit: a blit may
	// rewrite the unused X byte and the comparison below is byte-exact.
	const std::size_t bpp = surface_row_bytes(surface) /
		static_cast<std::size_t>(surface->w);
	const std::size_t span = static_cast<std::size_t>(x1 - x0) * bpp;
	const std::size_t offset_x = static_cast<std::size_t>(x0) * bpp;
	const auto* src = static_cast<const Uint8*>(surface->pixels);
	auto* dst = static_cast<Uint8*>(slot->pixels->pixels);
	for (int row = y0; row < y1; ++row)
	{
		memcpy(dst + static_cast<std::size_t>(row) * static_cast<std::size_t>(slot->pixels->pitch) + offset_x,
		       src + static_cast<std::size_t>(row) * static_cast<std::size_t>(surface->pitch) + offset_x,
		       span);
	}
}

void Screen::testing_forget_presented(SDL_Surface* surface)
{
	for (auto it = presented_snapshots_.begin(); it != presented_snapshots_.end(); ++it)
	{
		if (it->surface != surface)
			continue;
		SDL_DestroySurface(it->pixels);
		presented_snapshots_.erase(it);
		return;
	}
}

bool Screen::testing_render_matches_presented(std::string& detail) const
{
	detail.clear();
	if (render == nullptr)
	{
		detail = "no render surface";
		return false;
	}
	for (const PresentedSnapshot& snapshot : presented_snapshots_)
	{
		if (snapshot.surface != render)
			continue;
		if (snapshot.pixels == nullptr || snapshot.pixels->w != render->w ||
		    snapshot.pixels->h != render->h ||
		    snapshot.pixels->format != render->format)
		{
			detail = "presented at different dimensions";
			return false;
		}
		const std::size_t row_bytes = surface_row_bytes(render);
		const int bpp = static_cast<int>(row_bytes / static_cast<std::size_t>(render->w));
		const auto* presented = static_cast<const Uint8*>(snapshot.pixels->pixels);
		const auto* current = static_cast<const Uint8*>(render->pixels);
		// Bounding box of every differing pixel: names the culprit draw
		// (a whole-frame box is a clear; a button-sized one is a stale
		// widget redraw) without a debugger.
		int min_x = render->w, min_y = render->h, max_x = -1, max_y = -1;
		for (int y = 0; y < render->h; ++y)
		{
			const Uint8* prow = presented + static_cast<std::size_t>(y) * static_cast<std::size_t>(snapshot.pixels->pitch);
			const Uint8* crow = current + static_cast<std::size_t>(y) * static_cast<std::size_t>(render->pitch);
			if (memcmp(prow, crow, row_bytes) == 0)
				continue;
			for (int x = 0; x < render->w; ++x)
			{
				if (memcmp(prow + x * bpp, crow + x * bpp, static_cast<std::size_t>(bpp)) == 0)
					continue;
				min_x = std::min(min_x, x);
				max_x = std::max(max_x, x);
				min_y = std::min(min_y, y);
				max_y = std::max(max_y, y);
			}
		}
		if (max_x < 0)
			return true;
		detail = std::format("render differs from the presented frame in x {}..{}, y {}..{} of {}x{}",
		                     min_x, max_x, min_y, max_y, render->w, render->h);
		return false;
	}
	detail = "never presented";
	return false;
}

void Screen::testing_reset_window_state()
{
	// Black, as a completed exit fade leaves it (and as a process starts):
	// the next fading entry finds the ownership rule's promised state, and
	// a test that presents a frame of its own then owes that frame's
	// fade-out like any other screen.
	window_is_black_ = true;
	for (PresentedSnapshot& snapshot : presented_snapshots_)
		SDL_DestroySurface(snapshot.pixels);
	presented_snapshots_.clear();
	const auto snapshot_whole = [this](SDL_Surface* surface) {
		if (surface != nullptr)
			testing_snapshot_presented(surface, 0, 0, surface->w, surface->h);
	};
	snapshot_whole(ui_surf_);
	if (world_surf_ != ui_surf_)
		snapshot_whole(world_surf_);
	snapshot_whole(gameplay_ui_surf_);
}

SDL_Surface* Screen::native_world_view_surface_for_testing(
	std::size_t index) const
{
	return index < native_world_view_planes_.size()
		? native_world_view_planes_[index].surface : nullptr;
}

void Screen::fail_next_gameplay_ui_allocation_for_testing()
{
	destroy_gameplay_ui_overlay();
	fail_next_gameplay_ui_allocation_ = true;
}

void Screen::fail_next_world_canvas_allocation_for_testing()
{
	fail_next_world_canvas_allocation_ = true;
}

std::size_t Screen::native_world_view_ready_count_for_testing() const
{
	return native_world_view_ready_count_;
}

std::span<const NativeWorldViewDestination> Screen::native_world_view_destinations_for_testing( 	std::size_t index) const
{
	return index < native_world_view_planes_.size()
		? std::span<const NativeWorldViewDestination>(
			native_world_view_planes_[index].destinations)
		: std::span<const NativeWorldViewDestination>{};
}

void Screen::discard_native_world_views_for_testing()
{
	discard_native_world_views();
}

void Screen::fail_next_native_world_view_allocation_for_testing()
{
	fail_next_native_world_view_allocation_ = true;
}

std::size_t Screen::hud_sprite_ready_count_for_testing() const
{
	return hud_sprite_ready_count_;
}

SDL_Surface* Screen::hud_sprite_surface_for_testing(std::size_t index) const
{
	return index < hud_sprite_planes_.size()
		? hud_sprite_planes_[index].surface : nullptr;
}

NativeWorldViewDestination Screen::hud_sprite_destination_for_testing(
			std::size_t index) const
{
	return index < hud_sprite_planes_.size()
		? hud_sprite_planes_[index].destination
		: NativeWorldViewDestination{};
}

void Screen::discard_hud_sprites_for_testing()
{ discard_hud_sprites(); }

void Screen::fail_next_hud_sprite_allocation_for_testing()
{
	fail_next_hud_sprite_allocation_ = true;
}
